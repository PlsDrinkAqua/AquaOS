#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void check(int ok, const char *message) {
    if (!ok) { printf("mkdirtest: FAIL: %s (errno=%d)\n", message, errno); exit(1); }
}

int main(void) {
    char cwd[256], name[256], data[4];
    check(mkdir((const char *)0xC0000000U) == -1 && errno == EFAULT, "bad pointer");
    check(mkdir("") == -1 && errno == ENOENT, "empty path");
    check(mkdir("/") == -1 && errno == EEXIST, "root exists");
    check(mkdir("/hello.txt") == -1 && errno == EEXIST, "file exists");
    check(mkdir("/hello.txt/child") == -1 && errno == ENOTDIR, "file parent");
    check(mkdir("/absent/child") == -1 && errno == ENOENT, "missing parent");
    check(mkdir("/made") == 0, "create");
    check(mkdir("/made") == -1 && errno == EEXIST, "duplicate");
    check(chdir("/made") == 0, "enter new directory");
    check(mkdir(".") == -1 && errno == EEXIST, "dot exists");
    check(mkdir("..") == -1 && errno == EEXIST, "parent exists");
    check(mkdir("child///") == 0, "relative trailing slash");
    check(chdir("child") == 0, "enter child");
    check(getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/made/child") == 0, "child cwd");
    int fd = open("note", O_CREAT | O_WRONLY);
    check(fd >= 0 && write(fd, "data", 4) == 4, "write inside directory");
    close(fd);
    check(chdir("../..") == 0, "dotdot traversal");
    fd = open("/made/child/note", O_RDONLY);
    check(fd >= 0 && read(fd, data, 4) == 4 && strncmp(data, "data", 4) == 0, "read created file");
    close(fd);
    fd = open("/made/child", O_RDONLY);
    int dot = 0, dotdot = 0, note = 0, result;
    check(fd >= 0, "open directory");
    while ((result = readdir(fd, name, sizeof(name))) > 0) {
        if (!strcmp(name, ".")) dot++;
        if (!strcmp(name, "..")) dotdot++;
        if (!strcmp(name, "note")) note++;
    }
    check(result == 0 && dot == 1 && dotdot == 1 && note == 1, "directory entries");
    close(fd);
    /* Long names force the parent directory to allocate additional blocks. */
    check(chdir("/made") == 0, "enter parent");
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 100; j++) name[j] = 'a' + i;
        name[100] = 0;
        check(mkdir(name) == 0, "grow directory");
        check(chdir(name) == 0 && chdir("..") == 0, "grown directory traversal");
    }
    check(chdir("/") == 0, "return to root");
    puts("mkdirtest: PASS");
    return 0;
}
