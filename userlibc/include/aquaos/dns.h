#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AQUAOS_DNS_SERVER 0x06060603U
#define AQUAOS_DNS_PACKET_MAX 512U
/* ASCII hostname, optional trailing dot. A/IN only; first matching IPv4 result.
 * Query/parse return bytes/0 or -1 with errno. Parse EAGAIN means unrelated
 * response; EINVAL malformed, ENOENT NXDOMAIN/no A, EOVERFLOW truncated/too
 * complex, EIO server failure. CNAME chains are limited to eight links and
 * must contain their final A record in this response's answer section. */
int aquaos_dns_query(const char *name, uint16_t id, void *packet, size_t capacity);
int aquaos_dns_parse(const void *packet, size_t length, const char *name,
                     uint16_t id, uint32_t *address);
/* Blocking bounded resolver over nonblocking UDP. Three attempts, each with
 * up to three seconds for ARP/TX and three seconds for a reply (100 Hz PIT).
 * No DNS cache, search suffixes, EDNS, TCP fallback, or DNSSEC. */
int aquaos_dns_lookup(const char *name, uint32_t server, uint32_t *address);
#ifdef __cplusplus
}
#endif
