# Networking foundations

AquaOS provides PCI discovery and a polling E1000 driver with receive and
transmit DMA rings. It sends one diagnostic Ethernet broadcast at boot.
ARP resolves the static gateway and local-subnet peers, and answers requests for the guest IP.
A single IPv4/ICMP boot ping verifies the gateway path after ARP resolves.
A userspace `ping` command can repeat gateway probes; DHCP is not implemented.

## Console diagnostics

Normal builds keep asynchronous ARP, boot-ping and per-packet RX diagnostics off
so network activity cannot overwrite a shell prompt or input line. Networking
and the boot probe still run. To enable those console messages for debugging:

```sh
AQUAOS64_CC='gcc -DAQUAOS_NET_DEBUG' sh build.sh
python3 tools/iso64.py
```

The diagnostic messages described below are visible with that flag enabled.
`tests/network_smoke.py` and `tests/e1000_rx.py` build with it automatically and
restore normal kernel/ISO artifacts afterward, even on failure. Stop other
builds before running either test.

## Development network

Run `sh qemu.sh`. The launcher creates one E1000 (Intel 82540EM, PCI
8086:100e) with MAC `52:54:00:12:34:56`, attached to QEMU user networking.
It explicitly sets the subnet to `6.6.6.0/24`, host/gateway to `6.6.6.6`,
and first DHCP lease to `6.6.6.15`. The subnet's QEMU DNS proxy is `6.6.6.3`.
The guest now uses static IP `6.6.6.15/24` and resolves gateway `6.6.6.6`.
The userspace DNS resolver uses that proxy by default.

Each launch retains `network.pcap` beside `dev.img` under its new
`disk/aquaos64.*` directory and prints both paths. Open the capture in Wireshark
or inspect it with `tcpdump -nn -r disk/aquaos64.XXXXXX/network.pcap`.
Expect a 60-byte broadcast with experimental EtherType 0x88b5 and payload
`AquaOS TX test`, sourced from the programmed station MAC `06:07:06:07:06:07`.
The user backend requires no TAP device or root privileges. Future guest
connections can use its outbound NAT; arbitrary inbound connections require
explicit forwarding. Host-to-guest ping is not a suitable test of this backend.
See [QEMU network and capture options](https://www.qemu.org/docs/master/system/invocation.html).

When invoking QEMU directly, use the same network arguments:

```sh
-netdev user,id=net0,net=6.6.6.0/24,host=6.6.6.6,dhcpstart=6.6.6.15 \
-device e1000,netdev=net0,mac=52:54:00:12:34:56 \
-object filter-dump,id=netcapture,netdev=net0,file=network.pcap
```

## Kernel PCI interface

`kernel/include/kernel/pci.h` exposes config reads, command-bit enabling,
callback enumeration, and the selected E1000 descriptor. Discovery runs during
boot before entering userspace and prints vendor/device IDs, classes, E1000 BAR
addresses, IRQ line/pin and the resulting command register.

The implementation uses x86 configuration mechanism 1 at CF8/CFC. It scans all
256 conventional bus numbers, 32 slots, and up to eight functions per
multifunction device, including firmware-configured secondary buses. It decodes
I/O, 32-bit memory and 64-bit memory BAR addresses without changing BARs or
probing their sizes. It handles endpoint and bridge BAR counts. Descriptors
passed to enumeration callbacks are temporary and must be copied for retention.

For the first supported E1000, initialization enables memory decoding, bus
mastering, and I/O decoding if an I/O BAR exists. It preserves other command
bits and uses a 16-bit write so PCI status bits are not inadvertently cleared.
CF8/CFC transactions preserve and disable interrupts on this single-CPU kernel.
Unrelated devices are enumerated without changing their command registers.
Missing or unsupported NICs do not prevent boot.

BAR addresses are physical addresses. The driver maps MMIO uncached and
allocates physically contiguous, page-aligned descriptor storage and DMA buffers.
The reported IRQ is firmware routing information; discovery does not install an
IRQ handler or unmask the NIC interrupt. Extended PCIe config space, firmware
resource allocation, BAR sizing and SMP locking are outside this implementation.

## Verification

```sh
sh build.sh
python3 tools/iso64.py
python3 tests/network_smoke.py
```

The smoke test uses a disposable ext2 disk and boots four configurations:
E1000 on the root bus, E1000 behind a PCI bridge, E1000 as a secondary
function of a multifunction slot, and no NIC. It checks guest
BAR/IRQ decoding against QEMU's inventory, command enable bits, successful boot,
and Ethernet PCAP headers. Existing development disks are untouched.

## Transmit interface

`e1000_transmit(frame, length)` accepts a kernel Ethernet frame of 14–1514
bytes, excluding FCS. It copies the frame into driver-owned DMA storage and
zero-pads it to at least 60 bytes; hardware appends FCS. A successful return
means queued, not delivered. The caller can reuse its buffer immediately.
Invalid inputs return `-K_EINVAL`, an unavailable driver returns `-K_ENODEV`,
and a full queue returns `-K_EAGAIN` without waiting.

The legacy TX ring has 32 descriptors and keeps one slot unused. Each frame
requests EOP, IFCS and RS; completed descriptors are reclaimed only after DMA
sets DD. Submission and timer polling are serialized by interrupt masking on
the single CPU. NIC interrupts remain masked. There are no retransmissions,
completion callbacks or TX link-down timeouts yet.

Register and descriptor programming follows the
[Intel 8254x software developer manual](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf).

`python3 tests/network_smoke.py` checks the exact captured diagnostic frame.
`python3 tests/network_smoke.py --tx-stress` builds test-only traffic, checks
70 frames and completed descriptors across multiple ring wraps for all three
PCI layouts, and then rebuilds the normal kernel and ISO. The no-NIC boot is
also checked. The stress mode changes build artifacts; run it with other
builds stopped. Guest development disks are untouched.

## ARP gateway resolution

`kernel/kernel/NET/net.c` handles Ethernet/IPv4 ARP using the wire format in
[RFC 826](https://www.rfc-editor.org/rfc/rfc826). After both DMA rings initialize,
it broadcasts an ARP request for `6.6.6.6`, using static sender IP `6.6.6.15`
and the station MAC read back from the NIC. Timer polling retries at one-second
intervals (the current PIT runs at 100 Hz), with at most three attempts.
Resolution stops retries and prints `ARP: gateway 6.6.6.6 MAC=...`.

The driver passes complete, error-free RX frames to `net_receive()` before
recycling DMA buffers. The handler checks frame size, Ethernet destination,
ARP hardware/protocol types, address lengths, opcode, sender MAC validity and
agreement with the Ethernet source, and target IP. Only a directed gateway
reply with our target MAC, following a resolution attempt, populates the single
cache entry. `net_gateway_mac()` copies that entry for future IP transmission.
ARP requests for our IP receive a unicast reply. Unknown EtherTypes are ignored
by the network handler. All network state access currently requires interrupts
disabled on the single CPU; received buffer pointers are never retained.

The gateway entry remains pinned for the boot probes. An additional eight-entry
ARP table resolves unicast peers on the static `6.6.6.0/24` subnet, including
QEMU's DNS proxy at `6.6.6.3`. `net_resolve(ip, mac)` returns the peer MAC for
local destinations and the gateway MAC for off-subnet destinations. A cache miss
sends an ARP request and returns `-K_EAGAIN`; the caller must retry its datagram,
which is not retained by the kernel. Timer polling retries at one-second
intervals, with three total attempts, including failed TX submissions. After
one final response interval, resolution returns `-K_ETIMEDOUT` for a three-second
cooldown before a new lookup can start. Resolved peer entries expire after
60 seconds. Tick comparisons tolerate timer wraparound.

Only directed replies matching a pending lookup populate peer entries; requests,
unsolicited replies and replies after timeout do not update them. Concurrent
lookups share the bounded table; a full table returns `-K_ENOSPC` until entries
expire. IPv4 reception accepts the cached peer MAC or the resolved gateway MAC.
A router can deliver packets from local-subnet sources; QEMU's DNS proxy in
particular advertises its own ARP MAC but sends IP replies via the gateway MAC. Network, subnet broadcast, self, loopback, multicast and
reserved high-range destinations are rejected. The gateway uses its existing
boot retry policy and does not expire.

This is a static, single-interface milestone: address conflict detection,
VLAN support, DHCP and configurable routing remain unimplemented.
A full TX queue can drop an ARP reply; bounded gateway retries cover lost
requests without blocking the timer. ARP does not authenticate peer identities.

The QEMU network smoke test checks request fields, captures the gateway reply,
and compares its sender MAC with the guest's resolution message in all three
PCI layouts. Host checks exercise malformed/truncated packets, cache population,
request replies, bounded retries and timer wraparound:

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ikernel/include -idirafter libk/include \
  tests/arp_host.c kernel/kernel/NET/net.c -o /tmp/aquaos-arp-host
/tmp/aquaos-arp-host
```

## IPv4/ICMP boot ping

After ARP resolution, the next timer poll sends one ICMP Echo Request to
`6.6.6.6`, with identifier `0xa901`, sequence 1 and payload `AquaOS ping`.
The IPv4 header uses TTL 64 and Don't Fragment; both IPv4 and ICMP checksums
are computed over network-order bytes, including an odd final payload byte.
Wire formats follow [RFC 791](https://www.rfc-editor.org/rfc/rfc791.txt) and
[RFC 792](https://www.rfc-editor.org/rfc/rfc792.txt).

Successful reception prints `ICMP: reply from 6.6.6.6 seq=1`. With no matching
reply within 300 ticks (three seconds at the current 100 Hz PIT), it prints a
timeout. TX submission failure is reported separately and terminates the probe.
The shell continues running throughout; there is no blocking wait or allocation
in this path. If ARP never resolves, no Echo Request is sent.

The receive path checks Ethernet addresses, IPv4 version, header length, total
length, header checksum, TTL, protocol and endpoint addresses. Fragmented packets
and the reserved flag are rejected; DF is accepted. Echo replies must match the
identifier, sequence, payload length and contents, and ICMP checksum. Ethernet
padding is excluded using IPv4 total length. Duplicates and late replies are
ignored. Options count toward header length/checksum but are not interpreted.
This is one fixed gateway probe, without ICMP error handling, incoming Echo
Request responses, fragment reassembly, or a socket API.

`python3 tests/network_smoke.py` verifies the request/reply in the PCAP and the
guest success message for all three PCI layouts, plus boot without a NIC.
The host validation test exercises malformed frames, padding, checksum failures,
wrong reply identity, timer wraparound, late replies and TX failure:

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ikernel/include -idirafter libk/include tests/icmp_host.c -o /tmp/aquaos-icmp-host
/tmp/aquaos-icmp-host
```

## Shell ping command

Fresh development disks include `/bin/ping`. Run:

```sh
ping
ping 6.6.6.6
ping -c 2 6.6.6.6
```

The default is four requests, with one-second pauses between completed probes.
`-c` accepts 1–100; Ctrl+C stops the command. Each request has a three-second
timeout. Output includes sequence numbers, approximate RTT in milliseconds
(10 ms timer resolution), and transmitted/received counts. Exit status is 0
when all requests receive replies, 1 on send failure or packet loss, and 2 for
invalid arguments or unsupported destinations. Only `6.6.6.6` is supported;
there is no DNS lookup or general destination routing yet.

Syscalls 29/30 back `aquaos_ping_start()` and `aquaos_ping_result()` in
`<aquaos/net.h>`. Start queues one request; result reports EAGAIN while waiting,
then returns RTT or ETIMEDOUT and consumes the result. The caller sleeps between
polls. One process owns the active probe; other callers get EBUSY at start and
EPERM when reading another process's result. Process exit (including Ctrl+C)
cancels ownership. Wire sequence numbers change between requests to reject
stale replies. Existing development images need the rebuilt `/bin/ping` copied
in while QEMU is stopped, or use `sh qemu.sh` for a fresh image.

## UDP kernel interface

IPv4 now dispatches protocol 17 to UDP independently of the ICMP ping state.
`kernel/include/kernel/net.h` exposes nonblocking `net_udp_bind`,
`net_udp_unbind`, `net_udp_send` and `net_udp_receive`. These are kernel APIs:
all callers must disable interrupts, as with the existing network interface.
IP addresses use `0xAABBCCDD` integers and ports use host-order integers.
Userspace descriptors are exposed through the AquaOS UDP API below.

The implementation follows [RFC 768](https://www.rfc-editor.org/rfc/rfc768).
Send generates the IPv4 and UDP pseudo-header checksums, including odd-length
payloads and the zero-checksum-to-0xffff encoding. Receive validates IPv4 and
UDP lengths and nonzero UDP checksums; IPv4 UDP packets with checksum zero are
accepted. IPv4 options and Ethernet padding are handled. Fragmented packets
are dropped; payloads are limited to 1472 bytes (1500-byte IPv4 MTU).

Eight ports can be bound, each with a four-datagram FIFO holding owned copies
of payloads and source addresses/ports. Unbound traffic and new arrivals to a
full queue are silently dropped. A receive returns the payload length, including
zero for an empty datagram, or `-K_EAGAIN` when empty. A buffer that is too small
returns `-K_ERANGE` and leaves the datagram queued. Unbinding discards the queue.
Send returns zero when queued to the NIC, or propagates its error; it does not
promise delivery. Receive processing performs no memory allocations.

Sends resolve local-subnet peers directly and use the gateway for off-subnet
unicast destinations. An unresolved next hop returns `EAGAIN` without queuing
payload data; peer resolution can also return `ETIMEDOUT` or `ENOSPC` as described
above. Loopback, multicast, broadcast and self destinations are rejected.
Configurable routing, DHCP, ICMP errors for closed UDP ports and retransmission
remain unimplemented.

Host verification:

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ikernel/include -idirafter libk/include tests/udp_host.c -o /tmp/aquaos-udp-host
/tmp/aquaos-udp-host
```

The tests independently verify wire checksums for every payload size from
0 to 1472 bytes, checksum-zero encoding, receive source metadata, truncated and
corrupt packets, invalid lengths, IP options, fragments, Ethernet padding,
queue bounds, port exhaustion, unbinding, small-buffer retries and send errors.
`python3 tests/network_smoke.py` checks the existing QEMU ARP/ICMP behavior.

### QEMU UDP echo regression

```sh
python3 tests/network_smoke.py --udp-echo
```

This mode binds a real host UDP echo service on `127.0.0.1` at an OS-selected
port, then builds a test-only kernel that sends to that port through QEMU's
`6.6.6.6` host alias. No TAP, root privileges, Internet access or inbound port
forwarding is required. The host socket stays bound throughout the run.

After the boot ICMP reply, the guest binds UDP port 49152 and sends 40 datagrams
one at a time. Payload sizes cycle through 0, 1, 31, 32, 1471 and 1472 bytes,
with binary contents depending on the packet index. The guest checks each echo's
source IP, source port, length and every payload byte before proceeding. A send
error, invalid reply or three-second reply timeout produces `UDP: echo FAIL`;
success prints `UDP: echo PASS (40 datagrams)` and releases the bound port.
This exercises descriptor-ring wrap as well as minimum and maximum payloads.

The host checks the exact data received by its real UDP socket. After shutdown,
the test independently checks both directions in the PCAP: Ethernet/IP addresses,
ports, IP and UDP lengths, IPv4 header checksums, UDP pseudo-header checksums and
all payloads in order. Guest UDP checksums must be present; a zero checksum in
an incoming IPv4 UDP packet is permitted. All three existing PCI layouts run
these checks; the no-NIC boot still runs its normal regression.

Like `--tx-stress`, this mode rebuilds shared build artifacts and restores the
normal kernel and ISO in a `finally` block, including after test failure. Run
with other builds stopped. Disposable guest disks are used throughout. The
probe is compiled only when `AQUAOS_UDP_ECHO_TEST_PORT` is defined; normal builds
send no UDP test traffic. `--udp-echo` and `--tx-stress` can be combined.


## Userspace UDP descriptors

`<aquaos/net.h>` provides the following AquaOS-specific, nonblocking API:

```c
int aquaos_udp_socket(void);
int aquaos_udp_bind(int fd, uint16_t port);
int aquaos_udp_sendto(int fd, const void *data, size_t length,
                     uint32_t ip, uint16_t port);
int aquaos_udp_recvfrom(int fd, void *data, size_t capacity,
                       aquaos_udp_address *source);
```

Create a descriptor, bind it, then send or receive. Bind port zero to select
an available ephemeral port in 49152–65535, or specify a nonzero port. Each
open UDP description can be bound once. A port already bound elsewhere returns
`EBUSY`; exhaustion of the eight kernel port queues returns `ENOSPC`.
IP addresses and ports are numeric host integers: `0x06060606` means `6.6.6.6`.
Local peers are resolved through ARP; off-subnet traffic uses the gateway.
Retry sends returning `EAGAIN`; unresolved peer lookups can return `ETIMEDOUT`
or `ENOSPC`.

Socket creation returns a file descriptor; bind returns zero; send and receive
return payload bytes, including zero for empty datagrams. Errors return -1 and
set `errno`. Receive returns `EAGAIN` when empty. A buffer too small returns
`ERANGE` and preserves the datagram. The required source output contains the
sender's `ip` and `port` (and a reserved field set to zero). Invalid user buffers
return `EFAULT` before any packet is queued or consumed. Payloads remain limited
to 1472 bytes; receive capacities may be larger. There is no blocking mode, connect, DNS, or POSIX socket API.

Use ordinary `close`, `dup`, `dup2` and `fork` with these descriptors. Duplicated
or inherited descriptors share the port and receive FIFO. Descriptors survive
exec, as other descriptors do in AquaOS. The last close releases the port and
discards queued packets; normal exit and Ctrl+C use that same cleanup path.
`lseek` returns `ESPIPE`. Use the UDP calls for datagrams; generic `read`/`write`
are unsupported. `fcntl(F_GETFL)` reports `O_RDWR`, and append is unsupported.
Syscalls 31–34 implement create, bind, send and receive; fixed-width requests
are defined in `<aquaos/net_abi.h>`. Kernel buffers isolate user memory from DMA.

The `udpecho <host-port>` program sends 40 datagrams to the QEMU host alias,
using the same binary payloads as the kernel echo test. It checks source
metadata, data integrity, empty packets, maximum-size packets, small-buffer
retries and invalid-buffer retries. `udpecho --check` exercises descriptor and
pointer errors, dup/dup2, fork and exec inheritance, exit cleanup, ephemeral
binding, queue-slot exhaustion/reuse and descriptor exhaustion without a NIC.

```sh
python3 tests/network_smoke.py --udp-user
```

This mode builds the ordinary kernel and userspace, starts the host UDP echo
service, and installs `/bin/udpecho` on a disposable disk. All three NIC layouts
run the userspace client and validate the host socket data and PCAPs. Each layout
also interrupts a bound `udpecho --hold` process with Ctrl+C before running the
descriptor checks, proving that the port is released. The no-NIC layout runs
the same lifetime checks and verifies send failure with `ENODEV`. The kernel
boot echo probe is disabled in this mode. `--udp-user` and `--udp-echo` select
separate clients; either may be combined with `--tx-stress`. Normal build
artifacts are restored afterward. Stop other builds before running this mode.


## DNS and nslookup

Fresh development disks include `/bin/nslookup`:

```sh
nslookup example.com
```

It queries the QEMU DNS proxy at `6.6.6.3:53` through userspace UDP. Local ARP
resolution runs automatically; the client retries sends returning `EAGAIN`.
Success prints the queried name and its first IPv4 address. Usage errors return
status 2; lookup failures return 1 with a reason and errno. Existing disks need
the rebuilt command installed, or use `sh qemu.sh` to create a fresh disk.

`<aquaos/dns.h>` exposes `aquaos_dns_lookup(name, server, &address)`, returning
zero on success or -1 with errno. The server and result use `0xAABBCCDD` IPv4
integers. The library creates an ephemeral UDP socket and closes it on every
return path. At the current 100 Hz PIT, each of three attempts allows up to
three seconds for ARP/TX readiness and three seconds for a response. It sleeps
between nonblocking calls, processes at most one datagram per tick, and remains
interruptible by Ctrl+C. Unrelated and malformed replies are ignored within the
same bounded wait. Network errors, matching negative replies and unsupported
truncated responses terminate the lookup immediately.

The DNS wire format follows [RFC 1035](https://www.rfc-editor.org/rfc/rfc1035).
Queries request recursion and A/IN records. Names are ASCII labels of 1–63
letters, digits, hyphens or underscores, with an optional trailing dot and at
most 255 encoded bytes. Responses must match the server IP/port, transaction
ID, normalized question, type and class. Compression decoding checks pointer
bounds, backward targets, expanded lengths and traversal limits. Only answer
records belonging to the queried name or its CNAME chain can supply the result;
authority/additional addresses and unrelated records cannot. Up to eight CNAME
links are followed within one response, independent of record order. A CNAME
without its final A answer returns `ENOENT`.

`aquaos_dns_query` and `aquaos_dns_parse` expose the allocation-free wire helpers
for tests and other clients. The parser validates record bounds across all
sections, limits processing to 64 records and classic 512-byte UDP responses,
and writes the result only on success. Errors distinguish unrelated responses
(`EAGAIN`), malformed data (`EINVAL`), NXDOMAIN/no matching A (`ENOENT`), server
failure (`EIO`), truncation/complexity limits (`EOVERFLOW`) and timeout
(`ETIMEDOUT`). This first resolver has no cache, search suffixes, IDNA, IPv6,
EDNS, TCP fallback, follow-up CNAME queries or DNSSEC. Ping still supports only
the numeric gateway address.

Verification:

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined \
  -idirafter userlibc/include tests/dns_host.c \
  userlibc/aquaos/dns_wire.c userlibc/aquaos/dns.c -o /tmp/aquaos-dns-host
/tmp/aquaos-dns-host
sh build.sh
python3 tools/iso64.py
python3 tests/dns_smoke.py
```

Host tests cover query limits, malformed/truncated packets, compressed names,
CNAME ordering and cycles, question/ID matching, a deterministic mutation corpus,
ARP readiness, retry/timeout behavior, source filtering and descriptor cleanup.
The QEMU test uses a disposable disk and a socket-backed Ethernet fixture at
`6.6.6.3`; it needs no host DNS, Internet connection, TAP or root privileges.
It verifies local ARP, outgoing IP/UDP checksums, a dropped first query, invalid
replies, a compressed CNAME/A answer, NXDOMAIN, truncation, server failure,
timeout and repeated lookup cleanup, plus a separate no-NIC boot. Development
disks are untouched.
