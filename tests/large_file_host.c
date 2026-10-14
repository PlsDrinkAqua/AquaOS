#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <kernel/ext2.h>
static struct ext2_super_block super;
static unsigned char blocks[32][4096];
static unsigned used[32];
static struct ext2_inode stored;
int ext2_read_inode(uint32_t ino, struct ext2_inode *out) { (void)ino; *out=stored; return 0; }
int ext2_write_inode(uint32_t ino,const struct ext2_inode *in) { (void)ino; stored=*in; return 0; }
int ext2_enable_large_files(void) { return 0; }
static int allocation_budget=-1, write_budget=-1;
const struct ext2_super_block *ext2_sb(void) { return &super; }
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void *kmemset(void *p,int c,size_t n) { return memset(p,c,n); }
void *kmemcpy(void *d,const void *s,size_t n) { return memcpy(d,s,n); }
int ext2_alloc_block(uint32_t *out) {
    if(allocation_budget==0) return -1;
    if(allocation_budget>0) allocation_budget--;
    for(unsigned i=1;i<32;i++) if(!used[i]) {
        used[i]=1;memset(blocks[i],0,sizeof(blocks[i]));*out=i;return 0;
    }
    return -1;
}
int ext2_free_block(uint32_t b) { assert(b && b<32 && used[b]);used[b]=0;return 0; }
int ext2_read_block(uint32_t b,void *p) { assert(b && b<32 && used[b]);memcpy(p,blocks[b],1024U<<super.s_log_block_size);return 0; }
int ext2_write_block(uint32_t b,const void *p) {
    if(write_budget==0) return -1;
    if(write_budget>0) write_budget--;
    assert(b && b<32 && used[b]);memcpy(blocks[b],p,1024U<<super.s_log_block_size);return 0;
}
#include "../kernel/kernel/FILESYSTEM/ext2_api.c"
int main(void) {
    for(unsigned log=0;log<=2;log+=2) {
        super.s_log_block_size=log;
        uint64_t n=block_size()/4;
        uint64_t indices[]={11,12,12+n-1,12+n,12+n+n*n-1,12+n+n*n,12+n+n*n+n*n*n-1};
        struct ext2_inode inode={0};uint32_t b,found;
        for(unsigned i=0;i<7;i++) {
            assert(!inode_ensure_block(&inode,indices[i],&b));
            assert(!inode_block(&inode,indices[i],&found) && found==b);
        }
        assert(inode_block(&inode,12+n+n*n+n*n*n,&found)==-K_EFBIG);
        for(unsigned i=0;i<15;i++) if(inode.i_block[i]) assert(!free_tree(inode.i_block[i],i<12?0:i-11));
        for(unsigned i=1;i<32;i++) assert(!used[i]);
        for(int budget=0;budget<4;budget++) {
            memset(&inode,0,sizeof(inode));allocation_budget=budget;
            assert(inode_ensure_block(&inode,12+n+n*n,&b)==-K_ENOSPC);
            for(unsigned i=1;i<32;i++) assert(!used[i]);
        }
        allocation_budget=-1;
        for(int budget=0;budget<3;budget++) {
            memset(&inode,0,sizeof(inode));write_budget=budget;
            assert(inode_ensure_block(&inode,12+n+n*n,&b)==-K_EIO);
            assert(!inode.i_block[14] && !inode.i_blocks);
            for(unsigned i=1;i<32;i++) assert(!used[i]);
        }
        write_budget=-1;
        memset(&stored,0,sizeof(stored)); stored.i_mode=0x8000;
        ext2_file_t file={0};file.used=1;file.ino=1;
        file.pos=(12+n-1)*block_size();
        unsigned char data[8192];memset(data,'A',sizeof(data));
        allocation_budget=2;
        assert(ext2_file_write(&file,data,2*block_size(),0)==(int)block_size());
        assert(inode_size(&stored)==(12+n)*block_size());
        assert(file.pos==inode_size(&stored));
        allocation_budget=-1;
        unsigned char readback[4096];
        file.pos=(12+n-1)*block_size();
        assert(ext2_file_read(&file,readback,block_size())==(int)block_size());
        assert(!memcmp(readback,data,block_size()));
        assert(!inode_truncate(1,&stored));
        for(unsigned i=1;i<32;i++) assert(!used[i]);
    }
    puts("PASS: indirect boundaries, reclamation and allocation/write failure rollback");
}
