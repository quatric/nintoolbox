// WayForward "EngineBlack" waves (".wave": Mighty Switch Force!, Adventure Time: Hey Ice King!, Happy Feet Two; 3DS / DS / Wii): magic "VAW3", "WWAV", 0xE5B7ECFE (LE or
// BE) or 0xC9FB0C03 (DS), version @4, file size @8, rate as a float @0xc, samples @0x10, loop start/end @0x14/0x18, codec u8 @0x1c, channels u8 @0x1d, data start @0x20,
// interleave @0x24, extra data @0x28 (always 0x2c). Codec 0 PCM8, 1 PCM16 BE, 2 DSP-ADPCM (3DS; DS files use 2 for IMA when the data starts at 0x40 or earlier), 3 IMA.
// Decoded to "<name>.wav" (after vgmstream wave.c).
#include "lib-dsp.h"

static enumError extract_wayforward_wave_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".wave") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const u32 m = sz > 0x30 ? rd_be32 (raw) : 0;
	const bool vaw3 = sz > 0x30 && !memcmp (raw, "VAW3", 4), wwav = sz > 0x30 && !memcmp (raw, "WWAV", 4);
	if (sz > 0x30 && (vaw3 || wwav || m == 0xE5B7ECFE || rd_le32 (raw) == 0xE5B7ECFE || m == 0xC9FB0C03))
	{
		const bool be = m == 0xE5B7ECFE || wwav;
		#define W32(o) (be ? rd_be32 (raw + (o)) : rd_le32 (raw + (o)))
		const uint version = W32 (4);
		if (W32 (8) == sz && !raw[0x1e] && !raw[0x1f])
		{
			u32 fb = W32 (0xc);
			float rf;
			memcpy (&rf, &fb, 4);
			const uint rate = (uint)rf, ns = W32 (0x10), ch = raw[0x1d];
			uint codec = raw[0x1c];
			const size_t start = W32 (0x20), il = W32 (0x24), extra = W32 (0x28);
			if (codec == 0 && version == 0x50000 && start > 0x40)
				codec = 2;
			else if (codec == 2 && start <= 0x40)
				codec = 3;
			s16 *mix = rate >= 1000 && rate <= 192000 && ns && ns < 0x8000000 && ch >= 1 && ch <= 2 && start < sz && il && (il < 0x1000000 || ch == 1)
				? CALLOC ((size_t)ns * ch, sizeof (s16)) : 0;
			bool ok = mix != 0 && codec <= 3;
			const size_t per = codec == 2 ? (size_t)(ns + 13) / 14 * 8 : codec == 3 ? (ns + 1) / 2 : codec == 1 ? (size_t)ns * 2 : ns;
			for (uint c = 0; c < ch && ok; c++)
			{
				u8 *buf = CALLOC (1, per + 0x80);
				if (!buf) { ok = false; break; }
				for (size_t o = 0, k = 0; o < per; k++)
				{
					const size_t n = ch == 1 ? per : (il < per - o ? il : per - o), so = start + (ch == 1 ? 0 : (k * ch + c) * il);
					if (so >= sz) break;
					const size_t t = n < sz - so ? n : sz - so;
					memcpy (buf + o, raw + so, t < per - o ? t : per - o);
					o += n;
					if (ch == 1) break;
				}
				if (codec == 2)
				{
					const size_t hs = be ? 0x24 : 0x22, spc = version == 0x50000 ? 0x2e : 0x2c, base = extra + spc * c;
					u8 *d = CALLOC (1, 0x60 + per);
					u8 *w = 0;
					size_t wn = 0;
					if (!d || base + hs + 4 > sz) { FREE (d); FREE (buf); ok = false; break; }
					wr_be32 (d, ns);
					wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
					wr_be32 (d + 8, rate);
					for (uint k = 0; k < 16; k++)
					{
						const u8 *p = raw + base + 2 * k;
						d[0x1c + 2 * k] = be ? p[0] : p[1];
						d[0x1d + 2 * k] = be ? p[1] : p[0];
					}
					{
						const u8 *p = raw + base + (version == 0x50000 ? 0x24 : 0x22);
						d[0x40] = be ? p[0] : p[1]; d[0x41] = be ? p[1] : p[0];
						d[0x42] = be ? p[2] : p[3]; d[0x43] = be ? p[3] : p[2];
					}
					memcpy (d + 0x60, buf, per);
					if (DecodeDSPToWAV (d, 0x60 + per, &w, &wn) || wn < 44 + (size_t)ns * 2)
						ok = false;
					else
						for (uint i = 0; i < ns; i++)
							mix[(size_t)i * ch + c] = (s16)(w[44 + 2 * i] | w[45 + 2 * i] << 8);
					FREE (w);
					FREE (d);
				}
				else if (codec == 3)
				{
					u8 *b2 = CALLOC (1, per + 8);
					s16 *t = CALLOC (ns + 4, sizeof (s16));
					if (b2 && t)
					{
						memcpy (b2 + 4, buf, per);
						const uint got = nds_strm_ima_block (b2, (uint)per + 4, t, ns);
						for (uint i = 0; i < got; i++)
							mix[(size_t)i * ch + c] = t[i];
					}
					FREE (b2);
					FREE (t);
				}
				else
					for (uint i = 0; i < ns; i++)
						mix[(size_t)i * ch + c] = codec == 1 ? (s16)(buf[2 * i] << 8 | buf[2 * i + 1]) : (s16)((s8)buf[i] << 8);
				FREE (buf);
			}
			if (ok)
			{
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".wav");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE WAYFORWARD-WAVE:%s (%u ch, %u Hz, %u samples, codec %u) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns, codec);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *wv = nds_strm_make_wav (mix, ns, ch, rate, &wn);
					err = wv ? shade_write (arg, dest, basedir, name, wv, (uint)wn) : ERR_OUT_OF_MEMORY;
					FREE (wv);
				}
			}
			FREE (mix);
		}
		#undef W32
	}
	FREE (raw);
	return err;
}
