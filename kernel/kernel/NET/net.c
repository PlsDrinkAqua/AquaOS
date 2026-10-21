#include <kernel/net.h>
#include <kernel/tcp.h>
#include <kernel/errno.h>
#include <kernel/e1000.h>
#include <libk/string.h>
#include <libk/stdio.h>

#ifdef AQUAOS_NET_DEBUG
#define net_log(...) kprintf(__VA_ARGS__)
#else
/* Timer-context diagnostics must not overwrite the interactive prompt. */
static void net_log(const char *format, ...) { (void)format; }
#endif

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

/* Gateway stays pinned; peer entries have bounded retries and cache lifetime. */
#define ARP_PEERS 8U
#define ARP_CACHE_TICKS 6000U
#define ARP_FAILED_TICKS 300U
static struct arp_peer {
    uint32_t ip, changed;
    uint8_t mac[6];
    unsigned attempts;
    int state; /* 0=free, 1=pending, 2=resolved, 3=failed. */
} peers[ARP_PEERS];
static uint32_t network_now;
static int send_arp(unsigned, const uint8_t[6], const uint8_t[6], const uint8_t[4]);

static uint32_t read_ip(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int valid_destination(uint32_t ip) {
    return (ip >> 24) && (ip >> 28) < 14 && (ip >> 24) != 127 &&
        ip != 0x06060600U && ip != 0x060606ffU && ip != 0x0606060fU;
}

static void peer_request(struct arp_peer *peer) {
    static const uint8_t unknown[6];
    uint8_t ip[4];
    for (unsigned i = 0; i < 4; ++i) ip[i] = peer->ip >> (24 - i * 8);
    peer->changed = network_now;
    ++peer->attempts; /* Count failures too, so a full TX queue cannot spin. */
    send_arp(1, broadcast, unknown, ip);
}

/* Resolve the IPv4 next hop. A miss starts ARP, never queues the IP packet. */
int net_resolve(uint32_t destination, uint8_t out[6]) {
    if (!out || !valid_destination(destination)) return -K_EINVAL;
    if (!ready) return -K_ENODEV;
    if ((destination & 0xffffff00U) != 0x06060600U || destination == 0x06060606U) {
        if (!resolved) return -K_EAGAIN;
        kmemcpy(out, gateway_mac, 6);
        return 0;
    }
    struct arp_peer *slot = 0;
    for (unsigned i = 0; i < ARP_PEERS; ++i) {
        struct arp_peer *peer = &peers[i];
        if (peer->state == 2 && (uint32_t)(network_now - peer->changed) >= ARP_CACHE_TICKS)
            peer->state = 0;
        if (peer->state == 3 && (uint32_t)(network_now - peer->changed) >= ARP_FAILED_TICKS)
            peer->state = 0;
        if (!peer->state) { if (!slot) slot = peer; continue; }
        if (peer->ip != destination) continue;
        if (peer->state == 2) { kmemcpy(out, peer->mac, 6); return 0; }
        return peer->state == 3 ? -K_ETIMEDOUT : -K_EAGAIN;
    }
    if (!slot) return -K_ENOSPC;
    slot->ip = destination;
    slot->state = 1;
    slot->attempts = 0;
    peer_request(slot);
    return -K_EAGAIN;
}

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

/* Fixed storage keeps receive processing allocation-free in timer context. */
#define UDP_PORTS 8
#define UDP_DEPTH 4
struct udp_packet {
    uint32_t ip;
    uint16_t port, length;
    uint8_t data[NET_UDP_MAX_PAYLOAD];
};
static struct {
    uint16_t port;
    unsigned head, count;
    struct udp_packet packets[UDP_DEPTH];
} udp_ports[UDP_PORTS];

static uint16_t udp_checksum(const uint8_t *ip, const uint8_t *udp, size_t n) {
    uint32_t sum = (uint16_t)~checksum(ip + 12, 8);
    sum += 17 + n;
    sum += (uint16_t)~checksum(udp, n);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

int net_udp_bind(uint16_t port) {
    if (!port) return -K_EINVAL;
    int free_slot = -1;
    for (unsigned i = 0; i < UDP_PORTS; ++i) {
        if (udp_ports[i].port == port) return -K_EBUSY;
        if (!udp_ports[i].port) free_slot = (int)i;
    }
    if (free_slot < 0) return -K_ENOSPC;
    udp_ports[free_slot].head = udp_ports[free_slot].count = 0;
    udp_ports[free_slot].port = port;
    return 0;
}

void net_udp_unbind(uint16_t port) {
    for (unsigned i = 0; i < UDP_PORTS; ++i)
        if (udp_ports[i].port == port) {
            udp_ports[i].port = 0;
            udp_ports[i].count = 0;
        }
}

int net_udp_send(uint32_t destination, uint16_t source_port,
                 uint16_t destination_port, const void *data, size_t length) {
    if (!destination_port || length > NET_UDP_MAX_PAYLOAD || (!data && length))
        return -K_EINVAL;
    uint8_t next_mac[6];
    int result = net_resolve(destination, next_mac);
    if (result) return result;
    uint8_t frame[14 + 20 + 8 + NET_UDP_MAX_PAYLOAD] = {0};
    kmemcpy(frame, next_mac, 6);
    kmemcpy(frame + 6, local_mac, 6);
    write_be16(frame + 12, 0x0800);
    uint8_t *ip = frame + 14, *udp = ip + 20;
    ip[0] = 0x45;
    write_be16(ip + 2, 28 + length);
    write_be16(ip + 6, 0x4000);
    ip[8] = 64; ip[9] = 17;
    kmemcpy(ip + 12, local_ip, 4);
    for (unsigned i = 0; i < 4; ++i) ip[16 + i] = destination >> (24 - 8 * i);
    write_be16(ip + 10, checksum(ip, 20));
    write_be16(udp, source_port);
    write_be16(udp + 2, destination_port);
    write_be16(udp + 4, 8 + length);
    if (length) kmemcpy(udp + 8, data, length);
    uint16_t check = udp_checksum(ip, udp, 8 + length);
    write_be16(udp + 6, check ? check : 0xffff);
    return e1000_transmit(frame, 42 + length);
}

static void receive_udp(const uint8_t *ip, unsigned header, unsigned total) {
    const uint8_t *udp = ip + header;
    unsigned bytes = read_be16(udp + 4);
    if (bytes < 8 || bytes != total - header || bytes - 8 > NET_UDP_MAX_PAYLOAD ||
        (read_be16(udp + 6) && udp_checksum(ip, udp, bytes))) return;
    uint16_t port = read_be16(udp + 2);
    if (!port) return;
    for (unsigned i = 0; i < UDP_PORTS; ++i) {
        if (udp_ports[i].port != port) continue;
        if (udp_ports[i].count == UDP_DEPTH) return; /* Drop newest. */
        struct udp_packet *p = &udp_ports[i].packets[
            (udp_ports[i].head + udp_ports[i].count) % UDP_DEPTH];
        p->ip = ((uint32_t)read_be16(ip + 12) << 16) | read_be16(ip + 14);
        p->port = read_be16(udp);
        p->length = bytes - 8;
        kmemcpy(p->data, udp + 8, p->length);
        ++udp_ports[i].count;
        return;
    }
}

int net_udp_receive(uint16_t port, void *data, size_t capacity,
                    uint32_t *source_ip, uint16_t *source_port) {
    if (!port || (!data && capacity) || !source_ip || !source_port) return -K_EINVAL;
    for (unsigned i = 0; i < UDP_PORTS; ++i) {
        if (udp_ports[i].port != port) continue;
        if (!udp_ports[i].count) return -K_EAGAIN;
        struct udp_packet *p = &udp_ports[i].packets[udp_ports[i].head];
        if (capacity < p->length) return -K_ERANGE; /* Keep datagram for retry. */
        if (p->length) kmemcpy(data, p->data, p->length);
        *source_ip = p->ip; *source_port = p->port;
        int length = p->length;
        udp_ports[i].head = (udp_ports[i].head + 1) % UDP_DEPTH;
        --udp_ports[i].count;
        return length;
    }
    return -K_ENOENT;
}

int net_ipv4_send(uint32_t destination, unsigned protocol, const void *payload, size_t length) {
    if ((!payload && length) || length > 1480) return -K_EINVAL;
    uint8_t frame[1514] = {0};
    int result = net_resolve(destination, frame);
    if (result) return result;
    kmemcpy(frame+6,local_mac,6);write_be16(frame+12,0x0800);
    uint8_t *ip=frame+14;
    ip[0]=0x45;write_be16(ip+2,20+length);write_be16(ip+6,0x4000);
    ip[8]=64;ip[9]=protocol;
    kmemcpy(ip+12,local_ip,4);
    for(unsigned i=0;i<4;++i) ip[16+i]=destination>>(24-8*i);
    write_be16(ip+10,checksum(ip,20));
    if(length) kmemcpy(ip+20,payload,length);
    return e1000_transmit(frame,34+length);
}

static void receive_ipv4(const uint8_t *frame, size_t length) {
    if (length < 34 || kmemcmp(frame, local_mac, 6)) return;
    const uint8_t *ip = frame + 14;
    unsigned header = (ip[0] & 15) * 4U;
    if ((ip[0] >> 4) != 4 || header < 20 || header > length - 14) return;
    unsigned total = read_be16(ip + 2);
    if (total < header + 8 || total > length - 14 || checksum(ip, header) ||
        (read_be16(ip + 6) & 0xbfff) || !ip[8] || kmemcmp(ip + 16, local_ip, 4)) return;
    uint32_t source = read_ip(ip + 12);
    if (!valid_destination(source)) return;
    const uint8_t *expected = 0;
    if ((source & 0xffffff00U) != 0x06060600U || source == 0x06060606U) {
        if (resolved) expected = gateway_mac;
    } else {
        for (unsigned i = 0; i < ARP_PEERS; ++i)
            if (peers[i].state == 2 && peers[i].ip == source &&
                (uint32_t)(network_now - peers[i].changed) < ARP_CACHE_TICKS)
                expected = peers[i].mac;
    }
    /* A router may deliver traffic from an on-link source too. In particular,
     * QEMU's DNS proxy answers ARP with its own MAC but emits IP replies via
     * the gateway MAC. Accept either the peer or the resolved gateway. */
    if ((!expected || kmemcmp(frame + 6, expected, 6)) &&
        (!resolved || kmemcmp(frame + 6, gateway_mac, 6))) return;
    if (ip[9] == 6) { net_tcp_input(ip, header, total); return; }
    if (ip[9] == 17) {
        receive_udp(ip, header, total);
        return;
    }
    if (ip[9] != 1 || ping_state != 1 || kmemcmp(ip + 12, gateway_ip, 4)) return;
    const uint8_t *icmp = ip + header;
    unsigned bytes = total - header;
    if (bytes != PING_BYTES || checksum(icmp, bytes) || icmp[0] || icmp[1] ||
        read_be16(icmp + 4) != PING_ID || read_be16(icmp + 6) != ping_sequence ||
        kmemcmp(icmp + 8, ping_payload, sizeof(ping_payload) - 1)) return;
    ping_state = 2;
    if (!ping_owner) net_log("ICMP: reply from 6.6.6.6 seq=1\n");
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
#ifdef AQUAOS_UDP_ECHO_TEST_PORT
    net_udp_echo_test_reset();
#endif
    kmemcpy(local_mac, mac, 6);
    for (unsigned i = 0; i < UDP_PORTS; ++i) {
        udp_ports[i].port = 0; udp_ports[i].count = 0;
    }
    for (unsigned i = 0; i < ARP_PEERS; ++i) peers[i].state = 0;
    net_tcp_init();
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
    network_now = now;
    net_tcp_poll(now);
    for (unsigned i = 0; i < ARP_PEERS; ++i) {
        struct arp_peer *peer = &peers[i];
        if (peer->state != 1 || (uint32_t)(now - peer->changed) < ARP_RETRY_TICKS)
            continue;
        if (peer->attempts < ARP_MAX_ATTEMPTS) peer_request(peer);
        else { peer->state = 3; peer->changed = now; }
    }
    if (ping_state == 1) ping_elapsed = (uint32_t)(now - ping_started);
    if (resolved) {
#ifdef AQUAOS_UDP_ECHO_TEST_PORT
        if (ping_state == 2) net_udp_echo_test_poll(now);
#endif
        if (!ping_state) {
            ping_started = now;
            if (!send_ping()) {
                ping_state = 1;
                net_log("ICMP: request to 6.6.6.6 seq=1\n");
            } else {
                ping_state = 3;
                net_log("ICMP: send failed for 6.6.6.6\n");
            }
        } else if (ping_state == 1 && (uint32_t)(now - ping_started) >= PING_TIMEOUT_TICKS) {
            ping_state = 3;
            if (!ping_owner) net_log("ICMP: timeout from 6.6.6.6 seq=1\n");
        }
        return;
    }
    if (attempts >= ARP_MAX_ATTEMPTS ||
        (uint32_t)(now - last_request) < ARP_RETRY_TICKS) return;
    static const uint8_t unknown[6];
    last_request = now;
    ++attempts; /* Bound attempts even if the TX queue is full. */
    if (!send_arp(1, broadcast, unknown, gateway_ip))
        net_log("ARP: request gateway 6.6.6.6 (%u/3)\n", attempts);
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
    /* Learn only solicited replies addressed to this interface. */
    if (opcode == 2 && !kmemcmp(frame, local_mac, 6)) {
        uint32_t source = read_ip(sender_ip);
        for (unsigned i = 0; i < ARP_PEERS; ++i) {
            struct arp_peer *peer = &peers[i];
            if (peer->state != 1 || peer->ip != source) continue;
            kmemcpy(peer->mac, sender_mac, 6);
            peer->state = 2;
            peer->changed = network_now;
            break;
        }
    }
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
        net_log("ARP: gateway 6.6.6.6 MAC=%s\n", text);
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
