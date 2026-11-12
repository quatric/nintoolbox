// Black Box / Shaba GameCube "GCub" audio (".gcub" / bigfile ".wav": Sega Soccer Slam, Shrek Superslam): channels @4, rate @8, data size @0xc, DSP coefs @0x10
// (0x20 per channel), ADPCM from 0x60 (0x88 after a "GCxx" tag at 0x60); stereo is either 0x8000 byte block interleaved or byte-interleaved ("sub-interleave",
// detected from plausible frame headers on the second channel). Decoded to "<name>.wav" (after vgmstream gcub.c).
#include "lib-dsp.h"

static bool gcub_subint (const u8 *raw, size_t sz, size_t off)
{
	int tests = 50;
	for (; tests >= 0 && off + 1 < sz; off += 0x10)
	{
		const u8 h2 = raw[off + 1];
		if (!h2)
			continue;
		if ((h2 & 15) > 12 || (h2 >> 4) > 8)
			return false;
		tests--;
	}
	return tests <= 0;
}

static enumError extract_gcub_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".gcub") && !is_ext (arg, ".wav") && !is_ext (arg, ".lwav")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint ch = sz > 0x90 ? rd_be32 (raw + 4) : 0, rate = sz > 0x90 ? rd_be32 (raw + 8) : 0;
	const size_t dsz = sz > 0x90 ? rd_be32 (raw + 0xc) : 0, start = sz > 0x90 && !memcmp (raw + 0x60, "GCxx", 4) ? 0x88 : 0x60;
	if (sz > 0x90 && !memcmp (raw, "GCub", 4) && ch >= 1 && ch <= 2 && rate >= 1000 && rate <= 192000 && dsz && start + dsz <= sz + 0x10)
	{
		const bool sub = ch > 1 && gcub_subint (raw, sz, start);
		size_t avail = sz - start;
		if (dsz < avail)
			avail = dsz;
		const size_t cb = avail / ch;
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
			memcpy (d + 0x1c, raw + 0x10 + c * 0x20, 32);
			if (ch == 1)
				memcpy (d + 0x60, raw + start, cb);
			else if (sub)
				for (size_t i = 0; i < cb; i++)
					d[0x60 + i] = raw[start + 2 * i + c];
			else
				for (size_t o = 0, k = 0; o < cb && ok; k++)
				{
					const size_t n = 0x8000 < cb - o ? 0x8000 : cb - o, so = start + (k * ch + c) * 0x8000;
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
					fprintf (stdlog, "%s%sDECODE GCUB:%s (%u ch, %u Hz, %u samples%s) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns, sub ? ", sub-interleaved" : "");
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
