#include <aquaos/net.h>
#include <stdio.h>
#include <errno.h>

static void address(const char *name, uint32_t ip) {
    printf("%s: %u.%u.%u.%u\n", name, ip >> 24, (ip >> 16) & 255,
           (ip >> 8) & 255, ip & 255);
}
int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) { fprintf(stderr, "usage: netinfo\n"); return 2; }
    aquaos_net_info info;
    if (aquaos_get_net_info(&info) < 0) {
        fprintf(stderr, "netinfo: cannot read configuration (errno=%d)\n", errno);
        return 1;
    }
    if (!(info.flags & AQUAOS_NET_PRESENT)) { puts("Network: no interface"); return 0; }
    static const char *states[] = {"disabled", "discovering (static fallback)",
        "requesting (static fallback)", "bound", "renewing", "rebinding", "static fallback"};
    printf("Interface: e1000\nLink: %s\n", info.flags & AQUAOS_NET_LINK_UP ? "up" : "down");
    printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", info.mac[0], info.mac[1],
           info.mac[2], info.mac[3], info.mac[4], info.mac[5]);
    address("IPv4", info.ip); address("Netmask", info.netmask);
    address("Gateway", info.gateway); address("DNS", info.dns);
    printf("DHCP: %s\n", info.dhcp_state < sizeof(states)/sizeof(states[0]) ? states[info.dhcp_state] : "unknown");
    if (info.lease_seconds) {
        address("DHCP server", info.server);
        if (info.lease_seconds == UINT32_MAX) puts("Lease: infinite");
        else printf("Lease: %u seconds, %u remaining\n", info.lease_seconds, info.lease_remaining);
    }
    return 0;
}
