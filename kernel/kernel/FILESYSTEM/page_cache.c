#include <kernel/page_cache.h>
#include <libk/string.h>

#define CACHE_PAGES 64U
#define SECTOR_BYTES 512U
#define PAGE_SECTORS 8U
#define ATA_LBA_LIMIT (1U << 28)
struct cache_page {
    uint32_t index;
    uint8_t valid;
    uint64_t accessed;
    uint8_t data[PAGE_SECTORS * SECTOR_BYTES];
};
static struct cache_page pages[CACHE_PAGES];
static uint64_t clock_tick;
page_cache_stats_t disk_cache_stats;

void page_cache_reset(void) {
    for (unsigned i=0;i<CACHE_PAGES;i++) pages[i].valid=0;
    clock_tick=0;
    disk_cache_stats=(page_cache_stats_t){0};
}
static struct cache_page *find_page(uint32_t index) {
    for (unsigned i=0;i<CACHE_PAGES;i++)
        if (pages[i].valid && pages[i].index==index) return &pages[i];
    return 0;
}
static struct cache_page *allocate_page(uint32_t index) {
    struct cache_page *victim=&pages[0];
    for (unsigned i=0;i<CACHE_PAGES;i++) {
        if (!pages[i].valid) { victim=&pages[i]; break; }
        if (pages[i].accessed<victim->accessed) victim=&pages[i];
    }
    if (victim->valid) disk_cache_stats.evictions++;
    victim->index=index; victim->valid=0;
    return victim;
}
static int valid_range(uint32_t lba, uint32_t sectors, const void *buffer) {
    return buffer && lba<ATA_LBA_LIMIT && sectors<=ATA_LBA_LIMIT-lba;
}
uint32_t page_cache_read(uint32_t lba,uint8_t count,uint8_t *buffer) {
    uint32_t sectors=count ? count : 256, done=0;
    if (!valid_range(lba,sectors,buffer)) return 0;
    while (done<sectors) {
        uint32_t sector=lba+done, offset=sector%PAGE_SECTORS;
        uint32_t n=PAGE_SECTORS-offset;
        if (n>sectors-done) n=sectors-done;
        unsigned mask=((1U<<n)-1)<<offset;
        struct cache_page *page=find_page(sector/PAGE_SECTORS);
        if (page && (page->valid&mask)==mask) disk_cache_stats.hits++;
        else {
            disk_cache_stats.misses++;
            if (!page) page=allocate_page(sector/PAGE_SECTORS);
            /* Only requested sectors are fetched: never read past device end. */
            uint32_t got=ata_raw_read_sectors(sector,(uint8_t)n,buffer+done*SECTOR_BYTES);
            if (got>n) got=n;
            disk_cache_stats.read_sectors+=got;
            if (got!=n) return done+got; /* Failed/short read is not cached. */
            kmemcpy(page->data+offset*SECTOR_BYTES,buffer+done*SECTOR_BYTES,n*SECTOR_BYTES);
            page->valid|=mask;
        }
        page->accessed=++clock_tick;
        kmemcpy(buffer+done*SECTOR_BYTES,page->data+offset*SECTOR_BYTES,n*SECTOR_BYTES);
        done+=n;
    }
    return done;
}
uint32_t page_cache_write(uint32_t lba,uint8_t count,const uint8_t *buffer) {
    uint32_t sectors=count ? count : 256;
    if (!valid_range(lba,sectors,buffer)) return 0;
    uint32_t written=ata_raw_write_sectors(lba,count,buffer);
    if (written>sectors) written=sectors;
    disk_cache_stats.write_sectors+=written;
    for (uint32_t done=0;done<sectors;) {
        uint32_t sector=lba+done,offset=sector%PAGE_SECTORS;
        uint32_t n=PAGE_SECTORS-offset;
        if (n>sectors-done) n=sectors-done;
        struct cache_page *page=find_page(sector/PAGE_SECTORS);
        if (page) {
            /* Partial failures can have modified the device. Invalidate all
             * affected sectors, including any possibly written prefix. */
            unsigned mask=((1U<<n)-1)<<offset;
            if (written!=sectors) page->valid&=~mask;
            else {
                kmemcpy(page->data+offset*SECTOR_BYTES,buffer+done*SECTOR_BYTES,n*SECTOR_BYTES);
                page->valid|=mask;
                page->accessed=++clock_tick;
            }
        }
        done+=n;
    }
    return written;
}
