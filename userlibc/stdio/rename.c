#include <stdio.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>

int rename(const char *old_path, const char *new_path) {
    return aquaos_sysret(aquaos_syscall3(SYS_RENAME, (intptr_t)old_path, (intptr_t)new_path, 0));
}
