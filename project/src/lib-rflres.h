#ifndef LIB_RFLRES_H
#define LIB_RFLRES_H

#include "lib-nintendo.h"

// Scan any Mii Face Library resource archive (RFL_Res, FFL_Res, CFL_Res, AFL_Res)
// Auto-detects Big Endian (Wii RFL, Wii U FFL) and Little Endian (3DS CFL, Switch FFL/AFL).
enumError ScanMiiRes (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError ScanRFLRes (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError ScanFFLRes (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError ScanCFLRes (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// Build Mii Face Library resource archive
enumError CreateMiiRes (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool big_endian);
enumError CreateRFLRes (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateFFLRes (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateCFLRes (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateAFLRes (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif
