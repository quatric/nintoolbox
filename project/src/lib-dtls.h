#ifndef LIB_DTLS_H
#define LIB_DTLS_H

#include "lib-nintendo.h"

enumError ScanDTLS (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *ls_data, uint ls_size,
	const u8 *dt_data, uint dt_size);

enumError CreateDTLS (
	u8 **out_ls, uint *out_ls_size, u8 **out_dt, uint *out_dt_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, bool compress, bool big_endian);

#endif
