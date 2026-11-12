// Yuke's GameCube "YDSP" streams (".ydsp": WWE Day of Reckoning, WrestleMania XIX): "YDSP", data bytes @8 (all channels), rate @0xc, channels u16 @0x10,
// interleave @0x14, DSP coefs @0x20 (0x24 per channel), DSP-ADPCM from 0x120 interleaved by blocks of `interleave` bytes; decoded to "<name>.wav" through
// synthetic per-channel DSP files and DecodeDSPToWAV (after vgmstream ydsp.c).
#include "lib-dsp.h"

static enumError extract_ydsp_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ydsp") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint rate = sz > 0x120 ? rd_be32 (raw + 0xc) : 0, ch = sz > 0x120 ? rd_be16 (raw + 0x10) : 0, il = sz > 0x120 ? rd_be32 (raw + 0x14) : 0;
	const size_t total = sz > 0x120 ? rd_be32 (raw + 8) : 0;
	if (sz > 0x120 && !memcmp (raw, "YDSP", 4) && rate >= 1000 && rate <= 192000 && ch >= 1 && ch <= 2 && total && il >= 8 && il < 0x1000000
		&& 0x20 + ch * 0x24 <= 0x120)
	{
		const size_t chbytes = total / ch, avail = sz - 0x120;
		const size_t cb = chbytes < avail / ch ? chbytes : avail / ch;
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
			memcpy (d + 0x1c, raw + 0x20 + c * 0x24, 32);
			for (size_t o = 0, pos = 0; o < cb; )
			{
				const size_t n = il < cb - o ? il : cb - o;
				const size_t so = 0x120 + (pos * ch + c) * il;
				if (so + n > sz) { ok = false; break; }
				memcpy (d + 0x60 + o, raw + so, n);
				o += n;
				pos++;
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
					fprintf (stdlog, "%s%sDECODE YDSP:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *wv = nds_strm_make_wav (mix, ns, ch, rate, &wn);
					err = wv ? shade_write (arg, dest, basedir, name, wv, (uint)wn) : ERR_OUT_OF_MEMORY;
					FREE (wv);
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
