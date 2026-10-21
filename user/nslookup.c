#include <aquaos/dns.h>
#include <errno.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: nslookup <hostname>\n");
        return 2;
    }
    uint32_t ip;
    if (aquaos_dns_lookup(argv[1], AQUAOS_DNS_SERVER, &ip) < 0) {
        const char *reason = "lookup failed";
        if (errno == ENOENT) reason = "name not found or no IPv4 answer";
        else if (errno == ETIMEDOUT) reason = "timed out";
        else if (errno == EINVAL) reason = "invalid hostname";
        else if (errno == EOVERFLOW) reason = "truncated or overly complex DNS response";
        else if (errno == EIO) reason = "DNS server failure";
        fprintf(stderr, "nslookup: %s (errno=%d)\n", reason, errno);
        return 1;
    }
    printf("Name: %s\nAddress: %u.%u.%u.%u\n", argv[1],
           ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return 0;
}
