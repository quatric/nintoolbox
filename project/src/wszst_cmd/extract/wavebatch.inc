// Firebrand Games wave bank (".wavebatch": "TABW", Need for Speed: The Run, Cars 2, Fast & Furious: Showdown, WRC; 3DS): BOM u16 @4 (0xFEFF = big endian, else LE),
// version @6 (6 or 7), subsong count @8, name table size @0xc, "unknown" count @0x10, "samples" count @0x14; after the name table and those two word tables come
// 0x24 byte headers {name offset, codec (0 PCM16, 1 PCM8, 2 DSP-ADPCM), rate, channels, ..., stream offset @0x18, stream size @0x1c, samples x channels @0x20} and the
// data. DSP streams start with (0x20 coefs + 0x14 history) per channel plus 0x0c padding, then the channels back to back. Every subsong is written as
// "<file>.d/<name>.wav" (after vgmstream wavebatch.c).
#include "lib-dsp.h"

static enumError extract_wavebatch_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".wavebatch") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x40 && !memcmp (raw, "TABW", 4))
	{
		const bool be = raw[4] == 0xFE && raw[5] == 0xFF;
		#define B32(o) (be ? rd_be32 (raw + (o)) : rd_le32 (raw + (o)))
		const uint ver = be ? rd_be16 (raw + 6) : rd_le16 (raw + 6), nsub = B32 (8);
		const size_t names = B32 (0xc), unk = B32 (0x10), smp = B32 (0x14);
		size_t hdr = 0x1c + names + unk * 4 + smp * 4;
		if ((ver == 6 || ver == 7) && nsub && nsub < 0x10000 && hdr + 0x24 * (size_t)nsub <= sz)
		{
			const size_t data = hdr + 0x24 * (size_t)nsub;
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			for (uint s = 0; s < nsub; s++)
			{
				const size_t h = hdr + 0x24 * (size_t)s;
				const size_t no = 0x1c + B32 (h);
				const uint codec = B32 (h + 4), rate = B32 (h + 8), ch = B32 (h + 0xc);
				const size_t so = data + B32 (h + 0x18), ss = B32 (h + 0x1c);
				if (ch < 1 || ch > 2 || rate < 1000 || rate > 192000 || codec > 2 || so + ss > sz || no >= sz)
					continue;
				const uint ns = B32 (h + 0x20) / ch;
				s16 *mix = ns && ns < 0x8000000 ? CALLOC ((size_t)ns * ch, sizeof (s16)) : 0;
				bool ok = mix != 0;
				if (codec == 2)
				{
					const size_t cfg = (0x20 + 0x14) * (size_t)ch + 0x0c * ch, per = ss > cfg ? (ss - cfg) / ch : 0;
					for (uint c = 0; c < ch && ok; c++)
					{
						u8 *d = CALLOC (1, 0x60 + per);
						u8 *w = 0;
						size_t wn = 0;
						if (!d || !per) { FREE (d); ok = false; break; }
						wr_be32 (d, ns);
						wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
						wr_be32 (d + 8, rate);
						for (uint k = 0; k < 16; k++)
						{
							const u8 *p = raw + so + 0x34 * c + 2 * k;
							d[0x1c + 2 * k] = be ? p[0] : p[1];
							d[0x1d + 2 * k] = be ? p[1] : p[0];
						}
						{
							const u8 *p = raw + so + 0x34 * c + 0x20;
							d[0x40] = be ? p[0] : p[1]; d[0x41] = be ? p[1] : p[0];
							d[0x42] = be ? p[2] : p[3]; d[0x43] = be ? p[3] : p[2];
						}
						const size_t need = (size_t)(ns + 13) / 14 * 8;
						memcpy (d + 0x60, raw + so + cfg + per * c, per < need ? per : need);
						if (DecodeDSPToWAV (d, 0x60 + per, &w, &wn) || wn < 44 + (size_t)ns * 2)
							ok = false;
						else
							for (uint i = 0; i < ns; i++)
								mix[(size_t)i * ch + c] = (s16)(w[44 + 2 * i] | w[45 + 2 * i] << 8);
						FREE (w);
						FREE (d);
					}
				}
				else
				{
					const uint bps = codec == 0 ? 2 : 1;
					for (size_t i = 0; i < (size_t)ns * ch && (i + 1) * bps <= ss; i++)
						mix[i] = codec == 0 ? (s16)(be ? rd_be16 (raw + so + 2 * i) : rd_le16 (raw + so + 2 * i)) : (s16)((s8)raw[so + i] << 8);
				}
				if (ok)
				{
					char name[PATH_MAX];
					snprintf (name, sizeof (name), "%.200s.wav", (ccp)raw + no);
					for (char *c = name; *c; c++)
						if (*c == '/' || *c == '\\')
							*c = '_';
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sDECODE WAVEBATCH:%s %s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, name, ch, rate, ns);
					if (testmode)
						err = ERR_OK;
					else
					{
						size_t wn = 0;
						u8 *wv = nds_strm_make_wav (mix, ns, ch, rate, &wn);
						if (wv && !shade_write (arg, dest, basedir, name, wv, (uint)wn))
							err = ERR_OK;
						FREE (wv);
					}
				}
				FREE (mix);
			}
		}
		#undef B32
	}
	FREE (raw);
	return err;
}
