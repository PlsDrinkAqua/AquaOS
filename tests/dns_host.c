#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../userlibc/include/aquaos/dns.h"
#include "../userlibc/include/aquaos/net.h"

static unsigned char query[512], response[512];
static size_t query_length, response_length;
static unsigned calls, ticks, closes, receives;
static int mode, config_mode;
static uint32_t configured_dns = AQUAOS_DNS_SERVER;
static void put(unsigned char *p, unsigned n) { p[0]=n>>8; p[1]=n; }
static void answer(const char *name) {
    int n=aquaos_dns_query(name,0x1234,response,sizeof(response)); assert(n>0);
    response_length=(size_t)n; response[2]=0x81;response[3]=0x80;put(response+6,1);
    unsigned char a[]={0xc0,12,0,1,0,1,0,0,0,60,0,4,192,0,2,42};
    memcpy(response+n,a,sizeof(a));response_length+=sizeof(a);
}
static int parse(void) {
    uint32_t address=0xdeadbeef;
    int r=aquaos_dns_parse(response,response_length,"aqua.test",0x1234,&address);
    if(!r) assert(address==0xc000022a);
    else assert(address==0xdeadbeef);
    return r;
}
int aquaos_get_net_info(aquaos_net_info *info) {
    if (config_mode == 1) { errno = EFAULT; return -1; }
    memset(info, 0, sizeof(*info));
    info->flags = config_mode == 2 ? 0 : AQUAOS_NET_PRESENT;
    info->dns = config_mode == 3 ? 0 : configured_dns; return 0;
}
int aquaos_udp_socket(void) { return 7; }
int aquaos_udp_bind(int fd, uint16_t port) { assert(fd==7 && !port);return 0; }
int aquaos_udp_sendto(int fd,const void *data,size_t n,uint32_t ip,uint16_t port) {
    assert(fd==7 && ip==configured_dns && port==53);++calls;
    if(mode==3) { errno=ENODEV;return -1; }
    if(calls<4) { errno=EAGAIN;return -1; }
    memcpy(query,data,n);query_length=n;return (int)n;
}
int aquaos_udp_recvfrom(int fd,void *data,size_t capacity,aquaos_udp_address *source) {
    assert(fd==7);++receives;
    if(mode==1 || (mode==4 && calls==4)) { errno=EAGAIN;return -1; }
    answer("aqua.test");memcpy(response,query,2);
    if(mode==2) { response[3]=0x83;put(response+6,0);response_length=query_length; }
    assert(capacity>=response_length);memcpy(data,response,response_length);
    source->ip=receives==1?0x08080808:configured_dns;
    source->port=receives==2?54:53;
    if(receives==3) ((unsigned char *)data)[0]^=1;
    if(receives==4) ((unsigned char *)data)[12]=0xff;
    return (int)response_length;
}
int close(int fd) { assert(fd==7);++closes;return 0; }
unsigned sleep(unsigned n) { ticks+=n;return 0; }
int main(void) {
    unsigned char encoded[512];
    assert(aquaos_dns_query("Aqua.Test.",0x1234,encoded,sizeof(encoded))==27);
    assert(!memcmp(encoded,"\x12\x34\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x04" "aqua\x04" "test\0\0\1\0\1",27));
    const char *invalid[]={"", ".", "a..b", "a..", "a b", "a/b"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
        assert(aquaos_dns_query(invalid[i],1,encoded,sizeof(encoded))<0 && errno==EINVAL);
    char longname[260];memset(longname,'a',sizeof(longname));longname[259]=0;
    assert(aquaos_dns_query(longname,1,encoded,sizeof(encoded))<0);
    for(unsigned i=0;i<4;++i) {memset(longname+i*64,'a',63);longname[i*64+63]='.';}
    longname[255]=0;assert(aquaos_dns_query(longname,1,encoded,sizeof(encoded))<0);
    longname[253]=0;assert(aquaos_dns_query(longname,1,encoded,sizeof(encoded))>0);
    assert(aquaos_dns_query("aqua.test",1,encoded,10)<0 && errno==ERANGE);
    answer("AQUA.test");assert(parse()==0);
    size_t good=response_length;
    for(size_t n=0;n<good;++n) {
        response_length=n;assert(parse()<0);
    }
    answer("aqua.test");response[0]^=1;assert(parse()<0 && errno==EAGAIN);
    answer("other.test");assert(parse()<0 && errno==EAGAIN);
    answer("aqua.test");response[2]=1;assert(parse()<0 && errno==EINVAL);
    answer("aqua.test");response[2]|=2;assert(parse()<0 && errno==EOVERFLOW);
    answer("aqua.test");response[3]=0x83;assert(parse()<0 && errno==ENOENT);
    answer("aqua.test");response[3]=0x82;assert(parse()<0 && errno==EIO);
    /* Compression bounds, cycles, reserved label tags, A length, RR counts. */
    unsigned offsets[]={27,28,37,38};
    for(unsigned i=0;i<4;++i) { answer("aqua.test");response[offsets[i]]=255;assert(parse()<0); }
    answer("aqua.test");response[27]=0xc0;response[28]=27;assert(parse()<0);
    answer("aqua.test");response[27]=0x40;assert(parse()<0);
    answer("aqua.test");put(response+6,65);assert(parse()<0 && errno==EOVERFLOW);
    answer("aqua.test");response[response_length++]=0;assert(parse()<0);
    /* Compressed CNAME plus A, deliberately out of order. Unrelated A ignored. */
    answer("aqua.test");response[28]=17;assert(parse()<0 && errno==ENOENT);
    int n=aquaos_dns_query("aqua.test",0x1234,response,sizeof(response));
    response[2]=0x81;response[3]=0x80;put(response+6,2);
    unsigned char records[]={
        5,'a','l','i','a','s',0xc0,17,0,1,0,1,0,0,0,60,0,4,192,0,2,42,
        0xc0,12,0,5,0,1,0,0,0,60,0,2,0xc0,27};
    memcpy(response+n,records,sizeof(records));response_length=n+sizeof(records);assert(parse()==0);
    response[response_length-1]=12;assert(parse()<0 && errno==EOVERFLOW); /* self alias */
    response[response_length-3]=3;assert(parse()<0 && errno==EINVAL); /* bad RDLENGTH */
    /* Deterministic mutation corpus: sanitizers catch any out-of-bounds walk. */
    answer("aqua.test");unsigned char original[512];memcpy(original,response,response_length);
    for(size_t i=0;i<response_length;++i) for(unsigned v=0;v<256;v+=17) {
        memcpy(response,original,response_length);response[i]=(unsigned char)v;
        uint32_t result;aquaos_dns_parse(response,response_length,"aqua.test",0x1234,&result);
    }
    uint32_t address=0;
    for(mode=0;mode<5;++mode) {
        calls=ticks=closes=receives=0;
        int result=aquaos_dns_lookup("aqua.test",AQUAOS_DNS_SERVER,&address);
        assert(closes==1);
        if(mode==0 || mode==4) assert(result==0 && address==0xc000022a && calls>=4);
        if(mode==1) assert(result<0 && errno==ETIMEDOUT && ticks==903);
        if(mode==2) assert(result<0 && errno==ENOENT);
        if(mode==3) assert(result<0 && errno==ENODEV && calls==1);
    }
    mode = 0; configured_dns = 0x0a141e09;
    calls=ticks=closes=receives=0;
    assert(!aquaos_dns_lookup("aqua.test",0,&address) && address==0xc000022a);
    assert(closes==1 && calls>=4);
    for(config_mode=1;config_mode<=3;++config_mode) {
        calls=ticks=closes=receives=0;
        assert(aquaos_dns_lookup("aqua.test",0,&address)<0);
        assert(errno==(config_mode==1?EFAULT:config_mode==2?ENODEV:ENOENT));
        assert(!calls && !closes);
    }
    puts("PASS: configured DNS, no-interface errors, DNS wire validation, compression, CNAME, mutation corpus, retries and cleanup");
}
