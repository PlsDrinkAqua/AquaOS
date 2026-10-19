#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <aquaos/net.h>

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
    if (strcmp(target, "6.6.6.6")) {
        fprintf(stderr, "ping: only gateway 6.6.6.6 is supported\n");
        return 2;
    }
    printf("PING %s: %u requests\n", target, count);
    unsigned received = 0;
    for (unsigned sequence = 1; sequence <= count; ++sequence) {
        if (aquaos_ping_start(0x06060606U) < 0) {
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
            printf("reply from %s: seq=%u time=%d ms\n", target, sequence, result);
        } else if (errno == ETIMEDOUT) printf("timeout from %s: seq=%u\n", target, sequence);
        else printf("ping: seq=%u failed (errno=%d)\n", sequence, errno);
        if (sequence < count) sleep(100);
    }
    printf("%u transmitted, %u received\n", count, received);
    return received == count ? 0 : 1;
usage:
    fprintf(stderr, "usage: ping [-c count] [6.6.6.6] (count 1..100)\n");
    return 2;
}
