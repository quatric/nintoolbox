// Cauldron GameCube streams (".str": Conan): u32 BE id 0xFAAF0001, rate @4, samples @8, interleave @0xc, DSP coefs @0x10 (0x20 per channel), stereo
// block-interleaved DSP-ADPCM from 0x60; decoded to "<name>.wav" via synthetic DSP files (after vgmstream ngc_str_cauldron.c).
#include "lib-dsp.h"

static enumError extract_conan_str_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".str") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint rate = sz > 0x60 ? rd_be32 (raw + 4) : 0, ch = 2, il = sz > 0x60 ? rd_be32 (raw + 0xc) : 0;
	const size_t total = sz > 0x60 ? (size_t)(rd_be32 (raw + 8) + 13) / 14 * 8 * 2 : 0;
	if (sz > 0x60 && rd_be32 (raw) == 0xFAAF0001 && rate >= 1000 && rate <= 192000 && total && il >= 8 && il < 0x1000000)
	{
		const size_t chbytes = total / ch, avail = sz - 0x60;
		const size_t cb = chbytes < avail / ch ? chbytes : avail / ch;
		uint ns = (uint)(cb / 8 * 14); if (ns > rd_be32 (raw + 8)) ns = rd_be32 (raw + 8);
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
			for (size_t o = 0, pos = 0; o < cb; )
			{
				const size_t n = il < cb - o ? il : cb - o;
				const size_t so = 0x60 + (pos * ch + c) * il;
				if (so + n > sz) { ok = false; break; }
				memcpy (d + 0x60 + o, raw + so, n);
				o += n;
				pos++;
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
					fprintf (stdlog, "%s%sDECODE CONAN-STR:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *wv = nds_strm_make_wav (mix, ns, ch, rate, &wn);
					err = wv ? shade_write (arg, dest, basedir, name, wv, (uint)wn) : ERR_OUT_OF_MEMORY;
					FREE (wv);
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
