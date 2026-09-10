// SPDX-License-Identifier: GPL-2.0+
#include "lib-nintendo-archives.h"
#include "lib-archive-util.h"
#include "lib-nintendo.h"
#include "lib-image.h" // TPL headers, for re-emitting PTLG textures
#include "lib-camelot.h"
#include "lib-yay0.h"
#include "lib-flim.h"
#include "lib-szs.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include <string.h>

// ----------------------------------------------------------------------------
// Repacking / Creation Implementations
// ----------------------------------------------------------------------------

#include <zlib.h>
#include <stdlib.h>

// ----------------------------------------------------------------------------
// Pokemon Stadium (N64) PERS-SZP asset container
//
// A 24-byte header wrapping an ordinary Yay0 stream, big-endian:
//
//   0x00  "PERS-SZP"
//   0x08  u32 header size (0x18 in every instance on the retail cart)
//   0x0c  u32 decompressed size
//   0x10  u32 decompressed size again
//   0x14  u32 zero
//   0x18  Yay0 stream
//
// Established against Pokemon Stadium (USA, Rev 1), which carries 410 of
// them. The sibling "FRAGMENT" signature in the same ROM is a different
// thing entirely -- a MIPS code overlay with a 0x20-byte header, not an
// asset -- so it is deliberately not claimed here.
// ----------------------------------------------------------------------------
// Animal Crossing: Pocket Camp .zdat
//
// A flat container: a 16-bit header naming three offsets and an entry count,
// then one 16-byte record per file (name length, size, uncompressed size, and
// a word that is always zero), then the names back to back, then the payloads
// back to back. Verified against 31 files pulled from the game's own CDN --
// single-entry up to 45 entries -- where the arithmetic closes exactly: the
// name table ends where the data begins, and the last payload ends at EOF.
//
// Every payload is a Unity asset bundle XORed with one repeated byte. The key
// is not a secret held elsewhere; it falls out of the bundle's own "UnityFS"
// signature, and the check below is what confirms it: a bundle records its own
// total length, and across all 168 payloads seen that length matched the entry
// size exactly once the key was applied. A wrong key does not survive that.
#define ZDAT_ENT_SIZE 16

static enumError zdat_read_header (
	const u8 *data, size_t size, uint *ent_off, uint *name_off, uint *data_off, uint *count)
{
	if (size < 0x30 || memcmp (data, "ZDAT", 4))
		return ERR_NOTHING_TO_DO;

	*ent_off = rd_le16 (data + 0x06);
	*name_off = rd_le16 (data + 0x0a);
	*data_off = rd_le16 (data + 0x0e);
	*count = rd_le16 (data + 0x12);

	// The name table begins immediately after the entry array, so its offset
	// is fixed by the count rather than being free: 0x30 only for a
	// single-entry container, further out for the rest (45 entries push it to
	// 0x2f0). Requiring 0x30 here would reject every archive holding more
	// than one file.
	if (*ent_off != 0x20 || !*count)
		return ERR_INVALID_DATA;
	if (*name_off != *ent_off + (u64)*count * ZDAT_ENT_SIZE)
		return ERR_INVALID_DATA;
	if (*data_off > size)
		return ERR_INVALID_DATA;
	return ERR_OK;
}

// Recover the repeated byte from the known signature, then require the bundle
// to agree about its own size. Returns 0 when this is not a bundle at all.
static int zdat_unmask (u8 *p, uint size)
{
	static const char sig[] = "UnityFS";
	const uint siglen = sizeof (sig) - 1;
	if (size < 0x20)
		return 0;

	const u8 key = p[0] ^ (u8)sig[0];
	for (uint i = 1; i < siglen; i++)
		if ((p[i] ^ key) != (u8)sig[i])
			return 0;

	for (uint i = 0; i < size; i++)
		p[i] ^= key;

	// UnityFS: signature, u32 format, two NUL-terminated version strings,
	// then the bundle's own total size as a big-endian u64.
	uint off = 8 + 4;
	for (int i = 0; i < 2; i++)
	{
		uint start = off;
		while (off < size && p[off])
			off++;
		if (off >= size || off == start + 0)
			; // an empty version string is unusual but not fatal
		if (off >= size)
			return 0;
		off++;
	}
	if (off + 8 > size)
		return 0;

	u64 total = 0;
	for (int i = 0; i < 8; i++)
		total = total << 8 | p[off + i];
	return total == size;
}

enumError ExtractZDATArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	uint ent_off, name_off, data_off, count;
	enumError err = zdat_read_header (raw, raw_size, &ent_off, &name_off, &data_off, &count);
	if (err)
	{
		FREE (raw);
		return err;
	}

	// Walk the table once before writing anything: a container whose entries
	// do not add up to the file is not one of these, and half an extraction
	// is worse than none.
	u64 name_pos = name_off, data_pos = data_off;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + ent_off + (size_t)i * ZDAT_ENT_SIZE;
		const u32 nlen = rd_le32 (e);
		const u32 size = rd_le32 (e + 4);
		if (!nlen || nlen > 4096 || name_pos + nlen > data_off)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
		if (data_pos + size > raw_size)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
		name_pos += nlen;
		data_pos += size;
	}
	if (name_pos != data_off || data_pos != raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZDAT:%s (%u file%s) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, count == 1 ? "" : "s", dest);

	name_pos = name_off;
	data_pos = data_off;
	uint written = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + ent_off + (size_t)i * ZDAT_ENT_SIZE;
		const u32 nlen = rd_le32 (e);
		const u32 size = rd_le32 (e + 4);

		char name[PATH_MAX];
		const uint keep = nlen < sizeof (name) - 1 ? nlen : sizeof (name) - 1;
		memcpy (name, raw + name_pos, keep);
		name[keep] = 0;
		name_pos += nlen;

		// Stored names may carry a directory part of their own.
		for (char *q = name; *q; q++)
			if (*q == '\\' || (*q == '.' && q[1] == '.'))
				*q = '_';

		u8 *payload = MALLOC (size ? size : 1);
		if (!payload)
		{
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		memcpy (payload, raw + data_pos, size);
		data_pos += size;

		const int unmasked = zdat_unmask (payload, size);

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%s", dest, name);
		if (!testmode)
		{
			CreatePath (out, false);
			if (!SaveFile (out, 0, 0, payload, size, 0))
				written++;
		}
		else
			written++;

		if (verbose > 0)
			fprintf (stdlog, "  %-40s %8u bytes%s\n", name, size,
				unmasked ? "" : "  (not a Unity bundle, stored as found)");

		FREE (payload);
	}

	FREE (raw);
	return written ? ERR_OK : ERR_INVALID_DATA;
}

///////////////////////////////////////////////////////////////////////////////

enumError ExtractPERSFile (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x20 || memcmp (raw, "PERS-SZP", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 hdr_size = rd_be32 (raw + 8);
	const u32 declared = rd_be32 (raw + 0x0c);
	if (hdr_size < 0x18 || hdr_size >= raw_size || !declared)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	if (memcmp (raw + hdr_size, "Yay0", 4))
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	u8 *decoded = 0;
	uint decoded_size = 0;
	const enumError derr
		= DecodeYay0 (&decoded, &decoded_size, raw + hdr_size, (uint)(raw_size - hdr_size));
	if (derr || !decoded)
	{
		FREE (decoded);
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	// The header states the payload size independently of the Yay0 stream's
	// own, so a mismatch means this was not really one of these.
	if (decoded_size != declared)
	{
		FREE (decoded);
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	ccp stem = strrchr (arg, '/');
	stem = stem ? stem + 1 : arg;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PERS:%s (%u bytes) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, decoded_size, dest);

	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s/%s.bin", dest, stem);
	if (!testmode)
		SaveFile (out, 0, 0, decoded, decoded_size, 0);

	FREE (decoded);
	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// Camelot GX texture bank (Mario Golf: Toadstool Tour, Mario Power Tennis)
//
// Camelot's GameCube discs name every asset with single letters (files/A/U/R),
// so there is no extension to key on and the container is usually wrapped in
// Camelot's own LZ codec. Layout, big-endian, after decompression:
//
//   0x00  u32 magic 0x0020af30
//   0x04  u32 texture count
//   0x08  u32 offset of the entry-pointer table
//         table: count * { u32 entry offset, u32 pad }
//   entry:
//     0x00  u16 width, u16 height
//     0x04  u32 GX texture format (the hardware's own numbering)
//     0x08  u32 offset of the pixel data from the start of the file
//     0x0c  sampler state
//
// Verified across every such file on both retail discs: 881 containers,
// 4252 texture entries, of which 4246 have a format/geometry whose GX size
// lands inside the file (670 of 676 single-entry files end exactly at the
// computed size). Palettised formats are declined rather than guessed at --
// three files disc-wide use one and where their palette lives is unknown.
// ----------------------------------------------------------------------------

#define CAMELOT_TEXBANK_MAGIC 0x0020af30

static image_format_t camelot_gx_image_format (u32 format)
{
	switch (format)
	{
		case 0:
			return IMG_I4;
		case 1:
			return IMG_I8;
		case 2:
			return IMG_IA4;
		case 3:
			return IMG_IA8;
		case 4:
			return IMG_RGB565;
		case 5:
			return IMG_RGB5A3;
		case 6:
			return IMG_RGBA32;
		case 14:
			return IMG_CMPR;
		default:
			return IMG_INVALID; // includes C4/C8/C14X2: palette location unknown
	}
}

// Bytes one GX level occupies, tile padding included.
static u32 camelot_gx_level_size (u32 format, u32 w, u32 h)
{
	uint bw, bh, bpp;
	switch (format)
	{
		case 0:
		case 14:
			bw = 8, bh = 8, bpp = 4;
			break;
		case 1:
		case 2:
			bw = 8, bh = 4, bpp = 8;
			break;
		case 3:
		case 4:
		case 5:
			bw = 4, bh = 4, bpp = 16;
			break;
		case 6:
			bw = 4, bh = 4, bpp = 32;
			break;
		default:
			return 0;
	}
	return ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bw * bh * bpp / 8;
}

// True when a bank header at RAW describes entries that all land inside the
// AVAIL bytes that follow it. Every offset in a bank is relative to the bank
// itself, not to the file, which is what lets one sit inside a larger module.
//
// Camelot's models are PPC relocatable modules (elfbin/xcmdl_*.sbn on Wii)
// that carry their textures inline, so the magic routinely appears at an
// offset rather than at the start of a file. Scanning for it needs this
// check: four bytes alone would false-positive on ordinary code and data.
static bool camelot_bank_valid (const u8 *raw, uint avail)
{
	if (avail < 16 || rd_be32 (raw) != CAMELOT_TEXBANK_MAGIC)
		return false;
	const u32 count = rd_be32 (raw + 4);
	const u32 tbl = rd_be32 (raw + 8);
	if (!count || count > 0x1000 || (u64)tbl + (u64)count * 8 > avail)
		return false;

	for (u32 i = 0; i < count; i++)
	{
		const u32 eoff = rd_be32 (raw + tbl + i * 8);
		if ((u64)eoff + 16 > avail)
			return false;
		const u32 w = rd_be16 (raw + eoff);
		const u32 h = rd_be16 (raw + eoff + 2);
		const u32 format = rd_be32 (raw + eoff + 4);
		const u32 doff = rd_be32 (raw + eoff + 8);
		const u32 need = camelot_gx_level_size (format, w, h);
		if (!w || !h || !need || (u64)doff + need > avail)
			return false;
	}
	return true;
}

// Write every texture in one bank to "<stem>_%04u.png" in DEST, numbering
// from *NEXT so a module holding several banks stays consistently numbered.
static uint camelot_texbank_to_pngs (const u8 *raw, uint size, ccp dest, ccp stem, uint *next)
{
	if (!camelot_bank_valid (raw, size))
		return 0;
	const u32 count = rd_be32 (raw + 4);
	const u32 tbl = rd_be32 (raw + 8);

	uint written = 0;
	for (u32 i = 0; i < count; i++)
	{
		const u32 eoff = rd_be32 (raw + tbl + i * 8);
		if ((u64)eoff + 16 > size)
			continue;
		const u32 w = rd_be16 (raw + eoff);
		const u32 h = rd_be16 (raw + eoff + 2);
		const u32 format = rd_be32 (raw + eoff + 4);
		const u32 doff = rd_be32 (raw + eoff + 8);

		const image_format_t iform = camelot_gx_image_format (format);
		const u32 need = camelot_gx_level_size (format, w, h);
		if (iform == IMG_INVALID || !w || !h || !need || (u64)doff + need > size)
			continue;

		// Wrap the GX pixels in a one-image TPL, the same shape the PTLG
		// reader builds, then let the image layer turn it into a PNG.
		const u32 tpl_hdr = sizeof (tpl_header_t);
		const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t);
		const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
		u8 *tpl = CALLOC (tpl_data + need, 1);
		if (!tpl)
			continue;

		write_be32 (tpl, TPL_MAGIC_NUM);
		write_be32 (tpl + 4, 1);
		write_be32 (tpl + 8, tpl_hdr);
		write_be32 (tpl + tpl_hdr, tpl_tab);
		write_be32 (tpl + tpl_hdr + 4, 0);
		write_be16 (tpl + tpl_tab, h);
		write_be16 (tpl + tpl_tab + 2, w);
		write_be32 (tpl + tpl_tab + 4, iform);
		write_be32 (tpl + tpl_tab + 8, tpl_data);
		write_be32 (tpl + tpl_tab + 20, 1);
		write_be32 (tpl + tpl_tab + 24, 1);
		memcpy (tpl + tpl_data, raw + doff, need);

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%s_%04u.png", dest, stem, (*next)++);

		Image_t img;
		if (AssignIMG (&img, 1, tpl, tpl_data + need, 0, false, &be_func, out) == ERR_OK
			&& SaveIMG (&img, FF_PNG, 0, 0, out, true) == ERR_OK)
			written++;
		ResetIMG (&img);
		FREE (tpl);
	}
	return written;
}

// Detection is deliberately structural rather than name- or first-byte-based:
// a Camelot LZ stream only announces itself with a 1 or 2 in byte 0, far too
// weak on its own (which is why GetNintendoFormat() only accepts it for an
// explicit .stpl/.camelot name). Requiring the decompressed result to carry
// the bank magic is the strong signal, and costs one bounded decode.
enumError ExtractCamelotTexBank (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	// A bank is found by scanning for its signature, because Camelot stores
	// banks inside relocatable modules that announce nothing themselves. That
	// scan has no business running over a file that does announce itself: a
	// 432 KB U8 archive from Excite Truck matched the signature 59 times and
	// was extracted as "59 textures in 59 banks", displacing the archive's
	// real contents. Anything carrying a known magic is left to the handler
	// for that format.
	// [[analyse-magic]]
	if (raw_size >= 8 && GetByMagicFF (raw, (uint)raw_size, (uint)raw_size) != FF_UNKNOWN)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (raw_size < 16 || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Decompress first when this is a Camelot stream, then look for banks in
	// whatever we ended up with.
	const u8 *bank = raw;
	uint bank_size = (uint)raw_size;
	u8 *decoded = 0;
	if (raw[0] == 1 || raw[0] == 2)
	{
		uint decoded_size = 0;
		if (!DecodeCamelot (&decoded, &decoded_size, raw, (uint)raw_size) && decoded
			&& decoded_size >= 16)
		{
			bank = decoded;
			bank_size = decoded_size;
		}
	}

	// Count the banks first so nothing is written for a file that turns out
	// to hold none. A bank can sit at any 4-byte-aligned offset: Camelot's
	// models are PPC modules with their textures inline.
	uint n_banks = 0, n_textures = 0;
	for (uint off = 0; off + 16 <= bank_size; off += 4)
		if (camelot_bank_valid (bank + off, bank_size - off))
		{
			n_banks++;
			n_textures += rd_be32 (bank + off + 4);
		}
	if (!n_banks)
	{
		FREE (decoded);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	ccp stem = strrchr (arg, '/');
	stem = stem ? stem + 1 : arg;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CAMELOT-TEX:%s (%u textures in %u bank%s) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_textures, n_banks,
			n_banks == 1 ? "" : "s", dest);

	uint written = 0, next = 0;
	if (!testmode)
		for (uint off = 0; off + 16 <= bank_size; off += 4)
			if (camelot_bank_valid (bank + off, bank_size - off))
				written += camelot_texbank_to_pngs (bank + off, bank_size - off, dest, stem, &next);

	FREE (decoded);
	FREE (raw);
	return testmode || written ? ERR_OK : ERR_INVALID_DATA;
}

// ----------------------------------------------------------------------------
// Bandai Namco NUS3AUDIO audio archive (.nus3audio / NUS3)
//
// Used by Super Smash Bros. Ultimate (and Namco's other NUS3 middleware
// titles) to bundle per-track audio streams. Little-endian throughout:
//
//   0x00  "NUS3"
//   0x04  u32 body size
//   0x08  chunks, each an 8-byte tag (4 characters, NUL-padded) followed by
//         a u32 size and that many payload bytes:
//           AUDIINDX  u32 track count
//           TNID      u32 track id per track
//           NMOF      u32 offset into TNNM per track
//           ADOF      u32 offset, u32 size into PACK per track
//           TNNM      name table: u8 length, name bytes, NUL terminator
//           PACK      the concatenated track payloads
//
// Track payloads are whole audio files. IDSP and Opus are the two that turn
// up in practice, so name members by their own magic and fall back to .bin.
// ----------------------------------------------------------------------------
enumError ExtractNUS3AudioArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".nus3audio") && !is_ext_match (arg, ".nus3bank")
		&& !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "NUS3", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u8 *nmof = 0, *adof = 0, *tnnm = 0;
	uint nmof_size = 0, adof_size = 0, tnnm_size = 0;
	const u8 *pack = 0;
	uint pack_size = 0;
	u32 n_tracks = 0;

	for (size_t pos = 8; pos + 12 <= raw_size;)
	{
		const u8 *tag = raw + pos;
		const u32 csize = rd_le32 (raw + pos + 8);
		const size_t payload = pos + 12;
		if (csize > raw_size - payload)
			break;

		if (!memcmp (tag, "AUDIINDX", 8) && csize >= 4)
			n_tracks = rd_le32 (raw + payload);
		else if (!memcmp (tag, "NMOF", 4))
			nmof = raw + payload, nmof_size = csize;
		else if (!memcmp (tag, "ADOF", 4))
			adof = raw + payload, adof_size = csize;
		else if (!memcmp (tag, "TNNM", 4))
			tnnm = raw + payload, tnnm_size = csize;
		else if (!memcmp (tag, "PACK", 4))
			pack = raw + payload, pack_size = csize;

		pos = payload + csize;
	}

	if (!n_tracks || n_tracks > 100000 || !adof || !pack || adof_size < (u64)n_tracks * 8)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NUS3AUDIO:%s (%u tracks) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_tracks, dest);

	for (uint i = 0; i < n_tracks; i++)
	{
		const u32 off = rd_le32 (adof + i * 8);
		const u32 size = rd_le32 (adof + i * 8 + 4);
		if (off > pack_size || size > pack_size - off)
			continue;
		const u8 *data = pack + off;

		// Names are optional: a track without one is keyed by its index.
		char name[PATH_MAX];
		name[0] = 0;
		if (tnnm && nmof && nmof_size >= (u64)(i + 1) * 4)
		{
			const u32 noff = rd_le32 (nmof + i * 4);
			if (noff < tnnm_size)
			{
				const uint nlen = tnnm[noff];
				if (nlen && noff + 1 + nlen <= tnnm_size)
				{
					memcpy (name, tnnm + noff + 1, nlen);
					name[nlen] = 0;
				}
			}
		}
		// Track names come straight out of the file, so keep them to a
		// single plain filename rather than letting one escape the
		// destination directory.
		bool name_ok = name[0] != 0;
		for (ccp c = name; name_ok && *c; c++)
			if (*c == '/' || *c == '\\' || (u8)*c < 0x20)
				name_ok = false;
		if (name_ok && (!strcmp (name, ".") || !strcmp (name, "..")))
			name_ok = false;
		if (!name_ok)
			snprintf (name, sizeof (name), "track_%04u", i);

		ccp ext = ".bin";
		if (size >= 4)
		{
			if (!memcmp (data, "IDSP", 4))
				ext = ".idsp";
			else if (!memcmp (data, "OPUS", 4) || !memcmp (data, "OpusHead", 4))
				ext = ".lopus";
			else if (!memcmp (data, "BNSF", 4))
				ext = ".bnsf";
			else if (!memcmp (data, "RIFF", 4))
				ext = ".wav";
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, name, ext);
		if (!testmode && size)
			SaveFile (out_path, 0, 0, data, size, 0);
	}

	FREE (raw);
	return ERR_OK;
}

enumError CreateNUS3AudioArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	// Strip directory and extension from each entry to get the track name
	char (*names)[PATH_MAX] = CALLOC (n_entries, sizeof (*names));
	if (!names)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	u32 tnnm_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp leaf = leaf_name (sorted[i].name);
		snprintf (names[i], sizeof (names[i]), "%s", leaf);
		char *dot = strrchr (names[i], '.');
		if (dot)
			*dot = 0;
		tnnm_size += 1 + (u32)strlen (names[i]) + 1; // 1 byte len + chars + NUL
	}

	const u32 audiindx_len = 4;
	const u32 tnid_len = n_entries * 4;
	const u32 nmof_len = n_entries * 4;
	const u32 adof_len = n_entries * 8;
	const u32 tnnm_chunk_len = tnnm_size;

	u32 pack_len = 0;
	for (uint i = 0; i < n_entries; i++)
		pack_len += sorted[i].size;

	const u32 body_size = (8 + 4 + audiindx_len) + (8 + 4 + tnid_len) + (8 + 4 + nmof_len)
		+ (8 + 4 + adof_len) + (8 + 4 + tnnm_chunk_len) + (8 + 4 + pack_len);

	const u32 total_size = 8 + body_size;
	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (names);
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (out, "NUS3", 4);
	wr_le32 (out + 4, body_size);

	u32 pos = 8;

	// 1. AUDIINDX
	memcpy (out + pos, "AUDIINDX", 8);
	wr_le32 (out + pos + 8, audiindx_len);
	wr_le32 (out + pos + 12, n_entries);
	pos += 12 + audiindx_len;

	// 2. TNID
	memcpy (out + pos, "TNID\0\0\0\0", 8);
	wr_le32 (out + pos + 8, tnid_len);
	for (uint i = 0; i < n_entries; i++)
		wr_le32 (out + pos + 12 + i * 4, 100 + i);
	pos += 12 + tnid_len;

	// 3. NMOF
	memcpy (out + pos, "NMOF\0\0\0\0", 8);
	wr_le32 (out + pos + 8, nmof_len);
	u32 cur_tnnm_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + pos + 12 + i * 4, cur_tnnm_off);
		cur_tnnm_off += 1 + (u32)strlen (names[i]) + 1;
	}
	pos += 12 + nmof_len;

	// 4. ADOF
	memcpy (out + pos, "ADOF\0\0\0\0", 8);
	wr_le32 (out + pos + 8, adof_len);
	u32 cur_pack_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + pos + 12 + i * 8, cur_pack_off);
		wr_le32 (out + pos + 12 + i * 8 + 4, sorted[i].size);
		cur_pack_off += sorted[i].size;
	}
	pos += 12 + adof_len;

	// 5. TNNM
	memcpy (out + pos, "TNNM\0\0\0\0", 8);
	wr_le32 (out + pos + 8, tnnm_chunk_len);
	u32 tnnm_payload = pos + 12;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 nlen = (u8)strlen (names[i]);
		out[tnnm_payload++] = nlen;
		memcpy (out + tnnm_payload, names[i], nlen);
		tnnm_payload += nlen;
		out[tnnm_payload++] = 0; // NUL terminator
	}
	pos += 12 + tnnm_chunk_len;

	// 6. PACK
	memcpy (out + pos, "PACK\0\0\0\0", 8);
	wr_le32 (out + pos + 8, pack_len);
	u32 pack_payload = pos + 12;
	for (uint i = 0; i < n_entries; i++)
	{
		if (sorted[i].data && sorted[i].size)
			memcpy (out + pack_payload, sorted[i].data, sorted[i].size);
		pack_payload += sorted[i].size;
	}

	FREE (names);
	FREE (sorted);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// Nd Cube Wii U flat container (.bin / "PAC\0") -- Mario Party 10 /
// Animal Crossing: amiibo Festival
// ----------------------------------------------------------------------------
// A public QuickBMS script for this exact format was the lead that broke
// this open (RandomTBush/RTB-QuickBMS-Scripts, "MP10-Unpacker.bms", header
// comment: "ND Cube - BIN Extractor, Works with Mario Party 10 and Animal
// Crossing: amiibo Festival"); its `comtype COMP_UNZIP_DYNAMIC` line hinted
// this was some deflate variant rather than encryption. Verified by hand
// against the retail Amiibo Festival disc: every member's compressed bytes
// actually open with a completely ordinary zlib header (0x78 0xda) and
// decompress via plain `zlib.decompress()`/windowBits=15 -- it is *not*
// raw/headerless deflate, and it is *not* encrypted. A prior investigation
// of this same container wrongly called it encrypted; that pass simply
// never got as far as trying a correct decompress call against the right
// byte range. DecodeZlibGrow() already handles this directly (it tries
// windowBits=15 first, only falling back to -15/raw deflate if that
// fails), so no new inflate primitive is needed here.
//
// Big-endian u32 header (per the BMS script):
//   0x00 magic "PAC\0"          0x20 FILETOTAL
//   0x04 HEADERLENGTH           0x24..0x2f  4 blank u32
//   0x08 (blank)                0x30 (this codebase left it out; see below)
//   0x0c OVERALLFILESTART       0x34 LANGUAGESTART
//   0x10 PACSIZE                0x38 FILEHEADERSTART (entries table)
//   0x14 LANGUAGECOUNT          0x3c STRINGSTART (names)
//   0x18/0x1c 2 unknown u32     0x40 OVERALLFILESTART2 (data area)
// Per-language block (LANGUAGECOUNT * 16 bytes, at LANGUAGESTART):
//   LANGUAGENAME, unknown, LANGUAGEFILECOUNT, LANGUAGEOFFSETSTART
// Per-file entry (FILETOTAL * 0x30 bytes, at FILEHEADERSTART):
//   FILENAMESTART, unknown, EXTENSIONSTART, unknown,
//   FILESTART, SIZE, ZSIZE, ZSIZE2, blank, blank, unknown, blank
// Each entry's name is a NUL-terminated string at FILENAMESTART; its
// payload is ZSIZE zlib-compressed bytes (standard 0x78 0xda header) at
// FILESTART, inflating to exactly SIZE bytes.

enum { PAC_ENTRY_SIZE = 0x30 };

enumError ExtractPACArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".bin") && !is_ext_match (arg, ".pac"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x44 || memcmp (raw, "PAC\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_total       = rd_be32 (raw + 0x20);
	const u32 fileheaderstart  = rd_be32 (raw + 0x38);
	const u32 stringstart      = rd_be32 (raw + 0x3c);

	if (!file_total || file_total > 200000
		|| (u64)fileheaderstart + (u64)file_total * PAC_ENTRY_SIZE > raw_size
		|| stringstart >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAC:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_total, dest);

	uint n_ok = 0, n_fail = 0;
	for (uint i = 0; i < file_total; i++)
	{
		const u64 entry_pos    = (u64)fileheaderstart + (u64)i * PAC_ENTRY_SIZE;
		const u32 name_offset  = rd_be32 (raw + entry_pos);
		const u32 file_start   = rd_be32 (raw + entry_pos + 0x10);
		const u32 size         = rd_be32 (raw + entry_pos + 0x14);
		const u32 zsize        = rd_be32 (raw + entry_pos + 0x18);

		char name[PATH_MAX];
		if (name_offset < raw_size)
		{
			const char *s = (const char *)(raw + name_offset);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
		}
		else
			snprintf (name, sizeof (name), "file_%05u.bin", i);

		if (!zsize || (u64)file_start + zsize > raw_size)
		{
			n_fail++;
			continue;
		}

		u8 *decomp = 0;
		uint decomp_size = 0;
		if (DecodeZlibGrow (&decomp, &decomp_size, raw + file_start, zsize) != ERR_OK || !decomp)
		{
			n_fail++;
			continue;
		}
		if (size && decomp_size != size)
		{
			// Trust the actual inflate output over a mismatched header
			// field rather than truncating/padding it.
			if (verbose > 0)
				fprintf (stdlog,
					"PAC: %s decompressed to %u bytes, header says %u\n",
					name, decomp_size, size);
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode)
			SaveFile (out_path, 0, 0, decomp, decomp_size, 0);
		FREE (decomp);
		n_ok++;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "PAC: %u extracted, %u failed (of %u)\n", n_ok, n_fail, file_total);

	FREE (raw);
	return ERR_OK;
}
