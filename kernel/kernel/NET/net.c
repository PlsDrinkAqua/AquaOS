#include <kernel/net.h>
#include <kernel/e1000.h>
#include <libk/string.h>
#include <libk/stdio.h>

static const uint8_t local_ip[4] = {10, 0, 2, 15};
static const uint8_t gateway_ip[4] = {10, 0, 2, 2};
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
static uint8_t local_mac[6], gateway_mac[6];
static int ready, resolved;
static unsigned attempts;
static uint32_t last_request;
#define ARP_RETRY_TICKS 100U /* Current PIT configuration: 100 Hz. */
#define ARP_MAX_ATTEMPTS 3U

static unsigned read_be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static int valid_sender(const uint8_t *mac) {
    static const uint8_t zero[6];
    return !(mac[0] & 1) && kmemcmp(mac, zero, 6) && kmemcmp(mac, local_mac, 6);
}

static int send_arp(unsigned opcode, const uint8_t destination[6],
                    const uint8_t target_mac[6], const uint8_t target_ip[4]) {
    /* Ethernet + Ethernet/IPv4 ARP; transmit adds padding and FCS. */
    uint8_t frame[42] = {0};
    kmemcpy(frame, destination, 6);
    kmemcpy(frame + 6, local_mac, 6);
    frame[12] = 0x08; frame[13] = 0x06;
    frame[15] = 1; /* Ethernet hardware type, network byte order. */
    frame[16] = 0x08; frame[17] = 0x00; /* IPv4 protocol type. */
    frame[18] = 6; frame[19] = 4;
    frame[21] = opcode;
    kmemcpy(frame + 22, local_mac, 6);
    kmemcpy(frame + 28, local_ip, 4);
    kmemcpy(frame + 32, target_mac, 6);
    kmemcpy(frame + 38, target_ip, 4);
    return e1000_transmit(frame, sizeof(frame));
}

void net_init(const uint8_t mac[6], uint32_t now) {
    kmemcpy(local_mac, mac, 6);
    resolved = 0;
    attempts = 0;
    ready = 1;
    last_request = now - ARP_RETRY_TICKS;
    kprintf("NET: static IP=10.0.2.15/24 gateway=10.0.2.2\n");
    net_poll(now);
}

void net_poll(uint32_t now) {
    if (!ready || resolved || attempts >= ARP_MAX_ATTEMPTS ||
        (uint32_t)(now - last_request) < ARP_RETRY_TICKS) return;
    static const uint8_t unknown[6];
    last_request = now;
    ++attempts; /* Bound attempts even if the TX queue is full. */
    if (!send_arp(1, broadcast, unknown, gateway_ip))
        kprintf("ARP: request gateway 10.0.2.2 (%u/3)\n", attempts);
}

void net_receive(const uint8_t *frame, size_t length) {
    if (!ready || !frame || length < 42 || read_be16(frame + 12) != 0x0806)
        return;
    if (kmemcmp(frame, local_mac, 6) && kmemcmp(frame, broadcast, 6)) return;
    if (read_be16(frame + 14) != 1 || read_be16(frame + 16) != 0x0800 ||
        frame[18] != 6 || frame[19] != 4) return;
    unsigned opcode = read_be16(frame + 20);
    if (opcode != 1 && opcode != 2) return;
    const uint8_t *sender_mac = frame + 22, *sender_ip = frame + 28;
    if (!valid_sender(sender_mac) || kmemcmp(frame + 6, sender_mac, 6) ||
        kmemcmp(frame + 38, local_ip, 4)) return;
    if (opcode == 2 && kmemcmp(frame + 32, local_mac, 6)) return;
    /* Only a solicited, directed reply can populate this first gateway cache. */
    if (opcode == 2 && attempts && !resolved &&
        !kmemcmp(frame, local_mac, 6) && !kmemcmp(sender_ip, gateway_ip, 4)) {
        kmemcpy(gateway_mac, sender_mac, 6);
        resolved = 1;
        static const char hex[] = "0123456789abcdef";
        char text[18];
        for (unsigned i = 0; i < 6; ++i) {
            text[i * 3] = hex[gateway_mac[i] >> 4];
            text[i * 3 + 1] = hex[gateway_mac[i] & 15];
            text[i * 3 + 2] = ':';
        }
        text[17] = 0;
        kprintf("ARP: gateway 10.0.2.2 MAC=%s\n", text);
    }
    if (opcode == 1) send_arp(2, sender_mac, sender_mac, sender_ip);
}

int net_gateway_mac(uint8_t out[6]) {
    if (!resolved || !out) return 0;
    kmemcpy(out, gateway_mac, 6);
    return 1;
}
