// Midway streaming banks (".ASF": TNA Impact! COMMSTRM/FEMUSIC/...; mixed endian): a 16-byte descriptor {u32 LE count (first only), u32 LE audio offset, u32 LE audio bytes, u32 LE flags 0x0201}
// followed by a 4-character track tag ("MTYT", "MTYU", "M_01", ...) and a standard 0x60-byte GameCube DSP-ADPCM header (big endian), repeated every 0x70 bytes (entry k's descriptor at 0x70*k, the tag at 0x70*k + 0x10).
// Each entry's mono ADPCM data sits at its absolute audio offset. Decoded to "NNNN_<tag>.wav" in "<name>.ASF.d/" via `wszst extract --decode`.
#include "lib-dsp.h"

static enumError extract_midway_asf_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".asf") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t count = sz > 0x80 ? rd_le32 (raw) : 0;
	if (count && count < 0x10000 && 0x10 + 0x70 * count <= sz && rd_le32 (raw + 0xc) == 0x201 && raw[0x10] >= 0x20 && raw[0x10] < 0x7f)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint wrote = 0;
		enumError werr = ERR_OK;
		for (size_t k = 0; k < count && !werr; k++)
		{
			const u8 *ds = raw + 0x70 * k;
			const size_t off = rd_le32 (ds + 4), len = rd_le32 (ds + 8);
			const u8 *hd = ds + 0x14;
			const uint ns = rd_be32 (hd), nib = rd_be32 (hd + 4), rate = rd_be32 (hd + 8);
			if (!ns || !len || off + len > sz || rate < 2000 || rate > 96000 || nib / 2 > len + 1 || ns > 0x10000000)
				continue;
			u8 *d = CALLOC (1, 0x60 + len);
			u8 *w = 0;
			size_t wn = 0;
			if (!d)
				continue;
			memcpy (d, hd, 0x60);
			memcpy (d + 0x60, raw + off, len);
			if (!DecodeDSPToWAV (d, 0x60 + len, &w, &wn) && wn >= 44)
			{
				char name[32], tag[8];
				snprintf (tag, sizeof (tag), "%.4s", (ccp)ds + 0x10);
				for (char *c = tag; *c; c++)
					if (!isalnum ((u8)*c) && *c != '_')
						*c = '_';
				if (!tag[0])
					strcpy (tag, "trk");
				snprintf (name, sizeof (name), "%04zu_%s.wav", k, tag);
				if (!wrote && (verbose >= 0 || testmode))
					fprintf (stdlog, "%s%sDECODE MIDWAY-ASF:%s (%zu tracks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
				werr = shade_write (arg, dest, basedir, name, w, (uint)wn);
				wrote++;
			}
			FREE (w);
			FREE (d);
		}
		if (wrote)
			err = werr;
	}
	FREE (raw);
	return err;
}
