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

#endif
