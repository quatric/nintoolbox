// Codemasters 3DS "i3DS" interleaved DSP (".3ds": F1 2011): data size @8, interleave @0xc (0 = mono), then one or two CWAV headers (0xc0 each) from 0x10 (rate @+0x4c,
// samples @+0x54, little-endian DSP coefficients @+0x7c), "DATA" and the ADPCM interleaved from (0x10 + 0xc0 * channels + 0x10); decoded to "<name>.wav"
// through synthetic DSP files (after vgmstream i3ds.c).
#include "lib-dsp.h"

static enumError extract_i3ds_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".3ds") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x200 && !memcmp (raw, "i3DS", 4) && !memcmp (raw + 0x10, "CWAV", 4))
	{
		const size_t dsz = rd_le32 (raw + 8), il = rd_le32 (raw + 0xc);
		const uint ch = il ? 2 : 1, rate = rd_le32 (raw + 0x10 + 0x4c), ns = rd_le32 (raw + 0x10 + 0x54);
		const size_t start = 0x10 + 0xc0 * ch + 0x10;
		size_t total = dsz > start ? dsz - start : 0;
		if (start + total > sz)
			total = sz > start ? sz - start : 0;
		const size_t cb = (size_t)(ns + 13) / 14 * 8;
		if (rate >= 1000 && rate <= 192000 && ns && ns < 0x8000000 && total >= cb * ch && (!il || il <= 0x100000))
		{
			u8 *pcm[2] = { 0, 0 };
			bool ok = true;
			for (uint c = 0; c < ch && ok; c++)
			{
				u8 *d = CALLOC (1, 0x60 + cb);
				if (!d) { ok = false; break; }
				wr_be32 (d, ns);
				wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
				wr_be32 (d + 8, rate);
				for (uint k = 0; k < 16; k++)
				{
					const u8 *p = raw + 0x10 + 0x7c + 0xc0 * c + 2 * k;
					d[0x1c + 2 * k] = p[1];
					d[0x1d + 2 * k] = p[0];
				}
				if (ch == 1)
					memcpy (d + 0x60, raw + start, cb);
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
						fprintf (stdlog, "%s%sDECODE I3DS:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
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
	}
	FREE (raw);
	return err;
}
