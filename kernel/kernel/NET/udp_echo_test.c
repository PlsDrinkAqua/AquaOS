/* Test-only guest peer for tests/network_smoke.py --udp-echo. */
#ifdef AQUAOS_UDP_ECHO_TEST_PORT
#include <kernel/net.h>
#include <kernel/errno.h>
#include <libk/stdio.h>

#define TEST_PORT 49152
#define TEST_COUNT 40
#define TEST_TIMEOUT 300U
static unsigned test_case;
static int state; /* 0=bind, 1=send, 2=receive, 3=finished. */
static uint32_t started;
static uint8_t payload[NET_UDP_MAX_PAYLOAD];

void net_udp_echo_test_reset(void) {
    test_case = 0;
    state = 0;
}

static unsigned payload_length(void) {
    static const unsigned lengths[] = {0, 1, 31, 32, 1471, 1472};
    return lengths[test_case % 6];
}

static void finish(int success) {
    net_udp_unbind(TEST_PORT);
    state = 3;
    kprintf("UDP: echo %s (%u datagrams)\n", success ? "PASS" : "FAIL", test_case);
}

void net_udp_echo_test_poll(uint32_t now) {
    if (state == 3) return;
    if (!state) {
        if (net_udp_bind(TEST_PORT)) { finish(0); return; }
        state = 1;
    }
    unsigned length = payload_length();
    if (state == 1) {
        for (unsigned i = 0; i < length; ++i) payload[i] = (uint8_t)(test_case + i);
        if (net_udp_send(0x06060606U, TEST_PORT, AQUAOS_UDP_ECHO_TEST_PORT,
                         payload, length)) { finish(0); return; }
        started = now;
        state = 2;
        return;
    }
    uint32_t source;
    uint16_t port;
    int received = net_udp_receive(TEST_PORT, payload, sizeof(payload), &source, &port);
    if (received == -K_EAGAIN) {
        if ((uint32_t)(now - started) >= TEST_TIMEOUT) finish(0);
        return;
    }
    if (received != (int)length || source != 0x06060606U ||
        port != AQUAOS_UDP_ECHO_TEST_PORT) { finish(0); return; }
    for (unsigned i = 0; i < length; ++i)
        if (payload[i] != (uint8_t)(test_case + i)) { finish(0); return; }
    if (++test_case == TEST_COUNT) finish(1);
    else state = 1;
}
#endif
