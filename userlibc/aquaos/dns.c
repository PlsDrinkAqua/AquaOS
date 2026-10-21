#include <aquaos/dns.h>
#include <aquaos/net.h>
#include <errno.h>
#include <unistd.h>

int aquaos_dns_lookup(const char *name, uint32_t server, uint32_t *address) {
    static uint16_t next_id = 0xa900;
    unsigned char query[272], reply[1472];
    if (!address) { errno = EINVAL; return -1; }
    uint16_t id = ++next_id;
    int bytes = aquaos_dns_query(name, id, query, sizeof(query));
    if (bytes < 0) return -1;
    int fd = aquaos_udp_socket();
    if (fd < 0) return -1;
    int result = -1, error = ETIMEDOUT;
    if (aquaos_udp_bind(fd, 0) < 0) { error = errno; goto done; }
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        int sent = 0;
        for (unsigned tick = 0; tick < 300; ++tick) {
            int n = aquaos_udp_sendto(fd, query, (size_t)bytes, server, 53);
            if (n == bytes) { sent = 1; break; }
            if (n >= 0 || errno != EAGAIN) { error = n >= 0 ? EIO : errno; goto done; }
            sleep(1);
        }
        if (!sent) continue;
        for (unsigned tick = 0; tick < 300; ++tick) {
            aquaos_udp_address source;
            int n = aquaos_udp_recvfrom(fd, reply, sizeof(reply), &source);
            if (n < 0 && errno != EAGAIN) { error = errno; goto done; }
            if (n >= 0 && source.ip == server && source.port == 53) {
                if (!aquaos_dns_parse(reply, (size_t)n, name, id, address)) {
                    result = 0; goto done;
                }
                /* Invalid and unrelated packets cannot terminate a lookup. */
                if (errno != EINVAL && errno != EAGAIN) { error = errno; goto done; }
            }
            sleep(1);
        }
    }
done:
    close(fd);
    if (result < 0) errno = error;
    return result;
}
