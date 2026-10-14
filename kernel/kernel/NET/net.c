#include <kernel/net.h>
#include <kernel/errno.h>
#include <kernel/e1000.h>
#include <libk/string.h>
#include <libk/stdio.h>

static const uint8_t local_ip[4] = {6, 6, 6, 15};
static const uint8_t gateway_ip[4] = {6, 6, 6, 6};
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
static uint8_t local_mac[6], gateway_mac[6];
static int ready, resolved;
static unsigned attempts;
static uint32_t last_request;
static uint32_t ping_started;
static uint32_t ping_owner, ping_elapsed;
static uint16_t ping_sequence = 1;
static int ping_state; /* 0=unsent, 1=waiting, 2=reply, 3=timeout/send failure. */
#define PING_TIMEOUT_TICKS 300U
#define PING_ID 0xa901

#define ARP_RETRY_TICKS 100U /* Current PIT configuration: 100 Hz. */
#define ARP_MAX_ATTEMPTS 3U

static unsigned read_be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

/* Internet checksum over network-order bytes; includes odd trailing bytes. */
static uint16_t checksum(const uint8_t *p, size_t length) {
    uint32_t sum = 0;
    while (length >= 2) {
        sum += read_be16(p);
        p += 2; length -= 2;
    }
    if (length) sum += (unsigned)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static void write_be16(uint8_t *p, unsigned value) {
    p[0] = value >> 8; p[1] = value;
}

static const uint8_t ping_payload[] = "AquaOS ping";
#define PING_BYTES (8U + sizeof(ping_payload) - 1U)

static int send_ping(void) {
    uint8_t frame[14 + 20 + PING_BYTES] = {0};
    kmemcpy(frame, gateway_mac, 6);
    kmemcpy(frame + 6, local_mac, 6);
    write_be16(frame + 12, 0x0800);
    uint8_t *ip = frame + 14, *icmp = ip + 20;
    ip[0] = 0x45;
    write_be16(ip + 2, 20 + PING_BYTES);
    write_be16(ip + 4, 1);
    write_be16(ip + 6, 0x4000); /* Don't fragment. */
    ip[8] = 64; ip[9] = 1; /* TTL and ICMP protocol. */
    kmemcpy(ip + 12, local_ip, 4);
    kmemcpy(ip + 16, gateway_ip, 4);
    write_be16(ip + 10, checksum(ip, 20));
    icmp[0] = 8; /* Echo request, code 0. */
    write_be16(icmp + 4, PING_ID);
    write_be16(icmp + 6, ping_sequence);
    kmemcpy(icmp + 8, ping_payload, sizeof(ping_payload) - 1);
    write_be16(icmp + 2, checksum(icmp, PING_BYTES));
    return e1000_transmit(frame, sizeof(frame));
}

static void receive_ipv4(const uint8_t *frame, size_t length) {
    if (ping_state != 1 || length < 34 || kmemcmp(frame, local_mac, 6) ||
        kmemcmp(frame + 6, gateway_mac, 6)) return;
    const uint8_t *ip = frame + 14;
    unsigned header = (ip[0] & 15) * 4U;
    if ((ip[0] >> 4) != 4 || header < 20 || header > length - 14) return;
    unsigned total = read_be16(ip + 2);
    if (total < header + 8 || total > length - 14 || checksum(ip, header) ||
        (read_be16(ip + 6) & 0xbfff) || !ip[8] || ip[9] != 1 ||
        kmemcmp(ip + 12, gateway_ip, 4) || kmemcmp(ip + 16, local_ip, 4)) return;
    const uint8_t *icmp = ip + header;
    unsigned bytes = total - header;
    if (bytes != PING_BYTES || checksum(icmp, bytes) || icmp[0] || icmp[1] ||
        read_be16(icmp + 4) != PING_ID || read_be16(icmp + 6) != ping_sequence ||
        kmemcmp(icmp + 8, ping_payload, sizeof(ping_payload) - 1)) return;
    ping_state = 2;
    if (!ping_owner) kprintf("ICMP: reply from 6.6.6.6 seq=1\n");
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
    ping_state = 0;
    ping_owner = 0;
    ping_sequence = 1;
    attempts = 0;
    ready = 1;
    last_request = now - ARP_RETRY_TICKS;
    kprintf("NET: static IP=6.6.6.15/24 gateway=6.6.6.6\n");
    net_poll(now);
}

void net_poll(uint32_t now) {
    if (!ready) return;
    if (ping_state == 1) ping_elapsed = (uint32_t)(now - ping_started);
    if (resolved) {
        if (!ping_state) {
            ping_started = now;
            if (!send_ping()) {
                ping_state = 1;
                kprintf("ICMP: request to 6.6.6.6 seq=1\n");
            } else {
                ping_state = 3;
                kprintf("ICMP: send failed for 6.6.6.6\n");
            }
        } else if (ping_state == 1 && (uint32_t)(now - ping_started) >= PING_TIMEOUT_TICKS) {
            ping_state = 3;
            if (!ping_owner) kprintf("ICMP: timeout from 6.6.6.6 seq=1\n");
        }
        return;
    }
    if (attempts >= ARP_MAX_ATTEMPTS ||
        (uint32_t)(now - last_request) < ARP_RETRY_TICKS) return;
    static const uint8_t unknown[6];
    last_request = now;
    ++attempts; /* Bound attempts even if the TX queue is full. */
    if (!send_arp(1, broadcast, unknown, gateway_ip))
        kprintf("ARP: request gateway 6.6.6.6 (%u/3)\n", attempts);
}

void net_receive(const uint8_t *frame, size_t length) {
    if (!ready || !frame || length < 14) return;
    if (read_be16(frame + 12) == 0x0800) {
        receive_ipv4(frame, length);
        return;
    }
    if (length < 42 || read_be16(frame + 12) != 0x0806) return;
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
        kprintf("ARP: gateway 6.6.6.6 MAC=%s\n", text);
    }
    if (opcode == 1) send_arp(2, sender_mac, sender_mac, sender_ip);
}

int net_gateway_mac(uint8_t out[6]) {
    if (!resolved || !out) return 0;
    kmemcpy(out, gateway_mac, 6);
    return 1;
}

/* One outstanding userspace probe. Syscall entry and timer have IRQs disabled. */
int net_ping_start(uint32_t pid, uint32_t ip, uint32_t now) {
    if (ip != 0x06060606U) return -K_EINVAL;
    if (!ready) return -K_ENODEV;
    if (!resolved) return -K_EAGAIN;
    if (ping_owner || ping_state == 1) return -K_EBUSY;
    ++ping_sequence;
    if (!ping_sequence) ++ping_sequence;
    int result = send_ping();
    if (result) return result;
    ping_owner = pid;
    ping_started = now;
    ping_elapsed = 0;
    ping_state = 1;
    return 0;
}

int net_ping_result(uint32_t pid) {
    if (!ping_owner || ping_owner != pid) return -K_EPERM;
    if (ping_state == 1) return -K_EAGAIN;
    int result = ping_state == 2 ? (int)(ping_elapsed * 10U) : -K_ETIMEDOUT;
    ping_owner = 0;
    return result;
}

void net_ping_cancel(uint32_t pid) {
    if (ping_owner == pid) {
        ping_owner = 0;
        ping_state = 3;
    }
}
