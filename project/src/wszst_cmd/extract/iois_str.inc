// IO Interactive "IOISNDSTREAM" sound streams (".str": Mini Ninjas; little endian; the big "Wii_Def.str" holds the music/ambience, the per-language ones the dialogue): 0x800-byte
// header {"IOISNDSTREAM", u32 version 9, u64 timestamp, u32 @0x18, u32 stream count @0x1c}, the 0x800-aligned stream data, and a table of count * 72-byte rows at the very end of
// the file, each {u64 index, u64 data offset, u64 data size, u64 record pointer, u32 record size (74 mono / 120 stereo), u32 hash, u64 name length, u64 name pointer, u64 0, u64 0}.
// The record {u32 name length, u32 sample count, u32 channels, u32 rate, u32 format (4 = DSP-ADPCM), 8 stale bytes, then per channel 46 bytes: 32 bytes of big-endian DSP
// coefficients, 8 zero bytes (gain/history) and 6 stale bytes} gives the codec setup, the name is a NUL-terminated path ("Dialog/C01/Dialogue/281_Tengu.wav") in the string area.
// Stereo data is interleaved in 2-byte units (even halfwords left, odd right). Format 4 (DSP-ADPCM) and format 16 (big-endian 16-bit PCM, record 32 bytes) streams are decoded to
// "<name>.str.d/<path>.wav" with --decode.
#include "lib-dsp.h"

static bool iois_str_read (FILE *f, u64 off, void *buf, size_t n)
{
	return !fseeko (f, (off_t)off, SEEK_SET) && fread (buf, 1, n, f) == n;
}

static enumError extract_iois_str_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".str") || (export_count == 0 && !opt_decode) || !file_has_magic (arg, "IOISNDSTREAM", 12))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 hdr[0x20];
	fseeko (f, 0, SEEK_END);
	const u64 fsz = (u64)ftello (f);
	const uint n = iois_str_read (f, 0, hdr, sizeof hdr) ? rd_le32 (hdr + 0x1c) : 0;
	if (n && n < 100000 && fsz > 0x800 + (u64)n * 72 && rd_le32 (hdr + 0xc) == 9)
	{
		u8 *toc = MALLOC ((size_t)n * 72);
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (toc && iois_str_read (f, fsz - (u64)n * 72, toc, (size_t)n * 72))
		{
			uint wrote = 0;
			enumError werr = ERR_OK;
			for (uint i = 0; i < n && !werr; i++)
			{
				const u8 *r = toc + (size_t)i * 72;
				const u64 off = rd_le64 (r + 8), size = rd_le64 (r + 16), rp = rd_le64 (r + 24), np = rd_le64 (r + 48);
				const uint rsz = rd_le32 (r + 32), nl = (uint)rd_le64 (r + 40);
				u8 rec[128], nm[512];
				if (!off || !size || off + size > fsz || rp + rsz > fsz || rsz < 32 || rsz > 128 || nl < 3 || nl > 480 || np + nl > fsz || !iois_str_read (f, rp, rec, rsz)
					|| !iois_str_read (f, np, nm, nl) || nm[nl - 1] == 0xff)
					continue;
				nm[nl] = 0;
				const uint ch = rd_le32 (rec + 8), rate = rd_le32 (rec + 12), fmt = rd_le32 (rec + 16);
				if ((fmt != 4 && fmt != 16) || (ch != 1 && ch != 2) || rsz != (fmt == 16 ? 32u : ch == 1 ? 74u : 120u) || rate < 2000 || rate > 96000 || size > 0x20000000 || size % (fmt == 16 ? 2 * ch : 8 * ch))
					continue;
				u8 *data = MALLOC ((size_t)size);
				if (!data || !iois_str_read (f, off, data, (size_t)size))
				{
					FREE (data);
					continue;
				}
				if (fmt == 16)
				{
					// big-endian 16-bit PCM, channels interleaved per sample
					u8 *out = MALLOC (44 + (size_t)size);
					if (out)
					{
						memcpy (out, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\1\0", 22);
						wr_le16 (out + 22, ch);
						wr_le32 (out + 24, rate);
						wr_le32 (out + 28, rate * ch * 2);
						wr_le16 (out + 32, ch * 2);
						wr_le16 (out + 34, 16);
						memcpy (out + 36, "data", 4);
						wr_le32 (out + 40, (uint)size);
						wr_le32 (out + 4, (uint)(36 + size));
						for (size_t k = 0; k + 1 < (size_t)size; k += 2)
						{
							out[44 + k] = data[k + 1];
							out[45 + k] = data[k];
						}
						char name[512];
						snprintf (name, sizeof (name), "%s", (ccp)nm);
						char *dot = strrchr (name, '.');
						if (!dot || strchr (dot, '/'))
							strcat (name, ".wav");
						else
							strcpy (dot, ".wav");
						if (!wrote && (verbose >= 0 || testmode))
							fprintf (stdlog, "%s%sDECODE IOIS-STR:%s (%u streams) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
						werr = shade_write (arg, dest, basedir, name, out, (uint)(44 + size));
						wrote++;
						FREE (out);
					}
					FREE (data);
					continue;
				}
				const size_t per = (size_t)size / ch;
				u8 *chd[2] = { 0, 0 }, *wav[2] = { 0, 0 };
				size_t wn[2] = { 0, 0 };
				bool ok = true;
				for (uint c = 0; c < ch && ok; c++)
				{
					chd[c] = CALLOC (1, 0x60 + per);
					ok = chd[c] != 0;
					if (!ok)
						break;
					const uint ns = (uint)(per / 8 * 14);
					wr_be32 (chd[c], ns);
					wr_be32 (chd[c] + 4, ns / 14 * 16);
					wr_be32 (chd[c] + 8, rate);
					memcpy (chd[c] + 0x1c, rec + 28 + 46 * c, 32);
					if (ch == 1)
						memcpy (chd[c] + 0x60, data, per);
					else
						for (size_t k = 0; k < per; k += 2)
						{
							chd[c][0x60 + k] = data[2 * k + 2 * c];
							chd[c][0x60 + k + 1] = data[2 * k + 2 * c + 1];
						}
					wr_be16 (chd[c] + 0x3e, chd[c][0x60]);
					ok = !DecodeDSPToWAV (chd[c], 0x60 + per, &wav[c], &wn[c]) && wn[c] > 44;
				}
				if (ok)
				{
					u8 *out = wav[0];
					size_t on = wn[0];
					if (ch == 2)
					{
						const size_t frames = (wn[0] < wn[1] ? wn[0] : wn[1]) - 44 >> 1;
						out = MALLOC (44 + frames * 4);
						if (out)
						{
							memcpy (out, wav[0], 44);
							wr_le16 (out + 22, 2);
							wr_le32 (out + 28, rd_le32 (wav[0] + 24) * 4);
							wr_le16 (out + 32, 4);
							wr_le32 (out + 4, (uint)(36 + frames * 4));
							wr_le32 (out + 40, (uint)(frames * 4));
							for (size_t k = 0; k < frames; k++)
							{
								memcpy (out + 44 + 4 * k, wav[0] + 44 + 2 * k, 2);
								memcpy (out + 46 + 4 * k, wav[1] + 44 + 2 * k, 2);
							}
							on = 44 + frames * 4;
						}
						else
							ok = false;
					}
					if (ok)
					{
						char name[512];
						snprintf (name, sizeof (name), "%s", (ccp)nm);
						char *dot = strrchr (name, '.');
						if (!dot || strchr (dot, '/'))
							strcat (name, ".wav");
						else
							strcpy (dot, ".wav");
						if (!wrote && (verbose >= 0 || testmode))
							fprintf (stdlog, "%s%sDECODE IOIS-STR:%s (%u streams) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
						werr = shade_write (arg, dest, basedir, name, out, (uint)on);
						wrote++;
					}
					if (out != wav[0])
						FREE (out);
				}
				for (uint c = 0; c < 2; c++)
				{
					FREE (chd[c]);
					FREE (wav[c]);
				}
				FREE (data);
			}
			if (wrote)
				err = werr;
		}
		FREE (toc);
	}
	fclose (f);
	return err;
}
