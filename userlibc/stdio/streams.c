#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include "internal.h"

/* Read-ahead only for seekable files. Terminal/pipe/socket input and all
 * output remain unbuffered. Input positioning is synchronized explicitly. */
#define READ_BUFFER_SIZE 4096U
struct aquaos_file {
    int used, fd, readable, writable, eof, error, pushed, seekable;
    unsigned char *buffer;
    size_t position, end;
};
static FILE files[FOPEN_MAX] = {
    {.used=1,.fd=0,.readable=1,.pushed=EOF},
    {.used=1,.fd=1,.writable=1,.pushed=EOF},
    {.used=1,.fd=2,.writable=1,.pushed=EOF}
};
FILE *stdin = &files[0];
FILE *stdout = &files[1];
FILE *stderr = &files[2];

static int valid(FILE *f) {
    if (!f || !f->used) { errno = EBADF; return 0; }
    return 1;
}
static int fail(FILE *f, int error) { f->error = 1; errno = error; return EOF; }
static FILE *slot(void) {
    for (unsigned i = 0; i < FOPEN_MAX; i++) if (!files[i].used) return &files[i];
    errno = EMFILE; return NULL;
}
static int mode_flags(const char *mode, int *readable, int *writable) {
    if (!mode || (*mode != 'r' && *mode != 'w' && *mode != 'a')) { errno = EINVAL; return -1; }
    char kind = *mode++;
    int plus = 0, binary = 0;
    while (*mode) {
        if (*mode == '+' && !plus) plus = 1;
        else if (*mode == 'b' && !binary) binary = 1;
        else { errno = EINVAL; return -1; }
        mode++;
    }
    *readable = kind == 'r' || plus;
    *writable = kind != 'r' || plus;
    int flags = plus ? O_RDWR : kind == 'r' ? O_RDONLY : O_WRONLY;
    if (kind == 'w') flags |= O_CREAT | O_TRUNC;
    if (kind == 'a') flags |= O_CREAT | O_APPEND;
    return flags;
}
static FILE *attach(FILE *f, int fd, int r, int w) {
    int saved_errno = errno;
    int seekable = r && lseek(fd, 0, SEEK_CUR) >= 0;
    errno = saved_errno;
    *f = (FILE){.used=1,.fd=fd,.readable=r,.writable=w,.pushed=EOF,.seekable=seekable};
    return f;
}
FILE *fopen(const char *path, const char *mode) {
    int r, w, flags = mode_flags(mode, &r, &w);
    if (flags < 0) return NULL;
    FILE *f = slot();
    if (!f) return NULL;
    int fd = open(path, flags);
    if (fd < 0) return NULL;
    return attach(f, fd, r, w);
}
FILE *fdopen(int fd, const char *mode) {
    int r, w, requested = mode_flags(mode, &r, &w);
    if (requested < 0) return NULL;
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return NULL;
    int access = flags & O_ACCMODE;
    if ((r && access == O_WRONLY) || (w && access == O_RDONLY)) { errno = EINVAL; return NULL; }
    FILE *f = slot();
    if (!f) return NULL;
    if ((requested & O_APPEND) && fcntl(fd, F_SETFL, flags | O_APPEND) < 0) return NULL;
    return attach(f, fd, r, w);
}
static size_t unread(FILE *f) {
    return f->end - f->position + (f->pushed != EOF);
}
static int sync_input(FILE *f) {
    if (!f->seekable) return 0;
    size_t pending = unread(f);
    if (pending && lseek(f->fd, -(off_t)pending, SEEK_CUR) < 0) return EOF;
    f->position = f->end = 0;
    f->pushed = EOF;
    return 0;
}
/* Before fork/exec, align shared descriptors to logical stream positions.
 * Each process must still fflush before switching shared active handles. */
int __aquaos_stdio_sync_inputs(void) {
    int result=0, saved_errno=0;
    for (unsigned i=0;i<FOPEN_MAX;i++) {
        if (files[i].used && sync_input(&files[i])<0 && !result) {
            result=-1; saved_errno=errno;
        }
    }
    if (result<0) errno=saved_errno;
    return result;
}
int fclose(FILE *f) {
    if (!valid(f)) return EOF;
    int result=sync_input(f), saved_errno=errno;
    if (close(f->fd)<0) result=EOF;
    else if (result<0) errno=saved_errno;
    free(f->buffer);
    f->buffer=NULL;
    f->used=0;
    return result < 0 ? EOF : 0;
}
int fflush(FILE *f) {
    /* Output is already synchronous. NULL flushes output streams only. */
    if (!f) return 0;
    if (!valid(f)) return EOF;
    return sync_input(f);
}
int fileno(FILE *f) { return valid(f) ? f->fd : -1; }
int feof(FILE *f) { return valid(f) ? f->eof : 0; }
int ferror(FILE *f) { return valid(f) ? f->error : 1; }
void clearerr(FILE *f) { if (valid(f)) f->eof = f->error = 0; }

size_t fread(void *ptr, size_t size, size_t count, FILE *f) {
    if (!valid(f) || !size || !count) return 0;
    if (!f->readable) { fail(f, EBADF); return 0; }
    if (count > SIZE_MAX / size) { fail(f, EOVERFLOW); return 0; }
    if (f->eof) return 0;
    size_t wanted = size * count, done = 0;
    unsigned char *out = ptr;
    if (f->pushed != EOF) { out[done++] = f->pushed; f->pushed = EOF; }
    while (done < wanted) {
        if (f->position < f->end) {
            size_t n=f->end-f->position;
            if(n>wanted-done) n=wanted-done;
            memcpy(out+done,f->buffer+f->position,n);
            f->position+=n; done+=n;
            continue;
        }
        size_t n=wanted-done;
        if (n>INT_MAX) n=INT_MAX;
        if (f->seekable && n<READ_BUFFER_SIZE && !f->buffer) {
            int saved_errno=errno;
            f->buffer=malloc(READ_BUFFER_SIZE);
            errno=saved_errno; /* Allocation failure falls back to direct I/O. */
        }
        if (f->seekable && n<READ_BUFFER_SIZE && f->buffer) {
            int result=read(f->fd,f->buffer,READ_BUFFER_SIZE);
            if (result<0) { f->error=1; break; }
            if (!result) { f->eof=1; break; }
            f->position=0; f->end=(unsigned)result;
        } else {
            int result=read(f->fd,out+done,n);
            if (result<0) { f->error=1; break; }
            if (!result) { f->eof=1; break; }
            done+=(unsigned)result;
        }
    }
    return done / size;
}
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *f) {
    if (!valid(f) || !size || !count) return 0;
    if (!f->writable) { fail(f, EBADF); return 0; }
    if (count > SIZE_MAX / size) { fail(f, EOVERFLOW); return 0; }
    if (sync_input(f)<0) { f->error=1; return 0; }
    size_t wanted = size * count, done = 0;
    while (done < wanted) {
        size_t n = wanted - done;
        if (n > INT_MAX) n = INT_MAX;
        int result = write(f->fd, (const unsigned char *)ptr + done, n);
        if (result < 0) { f->error = 1; break; }
        if (!result) { fail(f, EIO); break; }
        done += (unsigned)result;
    }
    return done / size;
}
int fseek(FILE *f, long offset, int whence) {
    if (!valid(f)) return -1;
    int64_t adjusted = offset;
    if (whence == SEEK_CUR) {
        size_t pending=unread(f);
        if (adjusted < INT64_MIN+(int64_t)pending) { errno=EOVERFLOW; return -1; }
        adjusted-=(int64_t)pending;
    }
    if (lseek(f->fd, (off_t)adjusted, whence) < 0) return -1;
    f->position=f->end=0;
    f->pushed = EOF; f->eof = 0;
    return 0;
}
long ftell(FILE *f) {
    if (!valid(f)) return -1;
    off_t pos = lseek(f->fd, 0, SEEK_CUR);
    if (pos < 0) return -1;
    return (long)pos - (long)unread(f);
}
void rewind(FILE *f) { if (valid(f)) { fseek(f, 0, SEEK_SET); clearerr(f); } }
int fgetc(FILE *f) { unsigned char c; return fread(&c, 1, 1, f) == 1 ? c : EOF; }
int getc(FILE *f) { return fgetc(f); }
int ungetc(int c, FILE *f) {
    if (!valid(f) || !f->readable || c == EOF || f->pushed != EOF) return EOF;
    f->pushed = (unsigned char)c; f->eof = 0;
    return f->pushed;
}
char *fgets(char *buffer, int size, FILE *f) {
    if (size <= 0) { errno = EINVAL; return NULL; }
    if (!valid(f)) return NULL;
    int used = 0;
    while (used + 1 < size) {
        int c = fgetc(f);
        if (c == EOF) {
            if (ferror(f) || !used) return NULL;
            break;
        }
        buffer[used++] = c;
        if (c == '\n') break;
    }
    buffer[used] = 0;
    return buffer;
}
int fputc(int c, FILE *f) { unsigned char byte = c; return fwrite(&byte, 1, 1, f) == 1 ? byte : EOF; }
int putc(int c, FILE *f) { return fputc(c, f); }
int fputs(const char *s, FILE *f) { size_t n = strlen(s); return fwrite(s, 1, n, f) == n ? 0 : EOF; }
int remove(const char *path) {
    if (unlink(path) == 0) return 0;
    return errno == EISDIR || errno == EBUSY ? rmdir(path) : -1;
}
