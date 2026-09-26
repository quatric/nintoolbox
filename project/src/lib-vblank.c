// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Vblank Entertainment Wii package scanners; see lib-vblank.h for the layouts.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-vblank.h"
#include "lib-dspadpcm.h"
#include <string.h>
#include <ctype.h>
#include <zlib.h>

#define VB_MAX_ENTRIES 4096
#define VB_MAX_SIZE 0x10000000u

static const u32 vb_table[256] = { 0x3c5f0ceb, 0x67465cea, 0x09bb5a2c, 0x377b67b7, 0x00563bf7,
	0x62707630, 0x5b823037, 0x79257731, 0x086c2b35, 0x475a04fd, 0x4ef43a04, 0x63023acb, 0x7cbc241c,
	0x4ff50381, 0x467f5045, 0x0be545bf, 0x53a570b7, 0x1693111f, 0x28143a7f, 0x4a4230bb, 0x60482c24,
	0x186c46f0, 0x73a6278b, 0x7eb55662, 0x710d6318, 0x481d5c40, 0x69af7616, 0x6e795fca, 0x7e1f0459,
	0x30241b10, 0x41aa4d21, 0x2df220fc, 0x67eb2142, 0x25652492, 0x709a0482, 0x1b244577, 0x45a57e43,
	0x09a805b0, 0x7f825d62, 0x68394cad, 0x43c3735e, 0x1e191b86, 0x71eb0abd, 0x35ff5684, 0x12801cab,
	0x65c647e3, 0x3c6109e5, 0x3e673fd6, 0x645a5ad5, 0x182575cc, 0x4af64afe, 0x0f070ef2, 0x7c94239b,
	0x238b6df9, 0x17bd2983, 0x53993019, 0x4db52250, 0x3fb67b56, 0x515034c0, 0x307a0202, 0x2807285c,
	0x10445f83, 0x114b4857, 0x2d2c3256, 0x781a6db8, 0x57391754, 0x22cf74bc, 0x2ed56a34, 0x153e2175,
	0x377d6f52, 0x590037b8, 0x02bd4aae, 0x200c4a36, 0x075c6838, 0x758704e9, 0x78d4394b, 0x70de76b2,
	0x33046b77, 0x1f111e40, 0x0e297c83, 0x16523e33, 0x2f0b4fb2, 0x67ce3c81, 0x27742846, 0x63cb1119,
	0x7ae62d43, 0x63f307c7, 0x5b8d5a75, 0x0fef5958, 0x337302b4, 0x50385ffe, 0x4df34767, 0x632a6af6,
	0x35702947, 0x485b7567, 0x19471666, 0x762a448e, 0x5001196f, 0x0d9b3118, 0x49ce0e2f, 0x00621fcf,
	0x072f7f54, 0x5d3d6d79, 0x67f21175, 0x368751fd, 0x66631f53, 0x570b7ec8, 0x12c16b5f, 0x39167c6f,
	0x62ef0a7c, 0x73d00b96, 0x2a6c6c05, 0x12cb0d10, 0x4e852312, 0x0bdd3548, 0x0ac954f7, 0x472b0edf,
	0x59be710e, 0x6d871096, 0x55d215b7, 0x70fc4a6f, 0x2694469c, 0x03a66e0f, 0x4e24583a, 0x70c73667,
	0x6ae8349d, 0x0c187293, 0x41815d6a, 0x2b5d3802, 0x22fe4f23, 0x5e3c7fd8, 0x034f29a7, 0x584f3390,
	0x53fc41f8, 0x41786ce6, 0x77170141, 0x60756cf4, 0x5e6f3518, 0x53b40e9b, 0x2b063500, 0x4c683824,
	0x60c50132, 0x7fdc1027, 0x026f3e9d, 0x430779ac, 0x29d4342b, 0x04621b91, 0x70472d46, 0x17f6772c,
	0x3b51659b, 0x09b95230, 0x41a7621d, 0x6a1a77d5, 0x5c5a5b4d, 0x48db1533, 0x784e1cb9, 0x521f34ef,
	0x3bed7dc3, 0x41c41e1a, 0x351c57a4, 0x20f21a56, 0x236e1cb1, 0x01f4673b, 0x329874dc, 0x2e4756f9,
	0x3926037d, 0x7af1643e, 0x4f6c3a53, 0x37143d5a, 0x52be5dc5, 0x68c30aa1, 0x28e41e6e, 0x4c147410,
	0x57c86bd9, 0x48772a34, 0x45726489, 0x50467648, 0x3436073d, 0x5e9d159e, 0x4f2c0972, 0x076b6440,
	0x5ae17728, 0x4dc91ad6, 0x5e4c7fe9, 0x348b23ca, 0x58041508, 0x3d154bab, 0x53b03d27, 0x487150cf,
	0x73be40fb, 0x0e9e163d, 0x43581554, 0x202a7dc9, 0x64932657, 0x26032d81, 0x6eea680e, 0x538a4448,
	0x11ec5024, 0x3ee941c1, 0x50311ce9, 0x13a6256f, 0x66920d9c, 0x5379091a, 0x33996fea, 0x195b3a74,
	0x333726b2, 0x12e017fc, 0x62b50e0b, 0x23c63523, 0x20ed6088, 0x67ce09ad, 0x5eba01bb, 0x06ca305b,
	0x33ad51f7, 0x0ef878c7, 0x2b026f5a, 0x498e508f, 0x5cd2080b, 0x3d9647b5, 0x278a21c1, 0x54fc3446,
	0x1d9b7a85, 0x57e6393a, 0x7b7366b8, 0x3243349c, 0x39ad5057, 0x37a758eb, 0x0f843b7e, 0x595675bf,
	0x798e742b, 0x029f33af, 0x18a74945, 0x0f6b4773, 0x7d2a78dd, 0x11156046, 0x326831b3, 0x557c558e,
	0x1e524dfc, 0x645757bf, 0x09792b62, 0x66c9287d, 0x6339444d, 0x2d361e01, 0x16306e61, 0x475475bd,
	0x00f56248, 0x62853a43, 0x670770b1, 0x62644063, 0x6e040898, 0x679d7f93, 0x7b1c72c8, 0x39034995,
	0x04c4669f, 0x42dc2553, 0x2caf5c12 };

#include "lib-vblank-names.inc"

static u32 vb_rd32 (const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}
static u32 vb_rd32be (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static u32 vb_hash (ccp name)
{
	u32 h = 0;
	for (const u8 *p = (const u8 *)name; *p; p++)
		h = (h << 1) ^ vb_table[(h & 255) ^ toupper (*p)];
	return h;
}

static ccp vb_name (u32 hash)
{
	for (uint i = 0; i < sizeof (vb_names) / sizeof (*vb_names); i++)
		if (vb_names[i].hash == hash)
			return vb_names[i].name;
	return 0;
}

// Inflate a zlib stream to exactly 'size' bytes; returns an owned buffer.
static u8 *vb_inflate (const u8 *src, size_t src_size, size_t size)
{
	u8 *out = MALLOC (size ? size : 1);
	if (!out)
		return 0;
	uLongf n = size;
	if (uncompress (out, &n, src, (uLong)src_size) != Z_OK || n != size)
	{
		FREE (out);
		return 0;
	}
	return out;
}

static ccp vb_ext (const u8 *d, uint n)
{
	if (n >= 4
		&& !memcmp (d,
			"\xff"
			"REC",
			4))
		return ".rec";
	if (n >= 4
		&& !memcmp (d,
			"\xff"
			"MAP",
			4))
		return ".map";
	if (n >= 4
		&& !memcmp (d,
			"\xff"
			"TIL",
			4))
		return ".til";
	if (n >= 4
		&& !memcmp (d,
			"\xff"
			"BMD",
			4))
		return ".bmd";
	if (n >= 4
		&& !memcmp (d,
			"\xff"
			"AN2",
			4))
		return ".an2";
	if (n >= 4
		&& !memcmp (d,
			"\xff"
			"PFX",
			4))
		return ".pfx";
	if (n >= 4
		&& !memcmp (d,
			"\xff"
			"FRC",
			4))
		return ".frc";
	if (n >= 4 && !memcmp (d, "BTRK", 4))
		return ".btrk";
	if (n >= 4 && !memcmp (d, "OggS", 4))
		return ".ogg";
	if (n >= 4 && !memcmp (d, "RIFF", 4))
		return ".wav";
	return ".bin";
}

// Name of the member ('name.ext' if known, else 'hash.ext'); ext comes from the content.
static void vb_member_name (
	char *dest, size_t sz, u32 hash, uint idx, bool slot, const u8 *d, uint n)
{
	ccp nm = slot ? 0 : vb_name (hash);
	if (nm)
		snprintf (dest, sz, "%s", nm);
	else if (slot)
		snprintf (dest, sz, "slot_%03u%s", idx, vb_ext (d, n));
	else
		snprintf (dest, sz, "%08x%s", hash, vb_ext (d, n));
}

//-----------------------------------------------------------------------------

bool IsVblankBfp (const u8 *data, size_t size)
{
	if (!data || size < 0x40 + 256 * 12 || memcmp (data, "BFP2", 4))
		return false;
	const u32 n = vb_rd32 (data + 4);
	return n && n <= 192 && 0x40 + 16ull * n + 256 * 12 <= size;
}

// One member: stored (size == stored) or zlib. Adds it to 'out' under the name
// derived from its hash / slot index and content.
static bool vb_add_member (nintendo_sarc_entry_t *out, uint *n, const u8 *data, size_t total,
	u32 hash, uint idx, bool slot, u32 off, u32 size, u32 stored)
{
	if (!off || size > VB_MAX_SIZE || stored > VB_MAX_SIZE || (u64)off + stored > total)
		return false;
	u8 *plain = 0;
	const u8 *body = data + off;
	if (size != stored)
	{
		plain = vb_inflate (data + off, stored, size);
		if (!plain)
			return false;
		body = plain;
	}
	char name[80];
	vb_member_name (name, sizeof (name), hash, idx, slot, body, size);
	bool ok = OwnedEntryAdd (out, *n, name, body, size);
	FREE (plain);
	if (ok)
		(*n)++;
	return ok;
}

enumError ScanVblankBfp (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !IsVblankBfp (data, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const uint cnt = vb_rd32 (data + 4);
	nintendo_sarc_entry_t *out = CALLOC (cnt + 256 + 1, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint n = 0;
	for (uint i = 0; i < cnt; i++)
	{
		const u8 *e = data + 0x40 + 16 * i;
		if (!vb_add_member (out, &n, data, size, vb_rd32 (e), i, false, vb_rd32 (e + 4),
				vb_rd32 (e + 8), vb_rd32 (e + 12)))
			goto fail;
	}
	for (uint i = 0; i < 256; i++)
	{
		const u8 *e = data + 0x40 + 16ull * cnt + 12 * i;
		if (!vb_rd32 (e) && !vb_rd32 (e + 4) && !vb_rd32 (e + 8))
			continue;
		if (!vb_add_member (
				out, &n, data, size, 0, i, true, vb_rd32 (e), vb_rd32 (e + 4), vb_rd32 (e + 8)))
			goto fail;
	}
	if (!n)
		goto fail;
	*entries = out;
	*n_entries = n;
	return ERR_OK;

fail:
	ResetOwnedEntries (out, n);
	return EINVAL;
}

//-----------------------------------------------------------------------------

bool IsVblankBap (const u8 *data, size_t size)
{
	if (!data || size < 0x40)
		return false;
	if (!memcmp (data, "BPP3", 4))
	{
		const u32 base = vb_rd32 (data + 4), tab = vb_rd32 (data + 0x14),
				  n = data[0x10] | data[0x11] << 8;
		return n && n < VB_MAX_ENTRIES && base <= size && tab >= 0x40 && tab + 12ull * n <= size;
	}
	if (!memcmp (data, "BAP1", 4))
	{
		const u32 n = data[6] | data[7] << 8;
		return n && 0x20 + 16ull * n <= size;
	}
	return false;
}

// Mono DSP-ADPCM stream (0x60 byte big-endian header + frames) -> owned 16-bit WAV.
static u8 *vb_dsp_wav (const u8 *d, u32 size, size_t *wav_size)
{
	if (size < 0x60)
		return 0;
	const u32 samples = vb_rd32be (d), rate = vb_rd32be (d + 8);
	if (!samples || samples > 0x4000000 || DspAdpcmByteCount (samples) > (s64)size - 0x60 + 8)
		return 0;
	// The last frame is stored short; decode from a zero-padded copy.
	const size_t adpcm_bytes = (size_t)DspAdpcmByteCount (samples);
	u8 *adpcm = CALLOC (1, adpcm_bytes + 8);
	if (!adpcm)
		return 0;
	memcpy (adpcm, d + 0x60, size - 0x60 < adpcm_bytes ? size - 0x60 : adpcm_bytes);
	s16 coefs[16];
	for (uint i = 0; i < 16; i++)
		coefs[i] = (s16)(d[0x1c + 2 * i] << 8 | d[0x1d + 2 * i]);
	int h1 = (s16)(d[0x40] << 8 | d[0x41]), h2 = (s16)(d[0x42] << 8 | d[0x43]);
	const size_t bytes = 44 + (size_t)samples * 2;
	u8 *w = MALLOC (bytes);
	if (!w)
	{
		FREE (adpcm);
		return 0;
	}
	memcpy (w, "RIFF", 4);
	const u32 riff = (u32)(bytes - 8);
	w[4] = riff, w[5] = riff >> 8, w[6] = riff >> 16, w[7] = riff >> 24;
	memcpy (w + 8, "WAVEfmt ", 8);
	const u32 fmt[] = { 16, 0x10001, rate, rate * 2 };
	for (uint i = 0; i < 4; i++)
		for (uint k = 0; k < 4; k++)
			w[16 + 4 * i + k] = fmt[i] >> (8 * k);
	w[32] = 2, w[33] = 0, w[34] = 16, w[35] = 0;
	memcpy (w + 36, "data", 4);
	const u32 dsz = samples * 2;
	w[40] = dsz, w[41] = dsz >> 8, w[42] = dsz >> 16, w[43] = dsz >> 24;
	for (u32 done = 0, f = 0; done < samples; f++)
	{
		s16 tmp[14];
		const uint cnt = samples - done < 14 ? samples - done : 14;
		DspAdpcmDecodeBlock (adpcm + (size_t)f * 8, cnt, tmp, coefs, &h1, &h2);
		for (uint k = 0; k < cnt; k++)
		{
			w[44 + 2 * (done + k)] = tmp[k];
			w[45 + 2 * (done + k)] = tmp[k] >> 8;
		}
		done += cnt;
	}
	FREE (adpcm);
	*wav_size = bytes;
	return w;
}

// Read a length-prefixed (incl. NUL) string at *p; returns false at the end of data.
static bool vb_pstring (const u8 *data, size_t size, size_t *p, char *dest, size_t dsz)
{
	if (*p >= size)
		return false;
	const uint len = data[*p];
	if (!len || *p + 1 + len > size)
		return false;
	uint k = 0;
	for (uint i = 0; i + 1 < len && k + 1 < dsz; i++)
	{
		const u8 c = data[*p + 1 + i];
		dest[k++] = isalnum (c) || c == '-' || c == '.' ? c : '_';
	}
	dest[k] = 0;
	*p += 1 + len;
	return true;
}

static enumError vb_scan_bpp3 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	const u32 base = vb_rd32 (data + 4), tab = vb_rd32 (data + 0x14),
			  cnt = data[0x10] | data[0x11] << 8;
	nintendo_sarc_entry_t *out = CALLOC (cnt * 8 + 1, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint n = 0, cap = cnt * 8;
	for (uint i = 0; i < cnt; i++)
	{
		const u8 *r = data + tab + 12 * i;
		const u32 hdr = vb_rd32 (r), meta = vb_rd32 (r + 8);
		if (!hdr)
			continue;
		if ((u64)hdr + 8 > size)
			goto fail;
		const uint ns = data[hdr];
		char title[64] = "";
		if (meta && (u64)meta + 4 < size)
		{
			char artist[64];
			size_t p = meta + 4;
			if (vb_pstring (data, size, &p, artist, sizeof (artist)))
				vb_pstring (data, size, &p, title, sizeof (title));
		}
		for (uint s = 0; s < ns && n < cap; s++)
		{
			if ((u64)hdr + 8 + 4 * (s + 1) > size)
				goto fail;
			const u32 sd = vb_rd32 (data + hdr + 8 + 4 * s);
			if ((u64)sd + 24 > size)
				goto fail;
			const u32 kind = vb_rd32 (data + sd), sz = vb_rd32 (data + sd + 4),
					  off = vb_rd32 (data + sd + 20);
			if ((u64)base + off + sz > size)
				goto fail;
			const u8 *d = data + base + off;
			char name[160];
			int len = snprintf (name, sizeof (name), "%03u", i);
			if (*title)
				len += snprintf (name + len, sizeof (name) - len, "_%s", title);
			if (ns > 1)
				len += snprintf (name + len, sizeof (name) - len, "_%u", s);
			u8 *wav = 0;
			size_t wsz = 0;
			ccp ext = ".bin";
			if (kind == 0x100 && sz >= 4 && !memcmp (d, "OggS", 4))
				ext = ".ogg";
			else if (kind == 0x20 && (wav = vb_dsp_wav (d, sz, &wsz)))
				ext = ".wav";
			snprintf (name + len, sizeof (name) - len, "%s", ext);
			const bool ok = wav ? OwnedEntryAdd (out, n, name, wav, (uint)wsz)
								: OwnedEntryAdd (out, n, name, d, sz);
			FREE (wav);
			if (!ok)
				goto fail;
			n++;
		}
	}
	if (!n)
		goto fail;
	*entries = out;
	*n_entries = n;
	return ERR_OK;

fail:
	ResetOwnedEntries (out, n);
	return EINVAL;
}

static enumError vb_scan_bap1 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	const uint cnt = data[6] | data[7] << 8;
	const u32 pool = vb_rd32 (data + 0x18);
	nintendo_sarc_entry_t *out = CALLOC (cnt + 2, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint n = 0;
	for (uint i = 0; i < cnt; i++)
	{
		const u8 *r = data + 0x20 + 16 * i;
		const u32 off = vb_rd32 (r), sz = vb_rd32 (r + 4);
		if (!off && !sz)
			continue;
		if ((u64)off + 8 > size)
			goto fail;
		const u32 usz = vb_rd32 (data + off), st = vb_rd32 (data + off + 4);
		char name[32];
		if (usz != sz)
			goto fail;
		u8 *plain = 0;
		const u8 *body = data + off + 8;
		if (usz != st)
		{
			if ((u64)off + 8 + st > size || !(plain = vb_inflate (body, st, usz)))
				goto fail;
			body = plain;
		}
		else if ((u64)off + 8 + usz > size)
			goto fail;
		snprintf (name, sizeof (name), "%03u%s", i, vb_ext (body, usz));
		const bool ok = OwnedEntryAdd (out, n, name, body, usz);
		FREE (plain);
		if (!ok)
			goto fail;
		n++;
	}
	// Raw sample pool shared by the modules (8-bit PCM referenced by pool_off).
	if (pool && pool < size
		&& OwnedEntryAdd (out, n, "samples.raw", data + pool, (uint)(size - pool)))
		n++;
	if (!n)
		goto fail;
	*entries = out;
	*n_entries = n;
	return ERR_OK;

fail:
	ResetOwnedEntries (out, n);
	return EINVAL;
}

enumError ScanVblankBap (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !IsVblankBap (data, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;
	return !memcmp (data, "BPP3", 4) ? vb_scan_bpp3 (entries, n_entries, data, size)
									 : vb_scan_bap1 (entries, n_entries, data, size);
}
