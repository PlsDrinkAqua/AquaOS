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
#define O_EXCL   0x80

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

/* Logical block paths: 12 direct, then single/double/triple indirection. */
static uint64_t maximum_file_size(void) {
    uint64_t n = block_size() / 4;
    return (12 + n + n*n + n*n*n) * block_size();
}
static int block_path(uint64_t index, unsigned *root, unsigned path[3], unsigned *depth) {
    uint64_t n = block_size() / 4, span = n;
    if (index < 12) { *root = index; *depth = 0; return 0; }
    index -= 12;
    for (unsigned level=1;level<=3;level++,span*=n) {
        if (index < span) {
            *root = 11 + level; *depth = level;
            for (unsigned i=level;i;i--) { path[i-1]=index % n; index/=n; }
            return 0;
        }
        index -= span;
    }
    return -K_EFBIG;
}
static int inode_block(const struct ext2_inode *inode, uint64_t index, uint32_t *out) {
    unsigned root, path[3], depth;
    int result=block_path(index,&root,path,&depth);
    if (result) return result;
    uint32_t block=inode->i_block[root];
    uint32_t *p=depth ? kmalloc(block_size()) : 0;
    if (depth && !p) return -K_ENOMEM;
    for (unsigned i=0;i<depth && block;i++) {
        if(ext2_read_block(block,p)<0) { kfree(p); return -K_EIO; }
        block=p[path[i]];
    }
    kfree(p); *out=block; return 0;
}
static int inode_ensure_block(struct ext2_inode *inode, uint64_t index, uint32_t *out) {
    unsigned root,path[3],depth;
    int result=block_path(index,&root,path,&depth);
    if(result) return result;
    uint32_t block=inode->i_block[root], parent=0;
    unsigned missing=0;
    uint32_t *p=kmalloc(block_size());
    if(!p) return -K_ENOMEM;
    for (;missing<depth && block;missing++) {
        parent=block;
        if(ext2_read_block(block,p)<0) { result=-K_EIO; goto done; }
        block=p[path[missing]];
    }
    if(block) { *out=block; result=0; goto done; }
    /* Build the entire missing subtree privately before publishing its root. */
    unsigned count=depth-missing+1, allocated=0;
    uint32_t fresh[4];
    if(inode->i_blocks > UINT32_MAX-count*(block_size()/512)) { result=-K_EFBIG; goto done; }
    for(;allocated<count;allocated++) {
        if(ext2_alloc_block(&fresh[allocated])<0) { result=-K_ENOSPC; goto rollback; }
    }
    for(unsigned i=0;i+1<count;i++) {
        kmemset(p,0,block_size()); p[path[missing+i]]=fresh[i+1];
        if(ext2_write_block(fresh[i],p)<0) { result=-K_EIO; goto rollback; }
    }
    if(parent) {
        if(ext2_read_block(parent,p)<0) { result=-K_EIO; goto rollback; }
        p[path[missing-1]]=fresh[0];
        if(ext2_write_block(parent,p)<0) { result=-K_EIO; goto rollback; }
    } else inode->i_block[root]=fresh[0];
    inode->i_blocks += count*(block_size()/512);
    *out=fresh[count-1]; result=0; goto done;
rollback:
    while(allocated) ext2_free_block(fresh[--allocated]);
done:
    kfree(p); return result;
}
static int free_tree(uint32_t block, unsigned depth) {
    if(!block) return 0;
    if(depth) {
        uint32_t *p=kmalloc(block_size());
        if(!p) return -K_ENOMEM;
        if(ext2_read_block(block,p)<0) { kfree(p); return -K_EIO; }
        for(unsigned i=0;i<block_size()/4;i++) if(p[i]) {
            int result=free_tree(p[i],depth-1);
            if(result) { kfree(p); return result; }
        }
        kfree(p);
    }
    return ext2_free_block(block)<0 ? -K_EIO : 0;
}
static int inode_truncate(uint32_t ino, struct ext2_inode *inode) {
    if(inode->i_file_acl) return -K_ENOSYS;
    for(unsigned i=0;i<15;i++) if(inode->i_block[i]) {
        int result=free_tree(inode->i_block[i],i<12?0:i-11);
        if(result) return result;
        inode->i_block[i]=0;
    }
    inode_set_size(inode,0); inode->i_blocks=0;
    return ext2_write_inode(ino,inode)<0 ? -K_EIO : 0;
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

/* Initial rename API: regular files within one directory. Stage all directory
 * edits in memory so ordinary validation/allocation errors leave names intact.
 * As with other ext2 mutations, disk failure/crash recovery is not journaled. */
static int rename_parent(char *path, uint32_t *parent, const char **name) {
    size_t length = kstrlen(path), start = length;
    if (!length) return -K_ENOENT;
    if (path[length - 1] == '/') return -K_EINVAL;
    while (start && path[start - 1] != '/') start--;
    *name = path + start;
    if (!kstrcmp(*name, ".") || !kstrcmp(*name, "..")) return -K_EINVAL;
    if (length - start > EXT2_NAME_MAX) return -K_ENAMETOOLONG;
    if (start) path[start - 1] = 0;
    return resolve_path(start && *path ? path : "/", 0, parent);
}

int ext2_rename(char *old_path, char *new_path) {
    uint32_t parent_no, other_parent, source_no, target_no = 0;
    const char *old_name, *new_name;
    struct ext2_inode parent, source, target;
    int result = rename_parent(old_path, &parent_no, &old_name);
    if (result < 0) return result;
    result = rename_parent(new_path, &other_parent, &new_name);
    if (result < 0) return result;
    if (parent_no != other_parent) return -K_EXDEV;
    result = lookup(parent_no, old_name, &source_no);
    if (result < 0) return result;
    if (ext2_read_inode(source_no, &source) < 0) return -K_EIO;
    if ((source.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG) return -K_EPERM;
    result = lookup(parent_no, new_name, &target_no);
    if (result < 0 && result != -K_ENOENT) return result;
    if (target_no == source_no) return 0;
    if (target_no) {
        if (ext2_read_inode(target_no, &target) < 0) return -K_EIO;
        if ((target.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG) return -K_EPERM;
        if (!target.i_links_count) return -K_EIO;
        if (target.i_file_acl) return -K_ENOSYS;
    }
    if (ext2_read_inode(parent_no, &parent) < 0) return -K_EIO;
    uint32_t bs = block_size(), size = parent.i_size_lo;
    if (!size || size % bs) return -K_EIO;
    uint8_t *before = kmalloc(size), *after = kmalloc(size);
    if (!before || !after) { kfree(before); kfree(after); return -K_ENOMEM; }
    struct ext2_dir_entry_2 *src = NULL, *dst = NULL;
    result = -K_EIO;
    for (uint32_t base = 0; base < size; base += bs) {
        uint32_t block;
        if (inode_block(&parent, base / bs, &block) < 0 || !block ||
            ext2_read_block(block, before + base) < 0) goto done;
    }
    kmemcpy(after, before, size);
    for (uint32_t offset = 0; offset < size;) {
        if (bs - offset % bs < 8) goto done;
        struct ext2_dir_entry_2 *de = (void *)(after + offset);
        if (de->rec_len < 8 || (de->rec_len & 3) ||
            de->rec_len > bs - offset % bs || de->name_len > de->rec_len - 8) goto done;
        if (de->inode == source_no && de->name_len == kstrlen(old_name) &&
            !kmemcmp(de->name, old_name, de->name_len)) src = de;
        if (target_no && de->inode == target_no && de->name_len == kstrlen(new_name) &&
            !kmemcmp(de->name, new_name, de->name_len)) dst = de;
        offset += de->rec_len;
    }
    if (!src || (target_no && !dst)) goto done;
    if (!dst) {
        /* The editor's temporary name is longer than its destination name. */
        if (src->rec_len < dir_rec_size(kstrlen(new_name))) { result = -K_ENOSPC; goto done; }
        src->name_len = kstrlen(new_name);
        kmemcpy(src->name, new_name, src->name_len);
    } else {
        dst->inode = source_no;
        dst->file_type = EXT2_FT_REG_FILE;
        src->inode = 0; src->name_len = 0; src->file_type = 0;
    }
    for (uint32_t base = 0; base < size; base += bs) {
        uint32_t block;
        if (!kmemcmp(before + base, after + base, bs)) continue;
        if (inode_block(&parent, base / bs, &block) < 0 ||
            ext2_write_block(block, after + base) < 0) {
            /* Best-effort rollback on an I/O error, including the failed block. */
            for (uint32_t rollback = 0; rollback <= base; rollback += bs) {
                if (inode_block(&parent, rollback / bs, &block) == 0)
                    ext2_write_block(block, before + rollback);
            }
            result = -K_EIO; goto done;
        }
    }
    result = 0;
    if (target_no) {
        target.i_links_count--;
        if (ext2_write_inode(target_no, &target) < 0) { result = -K_EIO; goto done; }
        result = ext2_reclaim_unlinked(target_no);
    }
done:
    kfree(before); kfree(after);
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
    if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
        result = resolve_path(path, 0, &ino);
        if (!result) return -K_EEXIST;
        if (result != -K_ENOENT) return result;
    }
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
        uint32_t block, offset = file->pos % bs;
        uint64_t index = file->pos / bs;
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
    if (file->pos >= maximum_file_size() || count > maximum_file_size()-file->pos) return -K_EFBIG;
    if (file->pos + count > INT32_MAX && ext2_enable_large_files()<0) return -K_EIO;
    tmp = kmalloc(bs); if (!tmp) return -K_ENOMEM;
    while (total < count) {
        uint32_t block, offset = file->pos % bs;
        uint64_t index = file->pos / bs;
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
        inode_set_size(&file->inode,file->size);
        if(ext2_write_inode(file->ino,&file->inode)<0) { kfree(tmp); return -K_EIO; }
    }
    inode_set_size(&file->inode, file->size);
    if (ext2_write_inode(file->ino, &file->inode) < 0) { kfree(tmp); return total ? (int)total : -K_EIO; }
    kfree(tmp); return total;
}

int64_t ext2_file_seek(ext2_file_t *file, int64_t offset, int whence) {
    int64_t base, next;
    if (!file || !file->used) return -K_EBADF;
    if (file_refresh(file) < 0) return -K_EIO;
    if (file->size > INT64_MAX) return -K_EFBIG;
    if (whence == 0) base = 0; else if (whence == 1) base = file->pos;
    else if (whence == 2) base = file->size; else return -K_EINVAL;
    if ((offset > 0 && base > INT64_MAX-offset) ||
        (offset < 0 && offset < -base)) return -K_EINVAL;
    next = base + offset;
    if (next < 0) return -K_EINVAL;
    file->pos = next; return file->pos;
}

int ext2_file_read_dir(ext2_file_t *file, char *name, size_t name_size) {
    uint32_t bs = block_size(); uint8_t *buf;
    if (!file || !file->used || !name || !name_size) return -K_EINVAL;
    if (file_refresh(file) < 0) return -K_EIO;
    if (!ext2_file_is_dir(file)) return -K_ENOTDIR;
    buf = kmalloc(bs); if (!buf) return -K_ENOMEM;
    while (file->pos < file->size) {
        uint32_t block, offset = file->pos % bs;
        uint64_t index = file->pos / bs;
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
