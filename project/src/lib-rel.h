
#ifndef WSZST_LIB_REL_H
#define WSZST_LIB_REL_H

#include <stdint.h>
#include "types.h"

// BrawlCrate REL structures

typedef struct rel_info_s {
    u32 id;
    u32 linkNext;
    u32 linkPrev;
    u32 numSections;
    u32 sectionInfoOffset;
    u32 nameOffset;
    u32 nameSize;
    u32 version;
} __attribute__((packed)) rel_info_t;

typedef struct rel_header_s {
    rel_info_t info;
    u32 bssSize;
    u32 relOffset;
    u32 impOffset;
    u32 impSize;
    u8 prologSection;
    u8 epilogSection;
    u8 unresolvedSection;
    u8 bssSection;
    u32 prologOffset;
    u32 epilogOffset;
    u32 unresolvedOffset;
    u32 moduleAlign;
    u32 bssAlign;
    u32 commandOffset;
} __attribute__((packed)) rel_header_t;

typedef struct rel_section_entry_s {
    u32 offset;
    u32 size;
} __attribute__((packed)) rel_section_entry_t;

typedef struct rel_import_entry_s {
    u32 moduleId;
    u32 offset;
} __attribute__((packed)) rel_import_entry_t;

typedef struct rel_link_s {
    u16 prevOffset;
    u8 type;
    u8 section;
    u32 value;
} __attribute__((packed)) rel_link_t;

typedef enum rel_link_type_e {
    REL_LINK_NOP = 0x0,
    REL_LINK_WRITE_WORD = 0x1,
    REL_LINK_SET_BRANCH_OFFSET = 0x2,
    REL_LINK_WRITE_LOWER_HALF1 = 0x3,
    REL_LINK_WRITE_LOWER_HALF2 = 0x4,
    REL_LINK_WRITE_UPPER_HALF = 0x5,
    REL_LINK_WRITE_UPPER_HALF_AND_BIT1 = 0x6,
    REL_LINK_SET_BRANCH_COND_OFFSET1 = 0x7,
    REL_LINK_SET_BRANCH_COND_OFFSET2 = 0x8,
    REL_LINK_SET_BRANCH_COND_OFFSET3 = 0x9,
    REL_LINK_SET_BRANCH_DEST = 0xA,
    REL_LINK_SET_BRANCH_COND_DEST1 = 0xB,
    REL_LINK_SET_BRANCH_COND_DEST2 = 0xC,
    REL_LINK_SET_BRANCH_COND_DEST3 = 0xD,
    REL_LINK_INCREMENT_OFFSET = 0xC9,
    REL_LINK_SECTION = 0xCA,
    REL_LINK_END = 0xCB,
    REL_LINK_MRK_REF = 0xCC
} rel_link_type_t;

#endif // WSZST_LIB_REL_H
