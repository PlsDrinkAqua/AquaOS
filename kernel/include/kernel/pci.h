#ifndef _KERNEL_PCI_H
#define _KERNEL_PCI_H
#include <stdint.h>

#define PCI_COMMAND_IO 1U
#define PCI_COMMAND_MEMORY 2U
#define PCI_COMMAND_MASTER 4U

typedef struct {
    uint64_t address; /* Physical address; MMIO still needs mapping by the driver. */
    uint8_t is_io, is_64, prefetchable;
} pci_bar_t;
typedef struct {
    uint8_t bus, slot, function, class_code, subclass, prog_if, header_type;
    uint8_t irq_line, irq_pin;
    uint16_t vendor_id, device_id;
    pci_bar_t bars[6]; /* Upper half of a 64-bit BAR has no separate entry. */
} pci_device_t;

/* Conventional PCI config space only; offsets must be DWORD aligned. */
uint32_t pci_read32(const pci_device_t *device, uint8_t offset);
/* Command writes use 16-bit I/O to avoid clearing write-one-to-clear status. */
uint16_t pci_enable_command(const pci_device_t *device, uint16_t bits);
/* Callback gets a temporary descriptor; copy it if needed after returning. */
void pci_enumerate(void (*visit)(const pci_device_t *, void *), void *context);
void pci_init(void);
const pci_device_t *pci_network_device(void);
#endif
