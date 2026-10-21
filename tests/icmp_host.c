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
    assert(net_ping_start(10,0x06060603,0)==-K_EINVAL);
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
    puts("PASS: IPv4/ICMP validation, padding, timeout, wrap and send failure");
}
