#ifndef LIB_CTPK_H
#define LIB_CTPK_H

#include "lib-nintendo.h"

typedef struct nintendo_ctpk_entry_t
{
	char name[PATH_MAX];
	uint width;
	uint height;
	uint format;
	uint mip_level;
	uint type;
	const u8 *data;
	uint data_size;
} nintendo_ctpk_entry_t;

typedef struct nintendo_ctpk_t
{
	const u8 *data;
	uint size;
	uint version;
	uint n_entries;
	uint texture_offset;
	uint texture_size;
} nintendo_ctpk_t;

enumError ScanCTPK (nintendo_ctpk_t *ctpk, const u8 *data, uint size);
enumError GetCTPKEntry (const nintendo_ctpk_t *ctpk, uint index, nintendo_ctpk_entry_t *entry);
enumError DecodeCTPKTexture_RGBA (
	u8 **dest, uint *width, uint *height, const nintendo_ctpk_entry_t *entry);
enumError DecodePicaTexture (u8 **dest, uint *width, uint *height, const u8 *src, uint w, uint h,
	uint format, uint src_size);
enumError EncodeCTPK (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name);
enumError CreateCTPK (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

// Rebuild a CTPK byte-for-byte from a preserved prefix (everything up to
// texture_offset: header, entry table, and whatever undocumented bytes and
// string table sit between them, captured verbatim by the extractor rather
// than re-derived) plus each entry's own untouched raw pixel payload. See
// lib-ctpk.c for why this sidesteps needing to understand those fields at
// all: byte-for-byte fidelity only requires that neither half changed.
enumError RebuildCTPKFromPrefix (u8 **dest, uint *dest_size, const u8 *prefix, uint prefix_size,
	const nintendo_sarc_entry_t *payload_entries, uint n_payload_entries);

#endif
