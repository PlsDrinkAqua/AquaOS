#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Keep heap pages and an unlinked open inode live until forced termination. */
int main(int argc, char **argv) {
    char *memory = malloc(128 * 1024);
    int fd = open("/interrupt-live", O_CREAT | O_RDWR | O_TRUNC);
    if (!memory || fd < 0 || write(fd, "alive", 5) != 5 ||
        unlink("/interrupt-live") < 0) {
        puts("FAIL: interrupt setup");
        return 1;
    }
    memset(memory, 0x5a, 128 * 1024);
    const char *mode = argc > 1 ? argv[1] : "spin";
    if (!strcmp(mode, "tree")) {
        for (int i = 0; i < 2; i++) {
            int pid = fork();
            if (pid < 0) { puts("FAIL: interrupt fork"); return 1; }
            if (!pid) { for (;;) sleep(10000); }
        }
    }
    puts("interrupt target ready");
    if (!strcmp(mode, "sleep")) for (;;) sleep(10000);
    if (!strcmp(mode, "pipe")) {
        int pair[2];
        if (pipe(pair) < 0) { puts("FAIL: interrupt pipe"); return 1; }
        for (;;) write(pair[1], memory, 4096);
    }
    for (;;) __asm__ volatile ("" ::: "memory");
}
