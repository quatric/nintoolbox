// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Ubisoft UbiArt IPK archives (see lib-ipk.h).
//-----------------------------------------------------------------------------

#include "lib-std.h"
#include "lib-ipk.h"
#include "lib-lzma.h"
#include <string.h>

#define IPK_MAGIC 0x50ec12bau
#define IPK_HEADER_SIZE 0x30
#define IPK_MAX_ENTRIES 0x100000
#define IPK_MAX_OUTPUT NFMT_MAX_OUTPUT
#define IPK_MAX_STRLEN 2048
#define IPK_MAX_NAME 512

// Assign the two on-disk strings to (path, name). Older titles store
// name first, newer ones path first; the dotted string is the file name
// (just_dance_2014.bms heuristic), the slash-bearing string is the path.
static void ipk_split_names (const char *s1, const char *s2, ccp *out_path, ccp *out_name)
{
	const bool s1_slash = strchr (s1, '/') != 0 || strchr (s1, '\\') != 0;
	const bool s2_slash = strchr (s2, '/') != 0 || strchr (s2, '\\') != 0;
	if (s1_slash != s2_slash)
	{
		if (s1_slash)
		{
			*out_path = s1;
			*out_name = s2;
		}
		else
		{
			*out_path = s2;
			*out_name = s1;
		}
		return;
	}
	const bool s1_dot = strchr (s1, '.') != 0;
	const bool s2_dot = strchr (s2, '.') != 0;
	if (s1_dot != s2_dot)
	{
		if (s1_dot)
		{
			*out_path = s2;
			*out_name = s1;
		}
		else
		{
			*out_path = s1;
			*out_name = s2;
		}
		return;
	}
	// Default on-disk order: s1 = path, s2 = name.
	*out_path = s1;
	*out_name = s2;
}

enumError ScanIPK (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < IPK_HEADER_SIZE || rd_be32 (data) != IPK_MAGIC)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 base = rd_be32 (data + 0x0c);
	const u32 nfiles = rd_be32 (data + 0x10);
	if (!nfiles || nfiles > IPK_MAX_ENTRIES)
		return EINVAL;
	if (base < IPK_HEADER_SIZE || (u64)base > size)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (nfiles ? nfiles : 1, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	u64 pos = IPK_HEADER_SIZE;
	for (uint i = 0; i < nfiles; i++)
	{
		// Fixed part: flag1 + size + zsize + timestamp + offset = 28 bytes.
		if (pos + 28 > size || pos + 28 > base)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		const u32 flag1 = rd_be32 (data + pos);
		const u32 fsize = rd_be32 (data + pos + 4);
		const u32 zsize = rd_be32 (data + pos + 8);
		const u64 foff = rd_be64 (data + pos + 20);
		if (flag1 != 1 && flag1 != 2)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		if (fsize > IPK_MAX_OUTPUT || zsize > IPK_MAX_OUTPUT)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		pos += 28;
		if (flag1 == 2)
		{
			if (pos + 8 > size || pos + 8 > base)
			{
				ResetOwnedEntries (out, n);
				return EINVAL;
			}
			pos += 8;
		}

		// First string.
		if (pos + 4 > size || pos + 4 > base)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		const u32 s1len = rd_be32 (data + pos);
		if (s1len > IPK_MAX_STRLEN || pos + 4 + s1len > size || pos + 4 + s1len > base)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		pos += 4;
		const u8 *s1ptr = data + pos;
		for (uint k = 0; k < s1len; k++)
			if (s1ptr[k] == 0)
			{
				ResetOwnedEntries (out, n);
				return EINVAL;
			}
		pos += s1len;

		// Second string.
		if (pos + 4 > size || pos + 4 > base)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		const u32 s2len = rd_be32 (data + pos);
		if (s2len > IPK_MAX_STRLEN || pos + 4 + s2len > size || pos + 4 + s2len > base)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		pos += 4;
		const u8 *s2ptr = data + pos;
		for (uint k = 0; k < s2len; k++)
			if (s2ptr[k] == 0)
			{
				ResetOwnedEntries (out, n);
				return EINVAL;
			}
		pos += s2len;

		if (pos + 8 > size || pos + 8 > base)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		const u32 flag2 = rd_be32 (data + pos + 4);
		if (flag2 > 2)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		pos += 8;

		if (!s1len && !s2len)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}

		char s1[IPK_MAX_STRLEN + 1], s2[IPK_MAX_STRLEN + 1];
		memcpy (s1, s1ptr, s1len);
		s1[s1len] = 0;
		memcpy (s2, s2ptr, s2len);
		s2[s2len] = 0;

		ccp epath = 0, ename = 0;
		ipk_split_names (s1, s2, &epath, &ename);
		if (!ename || !*ename)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}

		char full[IPK_MAX_NAME * 2];
		char name[IPK_MAX_NAME];
		if (strlen (epath) + 1 + strlen (ename) >= sizeof (full))
			snprintf (name, sizeof (name), "%04u.bin", i);
		else
		{
			if (!*epath)
				snprintf (full, sizeof (full), "%s", ename);
			else if (epath[strlen (epath) - 1] == '/' || epath[strlen (epath) - 1] == '\\')
				snprintf (full, sizeof (full), "%s%s", epath, ename);
			else
				snprintf (full, sizeof (full), "%s/%s", epath, ename);
			// The owned-name gate rejects backslashes; retail trees use '/'.
			for (char *p = full; *p; p++)
				if (*p == '\\')
					*p = '/';
			ccp clean_full = full;
			while (*clean_full == '/')
				clean_full++;
			if (!*clean_full || !OwnedNameOk (clean_full))
				snprintf (name, sizeof (name), "%04u.bin", i);
			else
			{
				snprintf (name, sizeof (name), "%s", clean_full);
				name[sizeof (name) - 1] = 0;
			}
		}

		// Data slice: stored bytes are zsize (compressed) or size.
		const u32 stored = zsize ? zsize : fsize;
		if (foff >= size || stored > size || (u64)base + foff + stored > size)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		const u8 *sptr = data + base + foff;

		bool ok = false;
		if (!stored && !fsize)
			ok = OwnedEntryAdd (out, n, name, sptr, 0);
		else if (!zsize)
			ok = OwnedEntryAdd (out, n, name, sptr, fsize);
		else
		{
			u8 *dec = 0;
			uint dec_size = 0;
			if (DecodeZlibGrow (&dec, &dec_size, sptr, zsize) == ERR_OK && dec && dec_size == fsize)
				ok = OwnedEntryAdd (out, n, name, dec, dec_size);
			else
			{
				if (dec)
					FREE (dec);
				dec = 0;
				dec_size = 0;
				u8 *ldec = 0;
				uint ldec_size = 0;
				// Gated on the quiet IsLZMA() sniff: DecodeLZMAbin
				// logs on failure, so never call it blindly.
				if (IsLZMA (sptr, zsize) >= 0
					&& DecodeLZMAbin (&ldec, &ldec_size, 0, sptr, zsize) == ERR_OK && ldec
					&& ldec_size == fsize)
					ok = OwnedEntryAdd (out, n, name, ldec, ldec_size);
				else
				{
					// Unknown compression (e.g. XZ container): keep the
					// stored bytes so no member is silently dropped.
					ok = OwnedEntryAdd (out, n, name, sptr, zsize);
				}
				if (ldec)
					FREE (ldec);
			}
			if (dec)
				FREE (dec);
		}
		if (!ok)
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	// The directory must exactly fill the header..base span; anything
	// else is a coincidental magic match, not an IPK.
	if (pos != base)
	{
		ResetOwnedEntries (out, n);
		return EINVAL;
	}
	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
