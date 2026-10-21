#include <aquaos/dns.h>
#include <errno.h>
#include <string.h>

static int fail(int error) { errno = error; return -1; }
static unsigned get16(const unsigned char *p) { return (unsigned)p[0] * 256 + p[1]; }
static void put16(unsigned char *p, unsigned n) { p[0] = n >> 8; p[1] = n; }
static unsigned char lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int character(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

/* Encode while validating; bounded input traversal even for oversized names. */
int aquaos_dns_query(const char *name, uint16_t id, void *packet, size_t capacity) {
    if (!name || !packet) return fail(EINVAL);
    unsigned char wire[255];
    size_t n = 0, start = 0;
    for (;;) {
        if (n >= sizeof(wire) - 1) return fail(EINVAL);
        size_t label = n++;
        unsigned count = 0;
        while (name[start] && name[start] != '.') {
            if (count == 63 || !character((unsigned char)name[start]) || n >= sizeof(wire)-1)
                return fail(EINVAL);
            wire[n++] = lower((unsigned char)name[start++]);
            ++count;
        }
        if (!count) return fail(EINVAL);
        wire[label] = count;
        if (!name[start] || !name[++start]) break;
    }
    wire[n++] = 0;
    if (capacity < 12 + n + 4) return fail(ERANGE);
    unsigned char *p = packet;
    memset(p, 0, 12);
    put16(p, id); put16(p+2, 0x0100); put16(p+4, 1);
    memcpy(p+12, wire, n);
    put16(p+12+n, 1); put16(p+14+n, 1);
    return (int)(16+n);
}

/* Pointer targets must precede the pointer; a step budget also bounds cycles
 * involving labels. 'end' is the end of the original encoded name, not target. */
static int name_at(const unsigned char *p, size_t n, size_t at,
                   char out[254], size_t *end) {
    size_t used = 0, wire = 1;
    int jumped = 0;
    for (unsigned steps = 0; steps < 256; ++steps) {
        if (at >= n) return -1;
        unsigned len = p[at++];
        if (!len) {
            if (!jumped) *end = at;
            out[used] = 0;
            return 0;
        }
        if ((len & 0xc0) == 0xc0) {
            if (at >= n) return -1;
            size_t target = ((len & 63) << 8) | p[at];
            if (target < 12 || target >= at-1) return -1;
            if (!jumped) *end = at+1;
            jumped = 1; at = target;
            continue;
        }
        if (len & 0xc0 || at + len > n || wire + len + 1 > 255) return -1;
        wire += len+1;
        if (used) out[used++] = '.';
        if (used + len >= 254) return -1;
        for (unsigned i = 0; i < len; ++i) {
            if (!character(p[at])) return -1;
            out[used++] = lower(p[at++]);
        }
    }
    return -1;
}

int aquaos_dns_parse(const void *packet, size_t length, const char *name,
                     uint16_t id, uint32_t *address) {
    if (!packet || !address || !name) return fail(EINVAL);
    if (length < 12) return fail(EINVAL);
    const unsigned char *p = packet;
    if (get16(p) != id) return fail(EAGAIN);
    unsigned flags = get16(p+2);
    if (!(flags & 0x8000) || (flags & 0x7800) || get16(p+4) != 1) return fail(EINVAL);
    unsigned char query[272];
    int qlen = aquaos_dns_query(name, id, query, sizeof(query));
    if (qlen < 0) return -1;
    char expected[254], question[254];
    size_t end, at;
    if (name_at(query, (size_t)qlen, 12, expected, &end) ||
        name_at(p, length, 12, question, &at) || at+4 > length) return fail(EINVAL);
    if (strcmp(expected, question) || get16(p+at) != 1 || get16(p+at+2) != 1)
        return fail(EAGAIN);
    if ((flags & 0x0200) || length > AQUAOS_DNS_PACKET_MAX) return fail(EOVERFLOW);
    at += 4;
    unsigned answers = get16(p+6), total = answers + get16(p+8) + get16(p+10);
    if (total > 64) return fail(EOVERFLOW);
    /* Store offsets, not expanded names, to keep the userspace stack small. */
    struct record { size_t owner, data; unsigned type, length; } records[64];
    unsigned count = 0;
    for (unsigned i = 0; i < total; ++i) {
        size_t owner = at;
        char decoded[254];
        if (name_at(p, length, at, decoded, &at) || at+10 > length) return fail(EINVAL);
        unsigned type = get16(p+at), class = get16(p+at+2), bytes = get16(p+at+8);
        at += 10;
        if (bytes > length-at) return fail(EINVAL);
        if (class == 1 && type == 1 && bytes != 4) return fail(EINVAL);
        if (class == 1 && type == 5) {
            if (name_at(p, length, at, decoded, &end) || end != at+bytes || !decoded[0])
                return fail(EINVAL);
        }
        if (i < answers && class == 1 && (type == 1 || type == 5))
            records[count++] = (struct record){owner, at, type, bytes};
        at += bytes;
    }
    if (at != length) return fail(EINVAL);
    if ((flags & 15) == 3) return fail(ENOENT);
    if (flags & 15) return fail(EIO);
    for (unsigned depth = 0; depth <= 8; ++depth) {
        char target[254] = {0};
        int have_a = 0;
        uint32_t result = 0;
        for (unsigned i = 0; i < count; ++i) {
            char owner[254];
            if (name_at(p, length, records[i].owner, owner, &end)) return fail(EINVAL);
            if (strcmp(owner, expected)) continue;
            const unsigned char *data = p + records[i].data;
            if (records[i].type == 1) {
                if (!have_a) result = ((uint32_t)data[0]<<24) | ((uint32_t)data[1]<<16) |
                                     ((uint32_t)data[2]<<8) | data[3];
                have_a = 1;
            } else {
                char alias[254];
                if (name_at(p, length, records[i].data, alias, &end)) return fail(EINVAL);
                if (target[0] && strcmp(target, alias)) return fail(EINVAL);
                strcpy(target, alias);
            }
        }
        if (have_a && target[0]) return fail(EINVAL);
        if (have_a) { *address = result; return 0; }
        if (!target[0]) return fail(ENOENT);
        strcpy(expected, target);
    }
    return fail(EOVERFLOW);
}
