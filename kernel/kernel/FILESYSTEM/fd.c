#include <libk/string.h>

#include <kernel/fd.h>
#include <kernel/task.h>
#include <kernel/tty.h>

void fd_table_init(struct task *task) {
    if (!task) return;
    kmemset(task->fds, 0, sizeof(task->fds));
    task->fds[0].type = FD_STDIN;
    task->fds[1].type = FD_STDOUT;
    task->fds[2].type = FD_STDERR;
}

void fd_table_close_all(struct task *task) {
    if (!task) return;
    kmemset(task->fds, 0, sizeof(task->fds));
}

static fd_entry_t *fd_get(int fd) {
    task_t *task = task_current();
    if (!task || fd < 0 || fd >= TASK_FD_MAX || task->fds[fd].type == FD_NONE) {
        return NULL;
    }
    return &task->fds[fd];
}

fd_type_t fd_type(int fd) {
    fd_entry_t *entry = fd_get(fd);
    return entry ? entry->type : FD_NONE;
}

int fd_open(const char *path, int flags) {
    task_t *task = task_current();
    int fd;

    if (!task || !path || flags != 0) return -1;
    for (fd = 3; fd < TASK_FD_MAX; fd++) {
        if (task->fds[fd].type == FD_NONE) break;
    }
    if (fd == TASK_FD_MAX) return -1;
    task_preemption_disable();
    int result = ext2_file_open(path, &task->fds[fd].file);
    task_preemption_enable();
    if (result < 0) {
        return -1;
    }
    task->fds[fd].type = FD_EXT2;
    return fd;
}

int fd_close(int fd) {
    fd_entry_t *entry = fd_get(fd);
    if (!entry || fd < 3) return -1;
    kmemset(entry, 0, sizeof(*entry));
    return 0;
}

int fd_read(int fd, void *buf, size_t count) {
    fd_entry_t *entry = fd_get(fd);
    if (!entry || entry->type != FD_EXT2) return -1;
    task_preemption_disable();
    int result = ext2_file_read(&entry->file, buf, count);
    task_preemption_enable();
    return result;
}

int fd_write(int fd, const void *buf, size_t count) {
    fd_entry_t *entry = fd_get(fd);
    const char *bytes = buf;

    if (!entry || (entry->type != FD_STDOUT && entry->type != FD_STDERR) ||
        (!buf && count)) return -1;
    task_preemption_disable();
    for (size_t i = 0; i < count; i++) terminal_putchar(bytes[i]);
    task_preemption_enable();
    return (int)count;
}

int fd_seek(int fd, int32_t offset, int whence) {
    fd_entry_t *entry = fd_get(fd);
    if (!entry || entry->type != FD_EXT2) return -1;
    task_preemption_disable();
    int result = ext2_file_seek(&entry->file, offset, whence);
    task_preemption_enable();
    return result;
}

int fd_read_dir(int fd, char *name, size_t name_size) {
    fd_entry_t *entry = fd_get(fd);
    if (!entry || entry->type != FD_EXT2) return -1;
    task_preemption_disable();
    int result = ext2_file_read_dir(&entry->file, name, name_size);
    task_preemption_enable();
    return result;
}
