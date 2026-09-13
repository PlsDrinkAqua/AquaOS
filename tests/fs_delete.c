#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL fscheck line %d (errno=%d)\n", __LINE__, errno); exit(1); \
} } while (0)
#define ERROR(expr, code) do { errno = 0; CHECK((expr) == -1 && errno == (code)); } while (0)

static void check_contents(int fd, const char *expected) {
    char data[32];
    CHECK(lseek(fd, 0, SEEK_SET) == 0);
    int n = read(fd, data, sizeof(data) - 1);
    CHECK(n >= 0);
    data[n] = 0;
    CHECK(strcmp(data, expected) == 0);
}

int main(void) {
    ERROR(unlink((const char *)0), EFAULT);
    ERROR(rmdir((const char *)0), EFAULT);
    ERROR(unlink(""), ENOENT);
    ERROR(rmdir(""), ENOENT);
    ERROR(unlink("/absent"), ENOENT);
    ERROR(rmdir("/absent"), ENOENT);
    ERROR(rmdir("/"), EBUSY);
    ERROR(rmdir("///"), EBUSY);
    ERROR(rmdir("/."), EINVAL);
    ERROR(rmdir("/.."), EINVAL);
    ERROR(unlink("/fscheck-oversized"), EFBIG);
    ERROR(unlink("/fscheck-symlink"), EPERM);
    int linked = open("/fscheck-link-a", O_RDWR | O_APPEND);
    CHECK(linked >= 0 && unlink("/fscheck-link-a") == 0);
    CHECK(write(linked, "!", 1) == 1 && close(linked) == 0);
    linked = open("/fscheck-link-b", O_RDONLY);
    CHECK(linked >= 0);
    check_contents(linked, "linked!");
    CHECK(unlink("/fscheck-link-b") == 0);
    check_contents(linked, "linked!");
    CHECK(close(linked) == 0);
    CHECK(mkdir("/fscheck-dir") == 0);
    ERROR(unlink("/fscheck-dir"), EISDIR);
    CHECK(chdir("/fscheck-dir") == 0);
    ERROR(rmdir("."), EINVAL);
    ERROR(rmdir("../fscheck-dir//"), EBUSY);
    CHECK(chdir("/") == 0);
    int dir = open("/fscheck-dir", O_RDONLY);
    CHECK(dir >= 0);
    ERROR(rmdir("/fscheck-dir"), EBUSY);
    CHECK(close(dir) == 0);

    int a = open("/fscheck-dir/file", O_CREAT | O_RDWR);
    CHECK(a >= 0 && write(a, "old", 3) == 3);
    int b = open("/fscheck-dir/file", O_RDWR | O_APPEND);
    int copy = dup(a);
    CHECK(b >= 0 && copy >= 0);
    ERROR(rmdir("/fscheck-dir"), ENOTEMPTY);
    ERROR(rmdir("/fscheck-dir/file"), ENOTDIR);
    ERROR(unlink("/fscheck-dir/file/"), ENOTDIR);
    ERROR(unlink("/fscheck-dir/file/../file"), ENOTDIR);
    CHECK(unlink("/fscheck-dir/file") == 0);
    ERROR(open("/fscheck-dir/file", O_RDONLY), ENOENT);
    CHECK(write(b, "!", 1) == 1);
    check_contents(a, "old!");
    CHECK(close(a) == 0 && close(b) == 0);
    int replacement = open("/fscheck-dir/file", O_CREAT | O_RDWR);
    CHECK(replacement >= 0 && write(replacement, "new", 3) == 3);
    check_contents(copy, "old!");
    CHECK(close(copy) == 0);
    check_contents(replacement, "new");
    CHECK(close(replacement) == 0);
    CHECK(unlink("/fscheck-dir/file") == 0);

    /* Inherited descriptors survive unlink and the parent's close. */
    int ready[2], resume[2];
    CHECK(pipe(ready) == 0 && pipe(resume) == 0);
    a = open("/fscheck-dir/inherited", O_CREAT | O_RDWR);
    CHECK(a >= 0 && write(a, "child", 5) == 5);
    int pid = fork();
    CHECK(pid >= 0);
    if (!pid) {
        CHECK(close(ready[0]) == 0 && close(resume[1]) == 0);
        CHECK(chdir("/fscheck-dir") == 0);
        CHECK(write(ready[1], "r", 1) == 1);
        char token;
        CHECK(read(resume[0], &token, 1) == 1);
        check_contents(a, "child");
        /* exit must release the last reference, including dup aliases. */
        CHECK(dup(a) >= 0);
        return 0;
    }
    CHECK(close(ready[1]) == 0 && close(resume[0]) == 0);
    char token;
    CHECK(read(ready[0], &token, 1) == 1);
    CHECK(unlink("/fscheck-dir/inherited") == 0 && close(a) == 0);
    ERROR(rmdir("/fscheck-dir"), EBUSY);
    CHECK(write(resume[1], "r", 1) == 1);
    CHECK(wait(pid) == 0);
    CHECK(close(ready[0]) == 0 && close(resume[1]) == 0);

    /* Exercise direct and single-indirect reclamation, including sparse files. */
    char block[1024];
    for (unsigned i = 0; i < sizeof(block); i++) block[i] = 'x';
    for (int pass = 0; pass < 3; pass++) {
        a = open("/fscheck-dir/large", O_CREAT | O_RDWR);
        CHECK(a >= 0);
        for (int i = 0; i < 80; i++) CHECK(write(a, block, sizeof(block)) == sizeof(block));
        CHECK(close(a) == 0 && unlink("/fscheck-dir/large") == 0);
        a = open("/fscheck-dir/sparse", O_CREAT | O_RDWR);
        CHECK(a >= 0 && lseek(a, 80000, SEEK_SET) == 80000);
        CHECK(write(a, "s", 1) == 1);
        CHECK(unlink("/fscheck-dir/sparse") == 0 && close(a) == 0);
    }

    /* Remove first/middle/last entries across directory block boundaries. */
    char path[] = "/fscheck-dir/entry000";
    for (int i = 0; i < 300; i++) {
        path[18] = '0' + i / 100;
        path[19] = '0' + i / 10 % 10;
        path[20] = '0' + i % 10;
        a = open(path, O_CREAT | O_RDWR);
        CHECK(a >= 0 && close(a) == 0);
    }
    for (int parity = 0; parity < 2; parity++) {
        for (int i = parity; i < 300; i += 2) {
            path[18] = '0' + i / 100;
            path[19] = '0' + i / 10 % 10;
            path[20] = '0' + i % 10;
            CHECK(unlink(path) == 0);
        }
    }
    CHECK(rmdir("/fscheck-dir//") == 0);
    ERROR(open("/fscheck-dir", O_RDONLY), ENOENT);
    CHECK(mkdir("/fscheck-dir") == 0 && rmdir("/fscheck-dir") == 0);
    puts("filesystem deletion checks passed");
    return 0;
}
