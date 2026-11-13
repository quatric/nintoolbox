// Sega "gcax" sound banks (".dat": Sonic and the Secret Rings, Sonic and the Black Knight; Wii, big endian): "gcaxDTPK" {u32 @0x10 table size, u32 @0x1c "gcaxPCMD" offset}
// holding a "gcaxTBLD" table @0x60 and the "gcaxPCMD" sample pool. TBLD words from 0x68: the section offsets live at word 12.., the sample section is word 20 (end = word 21):
// {u32 count-1, 0x20, 2, checksum} followed by count+1 records of 0x44 bytes {DSP coefficients[16], gain u16, initial ps u16, hist1, hist2, loop ps, loop hist1, loop hist2,
// flags u16 (0x0200 DSP-ADPCM, +0x0400 looped), rate u16, pad u16, byte length, cumulative end offset (relative to the PCMD tag), 2 unknown words}. Sample k's data is the
// range from the previous record's end (0x20 for the first) up to its own end, DSP-ADPCM mono, and starts with the record's initial ps. Every sample is decoded to
// "<name>.dat.d/NNNN.wav" (mono, one-shot; the loop start is not known).
#include "lib-dsp.h"

static enumError extract_gcax_bank_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".dat") || (export_count == 0 && !opt_decode) || !file_has_magic (arg, "gcaxDTPK", 8))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	size_t pcmd = sz > 0x20 ? rd_be32 (raw + 0x1c) : 0;
	if (sz > 0x100 && !memcmp (raw + 0x60, "gcaxTBLD", 8) && pcmd + 0x20 < sz && !memcmp (raw + pcmd, "gcaxPCMD", 8))
	{
		const size_t sec = rd_be32 (raw + 0x68 + 4 * 20), secend = rd_be32 (raw + 0x68 + 4 * 21);
		const u8 *pc = raw + pcmd;
		const size_t pcsz = sz - pcmd;
		if (sec >= 0xc0 && sec + 0x20 < sz && secend > sec && secend <= sz + 4 && rd_be32 (raw + sec + 4) == 0x20)
		{
			const uint n = rd_be32 (raw + sec) + 1;
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			size_t prev = 0x20;
			uint wrote = 0;
			enumError werr = ERR_OK;
			for (uint k = 0; k < n && !werr; k++)
			{
				const size_t r = sec + 0x10 + (size_t)k * 0x44;
				if (r + 0x40 > sz)
					break;
				const u8 *rec = raw + r;
				const uint flags = rd_be16 (rec + 0x2e), rate = rd_be16 (rec + 0x30);
				const size_t len = rd_be32 (rec + 0x34), end = rd_be32 (rec + 0x38);
				const uint ps = rd_be16 (rec + 0x22);
				size_t start = prev, e = end;
				if (e <= start || e > pcsz)
					e = len ? ((start + len + 7) & ~(size_t)7) : 0;
				if (e)
					prev = e <= pcsz ? e : prev;
				// Sample-less placeholders and non-ADPCM records carry no usable range.
				if ((flags >> 8 & ~4u) != 2 || rate < 2000 || rate > 96000 || e <= start || e > pcsz || e - start < 8 || (uint)pc[start] != ps)
					continue;
				const size_t bytes = (e - start) & ~(size_t)7;
				const uint ns = (uint)(bytes / 8 * 14);
				u8 *d = CALLOC (1, 0x60 + bytes);
				u8 *w = 0;
				size_t wn = 0;
				if (!d)
					continue;
				wr_be32 (d, ns);
				wr_be32 (d + 4, ns / 14 * 16);
				wr_be32 (d + 8, rate);
				memcpy (d + 0x1c, rec, 32);
				wr_be16 (d + 0x3e, ps);
				wr_be16 (d + 0x40, rd_be16 (rec + 0x24));
				wr_be16 (d + 0x42, rd_be16 (rec + 0x26));
				memcpy (d + 0x60, pc + start, bytes);
				if (!DecodeDSPToWAV (d, 0x60 + bytes, &w, &wn) && wn >= 44)
				{
					char name[32];
					snprintf (name, sizeof (name), "%04u.wav", k);
					if (!wrote && (verbose >= 0 || testmode))
						fprintf (stdlog, "%s%sDECODE GCAX-BANK:%s (%u records) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
					werr = shade_write (arg, dest, basedir, name, w, (uint)wn);
					wrote++;
				}
				FREE (w);
				FREE (d);
			}
			if (wrote)
				err = werr;
		}
	}
	FREE (raw);
	return err;
}
