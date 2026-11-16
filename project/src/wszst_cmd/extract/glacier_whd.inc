// IO Interactive Glacier sound banks (Mini Ninjas, Wii): "<scene>.WHD" header + "<scene>.WAV" data
// side by side on the disc. Both start {u32, u32 file size, 0x03000000, 0x04000000}. The WHD holds
// 0x24-byte big-endian records {u16 type (1 PCM, 0x76 DSP-ADPCM), u16 flags (bit 15: streamed from
// the Wii_*.str banks, not in the .WAV), u32 rate, u32 format (0x10 = 16-bit PCM, 4 = DSP-ADPCM),
// u32 decoded bytes, u32 data size, u32 channels, u32 data offset in the .WAV, u32 samples (all
// channels),
// ...}; a DSP record is preceded by align8(46 * channels) + 8 bytes holding one 46-byte block per
// channel {32 bytes of coefficients, u16 gain, u16 predictor/scale, u16 hist1, u16 hist2, 6 pad
// bytes}. Stereo data is two consecutive halves. Every stored predictor/scale equals the first
// frame header of its channel (13,242 DSP records, 13,100 mono / 142 stereo, plus 69 PCM records
// over the 59 retail banks). Each sound is written as "<file>.WHD.d/NNNN.wav" with --decode.
#include "lib-dsp.h"

static bool glacier_snd_head_ok (const u8 *h, u64 size)
{
	return rd_be32 (h + 4) == size && rd_be32 (h + 8) == 0x03000000
		&& rd_be32 (h + 12) == 0x04000000;
}

// Returns a malloc'd mono WAV for one DSP channel.
static bool glacier_dsp_channel (
	const u8 *blk, const u8 *data, size_t n, uint rate, uint samples, u8 **wav, size_t *wn)
{
	u8 *d = CALLOC (1, 0x60 + n);
	if (!d)
		return false;
	const uint frames = (uint)(n / 8),
			   ns = samples && samples <= frames * 14 ? samples : frames * 14;
	wr_be32 (d, ns);
	wr_be32 (d + 4, frames * 16);
	wr_be32 (d + 8, rate);
	memcpy (d + 0x1c, blk, 32);
	memcpy (d + 0x3c, blk + 32, 8); // gain, ps, hist1, hist2
	memcpy (d + 0x60, data, n);
	const bool ok = !DecodeDSPToWAV (d, 0x60 + n, wav, wn) && *wn > 44;
	FREE (d);
	return ok;
}

static enumError extract_glacier_whd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".whd") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	char wpath[PATH_MAX];
	const size_t al = strlen (arg);
	if (al < 4 || al >= sizeof (wpath))
		return ERR_NOTHING_TO_DO;
	memcpy (wpath, arg, al + 1);
	memcpy (wpath + al - 3, arg[al - 3] == 'w' ? "wav" : "WAV", 3);
	u8 *hd = 0, *wv = 0;
	size_t hn = 0, wn = 0;
	if (LoadFileAlloc (arg, 0, 0, &hd, &hn, 0, 0, 0, false) || !hd || hn < 0x10
		|| !glacier_snd_head_ok (hd, hn))
	{
		FREE (hd);
		return ERR_NOTHING_TO_DO;
	}
	if (LoadFileAlloc (wpath, 0, 0, &wv, &wn, 0, 0, 0, true) || !wv || wn < 0x10
		|| !glacier_snd_head_ok (wv, wn))
	{
		FREE (hd);
		FREE (wv);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	uint nrec = 0, wrote = 0;
	enumError err = ERR_OK;
	for (size_t p = 0x10; p + 0x24 <= hn && !err; p += 4)
	{
		const uint type = rd_be16 (hd + p), flags = rd_be16 (hd + p + 2),
				   rate = rd_be32 (hd + p + 4), fmt = rd_be32 (hd + p + 8),
				   dec = rd_be32 (hd + p + 12), size = rd_be32 (hd + p + 16),
				   ch = rd_be32 (hd + p + 20), off = rd_be32 (hd + p + 24),
				   ns = rd_be32 (hd + p + 28);
		if (!((type == 1 && fmt == 0x10) || (type == 0x76 && fmt == 4)) || rate < 4000
			|| rate > 48000 || (ch != 1 && ch != 2) || !size || !dec)
			continue;
		const size_t area = ((46 * (size_t)ch + 7) & ~(size_t)7) + 8;
		if (fmt == 4 && p < 0x10 + area)
			continue;
		nrec++;
		if (flags & 0x8000 || (u64)off + size > wn || size % (fmt == 4 ? 8 * ch : 2 * ch))
			continue;
		u8 *out = 0;
		size_t on = 0;
		if (fmt == 0x10)
		{
			on = 44 + (size_t)size;
			out = MALLOC (on);
			if (out)
			{
				memcpy (out, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\1\0", 22);
				wr_le16 (out + 22, ch);
				wr_le32 (out + 24, rate);
				wr_le32 (out + 28, rate * ch * 2);
				wr_le16 (out + 32, ch * 2);
				wr_le16 (out + 34, 16);
				memcpy (out + 36, "data", 4);
				wr_le32 (out + 40, size);
				wr_le32 (out + 4, 36 + size);
				for (size_t k = 0; k + 1 < size; k += 2)
					out[44 + k] = wv[off + k + 1], out[45 + k] = wv[off + k];
			}
		}
		else
		{
			const u8 *blk = hd + p - area;
			const size_t per = size / ch;
			u8 *cw[2] = { 0, 0 };
			size_t cn[2] = { 0, 0 };
			bool ok = true;
			for (uint c = 0; c < ch && ok; c++)
				ok = rd_be16 (blk + 46 * c + 34) == wv[off + c * per]
					&& glacier_dsp_channel (
						blk + 46 * c, wv + off + c * per, per, rate, ns / ch, &cw[c], &cn[c]);
			if (ok && ch == 1)
				out = cw[0], on = cn[0], cw[0] = 0;
			else if (ok)
			{
				const size_t frames = ((cn[0] < cn[1] ? cn[0] : cn[1]) - 44) / 2;
				on = 44 + frames * 4;
				out = MALLOC (on);
				if (out)
				{
					memcpy (out, cw[0], 44);
					wr_le16 (out + 22, 2);
					wr_le32 (out + 28, rd_le32 (cw[0] + 24) * 4);
					wr_le16 (out + 32, 4);
					wr_le32 (out + 4, (uint)(36 + frames * 4));
					wr_le32 (out + 40, (uint)(frames * 4));
					for (size_t k = 0; k < frames; k++)
					{
						memcpy (out + 44 + 4 * k, cw[0] + 44 + 2 * k, 2);
						memcpy (out + 46 + 4 * k, cw[1] + 44 + 2 * k, 2);
					}
				}
			}
			FREE (cw[0]);
			FREE (cw[1]);
		}
		if (out)
		{
			char name[32];
			snprintf (name, sizeof (name), "%04u.wav", nrec - 1);
			if (!wrote && (verbose >= 0 || testmode))
				fprintf (stdlog, "%s%sDECODE GLACIER-WHD:%s -> %s/\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, dest);
			if (!testmode)
				err = shade_write (arg, dest, basedir, name, out, (uint)on);
			wrote++;
			FREE (out);
		}
		p += 0x20; // skip the rest of this record
	}
	FREE (hd);
	FREE (wv);
	if (verbose > 0 && wrote)
		fprintf (stdlog, "  %u of %u sounds written\n", wrote, nrec);
	return err; // a valid header pair is ours even without stored sounds (loader banks)
}
