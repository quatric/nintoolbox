// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_PTLG_H
#define LIB_PTLG_H 1

#include "lib-nintendo.h"

// Next Level Games PTLG texture container (.glt / .rlt)
enumError ExtractPTLGArchive (ccp arg, ccp basedir, uint depth);
enumError DecodePTLGToPNGDir (const u8 *data, uint size, ccp dest_dir, uint *n_written);
enumError CreatePTLGArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool is_gc);

#endif // LIB_PTLG_H
