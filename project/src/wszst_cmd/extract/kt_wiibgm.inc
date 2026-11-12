// Koei Tecmo "WiiBGM" DSP music (".dsp", and inside ".g1l" "G1L_0000" containers of type 9: Hyrule Warriors Wii U, Romance of the Three Kingdoms 12): "WiiBGM\0\0"
// or 0x474D0000 @4, samples @0x10, loop start @0x14 (BE), channels u8 @0x23, rate u16 @0x26, DSP coefficients @0x5c (0x60 per channel), ADPCM from 0x800 with the
// channels interleaved byte by byte. Every stream is decoded to "<name>.wav" ("<name>_<NN>.wav" per G1L entry), via synthetic DSP files (after vgmstream g1l.c).
#include "lib-dsp.h"

static bool ktwb_decode (const u8 *raw, size_t sz, size_t off, uint *ch_out, uint *rate_out, uint *ns_out, s16 **mix_out)
{
	if (off + 0x800 >= sz || (memcmp (raw + off, "WiiBGM\0\0", 8) && rd_be32 (raw + off + 4) != 0x474D0000))
		return false;
	const uint ns = rd_be32 (raw + off + 0x10), ch = raw[off + 0x23], rate = rd_be16 (raw + off + 0x26);
	if (!ns || ns > 0x8000000 || ch < 1 || ch > 8 || rate < 1000 || off + 0x5c + 0x60 * (ch - 1) + 32 > sz)
		return false;
	const size_t cb = (size_t)(ns + 13) / 14 * 8, start = off + 0x800;
	if (start + cb * ch > sz + 8 * ch)
		return false;
	u8 *pcm[8] = { 0 };
	bool ok = true;
	for (uint c = 0; c < ch && ok; c++)
	{
		u8 *d = CALLOC (1, 0x60 + cb);
		if (!d) { ok = false; break; }
		wr_be32 (d, ns);
		wr_be32 (d + 4, ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0));
		wr_be32 (d + 8, rate);
		memcpy (d + 0x1c, raw + off + 0x5c + 0x60 * c, 32);
		for (size_t i = 0; i < cb && start + (size_t)ch * i + c < sz; i++)
			d[0x60 + i] = raw[start + (size_t)ch * i + c];
		size_t wn = 0;
		if (DecodeDSPToWAV (d, 0x60 + cb, &pcm[c], &wn) || wn < 44 + (size_t)ns * 2)
			ok = false;
		FREE (d);
	}
	s16 *mix = ok ? MALLOC ((size_t)ns * ch * 2) : 0;
	if (mix)
		for (uint i = 0; i < ns; i++)
			for (uint c = 0; c < ch; c++)
				mix[(size_t)i * ch + c] = (s16)(pcm[c][44 + 2 * i] | pcm[c][45 + 2 * i] << 8);
	for (uint c = 0; c < ch; c++)
		FREE (pcm[c]);
	if (!mix)
		return false;
	*ch_out = ch;
	*rate_out = rate;
	*ns_out = ns;
	*mix_out = mix;
	return true;
}

static enumError extract_kt_wiibgm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool g1l = is_ext (arg, ".g1l");
	if ((!g1l && !is_ext (arg, ".dsp")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	size_t offs[0x400];
	uint n = 0;
	bool multi = false;
	if (g1l)
	{
		const bool be = sz > 0x20 && !memcmp (raw, "G1L_", 4), le = sz > 0x20 && !memcmp (raw, "_L1G", 4);
		if ((be || le) && !memcmp (raw + 4, "0000", 4) && sz > 0x1c)
		{
			#define G32(p) (be ? (u32)rd_be32 (p) : (u32)rd_le32 (p))
			const uint type = G32 (raw + 0x10), cnt = G32 (raw + 0x14);
			if (type == 9 && cnt >= 1 && cnt <= 0x400 && 0x18 + 4 * (size_t)cnt <= sz)
			{
				multi = cnt > 1;
				for (uint i = 0; i < cnt; i++)
				{
					bool dup = false;
					const size_t o = G32 (raw + 0x18 + 4 * i);
					for (uint j = 0; j < n; j++)
						dup |= offs[j] == o;
					if (!dup)
						offs[n++] = o;
				}
			}
			#undef G32
		}
	}
	else
		offs[n++] = 0;
	char dest[PATH_MAX], stem[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const char *sl = strrchr (arg, '/');
	snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (stem, '.');
	if (dot) *dot = 0;
	for (uint i = 0; i < n; i++)
	{
		uint ch, rate, ns;
		s16 *mix;
		if (!ktwb_decode (raw, sz, offs[i], &ch, &rate, &ns, &mix))
			continue;
		char name[PATH_MAX];
		if (multi)
			snprintf (name, sizeof (name), "%s_%02u.wav", stem, i);
		else
			snprintf (name, sizeof (name), "%s.wav", stem);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE KT-WIIBGM:%s #%u (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, i, ch, rate, ns);
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
	FREE (raw);
	return err;
}
