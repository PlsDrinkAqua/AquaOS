#include <unistd.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int execve(const char *path, const char *const argv[], const char *const envp[]) {
    return aquaos_sysret(aquaos_syscall3(SYS_EXEC, (intptr_t)path, (intptr_t)argv, (intptr_t)envp));
}

int exec(const char *path, const char *const argv[]) {
    return execve(path, argv, (const char *const *)environ);
}
