// SPDX-License-Identifier: GPL-2.0+
// See lib-csfnt.h for the layout.
#include "lib-csfnt.h"
#include "lib-archive-util.h"
#include <ctype.h>
#include <string.h>

#define CSFNT_TABLE_SIZE 256
#define CSFNT_HEADER_SIZE 12
#define CSFNT_REC_SIZE 12
#define CSFNT_MAX_USED 65536

enumError ExtractCSFontArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < CSFNT_HEADER_SIZE + CSFNT_TABLE_SIZE + 4 || memcmp (raw, "TNF.", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 first_char = rd_be16 (raw + 8);
	const u32 used_count = rd_be16 (raw + 10);
	const u8 *table = raw + CSFNT_HEADER_SIZE;
	const u64 glyph_off = CSFNT_HEADER_SIZE + CSFNT_TABLE_SIZE + 4;
	const u64 expect_size = glyph_off + (u64)used_count * CSFNT_REC_SIZE;

	// Gate on the invariant this layout was recovered from: the file's own
	// size must account for the header, the fixed 256-entry table, the
	// skipped pointer word, and exactly used_count glyph records -- with
	// nothing left over.
	if (used_count > CSFNT_MAX_USED || expect_size != raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT FNT:%s (%u glyphs) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, used_count, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/glyphs.txt", dest);

		const size_t cap = (size_t)CSFNT_TABLE_SIZE * 64 + 128;
		char *text = MALLOC (cap);
		size_t len = (size_t)snprintf (text, cap,
			"# first_used_char=%u\n# char\tglyph\tx\ty\twidth\theight\tadvance\n", first_char);
		for (u32 code = 0; code < CSFNT_TABLE_SIZE; code++)
		{
			const u8 glyph = table[code];
			if (glyph == 0xff || glyph >= used_count)
				continue;
			const u8 *rec = raw + glyph_off + (u64)glyph * CSFNT_REC_SIZE;
			const uint x = rd_be16 (rec), y = rd_be16 (rec + 2), x_end = rd_be16 (rec + 4),
					   height = rd_be16 (rec + 6), advance = rd_be16 (rec + 10);
			const char printable = isprint (code) ? (char)code : '.';
			len += (size_t)snprintf (text + len, cap - len, "%u('%c')\t%u\t%u\t%u\t%u\t%u\t%u\n",
				code, printable, glyph, x, y, x_end - x, height, advance);
		}
		if (SaveFile (out, 0, 0, (const u8 *)text, (uint)len, 0))
			err = ERR_CANT_CREATE;
		FREE (text);
	}

	FREE (raw);
	(void)depth;
	return err;
}
