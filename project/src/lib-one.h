// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_ONE_H
#define LIB_ONE_H 1

#include "lib-nintendo.h"

// Sonic Storybook ONE (Sonic and the Secret Rings / Sonic and the Black
// Knight) big-endian archive creator.
enumError CreateONEArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, u32 marker);

// PRS stream compressor. Writers a greedy-LZ1 stream in the Sonic Team PRS
// bit format that the tool's own decoder (decode_storybook_prs) accepts.
enumError EncodeStorybookPRS (u8 **dest, uint *dest_size, const u8 *data, uint size);

enumError create_one_dir (ccp source, ccp dest);

#endif // LIB_ONE_H