// Shin'en ".ogl" (Jett Rocket, Wii): u32 LE loop offset, loop start sample, loop block, sample count, size without padding,
// then Vorbis packets each prefixed with a u16 LE (size << 2 | flags): the identification ("\x01vorb"), comment and setup
// packets, followed by the audio packets. They are re-wrapped unchanged into a standard Ogg Vorbis stream
// (granule positions are only set on the final page: they would need the Vorbis block sizes) -> "<name>.ogg"
static u32 ogl_crc (const u8 *p, size_t n)
{
	static u32 tab[256];
	if (!tab[1])
		for (uint i = 0; i < 256; i++)
		{
			u32 r = i << 24;
			for (uint k = 0; k < 8; k++)
				r = r & 0x80000000 ? (r << 1) ^ 0x04c11db7 : r << 1;
			tab[i] = r;
		}
	u32 crc = 0;
	for (size_t i = 0; i < n; i++)
		crc = (crc << 8) ^ tab[(crc >> 24) ^ p[i]];
	return crc;
}

typedef struct
{
	u8 *buf;
	size_t len, cap;
	u32 serial, seq;
} ogl_out_t;

// one page holding whole packets (<= 255 lacing values)
static bool ogl_page (ogl_out_t *o, uint flags, s64 granule, const u8 *const *pk, const size_t *psz, uint np)
{
	u8 lace[256];
	uint nl = 0;
	size_t total = 0;
	for (uint i = 0; i < np; i++)
	{
		size_t s = psz[i];
		for (; s >= 255; s -= 255)
		{
			if (nl >= 255)
				return false;
			lace[nl++] = 255;
		}
		if (nl >= 255)
			return false;
		lace[nl++] = (u8)s;
		total += psz[i];
	}
	const size_t pl = 27 + nl + total;
	if (o->len + pl > o->cap)
	{
		u8 *nb = REALLOC (o->buf, (o->cap = (o->len + pl) * 2));
		if (!nb)
			return false;
		o->buf = nb;
	}
	u8 *h = o->buf + o->len;
	memcpy (h, "OggS", 4);
	h[4] = 0;
	h[5] = (u8)flags;
	wr_le32 (h + 6, (u32)granule);
	wr_le32 (h + 10, (u32)((u64)granule >> 32));
	wr_le32 (h + 14, o->serial);
	wr_le32 (h + 18, o->seq++);
	wr_le32 (h + 22, 0);
	h[26] = (u8)nl;
	memcpy (h + 27, lace, nl);
	size_t at = 27 + nl;
	for (uint i = 0; i < np; i++)
	{
		memcpy (h + at, pk[i], psz[i]);
		at += psz[i];
	}
	wr_le32 (h + 22, ogl_crc (h, pl));
	o->len += pl;
	return true;
}

static enumError extract_ogl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".ogl"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x40 || rd_le32 (raw) > 0x10000000 || memcmp (raw + 0x17, "vorb", 4) || rd_le32 (raw + 0x10) > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nsamples = rd_le32 (raw + 0x0c);
	ogl_out_t o = {0};
	o.serial = 0x4f474c31;
	size_t at = 0x14;
	const u8 *pk[64];
	size_t ps[64];
	uint np = 0, ntri = 0;
	bool ok = true;
	// header triad
	for (uint i = 0; i < 3 && ok; i++)
	{
		const size_t n = at + 2 <= sz ? rd_le16 (raw + at) >> 2 : 0;
		if (!n || at + 2 + n > sz)
		{
			ok = false;
			break;
		}
		pk[i] = raw + at + 2;
		ps[i] = n;
		at += 2 + n;
		ntri++;
	}
	ok = ok && ogl_page (&o, 2, 0, pk, ps, 1) && ogl_page (&o, 0, 0, pk + 1, ps + 1, 2);
	// audio packets: gather pages of up to 200 lacing values / 16 packets
	while (ok && at + 2 <= sz)
	{
		np = 0;
		size_t lace = 0;
		bool last = false;
		while (np < 16 && at + 2 <= sz)
		{
			const size_t n = rd_le16 (raw + at) >> 2;
			if (!n || rd_le16 (raw + at) == 0xffff || at + 2 + n > sz)
			{
				last = true;
				break;
			}
			if (lace + n / 255 + 1 > 200 && np)
				break;
			pk[np] = raw + at + 2;
			ps[np++] = n;
			lace += n / 255 + 1;
			at += 2 + n;
		}
		if (!np)
			break;
		const bool eos = last || at + 2 > sz || !(rd_le16 (raw + at) >> 2) || rd_le16 (raw + at) == 0xffff;
		ok = ogl_page (&o, eos ? 4 : 0, eos ? (nsamples ? (s64)nsamples : -1) : -1, pk, ps, np);
		if (eos)
			break;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && ntri == 3 && o.len)
	{
		char dest[PATH_MAX];
		beside_source_dest_ext (dest, sizeof (dest), arg, ".ogg");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT OGL:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (o.buf, 1, o.len, F.f) != o.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", o.len, dest);
			const enumError ce = ResetFile (&F, opt_preserve);
			err = err ? err : ce;
		}
	}
	FREE (o.buf);
	FREE (raw);
	return err;
}
