#include <stdint.h>
#include <libk/stdio.h>
#include "kernel/io.h"
#include "kernel/pic.h"
#include "kernel/kmalloc.h"
#include "kernel/ata.h"
#include <kernel/page_cache.h>
#include <kernel/task.h>

// Primary ATA I/O ports
#define ATA_PRIMARY_DATA       0x1F0
#define ATA_PRIMARY_ERROR      0x1F1
#define ATA_PRIMARY_SECCOUNT   0x1F2
#define ATA_PRIMARY_LBA0       0x1F3
#define ATA_PRIMARY_LBA1       0x1F4
#define ATA_PRIMARY_LBA2       0x1F5
#define ATA_PRIMARY_DRIVE      0x1F6
#define ATA_PRIMARY_STATUS     0x1F7
#define ATA_PRIMARY_COMMAND    0x1F7
#define ATA_PRIMARY_CONTROL    0x3F6  // nIEN + SRST

// ATA status bits
#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DRQ   0x08

// ATA commands
#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_IDENT  0xEC

#define MAX_BLOCK_DEVICES 4

static block_device_t *block_devices[MAX_BLOCK_DEVICES];

// Wait until BSY=0
static inline void ata_wait_busy(void) {
    while (inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY) io_wait();
}
// Wait until DRQ=1
static inline void ata_wait_drq(void) {
    while (!(inb(ATA_PRIMARY_STATUS) & ATA_SR_DRQ)) io_wait();
}
// Wait until BSY=0 and DRDY=1
static inline void ata_wait_ready(void) {
    while (inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY) io_wait();
    while (!(inb(ATA_PRIMARY_STATUS) & ATA_SR_DRDY)) io_wait();
}

// Soft reset, keeping IRQ disabled (nIEN=1)
void ata_soft_reset(void) {
    page_cache_reset();
    outb(ATA_PRIMARY_CONTROL, 0x04 | 0x02);
    io_wait();
    outb(ATA_PRIMARY_CONTROL, 0x02);
    for (volatile int i = 0; i < 100000; i++) io_wait();
}

// Initialization entry
void ata_init(void) {
    ata_soft_reset();
}

// Read multiple sectors; returns number of sectors read
uint32_t ata_raw_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    uint32_t sectors = count ? count : 256;
    // Ensure device ready
    ata_wait_ready();
    // Select master + high LBA bits
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F)); io_wait();
    // Send count and low LBA bits
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA0, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA2, (uint8_t)(lba >> 16));
    // Issue read command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ);
    // For each sector, poll and transfer
    for (uint32_t s = 0; s < sectors; s++) {
        ata_wait_busy();
        ata_wait_drq();
        // Transfer 256 words => 512 bytes
        for (int i = 0; i < 256; i++) {
            uint16_t data;
            __asm__ volatile ("inw %w1, %0" : "=a"(data) : "Nd"(ATA_PRIMARY_DATA));
            *buffer++ = data & 0xFF;
            *buffer++ = data >> 8;
        }
    }
    return sectors;
}

// Write multiple sectors; returns number of sectors written
uint32_t ata_raw_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer) {
    uint32_t sectors = count ? count : 256;
    ata_wait_ready();
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F)); io_wait();
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA0, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA2, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE);
    for (uint32_t s = 0; s < sectors; s++) {
        ata_wait_busy();
        ata_wait_drq();
        for (int i = 0; i < 256; i++) {
            uint16_t data = buffer[0] | (buffer[1] << 8);
            __asm__ volatile ("outw %0, %w1" : : "a"(data), "Nd"(ATA_PRIMARY_DATA));
            buffer += 2;
        }
    }
    return sectors;
}

/* Keep PIO and cache mutation serialized; safe before tasking_init as well. */
uint32_t ata_read_sectors(uint32_t lba,uint8_t count,uint8_t *buffer) {
    task_preemption_disable();
    uint32_t result=page_cache_read(lba,count,buffer);
    task_preemption_enable();
    return result;
}
uint32_t ata_write_sectors(uint32_t lba,uint8_t count,const uint8_t *buffer) {
    task_preemption_disable();
    uint32_t result=page_cache_write(lba,count,buffer);
    task_preemption_enable();
    return result;
}

// Self-test using kmalloc for buffer allocation
void ata_rw_selftest(void) {
    const uint32_t start_lba = 300;
    const uint8_t TEST_COUNT = 4;
    size_t buf_size = (size_t)TEST_COUNT * 512;
    uint8_t *write_buf = kmalloc(buf_size);
    uint8_t *read_buf  = kmalloc(buf_size);
    if (!write_buf || !read_buf) {
        kprintf("ATA selftest: kmalloc failed\n");
        if (write_buf) kfree(write_buf);
        if (read_buf)  kfree(read_buf);
        return;
    }
    ata_init();
    for (size_t i = 0; i < buf_size; i++) write_buf[i] = (uint8_t)i;
    ata_write_sectors(start_lba, TEST_COUNT, write_buf);
    ata_read_sectors(start_lba, TEST_COUNT, read_buf);
    for (size_t i = 0; i < buf_size; i++) {
        if (read_buf[i] != write_buf[i]) {
            kprintf("ATA R/W multi test FAILED at %zu: wrote=0x%02x read=0x%02x\n",
                   i, write_buf[i], read_buf[i]);
            kfree(write_buf); kfree(read_buf);
            return;
        }
    }
    kprintf("ATA R/W multi test PASSED LBA %u..%u\n",
           start_lba, start_lba + TEST_COUNT - 1);
    kfree(write_buf); kfree(read_buf);
}

static int block_device_count = 0;
int register_block_device(block_device_t *dev) {
    if (block_device_count >= MAX_BLOCK_DEVICES) return -1;
    block_devices[block_device_count++] = dev;
    return 0;
}

// IDENTIFY DEVICE for total blocks
uint32_t ata_get_total_blocks() {
    uint16_t id_data[256];
    ata_soft_reset();
    ata_wait_ready();
    outb(ATA_PRIMARY_DRIVE, 0xE0); io_wait();
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENT);
    ata_wait_busy();
    ata_wait_drq();
    for (int i = 0; i < 256; i++) id_data[i] = inw(ATA_PRIMARY_DATA);
    return ((uint32_t)id_data[61] << 16) | id_data[60];
}

void block_devices_init(void) {
    static block_device_t ata0;
    ata_init();
    ata0.read         = ata_read_sectors;
    ata0.write        = ata_write_sectors;
    ata0.block_size   = 512;
    ata0.total_blocks = ata_get_total_blocks();
    register_block_device(&ata0);
}
