// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// imageEpoch "RTDP" flat archive (.vol) and "WTMD" texture (.wtm), plus the
// Nintendo-standard LZ10 compression wrapper (.cxd) both are found behind
// on the Arc Rise Fantasia (Wii) disc. See lib-rtdp.c for the exact byte
// layout, confirmed against hundreds of real sample files.
//-----------------------------------------------------------------------------
#ifndef LIB_RTDP_H
#define LIB_RTDP_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// RTDP: magic "RTDP", { u32 BE header_size, u32 BE file_count,
// u32 BE archive_total_size }, 16 reserved bytes, then file_count * 40-byte
// records: char name[32] (NUL-padded ASCII), u32 BE size, u32 BE rel_offset
// (relative to header_size, 32-byte aligned between members). Confirmed
// byte-exact (every offset/size, and archive_total_size == real file size)
// across 40+ real .vol samples.
typedef struct rtdp_entry_t
{
	char name[33];
	u32 size;
	u32 offset;		// absolute offset into the archive buffer
} rtdp_entry_t;

typedef struct rtdp_t
{
	const u8 *raw;
	size_t raw_size;
	u32 header_size;
	uint n_entries;
	rtdp_entry_t *entries;
} rtdp_t;

bool IsRTDP (const u8 *data, size_t size);
enumError ScanRTDP (rtdp_t *rtdp, const u8 *data, size_t size);
void ResetRTDP (rtdp_t *rtdp);
enumError ExtractRTDPArchive (ccp arg, ccp basedir, uint depth);

//-----------------------------------------------------------------------------
// WTMD texture: magic "WTMD", u32 BE header_size (always 0x20 in every
// sample seen), u16 BE width, u16 BE height, u8 format_code, then further
// format/mipmap/palette fields whose exact meaning is NOT confirmed (only
// worked out indirectly by matching (width*height*bpp/8 [+palette]) against
// the real file size for a handful of format codes -- see lib-rtdp.c). No
// pixel decoder is implemented; this only exposes the confirmed header
// fields (dimensions, format code, raw payload) for metadata dumping.
typedef struct wtmd_header_t
{
	u32 header_size;
	u16 width, height;
	u8 format_code;
	const u8 *payload;	// data past the 32-byte header
	size_t payload_size;
} wtmd_header_t;

bool IsWTMD (const u8 *data, size_t size);
enumError ScanWTMDHeader (wtmd_header_t *hd, const u8 *data, size_t size);

//-----------------------------------------------------------------------------
// .cxd: a 4-byte Nintendo-standard "LZ10" header (u8 type == 0x10, u24 LE
// decompressed_size) immediately followed by LZSS-compressed data, wrapping
// either an RTDP archive or a single WTMD texture. Confirmed byte-exact
// (decompressed size matches exactly, and the decompressed payload's own
// magic + declared size are self-consistent) on 15 real .cxd samples of
// both kinds.
bool IsCXD (const u8 *data, size_t size);
enumError DecompressCXD (const u8 *data, size_t size, u8 **out, size_t *out_size);
enumError ExtractCXDFile (ccp arg, ccp basedir, uint depth);

#endif // LIB_RTDP_H
