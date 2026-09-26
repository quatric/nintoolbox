// SPDX-License-Identifier: GPL-2.0+
// Sonic Storybook ONE archive creator.
//
// Sonic and the Secret Rings / Sonic and the Black Knight pack their assets
// into a small big-endian ONE container whose payloads are PRS-compressed.
// The reader lives in wszst_cmd/create_update.inc (decode_storybook_prs +
// extract_storybook_one_file); this file is the write side, emiting the same
// bit format so CREATE -> EXTRACT is a content-exact round trip.
#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-one.h"
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
// PRS compressor
//
// The PRS stream is a Microsoft-style LZ77 where a flag byte precedes every
// run of up to eight tokens, LSB first:
//
//   flag bit 1          literal byte follows
//   flag bits 0,1       two-byte match: u16 word, little-endian.
//                           length_code = word & 7        (0 -> extra byte)
//                           offset_code = word >> 3       (13 bits)
//                           offset = offset_code - 0x2000, length = code+2
//                           (extra byte: length = byte + 1)
//   flag bits 0,0       one-byte match: offset byte then.
//                           length = (b0<<1 | b1) + 2 from the two next bits
//                           offset = byte - 0x100
//   flag bits 0,1 with  word == 0          end of stream
//
// The encoder below is a plain greedy LZ77 with a 64K hash of recent two-byte
// keys (four candidates per key), window cap 0x2000 and length cap 0x100.
// It is intentionally NOT byte-exact with Sonic Team's own compressor; it only
// has to emit a PRS stream the reader accepts and that round-trips data.
//-----------------------------------------------------------------------------

#define PRS_MAX_DIST 0x2000
#define PRS_MAX_LEN 0x100
#define PRS_HASH_KEYS 0x10000
#define PRS_HASH_SLOTS 4

// FILE-scope match tables: 1 MiB of BSS, zero-initialised, reused across
// members. The tool is single threaded, so no locking is needed.
static u32 prs_pos_slot[PRS_HASH_KEYS][PRS_HASH_SLOTS];
static u32 prs_pos_count[PRS_HASH_KEYS];

typedef struct prs_enc_t
{
	u8 *data;
	uint size, cap;
	uint flag_pos; // slot of the current flag byte, valid while nflag != 0
	uint nflag; // number of flag bits already written to that slot (0..7)
} prs_enc_t;

static int prs_enc_reserve (prs_enc_t *e, uint extra)
{
	if (e->size + extra > e->cap)
	{
		uint ncap = e->cap ? e->cap : 256;
		while (ncap < e->size + extra)
		{
			if (ncap > NFMT_MAX_OUTPUT)
				return 1;
			ncap *= 2;
			if (ncap < 256)
				ncap = 256;
		}
		u8 *nd = REALLOC (e->data, ncap);
		if (!nd)
			return 1;
		e->data = nd;
		e->cap = ncap;
	}
	return 0;
}

static void prs_enc_byte (prs_enc_t *e, u8 b)
{
	e->data[e->size++] = b;
}

static int prs_flag_bit (prs_enc_t *e, uint bit)
{
	// Start of a new flag byte: reserve its slot NOW, so the flag byte
	// always precedes the tokens it describes (the reader consumes the
	// stream as run-length encoded control bytes, not as a prefix header).
	if (!e->nflag)
	{
		if (prs_enc_reserve (e, 1))
			return 1;
		e->flag_pos = e->size++;
		e->data[e->flag_pos] = 0;
	}
	if (bit)
		e->data[e->flag_pos] |= (u8)(1u << e->nflag);
	if (++e->nflag == 8)
		e->nflag = 0;
	return 0;
}

static int prs_emit_literal (prs_enc_t *e, u8 b)
{
	if (prs_flag_bit (e, 1))
		return 1;
	prs_enc_byte (e, b);
	return 0;
}

static int prs_emit_short (prs_enc_t *e, uint dist, uint length)
{
	if (prs_flag_bit (e, 0) || prs_flag_bit (e, 0))
		return 1;
	const uint len_code = length - 2; // 0..3
	if (prs_flag_bit (e, len_code >> 1) || prs_flag_bit (e, len_code & 1))
		return 1;
	prs_enc_byte (e, (u8)(0x100 - dist));
	return 0;
}

static int prs_emit_long (prs_enc_t *e, uint dist, uint length)
{
	if (prs_flag_bit (e, 0) || prs_flag_bit (e, 1))
		return 1;
	const u16 word = (u16)((0x2000 - dist) << 3);
	if (length <= 9)
	{
		prs_enc_byte (e, (u8)(word | (length - 2)));
		prs_enc_byte (e, (u8)(word >> 8));
	}
	else
	{
		prs_enc_byte (e, (u8)word);
		prs_enc_byte (e, (u8)(word >> 8));
		prs_enc_byte (e, (u8)(length - 1));
	}
	return 0;
}

static int prs_emit_end (prs_enc_t *e)
{
	if (prs_flag_bit (e, 0) || prs_flag_bit (e, 1))
		return 1;
	prs_enc_byte (e, 0);
	prs_enc_byte (e, 0);
	return 0;
}

static void prs_push (ccp d, uint size, uint pos)
{
	if (pos + 1 >= size)
		return;
	const uint key = (uint)((u8)d[pos] << 8) | (u8)d[pos + 1];
	u32 *slot = prs_pos_slot[key];
	const uint count = prs_pos_count[key]++;
	slot[count % PRS_HASH_SLOTS] = pos;
}

// Longest match for position I against the window kept in prs_pos_slot.
static void prs_find_match (ccp d, uint size, uint i, uint *best_len, uint *best_dist)
{
	*best_len = 0;
	*best_dist = 0;
	if (i + 1 >= size)
		return;
	const uint key = (uint)((u8)d[i] << 8) | (u8)d[i + 1];
	u32 *slot = prs_pos_slot[key];
	uint count = prs_pos_count[key];
	if (count > PRS_HASH_SLOTS)
		count = PRS_HASH_SLOTS;
	for (uint s = 0; s < count; s++)
	{
		const uint p = slot[s];
		if (p >= i)
			continue;
		const uint dist = i - p;
		if (!dist || dist > PRS_MAX_DIST)
			continue;
		uint len = 0;
		while (i + len < size && len < PRS_MAX_LEN && d[p + len] == d[i + len])
			len++;
		if (len > *best_len || (len == *best_len && dist < *best_dist))
		{
			*best_len = len;
			*best_dist = dist;
		}
	}
}

enumError EncodeStorybookPRS (u8 **dest, uint *dest_size, const u8 *data, uint size)
{
	if (!dest || !dest_size || !data || !size)
		return EINVAL;

	prs_enc_t e = { 0 };
	enumError err = ERR_OK;
	uint i = 0;
	while (i < size)
	{
		uint len, dist;
		prs_find_match ((ccp)data, size, i, &len, &dist);
		if (len >= 3)
		{
			const bool short_ok = dist <= 0x100 && len <= 5;
			if (prs_enc_reserve (&e, short_ok ? 2 : len > 9 ? 4 : 3))
			{
				err = EFBIG;
				break;
			}
			if (short_ok)
			{
				if (prs_emit_short (&e, dist, len))
				{
					err = EFBIG;
					break;
				}
			}
			else if (prs_emit_long (&e, dist, len))
			{
				err = EFBIG;
				break;
			}
			const uint end = i + len;
			while (i < end)
			{
				prs_push ((ccp)data, size, i);
				i++;
			}
		}
		else
		{
			if (prs_enc_reserve (&e, 2))
			{
				err = EFBIG;
				break;
			}
			if (prs_emit_literal (&e, data[i]))
			{
				err = EFBIG;
				break;
			}
			prs_push ((ccp)data, size, i);
			i++;
		}
	}
	if (!err && prs_enc_reserve (&e, 4))
		err = EFBIG;
	if (err)
	{
		FREE (e.data);
		return err;
	}
	if (prs_emit_end (&e))
	{
		FREE (e.data);
		return EFBIG;
	}

	*dest = e.data;
	*dest_size = e.size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// ONE container
//
//   0x00  u32 entry count
//   0x04  u32 entry-table offset (always 16)
//   0x08  u32 data offset            (table + count*48)
//   0x0c  u32 0 (Secret Rings) or -1 (Black Knight)
//   0x10  entry[count], 48 bytes each:
//           char name[32]
//           u32 zero
//           u32 absolute payload offset
//           u32 compressed size
//           u32 uncompressed size
//   Data: PRS-compressed members, back to back.
//-----------------------------------------------------------------------------

enumError CreateONEArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, u32 marker)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x40000)
		return EINVAL;

	// Compress every member up front so a failure (oversized output,
	// unencodable body) leaves no half-built container behind.
	u8 **blobs = CALLOC (n_entries, sizeof (*blobs));
	uint *csize = MALLOC (n_entries * sizeof (*csize));
	if (!blobs || !csize)
	{
		FREE (blobs);
		FREE (csize);
		return ERR_CANT_CREATE;
	}

	u64 cur = 16 + (u64)n_entries * 48;
	enumError err = ERR_OK;
	for (uint i = 0; i < n_entries && !err; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		const uint nlen = (uint)strlen (name);
		if (!nlen || nlen > 32 || !entries[i].size)
		{
			err = EINVAL;
			break;
		}
		err = EncodeStorybookPRS (&blobs[i], &csize[i], entries[i].data, entries[i].size);
		cur += csize[i];
		if (!err && cur > NFMT_MAX_OUTPUT)
			err = EFBIG;
	}
	if (err || cur > 0xffffffffULL)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (blobs[i]);
		FREE (blobs);
		FREE (csize);
		return err ? err : EFBIG;
	}

	u8 *out = CALLOC (1, (size_t)cur);
	if (!out)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (blobs[i]);
		FREE (blobs);
		FREE (csize);
		return ERR_CANT_CREATE;
	}

	wr_be32 (out + 0x00, n_entries);
	wr_be32 (out + 0x04, 16);
	wr_be32 (out + 0x08, (u32)cur); // overwritten below with the real start
	wr_be32 (out + 0x0c, marker);

	u64 pos = 16 + (u64)n_entries * 48;
	wr_be32 (out + 0x08, (u32)pos);
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		const uint nlen = (uint)strlen (name);
		u8 *entry = out + 16 + (size_t)i * 48;
		memcpy (entry, name, nlen);
		wr_be32 (entry + 36, (u32)pos);
		wr_be32 (entry + 40, csize[i]);
		wr_be32 (entry + 44, entries[i].size);
		memcpy (out + pos, blobs[i], csize[i]);
		pos += csize[i];
		FREE (blobs[i]);
	}
	FREE (blobs);
	FREE (csize);

	*dest = out;
	*dest_size = (uint)cur;
	return ERR_OK;
}

// Sonic Storybook ONE. Compresses every member with EncodeStorybookPRS.
// Reuses the original archive's marker byte (0 = Secret Rings, -1 = Black
// Knight) when the destination already holds one; brand-new containers are
// written for Secret Rings.
enumError create_one_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u32 marker = 0;
	FILE *in = fopen (dest, "rb");
	if (in)
	{
		u8 head[16];
		const u32 marker_be = fread (head, 1, 16, in) == 16 ? be32 (head + 12) : 0;
		if (marker_be == 0 || marker_be == 0xffffffff)
			marker = marker_be;
		fclose (in);
	}

	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateONEArchive (&data, &size, list.entry, list.used, marker);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, source);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}
