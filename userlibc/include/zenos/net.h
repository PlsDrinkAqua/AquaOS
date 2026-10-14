#pragma once
#include <stdint.h>
/* Gateway-only ICMP API. Numeric IPv4 encoded as 0xAABBCCDD.
 * Result: round-trip milliseconds or -1 with errno (EAGAIN while pending). */
int zenos_ping_start(uint32_t ip);
int zenos_ping_result(void);
