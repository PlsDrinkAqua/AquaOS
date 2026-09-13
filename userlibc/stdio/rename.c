#include <stdio.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>

int rename(const char *old_path, const char *new_path) {
    return zenos_sysret(zenos_syscall3(SYS_RENAME, (int)old_path, (int)new_path, 0));
}
