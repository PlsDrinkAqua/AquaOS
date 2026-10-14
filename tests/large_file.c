#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL large-file line %d\n", __LINE__); return 1; } } while(0)
int main(void) {
    off_t high=INT64_C(5)*1024*1024*1024;
    off_t offsets[]={12287,49151,274431,4243455,67383295,4299210751,high};
    int fd=open("/large-retained",O_RDWR|O_CREAT|O_TRUNC);CHECK(fd>=0);
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        CHECK(lseek(fd,offsets[i],SEEK_SET)==offsets[i]);CHECK(write(fd,"XY",2)==2);
    }
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        char b[2];CHECK(lseek(fd,offsets[i],SEEK_SET)==offsets[i]);
        CHECK(read(fd,b,2)==2 && b[0]=='X' && b[1]=='Y');
    }
    CHECK(lseek(fd,high,SEEK_SET)==high);
    int alias=dup(fd);CHECK(alias>=0 && lseek(alias,0,SEEK_CUR)==high);
    CHECK(lseek(fd,INT64_MAX,SEEK_CUR)==-1);
    CHECK(lseek(fd,INT64_MIN,SEEK_CUR)==-1);
    CHECK(lseek(fd,0,SEEK_CUR)==high);
    CHECK(close(alias)==0);CHECK(close(fd)==0);
    FILE *f=fopen("/large-retained","r+");CHECK(f);
    CHECK(fseek(f,(long)high,SEEK_SET)==0 && ftell(f)==high);
    CHECK(fputc('Z',f)=='Z');CHECK(ftell(f)==high+1);
    CHECK(fseek(f,-1,SEEK_CUR)==0 && fgetc(f)=='Z');
    CHECK(fseek(f,high-100,SEEK_SET)==0 && fgetc(f)==0);
    CHECK(fclose(f)==0);
    /* Retained file ends at high+2; rebuild a final sparse one-byte tail. */
    fd=open("/large-retained",O_WRONLY|O_TRUNC);CHECK(fd>=0);
    CHECK(lseek(fd,high,SEEK_SET)==high && write(fd,"Z",1)==1);CHECK(close(fd)==0);
    fd=open("/large-delete",O_RDWR|O_CREAT|O_TRUNC);CHECK(fd>=0);
    CHECK(lseek(fd,high,SEEK_SET)==high && write(fd,"Q",1)==1);
    CHECK(unlink("/large-delete")==0);CHECK(lseek(fd,high,SEEK_SET)==high);
    char c;CHECK(read(fd,&c,1)==1 && c=='Q');CHECK(close(fd)==0);
    puts("large-file checks passed");return 0;
}
