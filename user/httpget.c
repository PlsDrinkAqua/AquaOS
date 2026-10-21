#include <aquaos/net.h>
#include <aquaos/dns.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Deliberately emit the complete HTTP response, including headers. HTTP/1.0
 * plus Connection: close avoids needing a chunked/body framing decoder. */
static int ipv4(const char *s,uint32_t *ip) {
    uint32_t result=0;
    for(unsigned part=0;part<4;++part) {
        unsigned n=0,digits=0;
        while(*s>='0' && *s<='9') {
            if(++digits>3) return -1;
            n=n*10+(*s++-'0');if(n>255) return -1;
        }
        if(!digits) return -1;
        result=(result<<8)|n;
        if(part<3) {if(*s++!='.') return -1;}
        else if(*s) return -1;
    }
    *ip=result;return 0;
}
int main(int argc,char **argv) {
    if(argc<2 || argc>4) goto usage;
    const char *host=argv[1], *path=argc>2?argv[2]:"/";
    unsigned port=80;
    if(argc==4) {
        port=0;
        for(const char *p=argv[3];*p;++p) {
            if(*p<'0' || *p>'9' || port>6553) goto usage;
            port=port*10+(*p-'0');
        }
        if(!port || port>65535) goto usage;
    }
    if(!*host || strlen(host)>253 || *path!='/' || strlen(path)>1024) goto usage;
    for(const char *p=host;*p;++p)
        if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
             (*p>='0' && *p<='9') || *p=='.' || *p=='-' || *p=='_')) goto usage;
    for(const char *p=path;*p;++p) if((unsigned char)*p<=32 || (unsigned char)*p>=127) goto usage;
    uint32_t ip;
    if(ipv4(host,&ip) && aquaos_dns_lookup(host,AQUAOS_DNS_SERVER,&ip)<0) {
        fprintf(stderr,"httpget: DNS failed (errno=%d)\n",errno);return 1;
    }
    char request[1400];
    int length=snprintf(request,sizeof(request),"GET %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",path,host,port);
    if(length<0 || (size_t)length>=sizeof(request)) goto usage;
    int fd=aquaos_tcp_socket();
    if(fd<0) goto error;
    if(aquaos_tcp_connect(fd,ip,(uint16_t)port)<0) goto close_error;
    int result;
    do { result=aquaos_tcp_status(fd);if(result<0 && errno==EAGAIN) sleep(1); }
    while(result<0 && errno==EAGAIN);
    if(result<0) goto close_error;
    for(int sent=0;sent<length;) {
        int n=write(fd,request+sent,(size_t)(length-sent));
        if(n<0) {if(errno==EAGAIN) {sleep(1);continue;} goto close_error;}
        if(!n) {errno=EIO;goto close_error;}
        sent+=n;
    }
    unsigned char buffer[1024];
    for(;;) {
        int n=read(fd,buffer,sizeof(buffer));
        if(n<0) {if(errno==EAGAIN) {sleep(1);continue;} goto close_error;}
        if(!n) break;
        for(int written=0;written<n;) {
            int out=write(1,buffer+written,(size_t)(n-written));
            if(out<=0) {errno=EIO;goto close_error;}
            written+=out;
        }
    }
    aquaos_tcp_shutdown(fd);close(fd);return 0;
close_error: {
    int saved=errno;close(fd);errno=saved;
}
error:
    fprintf(stderr,"httpget: connection failed (errno=%d)\n",errno);return 1;
usage:
    fprintf(stderr,"usage: httpget <hostname|IPv4> [/path] [port]\n");return 2;
}
