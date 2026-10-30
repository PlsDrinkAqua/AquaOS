#pragma once
#include <stdint.h>
/* Single primary ATA device: bounded 4 KiB disk pages, sector-valid masks.
 * All callers serialize with task_preemption_disable (single CPU).
 * Writes reach the device synchronously; there are no dirty cache pages. */
typedef struct {
    uint64_t hits, misses, read_sectors, write_sectors, evictions;
} page_cache_stats_t;
extern page_cache_stats_t disk_cache_stats;
void page_cache_reset(void);
uint32_t page_cache_read(uint32_t lba, uint8_t count, uint8_t *buffer);
uint32_t page_cache_write(uint32_t lba, uint8_t count, const uint8_t *buffer);
/* Uncached device backend; only the cache should call these. */
uint32_t ata_raw_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
uint32_t ata_raw_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer);
