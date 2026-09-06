#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void _start(void) {
    char buf[16];
    char name[256];
    int files[13];
    int found = 0;
    int fd = open("hello.txt", O_RDONLY);

    if (fd < 3 || read(fd, buf, 5) != 5 || strncmp(buf, "hello", 5) != 0) {
        puts("fdtest: open/read failed");
        exit(1);
    }
    if (lseek(fd, 0, SEEK_SET) != 0 || read(fd, buf, 5) != 5 ||
        strncmp(buf, "hello", 5) != 0) {
        puts("fdtest: lseek failed");
        exit(2);
    }
    if (close(fd) < 0 || read(fd, buf, 1) != -1) {
        puts("fdtest: close failed");
        exit(3);
    }
    fd = open("subdir/bar.dat", O_RDONLY);
    if (fd < 3 || read(fd, buf, 4) != 4 || strncmp(buf, "foo\n", 4) != 0) {
        puts("fdtest: relative nested path failed");
        exit(4);
    }
    close(fd);
    if (close(-1) != -1 || open("/missing", O_RDONLY) != -1) {
        puts("fdtest: invalid operation accepted");
        exit(5);
    }

    for (int i = 0; i < 13; i++) {
        files[i] = open("/hello.txt", O_RDONLY);
        if (files[i] < 0) {
            puts("fdtest: descriptor table filled too early");
            exit(6);
        }
    }
    if (open("/hello.txt", O_RDONLY) != -1) {
        puts("fdtest: descriptor limit not enforced");
        exit(7);
    }
    for (int i = 0; i < 13; i++) close(files[i]);

    fd = open("/", O_RDONLY);
    if (fd < 0) {
        puts("fdtest: root open failed");
        exit(8);
    }
    while (readdir(fd, name, sizeof(name)) > 0) {
        if (strcmp(name, "hello.txt") == 0) found = 1;
    }
    close(fd);
    if (!found) {
        puts("fdtest: directory entry not found");
        exit(9);
    }

    puts("fdtest: PASS");
    exit(0);
}
