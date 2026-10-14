#include <kernel/pci.h>
#include <kernel/io.h>
#include <libk/stdio.h>

static pci_device_t network_device;
static int have_network;

/* AquaOS is single CPU: preserve IF around the shared CF8/CFC transaction. */
static uintptr_t config_lock(void) {
    uintptr_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void config_unlock(uintptr_t flags) {
    if (flags & (1U << 9)) __asm__ volatile ("sti" : : : "memory");
}
static uint32_t config_address(const pci_device_t *d, uint8_t offset) {
    return 0x80000000U | ((uint32_t)d->bus << 16) |
           ((uint32_t)d->slot << 11) | ((uint32_t)d->function << 8) | (offset & 0xfcU);
}
uint32_t pci_read32(const pci_device_t *d, uint8_t offset) {
    uintptr_t flags = config_lock();
    outl(0xcf8, config_address(d, offset));
    uint32_t value = inl(0xcfc);
    config_unlock(flags);
    return value;
}
uint16_t pci_enable_command(const pci_device_t *d, uint16_t bits) {
    uintptr_t flags = config_lock();
    outl(0xcf8, config_address(d, 4));
    uint16_t command = inw(0xcfc) | (bits & 7U);
    outw(0xcfc, command);
    command = inw(0xcfc);
    config_unlock(flags);
    return command;
}
static void describe(pci_device_t *d) {
    uint32_t id = pci_read32(d, 0), cls = pci_read32(d, 8);
    d->vendor_id = id;
    d->device_id = id >> 16;
    d->class_code = cls >> 24;
    d->subclass = cls >> 16;
    d->prog_if = cls >> 8;
    d->header_type = pci_read32(d, 12) >> 16;
    uint32_t irq = pci_read32(d, 0x3c);
    d->irq_line = irq;
    d->irq_pin = irq >> 8;
    unsigned type = d->header_type & 0x7f;
    unsigned count = type == 0 ? 6 : type == 1 ? 2 : 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t raw = pci_read32(d, 0x10 + i * 4);
        if (!raw || raw == 0xffffffffU) continue;
        pci_bar_t *bar = &d->bars[i];
        bar->is_io = raw & 1;
        if (bar->is_io) {
            bar->address = raw & ~3U;
        } else {
            unsigned memory_type = (raw >> 1) & 3;
            if (memory_type == 3) continue; /* Reserved encoding. */
            bar->address = raw & ~15U;
            bar->prefetchable = !!(raw & 8);
            if (memory_type == 2) {
                if (i + 1 >= count) { bar->address = 0; continue; }
                bar->is_64 = 1;
                bar->address |= (uint64_t)pci_read32(d, 0x10 + (++i) * 4) << 32;
            }
        }
    }
}
void pci_enumerate(void (*visit)(const pci_device_t *, void *), void *context) {
    if (!visit) return;
    /* Scan all conventional buses, including firmware-configured bridges. */
    for (unsigned bus = 0; bus < 256; ++bus) {
        for (unsigned slot = 0; slot < 32; ++slot) {
            pci_device_t first = {.bus = bus, .slot = slot};
            if ((pci_read32(&first, 0) & 0xffffU) == 0xffffU) continue;
            unsigned functions = (pci_read32(&first, 12) & 0x800000U) ? 8 : 1;
            for (unsigned function = 0; function < functions; ++function) {
                pci_device_t d = {.bus = bus, .slot = slot, .function = function};
                if ((pci_read32(&d, 0) & 0xffffU) == 0xffffU) continue;
                describe(&d);
                visit(&d, context);
            }
        }
    }
}
static void discover(const pci_device_t *d, void *context) {
    (void)context;
    kprintf("PCI %x:%x.%u %x:%x class=%x:%x\n",
            d->bus, d->slot, d->function, d->vendor_id, d->device_id,
            d->class_code, d->subclass);
    /* QEMU's e1000 models an Intel 82540EM. Only prepare this supported NIC. */
    if (have_network || d->vendor_id != 0x8086 || d->device_id != 0x100e ||
        d->class_code != 2 || d->subclass != 0 || !d->bars[0].address ||
        d->bars[0].is_io || (d->header_type & 0x7f) != 0) return;
    network_device = *d;
    uint16_t bits = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    for (unsigned i = 0; i < 6; ++i)
        if (d->bars[i].is_io && d->bars[i].address) bits |= PCI_COMMAND_IO;
    uint16_t command = pci_enable_command(d, bits);
    if ((command & bits) != bits) {
        kprintf("PCI: E1000 command enable failed\n");
        return;
    }
    have_network = 1;
    kprintf("PCI: E1000 MMIO=0x%lx IRQ=%u pin=%u command=0x%x\n",
            (unsigned long)d->bars[0].address, d->irq_line, d->irq_pin, command);
    for (unsigned i = 0; i < 6; ++i)
        if (d->bars[i].address)
            kprintf("PCI: BAR%u %s address=0x%lx width=%u\n", i,
                    d->bars[i].is_io ? "IO" : "MMIO",
                    (unsigned long)d->bars[i].address, d->bars[i].is_64 ? 64 : 32);
}
void pci_init(void) {
    have_network = 0;
    pci_enumerate(discover, 0);
    if (!have_network) kprintf("PCI: no supported E1000 network card\n");
}
const pci_device_t *pci_network_device(void) {
    return have_network ? &network_device : 0;
}
