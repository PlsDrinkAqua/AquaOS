#include <aquaos/display.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { printf("FAIL: display line %d\n",__LINE__); exit(1); } } while(0)
int main(void) {
    aquaos_display_info info;
    CHECK(aquaos_display_get_info(&info)==0 && info.width>=640 && info.height>=400);
    CHECK(aquaos_display_get_info((void *)1)==-1 && errno==EFAULT);
    CHECK(aquaos_sysret(aquaos_syscall1(SYS_DISPLAY_SHOW,1))==-1 && errno==EFAULT);
    uint32_t pixel=0xff0000;
    CHECK(aquaos_display_show(&pixel,0,1,0,0)==-1 && errno==EINVAL);
    CHECK(aquaos_display_show(&pixel,2048,2048,0,0)==-1 && errno==EINVAL);
    CHECK(aquaos_display_show((void *)1,1,1,0,0)==-1 && errno==EFAULT);
    CHECK(aquaos_display_show((void *)0x0000800000000000UL,1,1,0,0)==-1 && errno==EFAULT);
    CHECK(aquaos_display_show(&pixel,1,1,-1,-1)==0);
    int child=fork(); CHECK(child>=0);
    if(!child) {
        CHECK(aquaos_display_show(&pixel,1,1,0,0)==-1 && errno==EBUSY);
        CHECK(aquaos_display_restore()==0); /* Cannot release parent's screen. */
        CHECK(aquaos_display_show(&pixel,1,1,0,0)==-1 && errno==EBUSY);
        exit(0);
    }
    CHECK(wait(child)==0);
    CHECK(aquaos_display_restore()==0);
    child=fork(); CHECK(child>=0);
    if(!child) { CHECK(aquaos_display_show(&pixel,1,1,0,0)==0); exit(0); }
    CHECK(wait(child)==0);
    CHECK(aquaos_display_show(&pixel,1,1,0,0)==0); /* Exit freed the lease. */
    CHECK(aquaos_display_restore()==0);
    child=fork(); CHECK(child>=0);
    if(!child) {
        CHECK(aquaos_display_show(&pixel,1,1,0,0)==0);
        const char *args[]={"echo","display-exec",NULL};
        exec("/bin/echo",args);
        CHECK(0);
    }
    CHECK(wait(child)==0);
    CHECK(aquaos_display_show(&pixel,1,1,0,0)==0); /* Exec freed the lease. */
    CHECK(aquaos_display_restore()==0);
    puts("PASS: display ABI, invalid pointers, bounds and ownership");
    return 0;
}
