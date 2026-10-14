# Networking foundations

AquaOS provides PCI discovery and a polling E1000 driver with receive and
transmit DMA rings. It sends one diagnostic Ethernet broadcast at boot.
ARP resolves the static gateway and answers requests for the guest IP.
A single IPv4/ICMP boot ping verifies the gateway path after ARP resolves.
A userspace `ping` command can repeat gateway probes; DHCP is not implemented.

## Development network

Run `sh qemu.sh`. The launcher creates one E1000 (Intel 82540EM, PCI
8086:100e) with MAC `52:54:00:12:34:56`, attached to QEMU user networking.
It explicitly sets the subnet to `6.6.6.0/24`, host/gateway to `6.6.6.6`,
and first DHCP lease to `6.6.6.15`. The subnet's QEMU DNS proxy is `6.6.6.3`.
The guest now uses static IP `6.6.6.15/24` and resolves gateway `6.6.6.6`.
DNS is not yet used.

Each launch retains `network.pcap` beside `dev.img` under its new
`disk/zenos64.*` directory and prints both paths. Open the capture in Wireshark
or inspect it with `tcpdump -nn -r disk/zenos64.XXXXXX/network.pcap`.
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

This is a static, single-interface milestone: no general ARP table, expiry,
address conflict detection, VLAN support, DHCP or general IP routing.
A full TX queue can drop an ARP reply; bounded gateway retries cover lost
requests without blocking the timer. ARP does not authenticate peer identities.

The QEMU network smoke test checks request fields, captures the gateway reply,
and compares its sender MAC with the guest's resolution message in all three
PCI layouts. Host checks exercise malformed/truncated packets, cache population,
request replies, bounded retries and timer wraparound:

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ikernel/include -idirafter libk/include \
  tests/arp_host.c kernel/kernel/NET/net.c -o /tmp/zenos-arp-host
/tmp/zenos-arp-host
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

Syscalls 29/30 back `zenos_ping_start()` and `zenos_ping_result()` in
`<zenos/net.h>`. Start queues one request; result reports EAGAIN while waiting,
then returns RTT or ETIMEDOUT and consumes the result. The caller sleeps between
polls. One process owns the active probe; other callers get EBUSY at start and
EPERM when reading another process's result. Process exit (including Ctrl+C)
cancels ownership. Wire sequence numbers change between requests to reject
stale replies. Existing development images need the rebuilt `/bin/ping` copied
in while QEMU is stopped, or use `sh qemu.sh` for a fresh image.
