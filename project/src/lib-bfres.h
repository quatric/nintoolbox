#ifndef LIB_BFRES_H
#define LIB_BFRES_H

#include "lib-model-glb.h"
#include <stdint.h>
#include <stddef.h>

model_t *ParseBFRES (const uint8_t *data, size_t size);

// Switch flavour: little endian, version-major-gated header layout, a
// separate FRES-wide buffer pool (BufferInfo) index/vertex data lives in.
// See the comment above ParseBFRESSwitch() in lib-bfres.c.
model_t *ParseBFRESSwitch (const uint8_t *data, size_t size);

// Wii U BFRES 3.x archive resource census. The header carries a fixed table
// of 12 dictionary slots (offset at 0x20 + 4*slot, count at 0x50 + 2*slot)
// that register, per real Yoshi's Woolly World (v3.5.0.3) samples: slot 0
// FMDL, slot 1 FTEX, slot 2 FSKA (skeletal anim), slots 3/4/5 FSHU (shader
// param collections), slot 6 FTXP, slots 7/8 FVIS (visibility), slot 9 FSHA
// (shape anim), slot 10 FSCN (scene anim). Returns 1 when `data` is a valid
// big-endian v3.x FRES archive (and the census was written to `out`), else 0.
typedef struct {
	uint16_t slot;        // dictionary slot index (0..11)
	uint16_t count_meta;  // header count field (0x50 + 2*slot)
	uint16_t count_dict;  // ResDict node count (header at REL(0x20+4*slot))
	uint8_t magic[4];     // object magic of the slot's first entry
} bfres_slot_census_t;

typedef struct {
	char name[128];               // archive name from 0x14 (string table)
	uint32_t n_objects;           // total entries across used slots
	bfres_slot_census_t slots[12];
	uint8_t n_slots;
} bfres_archive_t;

int ParseBFRESArchive (const uint8_t *data, size_t size, bfres_archive_t *out);

#endif
