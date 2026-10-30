#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kernel/page_cache.h>
static unsigned char disk[1024][512];
static unsigned reads, writes;
static int short_read=-1, short_write=-1;
void *kmemcpy(void *d,const void *s,size_t n) { return memcpy(d,s,n); }
uint32_t ata_raw_read_sectors(uint32_t lba,uint8_t count,uint8_t *buffer) {
    unsigned n=count ? count : 256; reads++;
    assert(lba+n<=1024);
    if(short_read>=0 && n>(unsigned)short_read) n=short_read;
    memcpy(buffer,disk[lba],n*512); return n;
}
uint32_t ata_raw_write_sectors(uint32_t lba,uint8_t count,const uint8_t *buffer) {
    unsigned n=count ? count : 256; writes++;
    assert(lba+n<=1024);
    if(short_write>=0 && n>(unsigned)short_write) n=short_write;
    memcpy(disk[lba],buffer,n*512); return n;
}
#include "../kernel/kernel/FILESYSTEM/page_cache.c"
int main(void) {
    static unsigned char buffer[256*512], replacement[256*512];
    for(unsigned s=0;s<1024;s++) for(unsigned b=0;b<512;b++) disk[s][b]=(s*7+b)%251;
    memset(replacement,0xa5,sizeof(replacement));
    page_cache_reset();
    assert(page_cache_read(16,8,buffer)==8 && reads==1);
    assert(!memcmp(buffer,disk[16],4096));
    assert(page_cache_read(16,8,buffer)==8 && reads==1 && disk_cache_stats.hits==1);
    assert(page_cache_read(22,4,buffer)==4 && reads==2);
    assert(!memcmp(buffer,disk[22],2048));
    assert(page_cache_read(22,4,buffer)==4 && reads==2);
    assert(page_cache_write(23,3,replacement)==3 && writes==1);
    assert(!memcmp(disk[23],replacement,1536)); /* Write-through, no deferred dirt. */
    assert(page_cache_read(22,4,buffer)==4 && reads==2);
    assert(!memcmp(buffer,disk[22],2048));
    short_write=1;
    memset(replacement,0x3c,sizeof(replacement));
    assert(page_cache_write(23,3,replacement)==1);
    short_write=-1;
    unsigned before=reads;
    assert(page_cache_read(22,4,buffer)==4 && reads>before);
    assert(!memcmp(buffer,disk[22],2048)); /* No old cache after partial writes. */
    page_cache_reset(); reads=0;
    short_read=1;
    assert(page_cache_read(0,4,buffer)==1 && reads==1);
    short_read=-1;
    assert(page_cache_read(0,4,buffer)==4 && reads==2);
    assert(!memcmp(buffer,disk[0],2048));
    page_cache_reset(); reads=0;
    for(unsigned i=0;i<64;i++) assert(page_cache_read(i*8,8,buffer)==8);
    assert(reads==64);
    assert(page_cache_read(0,8,buffer)==8 && reads==64); /* Make page zero MRU. */
    assert(page_cache_read(64*8,8,buffer)==8 && reads==65);
    assert(disk_cache_stats.evictions==1);
    assert(page_cache_read(0,8,buffer)==8 && reads==65);
    assert(page_cache_read(8,8,buffer)==8 && reads==66); /* LRU page one evicted. */
    page_cache_reset(); reads=writes=0;
    assert(page_cache_read(0,0,buffer)==256 && !memcmp(buffer,disk[0],sizeof(buffer)));
    assert(page_cache_write(0,0,replacement)==256 && writes==1);
    assert(page_cache_read(0,0,buffer)==256 && !memcmp(buffer,replacement,sizeof(buffer)));
    before=reads;
    assert(page_cache_read(0xffffffff,1,buffer)==0 && reads==before);
    assert(page_cache_read((1U<<28)-1,2,buffer)==0 && reads==before);
    page_cache_reset();
    assert(page_cache_read(16,8,buffer)==8 && reads>before);
    puts("PASS: page cache hits, boundaries, LRU, write-through, failures and reset");
}
