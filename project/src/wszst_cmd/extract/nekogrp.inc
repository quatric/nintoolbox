// Neko Entertainment "MUSIC.GRP" (Cocoto Kart Racer 2 / Magic Circus, Wii): little-endian, u16 entry count N, u16 0x20, then N pairs {u32 size, u32 offset}.
// The first N/2 entries are 0x20 byte descriptors (big-endian words {1, 2, 48000, ...}), the last N/2 are raw big-endian 16 bit mono PCM tracks
// that come in left/right pairs of equal size. Every pair is written as "<file>.d/music_NN.wav" (stereo, 48 kHz from the first descriptor).
static void nekogrp_put32 (u8 *p, u32 v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static enumError extract_nekogrp_file (ccp arg, ccp basedir, uint depth)
{
	if ((!is_ext (arg, ".grp") && !is_ext (arg, ".GRP")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 head[0x400];
	fseeko (f, 0, SEEK_END);
	const off_t fsize = ftello (f);
	fseeko (f, 0, SEEK_SET);
	if (fsize > 0x400 && fread (head, 1, sizeof (head), f) == sizeof (head))
	{
		const uint n = head[0] | head[1] << 8;
		bool ok = n >= 2 && !(n & 1) && n <= 120 && (head[2] | head[3] << 8) == 0x20 && 4 + 8 * n <= sizeof (head);
		u32 size[120], offs[120];
		for (uint i = 0; ok && i < n; i++)
		{
			size[i] = kyl_le32 (head + 4 + 8 * i);
			offs[i] = kyl_le32 (head + 8 + 8 * i);
			ok = (off_t)offs[i] + size[i] <= fsize;
		}
		for (uint i = 0; ok && i < n / 2; i++)
			ok = size[i] == 0x20;
		for (uint i = n / 2; ok && i < n; i += 2)
			ok = i + 1 < n && size[i] == size[i + 1] && size[i] > 1000 && size[i] < 0x40000000;
		if (ok)
		{
			uint rate = (u64)offs[0] + 12 <= sizeof (head) ? rd_be32 (head + offs[0] + 8) : 0;
			if (rate < 8000 || rate > 96000)
				rate = 48000;
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			err = ERR_OK;
			for (uint t = 0; t < (n / 2) / 2 * 1 && !err; t++)
			{
				const uint l = n / 2 + 2 * t, r = l + 1;
				const u32 sz = size[l] & ~1u;
				u8 *wav = MALLOC (44 + (size_t)sz * 2);
				u8 *a = MALLOC (sz), *b = MALLOC (sz);
				if (!wav || !a || !b)
				{
					FREE (wav); FREE (a); FREE (b);
					err = ERR_OUT_OF_MEMORY;
					break;
				}
				fseeko (f, offs[l], SEEK_SET);
				const bool rl = fread (a, 1, sz, f) == sz;
				fseeko (f, offs[r], SEEK_SET);
				const bool rr = fread (b, 1, sz, f) == sz;
				if (rl && rr)
				{
					memcpy (wav, "RIFF", 4); nekogrp_put32 (wav + 4, 36 + sz * 2);
					memcpy (wav + 8, "WAVEfmt ", 8); nekogrp_put32 (wav + 16, 16);
					wav[20] = 1; wav[21] = 0; wav[22] = 2; wav[23] = 0;
					nekogrp_put32 (wav + 24, rate); nekogrp_put32 (wav + 28, rate * 4);
					wav[32] = 4; wav[33] = 0; wav[34] = 16; wav[35] = 0;
					memcpy (wav + 36, "data", 4); nekogrp_put32 (wav + 40, sz * 2);
					for (u32 i = 0; i < sz; i += 2)
					{
						wav[44 + i * 2] = a[i + 1]; // the tracks are big-endian samples
						wav[44 + i * 2 + 1] = a[i];
						wav[44 + i * 2 + 2] = b[i + 1];
						wav[44 + i * 2 + 3] = b[i];
					}
					char fn[64];
					snprintf (fn, sizeof (fn), "music_%02u.wav", t);
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sEXTRACT NEKO-GRP:%s track %u -> %s/%s%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, t, dest, basedir ? basedir : "", fn);
					if (!testmode)
						err = shade_write (arg, dest, basedir, fn, wav, 44 + sz * 2);
				}
				FREE (wav); FREE (a); FREE (b);
			}
		}
	}
	fclose (f);
	return err;
}
