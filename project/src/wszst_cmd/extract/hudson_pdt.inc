// Hudson GameCube sound bank (".pdt": Mario Party 4, Cubic Lode Runner, Adventure Island): u16 BE 1, entry count u16 @2, 2 or 4 @4, 0x7d00 @8, 2 or 4 @0xc,
// entry pointer table @0x10, coefficient table @0x14 (0x20 per set); an entry is {flags (1 stereo, 2 loop), rate @4, nibbles @8, loop start @0xc, channel 1 offset
// @0x10, coef set u16 @0x14, channel 2 offset @0x18}. Entries sharing a data offset are one sound; each becomes "<file>.d/<name>_<NNN>.wav" (after vgmstream pdt.c).
#include "lib-dsp.h"

static enumError extract_hudson_pdt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".pdt") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint cnt = sz > 0x20 ? rd_be16 (raw + 2) : 0;
	if (sz > 0x20 && rd_be16 (raw) == 1 && (rd_be32 (raw + 4) == 2 || rd_be32 (raw + 4) == 4) && rd_be32 (raw + 8) == 0x7d00
		&& (rd_be32 (raw + 0xc) == 2 || rd_be32 (raw + 0xc) == 4) && cnt && cnt <= 0x2000)
	{
		const size_t eo = rd_be32 (raw + 0x10), co = rd_be32 (raw + 0x14);
		char dest[PATH_MAX], stem[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (stem, '.');
		if (dot) *dot = 0;
		u32 *seen = CALLOC (cnt, sizeof (u32));
		uint nseen = 0;
		for (uint i = 0; seen && i < cnt && eo + 4 * (size_t)(i + 1) <= sz; i++)
		{
			const size_t he = rd_be32 (raw + eo + 4 * i);
			if (!he || he + 0x20 > sz)
				continue;
			const u32 data = rd_be32 (raw + he + 0x10);
			bool dup = false;
			for (uint j = 0; j < nseen; j++)
				dup |= seen[j] == data;
			if (dup)
				continue;
			seen[nseen++] = data;
			const uint flags = raw[he], rate = rd_be32 (raw + he + 4), nib = rd_be32 (raw + he + 8), ch = flags & 1 ? 2 : 1;
			const size_t cs = co + (size_t)rd_be16 (raw + he + 0x14) * 0x20;
			const u32 off[2] = { data, rd_be32 (raw + he + 0x18) };
			const uint ns = nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0);
			const size_t cb = (size_t)(nib + 1) / 2;
			if (rate < 1000 || rate > 192000 || !ns || ns > 0x8000000 || cs + 32 * ch > sz)
				continue;
			u8 *pcm[2] = { 0, 0 };
			bool ok = true;
			for (uint c = 0; c < ch && ok; c++)
			{
				if ((size_t)off[c] + cb > sz) { ok = false; break; }
				u8 *d = CALLOC (1, 0x60 + cb);
				if (!d) { ok = false; break; }
				wr_be32 (d, ns);
				wr_be32 (d + 4, nib);
				wr_be32 (d + 8, rate);
				memcpy (d + 0x1c, raw + cs + 0x20 * c, 32);
				memcpy (d + 0x60, raw + off[c], cb);
				size_t wn = 0;
				if (DecodeDSPToWAV (d, 0x60 + cb, &pcm[c], &wn) || wn < 44 + (size_t)ns * 2)
					ok = false;
				FREE (d);
			}
			if (ok)
			{
				s16 *mix = MALLOC ((size_t)ns * ch * 2);
				if (mix)
				{
					for (uint k = 0; k < ns; k++)
						for (uint c = 0; c < ch; c++)
							mix[(size_t)k * ch + c] = (s16)(pcm[c][44 + 2 * k] | pcm[c][45 + 2 * k] << 8);
					char name[PATH_MAX];
					snprintf (name, sizeof (name), "%s_%03u.wav", stem, nseen - 1);
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sDECODE HUDSON-PDT:%s #%u (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nseen - 1, ch, rate, ns);
					if (testmode)
						err = ERR_OK;
					else
					{
						size_t wn = 0;
						u8 *w = nds_strm_make_wav (mix, ns, ch, rate, &wn);
						if (w && !shade_write (arg, dest, basedir, name, w, (uint)wn))
							err = ERR_OK;
						FREE (w);
					}
					FREE (mix);
				}
			}
			FREE (pcm[0]);
			FREE (pcm[1]);
		}
		FREE (seen);
	}
	FREE (raw);
	return err;
}
