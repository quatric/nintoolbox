// Nintendo Switch Opus (".opus" / ".lopus" / ".opusnx"; magic 0x80000001 little endian, or the big endian "OPUS" wrapper of NUS3AUDIO
// files): the header chunks (0x80000001 info, 0x80000003 context, 0x80000004 data, 0x80000005 multistream) are read like vgmstream's opus.c
// and the packets (each prefixed by a big endian u32 size and a u32 final-range value) are repacked into a standard Ogg Opus file
// "<name>.ogg", which is then decoded to "<name>.wav" with ffmpeg/mobipeg when one is installed.
static u32 swo_crc_tab[256];

static u32 swo_crc (u32 crc, const u8 *p, size_t n)
{
	if (!swo_crc_tab[1])
		for (uint i = 0; i < 256; i++)
		{
			u32 r = i << 24;
			for (uint k = 0; k < 8; k++)
				r = r & 0x80000000u ? (r << 1) ^ 0x04c11db7u : r << 1;
			swo_crc_tab[i] = r;
		}
	for (size_t i = 0; i < n; i++)
		crc = (crc << 8) ^ swo_crc_tab[(crc >> 24) ^ p[i]];
	return crc;
}

// samples (at 48 kHz) of one Opus packet, 0 when invalid
static uint swo_packet_samples (const u8 *p, uint n)
{
	if (!n)
		return 0;
	const uint toc = p[0], cfg = toc >> 3;
	uint dur; // in 2.5 ms units * 10 => use samples at 48k
	if (cfg < 12)
		dur = (cfg & 3) == 0 ? 480 : (cfg & 3) == 1 ? 960 : (cfg & 3) == 2 ? 1920 : 2880;
	else if (cfg < 16)
		dur = (cfg & 1) ? 960 : 480;
	else
		dur = (120 << (cfg & 3));
	uint frames = 1;
	switch (toc & 3)
	{
	case 1: case 2: frames = 2; break;
	case 3: if (n < 2) return 0; frames = p[1] & 63; break;
	}
	return dur * frames;
}

typedef struct swo_ogg_t { u8 *d; size_t n, cap; u32 serial, seq; } swo_ogg_t;

static void swo_put (swo_ogg_t *o, const u8 *s, size_t n)
{
	if (o->n + n > o->cap)
	{
		size_t c = o->cap ? o->cap : 4096;
		while (c < o->n + n)
			c *= 2;
		u8 *p = REALLOC (o->d, c);
		if (!p)
			return;
		o->d = p;
		o->cap = c;
	}
	memcpy (o->d + o->n, s, n);
	o->n += n;
}

// one packet per page (packets up to 255*255 bytes); flags: 2 = first page, 4 = last page
static void swo_page (swo_ogg_t *o, const u8 *pkt, uint len, u64 granule, uint flags)
{
	u8 hdr[27 + 256], *h = hdr;
	uint nseg = len / 255 + 1;
	if (nseg > 255)
		return;
	memcpy (h, "OggS", 4);
	h[4] = 0; h[5] = flags;
	for (uint i = 0; i < 8; i++) h[6 + i] = granule >> (8 * i);
	for (uint i = 0; i < 4; i++) h[14 + i] = o->serial >> (8 * i);
	for (uint i = 0; i < 4; i++) h[18 + i] = o->seq >> (8 * i);
	h[22] = h[23] = h[24] = h[25] = 0;
	h[26] = nseg;
	for (uint i = 0; i + 1 < nseg; i++) h[27 + i] = 255;
	h[27 + nseg - 1] = len % 255;
	const size_t hl = 27 + nseg;
	u32 crc = swo_crc (swo_crc (0, hdr, hl), pkt, len);
	for (uint i = 0; i < 4; i++) h[22 + i] = crc >> (8 * i);
	swo_put (o, hdr, hl);
	swo_put (o, pkt, len);
	o->seq++;
}

static enumError extract_switch_opus_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".opus") && !is_ext (arg, ".lopus") && !is_ext (arg, ".opusnx")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	swo_ogg_t og = { 0 };
	size_t base = 0;
	if (sz > 0x28 && !memcmp (raw, "OPUS", 4))
		base = rd_be32 (raw + 0x20);
	if (base + 0x28 < sz && rd_le32 (raw + base) == 0x80000001)
	{
		const u8 *h = raw + base;
		const uint ch = h[9], skip = h[0x1c] | h[0x1d] << 8;
		uint rate = rd_le32 (h + 0x0c);
		const u32 data_off = rd_le32 (h + 0x10);
		const uint mso = rd_le32 (h + 0x20) == 0x80000005 && ch <= 8 && ch > 2;
		(void)rate;
		if (ch >= 1 && ch <= 8 && base + data_off + 8 <= sz && rd_le32 (raw + base + data_off) == 0x80000004)
		{
			const u32 dsz = rd_le32 (raw + base + data_off + 4);
			const u8 *p = raw + base + data_off + 8, *end = p + dsz;
			if (end > raw + sz)
				end = raw + sz;
			// OpusHead
			u8 oh[19 + 2 + 8] = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, (u8)ch, (u8)skip, (u8)(skip >> 8), 0x80, 0xbb, 0, 0, 0, 0, 0 };
			uint ohn = 19;
			if (mso)
			{
				oh[18] = 1;
				oh[19] = h[0x28];
				oh[20] = h[0x29];
				ohn = 21;
				u8 big[21 + 8];
				memcpy (big, oh, 21);
				for (uint i = 0; i < ch; i++)
					big[ohn++] = h[0x2a + i];
				memcpy (oh, big, ohn);
			}
			og.serial = 0x4e494e54;
			swo_page (&og, oh, ohn, 0, 2);
			static const u8 tags[] = { 'O', 'p', 'u', 's', 'T', 'a', 'g', 's', 5, 0, 0, 0, 'n', 'i', 'n', 't', 'o', 0, 0, 0, 0 };
			swo_page (&og, tags, sizeof (tags), 0, 0);
			u64 gp = 0;
			uint npk = 0;
			while (p + 8 <= end)
			{
				const uint len = rd_be32 (p);
				if (!len || p + 8 + len > end)
					break;
				const uint ns = swo_packet_samples (p + 8, len);
				if (!ns)
					break;
				gp += ns;
				p += 8 + len;
				swo_page (&og, p - len, len, gp, p + 8 > end ? 4 : 0);
				npk++;
			}
			if (npk)
			{
				char dest[PATH_MAX], name[PATH_MAX], wav[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) *dot = 0;
				char ogg[PATH_MAX];
				snprintf (ogg, sizeof (ogg), "%s.ogg", name);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE SWITCH-OPUS:%s (%u ch, %u packets) -> OGG/WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, npk);
				if (testmode)
					err = ERR_OK;
				else
				{
					err = shade_write (arg, dest, basedir, ogg, og.d, (uint)og.n);
					if (!err)
					{
						snprintf (ogg, sizeof (ogg), "%s/%s%s.ogg", dest, basedir ? basedir : "", name);
						snprintf (wav, sizeof (wav), "%s/%s%s.wav", dest, basedir ? basedir : "", name);
						if (PassthruDecodeAudio (ogg, wav) == ERR_NOTHING_TO_DO && verbose >= 0)
							fprintf (stdlog, "  (no ffmpeg found, only the Ogg Opus file was written)\n");
					}
				}
			}
		}
	}
	FREE (og.d);
	FREE (raw);
	return err;
}
