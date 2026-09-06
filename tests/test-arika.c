// Standalone regression for Arika's INFO.DAT/GAME.DAT archive pair and its
// ALZ1 compression, ported from GBATEK's "DS Encrypted Arika Archives with
// ALZ1 compression" page (Dr. Mario Online Rx, Dr. Mario Express, the
// original DS Endless Ocean) plus aluigi's arika.bms/endless_ocean.bms for
// the RF2 sub-container grouping used by Endless Ocean: Blue World.
//
// No retail sample of either title was available in this environment, so
// every case here is either a hand-built fixture checked against the
// documented byte-level algorithm directly (not just against this file's
// own encoder), or a create->extract round trip through this project's own
// encoder/decoder pair.
#include "lib-nintendo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The project test build enables allocation tracing. Keep this standalone
// regression independent of the full CLI support graph.
void trace_free (ccp f, ccp p, uint l, void *v)
{
	(void)f;
	(void)p;
	(void)l;
	free (v);
}
void *trace_malloc (ccp f, ccp p, uint l, size_t n)
{
	(void)f;
	(void)p;
	(void)l;
	return malloc (n);
}
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s)
{
	(void)f;
	(void)p;
	(void)l;
	return calloc (n, s);
}
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n)
{
	(void)f;
	(void)p;
	(void)l;
	return realloc (v, n);
}
char *trace_strdup (ccp f, ccp p, uint l, ccp s)
{
	(void)f;
	(void)p;
	(void)l;
	return s ? strdup (s) : 0;
}
void dclib_free (void *v)
{
	free (v);
}
void *dclib_malloc (size_t n)
{
	return malloc (n);
}
void *dclib_calloc (size_t n, size_t s)
{
	return calloc (n, s);
}
void *dclib_realloc (void *v, size_t n)
{
	return realloc (v, n);
}
char *dclib_strdup (ccp s)
{
	return s ? strdup (s) : 0;
}

// Hand-built ALZ1 bitstream vectors, derived directly from GBATEK's
// decompression pseudocode (not from this project's own EncodeALZ1), to
// catch a decoder that's merely self-consistent with its own encoder but
// wrong against the spec.
static int check_alz1_hand_vectors (void)
{
	// All-literal: flag byte's low 5 bits set (1=literal, LSB-first),
	// followed by the 5 literal bytes "ABCDE".
	{
		static const u8 stream[] = { 0x1f, 'A', 'B', 'C', 'D', 'E' };
		u8 out[5];
		if (DecodeALZ1 (out, 5, stream, sizeof (stream)))
			return 1;
		if (memcmp (out, "ABCDE", 5))
			return 2;
	}

	// One literal 'A', then a length-7 match reading back distance 1 --
	// expands to eight 'A's via ring self-overlap (q catches up to p).
	// p starts at 0xfee; after the literal, p=0xfee+1=0xfef, so the match
	// reads from ring address (p-1)&0xfff = 0xfee = 0xf<<8 | 0xee.
	{
		static const u8 stream[] = { 0x01, 'A', 0xee, 0xf4 }; // len-3=4 -> len=7
		u8 out[8];
		if (DecodeALZ1 (out, 8, stream, sizeof (stream)))
			return 3;
		for (int i = 0; i < 8; i++)
			if (out[i] != 'A')
				return 4;
	}

	// Leading zero run purely from the pre-zeroed ring: a single match
	// token referencing ring address 0 (far outside anything written yet)
	// for length 18 (max), decoded before any literal has ever been
	// written. GBATEK explicitly calls this out as the reason the ring
	// must start zero-filled rather than empty.
	{
		// q=0 -> byte0=0x00, byte1=((0>>8)&0xf)<<4|(18-3)=0x0f
		static const u8 stream[] = { 0x00, 0x00, 0x0f };
		u8 out[18];
		memset (out, 0xaa, sizeof (out));
		if (DecodeALZ1 (out, 18, stream, sizeof (stream)))
			return 5;
		for (int i = 0; i < 18; i++)
			if (out[i] != 0)
				return 6;
	}

	return 0;
}

// ALZ1 encode -> decode round trip across a mix of compressible and
// incompressible content, including edge sizes (0, 1, boundary lengths
// around the 18-byte max match and the 4096-byte window).
static int check_alz1_roundtrip (void)
{
	static const uint sizes[] = { 0, 1, 2, 3, 17, 18, 19, 100, 4095, 4096, 4097, 20000 };
	for (uint s = 0; s < sizeof (sizes) / sizeof (*sizes); s++)
	{
		uint n = sizes[s];
		u8 *src = n ? malloc (n) : 0;
		for (uint i = 0; i < n; i++)
			// Mix of runs, ramps and pseudo-random bytes so both the
			// literal and match paths get real exercise.
			src[i] = (u8)((i / 37) ^ (i * 2654435761u >> 24) ^ (i % 5 == 0 ? 0 : i));

		u8 *z = 0;
		uint zn = 0;
		if (EncodeALZ1 (&z, &zn, src, n))
		{
			free (src);
			return 10;
		}

		u8 *out = n ? malloc (n) : 0;
		enumError derr = DecodeALZ1 (out, n, z, zn);
		int bad = derr || (n && memcmp (out, src, n));
		free (src);
		free (out);
		free (z);
		if (bad)
			return 11;
	}
	return 0;
}

// INFO.DAT encrypt/decrypt round trip, checked against GBATEK's decrypt
// formula applied by hand for one byte, plus a full-buffer round trip.
static int check_crypt (void)
{
	u8 buf[0x40];
	memcpy (buf, "*Dr.Mario-DSi!!!", 16);
	for (uint i = 0x10; i < sizeof (buf); i++)
		buf[i] = (u8)(i * 73 + 11);

	u8 plain[sizeof (buf)];
	memcpy (plain, buf, sizeof (buf));

	u8 enc[sizeof (buf)];
	memcpy (enc, plain, sizeof (buf));
	if (EncryptArikaInfo (enc, sizeof (enc)))
		return 20;
	// Title bytes themselves are never touched.
	if (memcmp (enc, plain, 0x10))
		return 21;

	u8 dec[sizeof (buf)];
	memcpy (dec, enc, sizeof (buf));
	if (DecryptArikaInfo (dec, sizeof (dec)))
		return 22;
	if (memcmp (dec, plain, sizeof (dec)))
		return 23;

	// Hand-apply GBATEK's decrypt formula to byte 0x10 of the encrypted
	// buffer directly ("buf[i]=((buf[i] ror 4) xor FFh)-buf[i AND 0Fh]")
	// and compare against what DecryptArikaInfo() produced there -- an
	// independent check against the spec text, not just self-consistency
	// with EncryptArikaInfo().
	{
		u8 x = enc[0x10];
		u8 hand = (u8)(x >> 4 | x << 4); // ror 4
		hand ^= 0xff;
		hand = (u8)(hand - enc[0x10 & 0xf]); // key byte = enc[0] (untouched title)
		if (hand != dec[0x10])
			return 26;
	}

	// A title starting with a NUL byte means "unencrypted": Encrypt must
	// be a no-op and Decrypt must leave the buffer untouched too.
	u8 nokey[sizeof (buf)];
	memcpy (nokey, buf, sizeof (buf));
	nokey[0] = 0;
	u8 nokey_orig[sizeof (buf)];
	memcpy (nokey_orig, nokey, sizeof (buf));
	if (EncryptArikaInfo (nokey, sizeof (nokey)) || memcmp (nokey, nokey_orig, sizeof (nokey)))
		return 24;
	if (DecryptArikaInfo (nokey, sizeof (nokey)) || memcmp (nokey, nokey_orig, sizeof (nokey)))
		return 25;

	return 0;
}

static nintendo_sarc_entry_t *make_entries (uint n, uint base_size)
{
	nintendo_sarc_entry_t *e = calloc (n, sizeof (*e));
	for (uint i = 0; i < n; i++)
	{
		char name[64];
		snprintf (name, sizeof (name), "com/chr/file_%02u.dat", i);
		e[i].name = strdup (name);
		uint sz = base_size + i * 37;
		u8 *data = malloc (sz ? sz : 1);
		for (uint j = 0; j < sz; j++)
			// A run-heavy pattern so ALZ1 compression actually engages for
			// at least some members (mirrors real asset files, which are
			// rarely pure random noise).
			data[j] = (u8)(i == 0 ? 0 : (j / 23) ^ (j * 2654435761u >> 27));
		e[i].data = data;
		e[i].size = sz;
	}
	return e;
}

static void free_entries (nintendo_sarc_entry_t *e, uint n)
{
	for (uint i = 0; i < n; i++)
	{
		free ((void *)e[i].name);
		free ((void *)e[i].data);
	}
	free (e);
}

// Dr. Mario Online Rx / Dr. Mario Express style archive: encrypted
// INFO.DAT, ALZ1-compressed GAME.DAT members. CreateArika -> ExtractArika
// round trip since no retail sample was available to extract directly.
static int check_dr_mario_style_roundtrip (void)
{
	enum
	{
		N = 6
	};
	nintendo_sarc_entry_t *in = make_entries (N, 50);

	u8 *info = 0, *game = 0;
	uint info_size = 0, game_size = 0;
	// "*Dr.Mario-DSi!!!" is the exact retail title GBATEK gives for the DSi
	// Dr. Mario titles; using it here doubles as documentation of the key.
	if (CreateArika (&info, &info_size, &game, &game_size, in, N, "*Dr.Mario-DSi!!!", true))
	{
		free_entries (in, N);
		return 30;
	}

	// INFO.DAT must actually come out encrypted (title[0] != 0 requests
	// it): byte 0x10 must differ from a plain byte written at that offset
	// pre-encryption in general, and the buffer must NOT already equal its
	// own decrypted form.
	u8 *redecoded = malloc (info_size);
	memcpy (redecoded, info, info_size);
	if (DecryptArikaInfo (redecoded, info_size))
	{
		free (redecoded);
		free (info);
		free (game);
		free_entries (in, N);
		return 31;
	}
	if (!memcmp (redecoded, info, info_size))
	{
		free (redecoded);
		free (info);
		free (game);
		free_entries (in, N);
		return 32;
	} // should have changed
	free (redecoded);

	nintendo_sarc_entry_t *out = 0;
	uint n_out = 0;
	enumError err = ExtractArika (&out, &n_out, info, info_size, game, game_size);
	free (info);
	free (game);
	if (err)
	{
		free_entries (in, N);
		return 33;
	}
	if (n_out != N)
	{
		free_entries (in, N);
		free_entries (out, n_out);
		return 34;
	}

	int rc = 0;
	for (uint i = 0; i < N && !rc; i++)
	{
		if (strcmp (in[i].name, out[i].name))
			rc = 35;
		else if (in[i].size != out[i].size)
			rc = 36;
		else if (in[i].size && memcmp (in[i].data, out[i].data, in[i].size))
			rc = 37;
	}

	free_entries (in, N);
	free_entries (out, n_out);
	return rc;
}

// Same archive family but unencrypted and uncompressed (the original DS
// Endless Ocean / early titles GBATEK lists as also using this container).
static int check_unencrypted_uncompressed_roundtrip (void)
{
	enum
	{
		N = 4
	};
	nintendo_sarc_entry_t *in = make_entries (N, 30);

	u8 *info = 0, *game = 0;
	uint info_size = 0, game_size = 0;
	if (CreateArika (&info, &info_size, &game, &game_size, in, N, NULL, false))
	{
		free_entries (in, N);
		return 40;
	}
	if (info[0])
	{
		free (info);
		free (game);
		free_entries (in, N);
		return 41;
	} // no title -> unencrypted

	nintendo_sarc_entry_t *out = 0;
	uint n_out = 0;
	enumError err = ExtractArika (&out, &n_out, info, info_size, game, game_size);
	free (info);
	free (game);
	if (err || n_out != N)
	{
		free_entries (in, N);
		free_entries (out, n_out);
		return 42;
	}

	int rc = 0;
	for (uint i = 0; i < N && !rc; i++)
		if (in[i].size != out[i].size
			|| (in[i].size && memcmp (in[i].data, out[i].data, in[i].size)))
			rc = 43;

	free_entries (in, N);
	free_entries (out, n_out);
	return rc;
}

// Endless Ocean: Blue World style RF2 grouping: a hand-built GAME.DAT whose
// one directory entry is itself an "RF2" tag with three sub-members, one of
// which is a *nested* RF2 tag (grouping recurses in the real format). This
// is checked against the layout documented in aluigi's EXTRACT_RF2()
// directly, not through CreateArika (which never re-nests on its own).
static int check_rf2_grouping (void)
{
	// Sub-payload bytes for the two real leaf members.
	static const u8 leaf_a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	static const u8 leaf_b[4] = { 9, 8, 7, 6 };
	static const u8 leaf_c[3] = { 'x', 'y', 'z' };

	// Layout (offsets relative to the RF2 tag at game[0]):
	//   0x00 RF2 header (16 bytes)
	//   0x10 sub-entry 0: nested RF2 tag at 0x40 (relative to outer base)
	//   0x30 sub-entry 1: leaf_b at 0x60
	//   0x50 padding start; real data begins at fixed slots below
	// Kept simple: three top-level sub-entries, the first pointing at a
	// second, nested RF2 tag with its own single leaf.
	enum
	{
		OUT_HDR = 16,
		SUB = 0x20
	};
	enum
	{
		N_SUB = 3
	};
	// relative offsets for the 3 sub-entries' data, chosen so nothing overlaps
	enum
	{
		NEST_OFF = 0x80,
		LEAFB_OFF = 0xC0,
		LEAFC_OFF = 0xD0,
		GAME_SIZE = 0x100
	};

	u8 *game = calloc (1, GAME_SIZE);
	// outer RF2 header
	memcpy (game + 0, "RF2", 3);
	memcpy (game + 3, "TYP", 3);
	game[6] = 1;
	game[7] = 0; // VER
	u32 info_size = N_SUB * 0x20;
	game[8] = (u8)info_size;
	game[9] = (u8)(info_size >> 8);
	game[10] = (u8)(info_size >> 16);
	game[11] = 0;
	game[12] = game[13] = game[14] = game[15] = 0; // HEAD_SIZE, unused

	u8 *rec = game + 16;
	// sub 0: nested RF2 container. The SIZE field is only consulted as a
	// "walk this entry at all" gate and (for a non-RF2 leaf) as its byte
	// length -- since this entry resolves to another RF2 tag, its exact
	// value beyond non-zero doesn't matter; 1 keeps it simple.
	memcpy (rec + 0, "group_a", 7);
	rec[20] = 1;
	rec[21] = rec[22] = rec[23] = 0;
	rec[24] = (u8)NEST_OFF;
	rec[25] = rec[26] = rec[27] = 0; // OFFSET (relative)
	rec += 0x20;
	// sub 1: leaf_b
	memcpy (rec + 0, "leaf_b", 6);
	u32 sz = sizeof (leaf_b);
	rec[20] = (u8)sz;
	rec[21] = (u8)(sz >> 8);
	rec[22] = (u8)(sz >> 16);
	rec[23] = (u8)(sz >> 24);
	rec[24] = (u8)LEAFB_OFF;
	rec[25] = rec[26] = rec[27] = 0;
	rec += 0x20;
	// sub 2: leaf_c
	memcpy (rec + 0, "leaf_c", 6);
	sz = sizeof (leaf_c);
	rec[20] = (u8)sz;
	rec[21] = (u8)(sz >> 8);
	rec[22] = (u8)(sz >> 16);
	rec[23] = (u8)(sz >> 24);
	rec[24] = (u8)LEAFC_OFF;
	rec[25] = rec[26] = rec[27] = 0;

	// nested RF2 tag at NEST_OFF, one leaf sub-entry
	u8 *nest = game + NEST_OFF;
	memcpy (nest + 0, "RF2", 3);
	memcpy (nest + 3, "TYP", 3);
	nest[6] = 1;
	nest[7] = 0;
	u32 nest_info_size = 1 * 0x20;
	nest[8] = (u8)nest_info_size;
	nest[9] = (u8)(nest_info_size >> 8);
	nest[10] = 0;
	nest[11] = 0;
	nest[12] = nest[13] = nest[14] = nest[15] = 0;
	u8 *nrec = nest + 16;
	memcpy (nrec + 0, "leaf_a", 6);
	sz = sizeof (leaf_a);
	nrec[20] = (u8)sz;
	nrec[21] = (u8)(sz >> 8);
	nrec[22] = 0;
	nrec[23] = 0;
	// leaf_a is stored right after this nested RF2's own sub-table, i.e.
	// at NEST_OFF + 0x30, well clear of LEAFB_OFF/LEAFC_OFF.
	u32 leafa_rel = 0x30;
	nrec[24] = (u8)leafa_rel;
	nrec[25] = nrec[26] = nrec[27] = 0;
	memcpy (game + NEST_OFF + leafa_rel, leaf_a, sizeof (leaf_a));

	memcpy (game + LEAFB_OFF, leaf_b, sizeof (leaf_b));
	memcpy (game + LEAFC_OFF, leaf_c, sizeof (leaf_c));

	// INFO.DAT: one directory entry pointing at the whole RF2 blob, stored
	// "raw" (zsize==dsize so ExtractArika treats it as a plain slice and
	// probes it for the RF2 tag).
	u8 info[0x60];
	memset (info, 0, sizeof (info));
	// title[0]=0 -> unencrypted
	info[0x24] = 1; // sector size = 1 byte/sector, so byte offsets are exact
	info[0x2c] = 1; // one directory entry
	memcpy (info + 0x30, "group", 5);
	u32 rsize = GAME_SIZE;
	info[0x30 + 0x20] = (u8)rsize;
	info[0x30 + 0x21] = (u8)(rsize >> 8);
	info[0x30 + 0x22] = (u8)(rsize >> 16);
	info[0x30 + 0x23] = (u8)(rsize >> 24);
	info[0x30 + 0x24] = 0; // offset in sectors (=bytes here) = 0
	info[0x30 + 0x2c] = (u8)rsize;
	info[0x30 + 0x2d] = (u8)(rsize >> 8); // dsize == zsize -> "raw"

	nintendo_sarc_entry_t *out = 0;
	uint n_out = 0;
	enumError err = ExtractArika (&out, &n_out, info, sizeof (info), game, GAME_SIZE);
	free (game);
	if (err)
		return 50;
	// Expect: group/group_a/leaf_a, group/leaf_b, group/leaf_c
	if (n_out != 3)
	{
		free_entries (out, n_out);
		return 51;
	}

	int found_a = 0, found_b = 0, found_c = 0;
	for (uint i = 0; i < n_out; i++)
	{
		if (!strcmp (out[i].name, "group/group_a/leaf_a"))
		{
			if (out[i].size != sizeof (leaf_a) || memcmp (out[i].data, leaf_a, sizeof (leaf_a)))
				return (free_entries (out, n_out), 52);
			found_a = 1;
		}
		else if (!strcmp (out[i].name, "group/leaf_b"))
		{
			if (out[i].size != sizeof (leaf_b) || memcmp (out[i].data, leaf_b, sizeof (leaf_b)))
				return (free_entries (out, n_out), 53);
			found_b = 1;
		}
		else if (!strcmp (out[i].name, "group/leaf_c"))
		{
			if (out[i].size != sizeof (leaf_c) || memcmp (out[i].data, leaf_c, sizeof (leaf_c)))
				return (free_entries (out, n_out), 54);
			found_c = 1;
		}
	}
	free_entries (out, n_out);
	if (!found_a || !found_b || !found_c)
		return 55;
	return 0;
}

int main (void)
{
	struct
	{
		ccp name;
		int (*fn) (void);
	} tests[] = {
		{ "ALZ1 hand-built vectors (vs GBATEK pseudocode)", check_alz1_hand_vectors },
		{ "ALZ1 encode/decode round trip", check_alz1_roundtrip },
		{ "INFO.DAT encrypt/decrypt (rotate/xor/subtract) round trip", check_crypt },
		{ "Dr. Mario style archive (encrypted INFO.DAT, ALZ1 GAME.DAT) round trip",
			check_dr_mario_style_roundtrip },
		{ "Unencrypted/uncompressed archive round trip (DS Endless Ocean style)",
			check_unencrypted_uncompressed_roundtrip },
		{ "RF2 nested grouping (Endless Ocean: Blue World style)", check_rf2_grouping },
	};
	int rc = 0;
	for (uint i = 0; i < sizeof (tests) / sizeof (*tests); i++)
	{
		int r = tests[i].fn ();
		printf ("%s %s (rc=%d)\n", r ? "FAIL" : "ok  ", tests[i].name, r);
		if (r)
			rc = 1;
	}
	return rc;
}
