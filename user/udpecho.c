#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <aquaos/net.h>
#include <aquaos/syscall.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"udpecho: check failed line %d (errno=%d)\n",__LINE__,errno); return 1; \
} } while (0)

/* Run without a NIC too: exercise descriptor ownership and the syscall boundary. */
static int check_descriptors(void) {
    unsigned char data[1472];
    aquaos_udp_address source;
    int fd = aquaos_udp_socket(); CHECK(fd >= 0);
    CHECK(aquaos_udp_sendto(fd, data, 1, 0x06060606, 9) == -1 && errno == EINVAL);
    CHECK(aquaos_udp_bind(fd, 49152) == 0);
    CHECK(aquaos_udp_bind(fd, 49153) == -1 && errno == EINVAL);
    CHECK(aquaos_udp_recvfrom(fd, data, sizeof(data), &source) == -1 && errno == EAGAIN);
    CHECK(aquaos_udp_sendto(fd, (void *)1, 1, 0x06060606, 9) == -1 && errno == EFAULT);
    CHECK(aquaos_udp_recvfrom(fd, (void *)1, 1, &source) == -1 && errno == EFAULT);
    CHECK(aquaos_udp_recvfrom(fd, data, sizeof(data), (void *)1) == -1 && errno == EFAULT);
    CHECK(aquaos_udp_sendto(fd, data, 1473, 0x06060606, 9) == -1 && errno == EINVAL);
    CHECK(aquaos_udp_sendto(fd, data, 1, 0x06060606, 0) == -1 && errno == EINVAL);
    CHECK(aquaos_syscall3(SYS_UDP_SEND, fd, 1, 0) == -EFAULT);
    aquaos_udp_request_t malformed = {(uintptr_t)data, 1, 0x06060606, 9, 1};
    CHECK(aquaos_syscall3(SYS_UDP_SEND, fd, (intptr_t)&malformed, 0) == -EINVAL);
    CHECK(aquaos_syscall3(SYS_UDP_RECEIVE, fd, (intptr_t)&malformed, (intptr_t)&source) == -EINVAL);
    CHECK(aquaos_udp_recvfrom(fd, data, 4096, &source) == -1 && errno == EAGAIN);
    CHECK(aquaos_syscall3(SYS_UDP_BIND, fd, 65536, 0) == -EINVAL);
    CHECK(aquaos_udp_bind(1, 5) == -1 && errno == EBADF);
    CHECK(lseek(fd, 0, SEEK_SET) == -1 && errno == ESPIPE);
    CHECK(read(fd, data, 1) == -1 && errno == EBADF);
    CHECK(write(fd, data, 1) == -1 && errno == EBADF);
    CHECK(fcntl(fd, F_GETFL) == O_RDWR);
    int other = aquaos_udp_socket(); CHECK(other >= 0);
    CHECK(aquaos_udp_bind(other, 49152) == -1 && errno == EBUSY);
    int alias = dup(fd); CHECK(alias >= 0);
    CHECK(close(fd) == 0);
    CHECK(aquaos_udp_bind(other, 49152) == -1 && errno == EBUSY);
    CHECK(dup2(alias, other) == other);
    CHECK(close(alias) == 0);
    CHECK(aquaos_udp_recvfrom(other, data, 1, &source) == -1 && errno == EAGAIN);

    int child = fork(); CHECK(child >= 0);
    if (!child) {
        /* Parent closes its reference before this inherited reference. */
        sleep(20);
        char number[16];
        snprintf(number, sizeof(number), "%d", other);
        const char *args[] = {"udpecho", "--inherited", number, NULL};
        exec("/bin/udpecho", args);
        return 1;
    }
    CHECK(close(other) == 0);
    CHECK(wait(child) == 0);
    fd = aquaos_udp_socket(); CHECK(fd >= 0);
    CHECK(aquaos_udp_bind(fd, 49152) == 0);
    CHECK(close(fd) == 0);
    CHECK(aquaos_udp_recvfrom(fd, data, 1, &source) == -1 && errno == EBADF);

    /* All eight port queues can be reclaimed repeatedly. */
    for (unsigned round = 0; round < 3; ++round) {
        int sockets[9];
        for (unsigned i = 0; i < 9; ++i) {
            sockets[i] = aquaos_udp_socket(); CHECK(sockets[i] >= 0);
            int result = aquaos_udp_bind(sockets[i], 0);
            CHECK(i < 8 ? result == 0 : result == -1 && errno == ENOSPC);
        }
        for (unsigned i = 0; i < 9; ++i) CHECK(close(sockets[i]) == 0);
    }
    /* A failed socket creation must not consume an open-file slot. */
    int sockets[16], count = 0;
    while ((fd = aquaos_udp_socket()) >= 0) sockets[count++] = fd;
    CHECK(count == 13 && errno == EMFILE);
    while (count) CHECK(close(sockets[--count]) == 0);
    printf("UDP descriptor checks passed\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--inherited")) {
        unsigned char data;
        aquaos_udp_address source;
        int inherited = atoi(argv[2]);
        CHECK(aquaos_udp_recvfrom(inherited, &data, 1, &source) == -1 && errno == EAGAIN);
        int fd = aquaos_udp_socket(); CHECK(fd >= 0);
        CHECK(aquaos_udp_bind(fd, 49152) == -1 && errno == EBUSY);
        CHECK(close(inherited) == 0);
        CHECK(aquaos_udp_bind(fd, 49152) == 0);
        /* Normal process exit releases this deliberately open bound socket. */
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--hold")) {
        int fd = aquaos_udp_socket(); CHECK(fd >= 0);
        CHECK(aquaos_udp_bind(fd, 49152) == 0);
        printf("UDP holding port 49152\n");
        for (;;) sleep(100);
    }
    if (argc == 2 && !strcmp(argv[1], "--check")) return check_descriptors();
    if (argc != 2) goto usage;
    unsigned port = 0;
    for (const char *p = argv[1]; *p; ++p) {
        if (*p < '0' || *p > '9' || port > 6553) goto usage;
        port = port * 10 + (*p - '0');
    }
    if (!port || port > 65535) goto usage;
    int fd = aquaos_udp_socket(); CHECK(fd >= 0);
    CHECK(aquaos_udp_bind(fd, 49152) == 0);
    const unsigned lengths[] = {0, 1, 31, 32, 1471, 1472};
    unsigned char payload[1472], reply[1472];
    for (unsigned packet = 0; packet < 40; ++packet) {
        unsigned length = lengths[packet % 6];
        for (unsigned i = 0; i < length; ++i) payload[i] = (unsigned char)(packet + i);
        int result = aquaos_udp_sendto(fd, payload, length, 0x06060606, port);
        if (result != (int)length) {
            fprintf(stderr,"UDP user echo FAIL: send (errno=%d)\n",errno);
            close(fd); return 1;
        }
        aquaos_udp_address source;
        /* A receive into a short buffer must preserve the full datagram. */
        unsigned tries;
        for (tries = 0; tries < 300; ++tries) {
            result = aquaos_udp_recvfrom(fd, reply, length ? length - 1 : 0, &source);
            if (result >= 0 || errno != EAGAIN) break;
            sleep(1);
        }
        if (length && result == -1 && errno == ERANGE) {
            /* Invalid output must also preserve a queued datagram. */
            CHECK(aquaos_udp_recvfrom(fd, (void *)1, length, &source) == -1 && errno == EFAULT);
            CHECK(aquaos_udp_recvfrom(fd, reply, sizeof(reply), (void *)1) == -1 && errno == EFAULT);
            result = aquaos_udp_recvfrom(fd, reply, 4096, &source);
        }
        if (result != (int)length || source.ip != 0x06060606 || source.port != port ||
            memcmp(payload, reply, length)) {
            fprintf(stderr,"UDP user echo FAIL: reply %u (errno=%d)\n",packet,errno);
            close(fd); return 1;
        }
    }
    CHECK(close(fd) == 0);
    printf("UDP user echo PASS (40 datagrams)\n");
    return 0;
usage:
    fprintf(stderr,"usage: udpecho <host-port> | --check\n");
    return 2;
}
