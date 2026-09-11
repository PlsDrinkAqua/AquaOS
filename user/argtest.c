#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static void check(int condition, const char *message) {
    if (!condition) { printf("argtest: FAIL: %s\n", message); exit(1); }
}

int main(int argc, char **argv) {
    check(argv != NULL && argv[argc] == NULL, "argv terminator");
    char cwd[256];
    if (argc == 0) return 38;
    if (argc == 32) {
        for (int i = 0; i < argc; i++) check(strcmp(argv[i], "") == 0, "maximum argv");
        return 39;
    }
    if (argc == 4) {
        check(getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/subdir") == 0, "exec cwd");
        check(chdir("/") == 0, "child chdir");
        check(strcmp(argv[0], "custom-name") == 0, "argv[0]");
        check(strcmp(argv[1], "two words") == 0, "argument with spaces");
        check(strcmp(argv[2], "") == 0, "empty argument");
        check(strcmp(argv[3], "last") == 0, "last argument");
        return 37;
    }
    check(argc == 1, "spawn/default argc");
    const char *args[] = {"custom-name", "two words", "", "last", NULL};
    check(chdir("/subdir") == 0, "chdir");
    check(getcwd(cwd, 1) == NULL && errno == ERANGE, "getcwd small buffer");
    check(getcwd((char *)0xC0000000U, 256) == NULL && errno == EFAULT, "getcwd invalid buffer");
    int pid = fork();
    check(pid >= 0, "fork");
    if (pid == 0) { exec("/argtest", args); exit(2); }
    check(wait(pid) == 37, "exec arguments and main return status");

    check(getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/subdir") == 0, "fork cwd isolation");
    check(chdir("..") == 0, "relative parent directory");
    check(chdir("/hello.txt") == -1 && errno == ENOTDIR, "chdir regular file");
    check(chdir("/does-not-exist") == -1, "chdir missing directory");
    check(getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/") == 0, "failed chdir preserves cwd");

    const char *empty[] = {NULL};
    pid = fork();
    check(pid >= 0, "empty argv fork");
    if (pid == 0) { exec("/argtest", empty); exit(2); }
    check(wait(pid) == 38, "empty argv");
    const char *maximum[33];
    for (int i = 0; i < 32; i++) maximum[i] = "";
    maximum[32] = NULL;
    pid = fork();
    check(pid >= 0, "maximum argv fork");
    if (pid == 0) { exec("/argtest", maximum); exit(2); }
    check(wait(pid) == 39, "maximum argument count");

    errno = 0;
    check(exec("/argtest", (const char *const *)0xC0000000U) == -1 &&
          errno == EFAULT, "invalid argv");
    const char *bad[] = {(const char *)0xC0000000U, NULL};
    check(exec("/argtest", bad) == -1 && errno == EFAULT, "invalid string");
    const char *many[34];
    for (int i = 0; i < 33; i++) many[i] = "";
    many[33] = NULL;
    check(exec("/argtest", many) == -1 && errno == E2BIG, "argument count limit");
    char long_arg[1025];
    for (int i = 0; i < 1024; i++) long_arg[i] = 'a';
    long_arg[1024] = 0;
    const char *large[] = {long_arg, NULL};
    check(exec("/argtest", large) == -1 && errno == E2BIG, "argument byte limit");
    check(exec("/missing-program", args) == -1 && errno == ENOENT, "failed exec survives");
    puts("argtest: PASS");
    return 0;
}
