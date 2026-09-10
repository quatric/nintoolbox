#ifndef LIB_KENSAKU_H
#define LIB_KENSAKU_H

#include "lib-nintendo.h"

// And-Kensaku ".rz" archives (Nintendo DS "Kanji Sonomama Rakubiki Jiten" /
// "Rakubiki Jiten Kanji" family). The file is a whole-stream Nintendo LZ77
// (type 0x10 or 0x11) blob; the decompressed payload is a CyberConnect2
// "Pres" archive with a fixed 0x80-byte header, a table of 0x20-byte entries,
// and a trailing string pool. Ported from Luigi Auriemma's and_kensaku.bms.

typedef struct kensaku_entry_t
{
	ccp name; // full "folder/name.ext" relative path (points into kensaku_t::names)
	u32 offset; // start of the member inside kensaku_t::blob
	u32 size;
} kensaku_entry_t;

typedef struct kensaku_t
{
	u8 *blob; // decompressed "Pres" archive
	uint blob_size;
	kensaku_entry_t *entries;
	uint n_entries;
	char *names; // backing store for entry names
	uint compression; // 0x10 or 0x11, the LZ77 sub-type the .rz used
} kensaku_t;

void ResetKensaku (kensaku_t *k);
enumError ScanKensakuRZ (kensaku_t *k, const u8 *data, uint size);

#endif
