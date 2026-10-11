/* Shared file-only cp/mv operations. Stage copies before replacing a name. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FILE_PATH_MAX 256

static int absolute_path(const char *path, char output[FILE_PATH_MAX]) {
    char joined[512], cwd[FILE_PATH_MAX];
    if (!*path) { errno = ENOENT; return -1; }
    if (*path == '/') {
        if (strlen(path) >= sizeof(joined)) { errno = ENAMETOOLONG; return -1; }
        strcpy(joined, path);
    } else {
        if (!getcwd(cwd, sizeof(cwd))) return -1;
        if (snprintf(joined, sizeof(joined), "%s/%s", cwd, path) >= (int)sizeof(joined)) {
            errno = ENAMETOOLONG; return -1;
        }
    }
    size_t length = 1;
    output[0] = '/'; output[1] = 0;
    for (const char *p = joined; *p;) {
        while (*p == '/') p++;
        const char *start = p;
        while (*p && *p != '/') p++;
        size_t n = (size_t)(p - start);
        if (!n || (n == 1 && start[0] == '.')) continue;
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            while (length > 1 && output[length - 1] != '/') length--;
            if (length > 1) length--;
            output[length] = 0;
            continue;
        }
        if (length + (length > 1) + n >= FILE_PATH_MAX) { errno = ENAMETOOLONG; return -1; }
        if (length > 1) output[length++] = '/';
        memcpy(output + length, start, n); length += n; output[length] = 0;
    }
    return 0;
}

static int directory(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    char entry[256];
    int result = readdir(fd, entry, sizeof(entry));
    int error = errno;
    if (close(fd) < 0) return -1;
    if (result >= 0) return 1;
    if (error == ENOTDIR) return 0;
    errno = error; return -1;
}

static int file_paths(const char *old, const char *new, char source[256], char target[256]) {
    if (absolute_path(old, source) < 0 || absolute_path(new, target) < 0) return -1;
    int result = directory(source);
    if (result != 0) { if (result > 0) errno = EISDIR; return -1; }
    /* Preserve the requirement that a trailing slash names a directory. */
    if (old[strlen(old) - 1] == '/') { errno = ENOTDIR; return -1; }
    result = directory(target);
    if (result < 0) return -1;
    if (result) {
        const char *name = strrchr(source, '/') + 1;
        size_t length = strlen(target), n = strlen(name);
        if (length + (length > 1) + n >= 256) { errno = ENAMETOOLONG; return -1; }
        if (length > 1) target[length++] = '/';
        memcpy(target + length, name, n + 1);
    } else if (new[strlen(new) - 1] == '/') { errno = ENOTDIR; return -1; }
    return 0;
}

static int copy_file(const char *source, const char *target) {
    int input = open(source, O_RDONLY);
    if (input < 0) return -1;
    char temporary[256], buffer[4096];
    int output = -1, error = 0;
    for (unsigned attempt = 0; attempt < 32; attempt++) {
        if (snprintf(temporary, sizeof(temporary), "%s.copy-%u", target, attempt) >= (int)sizeof(temporary)) {
            error = ENAMETOOLONG; goto failed;
        }
        output = open(temporary, O_WRONLY | O_CREAT | O_EXCL);
        if (output >= 0) break;
        if (errno != EEXIST) { error = errno; goto failed; }
    }
    if (output < 0) { error = EEXIST; goto failed; }
    for (;;) {
        int n = read(input, buffer, sizeof(buffer));
        if (n < 0) { error = errno; goto failed; }
        if (!n) break;
        int done = 0;
        while (done < n) {
            int written = write(output, buffer + done, (size_t)(n - done));
            if (written <= 0) { error = written < 0 ? errno : EIO; goto failed; }
            done += written;
        }
    }
    if (close(input) < 0) { input = -1; error = errno; goto failed; }
    input = -1;
    if (close(output) < 0) { output = -1; error = errno; unlink(temporary); goto failed; }
    output = -1;
    if (rename(temporary, target) < 0) { error = errno; unlink(temporary); goto failed; }
    return 0;
failed:
    if (input >= 0) close(input);
    if (output >= 0) { close(output); unlink(temporary); }
    errno = error; return -1;
}
