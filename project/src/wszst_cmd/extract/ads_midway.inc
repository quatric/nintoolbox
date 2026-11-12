// Midway GameCube ".ads" ("dhSS" ... "dbSS", big endian; Gauntlet: Dark Legacy): rate @0xc, channels @0x10, interleave @0x14, codec @8 (0x20 = DSP-ADPCM; the Xbox
// IMA variant is not handled); a standard 0x60 byte DSP header per channel from 0x28, ADPCM after them with the channels interleaved. Decoded to "<name>.wav"
// (after vgmstream ads_midway.c).
#include "lib-dsp.h"

static enumError extract_ads_midway_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ads") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint ch = sz > 0x100 ? rd_be32 (raw + 0x10) : 0, rate = sz > 0x100 ? rd_be32 (raw + 0xc) : 0;
	if (sz > 0x100 && !memcmp (raw, "dhSS", 4) && !memcmp (raw + 0x20, "dbSS", 4) && rd_be32 (raw + 8) == 0x20 && ch >= 1 && ch <= 2 && rate >= 1000 && rate <= 192000)
	{
		const size_t il = ch == 2 ? rd_be32 (raw + 0x14) : 0, start = 0x28 + 0x60 * ch;
		const uint ns = rd_be32 (raw + 0x28);
		const size_t cb = (size_t)(ns + 13) / 14 * 8;
		u8 *pcm[2] = { 0, 0 };
		bool ok = ns && ns < 0x8000000 && start < sz && (ch == 1 || (il >= 8 && il < 0x1000000));
		for (uint c = 0; c < ch && ok; c++)
		{
			u8 *d = CALLOC (1, 0x60 + cb);
			if (!d) { ok = false; break; }
			memcpy (d, raw + 0x28 + 0x60 * c, 0x60);
			wr_be32 (d + 8, rate);
			wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
			wr_be16 (d + 0xe, 0);
			if (ch == 1)
			{
				memcpy (d + 0x60, raw + start, cb < sz - start ? cb : sz - start);
			}
			else
				for (size_t o = 0, k = 0; o < cb && ok; k++)
				{
					const size_t n = il < cb - o ? il : cb - o, so = start + (k * 2 + c) * il;
					if (so + n > sz) { ok = false; break; }
					memcpy (d + 0x60 + o, raw + so, n);
					o += n;
				}
			size_t wn = 0;
			if (ok && (DecodeDSPToWAV (d, 0x60 + cb, &pcm[c], &wn) || wn < 44 + (size_t)ns * 2))
				ok = false;
			FREE (d);
		}
		if (ok)
		{
			s16 *mix = MALLOC ((size_t)ns * ch * 2);
			if (mix)
			{
				for (uint i = 0; i < ns; i++)
					for (uint c = 0; c < ch; c++)
						mix[(size_t)i * ch + c] = (s16)(pcm[c][44 + 2 * i] | pcm[c][45 + 2 * i] << 8);
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".wav");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE ADS-MIDWAY:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *w = nds_strm_make_wav (mix, ns, ch, rate, &wn);
					err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
					FREE (w);
				}
				FREE (mix);
			}
		}
		FREE (pcm[0]);
		FREE (pcm[1]);
	}
	FREE (raw);
	return err;
}
