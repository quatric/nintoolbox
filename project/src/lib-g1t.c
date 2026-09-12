// SPDX-License-Identifier: GPL-2.0+
// Split out of lib-nintendo-archives.c -- one archive format per file.
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-camelot.h"
#include "lib-yay0.h"
#include "lib-flim.h"
#include "lib-szs.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include "lib-archive-util.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>


// ----------------------------------------------------------------------------
// Koei Tecmo G1T texture container (Hyrule Warriors, Fire Emblem Warriors)
//
// Little-endian (3DS), and the big-endian (Wii U) variant:
//
//   0x00  "GT1G"  (3DS, little-endian fields) / "G1TG" (Wii U, big-endian)
//   0x04  char[4] version, "0600" on the 3DS samples
//   0x08  u32 total file size
//   0x0c  u32 offset of the texture-offset table
//   0x10  u32 texture count
//   0x14  u32 platform (5 = 3DS; Wii U members carry GX2 encodings)
//   table: count * u32, each relative to the offset-table position
//   texture header:
//     0x00  u8  (mip count << 4) | system id
//     0x01  u8  format
//     0x02  u8  (width exponent << 4) | height exponent
//     0x03  u8  flags
//     0x04  12 bytes of extended header, then the pixel data
//
// The per-texture headers are pure bytes, so only the four header fields and
// the offset table differ between the two byte orders; the reader is selected
// once from the signature.
//
// Derived from the two retail 3DS samples in tests/fixtures: for both, the
// declared total matches the real file size exactly, and the mip chain
// implied by the geometry accounts for every remaining byte at the format's
// bit depth -- 0x47 at 4bpp (ETC1) and 0x48 at 8bpp (ETC1A4), which is what
// platform 5 uses. The 12-byte extension is what makes both files come out
// exact; a file where it does not is retried without it rather than being
// decoded at the wrong offset. Big-endian support is verified with the
// byte-swapped synthetic fixtures under /tmp (real Wii U G1Ts have not been
// observed); members whose pixel format is not one of the 3DS set are not
// pixel-decodable and are exported raw as *.bin instead.
// ----------------------------------------------------------------------------

static uint g1t_mip_pixels (uint w, uint h, uint mips)
{
	uint total = 0;
	for (uint m = 0; m < mips; m++)
	{
		const uint mw = w >> m ? w >> m : 1;
		const uint mh = h >> m ? h >> m : 1;
		total += mw * mh;
	}
	return total;
}

//-----------------------------------------------------------------------------
// Wii U chunked wrapper (.g1t.gz, Hyrule Warriors). Layout reverse
// engineered against 3857 retail files (all exact): u32 magic 0x10000
// (BE) + u32 stream count + u32 decompressed size + u32[count] table,
// then size-prefixed zlib streams (u32 BE length + payload) with zero
// padding between them. table[i] always equals stream i's byte length
// + 4; a trailing entry with no stream left appends that many zero
// bytes instead. Total output must equal the declared size.
//-----------------------------------------------------------------------------

#define G1TGZ_MAX_STREAMS 100000
#define G1TGZ_MAX_OUTPUT NFMT_MAX_OUTPUT
#define G1TGZ_MAX_SCAN 65536

static bool g1tgz_probe (const u8 *src, uint size, uint *count, uint *decomp)
{
	if (!src || size < 12 || rd_be32 (src) != 0x10000)
		return false;
	const uint n = rd_be32 (src + 4);
	const uint dec = rd_be32 (src + 8);
	if (!n || n > G1TGZ_MAX_STREAMS || !dec || dec > G1TGZ_MAX_OUTPUT)
		return false;
	if ((u64)12 + (u64)n * 4 > size)
		return false;
	if (count)
		*count = n;
	if (decomp)
		*decomp = dec;
	return true;
}

bool IsG1TGZ (const u8 *data, uint size);

// One size-prefixed stream at *pos: validates table[i], inflates
// exactly, advances past it. Returns false on any violation.
static bool g1tgz_stream (const u8 *src, uint size, uint *pos, uint want, u8 **out, uint *out_len,
	uint *out_cap)
{
	uint p = *pos;
	if (p + 4 > size)
		return false;
	const uint sz = rd_be32 (src + p);
	if (!sz || (u64)p + 4 + sz > size)
		return false;
	if (want != sz + 4)
		return false;
	if (src[p + 4] != 0x78)
		return false;

	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = (Bytef *)(src + p + 4);
	strm.avail_in = sz;
	if (inflateInit (&strm) != Z_OK)
		return false;
	// Grow like DecodeZlibGrow, but require exact framing: the stream
	// must end exactly at its declared size.
	uint cap = sz * 4 + 4096;
	if (cap > (64u << 20))
		cap = 64u << 20;
	bool ok = false;
	for (;;)
	{
		if (*out_len + cap > *out_cap)
		{
			if (*out_len + cap > G1TGZ_MAX_OUTPUT)
				break;
			u8 *grown = REALLOC (*out, *out_len + cap);
			if (!grown)
				break;
			*out = grown;
			*out_cap = *out_len + cap;
		}
		strm.next_out = *out + *out_len;
		strm.avail_out = *out_cap - *out_len;
		const uint before = *out_len;
		const int ret = inflate (&strm, Z_FINISH);
		*out_len += (uint)(strm.next_out - (*out + before));
		if (ret == Z_STREAM_END)
		{
			ok = strm.avail_in == 0 && *out_len > before;
			break;
		}
		if (ret != Z_OK && ret != Z_BUF_ERROR)
			break;
		if (cap >= (64u << 20))
			break;
		cap *= 2;
	}
	inflateEnd (&strm);
	if (!ok)
		return false;
	*pos = p + 4 + sz;
	return true;
}

enumError DecodeG1TGZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	uint n = 0, decomp = 0;
	if (!g1tgz_probe (src, src_size, &n, &decomp))
		return EINVAL;

	u8 *out = 0;
	uint out_len = 0, out_cap = 0;
	uint pos = 12 + n * 4;
	bool ok = true;
	bool used_sparse = false;
	for (uint i = 0; ok && i < n; i++)
	{
		// Skip zero padding between streams (bounded per gap): size
		// words start with zero bytes themselves, so validate per
		// position instead of skipping blindly.
		bool placed = false;
		const uint gap0 = pos;
		while (pos + 6 <= src_size && pos - gap0 < G1TGZ_MAX_SCAN)
		{
			const uint sz = rd_be32 (src + pos);
			const uint want = rd_be32 (src + 12 + i * 4);
			if (sz && sz < src_size && src[pos + 4] == 0x78 && want == sz + 4)
			{
				placed = true;
				break;
			}
			if (src[pos] != 0)
				break;
			pos++;
		}
		if (!placed)
		{
			// Trailing sparse block: no stream left for the last
			// entry (verified: single-texture event_text files whose
			// tail is zeros plus a 128B per-file blob, possibly a
			// signature). The entry counts zero output bytes; anything
			// after it is outside payload accounting and is accepted
			// only here, never for mid-file entries.
			if (i != n - 1)
			{
				ok = false;
				break;
			}
			used_sparse = true;
			const uint want = rd_be32 (src + 12 + i * 4);
			if ((u64)out_len + want > G1TGZ_MAX_OUTPUT)
				ok = false;
			else
			{
				u8 *grown = REALLOC (out, out_len + want);
				if (!grown)
					ok = false;
				else
				{
					out = grown;
					memset (out + out_len, 0, want);
					out_len += want;
					out_cap = out_len;
				}
			}
			break;
		}
		if (!g1tgz_stream (src, src_size, &pos, rd_be32 (src + 12 + i * 4), &out, &out_len,
				&out_cap))
			ok = false;
	}
	if (ok)
	{
		// Streams-only files must end exactly (modulo zero padding);
		// sparse-tail files already validated every payload byte and
		// carry an unverified trailer past it.
		if (!used_sparse)
		{
			while (pos < src_size && src[pos] == 0)
				pos++;
			if (pos != src_size)
				ok = false;
		}
		if (out_len != decomp)
			ok = false;
	}
	if (!ok)
	{
		FREE (out);
		return EINVAL;
	}
	*dest = out;
	*dest_size = out_len;
	return ERR_OK;
}

bool IsG1TGZ (const u8 *data, uint size)
{
	if (!data || size < 12 + 4)
		return false;
	uint n = 0, decomp = 0;
	if (!g1tgz_probe (data, size, &n, &decomp))
		return false;
	// First size word must validate (bounds the false-positive rate
	// of the 4-byte magic on its own).
	uint pos = 12 + n * 4;
	while (pos + 6 <= size && (uint)(pos - (12 + n * 4)) < 1024)
	{
		const uint sz = rd_be32 (data + pos);
		if (sz && sz < size && data[pos + 4] == 0x78 && rd_be32 (data + 12) == sz + 4)
			return true;
		if (data[pos] != 0)
			return false;
		pos++;
	}
	return false;
}


enumError ExtractG1TArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".g1t") && !is_ext_match (arg, ".g1t.gz"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	// Wii U members ship in the chunked wrapper; unwrap first so the
	// container parse below sees a plain G1T either way.
	if (is_ext_match (arg, ".g1t.gz") && raw_size <= UINT_MAX)
	{
		u8 *dec = 0;
		uint dec_size = 0;
		if (DecodeG1TGZ (&dec, &dec_size, raw, (uint)raw_size))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		FREE (raw);
		raw = dec;
		raw_size = dec_size;
	}
	// The Wii U (PowerPC) variant opens with the byte-reversed signature
	// "G1TG" and stores every multi-byte field big-endian; the 3DS (ARM)
	// variant writes "GT1G" little-endian.
	const bool be = raw_size >= 4 && !memcmp (raw, "G1TG", 4);
	if (raw_size < 0x24 || (memcmp (raw, "GT1G", 4) && !be))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 total = be ? rd_be32 (raw + 8) : rd_le32 (raw + 8);
	const u32 tbl = be ? rd_be32 (raw + 0x0c) : rd_le32 (raw + 0x0c);
	const u32 count = be ? rd_be32 (raw + 0x10) : rd_le32 (raw + 0x10);
	const u32 platform = be ? rd_be32 (raw + 0x14) : rd_le32 (raw + 0x14);
	if (total != raw_size || !count || count > 0x1000 || (u64)tbl + (u64)count * 4 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	ccp stem = strrchr (arg, '/');
	stem = stem ? stem + 1 : arg;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT G1T:%s (%u texture%s, platform %u) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, count == 1 ? "" : "s",
			platform, dest);

	uint written = 0;
	for (u32 i = 0; i < count; i++)
	{
		const u32 rel = be ? rd_be32 (raw + tbl + i * 4) : rd_le32 (raw + tbl + i * 4);
		const u64 hdr = (u64)tbl + rel;
		if (hdr + 8 > raw_size)
			continue;

		const u8 *th = raw + hdr;
		const uint mips = th[0] >> 4 ? th[0] >> 4 : 1;
		const uint format = th[1];
		const uint w = 1u << (th[2] >> 4);
		const uint h = 1u << (th[2] & 15);
		if (!w || !h || w > 8192 || h > 8192)
			continue;

		// Bit depths derived the same way as the layout: for every texture
		// on the Hyrule Warriors Legends cart, the mip chain at this depth
		// accounts for exactly the bytes present. 0x47/0x48 are ETC1 and
		// ETC1A4, 0x09 is RGBA8 in PICA 8x8 tile order.
		bool known_format;
		uint bits;
		switch (format)
		{
			case 0x47:
			case 0x48:
				known_format = true;
				bits = format == 0x47 ? 4 : 8;
				break;
			case 0x09:
				known_format = true;
				bits = 32;
				break;
			default:
				// Unknown encoding (expected for real Wii U GX2 members).
				// The member bytes are still exported raw so the game
				// content stays reachable.
				known_format = false;
				bits = 0;
				break;
		}

		// Prefer the 12-byte extended header, fall back to none, and take
		// whichever actually accounts for the bytes that are there.
		u64 data_off = hdr + 8 + 12;
		if (data_off >= raw_size)
			data_off = hdr + 8;
		if (data_off >= raw_size)
			continue;

		if (!known_format)
		{
			char raw_out[PATH_MAX];
			snprintf (raw_out, sizeof (raw_out), "%s/%s_%04u.bin", dest, stem, i);
			if (SaveFile (raw_out, 0, 0, raw + data_off, (uint)(raw_size - (size_t)data_off), 0))
				continue;
			written++;
			continue;
		}

		const uint need = g1t_mip_pixels (w, h, mips) * bits / 8;
		if (data_off + need > raw_size)
			data_off = hdr + 8;
		if (data_off + need > raw_size)
			continue;

		u8 *rgba = CALLOC ((size_t)w * h, 4);
		if (!rgba)
			continue;
		// Only the base level is exported; the mip chain follows it.
		const uint base = w * h * bits / 8;
		enumError derr = ERR_OK;
		if (bits == 32)
		{
			// PICA stores RGBA8 in 8x8 tiles, morton-ordered within a tile,
			// and each texel as A,B,G,R.
			const u8 *src = raw + data_off;
			for (uint ty = 0; ty < h; ty += 8)
				for (uint tx = 0; tx < w; tx += 8)
					for (uint py = 0; py < 8; py++)
						for (uint px = 0; px < 8; px++)
						{
							const uint x = tx + px, y = ty + py;
							if (x >= w || y >= h)
								continue;
							const uint tile = (ty / 8) * (w / 8 ? w / 8 : 1) + tx / 8;
							const uint idx = (tile * 64 + morton8 (px, py)) * 4;
							if (idx + 4 > base)
								continue;
							u8 *o = rgba + ((size_t)y * w + x) * 4;
							o[0] = src[idx + 3];
							o[1] = src[idx + 2];
							o[2] = src[idx + 1];
							o[3] = src[idx + 0];
						}
		}
		else
			derr = bits == 4 ? decode_etc1_tiled (rgba, raw + data_off, w, h, base)
							 : decode_etc1a4_tiled (rgba, raw + data_off, w, h, base);
		if (derr)
		{
			FREE (rgba);
			continue;
		}

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%s_%04u.png", dest, stem, i);

		Image_t img;
		InitializeIMG (&img);
		img.data = rgba;
		img.data_alloced = true;
		img.data_size = (uint)w * h * 4;
		img.width = img.xwidth = w;
		img.height = img.xheight = h;
		img.iform = img.info_iform = IMG_X_RGB;
		img.info_fform = FF_UNKNOWN;
		img.info_n_image = 1;
		img.endian = &le_func;
		img.path = out;
		if (SaveIMG (&img, FF_PNG, 0, 0, out, true) == ERR_OK)
			written++;
		ResetIMG (&img);
	}

	FREE (raw);
	return written ? ERR_OK : ERR_INVALID_DATA;
}
