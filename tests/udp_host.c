#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/net.h>
void *kmemset(void *p, int c, size_t n) { return memset(p,c,n); }
void *kmemcpy(void *d, const void *s, size_t n) { return memcpy(d,s,n); }
int kmemcmp(const void *a, const void *b, size_t n) { return memcmp(a,b,n); }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
static uint8_t sent[1514];
static size_t sent_length;
static int send_error;
int e1000_transmit(const void *p, size_t n) {
    assert(n <= sizeof(sent)); memcpy(sent,p,n); sent_length=n; return send_error;
}
#include "../kernel/kernel/NET/net.c"
#include "../kernel/kernel/NET/tcp.c"
/* Independent wire checksum including pseudo header. */
static unsigned wire_sum(const uint8_t *p, size_t n, unsigned sum) {
    for (size_t i=0;i<n;i+=2) sum += (unsigned)p[i]*256 + (i+1<n?p[i+1]:0);
    while(sum>>16) sum=(sum&65535)+(sum>>16);
    return sum;
}
static void reply(uint8_t *out) {
    memcpy(out,sent,sent_length);
    memcpy(out,local_mac,6); memcpy(out+6,gateway_mac,6);
    memcpy(out+26,sent+30,4); memcpy(out+30,local_ip,4);
    memcpy(out+34,sent+36,2); memcpy(out+36,sent+34,2);
    out[24]=out[25]=out[40]=out[41]=0;
    write_be16(out+24,~wire_sum(out+14,20,0));
    unsigned sum=wire_sum(out+26,8,17+sent_length-34);
    unsigned c=(~wire_sum(out+34,sent_length-34,sum))&65535;
    write_be16(out+40,c?c:65535);
}
int main(void) {
    assert(net_udp_send(0x08080808,100,200,0,0)==-K_ENODEV);
    net_init((uint8_t[]){6,7,6,7,6,7},0);
    assert(net_udp_send(0x08080808,100,200,0,0)==-K_EAGAIN);
    memcpy(gateway_mac,(uint8_t[]){0x52,0x55,6,6,6,6},6); resolved=1;
    assert(net_udp_bind(0)==-K_EINVAL);
    assert(net_udp_bind(100)==0); assert(net_udp_bind(100)==-K_EBUSY);
    uint8_t frame[1514],bad[1514], data[1472]={0}, out[1472];
    uint32_t ip; uint16_t port;
    for(unsigned n=0;n<=1472;++n) {
        data[n? n-1:0]=(uint8_t)n;
        assert(net_udp_send(0x08080808,100,200,data,n)==0);
        assert(sent_length==42+n && sent[23]==17);
        assert(wire_sum(sent+14,20,0)==65535);
        assert(wire_sum(sent+34,n+8,wire_sum(sent+26,8,17+n+8))==65535);
        reply(frame);net_receive(frame,sent_length);
        assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==(int)n);
        assert(ip==0x08080808 && port==200 && !memcmp(data,out,n));
    }
    assert(net_udp_send(0x08080808,100,200,"abc",3)==0);reply(frame);
    for(unsigned n=0;n<45;++n) {
        net_receive(frame,n);
        assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==-K_EAGAIN);
    }
    unsigned offsets[]={0,6,14,16,20,22,23,24,26,30,34,36,38,40,44};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        memcpy(bad,frame,45);bad[offsets[i]]^=1;net_receive(bad,45);
        assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==-K_EAGAIN);
    }
    /* Fragmentation, TTL, and protocol rejection with a valid IP checksum. */
    unsigned ip_offsets[]={20,21,22,23};
    for(unsigned i=0;i<4;++i) {
        memcpy(bad,frame,45);bad[ip_offsets[i]]=i==2?0:1;
        bad[24]=bad[25]=0;write_be16(bad+24,~wire_sum(bad+14,20,0));
        net_receive(bad,45);
        assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==-K_EAGAIN);
    }
    /* IPv4 options are included in header length and checksum. */
    memcpy(bad,frame,34);memset(bad+34,0,4);memcpy(bad+38,frame+34,11);
    bad[14]=0x46;write_be16(bad+16,35);bad[24]=bad[25]=0;
    write_be16(bad+24,~wire_sum(bad+14,24,0));net_receive(bad,49);
    assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==3);
    /* A computed checksum of zero must be transmitted as 0xffff. */
    int found=0;
    for(unsigned n=0;n<65536;++n) {
        uint8_t payload[2]={(uint8_t)(n>>8),(uint8_t)n};
        assert(net_udp_send(0x08080808,100,200,payload,2)==0);
        if(read_be16(sent+40)==65535) {
            sent[40]=sent[41]=0;
            if(wire_sum(sent+34,10,wire_sum(sent+26,8,27))==65535) {found=1;break;}
        }
    }
    assert(found);
    assert(net_udp_send(0x08080808,100,200,"abc",3)==0);
    /* Invalid UDP lengths with checksum disabled; Ethernet padding ignored. */
    memcpy(bad,frame,45);bad[40]=bad[41]=0;
    for(unsigned n=0;n<16;++n) if(n!=11) {
        write_be16(bad+38,n);net_receive(bad,45);
        assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==-K_EAGAIN);
    }
    write_be16(bad+38,11);net_receive(bad,60);
    assert(net_udp_receive(100,out,2,&ip,&port)==-K_ERANGE);
    assert(net_udp_receive(100,out,3,&ip,&port)==3);
    for(unsigned n=0;n<8;++n) net_receive(frame,45);
    for(unsigned n=0;n<4;++n) assert(net_udp_receive(100,out,3,&ip,&port)==3);
    assert(net_udp_receive(100,out,3,&ip,&port)==-K_EAGAIN);
    for(unsigned n=101;n<108;++n) assert(net_udp_bind(n)==0);
    assert(net_udp_bind(108)==-K_ENOSPC);
    net_receive(frame,45);net_udp_unbind(100);
    assert(net_udp_receive(100,out,3,&ip,&port)==-K_ENOENT);
    assert(net_udp_bind(100)==0);
    assert(net_udp_receive(100,out,3,&ip,&port)==-K_EAGAIN);
    assert(net_udp_send(0x06060603,100,200,data,1)==-K_EAGAIN);
    assert(net_udp_send(0x08080808,100,0,data,1)==-K_EINVAL);
    assert(net_udp_send(0x08080808,100,200,0,1)==-K_EINVAL);
    assert(net_udp_send(0x08080808,100,200,data,1473)==-K_EINVAL);
    /* Resolve a local peer, rejecting unsolicited and mismatched replies. */
    uint8_t arp[42], peer_mac[6] = {0x52,0x55,6,6,6,3};
    memcpy(arp, sent, 42); /* Last successful send was the peer ARP request. */
    memcpy(arp,local_mac,6); memcpy(arp+6,peer_mac,6);
    arp[21]=2; memcpy(arp+22,peer_mac,6);
    memcpy(arp+28,(uint8_t[]){6,6,6,3},4);
    memcpy(arp+32,local_mac,6); memcpy(arp+38,local_ip,4);
    memcpy(bad,arp,42);bad[28]=7;net_receive(bad,42);
    assert(net_resolve(0x06060603,out)==-K_EAGAIN);
    memcpy(bad,arp,42);bad[32]^=1;net_receive(bad,42);
    assert(net_resolve(0x06060603,out)==-K_EAGAIN);
    net_receive(arp,42);
    assert(net_udp_send(0x06060603,100,200,"abc",3)==0);
    assert(!memcmp(sent,peer_mac,6));
    reply(frame);memcpy(frame+6,peer_mac,6);net_receive(frame,45);
    assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==3 && ip==0x06060603);
    memcpy(frame+6,gateway_mac,6);net_receive(frame,45);
    assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==3 && ip==0x06060603);
    frame[6]^=2;net_receive(frame,45);
    assert(net_udp_receive(100,out,sizeof(out),&ip,&port)==-K_EAGAIN);
    /* Expiry requires fresh resolution; late replies after failure are ignored. */
    net_poll(ARP_CACHE_TICKS);
    assert(net_resolve(0x06060603,out)==-K_EAGAIN);
    net_poll(ARP_CACHE_TICKS+100);net_poll(ARP_CACHE_TICKS+200);
    net_poll(ARP_CACHE_TICKS+300);
    net_receive(arp,42);
    assert(net_resolve(0x06060603,out)==-K_ETIMEDOUT);
    net_poll(ARP_CACHE_TICKS+600);
    assert(net_resolve(0x06060603,out)==-K_EAGAIN);
    net_receive(arp,42);assert(net_resolve(0x06060603,out)==0);
    /* Simultaneous unresolved peers are bounded; none evicts the gateway. */
    for(unsigned n=20;n<27;++n) assert(net_resolve(0x06060600+n,out)==-K_EAGAIN);
    assert(net_resolve(0x0606061b,out)==-K_ENOSPC);
    assert(net_resolve(0x08080808,out)==0 && !memcmp(out,gateway_mac,6));
    uint32_t invalid[]={0,0x7f000001,0xe0000001,0xffffffff,0x06060600,0x060606ff,0x0606060f};
    for(unsigned n=0;n<sizeof(invalid)/sizeof(invalid[0]);++n)
        assert(net_resolve(invalid[n],out)==-K_EINVAL);
    send_error=-K_EAGAIN;
    assert(net_udp_send(0x08080808,100,200,data,1)==-K_EAGAIN);
    /* Peer retry deadlines wrap, and TX failure still consumes retry budget. */
    send_error=-K_EAGAIN;
    net_init((uint8_t[]){6,7,6,7,6,7},UINT32_MAX-50);
    assert(net_resolve(0x06060603,out)==-K_EAGAIN);
    assert(peers[0].attempts==1);
    net_poll(48);assert(peers[0].attempts==1);
    net_poll(49);assert(peers[0].attempts==2);
    net_poll(149);assert(peers[0].attempts==3);
    net_poll(249);assert(net_resolve(0x06060603,out)==-K_ETIMEDOUT);
    send_error=0;
    puts("PASS: UDP checksums, validation, queues; peer ARP routing, expiry, retries and limits");
}
