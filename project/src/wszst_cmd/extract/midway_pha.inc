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
