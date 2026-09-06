#include <stdint.h>
#include <libk/stdio.h>
#include <libk/string.h>
#include <kernel/ata.h>
#include <kernel/errno.h>
#include <kernel/ext2.h>
#include <kernel/ext2_api.h>
#include <kernel/kmalloc.h>

#define EXT2_ROOT_INO 2
#define EXT2_S_IFMT  0xF000
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000
#define O_ACCMODE 3
#define O_RDONLY 0
#define O_CREAT  0x40
#define O_TRUNC  0x200

static uint32_t block_size(void) { return 1024U << ext2_sb()->s_log_block_size; }
static uint32_t dir_rec_size(uint32_t name_len) { return (8 + name_len + 3) & ~3U; }

static uint64_t inode_size(const struct ext2_inode *inode) {
    uint64_t size = inode->i_size_lo;
    if ((inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG)
        size |= (uint64_t)inode->i_size_hi << 32;
    return size;
}

static void inode_set_size(struct ext2_inode *inode, uint64_t size) {
    inode->i_size_lo = (uint32_t)size;
    inode->i_size_hi = (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG ? (uint32_t)(size >> 32) : 0;
}

static int inode_block(const struct ext2_inode *inode, uint32_t index, uint32_t *out) {
    uint32_t bs = block_size(), pointers = bs / sizeof(uint32_t);
    uint32_t *indirect;
    if (!out) return -K_EINVAL;
    if (index < 12) { *out = inode->i_block[index]; return 0; }
    index -= 12;
    if (index >= pointers) return -K_EFBIG;
    if (!inode->i_block[12]) { *out = 0; return 0; }
    indirect = kmalloc(bs);
    if (!indirect) return -K_ENOMEM;
    if (ext2_read_block(inode->i_block[12], indirect) < 0) { kfree(indirect); return -K_EIO; }
    *out = indirect[index]; kfree(indirect); return 0;
}

static int inode_ensure_block(struct ext2_inode *inode, uint32_t index, uint32_t *out) {
    uint32_t bs = block_size(), pointers = bs / sizeof(uint32_t), allocated;
    uint32_t *indirect;
    if (index < 12) {
        if (!inode->i_block[index]) {
            if (ext2_alloc_block(&allocated) < 0) return -K_ENOSPC;
            inode->i_block[index] = allocated;
            inode->i_blocks += bs / 512;
        }
        *out = inode->i_block[index]; return 0;
    }
    index -= 12;
    if (index >= pointers) return -K_EFBIG;
    if (!inode->i_block[12]) {
        if (ext2_alloc_block(&allocated) < 0) return -K_ENOSPC;
        inode->i_block[12] = allocated;
        inode->i_blocks += bs / 512;
    }
    indirect = kmalloc(bs);
    if (!indirect) return -K_ENOMEM;
    if (ext2_read_block(inode->i_block[12], indirect) < 0) { kfree(indirect); return -K_EIO; }
    if (!indirect[index]) {
        if (ext2_alloc_block(&allocated) < 0) { kfree(indirect); return -K_ENOSPC; }
        indirect[index] = allocated;
        inode->i_blocks += bs / 512;
        if (ext2_write_block(inode->i_block[12], indirect) < 0) { kfree(indirect); return -K_EIO; }
    }
    *out = indirect[index]; kfree(indirect); return 0;
}

static int inode_truncate(uint32_t ino, struct ext2_inode *inode) {
    uint32_t bs = block_size(), *indirect;
    for (int i = 0; i < 12; i++) if (inode->i_block[i]) {
        if (ext2_free_block(inode->i_block[i]) < 0) return -K_EIO;
        inode->i_block[i] = 0;
    }
    if (inode->i_block[12]) {
        indirect = kmalloc(bs);
        if (!indirect) return -K_ENOMEM;
        if (ext2_read_block(inode->i_block[12], indirect) < 0) { kfree(indirect); return -K_EIO; }
        for (uint32_t i = 0; i < bs / sizeof(uint32_t); i++)
            if (indirect[i] && ext2_free_block(indirect[i]) < 0) { kfree(indirect); return -K_EIO; }
        kfree(indirect);
        if (ext2_free_block(inode->i_block[12]) < 0) return -K_EIO;
        inode->i_block[12] = 0;
    }
    inode_set_size(inode, 0); inode->i_blocks = 0;
    return ext2_write_inode(ino, inode) < 0 ? -K_EIO : 0;
}

int ext2_init(void) {
    block_devices_init();
    if (ext2_driver_init() < 0) { kprintf("EXT2 initialization failed!\n"); return -K_EIO; }
    return 0;
}

static uint32_t lookup(uint32_t dir_ino, const char *name) {
    struct ext2_inode inode;
    uint32_t bs = block_size(), blocks;
    uint8_t *buf;
    size_t wanted = kstrlen(name);
    if (ext2_read_inode(dir_ino, &inode) < 0 || (inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return 0;
    blocks = (inode.i_size_lo + bs - 1) / bs;
    buf = kmalloc(bs); if (!buf) return 0;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t block, offset = 0;
        if (inode_block(&inode, i, &block) < 0 || !block || ext2_read_block(block, buf) < 0) continue;
        while (offset + 8 <= bs && (uint64_t)i * bs + offset < inode.i_size_lo) {
            struct ext2_dir_entry_2 *de = (void *)(buf + offset);
            if (de->rec_len < 8 || offset + de->rec_len > bs || de->name_len > de->rec_len - 8) break;
            if (de->inode && de->name_len == wanted && kmemcmp(de->name, name, wanted) == 0) {
                uint32_t found = de->inode; kfree(buf); return found;
            }
            offset += de->rec_len;
        }
    }
    kfree(buf); return 0;
}

static int add_dir_entry(uint32_t dir_ino, struct ext2_inode *dir,
                         const char *name, uint32_t child_ino) {
    uint32_t bs = block_size(), need = dir_rec_size(kstrlen(name));
    uint32_t blocks = (dir->i_size_lo + bs - 1) / bs;
    uint8_t *buf = kmalloc(bs);
    if (!buf) return -K_ENOMEM;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t block, offset = 0;
        if (inode_block(dir, i, &block) < 0 || !block || ext2_read_block(block, buf) < 0) continue;
        while (offset + 8 <= bs) {
            struct ext2_dir_entry_2 *de = (void *)(buf + offset);
            uint32_t actual;
            if (de->rec_len < 8 || offset + de->rec_len > bs) break;
            actual = de->inode ? dir_rec_size(de->name_len) : 0;
            if ((!de->inode && de->rec_len >= need) || (de->inode && de->rec_len >= actual + need)) {
                uint16_t available = de->rec_len;
                struct ext2_dir_entry_2 *new_de;
                if (de->inode) { de->rec_len = actual; new_de = (void *)(buf + offset + actual); available -= actual; }
                else new_de = de;
                new_de->inode = child_ino; new_de->rec_len = available;
                new_de->name_len = kstrlen(name); new_de->file_type = EXT2_FT_REG_FILE;
                kmemcpy(new_de->name, name, new_de->name_len);
                int result = ext2_write_block(block, buf) < 0 ? -K_EIO : 0;
                kfree(buf); return result;
            }
            offset += de->rec_len;
            if (offset >= bs) break;
        }
    }
    uint32_t block;
    if (inode_ensure_block(dir, blocks, &block) < 0) { kfree(buf); return -K_ENOSPC; }
    kmemset(buf, 0, bs);
    struct ext2_dir_entry_2 *de = (void *)buf;
    de->inode = child_ino; de->rec_len = bs; de->name_len = kstrlen(name); de->file_type = EXT2_FT_REG_FILE;
    kmemcpy(de->name, name, de->name_len);
    dir->i_size_lo += bs;
    if (ext2_write_block(block, buf) < 0 || ext2_write_inode(dir_ino, dir) < 0) { kfree(buf); return -K_EIO; }
    kfree(buf); return 0;
}

static int resolve_path(const char *path, int create, uint32_t *ino_out) {
    uint32_t ino = EXT2_ROOT_INO;
    char *tmp, *save = NULL, *token;
    size_t len;
    if (!path || !*path || !ino_out) return -K_EINVAL;
    len = kstrlen(path); tmp = kmalloc(len + 1);
    if (!tmp) return -K_ENOMEM;
    kstrcpy(tmp, path); token = kstrtok_r(tmp, "/", &save);
    while (token) {
        char *next = kstrtok_r(NULL, "/", &save);
        uint32_t found = lookup(ino, token);
        if (!found) {
            struct ext2_inode parent, child;
            uint32_t child_ino;
            if (!create || next || !*token || kstrlen(token) > EXT2_NAME_MAX) { kfree(tmp); return -K_ENOENT; }
            if (ext2_read_inode(ino, &parent) < 0 || (parent.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
                kfree(tmp); return -K_ENOTDIR;
            }
            if (ext2_alloc_inode(&child_ino) < 0) { kfree(tmp); return -K_ENOSPC; }
            kmemset(&child, 0, sizeof(child)); child.i_mode = EXT2_S_IFREG | 0644; child.i_links_count = 1;
            if (ext2_write_inode(child_ino, &child) < 0 || add_dir_entry(ino, &parent, token, child_ino) < 0) {
                kfree(tmp); return -K_EIO;
            }
            found = child_ino;
        }
        ino = found; token = next;
    }
    kfree(tmp); *ino_out = ino; return 0;
}

int ext2_file_open(const char *path, int flags, ext2_file_t *file) {
    uint32_t ino;
    struct ext2_inode inode;
    int result;
    if (!file || !path || !*path || (flags & O_ACCMODE) == O_ACCMODE) return -K_EINVAL;
    result = resolve_path(path, (flags & O_CREAT) != 0, &ino);
    if (result < 0) return result;
    if (ext2_read_inode(ino, &inode) < 0) return -K_EIO;
    if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR && (flags & O_ACCMODE) != O_RDONLY) return -K_EISDIR;
    if ((flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY &&
        (inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFREG) {
        result = inode_truncate(ino, &inode); if (result < 0) return result;
    }
    kmemset(file, 0, sizeof(*file)); file->used = 1; file->ino = ino;
    file->inode = inode; file->size = inode_size(&inode); return 0;
}

size_t ext2_file_size(const ext2_file_t *file) { return file && file->used ? (size_t)file->size : 0; }
int ext2_file_is_dir(const ext2_file_t *file) {
    return file && file->used && (file->inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

int ext2_file_read(ext2_file_t *file, void *buf, size_t count) {
    uint32_t bs = block_size(); uint8_t *tmp; size_t total = 0;
    if (!file || !file->used || (!buf && count)) return -K_EFAULT;
    if (ext2_file_is_dir(file)) return -K_EISDIR;
    if (!count) return 0;
    tmp = kmalloc(bs); if (!tmp) return -K_ENOMEM;
    while (total < count && file->pos < file->size) {
        uint32_t block, index = file->pos / bs, offset = file->pos % bs;
        size_t chunk = bs - offset;
        if (inode_block(&file->inode, index, &block) < 0) { kfree(tmp); return total ? (int)total : -K_EIO; }
        if (chunk > count - total) chunk = count - total;
        if (chunk > file->size - file->pos) chunk = file->size - file->pos;
        if (!block) kmemset(tmp, 0, bs);
        else if (ext2_read_block(block, tmp) < 0) { kfree(tmp); return total ? (int)total : -K_EIO; }
        kmemcpy((uint8_t *)buf + total, tmp + offset, chunk);
        file->pos += chunk; total += chunk;
    }
    kfree(tmp); return total;
}

int ext2_file_write(ext2_file_t *file, const void *buf, size_t count, int append) {
    uint32_t bs = block_size(); uint8_t *tmp; size_t total = 0;
    if (!file || !file->used || (!buf && count)) return -K_EFAULT;
    if (ext2_file_is_dir(file)) return -K_EISDIR;
    if (append) file->pos = file->size;
    if (!count) return 0;
    tmp = kmalloc(bs); if (!tmp) return -K_ENOMEM;
    while (total < count) {
        uint32_t block, index = file->pos / bs, offset = file->pos % bs;
        size_t chunk = bs - offset; int result = inode_ensure_block(&file->inode, index, &block);
        if (result < 0) { kfree(tmp); return total ? (int)total : result; }
        if (chunk > count - total) chunk = count - total;
        if (offset || chunk < bs) {
            if (ext2_read_block(block, tmp) < 0) { kfree(tmp); return total ? (int)total : -K_EIO; }
        } else kmemset(tmp, 0, bs);
        kmemcpy(tmp + offset, (const uint8_t *)buf + total, chunk);
        if (ext2_write_block(block, tmp) < 0) { kfree(tmp); return total ? (int)total : -K_EIO; }
        file->pos += chunk; total += chunk;
        if (file->pos > file->size) file->size = file->pos;
    }
    inode_set_size(&file->inode, file->size);
    if (ext2_write_inode(file->ino, &file->inode) < 0) { kfree(tmp); return total ? (int)total : -K_EIO; }
    kfree(tmp); return total;
}

int ext2_file_seek(ext2_file_t *file, int32_t offset, int whence) {
    int64_t base, next;
    if (!file || !file->used) return -K_EBADF;
    if (whence == 0) base = 0; else if (whence == 1) base = file->pos;
    else if (whence == 2) base = file->size; else return -K_EINVAL;
    next = base + offset;
    if (next < 0 || next > 0x7fffffff) return -K_EINVAL;
    file->pos = next; return file->pos;
}

int ext2_file_read_dir(ext2_file_t *file, char *name, size_t name_size) {
    uint32_t bs = block_size(); uint8_t *buf;
    if (!file || !file->used || !name || !name_size) return -K_EINVAL;
    if (!ext2_file_is_dir(file)) return -K_ENOTDIR;
    buf = kmalloc(bs); if (!buf) return -K_ENOMEM;
    while (file->pos < file->size) {
        uint32_t block, index = file->pos / bs, offset = file->pos % bs;
        struct ext2_dir_entry_2 *de;
        if (inode_block(&file->inode, index, &block) < 0 || !block ||
            ext2_read_block(block, buf) < 0 || offset + 8 > bs) { kfree(buf); return -K_EIO; }
        de = (void *)(buf + offset);
        if (de->rec_len < 8 || offset + de->rec_len > bs || de->name_len > de->rec_len - 8) {
            kfree(buf); return -K_EIO;
        }
        file->pos += de->rec_len;
        if (de->inode) {
            size_t len = de->name_len;
            if (len + 1 > name_size) { kfree(buf); return -K_EINVAL; }
            kmemcpy(name, de->name, len); name[len] = '\0'; kfree(buf); return 1;
        }
    }
    kfree(buf); return 0;
}

void ext2_selftest(void) {
    ext2_file_t file; char buf[129]; int n;
    if (ext2_file_open("/hello.txt", O_RDONLY, &file) < 0) { kprintf("ext2 selftest: open failed\n"); return; }
    n = ext2_file_read(&file, buf, sizeof(buf) - 1);
    if (n >= 0) { buf[n] = '\0'; kprintf("ext2 selftest: %s\n", buf); }
}
