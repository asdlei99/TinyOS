#include <kernel/assert.h>
#include <kernel/execelf/readelf.h>

#include <shared/proc_mem.h>
#include <shared/stdint.h>
#include <shared/string.h>
#include <shared/sys.h>
#include <shared/utility.h>

/*
    IMPROVE：load_elf应该进行适当的合法性检查
*/

#define PT_LOAD 1

#define EI_NIDENT 16

#define ELF32_Addr  uint32_t
#define ELF32_Half  uint16_t
#define ELF32_Off   uint32_t 
#define ELF32_SWord int32_t
#define ELF32_Word  uint32_t

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    ELF32_Half e_type;
    ELF32_Half e_machine;
    ELF32_Word e_version;
    ELF32_Addr e_entry;     // virtual address program entry
    ELF32_Off  e_phoff;     // program header offset
    ELF32_Off  e_shoff;
    ELF32_Word e_flags;
    ELF32_Half e_ehsize;    // elf header size
    ELF32_Half e_phentsize; // program header size
    ELF32_Half e_phnum;     // program header numbers
    ELF32_Half e_shentsize;
    ELF32_Half e_shnum;
    ELF32_Half e_shstrndx;
} elf32_ehdr;

typedef struct {
    ELF32_Word p_type;     // Only PT_LOAD need loading
    ELF32_Off  p_offset;
    ELF32_Addr p_vaddr;
    ELF32_Addr p_paddr;
    ELF32_Word p_filesz;
    ELF32_Word p_memsz;
    ELF32_Word p_flags;
    ELF32_Word p_align;
} elf32_phdr;

void *load_elf(const void *_filestart, size_t *beg, size_t *end)
{
    ASSERT(beg && end);
    size_t _beg = 0xffffffff, _end = 0;

    elf32_ehdr eh;
    const char *filestart = (const char *)_filestart;

    uint8_t magic_iden[EI_NIDENT] = { 0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01 };

    int pos = 0;
    for (pos = 0; pos < EI_NIDENT; pos++)
    {
        eh.e_ident[pos] = *(filestart + pos);
        if(eh.e_ident[pos] != magic_iden[pos])
            return NULL;
    }
    
    //initialize ELF header
    
    memcpy((char*)&eh, filestart, sizeof(eh));
    pos += sizeof(eh);

    elf32_phdr header;
    pos = eh.e_phoff;
    for(uint16_t i = 0; i < eh.e_phnum; i++)
    {
        memcpy((char*)&header, filestart + pos, sizeof(header));
        pos += sizeof(header);

        if(header.p_type != PT_LOAD)
            continue;

        memcpy((char*)header.p_vaddr, (const char *)(filestart + header.p_offset),
               header.p_filesz);
        if(header.p_memsz > header.p_filesz)
        {
            memset((char *)((char*)header.p_vaddr + header.p_filesz), 0x0,
                   header.p_memsz - header.p_filesz);
        }

        _beg = MIN(_beg, header.p_vaddr);
        _end = MAX(_end, header.p_vaddr + header.p_memsz);
    }

    *beg = _beg, *end = _end;
    return (void*)eh.e_entry;
}
