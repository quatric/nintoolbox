#pragma push_macro("calloc")
#pragma push_macro("free")
#undef calloc
#undef free
// Pikmin 1 "tekipara/*.bin" enemy parameter files (TekiParameters::read in the decompilation): BE {s32 version,
// 8 x 4 character IDs, the int parameters, the float parameters}. Names come from TekiIntParams / TekiFloatParams
// (20 ints, 50 floats). Enemy specific subclasses append extra values to both lists; the split between the two lists
// is found by looking for the first float-like word (Life) after the 20 named ints, and extras are written as
// "extra_int_N" / "extra_float_N". Version 8 files store floats 0..39 and 50.. only; older versions are not written; output is
// "<name>.bin.json".
static const char *const teki_int_names[20] = {
	"CorpseType",
	"ShadowType",
	"CullingType",
	"SpawnType",
	"SpawnCount",
	"AnimationType",
	"CarcassSize",
	"ModelType",
	"LightType",
	"SwallowCount",
	"FlickPikiCount1",
	"FlickPikiCount2",
	"FlickPikiCount3",
	"FlickDamageCount1",
	"FlickDamageCount2",
	"FlickDamageCount3",
	"FlickDamageCount4",
	"SpawnPelletScaleOff",
	"SpawnPelletForward",
	"RESERVED1" };
static const char *const teki_float_names[50] = {
	"Life",
	"Scale",
	"Weight",
	"WalkVelocity",
	"RunVelocity",
	"TurnVelocity",
	"VisibleRange",
	"VisibleAngle",
	"AttackableRange",
	"AttackableAngle",
	"AttackRange",
	"AttackHitRange",
	"AttackPower",
	"AttackWaitPeriod",
	"UnguardedPeriod",
	"DangerTerritoryRange",
	"SafetyTerritoryRange",
	"LifeGaugeOffset",
	"ShadowSize",
	"RippleScale",
	"CorpseSize",
	"CorpseHeight",
	"SpawnDistance",
	"SpawnHeight",
	"SpawnVelocity",
	"LifePeriod",
	"MessageRange",
	"WarnPeriod",
	"VisibleHeight",
	"FlightHeight",
	"DamageMotionPeriod",
	"DamageMotionAmplitude",
	"FlickProbability",
	"UpperFlickPower",
	"LowerFlickPower",
	"UpperAttackPower",
	"LowerAttackPower",
	"LifeRecoverRate",
	"TraceAngle",
	"SpawnPelletVelocityHoriz",
	"SpawnPelletVelocityVert",
	"CorpseVelocityHoriz",
	"CorpseVelocityVert",
	"BombDamageRate",
	"CollisionRadius",
	"LowerRange",
	"RESERVED4",
	"RESERVED3",
	"RESERVED2",
	"RESERVED1" };

static bool teki_small_int (u32 w)
{
	const s32 v = (s32)w;
	return v >= -4096 && v <= 0x00ffffff;
}

static enumError extract_pikmin_teki_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin") || !strstr (arg, "tekipara/"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const int ver = sz >= 4 ? (int)rd_be32 (raw) : 0;
	const uint nw = (uint)(sz / 4);
	if (sz % 4 || ver < 8 || ver > 11 || nw < 9 + (ver == 8 ? 60 : 70))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *w = raw + 36;
	const uint nparam = nw - 9;
	// version 8 stores floats 0..39 and 50.. only (the four-character range 40..49 keeps its defaults)
	const uint nfmin = ver == 8 ? 40 : 50;
	// number of int parameters: 20 named plus the leading extras that do not look like floats
	uint ni = 20;
	while (ni < nparam - nfmin && teki_small_int (rd_be32 (w + 4 * ni)))
		ni++;
	if (ni > nparam - nfmin)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = 4096 + (size_t)nparam * 64;
	char *out = (char *)calloc (1, cap);
	size_t n = 0;
	if (!out)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	n += (size_t)snprintf (out + n, cap - n, "{\n  \"version\": %d,\n  \"ids\": [", ver);
	for (int i = 0; i < 8; i++)
		n += (size_t)snprintf (out + n, cap - n, "%s\"%.4s\"", i ? ", " : "", (ccp)raw + 4 + 4 * i);
	n += (size_t)snprintf (out + n, cap - n, "],\n  \"int_params\": {");
	for (uint i = 0; i < ni; i++)
	{
		char nm[32];
		if (i < 20)
			snprintf (nm, sizeof (nm), "%s", teki_int_names[i]);
		else
			snprintf (nm, sizeof (nm), "extra_int_%u", i - 20);
		n += (size_t)snprintf (out + n, cap - n, "%s\n    \"%s\": %d", i ? "," : "", nm, (s32)rd_be32 (w + 4 * i));
	}
	n += (size_t)snprintf (out + n, cap - n, "\n  },\n  \"float_params\": {");
	for (uint i = 0; i < nparam - ni; i++)
	{
		char nm[40];
		const uint fi = ver == 8 && i >= 40 ? i + 10 : i;
		if (fi < 50)
			snprintf (nm, sizeof (nm), "%s", teki_float_names[fi]);
		else
			snprintf (nm, sizeof (nm), "extra_float_%u", fi - 50);
		const u32 bits = rd_be32 (w + 4 * (ni + i));
		float f;
		memcpy (&f, &bits, 4);
		n += (size_t)snprintf (out + n, cap - n, "%s\n    \"%s\": %.9g", i ? "," : "", nm, (double)f);
	}
	n += (size_t)snprintf (out + n, cap - n, "\n  }\n}\n");
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.json", arg);
	enumError err = ERR_OK;
	if (testmode)
		fprintf (stdlog, "WOULD EXTRACT TEKI:%s -> JSON:%s\n", arg, dest);
	else
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (out, 1, n, F.f) != n)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)n, dest);
		ResetFile (&F, opt_preserve);
		if (verbose >= 0 && !err)
			fprintf (stdlog, "%sEXTRACT TEKI:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	free (out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
