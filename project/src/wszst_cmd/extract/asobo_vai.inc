// Asobo Studio GameCube stereo DSP (".vai": Ratatouille): rate @0, data size @4 (file = size + 0x4060), DSP coefficients @0xc (0x20 per channel), ADPCM from 0x4060
// interleaved every 0x4000 bytes; decoded to "<name>.wav" through synthetic DSP files (after vgmstream vai.c).
#include "lib-dsp.h"

static enumError extract_asobo_vai_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".vai") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint rate = sz > 0x4060 ? rd_be32 (raw) : 0;
	const size_t dsz = sz > 0x4060 ? rd_be32 (raw + 4) : 0, il = 0x4000;
	if (sz > 0x4060 && rate >= 8000 && rate <= 48000 && dsz + 0x4060 == sz)
	{
		const size_t cb = dsz / 2;
		const uint ns = (uint)(cb / 8 * 14);
		u8 *pcm[2] = { 0, 0 };
		bool ok = ns && ns < 0x8000000;
		for (uint c = 0; c < 2 && ok; c++)
		{
			u8 *d = CALLOC (1, 0x60 + cb);
			if (!d) { ok = false; break; }
			wr_be32 (d, ns);
			wr_be32 (d + 4, ns / 14 * 16);
			wr_be32 (d + 8, rate);
			memcpy (d + 0x1c, raw + 0xc + 0x20 * c, 32);
			for (size_t o = 0, k = 0; o < cb && ok; k++)
			{
				const size_t n = il < cb - o ? il : cb - o, so = 0x4060 + (k * 2 + c) * il;
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
			s16 *mix = MALLOC ((size_t)ns * 4);
			if (mix)
			{
				for (uint i = 0; i < ns; i++)
					for (uint c = 0; c < 2; c++)
						mix[(size_t)i * 2 + c] = (s16)(pcm[c][44 + 2 * i] | pcm[c][45 + 2 * i] << 8);
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".wav");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE ASOBO-VAI:%s (2 ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, rate, ns);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *w = nds_strm_make_wav (mix, ns, 2, rate, &wn);
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
