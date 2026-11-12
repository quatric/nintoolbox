// Hal Laboratory GameCube sound bank (".ssm": Smash Bros. Melee, Kururin Squash!, Gashbell): u32 BE head size, data size, subsong count, id; then per subsong
// {u32 channels, u32 rate, then per channel a 0x40 record: loop flag s16 @0x00 (first channel only is used), loop start nibble @0x04, end nibble @0x08, first
// nibble @0x0c, 16 DSP coefs @0x10, hist1/hist2 @0x34/0x36}. Offsets are absolute DSP nibbles into the data block (file end - data size); channel data are laid
// out one after the other (the second channel's first nibble is the interleave). Every channel is rebuilt as a plain mono DSP file and decoded with
// DecodeDSPToWAV; the subsongs are written as "<name>_<NN>.wav" into "<file>.d" (after vgmstream ngc_ssm.c).
#include "lib-dsp.h"

static enumError extract_hal_ssm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ssm") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const u32 head = sz > 0x10 ? rd_be32 (raw) : 0, dsz = sz > 0x10 ? rd_be32 (raw + 4) : 0, nsub = sz > 0x10 ? rd_be32 (raw + 8) : 0;
	if (sz > 0x10 && nsub && nsub <= 0x1000 && rd_be32 (raw + 12) <= 0x1000 && head >= nsub * 0x48 && (u64)0x10 + head + dsz <= sz)
	{
		const size_t data = sz - dsz;
		size_t off = 0x10;
		uint written = 0;
		char dest[PATH_MAX], stem[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (stem, '.');
		if (dot) *dot = 0;
		for (uint s = 0; s < nsub && off + 8 <= 0x10 + head; s++)
		{
			const uint ch = rd_be32 (raw + off), rate = rd_be32 (raw + off + 4);
			if (ch < 1 || ch > 2 || off + 8 + ch * 0x40 > 0x10 + head)
				break;
			const u8 *r0 = raw + off + 8;
			const u32 first = rd_be32 (r0 + 0x0c), end = rd_be32 (r0 + 8);
			off += 8 + ch * 0x40;
			if (end <= first || !rate)
				continue;
			const u32 nib = end - first, ns = nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0);
			u8 *pcm[2] = { 0, 0 };
			uint ok = 1, frames = ns;
			for (uint c = 0; c < ch && ok; c++)
			{
				const u8 *rc = r0 + c * 0x40;
				const u32 cfirst = rd_be32 (rc + 0x0c), bytes = (nib + 1) / 2;
				const size_t so = data + (size_t)cfirst / 16 * 8;
				if (so + bytes > sz) { ok = 0; break; }
				u8 *d = CALLOC (1, 0x60 + bytes);
				if (!d) { ok = 0; break; }
				wr_be32 (d, ns);
				wr_be32 (d + 4, nib);
				wr_be32 (d + 8, rate);
				memcpy (d + 0x1c, rc + 0x10, 32);
				memcpy (d + 0x40, rc + 0x34, 4);
				memcpy (d + 0x60, raw + so, bytes);
				u8 *w = 0;
				size_t wn = 0;
				if (DecodeDSPToWAV (d, 0x60 + bytes, &w, &wn) || wn < 44 + (size_t)ns * 2)
					ok = 0;
				else
					pcm[c] = w;
				FREE (d);
			}
			if (ok && ns)
			{
				s16 *mix = MALLOC ((size_t)frames * ch * 2);
				if (mix)
				{
					for (uint i = 0; i < frames; i++)
						for (uint c = 0; c < ch; c++)
							mix[(size_t)i * ch + c] = (s16)(pcm[c][44 + 2 * i] | pcm[c][45 + 2 * i] << 8);
					char name[PATH_MAX];
					snprintf (name, sizeof (name), "%s_%02u.wav", stem, s);
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sDECODE HAL-SSM:%s #%u (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, s, ch, rate, frames);
					if (testmode)
						err = ERR_OK;
					else
					{
						size_t wn = 0;
						u8 *wv = nds_strm_make_wav (mix, frames, ch, rate, &wn);
						enumError e = wv ? shade_write (arg, dest, basedir, name, wv, (uint)wn) : ERR_OUT_OF_MEMORY;
						FREE (wv);
						if (!e) { err = ERR_OK; written++; }
					}
					FREE (mix);
				}
			}
			FREE (pcm[0]);
			FREE (pcm[1]);
		}
		(void)written;
	}
	FREE (raw);
	return err;
}
