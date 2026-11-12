// Inevitable Entertainment GameCube "IDSP" (".idsp": Defender): data size @4, rate @8, channels @0xc, interleave @0x10, DSP coefficients @0x14 (0x2e per channel), ADPCM
// from 0x70 interleaved by blocks; decoded to "<name>.wav" (after vgmstream idsp_ie.c). The Namco, Traveller's Tales and Next Level IDSP variants are recognised by
// dspwrap.inc, which is tried first for those.
#include "lib-dsp.h"

static enumError extract_idsp_ie_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".idsp") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t bytes = sz > 0x70 ? rd_be32 (raw + 4) : 0, il = sz > 0x70 ? rd_be32 (raw + 0x10) : 0;
	const uint rate = sz > 0x70 ? rd_be32 (raw + 8) : 0, ch = sz > 0x70 ? rd_be32 (raw + 0xc) : 0;
	if (sz > 0x70 && !memcmp (raw, "IDSP", 4) && rate >= 4000 && rate <= 192000 && ch >= 1 && ch <= 2 && bytes && 0x70 + bytes <= sz + 0x10 && il >= 8 && il < 0x1000000)
	{
		const size_t cb = bytes / ch;
		const uint ns = (uint)(cb / 8 * 14);
		u8 *pcm[2] = { 0, 0 };
		bool ok = ns && ns < 0x8000000;
		for (uint c = 0; c < ch && ok; c++)
		{
			u8 *d = CALLOC (1, 0x60 + cb);
			if (!d) { ok = false; break; }
			wr_be32 (d, ns);
			wr_be32 (d + 4, ns / 14 * 16);
			wr_be32 (d + 8, rate);
			memcpy (d + 0x1c, raw + 0x14 + 0x2e * c, 32);
			if (ch == 1)
				memcpy (d + 0x60, raw + 0x70, cb < sz - 0x70 ? cb : sz - 0x70);
			else
				for (size_t o = 0, k = 0; o < cb && ok; k++)
				{
					const size_t n = il < cb - o ? il : cb - o, so = 0x70 + (k * 2 + c) * il;
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
					fprintf (stdlog, "%s%sDECODE IDSP-IE:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
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
