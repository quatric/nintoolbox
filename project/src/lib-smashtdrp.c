// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 DRP encrypted container (.drp).
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/DRP.cs"
// (MIT licensed; clean-room C port).

#include "lib-smashtdrp.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <string.h>
#include <zlib.h>

// Seeded xorshift generator behind DRP obfuscation (DRP.RandomXS).
typedef struct smashdrp_rand_t
{
	s32 d[4];
} smashdrp_rand_t;

static void smashdrp_rand_init (smashdrp_rand_t *r, s32 seed)
{
	const s32 init = 0x41C64E6D;
	r->d[0] = seed * init + 0x3039;
	r->d[1] = r->d[0] * init + 0x3039;
	r->d[2] = r->d[1] * init + 0x3039;
	r->d[3] = r->d[2] * init + 0x3039;
}

static s32 smashdrp_rand_next (smashdrp_rand_t *r)
{
	const s32 last = r->d[3], first = r->d[0];
	const s32 third = r->d[2], second = r->d[1];
	const s32 xor1 = last ^ (last << 11);
	const s32 xor2 = first ^ (first >> 19);
	const s32 xor3 = xor1 ^ (xor1 >> 8);
	const s32 fin = xor2 ^ xor3;
	r->d[1] = first;
	r->d[3] = third;
	r->d[2] = second;
	r->d[0] = fin;
	return fin & 0x7FFFFFFF;
}

static u32 smashdrp_swap32 (u32 v)
{
	return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

// Decrypt `size` bytes; returns a fresh buffer (caller frees) or 0.
// Faithful port of DRP.Decrypt, including its word-at-a-time chaining.
static u8 *smashdrp_decrypt (const u8 *data, size_t size)
{
	if (!data || !size || (size & 3) || size < 0x20)
		return 0;
	const size_t words = size / 4;
	s32 *out = CALLOC (words, sizeof (*out));
	if (!out)
		return 0;

	const s32 seed = (s32)rd_be32 (data + 0x1C);
	smashdrp_rand_t rng;
	smashdrp_rand_init (&rng, seed);

	size_t wi = 0;
	u32 xorval = 0;
	if ((size % 8) == 0 && words)
	{
		const size_t blocks = words >> 3;
		for (size_t i = 0; i < blocks; i++)
			for (int x = 0; x < 8; x++, wi++)
			{
				const s32 ri = smashdrp_rand_next (&rng);
				const u32 w = rd_be32 (data + wi * 4);
				const u32 val = (w ^ (u32)ri) ^ xorval;
				xorval = ((u32)ri << 13) & 0x80000000u;
				out[wi] = (s32)smashdrp_swap32 (val);
			}
		for (; wi < words; wi++)
		{
			const s32 ri = smashdrp_rand_next (&rng);
			const u32 w = rd_be32 (data + wi * 4);
			const u32 val = (w ^ (u32)ri) ^ xorval;
			xorval = ((u32)ri << 13) & 0x80000000u;
			out[wi] = (s32)smashdrp_swap32 (val);
		}
		out[7] = (s32)smashdrp_swap32 ((u32)seed);
	}
	else
	{
		FREE (out);
		return 0;
	}
	return (u8 *)out;
}

// Table record: 0x40-byte name, s32 unk, s32 nextFile, u16 c2, u16 c1,
// 4 pad bytes, 4 s32 part sizes. Part j holds a 4-byte decompressed
// size followed by (partsize[j]-4) raw zlib bytes.
#define SMASHDRP_REC_SIZE (0x40 + 4 + 4 + 2 + 2 + 4 + 16)

static bool smashdrp_name_ok (const u8 *p)
{
	if (!memchr (p, 0, 0x40))
		return false;
	for (int i = 0; p[i]; i++)
		if (p[i] < 0x20 || p[i] >= 0x7F)
			return false;
	return p[0] != 0;
}

bool IsSmashDRP (const u8 *data, size_t size, ccp name)
{
	if (!name || !is_ext_match (name, ".drp"))
		return false;
	u8 *dec = smashdrp_decrypt (data, size);
	if (!dec)
		return false;
	bool ok = false;
	if (size >= 0x62)
	{
		const u32 count = rd_be16 (dec + 0x16);
		if (count && count < 10000 && 0x60 + (size_t)count * SMASHDRP_REC_SIZE <= size
			&& smashdrp_name_ok (dec + 0x60))
			ok = true;
	}
	FREE (dec);
	return ok;
}

enumError ExtractSmashDRPArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext_match (arg, ".drp"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	u8 *dec = smashdrp_decrypt (raw, raw_size);
	FREE (raw);
	if (!dec || raw_size < 0x62)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	const u32 count = rd_be16 (dec + 0x16);
	if (!count || count >= 10000
		|| 0x60 + (size_t)count * SMASHDRP_REC_SIZE > raw_size
		|| !smashdrp_name_ok (dec + 0x60))
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DRP:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, count, dest);

	// locate each record's part data: records are fixed-size, part
	// payloads follow the whole table
	size_t table_end = 0x60 + (size_t)count * SMASHDRP_REC_SIZE;
	size_t *data_off = CALLOC (count, sizeof (*data_off));
	if (!data_off)
	{
		FREE (dec);
		return ERR_CANT_CREATE;
	}
	size_t cursor = table_end;
	bool layout_ok = true;
	for (u32 i = 0; i < count; i++)
	{
		const u8 *rec = dec + 0x60 + (size_t)i * SMASHDRP_REC_SIZE;
		const u32 c1 = rd_be16 (rec + 0x40 + 8);
		data_off[i] = cursor;
		for (u32 j = 0; j < c1; j++)
		{
			const s32 psz = (s32)rd_be32 (rec + 0x40 + 16 + (size_t)j * 4);
			if (psz < 4 || cursor + (size_t)psz > raw_size)
			{
				layout_ok = false;
				break;
			}
			cursor += (size_t)psz;
		}
		if (!layout_ok)
			break;
	}
	if (!layout_ok)
	{
		FREE (data_off);
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	if (!testmode)
	{
		char man_path[PATH_MAX];
		snprintf (man_path, sizeof (man_path), "%s/Drp.txt", dest);
		FILE *man = fopen (man_path, "w");
		if (man)
		{
			fprintf (man, "#DRP\n# Super Smash Bros. 4 encrypted container\n\nfiles = %u\n\n[members]\n", count);
			for (u32 i = 0; i < count; i++)
				fprintf (man, "%s\n", dec + 0x60 + (size_t)i * SMASHDRP_REC_SIZE);
			fclose (man);
		}
	}

	enumError err = ERR_OK;
	for (u32 i = 0; !testmode && !err && i < count; i++)
	{
		const u8 *rec = dec + 0x60 + (size_t)i * SMASHDRP_REC_SIZE;
		const u32 c1 = rd_be16 (rec + 0x40 + 8);
		size_t part_pos = data_off[i];
		for (u32 j = 0; j < c1 && !err; j++)
		{
			const s32 psz = (s32)rd_be32 (rec + 0x40 + 16 + (size_t)j * 4);
			const u32 expect = rd_be32 (dec + part_pos);
			const u8 *comp = dec + part_pos + 4;
			const size_t comp_len = (size_t)psz - 4;
			part_pos += (size_t)psz;

			u8 *flat = 0;
			size_t flat_len = 0;
			if (expect && expect < 0x1000000)
			{
				flat = MALLOC (expect ? expect : 1);
				if (flat)
				{
					uLongf dl = expect;
					if (uncompress (flat, &dl, comp, comp_len) != Z_OK || dl != expect)
					{
						FREE (flat);
						flat = 0;
					}
					else
						flat_len = dl;
				}
			}

			char mag[5] = { 0 };
			if (flat && flat_len >= 4)
				memcpy (mag, flat, 4);
			bool printable = mag[0] >= 0x20 && mag[0] < 0x7F
				&& mag[1] >= 0x20 && mag[1] < 0x7F
				&& mag[2] >= 0x20 && mag[2] < 0x7F
				&& mag[3] >= 0x20 && mag[3] < 0x7F;

			char out_path[PATH_MAX];
			if (c1 > 1)
				snprintf (out_path, sizeof (out_path), "%s/%s.part%d.%.4s",
					dest, (const char *)rec, j, printable ? mag : "bin");
			else
				snprintf (out_path, sizeof (out_path), "%s/%s.%.4s",
					dest, (const char *)rec, printable ? mag : "bin");
			if (flat && flat_len)
				SaveFile (out_path, 0, 0, flat, flat_len, 0);
			else if (comp_len)
				SaveFile (out_path, 0, 0, comp, comp_len, 0);
			FREE (flat);
		}
	}

	FREE (data_off);
	FREE (dec);
	return err;
}
