# Networking foundations

ZenOS provides PCI discovery and a polling E1000 driver with receive and
transmit DMA rings. It sends one diagnostic Ethernet broadcast at boot.
ARP, IP, DHCP and ping are not implemented yet.

## Development network

Run `sh qemu.sh`. The launcher creates one E1000 (Intel 82540EM, PCI
8086:100e) with MAC `52:54:00:12:34:56`, attached to QEMU user networking.
It explicitly sets the subnet to `10.0.2.0/24`, host/gateway to `10.0.2.2`,
and first DHCP lease to `10.0.2.15`. QEMU's default DNS proxy is `10.0.2.3`.
For the future static-IP milestone, use `10.0.2.15/24` with that gateway and DNS.
These are harness settings, not configuration currently applied by the guest.

Each launch retains `network.pcap` beside `dev.img` under its new
`disk/zenos64.*` directory and prints both paths. Open the capture in Wireshark
or inspect it with `tcpdump -nn -r disk/zenos64.XXXXXX/network.pcap`.
Expect a 60-byte broadcast with experimental EtherType 0x88b5 and payload
`ZenOS TX probe`, sourced from the programmed station MAC `06:07:06:07:06:07`.
The user backend requires no TAP device or root privileges. Future guest
connections can use its outbound NAT; arbitrary inbound connections require
explicit forwarding. Host-to-guest ping is not a suitable test of this backend.
See [QEMU network and capture options](https://www.qemu.org/docs/master/system/invocation.html).

When invoking QEMU directly, use the same network arguments:

```sh
-netdev user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dhcpstart=10.0.2.15 \
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
completion callbacks, link-down timeouts or protocol handling yet.

Register and descriptor programming follows the
[Intel 8254x software developer manual](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf).

`python3 tests/network_smoke.py` checks the exact captured diagnostic frame.
`python3 tests/network_smoke.py --tx-stress` builds test-only traffic, checks
70 frames and completed descriptors across multiple ring wraps for all three
PCI layouts, and then rebuilds the normal kernel and ISO. The no-NIC boot is
also checked. The stress mode changes build artifacts; run it with other
builds stopped. Guest development disks are untouched.
