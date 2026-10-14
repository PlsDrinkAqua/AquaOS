#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL high-memory line %d\n", __LINE__); return 1; } } while (0)
int main(void) {
    unsigned char *p=malloc(2*1024*1024);
    CHECK(p);
    for (unsigned i=0;i<2*1024*1024;i+=4096) p[i]=(unsigned char)(i/4096);
    for (unsigned pass=0;pass<8;pass++) {
        int pid=fork(); CHECK(pid>=0);
        if(!pid) {
            for(unsigned i=0;i<2*1024*1024;i+=4096) {
                CHECK(p[i]==(unsigned char)(i/4096)); p[i]^=255;
            }
            sleep(2); free(p); return 0;
        }
        CHECK(wait(pid)==0);
        for(unsigned i=0;i<2*1024*1024;i+=4096) CHECK(p[i]==(unsigned char)(i/4096));
    }
    free(p); puts("high-memory fork checks passed"); return 0;
}
