// Koei Tecmo Switch "KTSS" audio (".kns" / ".kno" / ".ktss": Hyrule Warriors, Fire Emblem Warriors, Atelier Lydie & Suelle): u32 data size @4, codec @0x20 (2 =
// DSP-ADPCM; 9 = Opus is not handled here), version @0x22, data start = u32 @0x24 + 0x20, tracks @0x28, channels per track @0x29, rate @0x2c, samples @0x30; DSP
// coefficients are little-endian s16 x16 per channel at 0x40 (version 1, spacing 0x2e) or 0x5c (version 3, spacing 0x60), channels interleave every 8 bytes.
// Decoded to "<name>.wav" via synthetic DSP files (after vgmstream ktss.c).
#include "lib-dsp.h"

static enumError extract_ktss_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".kns") && !is_ext (arg, ".kno") && !is_ext (arg, ".ktss")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x80 && !memcmp (raw, "KTSS", 4) && raw[0x20] == 2 && (raw[0x22] == 1 || raw[0x22] == 3))
	{
		const uint ver = raw[0x22], ch = raw[0x28] * raw[0x29], rate = rd_le32 (raw + 0x2c), ns = rd_le32 (raw + 0x30);
		const size_t start = (size_t)rd_le32 (raw + 0x24) + 0x20, cof = ver == 1 ? 0x40 : 0x5c, sp = ver == 1 ? 0x2e : 0x60;
		if (ch >= 1 && ch <= 12 && rate >= 1000 && rate <= 192000 && ns && ns < 0x8000000 && start < sz && cof + sp * (ch - 1) + 32 <= sz)
		{
			const size_t cb = (size_t)(ns + 13) / 14 * 8, il = 8;
			u8 *pcm[12] = { 0 };
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
					const u8 *p = raw + cof + sp * c + 2 * k;
					d[0x1c + 2 * k] = p[1];
					d[0x1d + 2 * k] = p[0];
				}
				for (size_t o = 0, k = 0; o < cb; k++, o += il)
				{
					const size_t so = start + (k * ch + c) * il;
					if (so + il > sz) { if (so >= sz) ok = false; break; }
					memcpy (d + 0x60 + o, raw + so, il < cb - o ? il : cb - o);
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
						fprintf (stdlog, "%s%sDECODE KTSS:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
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
			for (uint c = 0; c < ch; c++)
				FREE (pcm[c]);
		}
	}
	FREE (raw);
	return err;
}
