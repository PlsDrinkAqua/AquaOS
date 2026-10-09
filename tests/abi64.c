#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <setjmp.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL abi64 line %d\n", __LINE__); return 1; } } while (0)

/* A real ELF64 image above 4 GiB, with no dependency on a host linker. */
static int high_image(void) {
    struct header {
        unsigned char ident[16]; uint16_t type, machine; uint32_t version;
        uint64_t entry, phoff, shoff; uint32_t flags;
        uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
    } h = {{0x7f,'E','L','F',2,1,1},2,62,1,UINT64_C(0x100000078),64,0,0,64,56,1,0,0,0};
    struct program {
        uint32_t type, flags; uint64_t offset, va, pa, filesz, memsz, align;
    } p = {1,5,0,UINT64_C(0x100000000),0,134,134,4096};
    /* mov $5,%eax; mov $42,%ebx; int $0x80; jmp . */
    const unsigned char code[]={0xb8,5,0,0,0,0xbb,42,0,0,0,0xcd,0x80,0xeb,0xfe};
    _Static_assert(sizeof(h)==64 && sizeof(p)==56,"ELF64 layout");
    FILE *f=fopen("/abi64-high", "wb"); CHECK(f);
    CHECK(fwrite(&h,1,sizeof(h),f)==sizeof(h));
    CHECK(fwrite(&p,1,sizeof(p),f)==sizeof(p));
    CHECK(fwrite(code,1,sizeof(code),f)==sizeof(code));
    CHECK(fclose(f)==0);
    int pid=fork(); CHECK(pid>=0);
    if(!pid) { const char *args[]={"/abi64-high",NULL}; exec(args[0],args); exit(1); }
    CHECK(wait(pid)==42);
    /* Reject wrapping segments without damaging the calling process. */
    p.va=UINT64_MAX-4095;
    f=fopen("/abi64-high", "r+b"); CHECK(f);
    CHECK(fseek(f,64,SEEK_SET)==0 && fwrite(&p,1,sizeof(p),f)==sizeof(p));
    CHECK(fclose(f)==0);
    CHECK(exec("/abi64-high",NULL)==-1);
    CHECK(unlink("/abi64-high")==0);
    return 0;
}
int main(void) {
    _Static_assert(sizeof(void *)==8,"64-bit pointers");
    _Static_assert(sizeof(long)==8,"LP64 ABI");
    char message[]="x86-64 high stack pointer\n";
    CHECK((uintptr_t)message>UINT32_MAX);
    CHECK(write(1,message,sizeof(message)-1)==(int)sizeof(message)-1);
    CHECK(write(1,(void *)UINT64_C(0x0000800000000000),1)==-1 && errno==EFAULT);
    CHECK(write(1,(void *)UINT64_C(0xffffffff80000000),1)==-1 && errno==EFAULT);
    void *allocation=malloc(33); CHECK(allocation && !((uintptr_t)allocation&15)); free(allocation);
    uint64_t saved, vector[2]={UINT64_C(0xfedcba9876543210),UINT64_C(0x123456789abcdef0)}, restored[2];
    __asm__ volatile ("mov %2,%%r12; movdqu %3,%%xmm15; mov $11,%%eax; int $0x80;"
                      "mov %%r12,%0; movdqu %%xmm15,%1"
                      : "=r"(saved),"=m"(restored)
                      : "r"(vector[0]),"m"(vector)
                      : "rax","r12","xmm15","cc","memory");
    CHECK(saved==vector[0] && !memcmp(vector,restored,sizeof(vector)));
    jmp_buf env;
    volatile int reached=0;
    int result=setjmp(env);
    if(!result) { reached=1; longjmp(env,42); }
    CHECK(result==42 && reached==1);
    CHECK(high_image()==0);
    puts("PASS: x86-64 ABI");
    return 0;
}
