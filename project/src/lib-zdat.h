// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_ZDAT_H
#define LIB_ZDAT_H 1

#include "lib-nintendo.h"

// Animal Crossing: Pocket Camp .zdat asset container.
//
// Every payload is a Unity bundle XORed with one repeated byte per entry;
// that key (0 meaning "kept as found") is remembered for byte-exact repacking
// in a small text file inside the extracted tree.
#define ZDAT_CACHE_FILE ".zdat-cache.txt"

enumError ExtractZDATArchive (ccp arg, ccp basedir, uint depth);

// Rebuild a ZDAT archive. mask_keys[i] holds the repeated XOR byte for entry
// i (NULL means all-zero keys, i.e. re-emit the payload bytes as-is). The
// header/offset words of the result are byte-identical to the extractor's
// input when the entries and keys came from a CREATE or from a saved cache.
enumError CreateZDATArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, const u8 *mask_keys);

enumError create_zdat_dir (ccp source, ccp dest);

#endif // LIB_ZDAT_H
