#ifndef _EXT2_API_H
#define _EXT2_API_H

#include <stdint.h>
#include "kernel/ext2.h"

typedef struct {
    uint32_t ino;
    uint64_t pos;
    uint64_t size;
    struct ext2_inode inode;
    int used;
} ext2_file_t;

#define EXT2_NAME_MAX 255

// 目录项 v2（带 file_type 字段）
struct ext2_dir_entry_2 {
    uint32_t inode;       // 本条目的 inode 号（0 表示空条目）
    uint16_t rec_len;     // 本条目长度（跳到下一个目录项）
    uint8_t  name_len;    // 文件名长度
    uint8_t  file_type;   // 文件类型，见下 enum
    char     name[];      // 文件名内容（不是 NUL 结尾，长度 = name_len）
};

// 文件类型常量（file_type 字段）
enum ext2_file_type {
    EXT2_FT_UNKNOWN   = 0,
    EXT2_FT_REG_FILE  = 1,
    EXT2_FT_DIR       = 2,
    EXT2_FT_CHRDEV    = 3,
    EXT2_FT_BLKDEV    = 4,
    EXT2_FT_FIFO      = 5,
    EXT2_FT_SOCK      = 6,
    EXT2_FT_SYMLINK   = 7,
};

int ext2_init(void);
int ext2_lookup_inode(const char *path, uint32_t *ino);
int ext2_remove(const char *path, int directory);
int ext2_reclaim_unlinked(uint32_t ino);
int ext2_rename(char *old_path, char *new_path);
int ext2_mkdir(const char *path);
void ext2_selftest(void);
int ext2_file_open(const char *path, int flags, ext2_file_t *file);
size_t ext2_file_size(const ext2_file_t *file);
int ext2_file_is_dir(const ext2_file_t *file);
int ext2_file_read(ext2_file_t *file, void *buf, size_t count);
int ext2_file_write(ext2_file_t *file, const void *buf, size_t count, int append);
int64_t ext2_file_seek(ext2_file_t *file, int64_t offset, int whence);
int ext2_file_read_dir(ext2_file_t *file, char *name, size_t name_size);

#endif
