#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/tcp.h>
void *kmemcpy(void *d,const void *s,size_t n) {return memcpy(d,s,n);}
void *kmemset(void *d,int c,size_t n) {return memset(d,c,n);}
static uint8_t sent[600];
static size_t sent_length;
static unsigned sends;
static int route_error,send_error;
uint32_t net_local_ip(void) { return 0x0606060fU; }
int net_resolve(uint32_t ip,uint8_t out[6]) {
    (void)out;if(!ip) return -K_EINVAL;return route_error;
}
int net_ipv4_send(uint32_t ip,unsigned protocol,const void *p,size_t n) {
    assert(ip==0x06060606 && protocol==6 && n<=sizeof(sent));
    memcpy(sent,p,n);sent_length=n;++sends;return send_error;
}
#include "../kernel/kernel/NET/tcp.c"
/* Independent checksum of received and transmitted pseudo headers. */
static unsigned sum(const uint8_t *p,size_t n,unsigned value) {
    for(size_t i=0;i<n;i+=2) value+=(unsigned)p[i]*256+(i+1<n?p[i+1]:0);
    while(value>>16) value=(value&65535)+(value>>16);
    return value;
}
static uint8_t last_input[20+20+TCP_RX];
static unsigned last_total;
static uint32_t remote_seq;
static int handle;
static uint16_t local_port;
static void inject(unsigned flags,uint32_t seq,uint32_t ack,const void *data,unsigned n,unsigned window) {
    uint8_t ip[20+20+TCP_RX]={0};assert(n<=TCP_RX);
    memcpy(ip+12,(uint8_t[]){6,6,6,6,6,6,6,15},8);
    uint8_t *t=ip+20;
    tcp_put16(t,80);tcp_put16(t+2,local_port);tcp_put32(t+4,seq);tcp_put32(t+8,ack);
    t[12]=0x50;t[13]=flags;tcp_put16(t+14,window);
    if(n) memcpy(t+20,data,n);
    tcp_put16(t+16,~sum(t,20+n,sum(ip+12,8,6+20+n)));
    memcpy(last_input,ip,40+n);last_total=40+n;
    net_tcp_input(ip,20,40+n);
}
static void wire(void) {
    uint8_t addresses[]={6,6,6,15,6,6,6,6};
    assert(sum(sent,sent_length,sum(addresses,8,6+sent_length))==65535);
    assert(tcp_get16(sent)==local_port && tcp_get16(sent+2)==80);
}
static void start(uint32_t now) {
    net_tcp_init();route_error=send_error=0;sends=0;net_tcp_poll(now);
    handle=net_tcp_socket();assert(handle>=0);
    assert(net_tcp_connect(handle,0x06060606,80)==0);
    assert(net_tcp_status(handle)==-K_EAGAIN);
    net_tcp_poll(now);assert(sent[13]==SYN && sent_length==24);
    local_port=tcp_get16(sent);wire();
    remote_seq=UINT32_MAX-5;
    inject(SYN|ACK,remote_seq,connections[handle].next,0,0,4096);
    ++remote_seq;
    assert(net_tcp_status(handle)==0 && sent[13]==ACK);wire();
}
int main(void) {
    start(0);
    /* Damaged TCP bytes cannot change state or elicit acknowledgements. */
    inject(ACK,remote_seq,connections[handle].next,0,0,4096);
    unsigned baseline=sends;
    uint8_t original[60];memcpy(original,last_input,last_total);
    for(unsigned i=20;i<last_total;++i) {
        uint8_t bad[60];memcpy(bad,original,last_total);bad[i]^=1;
        net_tcp_input(bad,20,last_total);assert(sends==baseline && net_tcp_status(handle)==0);
    }
    for(unsigned n=0;n<40;++n) {net_tcp_input(original,20,n);assert(sends==baseline);}
    uint8_t data[9000],out[9000];for(unsigned i=0;i<sizeof(data);++i) data[i]=(uint8_t)i;
    assert(net_tcp_send(handle,data,1000)==536);wire();
    uint32_t sequence=tcp_get32(sent+4);
    assert(!memcmp(sent+20,data,536));
    unsigned previous=sends;
    net_tcp_poll(99);assert(sends==previous);
    net_tcp_poll(100);assert(sends==previous+1 && tcp_get32(sent+4)==sequence);
    assert(!memcmp(sent+20,data,536));
    inject(ACK,remote_seq,sequence+600,0,0,4096); /* Invalid future ACK. */
    assert(net_tcp_send(handle,data,1)==-K_EAGAIN);
    inject(ACK,remote_seq,sequence+200,0,0,4096);
    net_tcp_poll(101);assert(tcp_get32(sent+4)==sequence+200);
    assert(sent_length==356 && !memcmp(sent+20,data+200,336));wire();
    inject(ACK,remote_seq,sequence+536,0,0,0);
    assert(net_tcp_send(handle,data,1)==-K_EAGAIN); /* Peer zero window. */
    inject(ACK,remote_seq,sequence+536,0,0,4096);
    assert(net_tcp_send(handle,data,1)==1);
    inject(ACK,remote_seq,sequence+537,0,0,4096);
    /* Future segment is not exposed, then overlap is delivered exactly once. */
    inject(ACK,remote_seq+3,sequence+537,"def",3,4096);
    assert(net_tcp_read(handle,out,10)==-K_EAGAIN);
    inject(ACK,remote_seq,sequence+537,"abc",3,4096);
    inject(ACK,remote_seq+1,sequence+537,"bcdef",5,4096);
    inject(ACK,remote_seq,sequence+537,"abcdef",6,4096);
    remote_seq+=6;assert(net_tcp_read(handle,out,20)==6 && !memcmp(out,"abcdef",6));
    assert(net_tcp_read(handle,out,20)==-K_EAGAIN);
    /* Fill receive window, reject excess, reopen by consuming; ring wrap. */
    inject(ACK,remote_seq,sequence+537,data,TCP_RX,4096);remote_seq+=TCP_RX;
    assert(tcp_get16(sent+14)==0);
    inject(ACK,remote_seq,sequence+537,"x",1,4096);
    assert(net_tcp_read(handle,out,4000)==4000 && !memcmp(out,data,4000));
    inject(ACK,remote_seq,sequence+537,data,4000,4096);remote_seq+=4000;
    assert(net_tcp_read(handle,out,sizeof(out))==TCP_RX);
    assert(!memcmp(out,data+4000,TCP_RX-4000));assert(!memcmp(out+TCP_RX-4000,data,4000));
    /* Passive FIN, EOF, local FIN and ACK release. */
    inject(FIN|ACK,remote_seq,sequence+537,0,0,4096);++remote_seq;
    assert(net_tcp_read(handle,out,1)==0);
    assert(net_tcp_shutdown(handle)==0 && (sent[13]&FIN));
    uint32_t fin=tcp_get32(sent+4);net_tcp_release(handle);
    inject(ACK,remote_seq,fin+1,0,0,4096);
    assert(connections[handle].state==FREE);
    /* Active FIN stays in TIME_WAIT and ACKs retransmitted peer FIN. */
    start(0);assert(net_tcp_shutdown(handle)==0);fin=tcp_get32(sent+4);
    inject(ACK,remote_seq,fin+1,0,0,4096);
    inject(FIN|ACK,remote_seq,fin+1,0,0,4096);++remote_seq;
    assert(connections[handle].state==TIME_WAIT);
    previous=sends;inject(FIN|ACK,remote_seq-1,fin+1,0,0,4096);assert(sends==previous+1);
    net_tcp_release(handle);net_tcp_poll(TCP_TIME_WAIT_TICKS);
    assert(connections[handle].state==FREE);
    /* Reset must match receive sequence. Terminal errors survive until close. */
    start(0);inject(RST|ACK,remote_seq+1,connections[handle].next,0,0,4096);
    assert(net_tcp_status(handle)==0);
    inject(RST|ACK,remote_seq,connections[handle].next,0,0,4096);
    assert(net_tcp_read(handle,out,1)==-K_ECONNRESET);net_tcp_release(handle);
    /* Refusal and wrong handshake ACK; packet checksums/options/lengths. */
    start(0);net_tcp_release(handle);net_tcp_init();handle=net_tcp_socket();
    assert(net_tcp_connect(handle,0x06060606,80)==0);net_tcp_poll(0);local_port=tcp_get16(sent);
    inject(SYN|ACK,100,connections[handle].next+1,0,0,4096);
    assert(net_tcp_status(handle)==-K_EAGAIN);
    inject(RST|ACK,100,connections[handle].next,0,0,4096);
    assert(net_tcp_status(handle)==-K_ECONNREFUSED);net_tcp_release(handle);
    /* Retransmission failure and timer wrap are bounded. */
    start(UINT32_MAX-50);assert(net_tcp_send(handle,data,1)==1);
    previous=sends;net_tcp_poll(48);assert(sends==previous);net_tcp_poll(49);assert(sends==previous+1);
    net_tcp_poll(249);net_tcp_poll(649);net_tcp_poll(1449);net_tcp_poll(2249);
    assert(net_tcp_status(handle)==-K_ETIMEDOUT);net_tcp_release(handle);
    net_tcp_init();route_error=-K_EAGAIN;handle=net_tcp_socket();
    assert(net_tcp_connect(handle,0x06060606,80)==0);
    net_tcp_poll(tcp_now+TCP_IDLE_TICKS);assert(net_tcp_status(handle)==-K_ETIMEDOUT);
    net_tcp_release(handle);route_error=0;
    for(unsigned i=0;i<TCP_SLOTS;++i) assert(net_tcp_socket()>=0);
    assert(net_tcp_socket()==-K_ENOSPC);
    for(unsigned i=0;i<TCP_SLOTS;++i) net_tcp_release((int)i);
    assert(net_tcp_socket()>=0);
    puts("PASS: TCP wire checksum, handshake, retransmit, partial ACK, windows, wrap, FIN, reset, timeout and limits");
}
