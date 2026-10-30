/* Diagnostic build only: include the real editor and time its first frame.
 * TSC ticks are an elapsed-time proxy in QEMU, not a hardware cycle profile. */
#define main kilo_main
#include "../user/kilo.c"
#undef main
#undef malloc
#undef realloc
static uint64_t read_ticks, alloc_ticks, write_ticks, read_calls, read_bytes, output_bytes;
static int measuring;
static uint64_t timestamp(void) {
    unsigned lo,hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo),"=d"(hi) :: "memory");
    return ((uint64_t)hi<<32)|lo;
}
extern ssize_t __real_read(int,void *,size_t);
extern ssize_t __real_write(int,const void *,size_t);
extern void *__real_malloc(size_t);
extern void *__real_realloc(void *,size_t);
ssize_t __wrap_read(int fd,void *p,size_t n) {
    uint64_t start=timestamp();
    ssize_t result=__real_read(fd,p,n);
    if(measuring) { read_ticks+=timestamp()-start; read_calls++; if(result>0) read_bytes+=result; }
    return result;
}
ssize_t __wrap_write(int fd,const void *p,size_t n) {
    uint64_t start=timestamp();
    ssize_t result=__real_write(fd,p,n);
    if(measuring) { write_ticks+=timestamp()-start; if(result>0) output_bytes+=result; }
    return result;
}
void *__wrap_malloc(size_t n) {
    uint64_t start=timestamp(); void *p=__real_malloc(n);
    if(measuring) alloc_ticks+=timestamp()-start;
    return p;
}
void *__wrap_realloc(void *old,size_t n) {
    uint64_t start=timestamp(); void *p=__real_realloc(old,n);
    if(measuring) alloc_ticks+=timestamp()-start;
    return p;
}
int main(int argc,char **argv) {
    if(argc!=2) return 1;
    initEditor(); editorSelectSyntaxHighlight(argv[1]);
    measuring=1;
    uint64_t start=timestamp();
    editorOpen(argv[1]);
    uint64_t opened=timestamp(), open_read=read_ticks, open_alloc=alloc_ticks;
    editorSetStatusMessage("profile");
    editorRefreshScreen();
    uint64_t rendered=timestamp();
    measuring=0;
    printf("\nPROFILE rows=%d bytes=%lu reads=%lu\n",E.numrows,(unsigned long)read_bytes,(unsigned long)read_calls);
    printf("OPEN ticks=%lu read=%lu alloc=%lu\n",(unsigned long)(opened-start),(unsigned long)open_read,(unsigned long)open_alloc);
    printf("FRAME ticks=%lu write=%lu alloc=%lu bytes=%lu\n",(unsigned long)(rendered-opened),(unsigned long)write_ticks,(unsigned long)(alloc_ticks-open_alloc),(unsigned long)output_bytes);
    puts("PROFILE DONE");
    return 0;
}
