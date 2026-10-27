#include <aquaos/net.h>
#include <aquaos/dns.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define HOST_MAX 253
#define PATH_MAX_HTTP 1024
#define LINE_MAX_HTTP 2048
#define HEAD_MAX_HTTP 16384U
#define REDIRECT_MAX 5
#define IDLE_TICKS 3000U /* 30 seconds at the current 100 Hz PIT. */
struct url { char host[HOST_MAX+1], path[PATH_MAX_HTTP+1]; unsigned port; };
struct reader { int fd; unsigned at, size; unsigned char data[1024]; };
struct response { unsigned status; int has_length, chunked; uint64_t length; char location[1400]; };
static const char *failure;
static int bad(const char *message) { failure=message; errno=EINVAL; return -1; }
static int lower(int c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; }
static int equal(const char *a,const char *b) {
    while(*a && lower((unsigned char)*a)==lower((unsigned char)*b)) { ++a; ++b; }
    return !*a && !*b;
}
static int prefix(const char *text,const char *start) {
    while(*start) if(lower((unsigned char)*text++)!=lower((unsigned char)*start++)) return 0;
    return 1;
}
static int copy(char *out,size_t capacity,const char *s,size_t n) {
    if(n>=capacity) return bad("URL or header too long");
    memcpy(out,s,n); out[n]=0; return 0;
}
static int port_number(const char *s,size_t n,unsigned *out) {
    unsigned value=0;
    if(!n) return bad("invalid port");
    for(size_t i=0;i<n;++i) {
        if(s[i]<'0' || s[i]>'9' || value>6553) return bad("invalid port");
        value=value*10+(s[i]-'0');
    }
    if(!value || value>65535) return bad("invalid port");
    *out=value; return 0;
}
static int hostname(const char *host) {
    if(!*host || strlen(host)>HOST_MAX) return bad("invalid hostname");
    for(const char *p=host;*p;++p)
        if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
             (*p>='0' && *p<='9') || *p=='.' || *p=='-' || *p=='_'))
            return bad("invalid hostname (IPv6 and credentials are unsupported)");
    return 0;
}
static int request_path(char *out,const char *path) {
    size_t n=0;
    while(path[n] && path[n]!='#') {
        if((unsigned char)path[n]<=32 || (unsigned char)path[n]>=127)
            return bad("invalid URL; percent-encode spaces and non-ASCII characters");
        ++n;
    }
    if(!n) return copy(out,PATH_MAX_HTTP+1,"/",1);
    if(*path=='?') {
        if(n>=PATH_MAX_HTTP) return bad("URL too long");
        out[0]='/'; return copy(out+1,PATH_MAX_HTTP,path,n);
    }
    if(*path!='/') return bad("path must start with /");
    return copy(out,PATH_MAX_HTTP+1,path,n);
}
static int parse_url(struct url *url,const char *text) {
    if(!prefix(text,"http://")) return bad("only http:// URLs are supported; HTTPS requires TLS");
    const char *host=text+7, *end=host;
    while(*end && *end!='/' && *end!='?' && *end!='#') ++end;
    const char *colon=0;
    for(const char *p=host;p<end;++p) if(*p==':') { if(colon) return bad("IPv6 is unsupported"); colon=p; }
    if(copy(url->host,sizeof(url->host),host,(size_t)((colon?colon:end)-host)) || hostname(url->host)) return -1;
    url->port=80;
    if(colon && port_number(colon+1,(size_t)(end-colon-1),&url->port)) return -1;
    return request_path(url->path,end);
}
/* Remove literal dot segments using RFC 3986's algorithm; keep repeated
 * slashes, percent-encoded bytes and the query string unchanged. */
static int normalize_path(char *path) {
    char out[PATH_MAX_HTTP+1], query[PATH_MAX_HTTP+1];
    char *q=strchr(path,'?');
    query[0]=0;
    if(q) { strcpy(query,q); *q=0; }
    const char *p=path;
    size_t n=0;
    while(*p) {
        if(!strncmp(p,"/./",3)) { p+=2; continue; }
        if(!strcmp(p,"/.")) { out[n++]='/'; break; }
        if(!strncmp(p,"/../",4) || !strcmp(p,"/..")) {
            if(n) { --n; while(n && out[n]!='/') --n; }
            if(!strcmp(p,"/..")) { out[n++]='/'; break; }
            p+=3; continue;
        }
        if(*p=='/') out[n++]=*p++;
        while(*p && *p!='/') out[n++]=*p++;
    }
    out[n]=0;
    if(n+strlen(query)>PATH_MAX_HTTP) return bad("redirect URL too long");
    strcpy(path,out); strcpy(path+n,query); return 0;
}
static int redirect_url(struct url *url,const char *location) {
    char path[PATH_MAX_HTTP+1];
    if(prefix(location,"http://")) {
        if(parse_url(url,location)) return -1;
    } else if(!strncmp(location,"//",2)) {
        char absolute[1408];
        int n=snprintf(absolute,sizeof(absolute),"http:%s",location);
        if(n<0 || (size_t)n>=sizeof(absolute)) return bad("redirect URL too long");
        if(parse_url(url,absolute)) return -1;
    } else {
        const char *p=location;
        while(*p && *p!='/' && *p!='?' && *p!='#') {
            if(*p++==':') return bad("redirect scheme unsupported; HTTPS requires TLS");
        }
        if(*location=='/') {
            if(request_path(path,location)) return -1;
        } else {
            if(copy(path,sizeof(path),url->path,strlen(url->path))) return -1;
            if(*location && *location!='#') {
                char *query=strchr(path,'?'); if(query) *query=0;
                size_t base=strlen(path);
                if(*location!='?') { char *slash=strrchr(path,'/'); base=(size_t)(slash-path)+1; }
                size_t length=0;
                while(location[length] && location[length]!='#') ++length;
                if(base+length>PATH_MAX_HTTP) return bad("redirect URL too long");
                memcpy(path+base,location,length); path[base+length]=0;
            }
            if(request_path(url->path,path)) return -1;
            strcpy(path,url->path);
        }
        strcpy(url->path,path);
    }
    return normalize_path(url->path);
}
static int ipv4(const char *s,uint32_t *ip) {
    uint32_t result=0;
    for(unsigned part=0;part<4;++part) {
        unsigned n=0,digits=0;
        while(*s>='0' && *s<='9') {
            if(++digits>3) return -1;
            n=n*10+(*s++-'0'); if(n>255) return -1;
        }
        if(!digits) return -1;
        result=(result<<8)|n;
        if(part<3) { if(*s++!='.') return -1; }
        else if(*s) return -1;
    }
    *ip=result; return 0;
}
static int connect_url(const struct url *url) {
    uint32_t ip;
    if(ipv4(url->host,&ip)) {
        int numeric=1;
        for(const char *p=url->host;*p;++p) if((*p<'0' || *p>'9') && *p!='.') numeric=0;
        if(numeric) return bad("invalid IPv4 address");
        if(aquaos_dns_lookup(url->host,0,&ip)<0) { failure="DNS failed"; return -1; }
    }
    failure="connection failed";
    int fd=aquaos_tcp_socket();
    if(fd<0) return -1;
    if(aquaos_tcp_connect(fd,ip,(uint16_t)url->port)<0) goto fail;
    for(unsigned tick=0;tick<IDLE_TICKS;++tick) {
        if(!aquaos_tcp_status(fd)) return fd;
        if(errno!=EAGAIN) goto fail;
        sleep(1);
    }
    errno=ETIMEDOUT;
fail: {
    int error=errno; close(fd); errno=error; return -1;
}}
static int write_all(int fd,const void *bytes,size_t length,int network) {
    const unsigned char *p=bytes;
    unsigned idle=0;
    while(length) {
        int n=write(fd,p,length);
        if(n<0 && network && errno==EAGAIN) {
            if(++idle>=IDLE_TICKS) { errno=ETIMEDOUT; return -1; }
            sleep(1); continue;
        }
        if(n<=0) { if(!n) errno=EIO; return -1; }
        p+=n; length-=(unsigned)n; idle=0;
    }
    return 0;
}
static int fill(struct reader *r) {
    if(r->at<r->size) return 1;
    for(unsigned tick=0;tick<IDLE_TICKS;++tick) {
        int n=read(r->fd,r->data,sizeof(r->data));
        if(n>=0) { r->at=0; r->size=(unsigned)n; return n?1:0; }
        if(errno!=EAGAIN) return -1;
        sleep(1);
    }
    errno=ETIMEDOUT; return -1;
}
static int byte(struct reader *r) {
    int status=fill(r);
    if(status<0) return -1;
    if(!status) return bad("incomplete HTTP response");
    return r->data[r->at++];
}
static int line(struct reader *r,char *out,size_t capacity,unsigned *budget) {
    size_t n=0;
    for(;;) {
        int c=byte(r); if(c<0) return -1;
        if(!*budget) return bad("HTTP headers or chunk metadata too large");
        --*budget;
        if(c=='\r') {
            c=byte(r); if(c<0) return -1;
            if(!*budget) return bad("HTTP headers or chunk metadata too large");
            --*budget;
            if(c!='\n') return bad("invalid HTTP line ending");
            out[n]=0; return 0;
        }
        if(c=='\n' || (!c || (c<32 && c!='\t') || c==127)) return bad("invalid HTTP header byte");
        if(n+1>=capacity) return bad("HTTP line too long");
        out[n++]=(char)c;
    }
}
static int token(int c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
        (c && strchr("!#$%&'*+-.^_`|~",c));
}
static int field(char *text,char **value) {
    char *p=text;
    while(token((unsigned char)*p)) ++p;
    if(p==text || *p!=':') return bad("invalid HTTP header field");
    *p++=0;
    while(*p==' ' || *p=='\t') ++p;
    *value=p;
    char *end=p+strlen(p);
    while(end>p && (end[-1]==' ' || end[-1]=='\t')) *--end=0;
    return 0;
}
static int decimal(const char *p,uint64_t *value) {
    uint64_t n=0;
    if(!*p) return bad("invalid Content-Length");
    for(;*p;++p) {
        if(*p<'0' || *p>'9' || n>(UINT64_MAX-(unsigned)(*p-'0'))/10)
            return bad("invalid Content-Length");
        n=n*10+(unsigned)(*p-'0');
    }
    *value=n; return 0;
}
static int headers(struct reader *r,struct response *response) {
    char text[LINE_MAX_HTTP];
    unsigned budget=HEAD_MAX_HTTP;
    for(unsigned interim=0;interim<9;++interim) {
        memset(response,0,sizeof(*response));
        if(line(r,text,sizeof(text),&budget)) return -1;
        if(strlen(text)<12 || (strncmp(text,"HTTP/1.0 ",9) && strncmp(text,"HTTP/1.1 ",9)) ||
           text[9]<'1' || text[9]>'5' || text[10]<'0' || text[10]>'9' ||
           text[11]<'0' || text[11]>'9' || (text[12] && text[12]!=' ')) return bad("invalid HTTP status line");
        response->status=(unsigned)(text[9]-'0')*100+(unsigned)(text[10]-'0')*10+(unsigned)(text[11]-'0');
        for(;;) {
            if(line(r,text,sizeof(text),&budget)) return -1;
            if(!*text) break;
            char *value;
            if(field(text,&value)) return -1;
            if(equal(text,"Content-Length")) {
                if(response->has_length || decimal(value,&response->length)) return bad("invalid or duplicate Content-Length");
                response->has_length=1;
            } else if(equal(text,"Transfer-Encoding")) {
                if(response->chunked || !equal(value,"chunked")) return bad("unsupported Transfer-Encoding");
                response->chunked=1;
            } else if(equal(text,"Content-Encoding") && !equal(value,"identity")) {
                return bad("unsupported Content-Encoding");
            } else if(equal(text,"Location")) {
                if(response->location[0] || !*value) return bad("invalid or duplicate Location");
                if(copy(response->location,sizeof(response->location),value,strlen(value))) return -1;
            }
        }
        if(response->chunked && response->has_length) return bad("ambiguous HTTP body length");
        if(response->status>=200) return 0;
        if(response->status==101 || response->chunked || response->has_length)
            return bad("unsupported informational response");
    }
    return bad("too many informational responses");
}
static int transfer(struct reader *r,int output,uint64_t remaining,int bounded) {
    while(!bounded || remaining) {
        int status=fill(r);
        if(status<0) return -1;
        if(!status) return bounded?bad("incomplete HTTP body"):0;
        unsigned n=r->size-r->at;
        if(bounded && remaining<n) n=(unsigned)remaining;
        if(write_all(output,r->data+r->at,n,0)) { failure="output failed"; return -1; }
        r->at+=n;
        if(bounded) remaining-=n;
    }
    return 0;
}
static int hex(int c) {
    if(c>='0' && c<='9') return c-'0';
    c=lower(c); return c>='a' && c<='f'?c-'a'+10:-1;
}
static int chunk_size(const char *p,uint64_t *out) {
    uint64_t n=0;
    int digit=hex((unsigned char)*p);
    if(digit<0) return bad("invalid chunk size");
    while((digit=hex((unsigned char)*p))>=0) {
        if(n>(UINT64_MAX-(unsigned)digit)/16) return bad("chunk size overflow");
        n=n*16+(unsigned)digit; ++p;
    }
    /* Extensions are bounded by the line limit. Validate token/quoted-string
     * syntax rather than interpreting their values. */
    while(*p) {
        while(*p==' ' || *p=='\t') ++p;
        if(*p++!=';') return bad("invalid chunk extension");
        while(*p==' ' || *p=='\t') ++p;
        const char *start=p; while(token((unsigned char)*p)) ++p;
        if(p==start) return bad("invalid chunk extension");
        while(*p==' ' || *p=='\t') ++p;
        if(*p=='=') {
            ++p; while(*p==' ' || *p=='\t') ++p;
            if(*p=='"') {
                ++p;
                while(*p && *p!='"') {
                    if(*p=='\\') { ++p; if(!*p) return bad("invalid quoted chunk extension"); }
                    ++p;
                }
                if(*p!='"') return bad("invalid quoted chunk extension");
                ++p;
            } else {
                start=p; while(token((unsigned char)*p)) ++p;
                if(p==start) return bad("invalid chunk extension");
            }
        }
    }
    *out=n; return 0;
}
static int body(struct reader *r,int output,const struct response *response) {
    if(response->status==204 || response->status==205) {
        if((response->status==204 && response->chunked) || (response->has_length && response->length))
            return bad("unexpected body on empty HTTP response");
        if(response->status==204 || response->has_length) return 0;
        if(!response->chunked) {
            int status=fill(r);
            return status>0?bad("unexpected body on empty HTTP response"):status;
        }
    }
    if(!response->chunked) return transfer(r,output,response->length,response->has_length);
    char text[LINE_MAX_HTTP];
    for(;;) {
        unsigned budget=LINE_MAX_HTTP;
        if(line(r,text,sizeof(text),&budget)) return -1;
        uint64_t size;
        if(chunk_size(text,&size)) return -1;
        if(!size) break;
        if(response->status==205) return bad("unexpected body on empty HTTP response");
        if(transfer(r,output,size,1)) return -1;
        int cr=byte(r); if(cr<0) return -1;
        int lf=byte(r); if(lf<0) return -1;
        if(cr!='\r' || lf!='\n') return bad("invalid chunk terminator");
    }
    unsigned budget=HEAD_MAX_HTTP;
    for(;;) {
        if(line(r,text,sizeof(text),&budget)) return -1;
        if(!*text) return 0;
        char *value;
        if(field(text,&value)) return -1;
        if(equal(text,"Content-Length") || equal(text,"Transfer-Encoding") || equal(text,"Content-Encoding"))
            return bad("forbidden framing field in HTTP trailer");
    }
}
static int temporary(const char *destination,char *path,size_t capacity) {
    for(unsigned i=0;i<100;++i) {
        int n=snprintf(path,capacity,"%s.httpget.%u",destination,i);
        if(n<0 || (size_t)n>=capacity) return bad("output path too long");
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0644);
        if(fd>=0 || errno!=EEXIST) return fd;
    }
    errno=EEXIST; return -1;
}
int main(int argc,char **argv) {
    struct url url;
    const char *destination=0;
    int arg=1;
    failure=0;
    if(arg<argc && !strcmp(argv[arg],"-o")) {
        if(arg+2>=argc || !argv[arg+1][0]) goto usage;
        destination=argv[arg+1]; arg+=2;
        if(!strcmp(destination,"-")) destination=0;
    }
    if(arg<argc && !strcmp(argv[arg],"--")) ++arg;
    int left=argc-arg;
    if(left<1 || left>3 || *argv[arg]=='-') goto usage;
    if(strstr(argv[arg],"://")) {
        if(left!=1) goto usage;
        if(parse_url(&url,argv[arg])) goto invalid;
    } else {
        if(copy(url.host,sizeof(url.host),argv[arg],strlen(argv[arg])) || hostname(url.host)) goto invalid;
        url.port=80;
        if(left==3 && port_number(argv[arg+2],strlen(argv[arg+2]),&url.port)) goto invalid;
        if(request_path(url.path,left>=2?argv[arg+1]:"/")) goto invalid;
    }
    for(unsigned redirects=0;;++redirects) {
        int fd=connect_url(&url);
        if(fd<0) goto error;
        char request[1600];
        int n=snprintf(request,sizeof(request),
            "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\nAccept-Encoding: identity\r\nUser-Agent: AquaOS-httpget/1\r\n\r\n",
            url.path,url.host,url.port);
        struct reader reader={.fd=fd};
        struct response response;
        failure="connection failed";
        int result=n<0 || (size_t)n>=sizeof(request)?bad("request too long"):write_all(fd,request,(size_t)n,1);
        if(!result) result=headers(&reader,&response);
        if(result<0) { int error=errno; close(fd); errno=error; goto error; }
        if(response.status==301 || response.status==302 || response.status==303 || response.status==307 || response.status==308) {
            close(fd);
            if(!response.location[0]) { bad("redirect has no Location"); goto error; }
            if(redirects>=REDIRECT_MAX) { bad("too many redirects"); goto error; }
            if(redirect_url(&url,response.location)) goto error;
            continue;
        }
        if(response.status<200 || response.status>=300 || response.status==206) {
            close(fd); fprintf(stderr,"httpget: HTTP status %u\n",response.status); return 1;
        }
        int output=1;
        char staging[256];
        staging[0]=0;
        if(destination) {
            failure="cannot create output";
            output=temporary(destination,staging,sizeof(staging));
            if(output<0) { int error=errno; close(fd); errno=error; goto error; }
        }
        failure="connection failed";
        result=body(&reader,output,&response);
        int error=errno;
        close(fd);
        if(destination) {
            if(close(output)<0 && !result) { failure="output close failed"; result=-1; error=errno; }
            if(!result && rename(staging,destination)<0) { failure="cannot install output"; result=-1; error=errno; }
            if(result) unlink(staging);
        }
        errno=error;
        if(result) goto error;
        return 0;
    }
invalid:
    fprintf(stderr,"httpget: %s\n",failure?failure:"invalid arguments");
usage:
    fprintf(stderr,"usage: httpget [-o file] <http://host[:port]/path>\n"
                   "       httpget [-o file] <hostname|IPv4> [/path] [port]\n");
    return 2;
error:
    fprintf(stderr,"httpget: %s (errno=%d)\n",failure?failure:"request failed",errno); return 1;
}
