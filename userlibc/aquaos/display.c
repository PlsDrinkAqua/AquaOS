#include <aquaos/display.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>
int aquaos_display_get_info(aquaos_display_info *info) {
    return aquaos_sysret(aquaos_syscall1(SYS_DISPLAY_INFO,(intptr_t)info));
}
int aquaos_display_show(const uint32_t *pixels,uint32_t w,uint32_t h,int32_t x,int32_t y) {
    aquaos_display_request r={(uintptr_t)pixels,w,h,x,y};
    return aquaos_sysret(aquaos_syscall1(SYS_DISPLAY_SHOW,(intptr_t)&r));
}
int aquaos_display_restore(void) {
    return aquaos_sysret(aquaos_syscall1(SYS_DISPLAY_RESTORE,0));
}
