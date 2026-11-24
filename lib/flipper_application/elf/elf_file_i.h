#pragma once
#include "elf_file.h"
#include <m-dict.h>

#ifdef __cplusplus
extern "C" {
#endif

DICT_DEF2(AddressCache, int, M_DEFAULT_OPLIST, Elf32_Addr, M_DEFAULT_OPLIST) //-V1048

/**
 * Callable elf entry type
 */
typedef int32_t(entry_t)(void*);

typedef struct ELFSection ELFSection;

struct ELFSection {
    void* data;
    Elf32_Word size;

    size_t rel_count;
    Elf32_Off rel_offset;

    /* .fast.rel record stream, read from the file during relocation */
    Elf32_Off fast_rel_offset;
    Elf32_Word fast_rel_size;

    uint16_t sec_idx;
};

DICT_DEF2(ELFSectionDict, const char*, M_CSTR_OPLIST, ELFSection, M_POD_OPLIST)

struct ELFFile {
    size_t sections_count;
    off_t section_table;
    off_t section_table_strings;
    size_t section_table_strings_size;

    size_t symbol_count;
    off_t symbol_table;
    off_t symbol_table_strings;
    off_t entry;
    ELFSectionDict_t sections;

    AddressCache_t relocation_cache;
    AddressCache_t trampoline_cache;
    bool trampoline_cache_freed;
    struct ELFTrampolineSlab* trampoline_slabs;

    File* fd;
    const ElfApiInterface* api_interface;
    ELFDebugLinkInfo debug_link_info;

    ELFSection* preinit_array;
    ELFSection* init_array;
    ELFSection* fini_array;

    bool init_array_called;
};

#ifdef __cplusplus
}
#endif