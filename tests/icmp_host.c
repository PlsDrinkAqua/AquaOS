#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/net.h>

void *kmemset(void *p, int c, size_t n) { return memset(p,c,n); }
void *kmemcpy(void *d, const void *s, size_t n) { return memcpy(d,s,n); }
int kmemcmp(const void *a, const void *b, size_t n) { return memcmp(a,b,n); }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
static uint8_t sent[128];
static unsigned sends;
static int send_error;
int e1000_transmit(const void *p, size_t n) {
    assert(n <= sizeof(sent)); memcpy(sent,p,n); ++sends; return send_error;
}
#include "../kernel/kernel/NET/net.c"
#include "../kernel/kernel/NET/tcp.c"

static void start(uint32_t now) {
    uint8_t station[6] = {6,7,6,7,6,7};
    net_init(station, now);
    memcpy(gateway_mac, (uint8_t[]){0x52,0x55,6,6,6,6}, 6);
    resolved = 1;
    net_poll(now);
}
static void fix(uint8_t *frame) {
    unsigned header = (frame[14] & 15)*4;
    unsigned total = read_be16(frame+16);
    uint8_t *icmp=frame+14+header;
    frame[24]=frame[25]=0;
    write_be16(frame+24,checksum(frame+14,header));
    icmp[2]=icmp[3]=0;
    write_be16(icmp+2,checksum(icmp,total-header));
}
int main(void) {
    start(0);
    assert(ping_state == 1 && checksum(sent+14,20)==0 && checksum(sent+34,19)==0);
    uint8_t reply[80]={0}; memcpy(reply,sent,53);
    memcpy(reply,local_mac,6); memcpy(reply+6,gateway_mac,6);
    memcpy(reply+26,gateway_ip,4); memcpy(reply+30,local_ip,4);
    reply[34]=0; fix(reply);
    for(unsigned n=0;n<53;++n) { net_receive(reply,n); assert(ping_state==1); }
    /* Reject corruption even when length is valid. */
    uint8_t bad[80];
    memcpy(bad,reply,80);bad[24]^=1;net_receive(bad,53);assert(ping_state==1);
    memcpy(bad,reply,80);bad[36]^=1;net_receive(bad,53);assert(ping_state==1);
    /* Semantic rejection even with valid checksums. */
    unsigned offsets[]={0,6,14,20,23,26,30,34,35,38,40,42};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        memcpy(bad,reply,80);bad[offsets[i]]^=1;
        if(offsets[i]!=14) fix(bad);
        net_receive(bad,53);assert(ping_state==1);
    }
    memcpy(bad,reply,80);bad[22]=0;fix(bad);net_receive(bad,53);assert(ping_state==1);
    memcpy(bad,reply,80);bad[14]=0x4f;net_receive(bad,53);assert(ping_state==1);
    memcpy(bad,reply,80);write_be16(bad+16,100);net_receive(bad,53);assert(ping_state==1);
    net_receive(reply,60);assert(ping_state==2); /* Ethernet padding is ignored. */
    unsigned before=sends;net_poll(400);assert(sends==before);
    start(UINT32_MAX-100);net_poll(198);assert(ping_state==1);
    net_poll(199);assert(ping_state==3);
    net_receive(reply,53);assert(ping_state==3); /* Late reply ignored. */
    send_error=-11;start(0);assert(ping_state==3);
    send_error=0;start(0);net_receive(reply,53);assert(ping_state==2);
    assert(net_ping_start(10,0x06060603,0)==-K_EAGAIN);
    assert(net_ping_start(10,0x06060606,100)==0);
    assert(net_ping_start(11,0x06060606,100)==-K_EBUSY);
    assert(net_ping_result(11)==-K_EPERM);
    assert(net_ping_result(10)==-K_EAGAIN);
    net_receive(reply,53);assert(ping_state==1); /* Previous sequence rejected. */
    net_poll(400);assert(net_ping_result(10)==-K_ETIMEDOUT);
    assert(net_ping_start(11,0x06060606,401)==0);
    net_ping_cancel(10);assert(ping_owner==11);
    net_ping_cancel(11);assert(ping_owner==0);
    assert(net_ping_start(12,0x06060606,402)==0);
    memcpy(reply,sent,53);memcpy(reply,local_mac,6);memcpy(reply+6,gateway_mac,6);
    memcpy(reply+26,gateway_ip,4);memcpy(reply+30,local_ip,4);reply[34]=0;fix(reply);
    net_poll(405);net_receive(reply,53);assert(net_ping_result(12)==30);
    assert(net_ping_result(12)==-K_EPERM);
    /* Remote destinations use the gateway MAC but match the remote source IP. */
    start(0);ping_state=3;
    assert(net_ping_start(42,0x08080808,0)==0);
    assert(!memcmp(sent,gateway_mac,6) && !memcmp(sent+30,(uint8_t[]){8,8,8,8},4));
    memcpy(reply,sent,53);memcpy(reply,local_mac,6);memcpy(reply+6,gateway_mac,6);
    memcpy(reply+26,(uint8_t[]){8,8,4,4},4);memcpy(reply+30,local_ip,4);reply[34]=0;fix(reply);
    net_receive(reply,53);assert(net_ping_result(42)==-K_EAGAIN);
    memcpy(reply+26,(uint8_t[]){8,8,8,8},4);fix(reply);
    net_poll(5);net_receive(reply,53);assert(net_ping_result(42)==50);
    /* Direct local ARP and timeout work even without a resolved gateway. */
    net_init((uint8_t[]){6,7,6,7,6,7},0);ping_state=3;
    assert(net_ping_start(43,0x06060603,0)==-K_EAGAIN);
    assert(!memcmp(sent+38,(uint8_t[]){6,6,6,3},4));
    uint8_t arp[42],peer[6]={0x52,0x55,6,6,6,3};memcpy(arp,sent,42);
    memcpy(arp,local_mac,6);memcpy(arp+6,peer,6);arp[21]=2;
    memcpy(arp+22,peer,6);memcpy(arp+28,(uint8_t[]){6,6,6,3},4);
    memcpy(arp+32,local_mac,6);memcpy(arp+38,local_ip,4);net_receive(arp,42);
    assert(!resolved && net_ping_start(43,0x06060603,0)==0);
    assert(!memcmp(sent,peer,6));
    net_poll(300);assert(net_ping_result(43)==-K_ETIMEDOUT);
    assert(net_ping_start(44,0x06060603,301)==0);
    memcpy(reply,sent,53);memcpy(reply,local_mac,6);memcpy(reply+6,peer,6);
    memcpy(reply+26,(uint8_t[]){6,6,6,3},4);memcpy(reply+30,local_ip,4);reply[34]=0;fix(reply);
    net_poll(302);net_receive(reply,53);assert(net_ping_result(44)==10);
    uint32_t invalid[]={0,0x7f000001,0xe0000001,0xffffffff,0x06060600,0x060606ff,0x0606060f};
    for(unsigned j=0;j<sizeof(invalid)/sizeof(invalid[0]);++j)
        assert(net_ping_start(44,invalid[j],303)==-K_EINVAL);
    puts("PASS: ICMP validation, remote/local routing, ARP, timeout, ownership and wrap");
}
