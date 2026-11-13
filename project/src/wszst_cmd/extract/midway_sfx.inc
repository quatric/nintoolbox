// Midway sound-effect banks (".sfx" members of TNA Impact! WADs; big endian): u32 0, count @4, 0xc @8, first sample offset @0xc, then count 20-byte entries from 0x10
// {0, 0, byte size of the sample record, name offset, end offset of the record}. A sample record is a 0x60-byte GameCube DSP-ADPCM header followed by its ADPCM data, padded to 16 bytes,
// and records follow each other from the first sample offset (a record starts where the previous one ended). NUL-terminated names follow the entry table. Decoded to
// "NNNN_<name>.wav" in "<name>.sfx.d/" via `wszst extract --decode`.
#include "lib-dsp.h"

static enumError extract_midway_sfx_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".sfx") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t count = sz > 0x20 ? rd_be32 (raw + 4) : 0, first = sz > 0x20 ? rd_be32 (raw + 12) : 0;
	if (sz > 0x20 && !rd_be32 (raw) && rd_be32 (raw + 8) == 0xc && count && count <= 4096 && 0x10 + 20 * count <= first && first + 0x60 <= sz)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		size_t start = first;
		uint wrote = 0;
		enumError werr = ERR_OK;
		for (size_t k = 0; k < count && !werr; k++)
		{
			const u8 *e = raw + 0x10 + 20 * k;
			const size_t end = rd_be32 (e + 16), noff = rd_be32 (e + 12);
			if (end <= start || end > sz + 15)
				break;
			const size_t cur = start;
			start = end;
			const u8 *hd = raw + cur;
			const uint ns = rd_be32 (hd), nib = rd_be32 (hd + 4), rate = rd_be32 (hd + 8);
			const size_t bytes = ((size_t)nib + 1) / 2;
			if (!ns || rate < 2000 || rate > 96000 || cur + 0x60 + bytes > sz)
				continue;
			u8 *d = CALLOC (1, 0x60 + bytes);
			u8 *w = 0;
			size_t wn = 0;
			if (!d)
				continue;
			memcpy (d, hd, 0x60);
			memcpy (d + 0x60, hd + 0x60, bytes);
			if (!DecodeDSPToWAV (d, 0x60 + bytes, &w, &wn) && wn >= 44)
			{
				char nm[40] = "", name[64];
				if (noff < sz)
					snprintf (nm, sizeof (nm), "%.32s", (ccp)raw + noff);
				for (char *c = nm; *c; c++)
					if (!isalnum ((u8)*c) && *c != '_' && *c != '-')
						*c = '_';
				snprintf (name, sizeof (name), "%04zu_%s.wav", k, nm);
				if (!wrote && (verbose >= 0 || testmode))
					fprintf (stdlog, "%s%sDECODE MIDWAY-SFX:%s (%zu sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
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
