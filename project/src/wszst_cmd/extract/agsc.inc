// Retro Studios audio group (".agsc": Metroid Prime / Prime 2, GameCube): "Audi" or u32 1, a name string, then size-prefixed chunks; the header chunk holds
// 0x20 byte entries {id, offset @4 into the data chunk, rate u16 @0xe, samples @0x10, loop start @0x14, loop length @0x18, coef offset @0x1c} followed by
// 0xFFFFFFFF and 0x28 byte coefficient records (coefs at +8 of the record start). Every mono DSP-ADPCM subsong is rebuilt as a plain DSP file and decoded
// with DecodeDSPToWAV to "<file>.d/<name>_<NN>.wav" (after vgmstream agsc.c).
#include "lib-dsp.h"

static uint agsc_str (const u8 *r, size_t sz, size_t off)
{
	uint n = 0;
	while (off + n < sz && n < 0x20 && r[off + n] >= 0x20 && r[off + n] < 0x7f)
		n++;
	return off + n < sz && !r[off + n] ? n : 0;
}

static enumError extract_agsc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".agsc") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	int ver = sz < 0x40 ? 0 : !memcmp (raw, "Audi", 4) ? 1 : rd_be32 (raw) == 1 ? 2 : 0;
	size_t off = 0, head = 0, data = 0, hsz = 0, dsz = 0;
	uint n;
	if (ver)
	{
		off = ver == 1 ? agsc_str (raw, sz, 0) + 1 : 4;
		n = agsc_str (raw, sz, off);
		if (n && ver == 1 ? agsc_str (raw, sz, 0) : n)
		{
			off += n + 1;
			if (ver == 1)
			{
				const size_t u1 = rd_be32 (raw + off);
				off += 4 + u1;
				if (off + 4 > sz) ver = 0;
				else
				{
					const size_t u2 = rd_be32 (raw + off);
					off += 4 + u2;
					if (off + 4 > sz) ver = 0;
					else
					{
						dsz = rd_be32 (raw + off);
						data = off + 4;
						off += 4 + dsz;
						if (off + 4 > sz) ver = 0;
						else { hsz = rd_be32 (raw + off); head = off + 4; }
					}
				}
			}
			else if (off + 18 <= sz)
			{
				off += 2;
				const size_t u1 = rd_be32 (raw + off), u2 = rd_be32 (raw + off + 4);
				hsz = rd_be32 (raw + off + 8);
				dsz = rd_be32 (raw + off + 12);
				head = off + 16 + u1 + u2;
				data = head + hsz;
			}
			else
				ver = 0;
		}
		else
			ver = 0;
	}
	if (ver && dsz && hsz >= 0x48 && head + hsz <= sz && data + dsz <= sz)
	{
		char dest[PATH_MAX], stem[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (stem, '.');
		if (dot) *dot = 0;
		const uint nsub = (hsz - 4) / 0x48;
		for (uint s = 0; s < nsub && s < 0x1000; s++)
		{
			const u8 *e = raw + head + 0x20 * s;
			const size_t so = data + rd_be32 (e + 4);
			const uint rate = e[0xe] << 8 | e[0xf], ns = rd_be32 (e + 0x10), co = head + 8 + rd_be32 (e + 0x1c);
			const size_t bytes = (size_t)(ns / 14) * 8 + (ns % 14 ? 1 + (ns % 14 + 1) / 2 : 0);
			if (!rate || !ns || ns > 0x4000000 || so + bytes > sz || co + 32 > sz)
				continue;
			u8 *d = CALLOC (1, 0x60 + bytes);
			if (!d)
				break;
			wr_be32 (d, ns);
			wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
			wr_be32 (d + 8, rate);
			memcpy (d + 0x1c, raw + co, 32);
			memcpy (d + 0x60, raw + so, bytes);
			u8 *w = 0;
			size_t wn = 0;
			if (!DecodeDSPToWAV (d, 0x60 + bytes, &w, &wn))
			{
				char name[PATH_MAX];
				snprintf (name, sizeof (name), "%s_%02u.wav", stem, s);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE AGSC:%s #%u (1 ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, s, rate, ns);
				if (testmode)
					err = ERR_OK;
				else if (!shade_write (arg, dest, basedir, name, w, (uint)wn))
					err = ERR_OK;
			}
			FREE (w);
			FREE (d);
		}
	}
	FREE (raw);
	return err;
}
