#ifndef _EXT2_H
#define _EXT2_H

#include <stdint.h>


struct ext2_super_block {
    /* 0x00 */ uint32_t s_inodes_count;       /* 总 inode 数量 */
    /* 0x04 */ uint32_t s_blocks_count;       /* 总 blocks 数量 */
    /* 0x08 */ uint32_t s_r_blocks_count;     /* 为超级用户保留的 blocks 数量 */
    /* 0x0C */ uint32_t s_free_blocks_count;  /* 空闲 blocks 数量 */
    /* 0x10 */ uint32_t s_free_inodes_count;  /* 空闲 inodes 数量 */
    /* 0x14 */ uint32_t s_first_data_block;   /* 超级块所在 block 号 */
    /* 0x18 */ uint32_t s_log_block_size;     /* log2(block size) - 10 */
    /* 0x1C */ uint32_t s_log_frag_size;      /* log2(fragment size) - 10 */
    /* 0x20 */ uint32_t s_blocks_per_group;   /* 每组 blocks 数 */
    /* 0x24 */ uint32_t s_frags_per_group;    /* 每组 fragments 数 */
    /* 0x28 */ uint32_t s_inodes_per_group;   /* 每组 inodes 数 */

    /* 0x2C */ uint32_t s_mtime;              /* 上次挂载时间 */
    /* 0x30 */ uint32_t s_wtime;              /* 上次写入时间 */

    /* 0x34 */ uint16_t s_mnt_count;          /* 自上次 fsck 起已挂载次数 */
    /* 0x36 */ uint16_t s_max_mnt_count;      /* 挂载次数上限 */
    /* 0x38 */ uint16_t s_magic;              /* 文件系统标志，固定 0xEF53 */
    /* 0x3A */ uint16_t s_state;              /* 文件系统状态 */
    /* 0x3C */ uint16_t s_errors;             /* 出错时处理方式 */
    /* 0x3E */ uint16_t s_minor_rev_level;    /* 次要版本号 */

    /* 0x40 */ uint32_t s_lastcheck;          /* 上次一致性检查时间 */
    /* 0x44 */ uint32_t s_checkinterval;      /* 强制检查间隔（秒） */
    /* 0x48 */ uint32_t s_creator_os;         /* 创建该 FS 的 OS ID */
    /* 0x4C */ uint32_t s_rev_level;          /* 主要版本号 */

    /* 0x50 */ uint16_t s_def_resuid;         /* 默认保留 blocks 的 UID */
    /* 0x52 */ uint16_t s_def_resgid;         /* 默认保留 blocks 的 GID */

    /* --- 扩展字段，Major >= 1 时有效 --- */

    /* 0x54 */ uint32_t s_first_ino;          /* 第一个非保留 inode（版本 ≥1.0） */
    /* 0x58 */ uint16_t s_inode_size;         /* 每个 inode 结构大小 */
    /* 0x5A */ uint16_t s_block_group_nr;     /* 本超级块所属的 block 组号 */

    /* 0x5C */ uint32_t s_feature_compat;     /* 可选兼容特性 */
    /* 0x60 */ uint32_t s_feature_incompat;   /* 不兼容特性 */
    /* 0x64 */ uint32_t s_feature_ro_compat;  /* 只读兼容特性 */

    /* 0x68 */ uint8_t  s_uuid[16];           /* 文件系统 UUID */
    /* 0x78 */ char     s_volume_name[16];    /* 卷名 */
    /* 0x88 */ char     s_last_mounted[64];   /* 上次挂载路径 */

    /* 0xC8 */ uint32_t s_compression_algo;   /* 压缩算法 ID */
    /* 0xCC */ uint8_t  s_prealloc_blocks;    /* 预分配文件 blocks 数 */
    /* 0xCD */ uint8_t  s_prealloc_dir_blocks;/* 预分配目录 blocks 数 */
    /* 0xCE */ uint16_t s_reserved;           /* 保留 */

    /* 0xD0 */ uint8_t  s_journal_uuid[16];   /* 日志 UUID */
    /* 0xE0 */ uint32_t s_journal_inum;       /* 日志 inode */
    /* 0xE4 */ uint32_t s_journal_dev;        /* 日志设备号 */
    /* 0xE8 */ uint32_t s_last_orphan;        /* orphan inode 列表头 */

    // 后续 0xEC–0x3FF 未使用
} __attribute__((packed));


struct ext2_group_desc {
    /* 0x00-0x03 */ uint32_t bg_block_bitmap;      /* 该组的 block 使用位图所在的块号 */
    /* 0x04-0x07 */ uint32_t bg_inode_bitmap;      /* 该组的 inode 使用位图所在的块号 */
    /* 0x08-0x0B */ uint32_t bg_inode_table;       /* 该组的 inode 表起始块号 */
    /* 0x0C-0x0D */ uint16_t bg_free_blocks_count; /* 该组中未分配的 block 数 */
    /* 0x0E-0x0F */ uint16_t bg_free_inodes_count; /* 该组中未分配的 inode 数 */
    /* 0x10-0x11 */ uint16_t bg_used_dirs_count;   /* 该组中目录条目数 */
    /* 0x12-0x1F */ uint8_t  bg_unused[14];        /* 保留（未使用）字节，填充到 32 字节 */
} __attribute__((packed));

#define EXT2_N_BLOCKS       15  /* 12 direct + indirects */

struct ext2_inode {
    uint16_t i_mode;        /* 文件类型和权限 */
    uint16_t i_uid;         /* 所有者用户 ID */
    uint32_t i_size_lo;     /* 低 32 位的文件大小（字节） */
    uint32_t i_atime;       /* 最后访问时间 */
    uint32_t i_ctime;       /* 创建时间 */
    uint32_t i_mtime;       /* 最后修改时间 */
    uint32_t i_dtime;       /* 删除时间 */
    uint16_t i_gid;         /* 所属组 ID */
    uint16_t i_links_count; /* 硬链接计数 */
    uint32_t i_blocks;      /* 占用的 512 字节扇区数（不含 inode 自身） */
    uint32_t i_flags;       /* 文件标志 */
    uint32_t i_osd1;        /* 操作系统专用 */
    uint32_t i_block[EXT2_N_BLOCKS]; /* 0–11: 直接块；12: 单间接；13: 双间接；14: 三间接 */
    uint32_t i_generation;  /* NFS 用的文件版本号 */
    uint32_t i_file_acl;    /* 文件 ACL 块（或版本 0 时保留） */
    uint32_t i_size_hi;     /* 高 32 位的文件大小（仅在 version ≥ 1 且启用大文件时有效） */
    uint32_t i_faddr;       /* 片地址（fragment） */
    uint8_t  i_osd2[12];    /* 操作系统专用 */
}__attribute__((packed));


// 返回指向只读 sb 的指针
const struct ext2_super_block *ext2_sb(void);

// 返回指向 descriptor table 首元素的指针
const struct ext2_group_desc *ext2_gbdt(void);

int ext2_read_inode(uint32_t inode_no, struct ext2_inode *inode_out);
int ext2_write_inode(uint32_t inode_no, const struct ext2_inode *inode);
int ext2_alloc_inode(uint32_t *inode_no);
int ext2_alloc_block(uint32_t *block_no);
int ext2_free_block(uint32_t block_no);

int ext2_driver_init();

int ext2_read_block(uint32_t block_no, void *buf);
int ext2_write_block(uint32_t block_no, const void *buf);

#endif
