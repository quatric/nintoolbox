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
