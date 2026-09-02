#ifndef LIB_SA01_H
#define LIB_SA01_H

#include "lib-nintendo.h"

enumError ScanSA01 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError DecodeSA01Container (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError CreateSA01 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries,
	bool compress, bool big_endian);
enumError CreateCA01 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries,
	bool compress, bool big_endian);

#endif
