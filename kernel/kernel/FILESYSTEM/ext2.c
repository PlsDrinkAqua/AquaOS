#include <stdint.h>
#include <libk/stdio.h>
#include <libk/string.h>

#include "kernel/ata.h"
#include "kernel/ext2.h"
#include "kernel/kmalloc.h"
#include "kernel/vmm.h"

#define SECTOR_SIZE 512U
#define SUPER_SECTOR 2        // Superblock 从 LBA=2 开始
#define SUPER_READ_SECS 2     // 共 2 扇区 = 1024B
#define INODE_SIZE 128

/* 本文件私有 */
static struct ext2_super_block sb;
static struct ext2_group_desc *gbdt;
static uint32_t sb_groups_count;

static uint8_t read_sb(void) {
    static uint8_t buf[SECTOR_SIZE * SUPER_READ_SECS];
    if (!ata_read_sectors(SUPER_SECTOR, SUPER_READ_SECS, buf)) {
        kprintf("ext2: read superblock failed\n");
        return 0;
    }
    kmemcpy(&sb, buf, sizeof(sb));

    if (sb.s_magic != 0xEF53) {
        kprintf("Not an ext2 filesystem! magic=0x%x\n", sb.s_magic);
        return 0;
    }
    return 1;
}

static int write_sb(void) {
    uint8_t buf[SECTOR_SIZE * SUPER_READ_SECS];
    if (ata_read_sectors(SUPER_SECTOR, SUPER_READ_SECS, buf) != SUPER_READ_SECS) return -1;
    kmemcpy(buf, &sb, sizeof(sb));
    return ata_write_sectors(SUPER_SECTOR, SUPER_READ_SECS, buf) == SUPER_READ_SECS ? 0 : -1;
}

static int write_gbdt(void) {
    uint32_t block_size = 1024U << sb.s_log_block_size;
    uint32_t gd_block = block_size == 1024 ? 2 : 1;
    uint32_t bytes = sb_groups_count * sizeof(*gbdt);
    uint32_t sectors = (bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint8_t *buf = kmalloc(sectors * SECTOR_SIZE);
    int result = -1;
    if (!buf) return -1;
    if (ata_read_sectors(gd_block * (block_size / SECTOR_SIZE), sectors, buf) == sectors) {
        kmemcpy(buf, gbdt, bytes);
        if (ata_write_sectors(gd_block * (block_size / SECTOR_SIZE), sectors, buf) == sectors) result = 0;
    }
    kfree(buf);
    return result;
}

/**
 * 从磁盘读取一个 inode 的完整内容
 *
 * @param inode_no   要读取的 inode 号 (1-based)
 * @param inode_out  调用者提供的缓冲区，大小至少 sb.s_inode_size
 * @return 0 on success, -1 on error
 */
int ext2_read_inode(uint32_t inode_no, struct ext2_inode *inode_out)
{
    // 只要 1 <= inode_no <= 总 inode 数就行
    if (inode_no == 0 || inode_no > sb.s_inodes_count)
        return -1;

    // inode_size 动态：版本 1+ 用 superblock 里的字段，否则固定 128
    uint32_t inode_size = (sb.s_rev_level >= 1 ? sb.s_inode_size : 128);

    /* 下面的逻辑不变：算出所在组 + 组内索引，算出哪个 block 和偏移 */
    uint32_t idx        = inode_no - 1;
    uint32_t group      = idx / sb.s_inodes_per_group;
    uint32_t index      = idx % sb.s_inodes_per_group;
    if (group >= sb_groups_count)
        return -1;

    uint32_t itable     = gbdt[group].bg_inode_table;
    uint32_t block_sz   = 1024U << sb.s_log_block_size;
    uint32_t byte_off   = index * inode_size;
    uint32_t block_off  = byte_off  / block_sz;
    uint32_t intra_off  = byte_off  % block_sz;

    uint32_t secs_pb    = block_sz / SECTOR_SIZE;
    uint32_t start_sec  = (itable + block_off) * secs_pb
                         + (intra_off / SECTOR_SIZE);
    uint32_t off_in_sec = intra_off % SECTOR_SIZE;
    uint32_t to_read    = (off_in_sec + inode_size + SECTOR_SIZE - 1)
                         / SECTOR_SIZE;

    /* 临时缓冲，读足扇区 */
    uint32_t tmp_bytes = to_read * SECTOR_SIZE;
    uint8_t *tmp = kmalloc(tmp_bytes);
    if (!tmp) return -1;
    if (!ata_read_sectors(start_sec, to_read, tmp)) {
        kfree(tmp);
        return -1;
    }

    /* 拷贝 inode_size 字节 */
    kmemcpy(inode_out, tmp + off_in_sec, INODE_SIZE);
    kfree(tmp);
    return 0;
}

int ext2_write_inode(uint32_t inode_no, const struct ext2_inode *inode) {
    uint32_t inode_size, idx, group, index, block_size, byte_off, start_sec;
    uint32_t off_in_sec, to_write;
    uint8_t *tmp;
    int result = -1;
    if (!inode || inode_no == 0 || inode_no > sb.s_inodes_count) return -1;
    inode_size = sb.s_rev_level >= 1 ? sb.s_inode_size : 128;
    idx = inode_no - 1; group = idx / sb.s_inodes_per_group; index = idx % sb.s_inodes_per_group;
    if (group >= sb_groups_count) return -1;
    block_size = 1024U << sb.s_log_block_size;
    byte_off = index * inode_size;
    start_sec = (gbdt[group].bg_inode_table + byte_off / block_size) * (block_size / SECTOR_SIZE)
                + (byte_off % block_size) / SECTOR_SIZE;
    off_in_sec = byte_off % SECTOR_SIZE;
    to_write = (off_in_sec + inode_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    tmp = kmalloc(to_write * SECTOR_SIZE);
    if (!tmp) return -1;
    if (ata_read_sectors(start_sec, to_write, tmp) == to_write) {
        kmemcpy(tmp + off_in_sec, inode, sizeof(*inode));
        if (ata_write_sectors(start_sec, to_write, tmp) == to_write) result = 0;
    }
    kfree(tmp);
    return result;
}

static int bitmap_allocate(uint32_t bitmap_block, uint32_t limit, uint32_t *bit_out) {
    uint32_t block_size = 1024U << sb.s_log_block_size;
    uint8_t *bitmap = kmalloc(block_size);
    if (!bitmap) return -1;
    if (ext2_read_block(bitmap_block, bitmap) < 0) { kfree(bitmap); return -1; }
    for (uint32_t bit = 0; bit < limit; bit++) {
        if (!(bitmap[bit / 8] & (1U << (bit % 8)))) {
            bitmap[bit / 8] |= 1U << (bit % 8);
            if (ext2_write_block(bitmap_block, bitmap) < 0) { kfree(bitmap); return -1; }
            *bit_out = bit; kfree(bitmap); return 0;
        }
    }
    kfree(bitmap); return -1;
}

int ext2_alloc_inode(uint32_t *inode_no) {
    if (!inode_no) return -1;
    for (uint32_t group = 0; group < sb_groups_count; group++) {
        uint32_t bit, limit = sb.s_inodes_per_group;
        if (!gbdt[group].bg_free_inodes_count) continue;
        if ((group + 1) * limit > sb.s_inodes_count) limit = sb.s_inodes_count - group * limit;
        if (bitmap_allocate(gbdt[group].bg_inode_bitmap, limit, &bit) == 0) {
            *inode_no = group * sb.s_inodes_per_group + bit + 1;
            gbdt[group].bg_free_inodes_count--; sb.s_free_inodes_count--;
            return write_gbdt() < 0 || write_sb() < 0 ? -1 : 0;
        }
    }
    return -1;
}

/* Directory accounting belongs to the group containing the inode. */
int ext2_adjust_dirs(uint32_t inode_no, int delta) {
    if (!inode_no || inode_no > sb.s_inodes_count) return -1;
    uint32_t group = (inode_no - 1) / sb.s_inodes_per_group;
    int count = (int)gbdt[group].bg_used_dirs_count + delta;
    if (count < 0 || count > 65535) return -1;
    gbdt[group].bg_used_dirs_count = count;
    if (write_gbdt() < 0) {
        gbdt[group].bg_used_dirs_count -= delta;
        return -1;
    }
    return 0;
}

int ext2_free_inode(uint32_t inode_no) {
    if (!inode_no || inode_no > sb.s_inodes_count) return -1;
    uint32_t group = (inode_no - 1) / sb.s_inodes_per_group;
    uint32_t bit = (inode_no - 1) % sb.s_inodes_per_group;
    uint8_t *bitmap = kmalloc(1024U << sb.s_log_block_size);
    if (!bitmap) return -1;
    if (ext2_read_block(gbdt[group].bg_inode_bitmap, bitmap) < 0 ||
        !(bitmap[bit / 8] & (1U << (bit % 8)))) { kfree(bitmap); return -1; }
    bitmap[bit / 8] &= ~(1U << (bit % 8));
    if (ext2_write_block(gbdt[group].bg_inode_bitmap, bitmap) < 0) { kfree(bitmap); return -1; }
    kfree(bitmap);
    gbdt[group].bg_free_inodes_count++;
    sb.s_free_inodes_count++;
    return write_gbdt() < 0 || write_sb() < 0 ? -1 : 0;
}

int ext2_alloc_block(uint32_t *block_no) {
    uint32_t block_size = 1024U << sb.s_log_block_size;
    uint8_t *zero;
    if (!block_no) return -1;
    for (uint32_t group = 0; group < sb_groups_count; group++) {
        uint32_t bit, limit = sb.s_blocks_per_group;
        uint32_t first = sb.s_first_data_block + group * sb.s_blocks_per_group;
        if (!gbdt[group].bg_free_blocks_count) continue;
        if (first + limit > sb.s_blocks_count) limit = sb.s_blocks_count - first;
        if (bitmap_allocate(gbdt[group].bg_block_bitmap, limit, &bit) == 0) {
            *block_no = first + bit;
            gbdt[group].bg_free_blocks_count--; sb.s_free_blocks_count--;
            if (write_gbdt() < 0 || write_sb() < 0) return -1;
            zero = kmalloc(block_size);
            if (!zero) return -1;
            kmemset(zero, 0, block_size);
            int result = ext2_write_block(*block_no, zero);
            kfree(zero); return result;
        }
    }
    return -1;
}

int ext2_free_block(uint32_t block_no) {
    uint32_t block_size = 1024U << sb.s_log_block_size;
    uint32_t group, bit;
    uint8_t *bitmap;
    if (block_no < sb.s_first_data_block || block_no >= sb.s_blocks_count) return -1;
    group = (block_no - sb.s_first_data_block) / sb.s_blocks_per_group;
    bit = (block_no - sb.s_first_data_block) % sb.s_blocks_per_group;
    bitmap = kmalloc(block_size);
    if (!bitmap) return -1;
    if (ext2_read_block(gbdt[group].bg_block_bitmap, bitmap) < 0 ||
        !(bitmap[bit / 8] & (1U << (bit % 8)))) { kfree(bitmap); return -1; }
    bitmap[bit / 8] &= ~(1U << (bit % 8));
    if (ext2_write_block(gbdt[group].bg_block_bitmap, bitmap) < 0) { kfree(bitmap); return -1; }
    kfree(bitmap); gbdt[group].bg_free_blocks_count++; sb.s_free_blocks_count++;
    return write_gbdt() < 0 || write_sb() < 0 ? -1 : 0;
}


int ext2_driver_init(void) {
    if (!read_sb())
        return -1;

    /* 1) 计算 Block Group 数量 */
    {
        uint32_t total_blocks     = sb.s_blocks_count;
        uint32_t first_data_block = sb.s_first_data_block;
        uint32_t bpg              = sb.s_blocks_per_group;
        sb_groups_count = (total_blocks - first_data_block + bpg - 1) / bpg;
    }

    /* 2) 读入 Group Descriptor Table */
    gbdt = kmalloc(sb_groups_count * sizeof(*gbdt));
    if (!gbdt) {
        kprintf("ext2_init: kmalloc for gbdt failed\n");
        return -1;
    }
    {
        uint32_t block_size      = 1024U << sb.s_log_block_size;
        uint32_t gd_block        = (block_size == 1024 ? 2 : 1);
        uint32_t secs_per_blk    = block_size / SECTOR_SIZE;
        uint32_t total_bytes     = sb_groups_count * sizeof(*gbdt);
        uint32_t secs_to_read    = (total_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
        ata_read_sectors(gd_block * secs_per_blk,
                         secs_to_read,
                         (uint8_t*)gbdt);
    }

    /* 3) 打印一些信息 */
    // kprintf("Total Blocks:        %u\n", sb.s_blocks_count);
    // kprintf("Free Blocks:         %u\n", sb.s_free_blocks_count);
    // kprintf("Group 0 Free Blocks: %u\n", gbdt[0].bg_free_blocks_count);
    // kprintf("Ext2 version: %u.%u\n",
    //        sb.s_rev_level, sb.s_minor_rev_level);
    // if (sb.s_rev_level >= 1) {
    //     kprintf("Supports extended superblock fields\n");
    // } else {
    //     kprintf("Old revision (no extended fields)\n");
    // }

    /* 4) 读 root inode 并打印它的大小 */
    {
        const uint32_t ROOT_INO = 2;
        uint32_t isz = (sb.s_rev_level >= 1 ? sb.s_inode_size : 128);
        struct ext2_inode *root_inode = kmalloc(isz);
        if (!root_inode) {
            kprintf("ext2_init: kmalloc inode buf failed\n");
            return -1;
        }
        if (ext2_read_inode(ROOT_INO, root_inode) < 0) {
            kprintf("ext2_init: read root inode failed\n");
            kfree(root_inode);
            return -1;
        }

        // uint64_t size = (sb.s_rev_level >= 1)
        //     ? ((uint64_t)root_inode->i_size_hi << 32)
        //       | root_inode->i_size_lo
        //     : root_inode->i_size_lo;
        // kprintf("Root inode size: %u bytes\n",
        //        (unsigned long long)size);

        kfree(root_inode);
    }
    return 0;
}

const struct ext2_super_block *ext2_sb(void) {
  return &sb;
}
const struct ext2_group_desc *ext2_gbdt(void) {
  return gbdt;
}


// 计算每块包含多少扇区
static inline uint32_t ext2_sectors_per_block(void) {
    // sb 已经是模块私有的 static struct ext2_super_block
    return (1024U << sb.s_log_block_size) / SECTOR_SIZE;
}

/**
 * 读取整个数据块到 buf。
 * @param block_no  要读的块号（基于 0 的块索引）
 * @param buf       目标缓冲区，大小至少为 block_size
 * @return 0 成功，-1 失败
 */
int ext2_read_block(uint32_t block_no, void *buf) {
    uint32_t secs = ext2_sectors_per_block();
    uint32_t start_sec = block_no * secs;
    // ata_read_sectors 返回实际读到的扇区数
    if (ata_read_sectors(start_sec, secs, buf) != secs)
        return -1;
    return 0;
}

int ext2_write_block(uint32_t block_no, const void *buf) {
    uint32_t secs = ext2_sectors_per_block();
    return ata_write_sectors(block_no * secs, secs, buf) == secs ? 0 : -1;
}
