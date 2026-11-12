// GameCube DSP-ADPCM files with non-standard wrappers, decoded to "<name>.wav" through DecodeDSPToWAV (after vgmstream lps.c / dsp_kceje.c / ymf.c):
//   Rave Master ".lps" and KCE Japan East ".dsp" (Yu-Gi-Oh! Falsebound Kingdom, Hikaru no Go 3): u32 BE data size @0 (file = size + 0xe0), 1 @4, 0x10000000 @8,
//     0 @0xc; a standard 0x60 byte DSP header at 0x20 and mono ADPCM from 0xe0.
//   Yuke's WrestleMania X8 ".ymf": 0x180 @0, 3 @8, 0xCCCCCCCC @0xc; data start = u32 @0, rate @0xa8, samples @0xdc, coefs @0xae (0x60 per channel), stereo
//     with 0x20000 byte interleave.
#include "lib-dsp.h"

static enumError gcdsp_write (ccp arg, ccp basedir, const u8 *wav, size_t wn, ccp tag)
{
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const char *sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot) strcpy (dot, ".wav");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", tag, arg);
	return testmode ? ERR_OK : shade_write (arg, dest, basedir, name, wav, (uint)wn);
}

static enumError extract_gcdsp_misc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool lps = is_ext (arg, ".lps") || is_ext (arg, ".dsp"), ymf = is_ext (arg, ".ymf");
	if ((!lps && !ymf) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (lps && sz > 0xe0 && (size_t)rd_be32 (raw) + 0xe0 == sz && rd_be32 (raw + 4) == 1 && rd_be32 (raw + 8) == 0x10000000 && !rd_be32 (raw + 12))
	{
		u8 *d = CALLOC (1, sz - 0x80);
		u8 *w = 0;
		size_t wn = 0;
		if (d)
		{
			memcpy (d, raw + 0x20, 0x60);
			memcpy (d + 0x60, raw + 0xe0, sz - 0xe0);
			if (!DecodeDSPToWAV (d, sz - 0x80, &w, &wn))
				err = gcdsp_write (arg, basedir, w, wn, "GC-DSP");
		}
		FREE (w);
		FREE (d);
	}
	else if (ymf && sz > 0x200 && rd_be32 (raw) == 0x180 && rd_be32 (raw + 8) == 3 && rd_be32 (raw + 12) == 0xCCCCCCCC)
	{
		const size_t start = rd_be32 (raw), il = 0x20000;
		const uint rate = rd_be32 (raw + 0xa8), ns = rd_be32 (raw + 0xdc);
		const size_t cb = (size_t)(ns + 13) / 14 * 8;
		u8 *pcm[2] = { 0, 0 };
		bool ok = rate >= 1000 && rate <= 192000 && ns && ns < 0x8000000 && start < sz;
		for (uint c = 0; c < 2 && ok; c++)
		{
			u8 *d = CALLOC (1, 0x60 + cb);
			if (!d) { ok = false; break; }
			wr_be32 (d, ns);
			wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
			wr_be32 (d + 8, rate);
			memcpy (d + 0x1c, raw + 0xae + c * 0x60, 32);
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
			s16 *mix = MALLOC ((size_t)ns * 4);
			if (mix)
			{
				for (uint i = 0; i < ns; i++)
					for (uint c = 0; c < 2; c++)
						mix[(size_t)i * 2 + c] = (s16)(pcm[c][44 + 2 * i] | pcm[c][45 + 2 * i] << 8);
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (mix, ns, 2, rate, &wn);
				if (w)
					err = gcdsp_write (arg, basedir, w, wn, "YMF");
				FREE (w);
				FREE (mix);
			}
		}
		FREE (pcm[0]);
		FREE (pcm[1]);
	}
	FREE (raw);
	return err;
}
