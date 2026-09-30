// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nintendo DSi ("TWL") title packaging and crypto; see lib-twl.h for the
// layouts. The AES construction is a port of twltool's dsi.c / f_xy.c.
//-----------------------------------------------------------------------------
#include "lib-twl.h"
#include "lib-aes.h"
#include "lib-std.h"
#include "crypt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

//-----------------------------------------------------------------------------
// small helpers
//-----------------------------------------------------------------------------

static u32 tw_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static u16 tw_be16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}
static u64 tw_be64 (const u8 *p)
{
	return (u64)tw_be32 (p) << 32 | tw_be32 (p + 4);
}
static u32 tw_le32 (const u8 *p)
{
	return (u32)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0];
}
static u64 tw_le64 (const u8 *p)
{
	u64 v = 0;
	for (int i = 7; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}
static void tw_put_le64 (u8 *p, u64 v)
{
	for (int i = 0; i < 8; i++, v >>= 8)
		p[i] = (u8)v;
}
static void tw_put_le32 (u8 *p, u32 v)
{
	for (int i = 0; i < 4; i++, v >>= 8)
		p[i] = (u8)v;
}

//-----------------------------------------------------------------------------
// key scrambler
//-----------------------------------------------------------------------------

void TwlKeyXY (u8 key[16], const u8 keyx[16], const u8 keyy[16])
{
	static const u32 c[4] = { 0x1a4f3e79, 0x2a680f5f, 0x29590258, 0xfffefb4e };
	u8 cb[16], xy[16];
	for (int i = 0; i < 4; i++)
		tw_put_le32 (cb + 4 * i, c[i]);
	for (int i = 0; i < 16; i++)
		xy[i] = keyx[i] ^ keyy[i];

	const u64 a0 = tw_le64 (cb), a1 = tw_le64 (cb + 8);
	const u64 s0 = a0 + tw_le64 (xy);
	const u64 s1 = a1 + tw_le64 (xy + 8) + (s0 < a0);

	// 128-bit rotate left by 42
	tw_put_le64 (key, s0 << 42 | s1 >> 22);
	tw_put_le64 (key + 8, s1 << 42 | s0 >> 22);
}

//-----------------------------------------------------------------------------
// twltool's "dsi_context": AES-CTR in a byte-reversed domain
//-----------------------------------------------------------------------------

typedef struct tw_ctx_t
{
	aes128_ctx_t aes;
	u8 ctr[16];
	u8 mac[16];
	u8 s0[16];
} tw_ctx_t;

static void tw_set_key (tw_ctx_t *c, const u8 key[16])
{
	u8 k[16];
	for (int i = 0; i < 16; i++)
		k[i] = key[15 - i];
	AES128_Init (&c->aes, k);
}

static void tw_set_ctr (tw_ctx_t *c, const u8 ctr[16])
{
	for (int i = 0; i < 16; i++)
		c->ctr[i] = ctr[15 - i];
}

// 128-bit big-endian increment of the (already reversed) counter
static void tw_inc_ctr (tw_ctx_t *c)
{
	for (int i = 15; i >= 0; i--)
		if (++c->ctr[i])
			break;
}

// Keystream block XOR 'in' (NULL = pure keystream), then advance the counter.
static void tw_crypt_block (tw_ctx_t *c, const u8 *in, u8 *out)
{
	u8 stream[16];
	memcpy (stream, c->ctr, 16);
	AES128_EncryptBlock (&c->aes, stream);
	for (int i = 0; i < 16; i++)
		out[i] = in ? (u8)(stream[15 - i] ^ in[i]) : stream[15 - i];
	tw_inc_ctr (c);
}

static void tw_ccm_init (tw_ctx_t *c, const u8 key[16], u32 payload_len, const u8 nonce[12])
{
	tw_set_key (c, key);
	payload_len = (payload_len + 15) & ~15u;

	// CCM B0 block: flags (MAC length 16, no associated data), nonce, size.
	c->mac[0] = (u8)(((16 - 2) / 2) << 3 | 2);
	for (int i = 0; i < 12; i++)
		c->mac[1 + i] = nonce[11 - i];
	c->mac[13] = (u8)(payload_len >> 16);
	c->mac[14] = (u8)(payload_len >> 8);
	c->mac[15] = (u8)payload_len;
	AES128_EncryptBlock (&c->aes, c->mac);

	// CCM counter block 0: flags, nonce, 24-bit counter; block 0 encrypts the MAC.
	c->ctr[0] = 2;
	for (int i = 0; i < 12; i++)
		c->ctr[1 + i] = nonce[11 - i];
	c->ctr[13] = c->ctr[14] = c->ctr[15] = 0;
	tw_crypt_block (c, 0, c->s0);
}

// Fold one plaintext block into the CBC-MAC.
static void tw_mac_block (tw_ctx_t *c, const u8 plain[16])
{
	for (int i = 0; i < 16; i++)
		c->mac[i] ^= plain[15 - i];
	AES128_EncryptBlock (&c->aes, c->mac);
}

static void tw_mac_out (const tw_ctx_t *c, u8 mac[16])
{
	for (int i = 0; i < 16; i++)
		mac[i] = c->mac[15 - i] ^ c->s0[i];
}

// CCM over 'size' bytes, in place. dir 0 = decrypt, 1 = encrypt.
static void tw_ccm_crypt (tw_ctx_t *c, u8 *buf, u32 size, bool encrypt, u8 mac[16])
{
	u8 block[16], out[16];
	while (size)
	{
		const u32 n = size < 16 ? size : 16;
		memset (block, 0, 16);
		memcpy (block, buf, n);
		if (encrypt)
		{
			tw_mac_block (c, block);
			tw_crypt_block (c, block, out);
			memcpy (buf, out, n);
		}
		else
		{
			tw_crypt_block (c, block, out);
			// a short tail is zero padded *after* decryption for the MAC
			memset (out + n, 0, 16 - n);
			tw_mac_block (c, out);
			memcpy (buf, out, n);
		}
		buf += n;
		size -= n;
	}
	tw_mac_out (c, mac);
}

//-----------------------------------------------------------------------------
// ES blocks
//-----------------------------------------------------------------------------

int TwlEsDecrypt (const u8 key[16], u8 *buf, const u8 meta[32], u32 size)
{
	tw_ctx_t c;
	memset (&c, 0, sizeof (c));
	u8 ctr[16], info[16], gen[16];
	memcpy (ctr, meta + 16, 16);
	ctr[0] = 0;
	ctr[13] = ctr[14] = ctr[15] = 0;
	tw_set_key (&c, key);
	tw_set_ctr (&c, ctr);
	tw_crypt_block (&c, meta + 16, info);

	if (info[0] != 0x3a)
		return -1;
	if (((u32)info[13] << 16 | info[14] << 8 | info[15]) != size)
		return -2;

	tw_ctx_t d;
	memset (&d, 0, sizeof (d));
	tw_ccm_init (&d, key, size, meta + 17);
	tw_ccm_crypt (&d, buf, size, false, gen);
	return memcmp (gen, meta, 16) ? -3 : 0;
}

void TwlEsEncrypt (const u8 key[16], u8 *buf, u8 meta[32], u32 size, const u8 nonce[12])
{
	static const u8 zero[12] = { 0 };
	if (!nonce)
		nonce = zero;

	tw_ctx_t d;
	memset (&d, 0, sizeof (d));
	u8 mac[16];
	tw_ccm_init (&d, key, size, nonce);
	tw_ccm_crypt (&d, buf, size, true, mac);

	u8 info[16] = { 0 };
	info[0] = 0x3a;
	info[13] = (u8)(size >> 16);
	info[14] = (u8)(size >> 8);
	info[15] = (u8)size;
	u8 ctr[16] = { 0 };
	memcpy (ctr + 1, nonce, 12);

	tw_ctx_t c;
	memset (&c, 0, sizeof (c));
	tw_set_key (&c, key);
	tw_set_ctr (&c, ctr);
	tw_crypt_block (&c, info, meta + 16);
	memcpy (meta + 17, nonce, 12);
	memcpy (meta, mac, 16);
}

//-----------------------------------------------------------------------------
// keys
//-----------------------------------------------------------------------------

// Constant ("fixed") ES key used for the banner, header, footer and saves.
static const u8 tw_fix_key[16]
	= { 0x3d, 0xa3, 0xea, 0x33, 0x4c, 0x86, 0xa6, 0xb0, 0x2a, 0xae, 0xdb, 0x51, 0x16, 0xea, 0x92, 0x62 };

static const u8 tw_var_keyy[16]
	= { 0xcc, 0xfc, 0xa7, 0x03, 0x20, 0x61, 0xbe, 0x84, 0xd3, 0xeb, 0xa4, 0x26, 0xb8, 0x6d, 0xbe, 0xc2 };

void TwlTadVarKey (u8 key[16], const u8 console_id[8])
{
	// twltool word-swaps the pasted ID: w[0] = BE(bytes 4..7), w[1] = BE(bytes 0..3)
	const u32 w0 = tw_be32 (console_id + 4), w1 = tw_be32 (console_id);
	u8 x[16];
	tw_put_le32 (x, 0x4e00004a);
	tw_put_le32 (x + 4, 0x4a00004e);
	tw_put_le32 (x + 8, w1 ^ 0xc80c4b72);
	tw_put_le32 (x + 12, w0);
	TwlKeyXY (key, x, tw_var_keyy);
}

// DSi common keys tried against a .tad's wrapped title key.
static const u8 tw_common_keys[3][16] = {
	{ 0xaf, 0x1b, 0xf5, 0x16, 0xa8, 0x07, 0xd2, 0x1a, 0xea, 0x45, 0x98, 0x4f, 0x04, 0x74, 0x28, 0x61 },
	{ 0xa2, 0xfd, 0xdd, 0xf2, 0xe4, 0x23, 0x57, 0x4a, 0xe7, 0xed, 0x86, 0x57, 0xb5, 0xab, 0x19, 0xd3 },
	{ 0xa1, 0x60, 0x4a, 0x6a, 0x71, 0x23, 0xb5, 0x29, 0xae, 0x8b, 0xec, 0x32, 0xc8, 0x16, 0xfc, 0xaa },
};

//-----------------------------------------------------------------------------
// modcrypt
//-----------------------------------------------------------------------------

#define SRL_HEAD 0x360

bool TwlSrlIsModcrypted (const u8 *srl, size_t size)
{
	return srl && size >= SRL_HEAD && (srl[0x12] & 2) && (srl[0x1c] & 2)
		&& (tw_le32 (srl + 0x220) || tw_le32 (srl + 0x228));
}

static bool tw_modcrypt_area (tw_ctx_t *c, u8 *srl, size_t size, u32 off, u32 len)
{
	if (!off)
		return true;
	if (off > size || len > size - off)
		return false;
	for (u32 i = 0; i + 16 <= len; i += 16)
		tw_crypt_block (c, srl + off + i, srl + off + i);
	return true;
}

static u16 tw_crc16 (const u8 *p, size_t n)
{
	u16 crc = 0xffff;
	for (size_t i = 0; i < n; i++)
	{
		crc ^= p[i];
		for (int b = 0; b < 8; b++)
			crc = crc & 1 ? (u16)(crc >> 1 ^ 0xa001) : (u16)(crc >> 1);
	}
	return crc;
}

bool TwlSrlClearModcryptFlag (u8 *srl, size_t size)
{
	if (!srl || size < SRL_HEAD)
		return false;
	// only touch the header CRC16 (0x15e, over 0..0x15d) if it was valid
	const bool crc_ok = tw_crc16 (srl, 0x15e) == (u16)(srl[0x15e] | srl[0x15f] << 8);
	srl[0x1c] &= (u8)~2;
	if (crc_ok)
	{
		const u16 crc = tw_crc16 (srl, 0x15e);
		srl[0x15e] = (u8)crc;
		srl[0x15f] = (u8)(crc >> 8);
	}
	return true;
}

bool TwlSrlModcrypt (u8 *srl, size_t size)
{
	if (!srl || size < SRL_HEAD)
		return false;

	u8 keyx[16], key[16];
	memcpy (keyx, "Nintendo", 8);
	memcpy (keyx + 8, srl + 0x0c, 4);
	keyx[12] = srl[0x0f];
	keyx[13] = srl[0x0e];
	keyx[14] = srl[0x0d];
	keyx[15] = srl[0x0c];

	// header bit 2 / 0x1bf bit 7 select the debug key, which is the header's
	// own first 16 bytes; retail goes through the key scrambler.
	if ((srl[0x1c] & 4) || (srl[0x1bf] & 0x80))
		memcpy (key, srl, 16);
	else
		TwlKeyXY (key, keyx, srl + 0x350);

	tw_ctx_t c;
	memset (&c, 0, sizeof (c));
	tw_set_key (&c, key);

	const u32 off0 = tw_le32 (srl + 0x220), len0 = tw_le32 (srl + 0x224);
	const u32 off1 = tw_le32 (srl + 0x228), len1 = tw_le32 (srl + 0x22c);
	if ((off0 && (off0 > size || len0 > size - off0)) || (off1 && (off1 > size || len1 > size - off1)))
		return false;

	tw_set_ctr (&c, srl + 0x300);
	tw_modcrypt_area (&c, srl, size, off0, len0);
	tw_set_ctr (&c, srl + 0x314);
	tw_modcrypt_area (&c, srl, size, off1, len1);
	return true;
}

//-----------------------------------------------------------------------------
// output list
//-----------------------------------------------------------------------------

typedef struct tw_out_t
{
	nintendo_sarc_entry_t *v;
	uint used, alloc;
} tw_out_t;

// Takes ownership of 'data'; copies 'name'.
static bool tw_emit (tw_out_t *o, ccp name, u8 *data, uint size)
{
	if (!data)
		return false;
	if (o->used == o->alloc)
	{
		const uint want = o->alloc ? o->alloc * 2 : 16;
		nintendo_sarc_entry_t *nv = REALLOC (o->v, want * sizeof (*nv));
		if (!nv)
		{
			FREE (data);
			return false;
		}
		o->v = nv;
		o->alloc = want;
	}
	o->v[o->used].name = STRDUP (name);
	o->v[o->used].data = data;
	o->v[o->used].size = size;
	o->used++;
	return true;
}

static u8 *tw_dup (const u8 *p, size_t n)
{
	u8 *r = MALLOC (n ? n : 1);
	if (r && n)
		memcpy (r, p, n);
	return r;
}

// Finish: hand over the list or report nothing.
static enumError tw_finish (tw_out_t *o, nintendo_sarc_entry_t **entries, uint *n_entries)
{
	if (!o->used)
	{
		FREE (o->v);
		return ERR_NOTHING_TO_DO;
	}
	*entries = o->v;
	*n_entries = o->used;
	return ERR_OK;
}

// SRL sanity: a real one has the Nintendo logo CRC at 0x15c and 7 zero bytes at 0x15.
static bool tw_looks_like_srl (const u8 *p, size_t size)
{
	if (size < 0x200)
		return false;
	for (int i = 0x15; i < 0x1c; i++)
		if (p[i])
			return false;
	return true;
}

//-----------------------------------------------------------------------------
// .tad (WAD-like installable title)
//-----------------------------------------------------------------------------

#define TAD_MAX_CONTENTS 64
#define TAD_MAX_CONTENT 0x20000000u

typedef struct tad_map_t
{
	u32 cert, crl, tik, tmd, data, footer; // sizes
	u64 o_cert, o_crl, o_tik, o_tmd, o_data, o_footer; // offsets
} tad_map_t;

static u64 tad_align (u64 v)
{
	return (v + 0x3f) & ~(u64)0x3f;
}

static bool tad_map (tad_map_t *m, const u8 *d, size_t size)
{
	if (size < 0x40 || tw_be32 (d) != 0x20 || d[4] != 'I' || d[5] != 's')
		return false;
	m->cert = tw_be32 (d + 8);
	m->crl = tw_be32 (d + 12);
	m->tik = tw_be32 (d + 16);
	m->tmd = tw_be32 (d + 20);
	m->data = tw_be32 (d + 24);
	m->footer = tw_be32 (d + 28);

	u64 off = 0x40;
	m->o_cert = off;
	off += tad_align (m->cert);
	m->o_crl = off;
	off += tad_align (m->crl);
	m->o_tik = off;
	off += tad_align (m->tik);
	m->o_tmd = off;
	off += tad_align (m->tmd);
	m->o_data = off;
	off += tad_align (m->data);
	m->o_footer = off;

	// every section must lie inside the file (the last needn't be padded)
	return m->o_cert + m->cert <= size && m->o_crl + m->crl <= size && m->o_tik + m->tik <= size
		&& m->o_tmd + m->tmd <= size && m->o_data + m->data <= size
		&& m->o_footer + m->footer <= size && m->tik >= 0x1f2 && m->tmd >= 0x1e4;
}

// Offset of the body that follows the signature, or 0 if the type is unknown.
static u32 tad_sig_body (const u8 *p, u32 size)
{
	if (size < 4)
		return 0;
	switch (tw_be32 (p))
	{
		case 0x10000: return 4 + 0x200 + 0x3c;
		case 0x10001: return 4 + 0x100 + 0x3c;
		case 0x10002: return 4 + 0x3c + 0x40;
	}
	return 0;
}

bool IsDsiTad (const u8 *data, size_t size)
{
	tad_map_t m;
	if (!data || !tad_map (&m, data, size))
		return false;
	// a TMD and ticket must both parse, or this is some other "Is" file
	const u32 tb = tad_sig_body (data + m.o_tmd, m.tmd);
	const u32 kb = tad_sig_body (data + m.o_tik, m.tik);
	return tb && kb && (u64)tb + 0xa4 <= m.tmd && (u64)kb + 0xb2 <= m.tik;
}

enumError ScanDsiTad (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	tad_map_t m;
	if (!entries || !n_entries || !IsDsiTad (d, size) || !tad_map (&m, d, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u8 *tmd = d + m.o_tmd, *tik = d + m.o_tik;
	const u32 tb = tad_sig_body (tmd, m.tmd), kb = tad_sig_body (tik, m.tik);
	const uint ncont = tw_be16 (tmd + tb + 0x9e);
	const uint boot = tw_be16 (tmd + tb + 0xa0);
	if (ncont > TAD_MAX_CONTENTS || (u64)tb + 0xa4 + (u64)ncont * 0x24 > m.tmd)
		return ERR_INVALID_DATA;

	tw_out_t out;
	memset (&out, 0, sizeof (out));
	if (m.cert)
		tw_emit (&out, "cert.bin", tw_dup (d + m.o_cert, m.cert), m.cert);
	if (m.crl)
		tw_emit (&out, "crl.bin", tw_dup (d + m.o_crl, m.crl), m.crl);
	tw_emit (&out, "title.tik", tw_dup (tik, m.tik), m.tik);
	tw_emit (&out, "title.tmd", tw_dup (tmd, m.tmd), m.tmd);
	if (m.footer)
		tw_emit (&out, "footer.bin", tw_dup (d + m.o_footer, m.footer), m.footer);

	// Candidate title keys, one per common key.
	u8 iv[16] = { 0 };
	memcpy (iv, tik + kb + 0x9c, 8);
	u8 tkeys[3][16];
	for (int k = 0; k < 3; k++)
	{
		memcpy (tkeys[k], tik + kb + 0x7f, 16);
		AES128_CBC_Decrypt (tw_common_keys[k], iv, tkeys[k], 16);
	}
	int good = -1; // index of the key that verified

	char manifest[4096];
	int mlen = snprintf (manifest, sizeof (manifest),
		"# DSi TAD: title id %08x%08x, %u content(s), boot index %u\n", tw_be32 (tmd + tb + 0x4c),
		tw_be32 (tmd + tb + 0x50), ncont, boot);

	u64 off = m.o_data;
	for (uint i = 0; i < ncont; i++)
	{
		const u8 *rec = tmd + tb + 0xa4 + (size_t)i * 0x24;
		const u32 cid = tw_be32 (rec);
		const u16 idx = tw_be16 (rec + 4);
		const u64 csize = tw_be64 (rec + 8);
		const u64 esize = (csize + 15) & ~(u64)15;
		if (csize > TAD_MAX_CONTENT || off + esize > m.o_data + m.data)
			break;

		u8 civ[16] = { 0 };
		civ[0] = (u8)(idx >> 8);
		civ[1] = (u8)idx;

		u8 *plain = tw_dup (d + off, (size_t)esize);
		bool ok = false;
		if (plain)
		{
			// the key that worked for the previous content goes first
			int order[3] = { 0, 1, 2 };
			if (good > 0)
			{
				order[0] = good;
				order[good] = 0;
			}
			for (int k = 0; k < 3 && !ok; k++)
			{
				const int kk = order[k];
				memcpy (plain, d + off, (size_t)esize);
				AES128_CBC_Decrypt (tkeys[kk], civ, plain, (size_t)esize);
				u8 sha[20];
				SHA1 (plain, (size_t)csize, sha);
				if (!memcmp (sha, rec + 16, 20))
				{
					ok = true;
					good = kk;
				}
			}
		}

		char name[64];
		if (ok)
		{
			snprintf (name, sizeof (name), "%08x.%s", cid, tw_looks_like_srl (plain, (size_t)csize) ? "srl" : "app");
			mlen += snprintf (manifest + mlen, sizeof (manifest) - mlen, "%-16s size %llu sha1 ok\n", name,
				(unsigned long long)csize);
			u8 *keep = plain;
			plain = 0;
			// modcrypt is left as it is: the SRL is written exactly as the title ships it
			tw_emit (&out, name, keep, (uint)csize);
		}
		else
		{
			snprintf (name, sizeof (name), "%08x.app.enc", cid);
			mlen += snprintf (manifest + mlen, sizeof (manifest) - mlen,
				"%-16s size %llu NOT decrypted (title key unknown)\n", name, (unsigned long long)csize);
			tw_emit (&out, name, tw_dup (d + off, (size_t)esize), (uint)esize);
		}
		FREE (plain);
		off += tad_align (esize);
		if (mlen > (int)sizeof (manifest) - 128)
			break;
	}
	if (mlen > 0)
		tw_emit (&out, "manifest.txt", tw_dup ((const u8 *)manifest, (size_t)mlen), (uint)mlen);
	return tw_finish (&out, entries, n_entries);
}

//-----------------------------------------------------------------------------
// SD-card export .bin
//-----------------------------------------------------------------------------

#define BIN_BANNER_END 0x4000
#define BIN_HEADER 0x4020
#define BIN_HEADER_LEN 0xb4
#define BIN_FOOTER 0x40f4
#define BIN_FOOTER_LEN 0x440
#define BIN_CONTENT 0x4554

// Decrypt one ES block at 'off' with the fixed key; NULL if it does not verify.
static u8 *bin_block (const u8 *d, size_t size, u64 off, u32 len, const u8 key[16])
{
	if (off + len + 0x20 > size)
		return 0;
	u8 *p = tw_dup (d + off, len);
	if (!p)
		return 0;
	if (TwlEsDecrypt (key, p, d + off + len, len))
	{
		FREE (p);
		return 0;
	}
	return p;
}

bool IsDsiExportBin (const u8 *d, size_t size)
{
	if (!d || size < BIN_FOOTER)
		return false;
	// Cheap gate first: the info block of the first ES block must decrypt to
	// the 0x3a marker and the 0x4000 length under the fixed key.
	tw_ctx_t c;
	memset (&c, 0, sizeof (c));
	u8 ctr[16], info[16];
	memcpy (ctr, d + BIN_BANNER_END + 16, 16);
	ctr[0] = 0;
	ctr[13] = ctr[14] = ctr[15] = 0;
	tw_set_key (&c, tw_fix_key);
	tw_set_ctr (&c, ctr);
	tw_crypt_block (&c, d + BIN_BANNER_END + 16, info);
	if (info[0] != 0x3a || ((u32)info[13] << 16 | info[14] << 8 | info[15]) != BIN_BANNER_END)
		return false;

	u8 *b = bin_block (d, size, 0, BIN_BANNER_END, tw_fix_key);
	if (!b)
		return false;
	FREE (b);
	u8 *h = bin_block (d, size, BIN_HEADER, BIN_HEADER_LEN, tw_fix_key);
	const bool ok = h && !memcmp (h, "4ANT", 4);
	FREE (h);
	return ok;
}

// Find "TW" + 16 hex digits (else any 16 hex digit run) in the certificate name.
static bool bin_console_id (const u8 *name, u32 len, u8 id[8])
{
	char s[65];
	memcpy (s, name, len > 64 ? 64 : len);
	s[len > 64 ? 64 : len] = 0;
	for (int pass = 0; pass < 2; pass++)
		for (u32 i = 0; i + 16 <= len && i + 16 <= 64; i++)
		{
			if (pass == 0 && !(i >= 2 && s[i - 2] == 'T' && s[i - 1] == 'W'))
				continue;
			bool hex = true;
			for (int k = 0; k < 16 && hex; k++)
				hex = (s[i + k] >= '0' && s[i + k] <= '9') || (s[i + k] >= 'a' && s[i + k] <= 'f')
					|| (s[i + k] >= 'A' && s[i + k] <= 'F');
			if (!hex)
				continue;
			for (int k = 0; k < 8; k++)
			{
				unsigned v = 0;
				sscanf (s + i + 2 * k, "%2x", &v);
				id[k] = (u8)v;
			}
			return true;
		}
	return false;
}

enumError ScanDsiExportBin (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	if (!entries || !n_entries || !IsDsiExportBin (d, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	tw_out_t out;
	memset (&out, 0, sizeof (out));
	char manifest[4096];
	int mlen = 0;

	u8 *banner = bin_block (d, size, 0, BIN_BANNER_END, tw_fix_key);
	u8 *head = bin_block (d, size, BIN_HEADER, BIN_HEADER_LEN, tw_fix_key);
	u8 *foot = bin_block (d, size, BIN_FOOTER, BIN_FOOTER_LEN, tw_fix_key);
	if (!banner || !head)
	{
		FREE (banner);
		FREE (head);
		FREE (foot);
		return ERR_INVALID_DATA;
	}

	// The icon/title block is 0x23c0 bytes of banner followed by zero padding.
	tw_emit (&out, "banner.bin", tw_dup (banner, 0x23c0), 0x23c0);
	u8 sha[20];
	SHA1 (banner, BIN_BANNER_END, sha);
	mlen += snprintf (manifest + mlen, sizeof (manifest) - mlen, "# DSiWare export, title id %08x%08x\n",
		tw_le32 (head + 0x24), tw_be32 (head + 0x20));
	mlen += snprintf (manifest + mlen, sizeof (manifest) - mlen, "banner.bin      sha1 %s\n",
		foot && !memcmp (sha, foot, 20) ? "ok" : "MISMATCH");
	tw_emit (&out, "header.bin", tw_dup (head, BIN_HEADER_LEN), BIN_HEADER_LEN);
	if (foot)
	{
		tw_emit (&out, "footer.bin", tw_dup (foot, BIN_FOOTER_LEN), BIN_FOOTER_LEN);
		tw_emit (&out, "ap.cert", tw_dup (foot + 0x140, 0x180), 0x180);
		tw_emit (&out, "tw.cert", tw_dup (foot + 0x2c0, 0x180), 0x180);
	}

	// Console-specific key from the TW certificate's key name.
	u8 cid[8], varkey[16];
	bool have_var = false;
	if (foot && bin_console_id (foot + 0x2c0 + 0xc4, 0x40, cid))
	{
		TwlTadVarKey (varkey, cid);
		have_var = true;
		mlen += snprintf (manifest + mlen, sizeof (manifest) - mlen, "console id %02X%02X%02X%02X%02X%02X%02X%02X\n",
			cid[0], cid[1], cid[2], cid[3], cid[4], cid[5], cid[6], cid[7]);
	}

	// header 0x28: tmd, app, 7 unused, public.sav, banner.sav (plain sizes)
	static const struct
	{
		ccp name;
		uint size_off; // in header
		int sha_off; // in footer, -1 = none
	} part[] = {
		{ "title.tmd", 0x28, 0x28 },
		{ "title.srl", 0x2c, 0x3c },
		{ "public.sav", 0x4c, 0xdc },
		{ "banner.sav", 0x50, 0xf0 },
	};

	// The seven unused slots sit between the app and public.sav; walk in file
	// order and skip empty ones (their ES block is absent).
	u64 off = BIN_CONTENT;
	for (uint slot = 0; slot < 11; slot++)
	{
		const u32 len = tw_be32 (head + 0x28 + 4 * slot);
		if (!len)
			continue;
		u8 *p = 0;
		// FIX and VAR are both tried; the MAC says which one was right.
		p = bin_block (d, size, off, len, tw_fix_key);
		if (!p && have_var)
			p = bin_block (d, size, off, len, varkey);

		ccp name = 0;
		int sha_off = -1;
		char other[32];
		for (uint k = 0; k < sizeof (part) / sizeof (*part); k++)
			if (part[k].size_off == 0x28 + 4 * slot)
			{
				name = part[k].name;
				sha_off = part[k].sha_off;
			}
		if (!name)
		{
			snprintf (other, sizeof (other), "part%u.bin", slot);
			name = other;
		}

		if (p)
		{
			bool sha_ok = false;
			if (foot && sha_off >= 0)
			{
				SHA1 (p, len, sha);
				sha_ok = !memcmp (sha, foot + sha_off, 20);
			}
			mlen += snprintf (manifest + mlen, sizeof (manifest) - mlen, "%-15s size %u sha1 %s\n", name,
				len, sha_off < 0 ? "n/a" : sha_ok ? "ok" : "MISMATCH");
			tw_emit (&out, name, p, len);
		}
		else
		{
			mlen += snprintf (manifest + mlen, sizeof (manifest) - mlen,
				"%-15s size %u NOT decrypted (%s)\n", name, len,
				have_var ? "MAC failed" : "no console id found");
		}
		off += (u64)len + 0x20;
		if (mlen > (int)sizeof (manifest) - 160)
			break;
	}
	tw_emit (&out, "manifest.txt", tw_dup ((const u8 *)manifest, (size_t)mlen), (uint)mlen);

	FREE (banner);
	FREE (head);
	FREE (foot);
	return tw_finish (&out, entries, n_entries);
}
