#include <zenos/net.h>
#include <zenos/syscall.h>
#include <zenos/sysret.h>
int zenos_ping_start(uint32_t ip) {
    return zenos_sysret(zenos_syscall1(SYS_PING_START, ip));
}
int zenos_ping_result(void) {
    return zenos_sysret(zenos_syscall1(SYS_PING_RESULT, 0));
}
