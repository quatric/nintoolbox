#define _GNU_SOURCE 1

#include "lib-rvz.h"
#include "lib-zstd.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
///////////////			WIA/RVZ container			///////////////
///////////////////////////////////////////////////////////////////////////////

// Real spec fetched from dolphin-emu's own docs/WiaAndRvz.md and confirmed
// field-by-field against a real retail GameCube .rvz (Pikmin, USA): magic,
// disc_type, n_part/n_raw_data/n_groups, and the compressed raw_data_t/
// group_t arrays all matched exactly, and the reconstructed disc's boot.bin
// (DOL offset 0x18000, FST offset 0x30d700, FST size 0x151e5) decoded to a
// real, sane GameCube file system (3501 files) once the chunk-placement fix
// below was found. GameCube only (disc_type==1, n_part==0) -- Wii's
// partition hash-removal reconstruction is a distinct, larger feature not
// implemented here.
//
// One non-obvious fix found only by testing against real data, not by
// reading the spec: group N's decoded bytes always go at absolute disc
// offset N*chunk_size, counting from disc offset 0 -- NOT from the raw_data
// entry's own raw_data_off. raw_data_off (128 for a typical GC disc, right
// after the 128-byte dhead prefix stored directly in the WIA header) is
// where the raw_data's *content* conceptually starts, but the chunk grid
// itself is disc-absolute; the first chunk still encodes all 131072 bytes
// from disc offset 0, redundantly duplicating the header's own dhead for
// its first 128 bytes. Placing group 0's decoded bytes at raw_data_off
// instead of 0 shifts every following byte, and silently zeroes out
// boot.bin's DOL/FST offset fields (they land past the shifted chunk's
// real content) -- the disc's title/game-ID text still decodes right
// (that part comes straight from the header's own dhead copy, not from any
// group), so this corruption is easy to miss without actually parsing the
// file system afterward.

bool IsRVZ (cvp data, uint size)
{
	if (!data || size < 4)
		return false;
	const u8 *d = data;
	return !memcmp (d, "RVZ\1", 4) || !memcmp (d, "WIA\1", 4);
}

///////////////////////////////////////////////////////////////////////////////
///////////////		RVZ junk-data PRNG (packed groups)		///////////////
///////////////////////////////////////////////////////////////////////////////

// RVZ replaces long runs of Nintendo's own disc-padding "junk" data with a
// short seed rather than storing (and compressing) it -- a Lagged
// Fibonacci generator, f=xor, j=32, k=521. Reconstructs that padding back
// from the 68-byte (17 32-bit word) seed on decode.
typedef struct rvz_junk_t
{
	u32 buf[521];
	uint pos;
} rvz_junk_t;

static void rvz_junk_advance (rvz_junk_t *g)
{
	for (uint i = 0; i < 32; i++)
		g->buf[i] ^= g->buf[i + 521 - 32];
	for (uint i = 32; i < 521; i++)
		g->buf[i] ^= g->buf[i - 32];
}

static void rvz_junk_init (rvz_junk_t *g, const u8 *seed68)
{
	for (uint i = 0; i < 17; i++)
		g->buf[i] = be32 (seed68 + i * 4);
	for (uint i = 17; i < 521; i++)
		g->buf[i] = (g->buf[i - 17] << 23) ^ (g->buf[i - 16] >> 9) ^ g->buf[i - 1];
	for (uint k = 0; k < 4; k++)
		rvz_junk_advance (g);
	g->pos = 0;
}

static void rvz_junk_fill (rvz_junk_t *g, u8 *dest, uint n)
{
	uint written = 0;
	while (written < n)
	{
		if (g->pos == 521)
		{
			rvz_junk_advance (g);
			g->pos = 0;
		}
		const u32 w = g->buf[g->pos++];
		const u8 wb[4] = { (u8)(w >> 24), (u8)(w >> 16), (u8)(w >> 8), (u8)w };
		uint take = n - written;
		if (take > 4)
			take = 4;
		memcpy (dest + written, wb, take);
		written += take;
	}
}

// Expands an RVZ "packed" stream (segments of either literal bytes or a
// junk-generator seed, each prefixed by a big-endian size/flag word) to
// exactly WANT bytes.
static void rvz_unpack (const u8 *src, u8 *dest, uint want)
{
	uint si = 0, di = 0;
	while (di < want)
	{
		u32 size = be32 (src + si);
		si += 4;
		const bool junk = (size & 0x80000000) != 0;
		size &= 0x7fffffff;
		if (junk)
		{
			rvz_junk_t g;
			rvz_junk_init (&g, src + si);
			si += 68;
			rvz_junk_fill (&g, dest + di, size);
		}
		else
		{
			memcpy (dest + di, src + si, size);
			si += size;
		}
		di += size;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////			decode to raw ISO			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError DecodeRVZFile (ccp src_path, ccp dest_path)
{
	FILE *f = fopen (src_path, "rb");
	if (!f)
		return ERROR0 (ERR_CANT_OPEN, "Can't open: %s", src_path);

	u8 head[0x48];
	if (fread (head, 1, sizeof (head), f) != sizeof (head))
	{
		fclose (f);
		return ERROR0 (ERR_READ_FAILED, "Short read: %s", src_path);
	}
	if (!IsRVZ (head, sizeof (head)))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}

	const u64 iso_file_size = be64 (head + 0x24);

	u8 disc[0xDC];
	if (fread (disc, 1, sizeof (disc), f) != sizeof (disc))
	{
		fclose (f);
		return ERROR0 (ERR_READ_FAILED, "Short read (disc_t): %s", src_path);
	}

	const u32 disc_type = be32 (disc + 0x00);
	const u32 compression = be32 (disc + 0x04);
	const u32 chunk_size = be32 (disc + 0x0C);
	const u8 *dhead = disc + 0x10; // 0x80 bytes: first 128 bytes of the disc
	const u32 n_part = be32 (disc + 0x90);
	const u32 n_raw_data = be32 (disc + 0xB4);
	const u64 raw_data_off = be64 (disc + 0xB8);
	const u32 raw_data_size = be32 (disc + 0xC0);
	const u32 n_groups = be32 (disc + 0xC4);
	const u64 group_off = be64 (disc + 0xC8);
	const u32 group_size = be32 (disc + 0xD0);

	if (disc_type != 1 || n_part != 0)
	{
		fclose (f);
		return ERROR0 (ERR_NOT_IMPLEMENTED,
			"RVZ/WIA Wii disc images (partition hash reconstruction) are not"
			" supported yet, only plain GameCube: %s",
			src_path);
	}
	if (compression != 5)
	{
		fclose (f);
		return ERROR0 (ERR_NOT_IMPLEMENTED,
			"RVZ/WIA compression method %u is not supported (only Zstandard/5): %s", compression,
			src_path);
	}
	if (!chunk_size || !n_groups)
	{
		fclose (f);
		return ERROR0 (ERR_INVALID_DATA, "Malformed RVZ/WIA disc_t: %s", src_path);
	}

	u8 *raw_data_comp = MALLOC (raw_data_size ? raw_data_size : 1);
	if (fseeko (f, (off_t)raw_data_off, SEEK_SET)
		|| fread (raw_data_comp, 1, raw_data_size, f) != raw_data_size)
	{
		FREE (raw_data_comp);
		fclose (f);
		return ERROR0 (ERR_READ_FAILED, "Short read (raw_data_t): %s", src_path);
	}
	u8 *raw_data_t_buf = 0;
	uint raw_data_t_written = 0;
	enumError err = DecodeZSTD (&raw_data_t_buf, &raw_data_t_written, raw_data_comp, raw_data_size);
	FREE (raw_data_comp);
	if (err > ERR_WARNING || !raw_data_t_buf || raw_data_t_written != (uint64_t)n_raw_data * 24)
	{
		FREE (raw_data_t_buf);
		fclose (f);
		return ERROR0 (ERR_INVALID_DATA, "Failed to decompress raw_data_t: %s", src_path);
	}

	u8 *group_comp = MALLOC (group_size ? group_size : 1);
	if (fseeko (f, (off_t)group_off, SEEK_SET) || fread (group_comp, 1, group_size, f) != group_size)
	{
		FREE (raw_data_t_buf);
		FREE (group_comp);
		fclose (f);
		return ERROR0 (ERR_READ_FAILED, "Short read (group_t): %s", src_path);
	}
	u8 *group_t_buf = 0;
	uint group_t_written = 0;
	err = DecodeZSTD (&group_t_buf, &group_t_written, group_comp, group_size);
	FREE (group_comp);
	if (err > ERR_WARNING || !group_t_buf || group_t_written != (uint64_t)n_groups * 12)
	{
		FREE (raw_data_t_buf);
		FREE (group_t_buf);
		fclose (f);
		return ERROR0 (ERR_INVALID_DATA, "Failed to decompress group_t: %s", src_path);
	}

	FILE *out = fopen (dest_path, "wb");
	if (!out)
	{
		FREE (raw_data_t_buf);
		FREE (group_t_buf);
		fclose (f);
		return ERROR0 (ERR_CANT_CREATE, "Can't create: %s", dest_path);
	}
	if (ftruncate (fileno (out), (off_t)iso_file_size))
	{
		// non-fatal: only pre-sizes the file for sparse all-zero groups,
		// every byte still gets written explicitly below regardless.
	}

	u8 *chunk_buf = MALLOC (chunk_size);
	u8 *comp_buf = 0;
	uint comp_buf_size = 0;
	u8 *unpack_buf = 0; // DecodeZSTD() owns/reallocates this each call

	err = ERR_OK;
	for (u32 ri = 0; ri < n_raw_data && err <= ERR_WARNING; ri++)
	{
		const u8 *rd = raw_data_t_buf + (size_t)ri * 24;
		const u32 g_index = be32 (rd + 16);
		const u32 r_ngroups = be32 (rd + 20);

		for (u32 k = 0; k < r_ngroups; k++)
		{
			const u32 gi = g_index + k;
			if (gi >= n_groups)
			{
				err = ERROR0 (ERR_INVALID_DATA, "group index out of range: %s", src_path);
				break;
			}
			const u8 *gt = group_t_buf + (size_t)gi * 12;
			const u64 data_off = (u64)be32 (gt + 0) * 4;
			const u32 data_size_raw = be32 (gt + 4);
			const u32 packed = be32 (gt + 8);
			const bool compressed = (data_size_raw & 0x80000000) != 0;
			const u32 size = data_size_raw & 0x7fffffff;

			const u64 abs_off = (u64)gi * chunk_size;
			if (abs_off >= iso_file_size)
				continue;
			const u32 chunk_bytes
				= (u32)((iso_file_size - abs_off < chunk_size) ? iso_file_size - abs_off : chunk_size);

			if (size == 0)
				memset (chunk_buf, 0, chunk_bytes);
			else
			{
				if (size > comp_buf_size)
				{
					FREE (comp_buf);
					comp_buf = MALLOC (size);
					comp_buf_size = size;
				}
				if (fseeko (f, (off_t)data_off, SEEK_SET) || fread (comp_buf, 1, size, f) != size)
				{
					err = ERROR0 (ERR_READ_FAILED, "Short read (group %u): %s", gi, src_path);
					break;
				}

				if (compressed && packed)
				{
					FREE (unpack_buf);
					unpack_buf = 0;
					uint written = 0;
					enumError e2 = DecodeZSTD (&unpack_buf, &written, comp_buf, size);
					if (e2 > ERR_WARNING || written != packed)
					{
						err = ERROR0 (ERR_INVALID_DATA, "group %u decompress failed: %s", gi, src_path);
						break;
					}
					rvz_unpack (unpack_buf, chunk_buf, chunk_bytes);
				}
				else if (compressed)
				{
					uint written = 0;
					enumError e2 = DecodeZSTDpart (chunk_buf, chunk_bytes, &written, comp_buf, size);
					if (e2 > ERR_WARNING || written != chunk_bytes)
					{
						err = ERROR0 (ERR_INVALID_DATA, "group %u decompress failed: %s", gi, src_path);
						break;
					}
				}
				else if (packed)
					rvz_unpack (comp_buf, chunk_buf, chunk_bytes);
				else
					memcpy (chunk_buf, comp_buf, chunk_bytes < size ? chunk_bytes : size);
			}

			if (fseeko (out, (off_t)abs_off, SEEK_SET) || fwrite (chunk_buf, 1, chunk_bytes, out) != chunk_bytes)
			{
				err = ERROR0 (ERR_WRITE_FAILED, "Write failed at group %u: %s", gi, dest_path);
				break;
			}
		}
	}

	if (err <= ERR_WARNING)
	{
		// dhead is authoritative for the first 128 bytes (also redundantly
		// encoded as part of group 0's own decoded content above).
		if (fseeko (out, 0, SEEK_SET) || fwrite (dhead, 1, 0x80, out) != 0x80)
			err = ERROR0 (ERR_WRITE_FAILED, "Write failed (dhead): %s", dest_path);
	}

	FREE (chunk_buf);
	FREE (comp_buf);
	FREE (unpack_buf);
	FREE (raw_data_t_buf);
	FREE (group_t_buf);
	fclose (f);
	fclose (out);
	return err;
}
