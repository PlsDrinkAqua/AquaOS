#include <stdint.h>

#include <libk/stdio.h>
#include <libk/string.h>

#include <kernel/ata.h>
#include <kernel/ext2.h>
#include <kernel/ext2_api.h>
#include <kernel/kmalloc.h>

#define EXT2_ROOT_INO 2
#define EXT2_S_IFMT  0xF000
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000

static uint64_t ext2_inode_size(const struct ext2_inode *inode) {
    uint64_t size = inode->i_size_lo;
    if ((inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG) {
        size |= (uint64_t)inode->i_size_hi << 32;
    }
    return size;
}

static int ext2_inode_block(const struct ext2_inode *inode,
                            uint32_t block_index,
                            uint32_t *block_out) {
    const struct ext2_super_block *sb = ext2_sb();
    uint32_t block_size = 1024U << sb->s_log_block_size;
    uint32_t pointers_per_block = block_size / sizeof(uint32_t);

    if (block_index < 12) {
        *block_out = inode->i_block[block_index];
        return 0;
    }

    block_index -= 12;
    if (block_index < pointers_per_block && inode->i_block[12]) {
        uint32_t *indirect = kmalloc(block_size);
        if (!indirect) return -1;
        if (ext2_read_block(inode->i_block[12], indirect) < 0) {
            kfree(indirect);
            return -1;
        }
        *block_out = indirect[block_index];
        kfree(indirect);
        return 0;
    }

    return -1;
}

int ext2_init(void) {
    block_devices_init();
    if (ext2_driver_init() < 0) {
        kprintf("EXT2 initialization failed!\n");
        return -1;
    }
    return 0;
}

static uint32_t ext2_lookup(uint32_t dir_ino, const char *name) {
    struct ext2_inode inode;
    const struct ext2_super_block *sb;
    uint32_t block_size;
    uint32_t blocks;
    uint8_t *buf;
    size_t wanted_len;

    if (ext2_read_inode(dir_ino, &inode) < 0 ||
        (inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return 0;

    sb = ext2_sb();
    block_size = 1024U << sb->s_log_block_size;
    blocks = (inode.i_size_lo + block_size - 1) / block_size;
    wanted_len = kstrlen(name);
    buf = kmalloc(block_size);
    if (!buf) return 0;

    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t block;
        uint32_t offset = 0;

        if (ext2_inode_block(&inode, i, &block) < 0 || !block ||
            ext2_read_block(block, buf) < 0) continue;

        while (offset + 8 <= block_size &&
               (uint64_t)i * block_size + offset < inode.i_size_lo) {
            struct ext2_dir_entry_2 *de = (void *)(buf + offset);
            if (de->rec_len < 8 || offset + de->rec_len > block_size ||
                de->name_len > de->rec_len - 8) break;
            if (de->inode && de->name_len == wanted_len &&
                kmemcmp(de->name, name, wanted_len) == 0) {
                uint32_t found = de->inode;
                kfree(buf);
                return found;
            }
            offset += de->rec_len;
        }
    }

    kfree(buf);
    return 0;
}

int ext2_file_open(const char *path, ext2_file_t *file) {
    uint32_t ino = EXT2_ROOT_INO;
    struct ext2_inode inode;
    char *saveptr;
    char *tok;
    char *tmp;
    size_t path_len;

    /* Until cwd support exists, relative paths are resolved from ext2 root. */
    if (!path || path[0] == '\0' || !file) return -1;

    path_len = kstrlen(path);
    tmp = kmalloc(path_len + 1);
    if (!tmp) return -1;
    kstrcpy(tmp, path);

    tok = kstrtok_r(tmp, "/", &saveptr);
    while (tok) {
        ino = ext2_lookup(ino, tok);
        if (!ino) {
            kfree(tmp);
            return -1;
        }
        tok = kstrtok_r(NULL, "/", &saveptr);
    }
    kfree(tmp);

    if (ext2_read_inode(ino, &inode) < 0) return -1;
    kmemset(file, 0, sizeof(*file));
    file->used = 1;
    file->ino = ino;
    file->inode = inode;
    file->size = ext2_inode_size(&inode);
    return 0;
}

size_t ext2_file_size(const ext2_file_t *file) {
    return file && file->used ? (size_t)file->size : 0;
}

int ext2_file_is_dir(const ext2_file_t *file) {
    return file && file->used &&
           (file->inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

int ext2_file_read(ext2_file_t *file, void *buf, size_t count) {
    const struct ext2_super_block *sb;
    uint32_t block_size;
    uint8_t *tmp;
    size_t total = 0;

    if (!file || !file->used || (!buf && count) || ext2_file_is_dir(file)) return -1;
    if (count == 0) return 0;

    sb = ext2_sb();
    block_size = 1024U << sb->s_log_block_size;
    tmp = kmalloc(block_size);
    if (!tmp) return -1;

    while (total < count && file->pos < file->size) {
        uint32_t block;
        uint32_t block_index = file->pos / block_size;
        uint32_t block_offset = file->pos % block_size;
        size_t chunk = block_size - block_offset;

        if (ext2_inode_block(&file->inode, block_index, &block) < 0 || !block ||
            ext2_read_block(block, tmp) < 0) break;
        if (chunk > count - total) chunk = count - total;
        if (chunk > file->size - file->pos) chunk = file->size - file->pos;
        kmemcpy((uint8_t *)buf + total, tmp + block_offset, chunk);
        file->pos += chunk;
        total += chunk;
    }

    kfree(tmp);
    return (int)total;
}

int ext2_file_seek(ext2_file_t *file, int32_t offset, int whence) {
    int64_t base;
    int64_t next;

    if (!file || !file->used) return -1;
    if (whence == 0) base = 0;
    else if (whence == 1) base = file->pos;
    else if (whence == 2) base = file->size;
    else return -1;

    next = base + offset;
    if (next < 0 || (uint64_t)next > file->size || next > 0x7fffffff) return -1;
    file->pos = (uint32_t)next;
    return (int)file->pos;
}

int ext2_file_read_dir(ext2_file_t *file, char *name, size_t name_size) {
    const struct ext2_super_block *sb;
    uint32_t block_size;
    uint8_t *buf;

    if (!file || !file->used || !name || name_size == 0 ||
        !ext2_file_is_dir(file)) return -1;

    sb = ext2_sb();
    block_size = 1024U << sb->s_log_block_size;
    buf = kmalloc(block_size);
    if (!buf) return -1;

    while (file->pos < file->size) {
        uint32_t block;
        uint32_t block_index = file->pos / block_size;
        uint32_t offset = file->pos % block_size;
        struct ext2_dir_entry_2 *de;

        if (ext2_inode_block(&file->inode, block_index, &block) < 0 || !block ||
            ext2_read_block(block, buf) < 0 || offset + 8 > block_size) {
            kfree(buf);
            return -1;
        }
        de = (void *)(buf + offset);
        if (de->rec_len < 8 || offset + de->rec_len > block_size ||
            de->name_len > de->rec_len - 8) {
            kfree(buf);
            return -1;
        }
        file->pos += de->rec_len;
        if (de->inode) {
            size_t len = de->name_len;
            if (len + 1 > name_size) {
                kfree(buf);
                return -1;
            }
            kmemcpy(name, de->name, len);
            name[len] = '\0';
            kfree(buf);
            return 1;
        }
    }

    kfree(buf);
    return 0;
}

void ext2_selftest(void) {
    ext2_file_t file;
    char buf[129];
    int n;

    if (ext2_file_open("/hello.txt", &file) < 0) {
        kprintf("ext2 selftest: open failed\n");
        return;
    }
    n = ext2_file_read(&file, buf, sizeof(buf) - 1);
    if (n >= 0) {
        buf[n] = '\0';
        kprintf("ext2 selftest: %s\n", buf);
    }
}
