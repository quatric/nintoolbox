// Piglet's Big Game sound bank (".sdd", "DSBH" + "DSBD", little endian even on GameCube): header size @4, entries from 0x20 {name\0, codec u8 (1 DSP, 2 PCM RIFF,
// 3 PS-ADPCM), bits, channels u8, rate u32, sound offset u32, sound size u32}, the data block "DSBD" at the header size, sounds at +0x20. DSP entries start with a 0x60 byte
// DSP header (nibble count unset: the sample count at 0 is used); PCM entries are stored as RIFF WAVE and written unchanged. Each mono sound is written to
// "<file>.d/<name>.wav" (after vgmstream sdd.c; PS-ADPCM entries are skipped).
#include "lib-dsp.h"

static enumError extract_sdd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".sdd") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t hs = sz > 0x40 ? rd_le32 (raw + 4) : 0;
	if (sz > 0x40 && !memcmp (raw, "DSBH", 4) && hs >= 0x20 && hs + 0x20 <= sz && !memcmp (raw + hs, "DSBD", 4))
	{
		const size_t dstart = hs + 0x20;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		for (size_t eo = 0x20; eo + 0x20 <= hs; eo += 0x20)
		{
			// entries are padded to 0x20 bytes; the name is zero terminated inside the entry
			uint nl = 0;
			while (eo + nl < hs && nl < 0x20 && raw[eo + nl])
				nl++;
			const size_t e = eo + nl + 1;
			if (nl == 0 || nl >= 0x20 || e + 15 > hs)
				continue;
			const uint codec = raw[e], ch = raw[e + 2], rate = rd_le32 (raw + e + 3);
			const size_t so = dstart + rd_le32 (raw + e + 7), ss = rd_le32 (raw + e + 11);
			if (ch != 1 || !ss || so + ss > sz)
				continue;
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%.*s.wav", (int)nl, raw + eo);
			u8 *w = 0;
			size_t wn = 0;
			bool own = false;
			if (codec == 1 && ss >= 0x60)
			{
				const u8 *s = raw + so;
				const uint ns = rd_be32 (s);
				u8 *d = CALLOC (1, ss);
				if (d && ns && ns < 0x8000000)
				{
					memcpy (d, s, ss);
					wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
					wr_be32 (d + 8, rate);
					wr_be16 (d + 0xe, 0);
					if (!DecodeDSPToWAV (d, ss, &w, &wn))
						own = true;
				}
				FREE (d);
			}
			else if (codec == 2 && ss > 0x2c && !memcmp (raw + so, "RIFF", 4))
				w = (u8 *)raw + so, wn = ss;
			if (!w)
				continue;
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE SDD:%s %s (%u Hz) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, name, rate);
			if (testmode)
				err = ERR_OK;
			else if (!shade_write (arg, dest, basedir, name, w, (uint)wn))
				err = ERR_OK;
			if (own)
				FREE (w);
		}
	}
	FREE (raw);
	return err;
}
