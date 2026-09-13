#include <stdint.h>
#include <libk/stdio.h>
#include <libk/string.h>
#include <kernel/ata.h>
#include <kernel/errno.h>
#include <kernel/ext2.h>
#include <kernel/ext2_api.h>
#include <kernel/kmalloc.h>
#include <kernel/fd.h>
#include <kernel/task.h>

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
    /* Do not partially free layouts this driver cannot traverse. */
    if (inode->i_block[13] || inode->i_block[14]) return -K_EFBIG;
    if (inode->i_file_acl) return -K_ENOSYS;
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

static int lookup(uint32_t dir_ino, const char *name, uint32_t *found) {
    struct ext2_inode inode;
    uint32_t bs = block_size(), blocks;
    uint8_t *buf;
    size_t wanted = kstrlen(name);
    if (ext2_read_inode(dir_ino, &inode) < 0) return -K_EIO;
    if ((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -K_ENOTDIR;
    blocks = (inode.i_size_lo + bs - 1) / bs;
    buf = kmalloc(bs); if (!buf) return -K_ENOMEM;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t block, offset = 0;
        if (inode_block(&inode, i, &block) < 0 || !block || ext2_read_block(block, buf) < 0) { kfree(buf); return -K_EIO; }
        while (offset + 8 <= bs && (uint64_t)i * bs + offset < inode.i_size_lo) {
            struct ext2_dir_entry_2 *de = (void *)(buf + offset);
            if (de->rec_len < 8 || offset + de->rec_len > bs || de->name_len > de->rec_len - 8) { kfree(buf); return -K_EIO; }
            if (de->inode && de->name_len == wanted && kmemcmp(de->name, name, wanted) == 0) {
                *found = de->inode; kfree(buf); return 0;
            }
            offset += de->rec_len;
        }
    }
    kfree(buf); return -K_ENOENT;
}

static int add_dir_entry(uint32_t dir_ino, struct ext2_inode *dir,
                         const char *name, uint32_t child_ino, uint8_t type) {
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
                new_de->name_len = kstrlen(name); new_de->file_type = type;
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
    de->inode = child_ino; de->rec_len = bs; de->name_len = kstrlen(name); de->file_type = type;
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
        uint32_t found;
        int result = lookup(ino, token, &found);
        if (result < 0 && result != -K_ENOENT) { kfree(tmp); return result; }
        if (result == -K_ENOENT) {
            struct ext2_inode parent, child;
            uint32_t child_ino;
            if (!create || next || !*token || kstrlen(token) > EXT2_NAME_MAX) { kfree(tmp); return -K_ENOENT; }
            if (ext2_read_inode(ino, &parent) < 0 || (parent.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
                kfree(tmp); return -K_ENOTDIR;
            }
            if (ext2_alloc_inode(&child_ino) < 0) { kfree(tmp); return -K_ENOSPC; }
            kmemset(&child, 0, sizeof(child)); child.i_mode = EXT2_S_IFREG | 0644; child.i_links_count = 1;
            if (ext2_write_inode(child_ino, &child) < 0 || add_dir_entry(ino, &parent, token, child_ino, EXT2_FT_REG_FILE) < 0) {
                kfree(tmp); return -K_EIO;
            }
            found = child_ino;
        }
        ino = found; token = next;
    }
    kfree(tmp); *ino_out = ino; return 0;
}

/* Callers serialize filesystem mutations with task_preemption_disable(). */
int ext2_lookup_inode(const char *path, uint32_t *ino) {
    return resolve_path(path, 0, ino);
}

int ext2_reclaim_unlinked(uint32_t ino) {
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -K_EIO;
    if (inode.i_links_count || fd_inode_is_open(ino)) return 0;
    int result = inode_truncate(ino, &inode);
    if (result < 0) return result;
    /* Small dtime values encode orphan-list inode numbers to e2fsck. Until
     * there is a wall clock, use the image timestamp (or 2000-01-01). */
    inode.i_dtime = ext2_sb()->s_wtime > ext2_sb()->s_inodes_count ?
        ext2_sb()->s_wtime : 946684800U;
    if (ext2_write_inode(ino, &inode) < 0) return -K_EIO;
    return ext2_free_inode(ino) < 0 ? -K_EIO : 0;
}

static int directory_empty(uint32_t ino) {
    ext2_file_t file;
    char name[EXT2_NAME_MAX + 1];
    kmemset(&file, 0, sizeof(file));
    file.used = 1; file.ino = ino;
    if (ext2_read_inode(ino, &file.inode) < 0) return -K_EIO;
    file.size = inode_size(&file.inode);
    int result;
    while ((result = ext2_file_read_dir(&file, name, sizeof(name))) > 0) {
        if (kstrcmp(name, ".") && kstrcmp(name, "..")) return -K_ENOTEMPTY;
    }
    return result;
}

static int remove_dir_entry(const struct ext2_inode *parent, const char *name,
                            uint32_t ino) {
    uint32_t bs = block_size();
    uint8_t *buf = kmalloc(bs);
    if (!buf) return -K_ENOMEM;
    int result = -K_ENOENT;
    for (uint32_t i = 0; (uint64_t)i * bs < parent->i_size_lo; i++) {
        uint32_t block, offset = 0;
        struct ext2_dir_entry_2 *previous = NULL;
        if (inode_block(parent, i, &block) < 0 || !block ||
            ext2_read_block(block, buf) < 0) { result = -K_EIO; break; }
        while (offset < bs) {
            struct ext2_dir_entry_2 *de = (void *)(buf + offset);
            if (offset + 8 > bs || de->rec_len < 8 || (de->rec_len & 3) ||
                offset + de->rec_len > bs || de->name_len > de->rec_len - 8) {
                result = -K_EIO; goto done;
            }
            if (de->inode == ino && de->name_len == kstrlen(name) &&
                !kmemcmp(de->name, name, de->name_len)) {
                if (previous) previous->rec_len += de->rec_len;
                else { de->inode = 0; de->name_len = 0; de->file_type = 0; }
                result = ext2_write_block(block, buf) < 0 ? -K_EIO : 0;
                goto done;
            }
            previous = de;
            offset += de->rec_len;
        }
    }
done:
    kfree(buf);
    return result;
}

int ext2_remove(const char *path, int directory) {
    if (!path || !*path) return -K_ENOENT;
    size_t length = kstrlen(path);
    int trailing_slash = path[length - 1] == '/';
    char *storage = kmalloc(length + 1);
    if (!storage) return -K_ENOMEM;
    kmemcpy(storage, path, length + 1);
    while (length && storage[length - 1] == '/') storage[--length] = 0;
    int result = -K_EBUSY;
    if (!length) goto done;
    size_t start = length;
    while (start && storage[start - 1] != '/') start--;
    const char *name = storage + start;
    if (!kstrcmp(name, ".") || !kstrcmp(name, "..")) { result = -K_EINVAL; goto done; }
    if (length - start > EXT2_NAME_MAX) { result = -K_ENAMETOOLONG; goto done; }
    if (start) storage[start - 1] = 0;
    uint32_t parent_ino, ino;
    struct ext2_inode parent, child;
    result = resolve_path(start && *storage ? storage : "/", 0, &parent_ino);
    if (result < 0) goto done;
    result = lookup(parent_ino, name, &ino);
    if (result < 0) goto done;
    if (ino == EXT2_ROOT_INO) { result = -K_EBUSY; goto done; }
    if (ext2_read_inode(parent_ino, &parent) < 0 || ext2_read_inode(ino, &child) < 0) {
        result = -K_EIO; goto done;
    }
    int is_dir = (child.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
    if ((directory || trailing_slash) && !is_dir) { result = -K_ENOTDIR; goto done; }
    if (!directory && is_dir) { result = -K_EISDIR; goto done; }
    if (!is_dir && (child.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
        result = -K_EPERM; goto done;
    }
    if (child.i_block[13] || child.i_block[14]) { result = -K_EFBIG; goto done; }
    if (child.i_file_acl) { result = -K_ENOSYS; goto done; }
    if (!child.i_links_count) { result = -K_EIO; goto done; }
    if (is_dir) {
        result = task_directory_in_use(ino);
        if (result < 0) goto done;
        if (result || fd_inode_is_open(ino)) { result = -K_EBUSY; goto done; }
        result = directory_empty(ino);
        if (result < 0) goto done;
        if (child.i_links_count != 2 || parent.i_links_count < 2) { result = -K_EIO; goto done; }
    }
    result = remove_dir_entry(&parent, name, ino);
    if (result < 0) goto done;
    child.i_links_count = is_dir ? 0 : child.i_links_count - 1;
    if (ext2_write_inode(ino, &child) < 0) { result = -K_EIO; goto done; }
    if (is_dir) {
        parent.i_links_count--;
        if (ext2_write_inode(parent_ino, &parent) < 0 || ext2_adjust_dirs(ino, -1) < 0) {
            result = -K_EIO; goto done;
        }
    }
    result = ext2_reclaim_unlinked(ino);
done:
    kfree(storage);
    return result;
}

int ext2_mkdir(const char *path) {
    if (!path || !*path) return -K_ENOENT;
    size_t length = kstrlen(path);
    char *parent_path = kmalloc(length + 1);
    if (!parent_path) return -K_ENOMEM;
    kmemcpy(parent_path, path, length + 1);
    while (length && parent_path[length - 1] == '/') parent_path[--length] = 0;
    if (!length) { kfree(parent_path); return -K_EEXIST; }
    size_t start = length;
    while (start && parent_path[start - 1] != '/') start--;
    const char *name = parent_path + start;
    if (length - start > EXT2_NAME_MAX) { kfree(parent_path); return -K_ENAMETOOLONG; }

    uint32_t parent_ino, existing, ino = 0, block = 0;
    struct ext2_inode parent, child;
    int result, counted = 0;
    if (start) {
        parent_path[start - 1] = 0;
        result = resolve_path(*parent_path ? parent_path : "/", 0, &parent_ino);
    } else {
        parent_ino = EXT2_ROOT_INO;
        result = 0;
    }
    if (result < 0) goto done;
    if (ext2_read_inode(parent_ino, &parent) < 0) { result = -K_EIO; goto done; }
    if ((parent.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) { result = -K_ENOTDIR; goto done; }
    result = lookup(parent_ino, name, &existing);
    if (!result) { result = -K_EEXIST; goto done; }
    if (result != -K_ENOENT) goto done;
    if (parent.i_links_count == 65535) { result = -K_EMLINK; goto done; }

    uint32_t bs = block_size();
    uint8_t *buf = kmalloc(bs);
    if (!buf) { result = -K_ENOMEM; goto done; }
    if (ext2_alloc_inode(&ino) < 0) { result = -K_ENOSPC; goto cleanup; }
    if (ext2_alloc_block(&block) < 0) { result = -K_ENOSPC; goto cleanup; }
    kmemset(buf, 0, bs);
    struct ext2_dir_entry_2 *dot = (void *)buf;
    dot->inode = ino; dot->rec_len = dir_rec_size(1);
    dot->name_len = 1; dot->file_type = EXT2_FT_DIR; dot->name[0] = '.';
    struct ext2_dir_entry_2 *dotdot = (void *)(buf + dot->rec_len);
    dotdot->inode = parent_ino; dotdot->rec_len = bs - dot->rec_len;
    dotdot->name_len = 2; dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = dotdot->name[1] = '.';
    kmemset(&child, 0, sizeof(child));
    child.i_mode = EXT2_S_IFDIR | 0755;
    child.i_links_count = 2;
    child.i_size_lo = bs; child.i_blocks = bs / 512; child.i_block[0] = block;
    result = -K_EIO;
    if (ext2_write_block(block, buf) < 0 || ext2_write_inode(ino, &child) < 0) goto cleanup;
    if (ext2_adjust_dirs(ino, 1) < 0) goto cleanup;
    counted = 1;
    parent.i_links_count++;
    if (ext2_write_inode(parent_ino, &parent) < 0) goto cleanup;
    result = add_dir_entry(parent_ino, &parent, name, ino, EXT2_FT_DIR);
    if (result < 0) {
        parent.i_links_count--;
        ext2_write_inode(parent_ino, &parent);
        goto cleanup;
    }
    kfree(buf);
    goto done;

cleanup:
    if (counted) ext2_adjust_dirs(ino, -1);
    if (block) ext2_free_block(block);
    if (ino) {
        kmemset(&child, 0, sizeof(child));
        ext2_write_inode(ino, &child);
        ext2_free_inode(ino);
    }
    kfree(buf);
done:
    kfree(parent_path);
    return result;
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

static int file_refresh(ext2_file_t *file) {
    if (ext2_read_inode(file->ino, &file->inode) < 0) return -K_EIO;
    file->size = inode_size(&file->inode);
    return 0;
}

int ext2_file_read(ext2_file_t *file, void *buf, size_t count) {
    uint32_t bs = block_size(); uint8_t *tmp; size_t total = 0;
    if (!file || !file->used || (!buf && count)) return -K_EFAULT;
    if (file_refresh(file) < 0) return -K_EIO;
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
    if (file_refresh(file) < 0) return -K_EIO;
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
    if (file_refresh(file) < 0) return -K_EIO;
    if (whence == 0) base = 0; else if (whence == 1) base = file->pos;
    else if (whence == 2) base = file->size; else return -K_EINVAL;
    next = base + offset;
    if (next < 0 || next > 0x7fffffff) return -K_EINVAL;
    file->pos = next; return file->pos;
}

int ext2_file_read_dir(ext2_file_t *file, char *name, size_t name_size) {
    uint32_t bs = block_size(); uint8_t *buf;
    if (!file || !file->used || !name || !name_size) return -K_EINVAL;
    if (file_refresh(file) < 0) return -K_EIO;
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
