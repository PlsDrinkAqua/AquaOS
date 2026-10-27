#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/net.h>
void *kmemset(void *p, int c, size_t n) { return memset(p,c,n); }
void *kmemcpy(void *d, const void *s, size_t n) { return memcpy(d,s,n); }
int kmemcmp(const void *a, const void *b, size_t n) { return memcmp(a,b,n); }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
int e1000_link_up(void) { return 1; }
static uint8_t sent[1514], last_dhcp[342];
static size_t sent_length;
static unsigned dhcp_sends;
static int tx_error;
int e1000_transmit(const void *p, size_t n) {
    assert(n <= sizeof(sent)); memcpy(sent,p,n); sent_length=n;
    if (n >= 42 && sent[23] == 17 && sent[35] == 68) {
        assert(n == sizeof(last_dhcp)); memcpy(last_dhcp,p,n); ++dhcp_sends;
    }
    return tx_error;
}
#include "../kernel/kernel/NET/net.c"
#include "../kernel/kernel/NET/tcp.c"

static uint8_t reply[342];
static void option(unsigned *n, unsigned code, uint32_t value) {
    uint8_t *p=reply+42;
    p[(*n)++]=code; p[(*n)++]=4; put_ip(p+*n,value); *n+=4;
}
static void make_reply(unsigned type, uint32_t lease) {
    memset(reply,0,sizeof(reply));
    memcpy(reply,broadcast,6); memcpy(reply+6,(uint8_t[]){2,0,0,0,0,2},6);
    write_be16(reply+12,0x0800);
    uint8_t *ip=reply+14, *udp=reply+34, *p=reply+42;
    ip[0]=0x45; write_be16(ip+2,328); ip[8]=64; ip[9]=17;
    put_ip(ip+12,0x0a141e01); put_ip(ip+16,UINT32_MAX);
    write_be16(ip+10,checksum(ip,20));
    write_be16(udp,67); write_be16(udp+2,68); write_be16(udp+4,308);
    p[0]=2; p[1]=1; p[2]=6; put_ip(p+4,dhcp.xid);
    put_ip(p+16,0x0a141e32); memcpy(p+28,local_mac,6); put_ip(p+236,0x63825363);
    unsigned n=240; p[n++]=53; p[n++]=1; p[n++]=type;
    option(&n,54,0x0a141e01); option(&n,1,0xffffff00);
    option(&n,3,0x0a141e01); option(&n,6,0x0a141e03);
    option(&n,51,lease); p[n]=255;
}
static void start(uint32_t now) {
    net_init((uint8_t[]){6,7,6,7,6,7},now); dhcp_sends=0;
}
static void acquire(uint32_t now, uint32_t lease) {
    start(now); net_poll(now+100);
    assert(dhcp_sends==1);
    assert(!checksum(last_dhcp+14,20) && !udp_checksum(last_dhcp+14,last_dhcp+34,308));
    assert(read_ip(last_dhcp+26)==0 && read_ip(last_dhcp+30)==UINT32_MAX && last_dhcp[284]==1);
    make_reply(2,lease); net_receive(reply,sizeof(reply));
    assert(dhcp.state==AQUAOS_DHCP_REQUESTING);
    net_poll(now+101); assert(dhcp_sends==2 && last_dhcp[284]==3);
    assert(last_dhcp[294]==50 && read_ip(last_dhcp+296)==0x0a141e32);
    make_reply(5,lease);
    put_ip(reply+287,0x0a141e02); net_receive(reply,sizeof(reply));
    assert(dhcp.state==AQUAOS_DHCP_REQUESTING); /* Wrong selected server. */
    make_reply(5,lease); put_ip(reply+58,0x0a141e33);
    net_receive(reply,sizeof(reply)); assert(dhcp.state==AQUAOS_DHCP_REQUESTING);
    make_reply(5,lease); net_receive(reply,sizeof(reply));
    assert(dhcp.state==AQUAOS_DHCP_BOUND && net_local_ip()==0x0a141e32);
}
int main(void) {
    aquaos_net_info info;
    net_get_info(&info); assert(!info.flags);
    start(0); net_poll(100); make_reply(2,20);
    /* Every truncated prefix and corrupted identity must leave us selecting. */
    for(unsigned n=0;n<sizeof(reply);++n) {
        net_receive(reply,n); assert(dhcp.state==AQUAOS_DHCP_SELECTING);
    }
    unsigned offsets[]={14,24,34,36,42,43,44,46,70,278,282,285};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        unsigned at=offsets[i]; reply[at]^=1; net_receive(reply,sizeof(reply));
        assert(dhcp.state==AQUAOS_DHCP_SELECTING); reply[at]^=1;
    }
    /* Option overrun, duplicate critical option, non-contiguous mask. */
    reply[286]=250; net_receive(reply,sizeof(reply)); assert(dhcp.state==1);
    make_reply(2,20); reply[291]=53; net_receive(reply,sizeof(reply)); assert(dhcp.state==1);
    make_reply(2,20); put_ip(reply+293,0xff00ff00); net_receive(reply,sizeof(reply)); assert(dhcp.state==1);
    make_reply(2,20);
    write_be16(reply+40,udp_checksum(reply+14,reply+34,308));
    reply[300]^=1; net_receive(reply,sizeof(reply)); assert(dhcp.state==1);
    /* Checksummed malformed DHCP cannot activate configuration. */
    acquire(0,20); net_get_info(&info);
    assert(net_udp_bind(68)==-K_EBUSY);
    int old_tcp=net_tcp_socket(); assert(old_tcp>=0);
    assert(!net_tcp_connect(old_tcp,0x08080808,80));
    assert(info.ip==0x0a141e32 && info.gateway==0x0a141e01 && info.dns==0x0a141e03);
    assert(info.lease_seconds==20 && info.lease_remaining==20 && info.flags==3);
    uint8_t out[6];
    assert(net_resolve(0x0a141e03,out)==-K_EAGAIN);
    assert(read_ip(sent+38)==0x0a141e03); /* New subnet ARP, not old gateway. */
    assert(net_resolve(0x0a141e00,out)==-K_EINVAL);
    assert(net_resolve(0x0a141eff,out)==-K_EINVAL);
    net_poll(1101); assert(dhcp.state==AQUAOS_DHCP_RENEWING);
    net_poll(1901); assert(dhcp.state==AQUAOS_DHCP_REBINDING);
    assert(read_ip(last_dhcp+26)==0x0a141e32 && read_ip(last_dhcp+30)==UINT32_MAX);
    net_poll(2101); assert(dhcp.state==AQUAOS_DHCP_FALLBACK && net_local_ip()==0x0606060f);
    assert(net_tcp_status(old_tcp)==-K_ECONNRESET);
    int new_tcp=net_tcp_socket(); assert(new_tcp>=0 && new_tcp!=old_tcp);
    net_tcp_release(old_tcp); net_tcp_release(new_tcp);
    acquire(UINT32_MAX-150,20); net_poll(951);
    assert(dhcp.state==AQUAOS_DHCP_RENEWING); /* Tick wrap is harmless. */
    /* Renewal ACK extends the lease, and a matching NAK restores fallback. */
    make_reply(5,40); net_receive(reply,sizeof(reply)); assert(dhcp.state==AQUAOS_DHCP_BOUND);
    net_get_info(&info); assert(info.lease_remaining==40);
    net_poll(2951); assert(dhcp.state==AQUAOS_DHCP_RENEWING);
    make_reply(6,40); net_receive(reply,sizeof(reply)); assert(dhcp.state==AQUAOS_DHCP_FALLBACK);
    start(0); tx_error=-K_EAGAIN;
    net_poll(100); net_poll(500); net_poll(900); net_poll(1300);
    assert(dhcp_sends==3 && dhcp.state==AQUAOS_DHCP_FALLBACK);
    tx_error=0; net_poll(7300); assert(dhcp.state==AQUAOS_DHCP_SELECTING);
    acquire(0,UINT32_MAX); net_poll(2000000000);
    net_get_info(&info); assert(info.lease_remaining==UINT32_MAX && info.dhcp_state==AQUAOS_DHCP_BOUND);
    puts("PASS: DHCP wire validation, acquisition, routing, renewal, rebinding, expiry, NAK, fallback and tick wrap");
}
