
/***************************************************************************
 *                    __            __ _ ___________                       *
 *                    \ \          / /| |____   ____|                      *
 *                     \ \        / / | |    | |                           *
 *                      \ \  /\  / /  | |    | |                           *
 *                       \ \/  \/ /   | |    | |                           *
 *                        \  /\  /    | |    | |                           *
 *                         \/  \/     |_|    |_|                           *
 *                                                                         *
 *                           Wiimms ISO Tools                              *
 *                         https://wit.wiimm.de/                           *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the WIT project.                                 *
 *   Visit https://wit.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2009-2021 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

// Wii "Virtual Console" content for the Wii U -- the hif_XXXXXX.nfs files that
// sit in a downloaded title's content/ directory next to rvlt.tik / rvlt.tmd.
// This is a straight port of nfs2iso2nfs (https://github.com/sabykos/nfs2iso2nfs,
// originally by Nicolas Sarkozy et al.), just streamed instead of shuffling
// temp files around.
//
// Two encryption layers stack here.  The OUTER layer wraps the whole thing in
// AES-128-CBC with a per-title key (the "htk" key, normally shipped as
// code/htk.bin); it is applied in 0x8000 byte units, the first three with a
// zero IV and the rest with a 32-bit big-endian counter that starts at
// 0x00001F00 in the low four IV bytes.  Peeling that off yields a *sparse* Wii
// disc image whose game-partition data has already had the INNER layer (the
// normal Wii partition AES, keyed by the title key inside each partition's
// ticket) removed, so this code re-applies the inner layer to hand back a
// standard, Dolphin-loadable encrypted ISO -- and reverses both directions to
// rebuild the .nfs set.
//
// Key discovery (first hit wins):
//   * $WIT_NFS_KEY -- 32 hex digits, or a path to a 16 byte key file
//   * htk.bin, code/htk.bin, ../code/htk.bin next to the .nfs files
//     (nfs->iso) or next to the source ISO and the output directory (iso->nfs)
//   * ./htk.bin
// The Wii common key needed for the inner layer is the one already built into
// libwbfs, so no wii_common_key.bin file is required.

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>

#include "x-formats.h"
#include "libwbfs/file-formats.h"
#include "libwbfs/wiidisc.h"
#include "libwbfs/rijndael.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    constants			///////////////
///////////////////////////////////////////////////////////////////////////////

#define NFS_SECTOR 0x8000 // outer-layer AES unit and sparse-map granularity
#define NFS_HEADER 0x200 // "EGGS" header, only in hif_000000.nfs
#define NFS_SPLIT 0xFA00000 // bytes per hif_XXXXXX.nfs on disk (== 8000 sectors)
#define NFS_MAGIC 0x45474753 // "EGGS"
#define NFS_MAGIC_END 0x53474745 // "SGGE", header footer

#define NFS_PART_HEAD 0x20000 // ticket (0x2a4) + partition header (0x1fd5c)
#define NFS_HASH_SIZE 0x400 // encrypted hash block in front of every cluster
#define NFS_DATA_SIZE (NFS_SECTOR - NFS_HASH_SIZE)
#define NFS_IV_OFF 0x3d0 // where the data IV lives inside the hash block

// nfs2iso2nfs pads the rebuilt ISO out to one of these two lengths.
#define NFS_SIZE_SL 0x118240000ll // single layer
#define NFS_SIZE_DL 0x1FB4E0000ll // dual layer

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   small helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static const u8 nfs_zero[NFS_SECTOR] = { 0 };

///////////////////////////////////////////////////////////////////////////////

static int hex_nibble (int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c |= 0x20;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

///////////////////////////////////////////////////////////////////////////////

static bool parse_hex_key (ccp text, u8 key[16])
{
	int n = 0;
	for (; text[n]; n++)
		if (hex_nibble ((u8)text[n]) < 0)
			return false;
	if (n != 32)
		return false;
	for (int i = 0; i < 16; i++)
		key[i] = hex_nibble ((u8)text[2 * i]) << 4 | hex_nibble ((u8)text[2 * i + 1]);
	return true;
}

///////////////////////////////////////////////////////////////////////////////

static bool load_key_file (ccp fname, u8 key[16])
{
	FILE *f = fopen (fname, "rb");
	if (!f)
		return false;
	const bool ok = fread (key, 1, 16, f) == 16 && fgetc (f) == EOF;
	fclose (f);
	return ok;
}

///////////////////////////////////////////////////////////////////////////////

// Look for htk.bin in and around DIR.  Returns true and fills KEY on success.

static bool key_near (ccp dir, u8 key[16])
{
	if (!dir || !*dir)
		return false;
	static const ccp rel[] = { "htk.bin", "code/htk.bin", "../code/htk.bin" };
	char buf[PATH_MAX];
	for (uint i = 0; i < sizeof (rel) / sizeof (*rel); i++)
	{
		PathCatPP (buf, sizeof (buf), dir, rel[i]);
		if (load_key_file (buf, key))
		{
			if (verbose >= 0)
				printf ("  htk key: %s\n", buf);
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////

static enumError find_nfs_key (ccp dir1, ccp dir2, u8 key[16])
{
	ccp env = getenv ("WIT_NFS_KEY");
	if (env && *env)
	{
		if (parse_hex_key (env, key))
		{
			if (verbose >= 0)
				printf ("  htk key: $WIT_NFS_KEY (hex)\n");
			return ERR_OK;
		}
		if (load_key_file (env, key))
		{
			if (verbose >= 0)
				printf ("  htk key: %s\n", env);
			return ERR_OK;
		}
		return ERROR0 (ERR_CANT_OPEN,
			"$WIT_NFS_KEY is neither 32 hex digits nor a 16 byte key file: %s\n", env);
	}

	if (key_near (dir1, key) || key_near (dir2, key) || key_near (".", key))
		return ERR_OK;

	return ERROR0 (ERR_CANT_OPEN,
		"Can't find the per-title AES key. Put it in 'htk.bin' (or 'code/htk.bin')"
		" next to the files, or point $WIT_NFS_KEY at it.\n");
}

///////////////////////////////////////////////////////////////////////////////

static void dir_of (ccp path, char *out, size_t out_size)
{
	char tmp[PATH_MAX];
	StringCopyS (tmp, sizeof (tmp), path);
	ccp d = dirname (tmp);
	StringCopyS (out, out_size, d && *d ? d : ".");
}

///////////////////////////////////////////////////////////////////////////////

// The outer-layer IV for sector index SEC: zero for the first three, then a
// big-endian counter from 0x00001F00 in the low four bytes.

static void outer_iv (u8 iv[16], u64 sec)
{
	memset (iv, 0, 16);
	if (sec >= 3)
	{
		const u32 ctr = 0x1f00 + (u32)(sec - 3);
		iv[12] = ctr >> 24;
		iv[13] = ctr >> 16;
		iv[14] = ctr >> 8;
		iv[15] = ctr;
	}
}

///////////////////////////////////////////////////////////////////////////////

static void sort_u32 (u32 *v, int n)
{
	for (int i = 1; i < n; i++)
	{
		const u32 x = v[i];
		int j = i - 1;
		for (; j >= 0 && v[j] > x; j--)
			v[j + 1] = v[j];
		v[j + 1] = x;
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		 nfs -> iso : sparse image source	///////////////
///////////////////////////////////////////////////////////////////////////////

// Forward-only reader over hif_000000.nfs, hif_000001.nfs, ... in one
// directory, with the 0x200 byte header of the first file already skipped.

typedef struct nfs_in_t
{
	char dir[PATH_MAX];
	int index;
	FILE *f;
	bool ended;
} nfs_in_t;

///////////////////////////////////////////////////////////////////////////////

static bool nfs_in_open_index (nfs_in_t *in)
{
	char buf[PATH_MAX];
	snprintf (buf, sizeof (buf), "%s/hif_%06d.nfs", in->dir, in->index);
	in->f = fopen (buf, "rb");
	return in->f != 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError nfs_in_init (nfs_in_t *in, ccp dir)
{
	memset (in, 0, sizeof (*in));
	StringCopyS (in->dir, sizeof (in->dir), dir);
	in->index = 0;
	if (!nfs_in_open_index (in))
		return ERROR0 (ERR_CANT_OPEN, "Can't open %s/hif_000000.nfs\n", dir);
	if (fseeko (in->f, NFS_HEADER, SEEK_SET))
	{
		fclose (in->f);
		in->f = 0;
		return ERROR0 (ERR_READ_FAILED, "Short hif_000000.nfs\n");
	}
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static void nfs_in_reset (nfs_in_t *in)
{
	if (in->f)
		fclose (in->f);
	in->f = 0;
}

///////////////////////////////////////////////////////////////////////////////

static size_t nfs_in_read (nfs_in_t *in, void *dest, size_t want)
{
	u8 *d = dest;
	size_t done = 0;
	while (done < want && !in->ended)
	{
		const size_t got = in->f ? fread (d + done, 1, want - done, in->f) : 0;
		done += got;
		if (done < want)
		{
			// current file drained -- step to the next one
			if (in->f)
				fclose (in->f);
			in->f = 0;
			in->index++;
			if (!nfs_in_open_index (in))
				in->ended = true;
		}
	}
	return done;
}

//
///////////////////////////////////////////////////////////////////////////////

// nfs_in + the outer AES layer, delivering plain sectors of the sparse image.

typedef struct nfs_dec_t
{
	nfs_in_t in;
	aes_key_t key;
	u64 sector;
} nfs_dec_t;

///////////////////////////////////////////////////////////////////////////////

static size_t nfs_dec_next (nfs_dec_t *dec, u8 out[NFS_SECTOR])
{
	const size_t got = nfs_in_read (&dec->in, out, NFS_SECTOR);
	if (!got)
		return 0;
	if (got < NFS_SECTOR)
		memset (out + got, 0, NFS_SECTOR - got);

	u8 iv[16];
	outer_iv (iv, dec->sector++);
	wd_aes_decrypt (&dec->key, iv, out, out, NFS_SECTOR);
	return got;
}

//
///////////////////////////////////////////////////////////////////////////////

// Expands the sparse map from the EGGS header: emits zero sectors for the
// gaps and decrypted sectors for the data runs, in order.

typedef struct nfs_unpack_t
{
	nfs_dec_t dec;
	u8 header[NFS_HEADER];
	u32 nparts;
	u32 part;
	u64 pos; // current output sector
	u64 run_start, run_end; // sectors, current part
	bool in_gap;
	bool done;
} nfs_unpack_t;

///////////////////////////////////////////////////////////////////////////////

static void nfs_unpack_load_part (nfs_unpack_t *u)
{
	if (u->part >= u->nparts)
	{
		u->done = true;
		return;
	}
	const u8 *e = u->header + 0x14 + 8 * u->part;
	u->run_start = be32 (e);
	u->run_end = u->run_start + be32 (e + 4);
	u->in_gap = u->pos < u->run_start;
}

///////////////////////////////////////////////////////////////////////////////

static void nfs_unpack_init (nfs_unpack_t *u)
{
	u->nparts = be32 (u->header + 0x10);
	u->part = 0;
	u->pos = 0;
	u->done = u->nparts == 0;
	if (!u->done)
		nfs_unpack_load_part (u);
}

///////////////////////////////////////////////////////////////////////////////

// Returns 1 and fills OUT with the next sector, or 0 at the end.

static int nfs_unpack_next (nfs_unpack_t *u, u8 out[NFS_SECTOR])
{
	while (!u->done)
	{
		if (u->in_gap)
		{
			if (u->pos < u->run_start)
			{
				memcpy (out, nfs_zero, NFS_SECTOR);
				u->pos++;
				return 1;
			}
			u->in_gap = false;
		}

		if (u->pos < u->run_end)
		{
			if (!nfs_dec_next (&u->dec, out))
			{
				u->done = true;
				return 0;
			}
			u->pos++;
			return 1;
		}

		u->part++;
		nfs_unpack_load_part (u);
	}
	return 0;
}

//
///////////////////////////////////////////////////////////////////////////////

// Byte-granular forward reader on top of nfs_unpack_next().

typedef struct sparse_rd_t
{
	nfs_unpack_t u;
	u8 buf[NFS_SECTOR];
	size_t have, off;
	bool eof;
} sparse_rd_t;

///////////////////////////////////////////////////////////////////////////////

static size_t sparse_read (sparse_rd_t *r, void *dest, size_t want)
{
	u8 *d = dest;
	size_t done = 0;
	while (done < want)
	{
		if (r->off >= r->have)
		{
			if (r->eof || !nfs_unpack_next (&r->u, r->buf))
			{
				r->eof = true;
				break;
			}
			r->have = NFS_SECTOR;
			r->off = 0;
		}
		size_t n = r->have - r->off;
		if (n > want - done)
			n = want - done;
		memcpy (d + done, r->buf + r->off, n);
		r->off += n;
		done += n;
	}
	return done;
}

///////////////////////////////////////////////////////////////////////////////

static enumError sparse_copy (sparse_rd_t *r, FILE *out, u64 n, ccp what)
{
	u8 buf[NFS_SECTOR];
	while (n)
	{
		const size_t chunk = n > sizeof (buf) ? sizeof (buf) : (size_t)n;
		if (sparse_read (r, buf, chunk) != chunk)
			return ERROR0 (ERR_INVALID_FILE, "Unexpected end of NFS data while reading %s\n", what);
		if (fwrite (buf, 1, chunk, out) != chunk)
			return ERROR0 (ERR_WRITE_FAILED, "Write failed\n");
		n -= chunk;
	}
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		     nfs -> iso : rebuild		///////////////
///////////////////////////////////////////////////////////////////////////////

// Walk the sparse (inner-decrypted) image and write a standard encrypted Wii
// ISO: copy everything up to the partition table verbatim, then for every
// game partition re-encrypt its hash blocks and clusters with the title key
// from its own ticket.  nfs2iso2nfs::manipulateISO(enc=true).

static enumError nfs_to_iso_rebuild (sparse_rd_t *r, FILE *out)
{
	enumError err = sparse_copy (r, out, 0x40000, "disc header");
	if (err)
		return err;

	u8 ptab[0x20];
	if (sparse_read (r, ptab, sizeof (ptab)) != sizeof (ptab))
		return ERROR0 (ERR_INVALID_FILE, "Truncated partition table\n");
	if (fwrite (ptab, 1, sizeof (ptab), out) != sizeof (ptab))
		return ERROR0 (ERR_WRITE_FAILED, "Write failed\n");

	// Partition groups, ordered by their info-table offset like the original.
	struct
	{
		u32 count, off;
	} grp[WII_MAX_PTAB];
	for (int i = 0; i < WII_MAX_PTAB; i++)
	{
		grp[i].count = be32 (ptab + 8 * i);
		grp[i].off = be32 (ptab + 8 * i + 4) * 4;
	}
	for (int i = 1; i < WII_MAX_PTAB; i++) // tiny insertion sort by .off
	{
		const u32 c = grp[i].count, o = grp[i].off;
		int j = i - 1;
		for (; j >= 0 && grp[j].off > o; j--)
			grp[j + 1] = grp[j];
		grp[j + 1].count = c;
		grp[j + 1].off = o;
	}

	u64 cur = 0x40020;
	u32 data_off[WII_MAX_PARTITIONS];
	int n_data = 0;

	for (int i = 0; i < WII_MAX_PTAB; i++)
	{
		if (!grp[i].count)
			continue;
		if (grp[i].off < cur)
			return ERROR0 (ERR_INVALID_FILE, "Partition info table before the previous one\n");
		if ((err = sparse_copy (r, out, grp[i].off - cur, "gap")))
			return err;
		cur = grp[i].off;

		const size_t tab_size = (size_t)8 * grp[i].count;
		u8 *tab = MALLOC (tab_size);
		if (sparse_read (r, tab, tab_size) != tab_size)
		{
			FREE (tab);
			return ERROR0 (ERR_INVALID_FILE, "Truncated partition info table\n");
		}
		cur += tab_size;
		for (u32 j = 0; j < grp[i].count; j++)
			if (tab[8 * j + 7] == 0 && n_data < WII_MAX_PARTITIONS) // type 0 == data
				data_off[n_data++] = be32 (tab + 8 * j) * 4;
		const bool wok = fwrite (tab, 1, tab_size, out) == tab_size;
		FREE (tab);
		if (!wok)
			return ERROR0 (ERR_WRITE_FAILED, "Write failed\n");
	}

	sort_u32 (data_off, n_data);

	for (int i = 0; i < n_data; i++)
	{
		if (data_off[i] < cur)
			return ERROR0 (ERR_INVALID_FILE, "Overlapping partitions\n");
		if ((err = sparse_copy (r, out, data_off[i] - cur, "gap")))
			return err;
		cur = data_off[i];

		u8 *head = MALLOC (NFS_PART_HEAD);
		if (sparse_read (r, head, NFS_PART_HEAD) != NFS_PART_HEAD)
		{
			FREE (head);
			return ERROR0 (ERR_INVALID_FILE, "Truncated partition header\n");
		}

		u8 title_key[WII_KEY_SIZE];
		wd_decrypt_title_key ((const wd_ticket_t *)head, title_key);
		aes_key_t pkey;
		wd_aes_set_key (&pkey, title_key);

		// data_size lives in the partition header at +0x2a4+0x18, in units of 4.
		u64 part_size = (u64)be32 (head + 0x2a4 + 0x18) * 4;
		const bool wok = fwrite (head, 1, NFS_PART_HEAD, out) == NFS_PART_HEAD;
		FREE (head);
		if (!wok)
			return ERROR0 (ERR_WRITE_FAILED, "Write failed\n");
		cur += NFS_PART_HEAD + part_size;

		if (verbose >= 0)
			printf ("  partition %d: data 0x%llx bytes\n", i, (unsigned long long)part_size);

		u8 dh[NFS_HASH_SIZE], eh[NFS_HASH_SIZE], body[NFS_DATA_SIZE], iv[16];
		while (part_size >= NFS_SECTOR)
		{
			if (sparse_read (r, dh, NFS_HASH_SIZE) != NFS_HASH_SIZE)
				break;
			memset (iv, 0, 16);
			wd_aes_encrypt (&pkey, iv, dh, eh, NFS_HASH_SIZE);
			if (fwrite (eh, 1, NFS_HASH_SIZE, out) != NFS_HASH_SIZE)
				return ERROR0 (ERR_WRITE_FAILED, "Write failed\n");

			const size_t got = sparse_read (r, body, NFS_DATA_SIZE);
			if (!got)
				break;
			if (got < NFS_DATA_SIZE)
				memset (body + got, 0, NFS_DATA_SIZE - got);
			memcpy (iv, eh + NFS_IV_OFF, 16);
			wd_aes_encrypt (&pkey, iv, body, body, NFS_DATA_SIZE);
			if (fwrite (body, 1, NFS_DATA_SIZE, out) != NFS_DATA_SIZE)
				return ERROR0 (ERR_WRITE_FAILED, "Write failed\n");
			part_size -= NFS_SECTOR;
		}
	}

	// Pad to the retail single- or dual-layer length, like nfs2iso2nfs.
	const s64 target = (s64)cur > NFS_SIZE_SL ? NFS_SIZE_DL : NFS_SIZE_SL;
	for (s64 rest = target - (s64)cur; rest > 0; rest -= NFS_SECTOR)
	{
		const size_t n = rest > NFS_SECTOR ? NFS_SECTOR : (size_t)rest;
		if (fwrite (nfs_zero, 1, n, out) != n)
			return ERROR0 (ERR_WRITE_FAILED, "Write failed\n");
	}
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		     iso -> nfs : split writer		///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct nfs_out_t
{
	char dir[PATH_MAX];
	int index;
	FILE *f;
	u64 in_file; // bytes in the current hif_*.nfs
	enumError err;
} nfs_out_t;

///////////////////////////////////////////////////////////////////////////////

static bool nfs_out_open_index (nfs_out_t *o)
{
	char buf[PATH_MAX];
	snprintf (buf, sizeof (buf), "%s/hif_%06d.nfs", o->dir, o->index);
	o->f = fopen (buf, "wb");
	o->in_file = 0;
	if (!o->f)
		o->err = ERROR0 (ERR_CANT_CREATE, "Can't create %s\n", buf);
	else if (verbose >= 0)
		printf ("  writing hif_%06d.nfs\n", o->index);
	return o->f != 0;
}

///////////////////////////////////////////////////////////////////////////////

static void nfs_out_init (nfs_out_t *o, ccp dir)
{
	memset (o, 0, sizeof (*o));
	StringCopyS (o->dir, sizeof (o->dir), dir);
	nfs_out_open_index (o);
}

///////////////////////////////////////////////////////////////////////////////

static void nfs_out_write (nfs_out_t *o, const void *data, size_t size)
{
	const u8 *p = data;
	while (size && !o->err)
	{
		if (o->in_file >= NFS_SPLIT)
		{
			fclose (o->f);
			o->f = 0;
			o->index++;
			if (!nfs_out_open_index (o))
				return;
		}
		size_t n = NFS_SPLIT - o->in_file;
		if (n > size)
			n = size;
		if (fwrite (p, 1, n, o->f) != n)
		{
			o->err = ERROR0 (ERR_WRITE_FAILED, "Write failed on hif_%06d.nfs\n", o->index);
			return;
		}
		o->in_file += n;
		p += n;
		size -= n;
	}
}

///////////////////////////////////////////////////////////////////////////////

static enumError nfs_out_close (nfs_out_t *o)
{
	if (o->f)
		fclose (o->f);
	o->f = 0;
	return o->err;
}

//
///////////////////////////////////////////////////////////////////////////////

// Outer-layer encrypt of a byte stream: buffer to 0x8000, encrypt with the
// sector IV, hand to the split writer.

typedef struct nfs_enc_t
{
	nfs_out_t out;
	aes_key_t key;
	u64 sector;
	u8 buf[NFS_SECTOR];
	size_t fill;
} nfs_enc_t;

///////////////////////////////////////////////////////////////////////////////

static void nfs_enc_flush_sector (nfs_enc_t *e, size_t len)
{
	u8 iv[16], tmp[NFS_SECTOR];
	outer_iv (iv, e->sector++);
	if (len < NFS_SECTOR)
		memset (e->buf + len, 0, NFS_SECTOR - len);
	wd_aes_encrypt (&e->key, iv, e->buf, tmp, NFS_SECTOR);
	nfs_out_write (&e->out, tmp, len < NFS_SECTOR ? (len + 15 & ~(size_t)15) : NFS_SECTOR);
	e->fill = 0;
}

///////////////////////////////////////////////////////////////////////////////

static void nfs_enc_write (nfs_enc_t *e, const void *data, size_t size)
{
	const u8 *p = data;
	while (size)
	{
		size_t n = NFS_SECTOR - e->fill;
		if (n > size)
			n = size;
		memcpy (e->buf + e->fill, p, n);
		e->fill += n;
		p += n;
		size -= n;
		if (e->fill == NFS_SECTOR)
			nfs_enc_flush_sector (e, NFS_SECTOR);
	}
}

///////////////////////////////////////////////////////////////////////////////

static enumError nfs_enc_finish (nfs_enc_t *e)
{
	if (e->fill)
		nfs_enc_flush_sector (e, e->fill);
	return nfs_out_close (&e->out);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		     iso -> nfs : partition scan	///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct iso_part_t
{
	u64 offset; // absolute byte offset of the partition (its ticket)
	u64 data_size; // partition data_size in bytes (already *4)
} iso_part_t;

///////////////////////////////////////////////////////////////////////////////

static enumError read_at (FILE *f, u64 off, void *buf, size_t size, ccp what)
{
	if (fseeko (f, (off_t)off, SEEK_SET) || fread (buf, 1, size, f) != size)
		return ERROR0 (ERR_READ_FAILED, "Can't read %s at 0x%llx\n", what, (unsigned long long)off);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError scan_iso_partitions (
	FILE *f, iso_part_t *part, int *n_part_out, u64 *first_off, u64 *span)
{
	u8 ptab[0x20];
	enumError err = read_at (f, WII_PTAB_REF_OFF, ptab, sizeof (ptab), "partition table");
	if (err)
		return err;

	u32 goff[WII_MAX_PTAB], gcnt[WII_MAX_PTAB];
	for (int i = 0; i < WII_MAX_PTAB; i++)
	{
		gcnt[i] = be32 (ptab + 8 * i);
		goff[i] = be32 (ptab + 8 * i + 4) * 4;
	}

	int n = 0;
	u32 raw[WII_MAX_PARTITIONS];
	for (int i = 0; i < WII_MAX_PTAB; i++)
	{
		if (!gcnt[i] || gcnt[i] > WII_MAX_PARTITIONS)
			continue;
		u8 tab[WII_MAX_PARTITIONS * 8];
		if ((err = read_at (f, goff[i], tab, 8 * gcnt[i], "partition info table")))
			return err;
		for (u32 j = 0; j < gcnt[i]; j++)
			if (tab[8 * j + 7] == 0 && n < WII_MAX_PARTITIONS) // data partition
				raw[n++] = be32 (tab + 8 * j) * 4;
	}
	if (!n)
		return ERROR0 (ERR_INVALID_FILE, "No data partition in the ISO\n");
	sort_u32 (raw, n);

	for (int i = 0; i < n; i++)
	{
		u8 dh[4];
		if ((err = read_at (f, raw[i] + 0x2a4 + 0x18, dh, 4, "partition header")))
			return err;
		part[i].offset = raw[i];
		part[i].data_size = (u64)be32 (dh) * 4;
	}

	// nfs2iso2nfs measures the game-data span from the first partition to the
	// end of the last, gaps included.
	*first_off = part[0].offset;
	*span = part[n - 1].offset + NFS_PART_HEAD + (part[n - 1].data_size & ~(u64)(NFS_SECTOR - 1))
		- part[0].offset;
	*n_part_out = n;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		     iso -> nfs : build body		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError feed_verbatim (FILE *in, u64 off, u64 size, nfs_enc_t *e)
{
	if (fseeko (in, (off_t)off, SEEK_SET))
		return ERROR0 (ERR_READ_FAILED, "Seek failed at 0x%llx\n", (unsigned long long)off);
	u8 buf[NFS_SECTOR];
	while (size)
	{
		const size_t n = size > sizeof (buf) ? sizeof (buf) : (size_t)size;
		if (fread (buf, 1, n, in) != n)
			return ERROR0 (ERR_READ_FAILED, "Short read from the ISO\n");
		nfs_enc_write (e, buf, n);
		size -= n;
	}
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Strip the inner Wii partition AES from one partition's clusters and feed the
// plaintext into the outer-layer encryptor.  nfs2iso2nfs::manipulateISO(enc=false).

static enumError feed_partition (FILE *in, const iso_part_t *p, nfs_enc_t *e)
{
	u8 tik[0x2a4];
	enumError err = read_at (in, p->offset, tik, sizeof (tik), "ticket");
	if (err)
		return err;
	u8 title_key[WII_KEY_SIZE];
	wd_decrypt_title_key ((const wd_ticket_t *)tik, title_key);
	aes_key_t pkey;
	wd_aes_set_key (&pkey, title_key);

	if ((err = feed_verbatim (in, p->offset, NFS_PART_HEAD, e))) // ticket + part header
		return err;

	if (fseeko (in, (off_t)(p->offset + NFS_PART_HEAD), SEEK_SET))
		return ERROR0 (ERR_READ_FAILED, "Seek failed\n");

	u8 eh[NFS_HASH_SIZE], body[NFS_DATA_SIZE], iv[16];
	for (u64 left = p->data_size & ~(u64)(NFS_SECTOR - 1); left >= NFS_SECTOR; left -= NFS_SECTOR)
	{
		if (fread (eh, 1, NFS_HASH_SIZE, in) != NFS_HASH_SIZE
			|| fread (body, 1, NFS_DATA_SIZE, in) != NFS_DATA_SIZE)
			return ERROR0 (ERR_READ_FAILED, "Short read in partition data\n");

		u8 dh[NFS_HASH_SIZE];
		memset (iv, 0, 16);
		wd_aes_decrypt (&pkey, iv, eh, dh, NFS_HASH_SIZE);
		nfs_enc_write (e, dh, NFS_HASH_SIZE);

		memcpy (iv, eh + NFS_IV_OFF, 16);
		wd_aes_decrypt (&pkey, iv, body, body, NFS_DATA_SIZE);
		nfs_enc_write (e, body, NFS_DATA_SIZE);
	}
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		     fw.img fakesign patch		///////////////
///////////////////////////////////////////////////////////////////////////////

// The Wii U's Wii-mode IOS rejects fakesigned tickets/TMDs unless this one
// hash check in fw.img is neutered.  nfs2iso2nfs does it by turning the
// second byte of a known ARM instruction pair to zero; the patch is
// idempotent.  Only run for iso->nfs, and never with $WIT_NFS_LEGIT set.

static void patch_fwimg (ccp path)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return;
	struct stat st;
	if (fstat (fileno (f), &st) || st.st_size < 4)
	{
		fclose (f);
		return;
	}
	u8 *buf = MALLOC ((size_t)st.st_size);
	const bool ok = fread (buf, 1, st.st_size, f) == (size_t)st.st_size;
	fclose (f);
	if (!ok)
	{
		FREE (buf);
		return;
	}

	static const u8 pat_a[4] = { 0x20, 0x07, 0x23, 0xa2 };
	static const u8 pat_b[4] = { 0x20, 0x07, 0x4b, 0x0b };
	int n = 0;
	for (off_t i = 0; i + 4 <= st.st_size; i++)
		if (!memcmp (buf + i, pat_a, 4) || !memcmp (buf + i, pat_b, 4))
		{
			buf[i + 1] = 0x00;
			n++;
		}

	if (n)
	{
		FILE *w = fopen (path, "r+b");
		if (w)
		{
			fwrite (buf, 1, st.st_size, w);
			fclose (w);
		}
		printf ("  fw.img: fakesign hash check patched (%d site%s): %s\n", n, n == 1 ? "" : "s",
			path);
	}
	else if (verbose >= 0)
		printf ("  fw.img: nothing to patch (already done?): %s\n", path);
	FREE (buf);
}

///////////////////////////////////////////////////////////////////////////////

static void maybe_patch_fwimg (ccp dir1, ccp dir2)
{
	if (getenv ("WIT_NFS_LEGIT"))
		return;

	ccp env = getenv ("WIT_NFS_FWIMG");
	if (env && *env)
	{
		patch_fwimg (env);
		return;
	}

	static const ccp rel[] = { "fw.img", "code/fw.img", "../code/fw.img" };
	char buf[PATH_MAX];
	for (int d = 0; d < 2; d++)
	{
		ccp dir = d ? dir2 : dir1;
		if (!dir || !*dir)
			continue;
		for (uint i = 0; i < sizeof (rel) / sizeof (*rel); i++)
		{
			PathCatPP (buf, sizeof (buf), dir, rel[i]);
			struct stat st;
			if (!stat (buf, &st) && S_ISREG (st.st_mode))
			{
				patch_fwimg (buf);
				return;
			}
		}
	}
	if (verbose >= 0)
		printf ("  fw.img: not found -- the .nfs set is built but not fakesign enabled"
				" (set $WIT_NFS_FWIMG, or $WIT_NFS_LEGIT to silence this)\n");
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     XINFO			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XInfoNFS (ccp source)
{
	FILE *f = fopen (source, "rb");
	if (!f)
		return ERROR1 (ERR_CANT_OPEN, "Can't open file: %s\n", source);
	u8 h[NFS_HEADER];
	const bool ok = fread (h, 1, sizeof (h), f) == sizeof (h);
	fclose (f);
	if (!ok || be32 (h) != NFS_MAGIC)
		return ERROR0 (ERR_INVALID_FILE, "Not an EGGS/NFS header: %s\n", source);

	const u32 nparts = be32 (h + 0x10);
	printf ("      eggs:     version 0x%08x, %u data run%s%s\n", be32 (h + 4), nparts,
		nparts == 1 ? "" : "s", be32 (h + 0x1fc) == NFS_MAGIC_END ? "" : " (bad footer)");

	u64 total = 0;
	for (u32 i = 0; i < nparts && 0x14 + 8 * i + 8 <= NFS_HEADER; i++)
	{
		const u32 start = be32 (h + 0x14 + 8 * i);
		const u32 len = be32 (h + 0x18 + 8 * i);
		total += (u64)len * NFS_SECTOR;
		printf ("        run %u: image 0x%llx, length 0x%llx\n", i,
			(unsigned long long)start * NFS_SECTOR, (unsigned long long)len * NFS_SECTOR);
	}
	printf ("      payload:  0x%llx bytes over all runs\n", (unsigned long long)total);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   XEXTRACT (nfs -> iso)		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XExtractNFS (ccp source, ccp dest)
{
	char src_dir[PATH_MAX], dst_dir[PATH_MAX];
	dir_of (source, src_dir, sizeof (src_dir));
	dir_of (dest, dst_dir, sizeof (dst_dir));

	u8 key[16];
	enumError err = find_nfs_key (src_dir, dst_dir, key);
	if (err)
		return err;

	sparse_rd_t *r = CALLOC (1, sizeof (*r));
	FILE *out = 0;

	// EGGS header from the first file.
	FILE *hf = fopen (source, "rb");
	if (!hf || fread (r->u.header, 1, NFS_HEADER, hf) != NFS_HEADER)
	{
		if (hf)
			fclose (hf);
		err = ERROR0 (ERR_CANT_OPEN, "Can't read %s\n", source);
		goto done;
	}
	fclose (hf);
	if (be32 (r->u.header) != NFS_MAGIC)
	{
		err = ERROR0 (ERR_INVALID_FILE, "Not an EGGS/NFS header: %s\n", source);
		goto done;
	}

	if ((err = nfs_in_init (&r->u.dec.in, src_dir)))
		goto done;
	wd_aes_set_key (&r->u.dec.key, key);
	nfs_unpack_init (&r->u);

	if ((err = CreatePath (dest, false)))
		goto done;
	out = fopen (dest, "wb");
	if (!out)
	{
		err = ERROR1 (ERR_CANT_CREATE, "Can't create file: %s\n", dest);
		goto done;
	}

	err = nfs_to_iso_rebuild (r, out);
	if (!err && verbose >= 0)
		printf ("  extracted %s -> %s\n", source, dest);

done:
	if (out)
		fclose (out);
	if (err && out)
		unlink (dest);
	nfs_in_reset (&r->u.dec.in);
	FREE (r);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   XCREATE (iso -> nfs)		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XCreateNFS (ccp source, ccp dest)
{
	// DEST names the first .nfs file (so the extension picks the format); the
	// files really live in its directory.
	char out_dir[PATH_MAX], src_dir[PATH_MAX];
	ccp dot = strrchr (dest, '.');
	if (dot && !strcasecmp (dot, ".nfs"))
		dir_of (dest, out_dir, sizeof (out_dir));
	else
		StringCopyS (out_dir, sizeof (out_dir), dest);
	dir_of (source, src_dir, sizeof (src_dir));

	u8 key[16];
	enumError err = find_nfs_key (src_dir, out_dir, key);
	if (err)
		return err;

	char probe[PATH_MAX];
	PathCatPP (probe, sizeof (probe), out_dir, "hif_000000.nfs");
	if ((err = CreatePath (probe, false)))
		return err;

	FILE *in = fopen (source, "rb");
	if (!in)
		return ERROR1 (ERR_CANT_OPEN, "Can't open file: %s\n", source);

	iso_part_t part[WII_MAX_PARTITIONS];
	int n_part = 0;
	u64 first_off = 0, span = 0;
	err = scan_iso_partitions (in, part, &n_part, &first_off, &span);
	if (err)
	{
		fclose (in);
		return err;
	}
	if (verbose >= 0)
		printf ("  %d data partition%s, game span 0x%llx bytes\n", n_part, n_part == 1 ? "" : "s",
			(unsigned long long)span);

	// EGGS header: three runs -- [0,0x8000), [0x40000,0x50000) and the game
	// span -- exactly as nfs2iso2nfs::packNFS lays them out.
	u8 header[NFS_HEADER];
	memset (header, 0xff, sizeof (header));
	write_be32 (header + 0x00, NFS_MAGIC);
	write_be32 (header + 0x04, 0x00011011);
	write_be32 (header + 0x08, 0);
	write_be32 (header + 0x0c, 0);
	write_be32 (header + 0x10, 3); // run count
	write_be32 (header + 0x14, 0); // run 0: start
	write_be32 (header + 0x18, 1); //        length (sectors)
	write_be32 (header + 0x1c, 8); // run 1: start
	write_be32 (header + 0x20, 2); //        length
	write_be32 (header + 0x24, (u32)(first_off / NFS_SECTOR)); // run 2: start
	write_be32 (header + 0x28, (u32)(span / NFS_SECTOR)); //        length
	write_be32 (header + 0x1fc, NFS_MAGIC_END);

	nfs_enc_t *e = CALLOC (1, sizeof (*e));
	wd_aes_set_key (&e->key, key);
	nfs_out_init (&e->out, out_dir);
	if (e->out.err)
	{
		err = e->out.err;
		goto done;
	}

	// The 0x200 EGGS header rides in front of the encrypted body, unencrypted.
	nfs_out_write (&e->out, header, NFS_HEADER);

	if (!(err = feed_verbatim (in, 0, NFS_SECTOR, e)) // run 0
		&& !(err = feed_verbatim (in, WII_PTAB_REF_OFF, NFS_SECTOR * 2, e))) // run 1
	{
		u64 cur = first_off;
		for (int i = 0; i < n_part && !err; i++)
		{
			if (part[i].offset > cur)
				err = feed_verbatim (in, cur, part[i].offset - cur, e); // inter-partition gap
			if (!err)
				err = feed_partition (in, part + i, e);
			cur = part[i].offset + NFS_PART_HEAD + part[i].data_size;
		}
	}
	if (e->out.err && !err)
		err = e->out.err;

	{
		const enumError ferr = nfs_enc_finish (e);
		if (!err)
			err = ferr;
	}

done:
	fclose (in);
	FREE (e);
	if (err)
		return err;

	maybe_patch_fwimg (out_dir, src_dir);
	if (verbose >= 0)
		printf ("  created %s (and siblings) from %s\n", probe, source);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
