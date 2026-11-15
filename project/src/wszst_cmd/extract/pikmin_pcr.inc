// Pikmin 1 ".pcr" particle generator files ("1-02", u16 offset of the generator parameter block at +4, big-endian).
// The parameter block (zen::particleGenerator::pmSetDDF in the decompilation) is written as "<name>.pcr.json";
// the billboard / oriented draw header in front of it is kept as raw hex.
typedef struct
{
	const u8 *d;
	size_t sz, p;
	bool ok;
	char *out;
	size_t len, cap;
	bool first;
} pcr_ctx_t;

static void pcr_emit (pcr_ctx_t *c, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	va_start (ap, fmt);
	const int n = vsnprintf (tmp, sizeof (tmp), fmt, ap);
	va_end (ap);
	if (n < 0 || c->len + (size_t)n + 1 > c->cap)
	{
		const size_t nc = (c->cap + n + 1) * 2;
		char *no = REALLOC (c->out, nc);
		if (!no)
		{
			c->ok = false;
			return;
		}
		c->out = no;
		c->cap = nc;
	}
	memcpy (c->out + c->len, tmp, n);
	c->len += n;
	c->out[c->len] = 0;
}

static void pcr_key (pcr_ctx_t *c, const char *name)
{
	pcr_emit (c, "%s\n  \"%s\": ", c->first ? "" : ",", name);
	c->first = false;
}

static bool pcr_need (pcr_ctx_t *c, size_t n)
{
	if (c->p + n > c->sz)
		c->ok = false;
	return c->ok;
}

static void pcr_float (pcr_ctx_t *c, const char *name, uint adv)
{
	if (!pcr_need (c, adv < 4 ? 4 : adv))
		return;
	u32 u = rd_be32 (c->d + c->p);
	float f;
	memcpy (&f, &u, 4);
	pcr_key (c, name);
	pcr_emit (c, "%.9g", (double)f);
	c->p += adv;
}

static void pcr_vec (pcr_ctx_t *c, const char *name)
{
	if (!pcr_need (c, 12))
		return;
	float f[3];
	for (int i = 0; i < 3; i++)
	{
		u32 u = rd_be32 (c->d + c->p + 4 * i);
		memcpy (f + i, &u, 4);
	}
	pcr_key (c, name);
	pcr_emit (c, "[%.9g, %.9g, %.9g]", (double)f[0], (double)f[1], (double)f[2]);
	c->p += 12;
}

static void pcr_u8 (pcr_ctx_t *c, const char *name, uint adv)
{
	if (!pcr_need (c, adv))
		return;
	pcr_key (c, name);
	pcr_emit (c, "%u", c->d[c->p]);
	c->p += adv;
}

static void pcr_s16 (pcr_ctx_t *c, const char *name, uint adv)
{
	if (!pcr_need (c, adv < 2 ? 2 : adv))
		return;
	pcr_key (c, name);
	pcr_emit (c, "%d", (int)(int16_t)rd_be16 (c->d + c->p));
	c->p += adv;
}

static void pcr_curve (pcr_ctx_t *c, const char *name, uint n)
{
	if (!pcr_need (c, (size_t)n * 8))
		return;
	char key[64];
	for (int pass = 0; pass < 2; pass++)
	{
		snprintf (key, sizeof (key), "%s%s", name, pass ? "Values" : "Keyframes");
		pcr_key (c, key);
		pcr_emit (c, "[");
		for (uint i = 0; i < n; i++)
		{
			u32 u = rd_be32 (c->d + c->p + 4 * i);
			float f;
			memcpy (&f, &u, 4);
			pcr_emit (c, "%s%.9g", i ? ", " : "", (double)f);
		}
		pcr_emit (c, "]");
		c->p += (size_t)n * 4;
	}
}

static enumError extract_pikmin_pcr_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".pcr"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = sz > 0x20 && !memcmp (raw, "1-0", 3);
	c.first = true;
	const uint ddf = c.ok ? rd_be16 (raw + 4) : 0;
	if (!c.ok || ddf < 8 || ddf + 0x40 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	c.p = ddf;
	pcr_emit (&c, "{");
	pcr_key (&c, "version");
	pcr_emit (&c, "\"%.4s\"", (ccp)raw);
	pcr_key (&c, "drawMode");
	pcr_emit (&c, "\"%s\"", raw[6] ? "billboard" : "oriented");
	pcr_key (&c, "drawHeaderHex");
	pcr_emit (&c, "\"");
	for (uint i = 8; i < ddf; i++)
		pcr_emit (&c, "%02x", raw[i]);
	pcr_emit (&c, "\"");
	pcr_key (&c, "flags");
	const u32 flags = c.p + 4 <= sz ? rd_be32 (raw + c.p) : 0;
	pcr_emit (&c, "%u", flags);
	c.p += 4;
	pcr_vec (&c, "emitPosOffset");
	pcr_vec (&c, "emitDir");
	pcr_vec (&c, "emissionBoxSize");
	pcr_float (&c, "emissionSpread", 4);
	if (flags & 0xc0) // emission rate manual | linear
	{
		const uint n = c.p < sz ? raw[c.p] : 0;
		pcr_u8 (&c, "emissionRateKeyCount", 4);
		pcr_curve (&c, "emissionRate", n);
	}
	else
		pcr_float (&c, "emissionRate", 4);
	pcr_float (&c, "emissionRateJitter", 4);
	pcr_float (&c, "emissionRadiusScale", 4);
	if (flags & 0x300)
	{
		const uint n = c.p < sz ? raw[c.p] : 0;
		pcr_u8 (&c, "emissionRadiusKeyCount", 4);
		pcr_curve (&c, "emissionRadius", n);
	}
	else
		pcr_float (&c, "emissionRadius", 4);
	if (flags & 0xc00)
	{
		const uint n = c.p < sz ? raw[c.p] : 0;
		pcr_u8 (&c, "initialVelocityKeyCount", 4);
		pcr_curve (&c, "initialVelocity", n);
	}
	else
		pcr_float (&c, "initialVelocity", 4);
	static const char *const fl[] = { "initialVelocityJitter", "drag", "dragJitter", "maxVelocity", "scaleThreshold1",
		"scaleThreshold2", "minScaleFactor1", "minScaleFactor2", "scaleSize", "sizeJitter", "alphaThreshold1",
		"alphaThreshold2", "alphaJitter", "lifetimeJitter" };
	for (uint i = 0; i < sizeof (fl) / sizeof (*fl); i++)
		pcr_float (&c, fl[i], 4);
	pcr_s16 (&c, "rotSpeedMin", 2);
	pcr_s16 (&c, "rotSpeedJitter", 2);
	pcr_s16 (&c, "rotAngle", 2);
	pcr_s16 (&c, "baseLifetime", 2);
	pcr_s16 (&c, "maxFrame", 2);
	pcr_u8 (&c, "freeParticleMotionTime", 1);
	pcr_u8 (&c, "unused", 1);
	pcr_u8 (&c, "maxPasses", 1);
	pcr_u8 (&c, "blendFactor", 1);
	pcr_u8 (&c, "zMode", 2);
	if (flags & 0x10000)
		pcr_vec (&c, "gravityFieldAccel");
	if (flags & 0x20000)
		pcr_vec (&c, "airFieldVelocity");
	if (flags & 0x40000)
	{
		pcr_vec (&c, "vortexCenter");
		pcr_float (&c, "vortexRotationSpeed", 4);
		pcr_float (&c, "vortexStrength", 4);
		pcr_float (&c, "vortexFalloffFactor", 4);
		pcr_float (&c, "vortexFalloffDivisor", 4);
	}
	if (flags & 0x80000)
	{
		pcr_vec (&c, "dampedNewtonFieldDir");
		pcr_float (&c, "dampedNewtonFieldStrength", 4);
	}
	if (flags & 0x100000)
	{
		pcr_vec (&c, "newtonFieldDir");
		pcr_float (&c, "newtonFieldStrength", 4);
	}
	if (flags & 0x200000)
	{
		pcr_vec (&c, "solidFieldForceMultiplier");
		pcr_u8 (&c, "solidFieldGridScale", 1);
		pcr_u8 (&c, "solidFieldType", 1);
		pcr_u8 (&c, "solidFieldSampleOffset", 2);
	}
	if (flags & 0x400000)
		pcr_float (&c, "jitterStrength", 4);
	if (flags & 0x800000)
	{
		pcr_vec (&c, "lineFieldAxis");
		pcr_float (&c, "lineFieldAxialForce", 4);
		pcr_float (&c, "lineFieldRadialForce", 4);
	}
	if (flags & 0x2000)
	{
		pcr_float (&c, "childScaleFactor", 4);
		pcr_float (&c, "childAlphaMultiplier", 4);
		pcr_float (&c, "childPosJitter", 4);
		if (pcr_need (&c, 4))
		{
			pcr_key (&c, "childColour");
			pcr_emit (&c, "[%u, %u, %u, %u]", raw[c.p], raw[c.p + 1], raw[c.p + 2], raw[c.p + 3]);
			c.p += 4;
		}
		pcr_u8 (&c, "child124", 1);
		pcr_u8 (&c, "childSpawnInterval", 3);
	}
	if (c.p != sz)
		c.ok = false; // the parameter block must end exactly at the end of the file
	pcr_emit (&c, "\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT PCR:%s -> JSON:%s\n", arg, dest);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT PCR:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
