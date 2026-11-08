// Electronic Arts "SCHl" streams whose audio is EALayer3 (codec2 0x17, Wii/GC/PS2/X360 EA games: *Celebrity Sports
// Showdown* ".ast", ...), an MP3 variant ffmpeg does not know. EA frames hold one granule each (header byte 0 =
// 0x00 or 0xee for a raw PCM block) with a compact MPEG header + side info; two granules are re-assembled into a
// standard MPEG-1 Layer III frame (one for MPEG-2/2.5), written as 320/160 kbit CBR so no bit reservoir is
// needed. Algorithm from vgmstream's mpeg_custom_utils_ealayer3.c (Zench's ealayer3 reverse engineering).
// The audio data of a "SCDl" block starts at +0x0c + channels * 4 + first offset; frames run across blocks and
// blocks end with up to 3 padding bytes. Raw PCM blocks (encoder prefetch) are skipped. Each stream of the file
// (SCHl .. SCEl) becomes "<file>.mp3" (or "<file>.N.mp3" for the 2nd+ stream).
typedef struct ealsc_frame_t
{
	uint flag, vi, si, cm, me, ch, mpeg1, gi, sr, md[2], scfsi[2], o1[2], o2[2], data_off, common, pcm, size;
	size_t at;
} ealsc_frame_t;

static uint ealsc_get (const u8 *b, size_t *pos, uint n)
{
	uint v = 0;
	while (n--)
	{
		v = (v << 1) | ((b[*pos >> 3] >> (7 - (*pos & 7))) & 1);
		(*pos)++;
	}
	return v;
}

static void ealsc_put (u8 *b, size_t *pos, uint n, u32 v)
{
	while (n--)
	{
		const uint bit = (v >> n) & 1;
		const size_t o = *pos >> 3;
		const uint sh = 7 - (*pos & 7);
		b[o] = (u8)((b[o] & ~(1u << sh)) | (bit << sh));
		(*pos)++;
	}
}

static bool ealsc_parse (const u8 *d, size_t n, size_t at, uint chpf, ealsc_frame_t *f)
{
	static const int vt[4] = {3, -1, 2, 1};
	static const uint sr[4][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}, {44100, 48000, 32000}};
	memset (f, 0, sizeof (*f));
	if (at + 4 > n)
		return false;
	// at most 0x1000 bytes of header+side info are looked at; the reader never goes beyond 'n'
	u8 tmp[0x40];
	memset (tmp, 0, sizeof (tmp));
	memcpy (tmp, d + at, n - at < sizeof (tmp) ? n - at : sizeof (tmp));
	size_t p = 0;
	f->at = at;
	f->flag = ealsc_get (tmp, &p, 8);
	uint pre = 1;
	if (f->flag != 0 && f->flag != 0xee)
		return false;
	if (f->flag == 0)
	{
		f->vi = ealsc_get (tmp, &p, 2);
		f->si = ealsc_get (tmp, &p, 2);
		f->cm = ealsc_get (tmp, &p, 2);
		f->me = ealsc_get (tmp, &p, 2);
		if ((!f->vi && !f->si && !f->cm && !f->me) || vt[f->vi] < 0 || f->si > 2 || !sr[f->vi][f->si])
			return false;
		f->ch = f->cm == 3 ? 1 : 2;
		f->mpeg1 = vt[f->vi] == 1;
		f->sr = sr[f->vi][f->si];
		const uint o2b = f->mpeg1 ? 15 : 19;
		const size_t need = 8 + 8 + 1 + (f->mpeg1 ? 8 : 0) + (size_t)f->ch * (12 + 32 + o2b);
		if (need > sizeof (tmp) * 8)
			return false;
		f->gi = ealsc_get (tmp, &p, 1);
		if (f->mpeg1 && f->gi == 1)
			for (uint i = 0; i < f->ch; i++)
				f->scfsi[i] = ealsc_get (tmp, &p, 4);
		uint ds = 0;
		for (uint i = 0; i < f->ch; i++)
		{
			f->md[i] = ealsc_get (tmp, &p, 12);
			f->o1[i] = ealsc_get (tmp, &p, 32);
			f->o2[i] = ealsc_get (tmp, &p, o2b);
			ds += f->md[i];
		}
		f->data_off = (uint)p;
		const uint total = (uint)p - 8 + ds;
		f->common = (total + ((8 - total % 8) % 8)) / 8;
	}
	else
	{
		if (at + 5 > n)
			return false;
		const uint ns = (d[at + 3] << 8) | d[at + 4];
		pre += 4;
		f->pcm = 2 * ns * chpf;
	}
	f->size = pre + f->common + f->pcm;
	return at + f->size <= n;
}

static uint ealsc_bits (const u8 *d, const ealsc_frame_t *f, u8 *out, size_t *op)
{
	size_t p = (size_t)f->data_off;
	const u8 *b = d + f->at;
	for (uint i = 0; i < f->ch; i++)
		for (uint j = 0; j < f->md[i]; j++)
			ealsc_put (out, op, 1, ealsc_get (b, &p, 1));
	return 0;
}

// returns frame size in bytes (0 on failure); out must hold >= 2100 bytes
static uint ealsc_rebuild (const u8 *d, const ealsc_frame_t *f0, const ealsc_frame_t *f1, u8 *out)
{
	const uint size = f0->mpeg1 ? 144u * 320 * 1000 / f0->sr : 72u * 160 * 1000 / f0->sr;
	memset (out, 0, size);
	size_t p = 0;
	ealsc_put (out, &p, 11, 0x7ff);
	ealsc_put (out, &p, 2, f0->vi);
	ealsc_put (out, &p, 2, 1);
	ealsc_put (out, &p, 1, 1);
	ealsc_put (out, &p, 4, 14);
	ealsc_put (out, &p, 2, f0->si);
	ealsc_put (out, &p, 2, 0);
	ealsc_put (out, &p, 2, f0->cm);
	ealsc_put (out, &p, 2, f0->me);
	ealsc_put (out, &p, 1, 1);
	ealsc_put (out, &p, 1, 1);
	ealsc_put (out, &p, 2, 0);
	if (f0->mpeg1)
	{
		ealsc_put (out, &p, 9, 0);
		ealsc_put (out, &p, f0->ch == 1 ? 5 : 3, 0);
		for (uint i = 0; i < f1->ch; i++)
			ealsc_put (out, &p, 4, f1->scfsi[i]);
		for (int g = 0; g < 2; g++)
		{
			const ealsc_frame_t *f = g ? f1 : f0;
			for (uint i = 0; i < f->ch; i++)
			{
				ealsc_put (out, &p, 12, f->md[i]);
				ealsc_put (out, &p, 32, f->o1[i]);
				ealsc_put (out, &p, 15, f->o2[i]);
			}
		}
		if (p / 8 + (f0->common + f1->common) > size + 64)
			return 0;
		ealsc_bits (d, f0, out, &p);
		ealsc_bits (d, f1, out, &p);
	}
	else
	{
		ealsc_put (out, &p, 8, 0);
		ealsc_put (out, &p, f0->ch == 1 ? 1 : 2, 0);
		for (uint i = 0; i < f0->ch; i++)
		{
			ealsc_put (out, &p, 12, f0->md[i]);
			ealsc_put (out, &p, 32, f0->o1[i]);
			ealsc_put (out, &p, 19, f0->o2[i]);
		}
		ealsc_bits (d, f0, out, &p);
	}
	return (p + 7) / 8 <= size ? size : 0;
}

static enumError ealsc_flush (ccp arg, const u8 *buf, size_t blen, const size_t *starts, uint nstarts, uint chpf,
	uint index, uint *n_out)
{
	size_t cap = blen * 4 + 4096, olen = 0;
	u8 *res = MALLOC (cap);
	if (!res)
		return ERR_OUT_OF_MEMORY;
	u8 frame[4200];
	uint si = 0;
#define EALSC_SNAP(o)                                                                                                   \
	do                                                                                                                 \
	{                                                                                                                  \
		while (si < nstarts && starts[si] < (o))                                                                       \
			si++;                                                                                                      \
		if (si < nstarts && starts[si] > (o) && starts[si] - (o) < 4)                                                  \
			(o) = starts[si];                                                                                          \
	} while (0)
	size_t o = 0;
	ealsc_frame_t f0, f1;
	while (o + 4 < blen && ealsc_parse (buf, blen, o, chpf, &f0))
	{
		o += f0.size;
		EALSC_SNAP (o);
		if (!f0.common)
			continue;
		bool have = true;
		if (f0.mpeg1)
		{
			have = false;
			while (o + 4 < blen && ealsc_parse (buf, blen, o, chpf, &f1))
			{
				o += f1.size;
				EALSC_SNAP (o);
				if (f1.common)
				{
					have = true;
					break;
				}
			}
			if (!have || f1.mpeg1 != f0.mpeg1 || f1.vi != f0.vi || f1.gi == f0.gi)
				break;
		}
		const uint fs = ealsc_rebuild (buf, &f0, f0.mpeg1 ? &f1 : &f0, frame);
		if (fs && olen + fs <= cap)
		{
			memcpy (res + olen, frame, fs);
			olen += fs;
		}
	}
#undef EALSC_SNAP
	enumError err = ERR_NOTHING_TO_DO;
	if (olen)
	{
		char dest[PATH_MAX];
		if (index)
			snprintf (dest, sizeof (dest), "%s.%u.mp3", arg, index);
		else
			snprintf (dest, sizeof (dest), "%s.mp3", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT EA-LAYER3:%s -> MP3:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (res, 1, olen, F.f) != olen)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", dest);
			ResetFile (&F, opt_preserve);
			if (!err && opt_decode)
			{
				char wav[PATH_MAX];
				snprintf (wav, sizeof (wav), "%s.wav", dest);
				PassthruDecodeAudio (dest, wav);
			}
		}
		(*n_out)++;
	}
	FREE (res);
	return err;
}

static enumError extract_ealayer3_file (ccp arg)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 16 || memcmp (raw, "SCHl", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t b = 0;
	uint index = 0, n_out = 0;
	enumError err = ERR_OK;
	while (!err && b + 12 <= sz)
	{
		while (b + 12 <= sz && !rd_be32 (raw + b))
			b += 4; // zero padding between streams
		if (b + 12 > sz || memcmp (raw + b, "SCHl", 4))
			break;
		const size_t hsz = rd_le32 (raw + b + 4);
		eaxa_hdr_t h;
		if (hsz < 12 || hsz > sz - b || !eaxa_parse (raw + b, hsz, &h) || h.codec2 != 0x17 || h.channels < 1 || h.channels > 2)
			break;
		u8 *buf = MALLOC (sz - b);
		size_t *starts = MALLOC (sizeof (size_t) * (1 + (sz - b) / 12));
		if (!buf || !starts)
		{
			FREE (buf);
			FREE (starts);
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		size_t blen = 0;
		uint ns = 0;
		b += hsz;
		while (b + 12 <= sz)
		{
			size_t bs = rd_le32 (raw + b + 4);
			if (bs > sz - b || bs < 8)
				bs = rd_be32 (raw + b + 4);
			if (!memcmp (raw + b, "SCEl", 4) || !memcmp (raw + b, "SCHl", 4) || bs < 8 || bs > sz - b)
			{
				if (!memcmp (raw + b, "SCEl", 4) && bs >= 8 && bs <= sz - b)
					b += bs;
				break;
			}
			if (!memcmp (raw + b, "SCDl", 4) && bs >= 12 + 4 * (size_t)h.channels)
			{
				const size_t st = 12 + 4 * (size_t)h.channels + rd_be32 (raw + b + 12);
				if (st <= bs)
				{
					memcpy (buf + blen, raw + b + st, bs - st);
					blen += bs - st;
					starts[ns++] = blen;
				}
			}
			b += bs;
		}
		err = ealsc_flush (arg, buf, blen, starts, ns, h.channels, index, &n_out);
		if (err == ERR_NOTHING_TO_DO)
			err = ERR_OK;
		FREE (buf);
		FREE (starts);
		index++;
	}
	FREE (raw);
	return err ? err : n_out ? ERR_OK : ERR_NOTHING_TO_DO;
}
