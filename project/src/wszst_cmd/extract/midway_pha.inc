// Midway animation packs (".pha", members of TNA Impact! ".WAD" packs; big endian): u16 animation count @8, u32 end of the NUL-separated name table @0x0c (names start at 0x20),
// then at that offset one 36-byte record per animation {u32 start frame (cumulative), u32 flags (0x40000028), u32 0, float speed, float blend time, 3 x u32 0, u32 data offset};
// the last name is a "ZZ_DUMMY" placeholder. Animation k's payload is the range up to the next record's offset (the last one runs to the end of the file). Written as
// "<name>.pha.d/NNNN_<animation>.anm" (raw payloads, their internal key format is not decoded) plus "index.txt" listing name, start frame, speed, blend and size.
// One animation payload is a list of chunks {u16 tag, u16 a, u16 count, u16 size, ...}: tag 0x0800 is the rotation of one bone (u32 bone id, then `count` big-endian words of 10-10-10-2 bits:
// the three smaller quaternion components as offset-binary values scaled by 1/sqrt(2), the two low bits giving the index of the dropped (largest) component, which is recomputed);
// size = 4 * count + 12. Tags 0x0200/0x0280 hold u32 id and 8-byte entries of 4 x s16 (size = 8 * count + 12), the others are written as raw words. The chunk sizes add up to the payload
// size for 924 of the 930 animations of the TNA Impact! pack; the quaternions of all 61546 sampled words are unit length with the dropped component the largest, which is what validates the decoding.
static bool midway_anm_keys (sumo_buf_t *b, uint anim, const u8 *p, size_t n)
{
	size_t o = 0;
	while (o + 12 <= n)
	{
		const uint tag = rd_be16 (p + o), cnt = rd_be16 (p + o + 4), sz = rd_be16 (p + o + 6);
		if (sz < 8 || o + sz > n)
			return false;
		const uint id = sz >= 12 ? rd_be32 (p + o + 8) : 0;
		if (tag == 0x0800 && sz == 4 * cnt + 12)
			for (uint i = 0; i < cnt; i++)
			{
				const u32 w = rd_be32 (p + o + 12 + 4 * i);
				const uint dropped = w & 3;
				float q[4], s = 0.0f;
				uint k = 0;
				for (uint c = 0; c < 4; c++)
				{
					if (c == dropped)
						continue;
					q[c] = ((float)((w >> (22 - 10 * k)) & 1023) - 512.0f) / 511.0f * 0.70710678f;
					s += q[c] * q[c];
					k++;
				}
				q[dropped] = s < 1.0f ? sqrtf (1.0f - s) : 0.0f;
				sumo_printf (b, "%u,rot,%u,%u,%g,%g,%g,%g\n", anim, id, i, q[0], q[1], q[2], q[3]);
			}
		else if ((tag == 0x0200 || tag == 0x0280) && sz == 8 * cnt + 12)
			for (uint i = 0; i < cnt; i++)
				sumo_printf (b, "%u,s16,%u,%u,%d,%d,%d,%d\n", anim, id, i, (s16)rd_be16 (p + o + 12 + 8 * i), (s16)rd_be16 (p + o + 14 + 8 * i), (s16)rd_be16 (p + o + 16 + 8 * i),
					(s16)rd_be16 (p + o + 18 + 8 * i));
		else
		{
			sumo_printf (b, "%u,raw%04x,%u,0", anim, tag, id);
			for (size_t k = 8; k + 4 <= sz && k < 8 + 4 * 24; k += 4)
				sumo_printf (b, ",%x", rd_be32 (p + o + k));
			sumo_printf (b, "\n");
		}
		o += sz;
	}
	return o == n;
}

static enumError extract_midway_pha_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".pha"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t n = sz > 0x40 ? rd_be16 (raw + 8) : 0, end = sz > 0x40 ? rd_be32 (raw + 0x0c) : 0;
	bool ok = n > 1 && end > 0x20 && end + n * 36 <= sz && !raw[end - 1];
	size_t first = ok ? rd_be32 (raw + end + 32) : 0;
	ok = ok && first >= end + n * 36 && first <= sz;
	for (size_t i = 1; ok && i < n; i++)
	{
		const size_t o0 = rd_be32 (raw + end + 36 * (i - 1) + 32), o1 = rd_be32 (raw + end + 36 * i + 32);
		ok = o1 >= o0 && o1 <= sz;
	}
	if (ok)
	{
		char *txt = CALLOC (n * 128 + 256, 1);
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MIDWAY-PHA:%s (%zu animations) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		size_t o = txt ? (size_t)sprintf (txt, "# name start_frame speed blend payload_bytes\n") : 0;
		sumo_buf_t kb = { 0, 0, 0, true };
		sumo_printf (&kb, "animation,type,id,index,v0,v1,v2,v3\n");
		uint keyed = 0;
		const u8 *nm = raw + 0x20;
		err = txt ? ERR_OK : ERR_NOTHING_TO_DO;
		for (size_t i = 0; txt && i < n && !err; i++)
		{
			const u8 *r = raw + end + 36 * i;
			const size_t a = rd_be32 (r + 32), b = i + 1 < n ? rd_be32 (raw + end + 36 * (i + 1) + 32) : sz;
			char nm1[96], name[160];
			if (nm >= raw + end)
				break;
			snprintf (nm1, sizeof (nm1), "%.80s", (ccp)nm);
			nm += strlen ((ccp)nm) + 1;
			uint sp = rd_be32 (r + 12), bl = rd_be32 (r + 16);
			float fs, fb;
			memcpy (&fs, &sp, 4);
			memcpy (&fb, &bl, 4);
			o += (size_t)sprintf (txt + o, "%s %u %.4f %.4f %zu\n", nm1, rd_be32 (r), fs, fb, b - a);
			for (char *c = nm1; *c; c++)
				if (!isalnum ((u8)*c) && *c != '_' && *c != '-')
					*c = '_';
			snprintf (name, sizeof (name), "%04zu_%s.anm", i, nm1);
			if (b > a)
			{
				const size_t kmark = kb.n;
				if (midway_anm_keys (&kb, (uint)i, raw + a, b - a) && kb.n > kmark)
					keyed++;
				else if (kb.p)
				{
					kb.n = kmark;
					kb.p[kb.n] = 0;
				}
				err = shade_write (arg, dest, basedir, name, raw + a, (uint)(b - a));
			}
		}
		if (!err)
			err = shade_write (arg, dest, basedir, "index.txt", (u8 *)txt, (uint)o);
		if (!err && keyed && kb.ok && kb.n < 0xffffffffu)
			err = shade_write (arg, dest, basedir, "keys.csv", (u8 *)kb.p, (uint)kb.n);
		FREE (kb.p);
		FREE (txt);
	}
	FREE (raw);
	return err;
}

// Midway physics hulls (".phm", members of TNA Impact! ".WAD" packs; big endian): a header whose float bounding box (min at 0x20, max at 0x30, w = 0) is followed at 0x44 by
// {u32 vertex count, u32 vertex offset, u32 0, u32 descriptor offset, u32 ~index count, u32 index offset}. Vertices are 20-byte records {u16 ?, s16 x, y, z, ...} running to the end of
// the file; the u16 index list sits right before them. The positions are fixed-point with a per-file scale (1/1023, 1/255, 1/32767 ... of the header box), recovered here by fitting the
// extent of the vertices to the header box (it matches to 4 digits on LOCKERRM, VEGAS and CHAIR). The index list mixes triangles with quads stored as (b, c, a, d) for the four consecutive
// vertices a..a+3, which become triangles (a, b, c) and (b, c, d). Written as "<name>.phm.d/mesh.obj".
static enumError extract_midway_phm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".phm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t nv = sz > 0x60 ? rd_be32 (raw + 0x44) : 0, vo = sz > 0x60 ? rd_be32 (raw + 0x48) : 0, io = sz > 0x60 ? rd_be32 (raw + 0x58) : 0;
	if (sz > 0x60 && nv > 3 && nv < 0x100000 && vo + 20 * nv <= sz && io >= 0x40 && io + 12 <= vo && !((vo - io) & 1))
	{
		const size_t ni = (vo - io) / 2;
		float hb[6];
		bool ok = true;
		for (uint k = 0; k < 6; k++)
		{
			const u32 w = rd_be32 (raw + 0x20 + 16 * (k / 3) + 4 * (k % 3));
			memcpy (hb + k, &w, 4);
			ok = ok && isfinite (hb[k]);
		}
		int lo[3] = { 32767, 32767, 32767 }, hi[3] = { -32768, -32768, -32768 };
		for (size_t i = 0; ok && i < nv; i++)
			for (uint k = 0; k < 3; k++)
			{
				const int c = (s16)rd_be16 (raw + vo + 4 + 20 * i + 2 * k);
				lo[k] = c < lo[k] ? c : lo[k];
				hi[k] = c > hi[k] ? c : hi[k];
			}
		double scale = 0.0;
		for (uint k = 0; ok && k < 3; k++)
			if (hi[k] > lo[k] && (hb[3 + k] - hb[k]) / (hi[k] - lo[k]) > scale)
				scale = (hb[3 + k] - hb[k]) / (hi[k] - lo[k]);
		ok = ok && scale > 0.0;
		for (size_t i = 0; ok && i < ni; i++)
			ok = rd_be16 (raw + io + 2 * i) < nv;
		if (ok)
		{
			sumo_buf_t b = { 0, 0, 0, true };
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT MIDWAY-PHM:%s (%zu vertices) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nv, dest);
			for (size_t i = 0; i < nv; i++)
			{
				const u8 *v = raw + vo + 4 + 20 * i;
				sumo_printf (&b, "v %g %g %g\n", (s16)rd_be16 (v) * scale, (s16)rd_be16 (v + 2) * scale, (s16)rd_be16 (v + 4) * scale);
			}
			for (size_t p = 0; p + 3 <= ni;)
			{
				const size_t i0 = rd_be16 (raw + io + 2 * p), i1 = rd_be16 (raw + io + 2 * p + 2), i2 = rd_be16 (raw + io + 2 * p + 4);
				if (p + 4 <= ni && i0 == i2 + 1 && i1 == i2 + 2 && rd_be16 (raw + io + 2 * p + 6) == i2 + 3)
				{
					sumo_printf (&b, "f %zu %zu %zu\nf %zu %zu %zu\n", i0 + 1, i1 + 1, i2 + 1, i1 + 1, i2 + 1, i2 + 4);
					p += 4;
				}
				else
				{
					if (i0 != i1 && i1 != i2 && i0 != i2)
						sumo_printf (&b, "f %zu %zu %zu\n", i0 + 1, i1 + 1, i2 + 1);
					p += 3;
				}
			}
			err = b.ok && b.p && b.n < 0xffffffffu ? shade_write (arg, dest, basedir, "mesh.obj", (u8 *)b.p, (uint)b.n) : ERR_NOTHING_TO_DO;
			FREE (b.p);
		}
	}
	FREE (raw);
	return err;
}
