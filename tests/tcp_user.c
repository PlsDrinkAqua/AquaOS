#include <aquaos/net.h>
#include <aquaos/syscall.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"tcpcheck: FAIL line %d errno=%d\n",__LINE__,errno);return 1;} } while(0)
int main(int argc,char **argv) {
    if(argc==3 && !strcmp(argv[1],"--inherited")) {
        int fd=atoi(argv[2]);CHECK(aquaos_tcp_status(fd)<0 && errno==EAGAIN);
        CHECK(close(fd)==0);return 0;
    }
    char byte;
    CHECK(aquaos_tcp_status(1)<0 && errno==EBADF);
    CHECK(aquaos_tcp_shutdown(1)<0 && errno==EBADF);
    int fd=aquaos_tcp_socket();CHECK(fd>=0);
    CHECK(aquaos_tcp_connect(fd,0,80)<0 && errno==EINVAL);
    CHECK(aquaos_tcp_connect(fd,0x06060606,0)<0 && errno==EINVAL);
    CHECK(aquaos_syscall3(SYS_TCP_CONNECT,fd,0x100000000ULL,80)==-EINVAL);
    CHECK(write(fd,&byte,1)<0 && errno==EAGAIN);
    CHECK(read(fd,&byte,1)<0 && errno==EAGAIN);
    CHECK(read(fd,(void *)1,1)<0 && errno==EFAULT);
    CHECK(write(fd,(void *)1,1)<0 && errno==EFAULT);
    CHECK(lseek(fd,0,0)<0 && errno==ESPIPE);
    int other=dup(fd);CHECK(other>=0);CHECK(close(fd)==0);
    CHECK(aquaos_tcp_status(other)<0 && errno==EAGAIN);
    int child=fork();CHECK(child>=0);
    if(!child) {
        char number[12];snprintf(number,sizeof(number),"%d",other);
        const char *args[]={"tcpcheck","--inherited",number,NULL};
        exec("/bin/tcpcheck",args);return 1;
    }
    CHECK(wait(child)==0);CHECK(aquaos_tcp_status(other)<0 && errno==EAGAIN);
    CHECK(dup2(other,10)==10);CHECK(close(other)==0);CHECK(close(10)==0);
    for(unsigned round=0;round<10;++round) {
        int fds[16],count=0;
        while(count<16 && (fd=aquaos_tcp_socket())>=0) fds[count++]=fd;
        CHECK(count==8 && errno==ENOSPC);
        while(count) CHECK(close(fds[--count])==0);
    }
    puts("TCP descriptor checks passed");return 0;
}
