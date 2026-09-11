#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fail(const char *message, int status) {
    printf("unixtest: FAIL: %s (errno=%d)\n", message, errno);
    exit(status);
}

int main(void) {
    char buf[256];
    int pair[2];
    int value = 41;
    int fd, copy, pid;

    errno = 0;
    if (open("/does-not-exist", O_RDONLY) != -1 || errno != ENOENT) fail("errno", 1);
    pid = fork();
    if (pid < 0) fail("fork", 2);
    if (pid == 0) { value = 99; exit(value == 99 ? 7 : 8); }
    if (wait(pid) != 7 || value != 41) fail("fork memory/wait", 3);

    fd = open("/hello.txt", O_RDONLY); copy = dup(fd);
    if (fd < 0 || copy < 0 || read(fd, buf, 5) != 5 ||
        read(copy, buf, 2) != 2 || strncmp(buf, ", ", 2) != 0) fail("dup shared offset", 4);
    close(fd); close(copy);

    if (pipe(pair) < 0) fail("pipe", 5);
    pid = fork();
    if (pid < 0) fail("pipe fork", 6);
    if (pid == 0) {
        close(pair[0]);
        if (write(pair[1], "pipe-data", 9) != 9) exit(10);
        close(pair[1]); exit(0);
    }
    close(pair[1]);
    if (read(pair[0], buf, 9) != 9 || strncmp(buf, "pipe-data", 9) != 0) fail("pipe transfer", 7);
    if (read(pair[0], buf, 1) != 0 || wait(pid) != 0) fail("pipe EOF", 8);
    close(pair[0]);

    fd = open("/written.txt", O_CREAT | O_TRUNC | O_RDWR);
    if (fd < 0 || write(fd, "first", 5) != 5 || lseek(fd, 0, SEEK_SET) != 0 ||
        read(fd, buf, 5) != 5 || strncmp(buf, "first", 5) != 0) fail("ext2 create/write/read", 9);
    close(fd);
    fd = open("/written.txt", O_WRONLY | O_APPEND);
    if (fd < 0 || write(fd, "-second", 7) != 7) fail("ext2 append", 10);
    close(fd);
    fd = open("/written.txt", O_RDONLY);
    if (fd < 0 || read(fd, buf, 12) != 12 || strncmp(buf, "first-second", 12) != 0)
        fail("ext2 persisted content", 11);
    close(fd);

    for (int i = 0; i < (int)sizeof(buf); i++) buf[i] = (char)('A' + (i % 26));
    fd = open("/large.txt", O_CREAT | O_TRUNC | O_RDWR);
    if (fd < 0) fail("large ext2 create", 12);
    for (int i = 0; i < 220; i++)
        if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) fail("large ext2 write", 13);
    if (lseek(fd, 50000, SEEK_SET) != 50000 || read(fd, buf + 128, 32) != 32)
        fail("single-indirect ext2 read", 14);
    for (int i = 0; i < 32; i++)
        if (buf[128 + i] != (char)('A' + ((50000 + i) % 256) % 26)) fail("large ext2 data", 15);
    close(fd);
    fd = open("/large.txt", O_WRONLY | O_TRUNC);
    if (fd < 0) fail("large ext2 truncate", 16);
    close(fd);

    if (pipe(pair) < 0) fail("exec pipe", 17);
    pid = fork();
    if (pid < 0) fail("exec fork", 18);
    if (pid == 0) {
        close(pair[0]); dup2(pair[1], 1); close(pair[1]);
        exec("/hello", NULL); exit(19);
    }
    close(pair[1]);
    if (read(pair[0], buf, sizeof(buf)) <= 0) fail("exec descriptor inheritance", 20);
    close(pair[0]);
    if (wait(pid) != 0) fail("exec child", 21);
    puts("unixtest: PASS"); exit(0);
}
