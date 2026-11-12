// Metroid Prime (GameCube) ".rsf": headerless stereo G.721 (32 kbit/s CCITT ADPCM) at 32000 Hz; the file is split into two halves (one per channel, interleave =
// (size + 1) / 2) of 4-bit codes, low nibble first. The format is recognised by the absence of zero nibbles in the first 0x20 bytes of both halves (after vgmstream
// raw_rsf.c). The decoder is Sun Microsystems' public-domain CCITT G.721 reference (the same code vgmstream uses), output as 16 bit PCM (sr << 2) to "<name>.wav".
typedef struct rsf_g721_t
{
	int yl;
	short yu, dms, dml, ap, a[2], b[6], pk[2], dq[6], sr[2];
	char td;
} rsf_g721_t;

static const short rsf_power2[15] = { 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000 };
static const short rsf_dqlntab[16] = { -2048, 4, 135, 213, 273, 323, 373, 425, 425, 373, 323, 273, 213, 135, 4, -2048 };
static const short rsf_witab[16] = { -12, 18, 41, 64, 112, 198, 355, 1122, 1122, 355, 198, 112, 64, 41, 18, -12 };
static const short rsf_fitab[16] = { 0, 0, 0, 0x200, 0x200, 0x200, 0x600, 0xE00, 0xE00, 0x600, 0x200, 0x200, 0x200, 0, 0, 0 };

static int rsf_quan (int val)
{
	int i;
	for (i = 0; i < 15; i++)
		if (val < rsf_power2[i])
			break;
	return i;
}

static int rsf_fmult (int an, int srn)
{
	short anmag, anexp, anmant, wanexp, wanmant, retval;
	anmag = (an > 0) ? an : ((-an) & 0x1FFF);
	anexp = rsf_quan (anmag) - 6;
	anmant = (anmag == 0) ? 32 : (anexp >= 0) ? anmag >> anexp : anmag << -anexp;
	wanexp = anexp + ((srn >> 6) & 0xF) - 13;
	wanmant = (anmant * (srn & 077) + 0x30) >> 4;
	retval = (wanexp >= 0) ? ((wanmant << wanexp) & 0x7FFF) : (wanmant >> -wanexp);
	return ((an ^ srn) < 0) ? -retval : retval;
}

static void rsf_g721_init (rsf_g721_t *s)
{
	s->yl = 34816;
	s->yu = 544;
	s->dms = s->dml = s->ap = 0;
	for (int i = 0; i < 2; i++)
	{
		s->a[i] = 0;
		s->pk[i] = 0;
		s->sr[i] = 32;
	}
	for (int i = 0; i < 6; i++)
	{
		s->b[i] = 0;
		s->dq[i] = 32;
	}
	s->td = 0;
}

static void rsf_g721_update (int y, int wi, int fi, int dq, int sr, int dqsez, rsf_g721_t *s)
{
	short mag, exp, a2p, a1ul, pks1, fa1, ylint, thr2, dqthr, ylfrac, thr1, pk0;
	char tr;
	pk0 = dqsez < 0 ? 1 : 0;
	mag = dq & 0x7FFF;
	ylint = s->yl >> 15;
	ylfrac = (s->yl >> 10) & 0x1F;
	thr1 = (32 + ylfrac) << ylint;
	thr2 = ylint > 9 ? 31 << 10 : thr1;
	dqthr = (thr2 + (thr2 >> 1)) >> 1;
	tr = s->td == 0 ? 0 : mag <= dqthr ? 0 : 1;
	s->yu = y + ((wi - y) >> 5);
	if (s->yu < 544)
		s->yu = 544;
	else if (s->yu > 5120)
		s->yu = 5120;
	s->yl += s->yu + ((-s->yl) >> 6);
	if (tr == 1)
	{
		s->a[0] = s->a[1] = 0;
		for (int i = 0; i < 6; i++)
			s->b[i] = 0;
		a2p = 0;
	}
	else
	{
		pks1 = pk0 ^ s->pk[0];
		a2p = s->a[1] - (s->a[1] >> 7);
		if (dqsez != 0)
		{
			fa1 = pks1 ? s->a[0] : -s->a[0];
			if (fa1 < -8191)
				a2p -= 0x100;
			else if (fa1 > 8191)
				a2p += 0xFF;
			else
				a2p += fa1 >> 5;
			if (pk0 ^ s->pk[1])
			{
				if (a2p <= -12160)
					a2p = -12288;
				else if (a2p >= 12416)
					a2p = 12288;
				else
					a2p -= 0x80;
			}
			else if (a2p <= -12416)
				a2p = -12288;
			else if (a2p >= 12160)
				a2p = 12288;
			else
				a2p += 0x80;
		}
		s->a[1] = a2p;
		s->a[0] -= s->a[0] >> 8;
		if (dqsez != 0)
		{
			if (pks1 == 0)
				s->a[0] += 192;
			else
				s->a[0] -= 192;
		}
		a1ul = 15360 - a2p;
		if (s->a[0] < -a1ul)
			s->a[0] = -a1ul;
		else if (s->a[0] > a1ul)
			s->a[0] = a1ul;
		for (int i = 0; i < 6; i++)
		{
			s->b[i] -= s->b[i] >> 8;
			if (dq & 0x7FFF)
			{
				if ((dq ^ s->dq[i]) >= 0)
					s->b[i] += 128;
				else
					s->b[i] -= 128;
			}
		}
	}
	for (int i = 5; i > 0; i--)
		s->dq[i] = s->dq[i - 1];
	if (mag == 0)
		s->dq[0] = dq >= 0 ? 0x20 : 0xFC20;
	else
	{
		exp = rsf_quan (mag);
		s->dq[0] = dq >= 0 ? (exp << 6) + ((mag << 6) >> exp) : (exp << 6) + ((mag << 6) >> exp) - 0x400;
	}
	s->sr[1] = s->sr[0];
	if (sr == 0)
		s->sr[0] = 0x20;
	else if (sr > 0)
	{
		exp = rsf_quan (sr);
		s->sr[0] = (exp << 6) + ((sr << 6) >> exp);
	}
	else if (sr > -32768)
	{
		mag = -sr;
		exp = rsf_quan (mag);
		s->sr[0] = (exp << 6) + ((mag << 6) >> exp) - 0x400;
	}
	else
		s->sr[0] = 0xFC20;
	s->pk[1] = s->pk[0];
	s->pk[0] = pk0;
	if (tr == 1)
		s->td = 0;
	else if (a2p < -11776)
		s->td = 1;
	else
		s->td = 0;
	s->dms += (fi - s->dms) >> 5;
	s->dml += ((fi << 2) - s->dml) >> 7;
	if (tr == 1)
		s->ap = 256;
	else if (y < 1536)
		s->ap += (0x200 - s->ap) >> 4;
	else if (s->td == 1)
		s->ap += (0x200 - s->ap) >> 4;
	else if (abs ((s->dms << 2) - s->dml) >= (s->dml >> 3))
		s->ap += (0x200 - s->ap) >> 4;
	else
		s->ap += (-s->ap) >> 4;
}

static int rsf_g721_decode (int code, rsf_g721_t *s)
{
	short sezi, sei, sez, se, y, sr, dq, dqsez;
	code &= 0x0f;
	sezi = rsf_fmult (s->b[0] >> 2, s->dq[0]);
	for (int i = 1; i < 6; i++)
		sezi += rsf_fmult (s->b[i] >> 2, s->dq[i]);
	sez = sezi >> 1;
	sei = sezi + rsf_fmult (s->a[1] >> 2, s->sr[1]) + rsf_fmult (s->a[0] >> 2, s->sr[0]);
	se = sei >> 1;
	if (s->ap >= 256)
		y = s->yu;
	else
	{
		int yy = s->yl >> 6, dif = s->yu - yy, al = s->ap >> 2;
		if (dif > 0)
			yy += (dif * al) >> 6;
		else if (dif < 0)
			yy += (dif * al + 0x3F) >> 6;
		y = yy;
	}
	{
		const int sign = code & 0x08;
		short dql = rsf_dqlntab[code] + (y >> 2);
		if (dql < 0)
			dq = sign ? -0x8000 : 0;
		else
		{
			short dex = (dql >> 7) & 15, dqt = 128 + (dql & 127), d = (dqt << 7) >> (14 - dex);
			dq = sign ? (d - 0x8000) : d;
		}
	}
	sr = dq < 0 ? se - (dq & 0x3FFF) : se + dq;
	dqsez = sr - se + sez;
	rsf_g721_update (y, rsf_witab[code] << 5, rsf_fitab[code], dq, sr, dqsez, s);
	return sr << 2;
}

static enumError extract_rsf_g721_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".rsf") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t il = (sz + 1) / 2;
	bool ok = sz > 0x100;
	for (uint i = 0; ok && i < 0x20; i++)
		ok = (raw[i] & 15) && (raw[i] & 0xf0) && (raw[il + i] & 15) && (raw[il + i] & 0xf0);
	if (ok)
	{
		const uint ns = (uint)(il * 2 < sz * 1 ? il * 2 : sz);
		s16 *pcm = CALLOC ((size_t)ns * 2, sizeof (s16));
		if (pcm)
		{
			for (uint c = 0; c < 2; c++)
			{
				rsf_g721_t st;
				rsf_g721_init (&st);
				for (uint i = 0; i < ns && il * c + i / 2 < sz; i++)
				{
					const uint code = raw[il * c + i / 2] >> (i & 1 ? 4 : 0);
					pcm[(size_t)i * 2 + c] = (s16)rsf_g721_decode (code, &st);
				}
			}
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".wav");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE RSF-G721:%s (2 ch, 32000 Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ns);
			if (testmode)
				err = ERR_OK;
			else
			{
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (pcm, ns, 2, 32000, &wn);
				err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
				FREE (w);
			}
			FREE (pcm);
		}
	}
	FREE (raw);
	return err;
}
