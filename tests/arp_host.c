#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <kernel/net.h>

void *kmemcpy(void *d, const void *s, size_t n) { return memcpy(d,s,n); }
int kmemcmp(const void *a, const void *b, size_t n) { return memcmp(a,b,n); }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
static uint8_t sent[64];
static unsigned sends;
int e1000_transmit(const void *frame, size_t n) {
    assert(n <= sizeof(sent)); memcpy(sent, frame, n); ++sends; return 0;
}
static const uint8_t guest[6] = {6,7,6,7,6,7};
static const uint8_t gateway[6] = {0x52,0x55,6,6,6,6};

int main(void) {
    uint8_t reply[42], out[6];
    /* Retry arithmetic also works across tick wrap. */
    net_init(guest, UINT32_MAX - 50);
    assert(sends == 1 && !net_gateway_mac(out));
    assert(sent[20] == 0 && sent[21] == 1);
    assert(!memcmp(sent + 22, guest, 6));
    assert(!memcmp(sent + 38, (uint8_t[]){6,6,6,6}, 4));
    net_poll(48); assert(sends == 1);
    net_poll(49); assert(sends == 2);
    net_poll(149); assert(sends == 3);
    net_poll(249); assert(sends == 3);
    memcpy(reply, sent, 42);
    memcpy(reply, guest, 6); memcpy(reply + 6, gateway, 6);
    reply[21] = 2;
    memcpy(reply + 22, gateway, 6);
    memcpy(reply + 28, (uint8_t[]){6,6,6,6}, 4);
    memcpy(reply + 32, guest, 6);
    memcpy(reply + 38, (uint8_t[]){6,6,6,15}, 4);
    for (unsigned n = 0; n < 42; ++n) {
        net_receive(reply, n); assert(!net_gateway_mac(out));
    }
    /* Malformed types, sizes, opcode, source/destination and target identity. */
    unsigned offsets[] = {0,6,12,14,16,18,19,20,21,22,28,32,38};
    for (unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
        uint8_t bad[42]; memcpy(bad, reply, 42); bad[offsets[i]] ^= 0x80;
        net_receive(bad, 42); assert(!net_gateway_mac(out));
    }
    uint8_t bad[42]; memcpy(bad,reply,42);
    bad[6] |= 1; bad[22] |= 1;
    net_receive(bad,42); assert(!net_gateway_mac(out));
    net_receive(reply,42);
    assert(net_gateway_mac(out) && !memcmp(out,gateway,6));
    /* Ping is covered separately; no further polling in this ARP-only test. */
    /* Requests for our IP produce a unicast reply with reversed peer fields. */
    reply[21] = 1; memset(reply + 32, 0, 6);
    memset(reply,255,6);
    net_receive(reply,42);
    assert(sends == 4 && sent[21] == 2);
    assert(!memcmp(sent,gateway,6) && !memcmp(sent+32,gateway,6));
    assert(!memcmp(sent+38,(uint8_t[]){6,6,6,6},4));
    puts("PASS: ARP validation, retries, cache, replies");
}
