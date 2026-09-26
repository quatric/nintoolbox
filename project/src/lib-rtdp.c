// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// imageEpoch RTDP archive, WTMD texture header, and the LZ10-compressed .cxd
// wrapper found on the Arc Rise Fantasia (Wii) disc; see lib-rtdp.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-rtdp.h"
#include "lib-archive-util.h"
#include <string.h>

#define RTDP_MAX_ENTRIES 0x10000

//-----------------------------------------------------------------------------
// RTDP

bool IsRTDP (const u8 *data, size_t size)
{
	if (!data || size < 0x20 || memcmp (data, "RTDP", 4))
		return false;
	const u32 header_size = rd_be32 (data + 4);
	const u32 total = rd_be32 (data + 12);
	return header_size >= 0x20 && header_size <= size && total == size;
}

enumError ScanRTDP (rtdp_t *rtdp, const u8 *data, size_t size)
{
	if (!rtdp || !IsRTDP (data, size))
		return ERR_INVALID_DATA;
	memset (rtdp, 0, sizeof (*rtdp));

	const u32 header_size = rd_be32 (data + 4);
	const u32 n = rd_be32 (data + 8);
	if (n > RTDP_MAX_ENTRIES || 0x20 + (u64)n * 40 > header_size)
		return ERR_INVALID_DATA;

	rtdp_entry_t *entries = CALLOC (n ? n : 1, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *rec = data + 0x20 + (u64)i * 40;
		memcpy (entries[i].name, rec, 32);
		entries[i].name[32] = 0;
		const u32 esize = rd_be32 (rec + 32);
		const u32 rel = rd_be32 (rec + 36);
		const u64 abs_off = (u64)header_size + rel;
		if (abs_off + esize > size)
		{
			FREE (entries);
			return ERR_INVALID_DATA;
		}
		entries[i].size = esize;
		entries[i].offset = (u32)abs_off;
	}

	rtdp->raw = data;
	rtdp->raw_size = size;
	rtdp->header_size = header_size;
	rtdp->n_entries = n;
	rtdp->entries = entries;
	return ERR_OK;
}

void ResetRTDP (rtdp_t *rtdp)
{
	if (!rtdp)
		return;
	FREE (rtdp->entries);
	memset (rtdp, 0, sizeof (*rtdp));
}

enumError ExtractRTDPArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".vol"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	rtdp_t rtdp;
	err = ScanRTDP (&rtdp, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RTDP:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, rtdp.n_entries, dest);

	(void)depth;
	for (uint i = 0; i < rtdp.n_entries; i++)
	{
		const rtdp_entry_t *e = rtdp.entries + i;
		char out_path[PATH_MAX];
		if (e->name[0] && OwnedNameOk (e->name))
			snprintf (out_path, sizeof (out_path), "%s/%04u_%s", dest, i, e->name);
		else
			snprintf (out_path, sizeof (out_path), "%s/%04u.bin", dest, i);

		if (!testmode)
			SaveFile (out_path, 0, 0, raw + e->offset, e->size, 0);
	}

	ResetRTDP (&rtdp);
	FREE (raw);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// WTMD

bool IsWTMD (const u8 *data, size_t size)
{
	if (!data || size < 0x20 || memcmp (data, "WTMD", 4))
		return false;
	const u32 header_size = rd_be32 (data + 4);
	return header_size == 0x20 && header_size <= size;
}

enumError ScanWTMDHeader (wtmd_header_t *hd, const u8 *data, size_t size)
{
	if (!hd || !IsWTMD (data, size))
		return ERR_INVALID_DATA;
	memset (hd, 0, sizeof (*hd));
	hd->header_size = rd_be32 (data + 4);
	hd->width = rd_be16 (data + 8);
	hd->height = rd_be16 (data + 10);
	hd->format_code = data[12];
	hd->payload = data + hd->header_size;
	hd->payload_size = size - hd->header_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// .cxd: Nintendo-standard "LZ10" compression (as used for GBA/DS/Wii
// overlays): u8 type (0x10), u24 LE decompressed_size, then LZSS-compressed
// data: repeating {u8 flag_bits (MSB first); per set bit, a 2-byte back
// reference (high nibble = length-3, low 12 bits = distance-1); per clear
// bit, one raw literal byte}. Confirmed byte-exact against 15 real .cxd
// samples: decompressed size always matches the header field exactly, and
// the decompressed payload always starts with a valid "RTDP" or "WTMD"
// magic whose own declared size matches the decompressed length.

bool IsCXD (const u8 *data, size_t size)
{
	if (!data || size < 5 || data[0] != 0x10)
		return false;
	const u32 dec_size = data[1] | data[2] << 8 | data[3] << 16;
	// Real-world sanity: seen ratios are well under 4x, and the format is
	// never used for tiny files. Reject the (2,3,4-byte) prefixes that
	// happen to start with 0x10 but aren't actually this container.
	return dec_size > size && dec_size < 64u * 1024 * 1024;
}

enumError DecompressCXD (const u8 *data, size_t size, u8 **out, size_t *out_size)
{
	if (!IsCXD (data, size) || !out || !out_size)
		return ERR_INVALID_DATA;

	const u32 dec_size = data[1] | data[2] << 8 | data[3] << 16;
	u8 *dst = MALLOC (dec_size ? dec_size : 1);
	if (!dst)
		return ERR_OUT_OF_MEMORY;

	size_t dp = 0, sp = 4;
	while (dp < dec_size)
	{
		if (sp >= size)
			goto bad;
		const u8 flags = data[sp++];
		for (uint bit = 0; bit < 8 && dp < dec_size; bit++)
		{
			if (flags & (0x80 >> bit))
			{
				if (sp + 1 >= size)
					goto bad;
				const u8 b0 = data[sp], b1 = data[sp + 1];
				sp += 2;
				const uint length = (b0 >> 4) + 3;
				const uint disp = ((b0 & 0xf) << 8 | b1) + 1;
				if (disp > dp)
					goto bad;
				for (uint i = 0; i < length && dp < dec_size; i++, dp++)
					dst[dp] = dst[dp - disp];
			}
			else
			{
				if (sp >= size)
					goto bad;
				dst[dp++] = data[sp++];
			}
		}
	}

	*out = dst;
	*out_size = dec_size;
	return ERR_OK;

bad:
	FREE (dst);
	return ERR_INVALID_DATA;
}

enumError ExtractCXDFile (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".cxd"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	u8 *dec = 0;
	size_t dec_size = 0;
	err = DecompressCXD (raw, raw_size, &dec, &dec_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	ccp sub_ext = ".bin";
	if (IsRTDP (dec, dec_size))
		sub_ext = ".vol";
	else if (IsWTMD (dec, dec_size))
		sub_ext = ".wtm";

	// Write the decompressed payload beside the source under its real
	// format's extension ("foo_vol.cxd" -> "foo_vol.vol"), then let that
	// extension's own extractor (RTDP) take it from there for archives; a
	// bare texture is just the decompressed file itself.
	(void)basedir;
	char base[PATH_MAX];
	StringCopyS (base, sizeof (base), arg);
	char *dot = strrchr (base, '.');
	char *slash = strrchr (base, '/');
	if (dot && (!slash || dot > slash))
		*dot = 0;
	char out_path[PATH_MAX];
	snprintf (out_path, sizeof (out_path), "%s%s", base, sub_ext);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECOMPRESS CXD:%s -> %s (%zu -> %zu bytes)\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, out_path, raw_size, dec_size);

	if (!testmode)
	{
		SaveFile (out_path, 0, 0, dec, dec_size, 0);
		if (!strcmp (sub_ext, ".vol"))
			ExtractRTDPArchive (out_path, 0, depth + 1);
	}

	FREE (dec);
	return ERR_OK;
}
