// GameCube "ISH"+"ISD" pairs (Pokemon XD: Gale of Darkness, Chaos Field): the header "<name>.ish" ("I_SF", rate @8, samples @0xc, channels @0x14, interleave @0x18,
// DSP coefficients @0x40 with 0x40 per channel) describes the raw interleaved DSP-ADPCM in the companion "<name>.isd"; decoded to "<name>.wav"
// (after vgmstream ish_isd.c). Only the ISH needs to be given; the ISD is found beside it.
#include "lib-dsp.h"

static enumError extract_ish_isd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ish") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *isd = 0;
	size_t sz = 0, isz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint rate = sz > 0x100 ? rd_be32 (raw + 8) : 0, ns = sz > 0x100 ? rd_be32 (raw + 0xc) : 0, ch = sz > 0x100 ? rd_be32 (raw + 0x14) : 0, il = sz > 0x100 ? rd_be32 (raw + 0x18) : 0;
	if (sz > 0x100 && !memcmp (raw, "I_SF", 4) && rate >= 1000 && rate <= 192000 && ns && ns < 0x8000000 && ch >= 1 && ch <= 2 && il >= 8 && il < 0x1000000
		&& 0x40 + 0x40 * ch <= sz)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s", arg);
		char *dot = strrchr (path, '.');
		strcpy (dot, ".isd");
		if (access (path, R_OK))
			strcpy (dot, ".ISD");
		if (!LoadFileAlloc (path, 0, 0, &isd, &isz, 0, 0, 0, false) && isd)
		{
			const size_t cb = (size_t)(ns + 13) / 14 * 8;
			u8 *pcm[2] = { 0, 0 };
			bool ok = true;
			for (uint c = 0; c < ch && ok; c++)
			{
				u8 *d = CALLOC (1, 0x60 + cb);
				if (!d) { ok = false; break; }
				wr_be32 (d, ns);
				wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
				wr_be32 (d + 8, rate);
				memcpy (d + 0x1c, raw + 0x40 + 0x40 * c, 32);
				if (ch == 1)
				{
					if (cb > isz) ok = false;
					else memcpy (d + 0x60, isd, cb);
				}
				else
					for (size_t o = 0, k = 0; o < cb && ok; k++)
					{
						const size_t n = il < cb - o ? il : cb - o, so = (k * 2 + c) * il;
						if (so + n > isz) { ok = false; break; }
						memcpy (d + 0x60 + o, isd + so, n);
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
					char *nd = strrchr (name, '.');
					if (nd) strcpy (nd, ".wav");
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sDECODE ISH-ISD:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
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
	FREE (isd);
	FREE (raw);
	return err;
}
