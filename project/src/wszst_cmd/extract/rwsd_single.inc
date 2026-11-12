// Single-stream NW4R wave sound data (".rwsd" / ".brwsd", header "RWSD" 0xFEFF0102; Wii): the WAVE block (offset @0x18) holds one wave {codec @0x10 (0 PCM8, 1 PCM16
// BE, 2 DSP-ADPCM), loop @0x11, channels @0x12, rate u16 @0x14, loop start nibble @0x18, nibble count @0x1c, stream size per channel @0x50, DSP coefficients @0x6c with
// 0x30 per channel}; the audio follows the header at the data offset @8 (after an optional "LABL" block). Decoded to "<name>.wav" (after vgmstream rwsd.c; multi-wave
// RWSD files and RWSDs paired with an RWAR are handled by the sound archive code).
#include "lib-dsp.h"

static enumError extract_rwsd_single_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".rwsd") && !is_ext (arg, ".brwsd")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t w = sz > 0x20 ? rd_be32 (raw + 0x18) : 0;
	if (sz > 0x20 && !memcmp (raw, "RWSD", 4) && rd_be32 (raw + 4) == 0xFEFF0102 && w + 0x80 <= sz && !memcmp (raw + w, "WAVE", 4)
		&& rd_be32 (raw + w + 8) == 1)
	{
		const uint codec = raw[w + 0x10], ch = raw[w + 0x12], rate = rd_be16 (raw + w + 0x14);
		const u32 nib = rd_be32 (raw + w + 0x1c);
		size_t start = rd_be32 (raw + 8);
		const size_t per = rd_be32 (raw + w + 0x50);
		if (start + 8 <= sz && !memcmp (raw + start, "LABL", 4))
			start += rd_be32 (raw + start + 4);
		if (codec <= 2 && ch >= 1 && ch <= 2 && rate >= 1000 && per && start + per <= sz && (codec != 2 || w + 0x6c + 0x30 * (ch - 1) + 32 <= sz))
		{
			const uint ns = codec == 2 ? nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0) : codec == 1 ? (uint)(per / 2) : (uint)per;
			s16 *mix = ns && ns < 0x8000000 ? CALLOC ((size_t)ns * ch, sizeof (s16)) : 0;
			bool ok = mix != 0;
			for (uint c = 0; c < ch && ok; c++)
			{
				const u8 *p = raw + start + per * c;
				const size_t avail = start + per * (c + 1) <= sz ? per : sz - start - per * c;
				if (codec == 2)
				{
					u8 *d = CALLOC (1, 0x60 + avail);
					u8 *wv = 0;
					size_t wn = 0;
					if (!d) { ok = false; break; }
					wr_be32 (d, ns);
					wr_be32 (d + 4, nib);
					wr_be32 (d + 8, rate);
					memcpy (d + 0x1c, raw + w + 0x6c + 0x30 * c, 32);
					memcpy (d + 0x60, p, avail);
					if (DecodeDSPToWAV (d, 0x60 + avail, &wv, &wn) || wn < 44 + (size_t)ns * 2)
						ok = false;
					else
						for (uint i = 0; i < ns; i++)
							mix[(size_t)i * ch + c] = (s16)(wv[44 + 2 * i] | wv[45 + 2 * i] << 8);
					FREE (wv);
					FREE (d);
				}
				else
					for (uint i = 0; i < ns && (codec == 1 ? 2 * i + 1 : i) < avail; i++)
						mix[(size_t)i * ch + c] = codec == 1 ? (s16)(p[2 * i] << 8 | p[2 * i + 1]) : (s16)((s8)p[i] << 8);
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
					fprintf (stdlog, "%s%sDECODE RWSD:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
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
	}
	FREE (raw);
	return err;
}
