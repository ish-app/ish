#ifndef ELF_H
#define ELF_H

#include "misc.h"

#define ELF_MAGIC "\177ELF"
#define ELF_32BIT 1
#define ELF_64BIT 2
#define ELF_LITTLEENDIAN 1
#define ELF_BIGENDIAN 2
#define ELF_LINUX_ABI 3
#define ELF_EXECUTABLE 2
#define ELF_X86 3

struct elf_header {
    uint32_t magic;
    byte_t bitness;
    byte_t endian;
    byte_t elfversion1;
    byte_t abi;
    byte_t abi_version;
    byte_t padding[7];
    uint16_t type; // library or executable or what
    uint16_t machine;
    uint32_t elfversion2;
    dword_t entry_point;
    dword_t prghead_off;
    dword_t secthead_off;
    uint32_t flags;
    uint16_t header_size;
    uint16_t phent_size;
    uint16_t phent_count;
    uint16_t shent_size;
    uint16_t shent_count;
    uint16_t sectname_index;
};

#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6
#define PT_TLS 7
#define PT_NUM 8

struct prg_header {
    uint32_t type;
    dword_t offset;
    dword_t vaddr;
    dword_t paddr;
    dword_t filesize;
    dword_t memsize;
    uint32_t flags;
    dword_t alignment; // must be power of 2
};

#define PT_R (1 << 2)
#define PT_W (1 << 1)
#define PT_X (1 << 0)

#endif
