// "CS12" sound container (Emergency Heroes, Wii; Sounds/CHRSOUND.DAT): "CS12", a few little-endian words (page table) and then
// 0x800 aligned "CHNK" blocks {"CHNK", u32 size, u32 count, u32 3, "SGSB", ...}. Sound blocks carry "MDWI" at +0x20: u32 at +0x44
// = DSP-ADPCM bytes, u16 at +0x48 = sample rate (16000), 16 big-endian s16 coefficients at +0x80 and the mono DSP-ADPCM
// frames at +0xf0. Each sound is written as "sound_NNNN.dsp" (decoded to WAV by the DSP extractor). The SOUND.SP bank
// (starts with "CHNK") has the same MDWI blocks with an extra "SS12" header of 0x20 bytes before the rate / coefficient
// fields (all offsets above shift by 0x20); its SSIC / SGSC blocks and SOUND.GSD ("BK32") are not decoded.
static enumError extract_cs12_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x2000 || sz > 0x40000000 || (memcmp (raw, "CS12", 4) && (memcmp (raw, "CHNK", 4) || !is_ext (arg, ".sp"))))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	uint n = 0;
	enumError err = ERR_OK;
	for (size_t m = 0x20; !err && m + 0xe0 <= sz; m++)
	{
		if (memcmp (raw + m, "MDWI", 4) || memcmp (raw + m - 0x20, "CHNK", 4) || memcmp (raw + m - 0x10, "SGSB", 4))
			continue;
		const size_t c = m - 0x20, sh = memcmp (raw + m + 0x10, "SS12", 4) ? 0 : 0x20;
		if (c + 0xf0 + sh > sz)
			continue;
		const u32 bytes = rd_le32 (raw + c + 0x44 + sh), rate = rd_le16 (raw + c + 0x48 + sh);
		if (!bytes || (bytes & 7) || rate < 4000 || rate > 96000 || c + 0xf0 + sh + (size_t)bytes > sz
			|| (raw[c + 0xf0 + sh] >> 4) > 7)
			continue;
		const u32 frames = bytes / 8;
		if (verbose >= 0 && n == 0)
			fprintf (stdlog, "%s%sEXTRACT CS12:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		if (testmode)
		{
			n++;
			continue;
		}
		u8 *out = CALLOC (1, 0x60 + bytes);
		if (!out)
		{
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		wr_be32 (out + 0x00, frames * 14);
		wr_be32 (out + 0x04, frames * 16);
		wr_be32 (out + 0x08, rate);
		wr_be32 (out + 0x10, 2);
		wr_be32 (out + 0x14, frames * 16 - 1);
		wr_be32 (out + 0x18, 2);
		memcpy (out + 0x1c, raw + c + 0x80 + sh, 32);
		out[0x3f] = raw[c + 0xf0 + sh];
		out[0x45] = raw[c + 0xf0 + sh];
		memcpy (out + 0x60, raw + c + 0xf0 + sh, bytes);
		char name[64];
		snprintf (name, sizeof (name), "sound_%04u.dsp", n);
		err = shade_write (arg, dest, basedir, name, out, 0x60 + bytes);
		FREE (out);
		n++;
	}
	FREE (raw);
	if (!err && n && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return n ? err : ERR_NOTHING_TO_DO;
}
