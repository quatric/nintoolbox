static void pcm_put32 (u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
// RIFF WAVE files with compressed codecs, decoded to 16 bit PCM "<file>.d/<name>_pcm.wav": MS ADPCM (format 0x0002), IMA / DVI ADPCM (0x0011, the Microsoft block layout with
// a 4 byte header per channel), A-law (0x0006) and mu-law (0x0007). Plain PCM files are left alone.
static const int wad_ima_step[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
	279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
	6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int wad_ima_idx[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static int wad_clamp (int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

static enumError extract_wavadpcm_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wav") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 44 && !memcmp (raw, "RIFF", 4) && !memcmp (raw + 8, "WAVE", 4))
	{
		uint tag = 0, ch = 0, rate = 0, align = 0, bits = 0, spb = 0;
		const u8 *fmt = 0, *data = 0;
		size_t dn = 0;
		for (size_t p = 12; p + 8 <= sz;)
		{
			const size_t cs = raw[p + 4] | raw[p + 5] << 8 | raw[p + 6] << 16 | (size_t)raw[p + 7] << 24;
			if (!memcmp (raw + p, "fmt ", 4) && cs >= 16 && p + 8 + cs <= sz)
			{
				fmt = raw + p + 8;
				tag = fmt[0] | fmt[1] << 8; ch = fmt[2] | fmt[3] << 8;
				rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16 | (uint)fmt[7] << 24;
				align = fmt[12] | fmt[13] << 8; bits = fmt[14] | fmt[15] << 8;
				if (cs >= 20) spb = fmt[18] | fmt[19] << 8;
			}
			else if (!memcmp (raw + p, "data", 4))
			{
				data = raw + p + 8;
				dn = p + 8 + cs <= sz ? cs : sz - p - 8;
			}
			p += 8 + cs + (cs & 1);
		}
		if (fmt && data && ch && ch <= 2 && rate && (tag == 2 || tag == 6 || tag == 7 || tag == 0x11))
		{
			size_t cap = tag == 6 || tag == 7 ? dn * 2 : (dn / (align ? align : 1) + 1) * (size_t)(spb ? spb : 2048) * ch * 2;
			if (tag == 2 || tag == 0x11)
				cap += 65536;
			u8 *out = MALLOC (cap ? cap : 1);
			size_t o = 0;
			bool ok = out != 0 && (tag == 6 || tag == 7 || (align >= 4 * ch && align <= 65536));
			if (ok && (tag == 6 || tag == 7))
			{
				for (size_t i = 0; i < dn; i++)
				{
					int v;
					if (tag == 7)
					{
						const int u = ~data[i] & 255, t = ((u & 15) << 3) + 132;
						v = (t << ((u & 0x70) >> 4)) - 132;
						if (u & 0x80) v = -v;
					}
					else
					{
						int a = data[i] ^ 0x55, seg = (a & 0x70) >> 4;
						v = (a & 15) << 4;
						v = seg == 0 ? v + 8 : (v + 0x108) << (seg - 1);
						if (!(a & 0x80)) v = -v;
					}
					out[o++] = v & 255; out[o++] = (v >> 8) & 255;
				}
				bits = 16;
			}
			else if (ok && tag == 0x11)
			{
				for (size_t b = 0; b + align <= dn; b += align)
				{
					const u8 *blk = data + b;
					int pred[2], idx[2];
					size_t frames_hdr = 1, base = o;
					for (uint c = 0; c < ch; c++)
					{
						pred[c] = (int16_t)(blk[c * 4] | blk[c * 4 + 1] << 8);
						idx[c] = blk[c * 4 + 2] > 88 ? 88 : blk[c * 4 + 2];
						if (o + 2 > cap) { ok = false; break; }
						out[o + c * 2] = pred[c] & 255; out[o + c * 2 + 1] = (pred[c] >> 8) & 255;
					}
					o += ch * 2;
					const size_t groups = (align - 4 * ch) / (4 * ch);
					const size_t frames = 1 + groups * 8;
					if (base + frames * ch * 2 > cap) { ok = false; break; }
					(void)frames_hdr;
					for (size_t g = 0; g < groups; g++)
						for (uint c = 0; c < ch; c++)
							for (uint k = 0; k < 8; k++)
							{
								const u8 byte = blk[4 * ch + g * 4 * ch + c * 4 + k / 2];
								const uint nib = k & 1 ? byte >> 4 : byte & 15;
								const int step = wad_ima_step[idx[c]];
								int diff = step >> 3;
								if (nib & 1) diff += step >> 2;
								if (nib & 2) diff += step >> 1;
								if (nib & 4) diff += step;
								if (nib & 8) diff = -diff;
								pred[c] = wad_clamp (pred[c] + diff);
								idx[c] += wad_ima_idx[nib];
								idx[c] = idx[c] < 0 ? 0 : idx[c] > 88 ? 88 : idx[c];
								const size_t at = base + (1 + g * 8 + k) * ch * 2 + c * 2;
								out[at] = pred[c] & 255; out[at + 1] = (pred[c] >> 8) & 255;
							}
					o = base + frames * ch * 2;
				}
				bits = 16;
			}
			else if (ok && tag == 2)
			{
				static const int ac1[7] = {256, 512, 0, 192, 240, 460, 392}, ac2[7] = {0, -256, 0, 64, 0, -208, -232};
				static const int adapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
				for (size_t b = 0; ok && b + align <= dn; b += align)
				{
					const u8 *blk = data + b;
					int c1[2], c2[2], delta[2], s1[2], s2[2];
					size_t p = 0;
					uint pred_i[2];
					for (uint c = 0; c < ch; c++) { pred_i[c] = blk[p++]; if (pred_i[c] > 6) ok = false; }
					for (uint c = 0; c < ch; c++) { delta[c] = (int16_t)(blk[p] | blk[p + 1] << 8); p += 2; }
					for (uint c = 0; c < ch; c++) { s1[c] = (int16_t)(blk[p] | blk[p + 1] << 8); p += 2; }
					for (uint c = 0; c < ch; c++) { s2[c] = (int16_t)(blk[p] | blk[p + 1] << 8); p += 2; }
					if (!ok) break;
					for (uint c = 0; c < ch; c++) { c1[c] = ac1[pred_i[c]]; c2[c] = ac2[pred_i[c]]; }
					const size_t nsamp = (align - p) * 2 / ch + 2;
					if (o + nsamp * ch * 2 > cap) { ok = false; break; }
					for (uint c = 0; c < ch; c++) { out[o + c * 2] = s2[c] & 255; out[o + c * 2 + 1] = (s2[c] >> 8) & 255; }
					o += ch * 2;
					for (uint c = 0; c < ch; c++) { out[o + c * 2] = s1[c] & 255; out[o + c * 2 + 1] = (s1[c] >> 8) & 255; }
					o += ch * 2;
					for (size_t i = 0; p + i / 2 < align && i < (align - p) * 2; i++)
					{
						const uint c = i % ch;
						const u8 byte = blk[p + i / 2];
						int nib = i & 1 ? byte & 15 : byte >> 4;
						const int sn = nib >= 8 ? nib - 16 : nib;
						int pr = (s1[c] * c1[c] + s2[c] * c2[c]) / 256 + sn * delta[c];
						pr = wad_clamp (pr);
						s2[c] = s1[c]; s1[c] = pr;
						delta[c] = adapt[nib] * delta[c] / 256;
						if (delta[c] < 16) delta[c] = 16;
						out[o++] = pr & 255; out[o++] = (pr >> 8) & 255;
					}
				}
				bits = 16;
			}
			if (ok && o)
			{
				char stem[PATH_MAX], name[PATH_MAX];
				const char *sl = strrchr (arg, '/');
				snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (stem, '.');
				if (dot) *dot = 0;
				snprintf (name, sizeof (name), "%s_pcm.wav", stem);
				u8 *wav = MALLOC (44 + o);
				if (wav)
				{
					memcpy (wav, "RIFF", 4); pcm_put32 (wav + 4, 36 + o);
					memcpy (wav + 8, "WAVEfmt ", 8); pcm_put32 (wav + 16, 16);
					wav[20] = 1; wav[21] = 0; wav[22] = ch; wav[23] = 0;
					pcm_put32 (wav + 24, rate); pcm_put32 (wav + 28, rate * ch * 2);
					wav[32] = ch * 2; wav[33] = 0; wav[34] = 16; wav[35] = 0;
					memcpy (wav + 36, "data", 4); pcm_put32 (wav + 40, o);
					memcpy (wav + 44, out, o);
					char dest[PATH_MAX];
					beside_source_dest (dest, sizeof (dest), arg);
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sEXTRACT WAV-ADPCM:%s -> %s/%s%s (%u ch, %u Hz, codec 0x%04x)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", name, ch, rate, tag);
					err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, wav, (uint)(44 + o));
					FREE (wav);
				}
			}
			FREE (out);
		}
	}
	FREE (raw);
	return err;
}
