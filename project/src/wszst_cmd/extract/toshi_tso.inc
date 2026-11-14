// Blue Tongue Toshi scripted sequences (".tso", Nickelodeon Barnyard Wii; big endian): cut-scene scripts, layout after OpenBarnyard (OpenBarnyard/Source/Units/ASSeq.h/.cpp). A
// 24-byte header {u32 string relocation offset, size (bytes), strings offset, u32 pointer relocation offset, size, pointers offset}; all other offsets are relative to the end of the
// header ("base"). Pointer words listed in the relocation tables hold offsets into the string area or the pointer area. The script at base is {char *name, s32 ?, s32 track count,
// s32 *track actor ids, u32 *track offsets (relative to base)}; every track is a list of instructions {s32 type, u32 flags, s32 flag, ...} of fixed size per type (0 actor event 0x1c,
// 1 actor event 0x14, 2 actor event 0x1c, 3 wait 0x14 {wait flag, float duration}, 4 animation 0x1c, 5 voice 0x18 {name, sound, type}, 6 unused 0x18, 7 fade 0x14 {fade out, float
// duration}, 8 actor flag 0x10, 9 set flag 0x10, 10 pause 0xc, 11 resume 0xc, 12 command 0x1c {command, 3 arguments}, 13 end of track). Written as a YAML-style listing
// "<file>.yaml"; words of the actor events and animations (whose structs the decompilation does not give) are listed raw, strings resolved through the relocation table.
static const uint tso_sizes[14] = { 0x1c, 0x14, 0x1c, 0x14, 0x1c, 0x18, 0x18, 0x14, 0x10, 0x10, 0x0c, 0x0c, 0x1c, 0x04 };
static ccp const tso_names[14] = { "actor-event0", "actor-event1", "actor-event2", "wait", "animation", "voice", "unk6", "fade", "actor-flag", "set-flag", "pause", "resume", "command", "end" };

typedef struct
{
	const u8 *b;	 // file
	size_t n;
	size_t base;	 // 0x18
	const u8 *strset; // 1 byte per word offset: 1 = string pointer
} tso_ctx_t;

static void tso_word (const tso_ctx_t *c, FILE *f, size_t off, u32 strbase)
{
	const u32 w = rd_be32 (c->b + off);
	if (c->strset[(off - c->base) / 4])
	{
		const size_t so = c->base + strbase + w;
		size_t k = 0;
		while (so + k < c->n && c->b[so + k])
			k++;
		if (so + k < c->n && k < 1024)
		{
			fprintf (f, "\"%s\"", (ccp)c->b + so);
			return;
		}
	}
	float fl;
	memcpy (&fl, &w, 4);
	if (w >= 0x30000000u && w < 0x50000000u)
		fprintf (f, "%.9g", fl);
	else if (w >= 0xb0000000u && w < 0xd0000000u)
		fprintf (f, "%.9g", fl);
	else
		fprintf (f, "%d", (int)w);
}

static enumError extract_toshi_tso_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".tso"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *strset = 0;
	do
	{
		if (sz < 0x18 + 20)
			break;
		const size_t base = 0x18, body = sz - base;
		const u32 sro = rd_be32 (raw), srs = rd_be32 (raw + 4), sto = rd_be32 (raw + 8), pro = rd_be32 (raw + 12), prs = rd_be32 (raw + 16), pto = rd_be32 (raw + 20);
		if ((u64)sro + srs > body || (u64)pro + prs > body || sto > body || pto > body || (srs & 3) || (prs & 3))
			break;
		strset = CALLOC (body / 4 + 2, 1);
		if (!strset)
			break;
		bool ok = true;
		for (u32 i = 0; i < srs / 4 && ok; i++)
		{
			const u32 r = rd_be32 (raw + base + sro + 4 * i);
			if ((u64)r + 4 > body || (r & 3))
				ok = false;
			else
				strset[r / 4] = 1;
		}
		if (!ok)
			break;
		// pointer relocations are applied by the engine: resolve our own copy of the values
		const u32 ntr = rd_be32 (raw + base + 8);
		if (body < 20 + 4 || ntr == 0 || ntr > 4096)
			break;
		// script fields are at base: name(string), unk, tracks, actors(ptr), offsets(ptr)
		const u32 nt = ntr, actp = rd_be32 (raw + base + 12) + pto, offp = rd_be32 (raw + base + 16) + pto;
		if ((u64)actp + 4ull * nt > body || (u64)offp + 4ull * nt > body)
			break;
		char dest[PATH_MAX];
		if (opt_dest)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".yaml", false);
		else
			snprintf (dest, sizeof (dest), "%s.yaml", arg);
		// first pass validates every track, the second writes
		for (int pass = 0; pass < 2 && ok; pass++)
		{
			File_t F;
			FILE *f = 0;
			bool real = pass == 1 && !testmode;
			if (pass == 1)
			{
				err = ERR_OK;
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT TOSHI-TSO:%s (%u tracks) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nt, dest);
				if (!real)
					break;
				err = CreateFileOpt (&F, true, dest, false, arg);
				f = F.f;
				if (!f)
					break;
			}
			tso_ctx_t c = { raw, sz, base, strset };
			if (f)
			{
				const u32 so = base + sto + rd_be32 (raw + base);
				fprintf (f, "# Blue Tongue Toshi scripted sequence, decoded from %s\n", arg);
				fprintf (f, "name: \"%s\"\n", so < sz ? (ccp)raw + so : "?");
				fprintf (f, "tracks:\n");
			}
			for (u32 t = 0; t < nt && ok; t++)
			{
				const int actor = (int)rd_be32 (raw + base + actp + 4 * t);
				size_t o = base + rd_be32 (raw + base + offp + 4 * t);
				if (f)
					fprintf (f, "  - actor: %d\n    instructions:\n", actor);
				for (uint k = 0; ok; k++)
				{
					if (o + 12 > sz || k > 100000)
					{
						ok = false;
						break;
					}
					const u32 ty = rd_be32 (raw + o);
					if (ty > 13)
					{
						ok = false;
						break;
					}
					if (ty == 13)
					{
						if (f)
							fprintf (f, "      - { type: end }\n");
						break;
					}
					const size_t is = tso_sizes[ty];
					if (o + is > sz)
					{
						ok = false;
						break;
					}
					if (f)
					{
						fprintf (f, "      - { type: %s, flags: %u, flag: %d", tso_names[ty], rd_be32 (raw + o + 4), (int)rd_be32 (raw + o + 8));
						if (is > 12)
						{
							fputs (", args: [", f);
							for (size_t w = 12; w < is; w += 4)
							{
								if (w > 12)
									fputs (", ", f);
								tso_word (&c, f, o + w, sto);
							}
							fputc (']', f);
						}
						fputs (" }\n", f);
					}
					o += is;
				}
			}
			if (f)
			{
				if (ferror (f))
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing YAML failed: %s\n", dest);
				ResetFile (&F, opt_preserve);
			}
		}
		if (!ok && err == ERR_NOTHING_TO_DO)
			break;
	} while (0);
	FREE (strset);
	FREE (raw);
	return err;
}
