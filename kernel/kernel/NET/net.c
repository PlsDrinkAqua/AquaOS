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

static uint8_t local_ip[4] = {6, 6, 6, 15};
static uint8_t gateway_ip[4] = {6, 6, 6, 6};
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
static uint8_t local_mac[6], gateway_mac[6];
static int ready, resolved;
static uint32_t subnet_mask = 0xffffff00U, dns_server = 0x06060603U;
static void dhcp_init(uint32_t now);
static void dhcp_poll(uint32_t now);
static void dhcp_receive(const uint8_t *ip, unsigned header, unsigned total);

static unsigned attempts;
static uint32_t last_request;
static uint32_t ping_started, ping_destination = 0x06060606U;
static uint32_t ping_owner, ping_elapsed;
static uint16_t ping_sequence = 1;
static int ping_state; /* 0=unsent, 1=waiting, 2=reply, 3=timeout/send failure. */
#define PING_TIMEOUT_TICKS 300U
#define PING_ID 0xa901

static void log_boot_ping(const char *action) {
    net_log("ICMP: %s %u.%u.%u.%u seq=%u\n", action,
            ping_destination >> 24, (ping_destination >> 16) & 255,
            (ping_destination >> 8) & 255, ping_destination & 255, ping_sequence);
}

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
        ip != (read_ip(local_ip) & subnet_mask) &&
        ip != (read_ip(local_ip) | ~subnet_mask) && ip != read_ip(local_ip);
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
    if ((destination & subnet_mask) != (read_ip(local_ip) & subnet_mask) || destination == read_ip(gateway_ip)) {
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

static int send_ping(uint32_t destination) {
    uint8_t icmp[PING_BYTES] = {0};
    icmp[0] = 8; /* Echo request, code 0. */
    write_be16(icmp + 4, PING_ID);
    write_be16(icmp + 6, ping_sequence);
    kmemcpy(icmp + 8, ping_payload, sizeof(ping_payload) - 1);
    write_be16(icmp + 2, checksum(icmp, PING_BYTES));
    return net_ipv4_send(destination, 1, icmp, sizeof(icmp));
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
    if (port == 68) return -K_EBUSY; /* Reserved for the DHCP client. */
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
    if (length < 34 || (kmemcmp(frame, local_mac, 6) && kmemcmp(frame, broadcast, 6))) return;
    const uint8_t *ip = frame + 14;
    unsigned header = (ip[0] & 15) * 4U;
    if ((ip[0] >> 4) != 4 || header < 20 || header > length - 14) return;
    unsigned total = read_be16(ip + 2);
    if (total < header + 8 || total > length - 14 || checksum(ip, header) ||
        (read_be16(ip + 6) & 0xbfff) || !ip[8]) return;
    /* DHCP must work before configuration/ARP, including broadcast replies.
     * This exception is restricted to server:67 -> client:68 below. */
    if (ip[9] == 17 && read_be16(ip + header) == 67 &&
        read_be16(ip + header + 2) == 68) {
        dhcp_receive(ip, header, total);
        return;
    }
    if (kmemcmp(frame, local_mac, 6) || kmemcmp(ip + 16, local_ip, 4)) return;
    uint32_t source = read_ip(ip + 12);
    if (!valid_destination(source)) return;
    const uint8_t *expected = 0;
    if ((source & subnet_mask) != (read_ip(local_ip) & subnet_mask) || source == read_ip(gateway_ip)) {
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
    if (ip[9] != 1 || ping_state != 1 || read_ip(ip + 12) != ping_destination) return;
    const uint8_t *icmp = ip + header;
    unsigned bytes = total - header;
    if (bytes != PING_BYTES || checksum(icmp, bytes) || icmp[0] || icmp[1] ||
        read_be16(icmp + 4) != PING_ID || read_be16(icmp + 6) != ping_sequence ||
        kmemcmp(icmp + 8, ping_payload, sizeof(ping_payload) - 1)) return;
    ping_state = 2;
    if (!ping_owner) log_boot_ping("reply from");
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
    kmemcpy(local_ip, (uint8_t[]){6,6,6,15}, 4);
    kmemcpy(gateway_ip, (uint8_t[]){6,6,6,6}, 4);
    subnet_mask = 0xffffff00U; dns_server = 0x06060603U;
    network_now = now;
    dhcp_init(now);
    for (unsigned i = 0; i < UDP_PORTS; ++i) {
        udp_ports[i].port = 0; udp_ports[i].count = 0;
    }
    for (unsigned i = 0; i < ARP_PEERS; ++i) peers[i].state = 0;
    net_tcp_init();
    resolved = 0;
    ping_state = 0;
    ping_owner = 0;
    ping_sequence = 1;
    ping_destination = 0x06060606U;
    attempts = 0;
    ready = 1;
    last_request = now - ARP_RETRY_TICKS;
    kprintf("NET: static IP=6.6.6.15/24 gateway=6.6.6.6 (DHCP pending)\n");
    net_poll(now);
}

void net_poll(uint32_t now) {
    if (!ready) return;
    network_now = now;
    dhcp_poll(now);
    net_tcp_poll(now);
    for (unsigned i = 0; i < ARP_PEERS; ++i) {
        struct arp_peer *peer = &peers[i];
        if (peer->state != 1 || (uint32_t)(now - peer->changed) < ARP_RETRY_TICKS)
            continue;
        if (peer->attempts < ARP_MAX_ATTEMPTS) peer_request(peer);
        else { peer->state = 3; peer->changed = now; }
    }
    if (ping_state == 1) {
        ping_elapsed = (uint32_t)(now - ping_started);
        if (ping_elapsed >= PING_TIMEOUT_TICKS) {
            ping_state = 3;
            if (!ping_owner) log_boot_ping("timeout from");
        }
    }
    if (resolved) {
#ifdef AQUAOS_UDP_ECHO_TEST_PORT
        if (ping_state == 2) net_udp_echo_test_poll(now);
#endif
        if (!ping_state) {
            ping_started = now;
            ping_destination = read_ip(gateway_ip);
            if (!send_ping(ping_destination)) {
                ping_state = 1;
                log_boot_ping("request to");
            } else {
                ping_state = 3;
                log_boot_ping("send failed for");
            }
        }
        return;
    }
    if (attempts >= ARP_MAX_ATTEMPTS ||
        (uint32_t)(now - last_request) < ARP_RETRY_TICKS) return;
    static const uint8_t unknown[6];
    last_request = now;
    ++attempts; /* Bound attempts even if the TX queue is full. */
    if (!send_arp(1, broadcast, unknown, gateway_ip))
        net_log("ARP: request gateway %u.%u.%u.%u (%u/3)\n",
                gateway_ip[0], gateway_ip[1], gateway_ip[2], gateway_ip[3], attempts);
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
        net_log("ARP: gateway %u.%u.%u.%u MAC=%s\n",
                gateway_ip[0], gateway_ip[1], gateway_ip[2], gateway_ip[3], text);
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
    if (!valid_destination(ip)) return -K_EINVAL;
    if (!ready) return -K_ENODEV;
    if (ping_owner || ping_state == 1) return -K_EBUSY;
    uint8_t mac[6];
    int route = net_resolve(ip, mac);
    if (route) return route;
    ++ping_sequence;
    if (!ping_sequence) ++ping_sequence;
    int result = send_ping(ip);
    if (result) return result;
    ping_destination = ip;
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

/* DHCP bootstrap bypasses ARP and ordinary UDP descriptors: the client has no
 * assigned source address yet. Only validated BOOTREPLY packets for our MAC
 * and transaction reach the state machine. All state is timer/IRQ serialized. */
static struct {
    uint32_t state, xid, offered, server, mask, router, dns;
    uint32_t lease, age, t1, t2, last_tick, fraction, last_send;
    unsigned tries;
} dhcp;

static void put_ip(uint8_t *p, uint32_t ip) {
    for (unsigned i = 0; i < 4; ++i) p[i] = ip >> (24 - i * 8);
}

uint32_t net_local_ip(void) { return read_ip(local_ip); }

void net_get_info(aquaos_net_info *out) {
    kmemset(out, 0, sizeof(*out));
    if (!ready) return;
    out->flags = AQUAOS_NET_PRESENT | (e1000_link_up() ? AQUAOS_NET_LINK_UP : 0);
    out->ip = read_ip(local_ip); out->netmask = subnet_mask;
    out->gateway = read_ip(gateway_ip); out->dns = dns_server;
    kmemcpy(out->mac, local_mac, 6);
    out->dhcp_state = dhcp.state; out->server = dhcp.server;
    out->lease_seconds = dhcp.lease;
    out->lease_remaining = dhcp.lease == UINT32_MAX ? UINT32_MAX :
        (dhcp.age < dhcp.lease ? dhcp.lease - dhcp.age : 0);
}

static void configure_network(uint32_t address, uint32_t mask,
                              uint32_t router, uint32_t dns) {
    int changed = address != read_ip(local_ip) || mask != subnet_mask ||
                  router != read_ip(gateway_ip);
    put_ip(local_ip, address); put_ip(gateway_ip, router);
    subnet_mask = mask; dns_server = dns;
    if (!changed) return;
    /* A changed address/route cannot keep transport or ARP state from the old
     * configuration. Retain descriptor ownership; fail old TCP connections. */
    net_tcp_config_changed();
    for (unsigned i = 0; i < UDP_PORTS; ++i) udp_ports[i].count = 0;
    for (unsigned i = 0; i < ARP_PEERS; ++i) peers[i].state = 0;
    resolved = 0; attempts = 0;
    last_request = network_now - ARP_RETRY_TICKS;
    ping_state = 3;
}

static void dhcp_init(uint32_t now) {
    uint32_t xid = dhcp.xid + 1;
    kmemset(&dhcp, 0, sizeof(dhcp));
    dhcp.xid = xid ^ now ^ read_ip(local_mac + 2) ^ 0xa90d0000U;
    dhcp.state = AQUAOS_DHCP_SELECTING;
    dhcp.last_tick = dhcp.last_send = now;
}

static void dhcp_fallback(void) {
    configure_network(0x0606060fU, 0xffffff00U, 0x06060606U, 0x06060603U);
    dhcp.state = AQUAOS_DHCP_FALLBACK;
    dhcp.lease = dhcp.age = dhcp.server = 0;
    dhcp.last_send = network_now;
}

static void dhcp_send(void) {
    uint8_t frame[342] = {0}; /* 300-byte BOOTP payload, padded per RFC 2131. */
    uint8_t *ip = frame + 14, *udp = ip + 20, *p = udp + 8;
    int renewing = dhcp.state == AQUAOS_DHCP_RENEWING;
    int leased = renewing || dhcp.state == AQUAOS_DHCP_REBINDING;
    uint32_t destination = renewing ? dhcp.server : UINT32_MAX;
    if (renewing) {
        if (net_resolve(destination, frame)) return;
    } else kmemcpy(frame, broadcast, 6);
    kmemcpy(frame + 6, local_mac, 6); write_be16(frame + 12, 0x0800);
    ip[0] = 0x45; write_be16(ip + 2, sizeof(frame) - 14);
    ip[8] = 64; ip[9] = 17;
    if (leased) kmemcpy(ip + 12, local_ip, 4);
    put_ip(ip + 16, destination); write_be16(ip + 10, checksum(ip, 20));
    write_be16(udp, 68); write_be16(udp + 2, 67); write_be16(udp + 4, 308);
    p[0] = 1; p[1] = 1; p[2] = 6; put_ip(p + 4, dhcp.xid);
    if (!renewing) write_be16(p + 10, 0x8000); /* Request broadcast replies. */
    if (leased) kmemcpy(p + 12, local_ip, 4);
    kmemcpy(p + 28, local_mac, 6); put_ip(p + 236, 0x63825363U);
    unsigned n = 240;
    p[n++] = 53; p[n++] = 1;
    p[n++] = dhcp.state == AQUAOS_DHCP_SELECTING ? 1 : 3;
    p[n++] = 61; p[n++] = 7; p[n++] = 1;
    kmemcpy(p + n, local_mac, 6); n += 6;
    if (dhcp.state == AQUAOS_DHCP_REQUESTING) {
        p[n++] = 50; p[n++] = 4; put_ip(p + n, dhcp.offered); n += 4;
        p[n++] = 54; p[n++] = 4; put_ip(p + n, dhcp.server); n += 4;
    }
    p[n++] = 55; p[n++] = 6;
    p[n++] = 1; p[n++] = 3; p[n++] = 6;
    p[n++] = 51; p[n++] = 58; p[n++] = 59; p[n] = 255;
    /* IPv4 permits a zero UDP checksum; generate one anyway. */
    uint16_t c = udp_checksum(ip, udp, 308);
    write_be16(udp + 6, c ? c : 0xffff);
    e1000_transmit(frame, sizeof(frame));
}

static void dhcp_poll(uint32_t now) {
    uint32_t delta = now - dhcp.last_tick;
    dhcp.last_tick = now;
    uint32_t seconds = delta / 100;
    dhcp.fraction += delta % 100;
    seconds += dhcp.fraction / 100; dhcp.fraction %= 100;
    if (dhcp.lease && dhcp.lease != UINT32_MAX) {
        if (seconds >= dhcp.lease - dhcp.age) { dhcp_fallback(); return; }
        dhcp.age += seconds;
        uint32_t state = dhcp.age >= dhcp.t2 ? AQUAOS_DHCP_REBINDING :
            dhcp.age >= dhcp.t1 ? AQUAOS_DHCP_RENEWING : AQUAOS_DHCP_BOUND;
        if (state != dhcp.state) {
            if (dhcp.state == AQUAOS_DHCP_BOUND) ++dhcp.xid;
            dhcp.state = state; dhcp.last_send = now - 400;
        }
    }
    if (dhcp.state == AQUAOS_DHCP_BOUND) return;
    if (dhcp.state == AQUAOS_DHCP_FALLBACK) {
        if ((uint32_t)(now - dhcp.last_send) >= 6000) dhcp_init(now);
        return;
    }
    /* Wait one second after driver setup; then three attempts, four seconds
     * apart. Count TX failures too. Static fallback remains usable meanwhile. */
    uint32_t interval = dhcp.tries ? 400 : 100;
    if ((uint32_t)(now - dhcp.last_send) < interval) return;
    if (dhcp.tries >= 3 && (dhcp.state == AQUAOS_DHCP_SELECTING ||
                            dhcp.state == AQUAOS_DHCP_REQUESTING)) {
        dhcp_fallback(); return;
    }
    dhcp.last_send = now;
    if (dhcp.tries < 3) ++dhcp.tries;
    dhcp_send();
}

static int dhcp_unicast(uint32_t ip) {
    return (ip >> 24) && (ip >> 24) != 127 && (ip >> 28) < 14;
}

static int dhcp_config_valid(uint32_t ip, uint32_t mask, uint32_t router, uint32_t dns) {
    uint32_t host = ~mask;
    return dhcp_unicast(ip) && mask && host >= 3 && !(host & (host + 1)) &&
        (ip & host) && (ip & host) != host && dhcp_unicast(router) &&
        router != ip && (router & mask) == (ip & mask) &&
        (router & host) && (router & host) != host && dhcp_unicast(dns) && dns != ip;
}

static void dhcp_receive(const uint8_t *ip, unsigned header, unsigned total) {
    if (dhcp.state != AQUAOS_DHCP_SELECTING && dhcp.state != AQUAOS_DHCP_REQUESTING &&
        dhcp.state != AQUAOS_DHCP_RENEWING && dhcp.state != AQUAOS_DHCP_REBINDING) return;
    const uint8_t *udp = ip + header, *p = udp + 8;
    unsigned bytes = read_be16(udp + 4);
    if (bytes < 249 || bytes != total - header ||
        (read_be16(udp + 6) && udp_checksum(ip, udp, bytes))) return;
    unsigned length = bytes - 8;
    if (p[0] != 2 || p[1] != 1 || p[2] != 6 || read_ip(p + 4) != dhcp.xid ||
        kmemcmp(p + 28, local_mac, 6) || read_ip(p + 236) != 0x63825363U) return;
    uint32_t destination = read_ip(ip + 16), address = read_ip(p + 16);
    if (destination != UINT32_MAX && destination != read_ip(local_ip) &&
        destination != address) return;
    uint32_t mask = 0, router = 0, dns = 0, server = 0, lease = 0, t1 = 0, t2 = 0;
    unsigned type = 0, seen = 0, ended = 0;
    for (unsigned n = 240; n < length;) {
        unsigned code = p[n++];
        if (code == 255) { ended = 1; break; }
        if (!code) continue;
        if (n == length) return;
        unsigned count = p[n++];
        if (count > length - n) return;
        unsigned bit = code == 53 ? 1 : code == 54 ? 2 : code == 1 ? 4 :
            code == 3 ? 8 : code == 6 ? 16 : code == 51 ? 32 :
            code == 58 ? 64 : code == 59 ? 128 : 0;
        if (bit && (seen & bit)) return; /* Ambiguous critical options. */
        seen |= bit;
        if (code == 53) { if (count != 1) return; type = p[n]; }
        else if (bit) {
            if (count < 4 || ((code == 3 || code == 6) ? count % 4 : count != 4)) return;
            uint32_t value = read_ip(p + n);
            if (code == 54) server = value;
            if (code == 1) mask = value;
            if (code == 3) router = value;
            if (code == 6) dns = value;
            if (code == 51) lease = value;
            if (code == 58) t1 = value;
            if (code == 59) t2 = value;
        }
        n += count;
    }
    if (!ended || !dhcp_unicast(server)) return;
    if (dhcp.state != AQUAOS_DHCP_SELECTING && dhcp.state != AQUAOS_DHCP_REBINDING &&
        server != dhcp.server) return;
    if (type == 6 && dhcp.state != AQUAOS_DHCP_SELECTING) { dhcp_fallback(); return; }
    if ((dhcp.state == AQUAOS_DHCP_SELECTING && type != 2) ||
        (dhcp.state != AQUAOS_DHCP_SELECTING && type != 5)) return;
    /* ACK may omit configuration offered earlier. Renewals retain existing
     * settings, but every new lease must carry a duration. */
    if (dhcp.state != AQUAOS_DHCP_SELECTING) {
        if (!(seen & 4)) mask = dhcp.mask;
        if (!(seen & 8)) router = dhcp.router;
        if (!(seen & 16)) dns = dhcp.dns;
        if (address != dhcp.offered) return;
    }
    if (lease < 4 || !dhcp_config_valid(address, mask, router, dns)) return;
    if (!t1) t1 = lease / 2;
    if (!t2) t2 = lease - lease / 8;
    if (!t1 || t1 >= t2 || t2 >= lease) return;
    dhcp.offered = address; dhcp.server = server;
    dhcp.mask = mask; dhcp.router = router; dhcp.dns = dns;
    if (type == 2) {
        dhcp.state = AQUAOS_DHCP_REQUESTING; dhcp.tries = 0;
        dhcp.last_send = network_now - 100;
        return;
    }
    configure_network(address, mask, router, dns);
    dhcp.lease = lease; dhcp.age = dhcp.fraction = 0;
    dhcp.t1 = t1; dhcp.t2 = t2; dhcp.last_tick = network_now;
    dhcp.state = AQUAOS_DHCP_BOUND;
}
