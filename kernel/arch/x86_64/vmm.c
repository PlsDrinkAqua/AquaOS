#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/layout.h>
#include <libk/string.h>

#define PAGE 4096UL
#define ADDRESS UINT64_C(0x000ffffffffff000)
extern page_directory_t boot_page_directory;
static page_directory_t *current;
static uint64_t *table(uint64_t entry) { return (void *)(KERNEL_DIRECT_BASE+(entry&ADDRESS)); }
static uintptr_t physical(const void *p) { return (uintptr_t)p-KERNEL_DIRECT_BASE; }
static void flush(void) { uintptr_t p=physical(current); __asm__ volatile("mov %0,%%cr3" :: "r"(p):"memory"); }
static uint64_t *walk(page_directory_t *root, uintptr_t va, int create, int user) {
    uint64_t *t=root->entries;
    for (int shift=39; shift>12; shift-=9) {
        uint64_t *e=&t[(va>>shift)&511];
        if (!(*e&1)) {
            if (!create) return NULL;
            uintptr_t p=pmm_alloc_frame();
            if (!p) return NULL;
            kmemset(table(p),0,PAGE);
            *e=p|3|(user?4:0);
        }
        if ((*e&128) || (user && !(*e&4))) return NULL;
        t=table(*e);
    }
    return &t[(va>>12)&511];
}
void vmm_init(void) { current=&boot_page_directory; }
page_directory_t *vmm_current_address_space(void) { return current; }
page_directory_t *vmm_create_address_space(void) {
    uintptr_t p=pmm_alloc_frame();
    if (!p) return NULL;
    page_directory_t *root=(void *)table(p);
    kmemset(root,0,PAGE);
    root->entries[511]=boot_page_directory.entries[511];
    return root;
}
void vmm_sync_kernel_mappings(page_directory_t *pd) { if(pd) pd->entries[511]=boot_page_directory.entries[511]; }
void vmm_switch_address_space(page_directory_t *pd) { if(pd) { current=pd; flush(); } }
int vmm_map_page(uintptr_t va, uintptr_t pa, uint32_t flags) {
    if ((va|pa)&(PAGE-1)) return -1;
    int user=!!(flags&4);
    if (user ? (va<PAGE || va>=USER_ADDRESS_LIMIT) : (va<KERNEL_DIRECT_BASE)) return -1;
    uint64_t *e=walk(current,va,1,user);
    if (!e || (*e&1)) return -1;
    *e=(pa&ADDRESS)|(flags&31U)|1;
    __asm__ volatile("invlpg (%0)" :: "r"(va):"memory");
    return 0;
}
uint32_t vmm_translate(uintptr_t va) {
    uint64_t *t=current->entries;
    for(int shift=39;shift>=12;shift-=9) {
        uint64_t e=t[(va>>shift)&511];
        if (!(e&1)) return 0;
        if (shift==12 || (e&128)) return (e&ADDRESS)+(va&((UINT64_C(1)<<shift)-1));
        t=table(e);
    }
    return 0;
}
bool vmm_user_range_valid(const void *ptr,size_t n,bool writable) {
    uintptr_t a=(uintptr_t)ptr;
    if(a>=USER_ADDRESS_LIMIT || n>USER_ADDRESS_LIMIT-a) return false;
    if(!n) return true;
    uintptr_t last=(a+n-1)&~(PAGE-1);
    for(a&=~(PAGE-1);;a+=PAGE) {
        uint64_t *e=walk(current,a,0,1);
        if(!e || (*e&5)!=5 || (writable && !(*e&2))) return false;
        if(a==last) return true;
    }
}
int vmm_unmap_page(uintptr_t va,bool release) {
    uint64_t *e=walk(current,va,0,0);
    if(!e || !(*e&1)) return -1;
    uintptr_t p=*e&ADDRESS;
    *e=0;
    __asm__ volatile("invlpg (%0)" :: "r"(va):"memory");
    if(release) pmm_free_frame(p);
    return 0;
}
static void destroy(uint64_t *t,int level) {
    for(unsigned i=0;i<512;i++) if(t[i]&1) {
        if(level>1) destroy(table(t[i]),level-1);
        pmm_free_frame(t[i]&ADDRESS);
    }
}
void vmm_destroy_address_space(page_directory_t *pd) {
    if(!pd || pd==current || pd==&boot_page_directory) return;
    for(unsigned i=0;i<256;i++) if(pd->entries[i]&1) {
        destroy(table(pd->entries[i]),3);
        pmm_free_frame(pd->entries[i]&ADDRESS);
    }
    pmm_free_frame(physical(pd));
}
static int clone_table(uint64_t *dst,const uint64_t *src,int level) {
    for(unsigned i=0;i<512;i++) if(src[i]&1) {
        uintptr_t p=pmm_alloc_frame();
        if(!p) return -1;
        dst[i]=p|(src[i]&7);
        if(level==1) kmemcpy(table(p),table(src[i]),PAGE);
        else {
            kmemset(table(p),0,PAGE);
            if(clone_table(table(p),table(src[i]),level-1)<0) return -1;
        }
    }
    return 0;
}
page_directory_t *vmm_clone_address_space(page_directory_t *src) {
    page_directory_t *dst=vmm_create_address_space();
    if(!dst) return NULL;
    for(unsigned i=0;i<256;i++) if(src->entries[i]&1) {
        uintptr_t p=pmm_alloc_frame();
        if(!p) goto fail;
        dst->entries[i]=p|(src->entries[i]&7);
        kmemset(table(p),0,PAGE);
        if(clone_table(table(p),table(src->entries[i]),3)<0) goto fail;
    }
    return dst;
fail: vmm_destroy_address_space(dst); return NULL;
}
int vmm_map_region(uintptr_t start,uintptr_t end,uintptr_t phys,uint32_t flags) {
    uintptr_t first=start&~(PAGE-1), va=first;
    for(;va<end;va+=PAGE) {
        uintptr_t p=phys?phys+va-first:pmm_alloc_frame();
        if(!p || vmm_map_page(va,p,flags)<0) {
            if(p && !phys) pmm_free_frame(p);
            vmm_unmap_region(first,va,!phys);
            return -1;
        }
    }
    return 0;
}
int vmm_unmap_region(uintptr_t start,uintptr_t end,bool release) {
    for(uintptr_t va=start&~(PAGE-1);va<end;va+=PAGE) vmm_unmap_page(va,release);
    return 0;
}
