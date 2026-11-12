// Tecmo "GSND" sound headers (".gsp" + companion ".gsb": Super Swing Golf 1 and 2 on Wii; big endian): chunk list from the offset @0x10, each {id, full size, ...}:
// "DATA" {data size, codec (4 = DSP-ADPCM), rate @8, channels u16 @0xe, samples @0x14}, "GCEX" {size, block header size @4, block count @8, DSP coefficients @0x18
// with 0x30 per channel}. The ".gsb" holds blocks of {0x20 byte header, 0x8000 bytes per channel}; the last block is split evenly. Decoded to "<name>.wav"
// (after vgmstream gsnd.c / blocked_gsnd.c; the PS3/Xbox 360 codecs are not handled).
#include "lib-dsp.h"

static bool gsnd_chunk (const u8 *raw, size_t sz, size_t first, ccp id, size_t *off)
{
	for (size_t p = first; p + 8 <= sz;)
	{
		const size_t size = rd_be32 (raw + p + 4);
		if (!memcmp (raw + p, id, 4))
		{
			*off = p + 8;
			return true;
		}
		if (size < 8)
			return false;
		p += size;
	}
	return false;
}

static enumError extract_gsnd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gsp") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *gsb = 0;
	size_t sz = 0, gsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	size_t d, g;
	const size_t first = sz > 0x14 ? rd_be32 (raw + 0x10) : 0;
	if (sz > 0x40 && !memcmp (raw, "GSND", 4) && first < sz && gsnd_chunk (raw, sz, first, "DATA", &d) && gsnd_chunk (raw, sz, first, "GCEX", &g)
		&& d + 0x18 <= sz && g + 0x18 <= sz && rd_be32 (raw + d + 4) == 4)
	{
		const size_t dsz = rd_be32 (raw + d), hdr = rd_be32 (raw + g + 4), blocks = rd_be32 (raw + g + 8);
		const uint rate = rd_be32 (raw + d + 8), ch = rd_be16 (raw + d + 0xe);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s", arg);
		strcpy (strrchr (path, '.'), ".gsb");
		if (ch >= 1 && ch <= 2 && rate >= 1000 && rate <= 192000 && hdr < dsz && blocks < 0x100000 && g + 0x18 + 0x30 * ch <= sz
			&& !LoadFileAlloc (path, 0, 0, &gsb, &gsz, 0, 0, 0, false) && gsb)
		{
			const uint ns = (uint)((dsz - hdr * blocks) / 8 / ch * 14);
			const size_t cb = (size_t)(ns + 13) / 14 * 8;
			u8 *dsp[2] = { 0, 0 };
			bool ok = ns && ns < 0x8000000;
			for (uint c = 0; c < ch && ok; c++)
			{
				dsp[c] = CALLOC (1, 0x60 + cb);
				if (!dsp[c]) { ok = false; break; }
				wr_be32 (dsp[c], ns);
				wr_be32 (dsp[c] + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
				wr_be32 (dsp[c] + 8, rate);
				memcpy (dsp[c] + 0x1c, raw + g + 0x18 + 0x30 * c, 32);
			}
			size_t done = 0;
			for (size_t bo = 0; ok && done < cb && bo + 0x20 < gsz; )
			{
				size_t per = 0x8000;
				size_t next = bo + 0x20 + per * ch;
				if (next > gsz)
					per = (gsz - bo - 0x20) / ch;
				const size_t take = per < cb - done ? per : cb - done;
				for (uint c = 0; c < ch; c++)
					memcpy (dsp[c] + 0x60 + done, gsb + bo + 0x20 + per * c, take);
				done += take;
				bo = next;
			}
			u8 *pcm[2] = { 0, 0 };
			for (uint c = 0; c < ch && ok; c++)
			{
				size_t wn = 0;
				if (DecodeDSPToWAV (dsp[c], 0x60 + cb, &pcm[c], &wn) || wn < 44 + (size_t)ns * 2)
					ok = false;
			}
			s16 *mix = ok ? MALLOC ((size_t)ns * ch * 2) : 0;
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
					fprintf (stdlog, "%s%sDECODE GSND:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns);
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
			for (uint c = 0; c < ch; c++)
			{
				FREE (dsp[c]);
				FREE (pcm[c]);
			}
		}
	}
	FREE (gsb);
	FREE (raw);
	return err;
}
