#include <stdarg.h>
#include <fcntl.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>
int fcntl(int fd, int command, ...) {
    int flags = 0;
    if (command == F_SETFL) {
        va_list args;
        va_start(args, command); flags = va_arg(args, int); va_end(args);
    }
    return aquaos_sysret(aquaos_syscall3(SYS_FCNTL, fd, command, flags));
}
