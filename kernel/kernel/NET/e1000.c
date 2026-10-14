#include <kernel/e1000.h>
#include <kernel/errno.h>
#include <kernel/layout.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <libk/stdio.h>
#include <libk/string.h>

/* Intel 8254x register offsets, in bytes (not PCI configuration offsets). */
#define E1000_STATUS 0x0008U
#define E1000_IMC    0x00d8U
#define E1000_RAL0   0x5400U
#define E1000_RAH0   0x5404U
#define E1000_CTRL   0x0000U
#define E1000_RCTL   0x0100U
#define E1000_RDBAL  0x2800U
#define E1000_RDBAH  0x2804U
#define E1000_RDLEN  0x2808U
#define E1000_RDH    0x2810U
#define E1000_RDT    0x2818U
#define E1000_MTA    0x5200U
#define E1000_TCTL   0x0400U
#define E1000_TIPG   0x0410U
#define E1000_TDBAL  0x3800U
#define E1000_TDBAH  0x3804U
#define E1000_TDLEN  0x3808U
#define E1000_TDH    0x3810U
#define E1000_TDT    0x3818U
#define TX_COUNT 32U
#define TX_DONE 1U
#define TX_COMMAND (1U | 2U | 8U) /* EOP, IFCS, RS. */
#define RX_COUNT 32U
#define RX_BUFFER_SIZE 2048U
#define RX_DONE 1U
#define RX_END  2U
#define RCTL_ENABLE (1U << 1)
#define RCTL_BROADCAST (1U << 15)
#define RCTL_STRIP_CRC (1U << 26)
#define E1000_STATUS_LU (1U << 1)
#define E1000_RAH_AV    (1U << 31)
#define PAGE_SIZE 4096U
/* Only map through RAH0; this probe does not use the rest of BAR0. */
#define PROBE_SIZE 0x6000U

static volatile uint32_t *registers;

/* Legacy receive descriptor layout consumed and updated by the NIC. */
typedef struct {
    uint64_t address;
    uint16_t length, checksum;
    uint8_t status, errors;
    uint16_t special;
} rx_descriptor_t;
_Static_assert(sizeof(rx_descriptor_t) == 16, "E1000 RX descriptor size");

static volatile rx_descriptor_t *rx_ring;
static uint32_t rx_buffers[RX_COUNT];
static unsigned rx_next;
static unsigned rx_packets;
static int rx_ready, rx_fragment;

static void format_mac(const uint8_t *bytes, char out[18]) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 6; ++i) {
        out[i * 3] = hex[bytes[i] >> 4];
        out[i * 3 + 1] = hex[bytes[i] & 15];
        out[i * 3 + 2] = ':';
    }
    out[17] = '\0';
}

static uint32_t read_register(unsigned offset) {
    return registers[offset / sizeof(uint32_t)];
}

/* One descriptor per frame; buffers stay owned by DMA until DD. */
typedef struct {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset, command;
    uint8_t status, checksum_start;
    uint16_t special;
} tx_descriptor_t;
_Static_assert(sizeof(tx_descriptor_t) == 16, "E1000 TX descriptor size");
static volatile tx_descriptor_t *tx_ring;
static uint32_t tx_buffers[TX_COUNT];
static unsigned tx_next, tx_clean, tx_pending;
static int tx_ready;

static void transmit_reclaim(void) {
    while (tx_pending && (tx_ring[tx_clean].status & TX_DONE)) {
        __asm__ volatile("lfence" ::: "memory");
        tx_clean = (tx_clean + 1) % TX_COUNT;
        --tx_pending;
    }
}

int e1000_transmit(const void *frame, size_t length) {
    if (!frame || length < 14 || length > E1000_FRAME_MAX) return -K_EINVAL;
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    int result = -K_ENODEV;
    if (!tx_ready) goto out;
    transmit_reclaim();
    result = -K_EAGAIN;
    /* Leave one descriptor unused so TDT never catches an unconsumed TDH. */
    if (tx_pending == TX_COUNT - 1) goto out;
    volatile tx_descriptor_t *desc = &tx_ring[tx_next];
    uint8_t *buffer = (uint8_t *)(KERNEL_DIRECT_BASE + tx_buffers[tx_next]);
    kmemcpy(buffer, frame, length);
    /* Pad short frames explicitly; callers need not supply padding or FCS. */
    if (length < 60) {
        kmemset(buffer + length, 0, 60 - length);
        length = 60;
    }
    desc->length = length;
    desc->checksum_offset = 0;
    desc->checksum_start = 0;
    desc->special = 0;
    desc->command = TX_COMMAND;
    desc->status = 0;
    tx_next = (tx_next + 1) % TX_COUNT;
    ++tx_pending;
    __asm__ volatile("mfence" ::: "memory");
    registers[E1000_TDT / 4] = tx_next;
    (void)read_register(E1000_STATUS);
    result = 0;
out:
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
    return result;
}

static void transmit_init(void) {
    registers[E1000_TCTL / 4] = 0;
    (void)read_register(E1000_STATUS);
    uint32_t physical = pmm_alloc_frame();
    if (!physical) goto failed;
    tx_ring = (volatile tx_descriptor_t *)(KERNEL_DIRECT_BASE + physical);
    kmemset((void *)tx_ring, 0, PAGE_SIZE);
    unsigned allocated = 0;
    for (; allocated < TX_COUNT; ++allocated) {
        tx_buffers[allocated] = pmm_alloc_frame();
        if (!tx_buffers[allocated]) {
            while (allocated) pmm_free_frame(tx_buffers[--allocated]);
            pmm_free_frame(physical);
            tx_ring = 0;
            goto failed;
        }
        tx_ring[allocated].address = tx_buffers[allocated];
        tx_ring[allocated].status = TX_DONE;
    }
    registers[E1000_TDBAL / 4] = physical;
    registers[E1000_TDBAH / 4] = 0;
    registers[E1000_TDLEN / 4] = TX_COUNT * sizeof(tx_descriptor_t);
    registers[E1000_TDH / 4] = 0;
    registers[E1000_TDT / 4] = 0;
    /* Copper 82540EM: IPGT=10, IPGR1=8, IPGR2=6; CT=15, COLD=64. */
    registers[E1000_TIPG / 4] = 10U | (8U << 10) | (6U << 20);
    __asm__ volatile("mfence" ::: "memory");
    registers[E1000_TCTL / 4] = (1U << 1) | (1U << 3) | (15U << 4) | (64U << 12);
    (void)read_register(E1000_STATUS);
    tx_ready = 1;
    kprintf("E1000: TX ready, %u descriptors (polling)\n", TX_COUNT);
    /* Experimental EtherType: harmless broadcast proves the DMA path at boot. */
    uint8_t probe[60] = {255,255,255,255,255,255,6,7,6,7,6,7,0x88,0xb5};
    kmemcpy(probe + 14, "ZenOS TX probe", 14);
    if (e1000_transmit(probe, sizeof(probe)) == 0)
        kprintf("E1000: TX probe queued\n");
#ifdef E1000_TX_STRESS
    /* Test-only traffic exercises DD reclamation and multiple ring wraps. */
    unsigned sent = 1;
    for (unsigned attempts = 0; sent < 70 && attempts < 10000000; ++attempts) {
        probe[28] = sent;
        if (e1000_transmit(probe, sizeof(probe)) == 0) ++sent;
        else __asm__ volatile("pause");
    }
    unsigned attempts = 0;
    while (tx_pending && attempts++ < 10000000) {
        transmit_reclaim();
        __asm__ volatile("pause");
    }
    kprintf("E1000: TX stress %s\n", sent == 70 && !tx_pending ? "PASS" : "FAIL");
#endif
    return;
failed:
    kprintf("E1000: TX allocation failed\n");
}

static void receive_init(void) {
    registers[E1000_RCTL / 4] = 0;
    (void)read_register(E1000_STATUS);
    uint32_t ring_phys = pmm_alloc_frame();
    if (!ring_phys) goto failed;
    rx_ring = (volatile rx_descriptor_t *)(KERNEL_DIRECT_BASE + ring_phys);
    kmemset((void *)rx_ring, 0, PAGE_SIZE);
    unsigned allocated = 0;
    for (; allocated < RX_COUNT; ++allocated) {
        rx_buffers[allocated] = pmm_alloc_frame();
        if (!rx_buffers[allocated]) {
            while (allocated) pmm_free_frame(rx_buffers[--allocated]);
            pmm_free_frame(ring_phys);
            rx_ring = 0;
            goto failed;
        }
        kmemset((void *)(KERNEL_DIRECT_BASE + rx_buffers[allocated]), 0, PAGE_SIZE);
        rx_ring[allocated].address = rx_buffers[allocated];
    }

    /* Disable other exact-match addresses and multicast hash filtering. */
    for (unsigned i = 1; i < 16; ++i)
        registers[(E1000_RAH0 + i * 8) / 4] = 0;
    for (unsigned i = 0; i < 128; ++i) registers[E1000_MTA / 4 + i] = 0;
    registers[E1000_RDBAL / 4] = ring_phys;
    registers[E1000_RDBAH / 4] = 0;
    registers[E1000_RDLEN / 4] = RX_COUNT * sizeof(rx_descriptor_t);
    registers[E1000_RDH / 4] = 0;
    /* Publish descriptor contents before handing ownership to DMA. */
    __asm__ volatile("mfence" ::: "memory");
    registers[E1000_RDT / 4] = RX_COUNT - 1;
    registers[E1000_CTRL / 4] = read_register(E1000_CTRL) | (1U << 6); /* Link up. */
    /* 2048-byte buffers (BSIZE=0), standard frames, no promiscuous mode. */
    registers[E1000_RCTL / 4] = RCTL_ENABLE | RCTL_BROADCAST | RCTL_STRIP_CRC;
    (void)read_register(E1000_STATUS);
    rx_ready = 1;
    kprintf("E1000: RX ready, %u descriptors, %u-byte buffers (polling)\n",
            RX_COUNT, RX_BUFFER_SIZE);
    return;
failed:
    kprintf("E1000: RX allocation failed\n");
}

void e1000_poll(void) {
    if (tx_ready) transmit_reclaim();
    if (!rx_ready) return;
    /* Bound time spent in the timer interrupt, even under continuous traffic. */
    for (unsigned budget = 0; budget < 4; ++budget) {
        volatile rx_descriptor_t *desc = &rx_ring[rx_next];
        uint8_t status = desc->status;
        if (!(status & RX_DONE)) break;
        /* DD transfers ownership to the CPU; then read DMA metadata/data. */
        __asm__ volatile("lfence" ::: "memory");
        unsigned length = desc->length;
        if (!rx_fragment && (status & RX_END) && !desc->errors &&
            length >= 14 && length <= RX_BUFFER_SIZE) {
            const uint8_t *packet = (const uint8_t *)(KERNEL_DIRECT_BASE + rx_buffers[rx_next]);
            char destination[18], source[18];
            format_mac(packet, destination);
            format_mac(packet + 6, source);
            unsigned type = ((unsigned)packet[12] << 8) | packet[13];
            kprintf("E1000: RX #%u len=%u type=0x%x\n", ++rx_packets, length, type);
            kprintf("  src=%s dst=%s\n", source, destination);
        }
        /* This first driver drops fragmented/error frames and recycles them. */
        rx_fragment = !(status & RX_END);
        desc->length = 0;
        desc->checksum = 0;
        desc->errors = 0;
        desc->special = 0;
        desc->status = 0;
        __asm__ volatile("mfence" ::: "memory");
        registers[E1000_RDT / 4] = rx_next;
        (void)read_register(E1000_STATUS);
        rx_next = (rx_next + 1) % RX_COUNT;
    }
}

void e1000_init(void) {
    const pci_device_t *nic = pci_network_device();
    if (!nic || registers) return;
    uint64_t physical = nic->bars[0].address;
    if ((physical & (PAGE_SIZE - 1)) ||
        physical > UINT64_C(0x000fffffffffffff) - PROBE_SIZE) {
        kprintf("E1000: invalid MMIO address\n");
        return;
    }

    /* Supervisor-only, writable, uncached device pages. */
    unsigned flags = 3U | VMM_WRITE_THROUGH | VMM_CACHE_DISABLE;
    if (vmm_map_region(KERNEL_MMIO_BASE, KERNEL_MMIO_BASE + PROBE_SIZE,
                       (uintptr_t)physical, flags) < 0) {
        kprintf("E1000: MMIO mapping failed\n");
        return;
    }
    registers = (volatile uint32_t *)(uintptr_t)KERNEL_MMIO_BASE;

    /* No IRQ handler yet: mask all NIC interrupt sources and flush the write. */
    registers[E1000_IMC / sizeof(uint32_t)] = UINT32_MAX;
    uint32_t status = read_register(E1000_STATUS);
    if (status == UINT32_MAX) {
        kprintf("E1000: MMIO registers unavailable\n");
        return;
    }

    /* Set the runtime station MAC to 06:07:06:07:06:07.
     * RAL stores bytes 0..3, least significant byte first; RAH stores 4..5.
     * AV marks the receive address as valid. EEPROM is not modified.
     */
    registers[E1000_RAH0 / sizeof(uint32_t)] = 0;
    registers[E1000_RAL0 / sizeof(uint32_t)] = 0x07060706U;
    registers[E1000_RAH0 / sizeof(uint32_t)] = E1000_RAH_AV | 0x0706U;
    status = read_register(E1000_STATUS); /* Flush posted MMIO writes. */
    uint32_t low = read_register(E1000_RAL0);
    uint32_t high = read_register(E1000_RAH0);
    if (status == UINT32_MAX || !(high & E1000_RAH_AV)) {
        kprintf("E1000: MAC register unavailable or invalid\n");
        return;
    }

    /* Format the programmed receive address read back from the NIC. */
    static const char hex[] = "0123456789abcdef";
    char mac[18];
    for (unsigned i = 0; i < 6; ++i) {
        uint8_t byte = i < 4 ? low >> (i * 8) : high >> ((i - 4) * 8);
        mac[i * 3] = hex[byte >> 4];
        mac[i * 3 + 1] = hex[byte & 15];
        mac[i * 3 + 2] = ':';
    }
    mac[17] = '\0';
    kprintf("E1000: MAC=%s link=%s STATUS=0x%x\n", mac,
            (status & E1000_STATUS_LU) ? "up" : "down", status);
    receive_init();
    transmit_init();
}
