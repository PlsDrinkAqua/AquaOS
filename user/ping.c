#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <aquaos/net.h>
#include <aquaos/dns.h>

static int parse_ip(const char *s, uint32_t *ip) {
    uint32_t value = 0;
    for (unsigned part = 0; part < 4; ++part) {
        unsigned byte = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (++digits > 3) return -1;
            byte = byte * 10 + (*s++ - '0');
            if (byte > 255) return -1;
        }
        if (!digits) return -1;
        value = (value << 8) | byte;
        if (part < 3) { if (*s++ != '.') return -1; }
        else if (*s) return -1;
    }
    *ip = value;
    return 0;
}

int main(int argc, char **argv) {
    unsigned count = 4;
    const char *target = "6.6.6.6";
    if (argc == 4 && !strcmp(argv[1], "-c")) {
        count = 0;
        for (const char *p = argv[2]; *p; ++p) {
            if (*p < '0' || *p > '9' || count > 100) goto usage;
            count = count * 10 + (*p - '0');
        }
        if (!count || count > 100) goto usage;
        target = argv[3];
    } else if (argc == 2) target = argv[1];
    else if (argc != 1) goto usage;
    if (*target == '-') goto usage;
    uint32_t address;
    if (parse_ip(target, &address)) {
        int numeric = 1;
        for (const char *p = target; *p; ++p)
            if ((*p < '0' || *p > '9') && *p != '.') numeric = 0;
        if (numeric) goto usage;
        if (aquaos_dns_lookup(target, AQUAOS_DNS_SERVER, &address) < 0) {
            fprintf(stderr, "ping: DNS lookup failed (errno=%d)\n", errno);
            return 1;
        }
    }
    char numeric[16];
    snprintf(numeric, sizeof(numeric), "%u.%u.%u.%u", address >> 24,
             (address >> 16) & 255, (address >> 8) & 255, address & 255);
    printf("PING %s: %u requests\n", target, count);
    if (strcmp(target, numeric)) printf("Address: %s\n", numeric);
    unsigned received = 0;
    for (unsigned sequence = 1; sequence <= count; ++sequence) {
        int started = -1;
        for (unsigned tick = 0; tick < 300; ++tick) {
            started = aquaos_ping_start(address);
            if (!started || errno != EAGAIN) break;
            sleep(1); /* Bounded wait for ARP resolution or NIC backpressure. */
        }
        if (started < 0) {
            fprintf(stderr, "ping: cannot send (errno=%d)\n", errno);
            return 1;
        }
        int result;
        do {
            sleep(1);
            result = aquaos_ping_result();
        } while (result < 0 && errno == EAGAIN);
        if (result >= 0) {
            ++received;
            printf("reply from %s: seq=%u time=%d ms\n", numeric, sequence, result);
        } else if (errno == ETIMEDOUT) printf("timeout from %s: seq=%u\n", numeric, sequence);
        else printf("ping: seq=%u failed (errno=%d)\n", sequence, errno);
        if (sequence < count) sleep(100);
    }
    printf("%u transmitted, %u received\n", count, received);
    return received == count ? 0 : 1;
usage:
    fprintf(stderr, "usage: ping [-c count] [hostname|IPv4] (count 1..100)\n");
    return 2;
}
