// Sega "Ninja GC" model files (".gno" and the sibling ".gnm" / ".gnj" / ".gnt" / ".gnv" / ".gnf" / ".gnh" / ".gnd" with the same chunk layout; Ghost Squad,
// Super Monkey Ball: Banana Blitz, Sonic Unleashed, ...; Wii, big-endian payload): chunks {char tag[4], u32 LE size (without the 8 byte head), body}:
// "NGIF" (u32 0x18 LE, u32 chunk count, 0x20, sizes), "NGTL" (texture list: 20 byte records + file names), "NGOB" / "NGMO" (object data), "NOF0"
// (u32 BE count, u32 0, then the offsets, relative to 0x20, of every pointer in the file), "NFN0" (file name), "NEND". Pointers are relative to 0x20.
// Findings used here (derived from the files, only the geometry is decoded): vertex arrays are described by 0x38 byte groups of 8 byte slots {u16 kind,
// u16 count, u32 ptr}: slot 0 positions (3 floats), slot 1 normals (3 s8, 1/64), slot 3 texcoords (2 s16, 1/128); and GX display lists start with the CP
// writes "08 50 <VCD lo>" "08 60 <VCD hi>" followed by an XF write and draw commands (0x80 quads, 0x90 triangles, 0x98 strip, 0xa0 fan) whose vertices
// are INDEX16 attributes (optional matrix index bytes first) into those arrays. A display list uses the first array group that has the attributes it needs
// and is large enough; node transforms, skinning and materials are not applied. Written as "model.obj" (one object per display list).
typedef struct gno_group_t { uint cnt[8]; u32 ptr[8]; } gno_group_t;

static enumError extract_gno_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gno") && !is_ext (arg, ".gnm") && !is_ext (arg, ".gnj") && !is_ext (arg, ".gnt") && !is_ext (arg, ".gnv") && !is_ext (arg, ".gnf")
		&& !is_ext (arg, ".gnh") && !is_ext (arg, ".gnd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	char *obj = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x80 || memcmp (raw, "NGIF", 4) || memcmp (raw + 0x20, "NGTL", 4) && memcmp (raw + 0x20, "NGOB", 4))
		goto done;
	{
		// locate NOF0
		size_t p = 0x20, nof = 0;
		while (p + 8 <= sz)
		{
			const u32 cs = rd_le32 (raw + p + 4);
			if (!memcmp (raw + p, "NOF0", 4))
			{
				nof = p;
				break;
			}
			if (p + 8 + (u64)cs > sz || !(raw[p] == 'N'))
				break;
			p += 8 + (size_t)cs;
		}
		if (!nof || nof + 16 > sz)
			goto done;
		const u32 np = rd_be32 (raw + nof + 8);
		if (!np || np > 100000 || nof + 16 + 4 * (u64)np > sz)
			goto done;
		bool *isptr = CALLOC (sz + 4, 1);
		u32 *tg = CALLOC (np, sizeof (u32));
		u32 *pl = CALLOC (np, sizeof (u32));
		gno_group_t *groups = CALLOC (256, sizeof (gno_group_t));
		u32 *gpos = CALLOC (256, sizeof (u32));
		uint ng = 0;
		size_t cap = 1 << 16, used = 0;
		obj = MALLOC (cap);
		bool ok = obj && isptr && tg && pl && groups && gpos;
		uint nt = 0;
		for (uint i = 0; ok && i < np; i++)
		{
			const u32 o = 0x20u + rd_be32 (raw + nof + 16 + 4 * (size_t)i);
			if (o + 4 > nof)
				continue;
			const u32 v = 0x20u + rd_be32 (raw + o);
			if (v >= nof)
				continue;
			isptr[o] = true;
			pl[nt] = o, tg[nt] = v, nt++;
		}
		// attribute entries: a pointer whose preceding u16 pair is {kind, count}; group them on the 8 byte slot grid
		for (uint i = 0; ok && i < nt; i++)
		{
			const u32 o = pl[i];
			if (o < 0x40 + 0x38)
				continue;
			const uint kind = rd_be16 (raw + o - 4), cnt = rd_be16 (raw + o - 2);
			if (kind < 1 || kind > 3 || !cnt || tg[i] < 0x40 || tg[i] >= nof)
				continue;
			// slot index from the kind: 1 = positions (slot 0), 3 = normals (slot 1), 2 = texcoords (slot 3)
			const uint slot = kind == 1 ? 0 : kind == 3 ? 1 : 3;
			const u32 gstart = o - 4 - 8 * slot;
			uint g = 0;
			for (; g < ng; g++)
				if (gpos[g] == gstart)
					break;
			if (g == ng)
			{
				if (ng == 256)
					continue;
				gpos[ng++] = gstart;
				memset (&groups[g], 0, sizeof (groups[g]));
			}
			groups[g].cnt[slot] = cnt, groups[g].ptr[slot] = tg[i];
		}
		// groups keep the file order of their position slot
		for (uint a = 0; a < ng; a++)
			for (uint b = a + 1; b < ng; b++)
				if (gpos[b] < gpos[a])
				{
					const u32 tp = gpos[a];
					gpos[a] = gpos[b];
					gpos[b] = tp;
					const gno_group_t tgp = groups[a];
					groups[a] = groups[b];
					groups[b] = tgp;
				}
#define GNO_EMIT(...) \
	do { \
		for (;;) { \
			const int w_ = snprintf (obj + used, cap - used, __VA_ARGS__); \
			if (w_ >= 0 && (size_t)w_ < cap - used) { used += (size_t)w_; break; } \
			char *nb_ = REALLOC (obj, cap * 2); \
			if (!nb_) { ok = false; break; } \
			obj = nb_; cap *= 2; \
		} \
	} while (0)
		uint ndl = 0, ntri = 0, vbase = 1;
		for (uint i = 0; ok && i < nt && ng; i++)
		{
			const u32 s = tg[i];
			if (s + 12 > nof || raw[s] != 0x08 || raw[s + 1] != 0x50 || raw[s + 6] != 0x08 || raw[s + 7] != 0x60)
				continue;
			const u32 vlo = rd_be32 (raw + s + 2), vhi = rd_be32 (raw + s + 8);
			// vertex layout: PMIDX (bit 0, u8), TnMIDX (bits 1..8, u8 each), POS (bits 9-10), NRM (11-12), COL0 (13-14), COL1 (15-16), TEX0 (hi bits 0-1)
			uint vsz = (vlo & 1) + __builtin_popcount ((vlo >> 1) & 0xff);
			const uint dpos = (vlo >> 9) & 3, dnrm = (vlo >> 11) & 3, dc0 = (vlo >> 13) & 3, dc1 = (vlo >> 15) & 3, dtx = vhi & 3;
			if (vhi >> 2 || dpos != 3 || dnrm == 1 || dnrm == 2 || dc0 == 2 || dc1 || dc0 == 1 || dtx == 1 || dtx == 2)
				continue;
			vsz += 2 + (dnrm ? 2 : 0) + (dc0 ? 2 : 0) + (dtx ? 2 : 0);
			// walk the commands
			size_t q = s + 12;
			bool drew = false;
			uint tri_here = 0;
			size_t obj_mark = used;
			// pick the group once the maximum indices are known: parse twice
			uint maxp = 0, maxn = 0, maxt = 0;
			for (int pass = 0; pass < 2 && ok; pass++)
			{
				uint gsel = 0;
				if (pass == 1)
				{
					for (; gsel < ng; gsel++)
						if (groups[gsel].cnt[0] > maxp && (!dnrm || groups[gsel].cnt[1] > maxn) && (!dtx || groups[gsel].cnt[3] > maxt))
							break;
					if (gsel == ng)
						break;
					GNO_EMIT ("o dl%u\n", ndl);
				}
				q = s + 12;
				drew = false;
				uint vstart = vbase, ntv = 0;
				while (ok && q < nof)
				{
					const u8 c = raw[q];
					if (c == 0x00 && !drew)
					{
						q++;
						continue;
					}
					if (c == 0x08)
						q += 6;
					else if (c == 0x10 && q + 5 <= nof)
						q += 5 + 4 * ((size_t)rd_be16 (raw + q + 1) + 1);
					else if (c >= 0x80 && c <= 0xbf)
					{
						const uint prim = c & 0xf8, n = rd_be16 (raw + q + 1);
						q += 3;
						if (!n || q + (size_t)n * vsz > nof || (prim != 0x80 && prim != 0x90 && prim != 0x98 && prim != 0xa0))
						{
							ok = ok && true;
							q = nof;
							break;
						}
						drew = true;
						const u8 *vp = raw + q;
						for (uint k = 0; k < n; k++)
						{
							const u8 *v = vp + (size_t)k * vsz;
							uint off = (vlo & 1) + __builtin_popcount ((vlo >> 1) & 0xff);
							const uint ip = rd_be16 (v + off);
							off += 2;
							uint in = 0, it = 0;
							if (dnrm)
								in = rd_be16 (v + off), off += 2;
							if (dc0)
								off += 2;
							if (dtx)
								it = rd_be16 (v + off);
							if (pass == 0)
							{
								if (ip > maxp)
									maxp = ip;
								if (dnrm && in > maxn)
									maxn = in;
								if (dtx && it > maxt)
									maxt = it;
							}
							else
							{
								const gno_group_t *G = &groups[gsel];
								float f[3] = { 0 };
								const u8 *pp = raw + G->ptr[0] + 12 * (size_t)ip;
								for (uint a = 0; a < 3; a++)
								{
									const u32 b = rd_be32 (pp + 4 * a);
									memcpy (&f[a], &b, 4);
									if (!isfinite (f[a]))
										f[a] = 0;
								}
								GNO_EMIT ("v %.5f %.5f %.5f\n", f[0], f[1], f[2]);
								if (dtx)
								{
									const u8 *tp = raw + G->ptr[3] + 4 * (size_t)it;
									GNO_EMIT ("vt %.5f %.5f\n", (short)rd_be16 (tp) / 128.0, 1.0 - (short)rd_be16 (tp + 2) / 128.0);
								}
								if (dnrm)
								{
									const u8 *np2 = raw + G->ptr[1] + 3 * (size_t)in;
									GNO_EMIT ("vn %.4f %.4f %.4f\n", (signed char)np2[0] / 64.0, (signed char)np2[1] / 64.0, (signed char)np2[2] / 64.0);
								}
							}
						}
						if (pass == 1)
						{
							// faces of this draw command (vertex numbers are vstart + ntv + k)
							for (uint k = 0; k < n; k++)
							{
								uint a = 0, b = 0, cc = 0;
								bool emit = false;
								if (prim == 0x90 && k % 3 == 2)
									a = k - 2, b = k - 1, cc = k, emit = true;
								else if (prim == 0x98 && k >= 2)
									a = (k & 1) ? k - 1 : k - 2, b = (k & 1) ? k - 2 : k - 1, cc = k, emit = true;
								else if (prim == 0xa0 && k >= 2)
									a = 0, b = k - 1, cc = k, emit = true;
								else if (prim == 0x80 && k % 4 == 3)
								{
									const uint bs = vstart + ntv + k - 3;
									GNO_EMIT ("f %u %u %u\nf %u %u %u\n", bs, bs + 1, bs + 2, bs, bs + 2, bs + 3);
									tri_here += 2;
								}
								if (emit)
								{
									const uint bs = vstart + ntv;
									GNO_EMIT ("f %u %u %u\n", bs + a, bs + b, bs + cc);
									tri_here++;
								}
							}
						}
						ntv += n;
						(void)vstart;
						q += (size_t)n * vsz;
					}
					else
						break;
				}
				if (pass == 1)
				{
					vbase += ntv;
					(void)obj_mark;
				}
			}
			if (tri_here)
			{
				ndl++;
				ntri += tri_here;
			}
		}
		if (ok && ntri)
		{
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT GNO:%s (%u display lists, %u triangles) -> %s/model.obj\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ndl, ntri, dest);
			err = ERR_OK;
			if (!testmode)
				err = shade_write (arg, dest, basedir, "model.obj", (u8 *)obj, (uint)used);
		}
		FREE (isptr);
		FREE (tg);
		FREE (pl);
		FREE (groups);
		FREE (gpos);
	}
done:
	FREE (obj);
	FREE (raw);
	return err;
}
