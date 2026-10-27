/* Exercise the real command with fragmented transport, short writes and I/O
 * failures. Real guest filesystem/descriptor behavior is checked in QEMU. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <aquaos/net.h>
#include <aquaos/dns.h>
static const unsigned char *responses[8];
static size_t response_sizes[8], position, fragment;
static unsigned sockets, current, reads, ticks, statuses, closes, opens, renames, unlinks;
static int output_error, rename_error, close_error, no_eof, tx_again, collision, read_error, write_block, status_block;
static unsigned char output[65536], installed[65536];
static size_t output_size, installed_size;
static char requests[8][2048], diagnostics[8192];
static size_t request_sizes[8], diagnostic_size;
static int test_read(int fd,void *p,size_t n);
static int test_write(int fd,const void *p,size_t n);
static int test_open(const char *path,int flags,...);
static int test_close(int fd);
static int test_sleep(unsigned n);
static int test_rename(const char *old,const char *new);
static int test_unlink(const char *p);
static int test_fprintf(FILE *stream,const char *format,...);
#define read test_read
#define write test_write
#define open test_open
#define close test_close
#define sleep test_sleep
#define rename test_rename
#define unlink test_unlink
#define fprintf test_fprintf
#define main httpget_main
#include "../user/httpget.c"
#undef main
#undef read
#undef write
#undef open
#undef close
#undef sleep
#undef rename
#undef unlink
#undef fprintf

int aquaos_tcp_socket(void) { assert(sockets<8); current=sockets++; position=0; return 100+(int)current; }
int aquaos_tcp_connect(int fd,uint32_t ip,uint16_t port) {
    assert(fd==100+(int)current && ip==0x06060606 && (port==80 || port==8080)); return 0;
}
int aquaos_tcp_status(int fd) {
    assert(fd==100+(int)current);
    if(status_block || !(statuses++%2)) { errno=EAGAIN; return -1; } return 0;
}
int aquaos_dns_lookup(const char *name,uint32_t server,uint32_t *ip) {
    assert(!strcmp(name,"example.test") && !server); *ip=0x06060606; return 0;
}
static int test_read(int fd,void *p,size_t n) {
    assert(fd==100+(int)current);
    if(read_error) { errno=ECONNRESET; return -1; }
    if(++reads%3==1 || (no_eof && position==response_sizes[current])) { errno=EAGAIN; return -1; }
    if(n>fragment) n=fragment;
    if(n>response_sizes[current]-position) n=response_sizes[current]-position;
    memcpy(p,responses[current]+position,n); position+=n; return (int)n;
}
static int test_write(int fd,const void *p,size_t n) {
    if(fd>=100 && fd<108) {
        if(tx_again || write_block) { tx_again=0; errno=EAGAIN; return -1; }
        if(n>7) n=7;
        assert(request_sizes[current]+n<sizeof(requests[0]));
        memcpy(requests[current]+request_sizes[current],p,n); request_sizes[current]+=n;
        return (int)n;
    }
    assert(fd==1 || fd==200);
    if(output_error) { errno=ENOSPC; return -1; }
    if(n>3) n=3;
    assert(output_size+n<=sizeof(output)); memcpy(output+output_size,p,n); output_size+=n;
    return (int)n;
}
static int test_open(const char *path,int flags,...) {
    assert(strstr(path,".httpget.") && (flags&O_EXCL) && (flags&O_CREAT) && !(flags&O_TRUNC));
    ++opens;
    if(collision) { --collision; errno=EEXIST; return -1; }
    return 200;
}
static int test_close(int fd) {
    assert((fd>=100 && fd<108) || fd==200); ++closes;
    if(fd==200 && close_error) { errno=EIO; return -1; }
    return 0;
}
static int test_sleep(unsigned n) { ticks+=n; return 0; }
static int test_rename(const char *old,const char *new) {
    assert(strstr(old,".httpget.") && !strcmp(new,"/file")); ++renames;
    if(rename_error) { errno=EIO; return -1; }
    memcpy(installed,output,output_size); installed_size=output_size; return 0;
}
static int test_unlink(const char *p) { assert(strstr(p,".httpget.")); ++unlinks; return 0; }
static int test_fprintf(FILE *stream,const char *format,...) {
    assert(stream==stderr);
    va_list args; va_start(args,format);
    int n=vsnprintf(diagnostics+diagnostic_size,sizeof(diagnostics)-diagnostic_size,format,args);
    va_end(args); assert(n>=0 && (size_t)n<sizeof(diagnostics)-diagnostic_size);
    diagnostic_size+=(size_t)n; return n;
}
static void reset(const char *message) {
    memset(responses,0,sizeof(responses)); memset(response_sizes,0,sizeof(response_sizes));
    responses[0]=(const unsigned char *)message; response_sizes[0]=strlen(message);
    position=sockets=current=reads=ticks=statuses=closes=opens=renames=unlinks=0;
    output_error=rename_error=close_error=no_eof=read_error=collision=write_block=status_block=0; tx_again=1;
    fragment=1; output_size=0; diagnostic_size=0; diagnostics[0]=0;
    installed_size=4; memcpy(installed,"KEEP",4);
    memset(requests,0,sizeof(requests)); memset(request_sizes,0,sizeof(request_sizes));
}
static int run(int file) {
    char *args[]={"httpget","-o","/file","http://example.test:8080/a/b?x#fragment"};
    char *plain[]={"httpget","http://example.test:8080/a/b?x#fragment"};
    return file?httpget_main(4,args):httpget_main(2,plain);
}
static void rejected(const char *message,const char *reason) {
    reset(message); assert(run(1)==1); assert(strstr(diagnostics,reason));
    assert(installed_size==4 && !memcmp(installed,"KEEP",4));
    assert(!opens || unlinks==1);
}
int main(void) {
    reset("HTTP/1.1 200 OK\r\nCoNtEnT-LeNgTh: 5 \t\r\n\r\nhelloEXTRA");
    no_eof=1; assert(!run(1)); assert(installed_size==5 && !memcmp(installed,"hello",5));
    assert(renames==1 && !unlinks && closes==2);
    assert(strstr(requests[0],"GET /a/b?x HTTP/1.1\r\n") && strstr(requests[0],"Host: example.test:8080\r\n"));
    assert(strstr(requests[0],"Accept-Encoding: identity\r\n"));
    reset("HTTP/1.0 200 OK\r\n\r\nhello"); assert(!run(0)); assert(output_size==5 && !memcmp(output,"hello",5));
    reset("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nLink: x\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: ChUnKeD\r\n\r\n3;name=\"a\\\"b\"\r\nabc\r\n2;flag\r\nde\r\n0\r\nX-Trailer: fine\r\n\r\n");
    no_eof=1; assert(!run(1)); assert(installed_size==5 && !memcmp(installed,"abcde",5));
    reset("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"); no_eof=1; assert(!run(1) && !installed_size);
    reset("HTTP/1.1 204 No Content\r\n\r\n"); no_eof=1; assert(!run(0) && !output_size);
    reset("HTTP/1.1 205 Reset Content\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    no_eof=1; assert(!run(1) && !installed_size);
    const char *bad_messages[]={
        "HTTP/1.1 205 Reset Content\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n0\r\n\r\n",
        "HTTP/1.1 205 Reset Content\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;foo=\"unterminated\r\nx\r\n0\r\n\r\n",
        "garbage\r\n\r\n", "HTTP/2.0 200 OK\r\n\r\n", "HTTP/1.1 200 OK\n\n",
        "HTTP/1.1 200 OK\r\n Content-Length: 1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length : 1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1, 1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10000000000000000\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nab",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx!!",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n",
        "HTTP/1.1 204 No Content\r\nContent-Length: 1\r\n\r\nx",
        "HTTP/1.1 101 Switching Protocols\r\n\r\n",
    };
    for(unsigned i=0;i<sizeof(bad_messages)/sizeof(bad_messages[0]);++i) {
        reset(bad_messages[i]); assert(run(1)==1); assert(!memcmp(installed,"KEEP",4) && installed_size==4);
    }
    rejected("HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\noops","HTTP status 404");
    rejected("HTTP/1.1 206 Partial Content\r\nContent-Length: 1\r\n\r\nx","HTTP status 206");
    rejected("HTTP/1.1 302 Found\r\n\r\n","redirect has no Location");
    rejected("HTTP/1.1 301 Moved\r\nLocation: https://example.test/\r\n\r\n","HTTPS requires TLS");
    reset("HTTP/1.1 302 Found\r\nLocation: ../file?new#part\r\n\r\n");
    responses[1]=(const unsigned char *)"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    response_sizes[1]=strlen((const char *)responses[1]);
    assert(!run(1) && sockets==2 && installed_size==2);
    assert(strstr(requests[1],"GET /file?new HTTP/1.1\r\n"));
    reset("HTTP/1.1 302 Found\r\nLocation: /loop\r\n\r\n");
    for(unsigned i=1;i<8;++i) { responses[i]=responses[0]; response_sizes[i]=response_sizes[0]; }
    assert(run(1)==1 && sockets==6 && !opens && strstr(diagnostics,"too many redirects"));
    reset("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"); output_error=1;
    assert(run(1)==1 && unlinks==1 && !renames && installed_size==4);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"); close_error=1;
    assert(run(1)==1 && unlinks==1 && !renames && installed_size==4);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"); rename_error=1;
    assert(run(1)==1 && unlinks==1 && renames==1 && installed_size==4);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"); collision=2;
    assert(!run(1) && opens==3 && !unlinks);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"); collision=100;
    assert(run(1)==1 && opens==100 && !unlinks && !renames && installed_size==4);
    reset(""); write_block=1;
    assert(run(1)==1 && ticks>=IDLE_TICKS && !opens && closes==1 && strstr(diagnostics,"errno=110"));
    reset(""); status_block=1;
    assert(run(1)==1 && ticks>=IDLE_TICKS && !opens && closes==1 && strstr(diagnostics,"errno=110"));
    reset("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n"); no_eof=1;
    assert(run(1)==1 && ticks>=IDLE_TICKS && strstr(diagnostics,"errno=110"));
    reset(""); read_error=1; assert(run(0)==1 && strstr(diagnostics,"errno=104"));
    struct url u;
    assert(!parse_url(&u,"HTTP://example.test?query#fragment") && !strcmp(u.path,"/?query"));
    assert(!redirect_url(&u,"//6.6.6.6:8080/a/../b?q#f") && !strcmp(u.path,"/b?q") && u.port==8080);
    assert(!redirect_url(&u,"?other") && !strcmp(u.path,"/b?other"));
    const char *invalid[]={"https://example.test/","http://example.test:0/","http://example.test:65536/","http://a@b/","http://[::1]/","http:///","http://a/a b"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) assert(parse_url(&u,invalid[i])<0);
    /* Header size limits reject large inputs before opening an output. */
    char huge[18000];
    strcpy(huge,"HTTP/1.1 200 OK\r\nX: ");
    size_t at=strlen(huge);
    memset(huge+at,'a',3000); strcpy(huge+at+3000,"\r\n\r\n");
    rejected(huge,"HTTP line too long");
    strcpy(huge,"HTTP/1.1 200 OK\r\n"); at=strlen(huge);
    for(unsigned i=0;i<2100;++i) { memcpy(huge+at,"X: abc\r\n",8); at+=8; }
    strcpy(huge+at,"\r\n"); rejected(huge,"too large");
    /* Binary body, headers and TCP segments may arrive in any split. */
    unsigned char binary[]="HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n\0\xff\r\n";
    for(unsigned split=1;split<=80;++split) {
        reset(""); responses[0]=binary; response_sizes[0]=sizeof(binary)-1; fragment=split;
        assert(!run(1) && installed_size==4 && !memcmp(installed,"\0\xff\r\n",4));
    }
    /* Deterministic wire mutations exercise length and grammar checks under
     * sanitizers. Every rejected download must preserve the old file. */
    unsigned char corpus[]="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4;flag\r\ntest\r\n0\r\n\r\n";
    unsigned char mutated[sizeof(corpus)];
    for(size_t i=0;i<sizeof(corpus)-1;++i) for(unsigned v=0;v<256;v+=17) {
        memcpy(mutated,corpus,sizeof(corpus)); mutated[i]=(unsigned char)v;
        reset(""); responses[0]=mutated; response_sizes[0]=sizeof(corpus)-1; fragment=7;
        int result=run(1); assert(result==0 || result==1);
        if(result) assert(installed_size==4 && !memcmp(installed,"KEEP",4));
    }
    puts("PASS: httpget URLs, redirects, status/framing, chunked/trailers, binary splits, timeouts and atomic output failures");
}
