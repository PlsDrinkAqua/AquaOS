#include <kernel/elf.h>
#include <kernel/task.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/kmalloc.h>
#include <kernel/ext2_api.h>
#include <libk/string.h>
#include <libk/stdio.h>

typedef struct {
    unsigned char ident[16];
    uint16_t type,machine;
    uint32_t version;
    uint64_t entry,phoff,shoff;
    uint32_t flags;
    uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx;
} header64;
typedef struct {
    uint32_t type,flags;
    uint64_t offset,vaddr,paddr,filesz,memsz,align;
} program64;
_Static_assert(sizeof(header64)==64,"ELF64 header");
_Static_assert(sizeof(program64)==56,"ELF64 program header");
int elf_load_from_memory(const void *image,size_t size,elf_load_result_t *out) {
    const header64 *h=image;
    if(!image || !out || size<sizeof(*h) || kmemcmp(h->ident,"\177ELF\2\1\1",7) ||
       h->machine!=62 || h->type!=2 || h->version!=1 || h->ehsize!=sizeof(*h) ||
       h->phentsize!=sizeof(program64) || !h->phnum || h->phoff>size ||
       h->phnum>(size-h->phoff)/sizeof(program64)) return -1;
    const program64 *ph=(const void *)((const char *)image+h->phoff);
    uintptr_t highest=0;
    int valid_entry=0;
    /* Validate all segments before allocating. Dynamic linking is unsupported. */
    for(unsigned i=0;i<h->phnum;i++) {
        const program64 *p=&ph[i];
        if(p->type==2 || p->type==3) return -1;
        if(p->type!=1 || !p->memsz) continue;
        if(p->filesz>p->memsz || p->offset>size || p->filesz>size-p->offset ||
           p->vaddr<4096 || p->vaddr>=USER_STACK_TOP-USER_STACK_BYTES-4096 ||
           p->memsz>USER_STACK_TOP-USER_STACK_BYTES-4096-p->vaddr ||
           (p->align>1 && ((p->align&(p->align-1)) || ((p->vaddr-p->offset)&(p->align-1))))) return -1;
        if((p->flags&1) && h->entry>=p->vaddr && h->entry-p->vaddr<p->memsz) valid_entry=1;
        uintptr_t end=p->vaddr+p->memsz;
        if(end>highest) highest=end;
    }
    if(!highest || !valid_entry) return -1;
    for(unsigned i=0;i<h->phnum;i++) {
        const program64 *p=&ph[i];
        if(p->type!=1 || !p->memsz) continue;
        uintptr_t end=(p->vaddr+p->memsz+4095)&~4095UL;
        for(uintptr_t va=p->vaddr&~4095UL;va<end;va+=4096) {
            if(!vmm_translate(va)) {
                uintptr_t physical=pmm_alloc_frame();
                if(!physical) return -1;
                if(vmm_map_page(va,physical,7)<0) { pmm_free_frame(physical); return -1; }
                kmemset((void *)va,0,4096);
            }
        }
        kmemcpy((void *)p->vaddr,(const char *)image+p->offset,p->filesz);
        kmemset((void *)(p->vaddr+p->filesz),0,p->memsz-p->filesz);
    }
    out->entry=h->entry;
    out->heap_start=out->heap_end=(highest+4095)&~4095UL;
    return 0;
}
int elf_load_from_file_into_address_space(const char *path,page_directory_t *space,elf_load_result_t *out) {
    ext2_file_t f;
    if(!path || !space || !out || ext2_file_open(path,0,&f)<0) return -1;
    size_t size=ext2_file_size(&f);
    if(!size) return -1;
    void *data=kmalloc(size);
    if(!data) return -1;
    int result=-1;
    if(ext2_file_read(&f,data,size)==(int)size) {
        page_directory_t *old=vmm_current_address_space();
        vmm_switch_address_space(space);
        result=elf_load_from_memory(data,size,out);
        vmm_switch_address_space(old);
    }
    kfree(data);
    return result;
}
int elf_load_from_file(const char *path,elf_load_result_t *out) {
    return elf_load_from_file_into_address_space(path,vmm_current_address_space(),out);
}
