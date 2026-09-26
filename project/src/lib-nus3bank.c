// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 sound bank (.nus3bank, "NUS3" + "BANKTOC").
//
// Reference: KillzXGaming/Smash-Forge,
// "Smash Forge/Filetypes/Sounds/NUS3BANK.cs"
// (MIT licensed; clean-room C port of the binary layout).

#include "lib-nus3bank.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <string.h>

typedef struct nus3bank_sec_t
{
	char magic[4];
	u32 size; // payload size (excludes the 8-byte section header)
	size_t payload; // file offset of the payload
} nus3bank_sec_t;

typedef struct nus3bank_t
{
	const u8 *data;
	size_t size;
	nus3bank_sec_t prop, binf, grp, dton, tone, pack;
	bool has_prop, has_binf, has_grp, has_dton, has_tone, has_pack;
} nus3bank_t;

// Parse and validate the bank; fills section descriptors.
static bool nus3bank_parse (const u8 *data, size_t size, nus3bank_t *b)
{
	memset (b, 0, sizeof (*b));
	b->data = data;
	b->size = size;
	if (!data || size < 0x20 || memcmp (data, "NUS3", 4))
		return false;
	if (memcmp (data + 8, "BANKTOC ", 8))
		return false;
	const u32 toc_size = rd_le32 (data + 16);
	const u32 nsec = rd_le32 (data + 20);
	if (!nsec || nsec > 64 || 0x18 + (size_t)nsec * 8 > size)
		return false;

	size_t payload = 0x14 + toc_size;
	for (u32 i = 0; i < nsec; i++)
	{
		const u8 *e = data + 0x18 + (size_t)i * 8;
		const u32 sz = rd_le32 (e + 4);
		if (payload + 8 + sz > size)
			return false;
		nus3bank_sec_t *dst = 0;
		bool *has = 0;
		if (!memcmp (e, "PROP", 4))
		{
			dst = &b->prop;
			has = &b->has_prop;
		}
		else if (!memcmp (e, "BINF", 4))
		{
			dst = &b->binf;
			has = &b->has_binf;
		}
		else if (!memcmp (e, "GRP ", 4))
		{
			dst = &b->grp;
			has = &b->has_grp;
		}
		else if (!memcmp (e, "DTON", 4))
		{
			dst = &b->dton;
			has = &b->has_dton;
		}
		else if (!memcmp (e, "TONE", 4))
		{
			dst = &b->tone;
			has = &b->has_tone;
		}
		else if (!memcmp (e, "PACK", 4))
		{
			dst = &b->pack;
			has = &b->has_pack;
		}
		else if (!memcmp (e, "JUNK", 4))
		{
			payload += 8 + sz;
			continue;
		}
		else
			return false; // unknown section: refuse to guess
		if (*has)
			return false; // duplicate section
		memcpy (dst->magic, e, 4);
		dst->size = sz;
		dst->payload = payload + 8;
		*has = true;
		payload += 8 + sz;
	}
	// TONE is the bank's raison d'etre; PROP/BINF/GRP/DTON come with it
	return b->has_tone && b->has_prop && b->has_binf && b->has_grp && b->has_dton;
}

bool IsNUS3Bank (const u8 *data, size_t size)
{
	nus3bank_t b;
	return nus3bank_parse (data, size, &b);
}

// --- section readers (bounds-checked; used by both text + extract) ---

static bool nus3bank_prop (nus3bank_t *b, char *proj, size_t projsz, char *ts, size_t tssz)
{
	const u8 *d = b->data;
	// exact mirror of the reference reader (note the irregular skips:
	// 6 bytes after the project name, 4 after the timestamp)
	size_t pos = b->prop.payload + 4 + 4 + 2 + 2;
	if (pos + 1 > b->size)
		return false;
	u32 s = d[pos];
	if (!s || pos + s + 6 + 2 > b->size)
		return false;
	size_t n = s - 1;
	if (n >= projsz)
		n = projsz - 1;
	memcpy (proj, d + pos + 1, n);
	proj[n] = 0;
	pos += s + 6 + 2;
	pos = (pos + 3) & ~(size_t)3;
	if (pos + 1 > b->size)
		return false;
	s = d[pos];
	if (!s || pos + s + 4 > b->size)
		return false;
	n = s - 1;
	if (n >= tssz)
		n = tssz - 1;
	memcpy (ts, d + pos + 1, n);
	ts[n] = 0;
	return true;
}

static bool nus3bank_binf (nus3bank_t *b, char *name, size_t namesz, u32 *flag)
{
	const u8 *d = b->data;
	size_t pos = b->binf.payload + 4 + 4; // zero pad + unk1
	if (pos + 1 > b->size)
		return false;
	const u32 s = d[pos];
	if (!s || pos + s > b->size)
		return false;
	size_t n = s - 1;
	if (n >= namesz)
		n = namesz - 1;
	memcpy (name, d + pos + 1, n);
	name[n] = 0;
	pos = (pos + s + 3) & ~(size_t)3;
	if (pos + 4 > b->size)
		return false;
	*flag = rd_le32 (d + pos);
	return true;
}

// Group/tone entries open with a reserved s32, then a length-prefixed
// name (u8 length including NUL, chars, align-4). Returns the name and
// the position just past it.
static bool nus3bank_entry_name (
	const u8 *data, size_t size, size_t pos, char *dst, size_t dstsz, size_t *after)
{
	if (pos + 5 > size)
		return false;
	pos += 4; // reserved
	const u32 s = data[pos];
	if (!s || pos + s > size)
		return false;
	size_t n = s - 1;
	if (n >= dstsz)
		n = dstsz - 1;
	memcpy (dst, data + pos + 1, n);
	dst[n] = 0;
	pos = (pos + s + 3) & ~(size_t)3;
	if (pos > size)
		return false;
	*after = pos;
	return true;
}

static u32 nus3bank_table_count (nus3bank_t *b, const nus3bank_sec_t *sec)
{
	if (sec->payload + 4 > b->size)
		return UINT32_MAX;
	return rd_le32 (b->data + sec->payload);
}

enumError DecodeNUS3Bank_Text (FILE *out, const u8 *data, size_t size)
{
	nus3bank_t b;
	if (!out || !nus3bank_parse (data, size, &b))
		return ERR_INVALID_DATA;

	char proj[256] = { 0 }, ts[256] = { 0 };
	char bank[256] = { 0 };
	u32 flag = 0;
	if (!nus3bank_prop (&b, proj, sizeof (proj), ts, sizeof (ts))
		|| !nus3bank_binf (&b, bank, sizeof (bank), &flag))
		return ERR_INVALID_DATA;

	const u32 ngrp = nus3bank_table_count (&b, &b.grp);
	const u32 ndes = nus3bank_table_count (&b, &b.dton);
	const u32 ntone = nus3bank_table_count (&b, &b.tone);
	if (ngrp == UINT32_MAX || ndes == UINT32_MAX || ntone == UINT32_MAX || ngrp > 100000
		|| ndes > 100000 || ntone > 100000)
		return ERR_INVALID_DATA;

	fprintf (out,
		"#NUS3BANK\n# Super Smash Bros. 4 sound bank\n\n"
		"project = %s\ntimestamp = %s\nbank = %s\nbank_flags = 0x%x\n"
		"groups = %u\ntone_descriptors = %u\ntones = %u\n",
		proj, ts, bank, flag, ngrp, ndes, ntone);

	fprintf (out, "\n[groups]\n");
	for (u32 i = 0; i < ngrp; i++)
	{
		size_t epos = b.grp.payload + 4 + (size_t)i * 8;
		if (epos + 8 > size)
			return ERR_INVALID_DATA;
		const u32 off = rd_le32 (data + epos);
		char gname[256] = { 0 };
		size_t after;
		if (!nus3bank_entry_name (data, size, b.grp.payload + off, gname, sizeof (gname), &after))
			return ERR_INVALID_DATA;
		fprintf (out, "group%u = %s\n", i, gname);
	}

	fprintf (out, "\n[tones]\n# idx | hash | name | pack_offset pack_size\n");
	for (u32 i = 0; i < ntone; i++)
	{
		size_t epos = b.tone.payload + 4 + (size_t)i * 8;
		if (epos + 8 > size)
			return ERR_INVALID_DATA;
		const u32 off = rd_le32 (data + epos);
		size_t p = b.tone.payload + off;
		if (p + 12 > size)
			return ERR_INVALID_DATA;
		const u32 hash = rd_le32 (data + p);
		char tname[256] = { 0 };
		size_t after;
		if (!nus3bank_entry_name (data, size, p + 8, tname, sizeof (tname), &after))
			return ERR_INVALID_DATA;
		p = after + 8; // skip reserved pair
		if (p + 8 > size)
			return ERR_INVALID_DATA;
		const u32 toff = rd_le32 (data + p), tsize = rd_le32 (data + p + 4);
		fprintf (out, "%u | 0x%08x | %s | 0x%x %u\n", i, hash, tname, toff, tsize);
	}
	return ERR_OK;
}

enumError ExtractNUS3BankArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext_match (arg, ".nus3bank") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	nus3bank_t b;
	if (!nus3bank_parse (raw, raw_size, &b))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 ntone = nus3bank_table_count (&b, &b.tone);
	if (ntone == UINT32_MAX || ntone > 100000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NUS3BANK:%s (%u tones) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, ntone, dest);

	if (!testmode)
	{
		char man_path[PATH_MAX];
		snprintf (man_path, sizeof (man_path), "%s/Nus3Bank.txt", dest);
		FILE *man = fopen (man_path, "w");
		if (man)
		{
			if (DecodeNUS3Bank_Text (man, raw, raw_size))
				fprintf (man, "#NUS3BANK\n(unparseable metadata)\n");
			fclose (man);
		}
	}

	for (u32 i = 0; !testmode && i < ntone; i++)
	{
		size_t epos = b.tone.payload + 4 + (size_t)i * 8;
		if (epos + 8 > raw_size)
			break;
		const u32 off = rd_le32 (raw + epos);
		size_t p = b.tone.payload + off;
		if (p + 12 > raw_size)
			break;
		char tname[256] = { 0 };
		size_t after;
		if (!nus3bank_entry_name (raw, raw_size, p + 8, tname, sizeof (tname), &after))
			break;
		p = after + 8;
		if (p + 8 > raw_size)
			break;
		const u32 toff = rd_le32 (raw + p), tsize = rd_le32 (raw + p + 4);
		if (!b.has_pack || !tsize)
			continue;
		// tone payloads sit 8 bytes into the PACK section (the section
		// payload opens with an 8-byte sub-header)
		if ((size_t)toff + 8 + tsize < (size_t)toff + 8
			|| b.pack.payload + 8 + toff + tsize > raw_size)
			continue;
		const u8 *payload = raw + b.pack.payload + 8 + toff;

		bool name_ok = tname[0] != 0;
		for (const char *c = tname; name_ok && *c; c++)
			if (*c == '/' || *c == '\\' || (u8)*c < 0x20)
				name_ok = false;
		char base[256];
		if (name_ok)
			snprintf (base, sizeof (base), "%s", tname);
		else
			snprintf (base, sizeof (base), "tone_%04u", i);
		ccp ext = ".bin";
		if (tsize >= 4)
		{
			if (!memcmp (payload, "IDSP", 4))
				ext = ".idsp";
			else if (!memcmp (payload, "RIFF", 4))
				ext = ".wav";
		}
		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, base, ext);
		SaveFile (out_path, 0, 0, payload, tsize, 0);
	}

	FREE (raw);
	return ERR_OK;
}
