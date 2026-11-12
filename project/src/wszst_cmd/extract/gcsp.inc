// THQ Australia (Studio Oz) GameCube sound bank (".sounds": SpongeBob Lights, Camera, Pants!, Jimmy Neutron Twonkies): "gcsp", data start @0xc, file count @0x14,
// name count @0x18, 0x44 byte file entries from 0x20 (nibbles @0xc, DSP coefs @0x10, history @0x34, next file's start offset @0x40; file 0 starts at the data
// start), then 0x1c byte name entries (file id @0). With names, each name is a stream and an unnamed following file is its right channel; 22050 Hz fixed.
// Every stream becomes "<file>.d/<name>_<NN>.wav" through synthetic DSP files and DecodeDSPToWAV (after vgmstream gcsp.c).
#include "lib-dsp.h"

static enumError extract_gcsp_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".sounds") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x20 && !memcmp (raw, "gcsp", 4))
	{
		const u32 dstart = rd_be32 (raw + 0xc), nfiles = rd_be32 (raw + 0x14), nnames = rd_be32 (raw + 0x18);
		const size_t ntab = 0x20 + (size_t)nfiles * 0x44;
		if (nfiles && nfiles < 0x10000 && nnames < 0x10000 && ntab + (size_t)nnames * 0x1c <= sz && dstart < sz)
		{
			char dest[PATH_MAX], stem[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (stem, '.');
			if (dot) *dot = 0;
			const uint total = nnames ? nnames : nfiles;
			for (uint s = 0; s < total; s++)
			{
				uint id[2] = { s, 0 }, ch = 1;
				if (nnames)
				{
					id[0] = rd_be32 (raw + ntab + (size_t)s * 0x1c);
					const uint nx = id[0] + 1;
					bool named = false;
					for (uint i = 0; i < nnames; i++)
						named |= rd_be32 (raw + ntab + (size_t)i * 0x1c) == nx;
					if (nx < nfiles && !named)
					{
						ch = 2;
						id[1] = nx;
					}
				}
				if (id[0] >= nfiles)
					continue;
				const u8 *el = raw + 0x20 + (size_t)id[0] * 0x44;
				const u32 nib = rd_be32 (el + 0xc), ns = nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0), bytes = (nib + 1) / 2;
				u8 *pcm[2] = { 0, 0 };
				bool ok = ns && ns < 0x4000000;
				for (uint c = 0; c < ch && ok; c++)
				{
					const u8 *ec = raw + 0x20 + (size_t)id[c] * 0x44;
					const size_t so = id[c] ? rd_be32 (raw + 0x20 + (size_t)(id[c] - 1) * 0x44 + 0x40) : dstart;
					if (so + bytes > sz) { ok = false; break; }
					u8 *d = CALLOC (1, 0x60 + bytes);
					if (!d) { ok = false; break; }
					wr_be32 (d, ns);
					wr_be32 (d + 4, nib);
					wr_be32 (d + 8, 22050);
					memcpy (d + 0x1c, ec + 0x10, 32);
					memcpy (d + 0x40, ec + 0x34, 4);
					memcpy (d + 0x60, raw + so, bytes);
					size_t wn = 0;
					if (DecodeDSPToWAV (d, 0x60 + bytes, &pcm[c], &wn) || wn < 44 + (size_t)ns * 2)
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
						char name[PATH_MAX];
						snprintf (name, sizeof (name), "%s_%02u.wav", stem, s);
						if (verbose >= 0 || testmode)
							fprintf (stdlog, "%s%sDECODE GCSP:%s #%u (%u ch, 22050 Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, s, ch, ns);
						if (testmode)
							err = ERR_OK;
						else
						{
							size_t wn = 0;
							u8 *wv = nds_strm_make_wav (mix, ns, ch, 22050, &wn);
							if (wv && !shade_write (arg, dest, basedir, name, wv, (uint)wn))
								err = ERR_OK;
							FREE (wv);
						}
						FREE (mix);
					}
				}
				FREE (pcm[0]);
				FREE (pcm[1]);
			}
		}
	}
	FREE (raw);
	return err;
}
