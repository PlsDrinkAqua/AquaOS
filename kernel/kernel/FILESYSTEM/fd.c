#include <libk/string.h>
#include <kernel/errno.h>
#include <kernel/fd.h>
#include <kernel/task.h>
#include <kernel/tty.h>

#define OPEN_FILE_MAX 64
#define PIPE_MAX 16
#define PIPE_CAPACITY 512
#define O_ACCMODE 3
#define O_WRONLY 1
#define O_APPEND 0x400

typedef struct pipe_object {
    uint8_t used;
    uint16_t readers, writers;
    size_t head, count;
    char data[PIPE_CAPACITY];
} pipe_object_t;

struct open_file {
    uint8_t used;
    uint16_t refs;
    fd_type_t type;
    int flags;
    union { ext2_file_t ext2; pipe_object_t *pipe; } object;
};

static open_file_t open_files[OPEN_FILE_MAX];
static pipe_object_t pipes[PIPE_MAX];

static open_file_t *open_file_alloc(fd_type_t type) {
    for (int i = 0; i < OPEN_FILE_MAX; i++) if (!open_files[i].used) {
        kmemset(&open_files[i], 0, sizeof(open_files[i]));
        open_files[i].used = 1;
        open_files[i].refs = 1;
        open_files[i].type = type;
        return &open_files[i];
    }
    return NULL;
}

static pipe_object_t *pipe_alloc(void) {
    for (int i = 0; i < PIPE_MAX; i++) if (!pipes[i].used) {
        kmemset(&pipes[i], 0, sizeof(pipes[i]));
        pipes[i].used = 1;
        return &pipes[i];
    }
    return NULL;
}

int fd_inode_is_open(uint32_t ino) {
    for (int i = 0; i < OPEN_FILE_MAX; i++) {
        open_file_t *file = &open_files[i];
        if (file->used && file->refs && file->type == FD_EXT2 &&
            file->object.ext2.used && file->object.ext2.ino == ino) return 1;
    }
    return 0;
}

static int open_file_put(open_file_t *file) {
    pipe_object_t *pipe;
    int result = 0;
    task_preemption_disable();
    if (!file || !file->used || file->refs == 0 || --file->refs != 0) {
        task_preemption_enable();
        return 0;
    }
    if (file->type == FD_EXT2 && file->object.ext2.used) {
        result = ext2_reclaim_unlinked(file->object.ext2.ino);
    }
    if (file->type == FD_PIPE_READ || file->type == FD_PIPE_WRITE) {
        pipe = file->object.pipe;
        if (pipe && pipe->used) {
            if (file->type == FD_PIPE_READ && pipe->readers) pipe->readers--;
            if (file->type == FD_PIPE_WRITE && pipe->writers) pipe->writers--;
            task_wake_pipe(pipe);
            if (!pipe->readers && !pipe->writers) kmemset(pipe, 0, sizeof(*pipe));
        }
    }
    kmemset(file, 0, sizeof(*file));
    task_preemption_enable();
    return result;
}

static fd_entry_t *fd_get(int fd) {
    task_t *task = task_current();
    if (!task || fd < 0 || fd >= TASK_FD_MAX || task->fds[fd].type == FD_NONE) return NULL;
    return &task->fds[fd];
}

static int fd_find_free(task_t *task, int start) {
    for (int fd = start; fd < TASK_FD_MAX; fd++)
        if (task->fds[fd].type == FD_NONE) return fd;
    return -1;
}

void fd_table_init(struct task *task) {
    if (!task) return;
    kmemset(task->fds, 0, sizeof(task->fds));
    task->fds[0].type = FD_STDIN;
    task->fds[1].type = FD_STDOUT;
    task->fds[2].type = FD_STDERR;
}

int fd_table_clone(struct task *dest, const struct task *source) {
    if (!dest || !source) return -K_EINVAL;
    kmemcpy(dest->fds, source->fds, sizeof(dest->fds));
    for (int fd = 0; fd < TASK_FD_MAX; fd++)
        if (dest->fds[fd].open) dest->fds[fd].open->refs++;
    return 0;
}

void fd_table_close_all(struct task *task) {
    if (!task) return;
    for (int fd = 0; fd < TASK_FD_MAX; fd++) {
        open_file_put(task->fds[fd].open);
        kmemset(&task->fds[fd], 0, sizeof(task->fds[fd]));
    }
}

fd_type_t fd_type(int fd) { fd_entry_t *e = fd_get(fd); return e ? e->type : FD_NONE; }

void *fd_wait_object(int fd) {
    fd_entry_t *e = fd_get(fd);
    if (!e || !e->open || (e->type != FD_PIPE_READ && e->type != FD_PIPE_WRITE)) return NULL;
    return e->open->object.pipe;
}

int fd_open(const char *path, int flags) {
    task_t *task = task_current();
    open_file_t *file;
    int fd, result;
    if (!task || !path || (flags & O_ACCMODE) == O_ACCMODE) return -K_EINVAL;
    char absolute[TASK_IMAGE_PATH_BYTES];
    result = task_resolve_path(path, absolute, sizeof(absolute));
    if (result < 0) return result;
    fd = fd_find_free(task, 3);
    if (fd < 0) return -K_EMFILE;
    file = open_file_alloc(FD_EXT2);
    if (!file) return -K_ENFILE;
    file->flags = flags;
    task_preemption_disable();
    result = ext2_file_open(absolute, flags, &file->object.ext2);
    task_preemption_enable();
    if (result < 0) { open_file_put(file); return result; }
    task->fds[fd].type = FD_EXT2;
    task->fds[fd].open = file;
    return fd;
}

int fd_close(int fd) {
    fd_entry_t *e = fd_get(fd);
    if (!e) return -K_EBADF;
    int result = open_file_put(e->open);
    kmemset(e, 0, sizeof(*e));
    return result;
}

int fd_dup(int oldfd) {
    task_t *task = task_current();
    fd_entry_t *source = fd_get(oldfd);
    int newfd;
    if (!source) return -K_EBADF;
    newfd = fd_find_free(task, 0);
    if (newfd < 0) return -K_EMFILE;
    task->fds[newfd] = *source;
    if (source->open) source->open->refs++;
    return newfd;
}

int fd_dup2(int oldfd, int newfd) {
    task_t *task = task_current();
    fd_entry_t *source = fd_get(oldfd);
    if (!source || newfd < 0 || newfd >= TASK_FD_MAX) return -K_EBADF;
    if (oldfd == newfd) return newfd;
    if (task->fds[newfd].type != FD_NONE) fd_close(newfd);
    task->fds[newfd] = *source;
    if (source->open) source->open->refs++;
    return newfd;
}

int fd_pipe(int pipefd[2]) {
    task_t *task = task_current();
    pipe_object_t *pipe;
    open_file_t *reader, *writer;
    int rfd, wfd;
    if (!task || !pipefd) return -K_EINVAL;
    rfd = fd_find_free(task, 0);
    if (rfd < 0) return -K_EMFILE;
    task->fds[rfd].type = FD_STDERR;
    wfd = fd_find_free(task, 0);
    task->fds[rfd].type = FD_NONE;
    if (wfd < 0) return -K_EMFILE;
    pipe = pipe_alloc(); reader = open_file_alloc(FD_PIPE_READ); writer = open_file_alloc(FD_PIPE_WRITE);
    if (!pipe || !reader || !writer) {
        if (reader) open_file_put(reader);
        if (writer) open_file_put(writer);
        if (pipe) kmemset(pipe, 0, sizeof(*pipe));
        return -K_ENFILE;
    }
    pipe->readers = pipe->writers = 1;
    reader->object.pipe = writer->object.pipe = pipe;
    task->fds[rfd].type = FD_PIPE_READ; task->fds[rfd].open = reader;
    task->fds[wfd].type = FD_PIPE_WRITE; task->fds[wfd].open = writer;
    pipefd[0] = rfd; pipefd[1] = wfd;
    return 0;
}

int fd_read(int fd, void *buf, size_t count) {
    fd_entry_t *e = fd_get(fd);
    int result = 0;
    if (!e) return -K_EBADF;
    if (e->type == FD_PIPE_READ) {
        pipe_object_t *pipe = e->open->object.pipe;
        if (!buf && count) return -K_EFAULT;
        if (!count) return 0;
        if (!pipe->count) return pipe->writers ? -K_EAGAIN : 0;
        while ((size_t)result < count && pipe->count) {
            ((char *)buf)[result++] = pipe->data[pipe->head];
            pipe->head = (pipe->head + 1) % PIPE_CAPACITY; pipe->count--;
        }
        task_wake_pipe(pipe);
        return result;
    }
    if (e->type != FD_EXT2 || !e->open || (e->open->flags & O_ACCMODE) == O_WRONLY) return -K_EBADF;
    task_preemption_disable();
    result = ext2_file_read(&e->open->object.ext2, buf, count);
    task_preemption_enable();
    return result;
}

int fd_write(int fd, const void *buf, size_t count) {
    fd_entry_t *e = fd_get(fd);
    int result = 0;
    if (!e) return -K_EBADF;
    if (e->type == FD_STDOUT || e->type == FD_STDERR) {
        if (!buf && count) return -K_EFAULT;
        task_preemption_disable();
        for (size_t i = 0; i < count; i++) terminal_putchar(((const char *)buf)[i]);
        task_preemption_enable();
        return (int)count;
    }
    if (e->type == FD_PIPE_WRITE) {
        pipe_object_t *pipe = e->open->object.pipe;
        if (!buf && count) return -K_EFAULT;
        if (!pipe->readers) return -K_EPIPE;
        if (!count) return 0;
        if (pipe->count == PIPE_CAPACITY) return -K_EAGAIN;
        while ((size_t)result < count && pipe->count < PIPE_CAPACITY) {
            size_t tail = (pipe->head + pipe->count) % PIPE_CAPACITY;
            pipe->data[tail] = ((const char *)buf)[result++]; pipe->count++;
        }
        task_wake_pipe(pipe);
        return result;
    }
    if (e->type != FD_EXT2 || !e->open || (e->open->flags & O_ACCMODE) == 0) return -K_EBADF;
    task_preemption_disable();
    result = ext2_file_write(&e->open->object.ext2, buf, count,
                             (e->open->flags & O_APPEND) != 0);
    task_preemption_enable();
    return result;
}

int fd_seek(int fd, int32_t offset, int whence) {
    fd_entry_t *e = fd_get(fd); int result;
    if (!e) return -K_EBADF;
    if (e->type != FD_EXT2 || !e->open) return -K_ESPIPE;
    task_preemption_disable(); result = ext2_file_seek(&e->open->object.ext2, offset, whence); task_preemption_enable();
    return result;
}

int fd_read_dir(int fd, char *name, size_t size) {
    fd_entry_t *e = fd_get(fd); int result;
    if (!e || e->type != FD_EXT2 || !e->open) return -K_EBADF;
    task_preemption_disable(); result = ext2_file_read_dir(&e->open->object.ext2, name, size); task_preemption_enable();
    return result;
}
