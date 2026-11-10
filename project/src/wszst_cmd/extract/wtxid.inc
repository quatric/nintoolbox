// "WTXID" object image (Cook Wars / Cooking Party / Fashion Idol / Petz Rescue: Geom/*.wtxg models, Anim/*.wtxa animations; any extension).
// Magic "WTXID" + kind byte ('g' geometry, 'a' animation), BE u16 version 2, flags; 0x80 byte header table of u32 BE {offset/count} pairs; the body is a
// serialised C++ object heap: class names (CClumpDef, CItemSkinMeshDef, CMtlStdDef, ...) and instance names (skeleton bones "CSBip Spine", materials, cameras)
// stored as NUL terminated strings with a length prefix, interleaved with big endian pointers / floats whose layout is the game's own. The object data itself is not decoded;
// the string inventory is written as "<file>.txt" ("offset<TAB>string").
// geometry of the "CItemSkinMeshDef" objects: the body holds length prefixed arrays (u32 BE count first), found by validated scans in file order per mesh:
// uv array (count x {f32 BE u, v}), display list (u32 vertex count, u32 byte size = 4 * count + padding, GX cmd 0x90 (triangles), u16 vertex count, count x {u16 vertex index,
// u16 uv index}) and the vertex array (count x {f32 pos[3], f32 normal[3], u8 influence count n, n x {u16 bone index, f32 weight}}; weights sum to 1). The three arrays of a mesh
// follow each other in that order. Written as "<file>.obj" ("<file>.N.obj" for more meshes) plus "<file>[.N].skin.txt" ("index bone:weight ...").
static bool wtxid_vertices (const u8 *r, size_t sz, size_t p, uint *nv, size_t *end)
{
	if (p + 4 > sz)
		return false;
	const u32 n = rd_be32 (r + p);
	if (n < 3 || n > 200000)
		return false;
	size_t q = p + 4;
	for (u32 i = 0; i < n; i++)
	{
		if (q + 25 > sz)
			return false;
		const uint k = r[q + 24];
		if (!k || k > 8 || q + 25 + 6 * (size_t)k > sz)
			return false;
		float f[6], ws = 0;
		for (uint c = 0; c < 6; c++)
		{
			const u32 v = rd_be32 (r + q + 4 * c);
			memcpy (&f[c], &v, 4);
			if (!(f[c] > -1e4f && f[c] < 1e4f))
				return false;
		}
		for (uint e = 0; e < k; e++)
		{
			const u32 v = rd_be32 (r + q + 27 + 6 * e);
			float w;
			memcpy (&w, &v, 4);
			ws += w;
		}
		if (ws < 0.98f || ws > 1.02f)
			return false;
		q += 25 + 6 * (size_t)k;
	}
	*nv = n;
	*end = q;
	return true;
}

// bone name table: the length prefixed ({u32 BE len, chars}) names between the "CClumpDef" and the first "CItemSkinMeshDef" class strings (one per bone, interleaved with the
// bone objects); bone indices of the skin entries index this list (.wtxa files carry the same list right after the header words {frames, fps, bone count}).
static uint wtxid_bone_names (const u8 *r, size_t sz, size_t *offs, uint max)
{
	size_t c0 = 0, e0 = 0;
	for (size_t p = 0x80; p + 16 < sz; p++)
		if (!memcmp (r + p, "CClumpDef", 9) && !c0)
			c0 = p + 9;
		else if (c0 && !memcmp (r + p, "CItemSkinMeshDef", 16))
		{
			e0 = p;
			break;
		}
	uint n = 0;
	for (size_t i = c0; c0 && e0 && i + 8 < e0 && n < max;)
	{
		const u32 l = rd_be32 (r + i);
		bool ok = l >= 3 && l <= 40 && i + 4 + l <= e0 && ((r[i + 4] | 0x20) >= 'a' && (r[i + 4] | 0x20) <= 'z');
		for (u32 k = 0; ok && k < l; k++)
			ok = r[i + 4 + k] == ' ' || r[i + 4 + k] == '_' || (r[i + 4 + k] >= '0' && r[i + 4 + k] <= '9') || ((r[i + 4 + k] | 0x20) >= 'a' && (r[i + 4 + k] | 0x20) <= 'z');
		if (ok)
		{
			offs[n++] = i;
			i += 4 + l;
		}
		else
			i++;
	}
	return n;
}

static void wtxid_meshes (ccp arg, const u8 *r, size_t sz)
{
	size_t boff[256];
	const uint nbn = wtxid_bone_names (r, sz, boff, 256);
	size_t uvp = 0, dlp = 0;
	uint uvn = 0, dln = 0, nm = 0;
	for (size_t p = 0x80; p + 16 < sz; p++)
	{
		// display list
		{
			const u32 n = rd_be32 (r + p), bs = rd_be32 (r + p + 4);
			if (n >= 3 && n % 3 == 0 && n < 200000 && bs >= 4 * (u64)n && bs <= 4 * (u64)n + 64 && p + 11 + 4 * (size_t)n <= sz && r[p + 8] == 0x90 && rd_be16 (r + p + 9) == n)
			{
				if (n > dln)
				{
					dlp = p + 11;
					dln = n;
				}
				p += 10;
				continue;
			}
		}
		// uv array
		{
			const u32 n = rd_be32 (r + p);
			if (n >= 3 && n < 200000 && p + 4 + 8 * (size_t)n <= sz)
			{
				uint good = 0;
				for (u32 i = 0; i < n && i < 64; i++)
				{
					const u32 a = rd_be32 (r + p + 4 + 8 * (size_t)i), b2 = rd_be32 (r + p + 8 + 8 * (size_t)i);
					float u, v;
					memcpy (&u, &a, 4);
					memcpy (&v, &b2, 4);
					if (u > -16.f && u < 16.f && v > -16.f && v < 16.f && (a == 0 || (a & 0x7f800000) != 0))
						good++;
				}
				if (good >= (n < 64 ? n : 64) && n > uvn && (r[p + 4] == 0x3f || r[p + 4] == 0xbf || r[p + 4] == 0x3e || r[p + 4] == 0xbe || r[p + 4] == 0x3d || r[p + 4] == 0x00))
				{
					uvp = p + 4;
					uvn = n;
				}
			}
		}
		uint nv;
		size_t end;
		if (dlp && uvp && wtxid_vertices (r, sz, p, &nv, &end))
		{
			char dest[PATH_MAX], ext[32];
			snprintf (ext, sizeof (ext), nm ? ".%u.obj" : ".obj", nm);
			beside_source_dest_ext (dest, sizeof (dest), arg, ext);
			size_t cap = 64 + (size_t)nv * 200 + (size_t)uvn * 40 + (size_t)dln * 40;
			char *t = MALLOC (cap), *sk = MALLOC ((size_t)nv * 520 + 64);
			size_t tl = 0, sl = 0;
			size_t q = p + 4;
			for (uint i = 0; i < nv; i++)
			{
				float f[6];
				for (uint c = 0; c < 6; c++)
				{
					const u32 v = rd_be32 (r + q + 4 * c);
					memcpy (&f[c], &v, 4);
				}
				tl += snprintf (t + tl, cap - tl, "v %.6g %.6g %.6g\n", f[0], f[1], f[2]);
				sl += snprintf (sk + sl, 160, "%u", i);
				const uint k = r[q + 24];
				for (uint e = 0; e < k; e++)
				{
					const u32 v = rd_be32 (r + q + 27 + 6 * e);
					float w;
					memcpy (&w, &v, 4);
					const uint bi = rd_be16 (r + q + 25 + 6 * e);
					if (bi < nbn)
						sl += snprintf (sk + sl, 200, " %u(%.*s):%.4f", bi, (int)rd_be32 (r + boff[bi]), (ccp)r + boff[bi] + 4, w);
					else
						sl += snprintf (sk + sl, 200, " %u:%.4f", bi, w);
				}
				sk[sl++] = '\n';
				q += 25 + 6 * (size_t)k;
			}
			q = p + 4;
			for (uint i = 0; i < nv; i++)
			{
				float f[6];
				for (uint c = 0; c < 6; c++)
				{
					const u32 v = rd_be32 (r + q + 4 * c);
					memcpy (&f[c], &v, 4);
				}
				tl += snprintf (t + tl, cap - tl, "vn %.6g %.6g %.6g\n", f[3], f[4], f[5]);
				q += 25 + 6 * (size_t)r[q + 24];
			}
			for (uint i = 0; i < uvn; i++)
			{
				const u32 a = rd_be32 (r + uvp + 8 * (size_t)i), b2 = rd_be32 (r + uvp + 4 + 8 * (size_t)i);
				float u, v;
				memcpy (&u, &a, 4);
				memcpy (&v, &b2, 4);
				tl += snprintf (t + tl, cap - tl, "vt %.6g %.6g\n", u, 1.0f - v);
			}
			for (uint i = 0; i + 2 < dln; i += 3)
			{
				uint vi[3], ti[3];
				bool ok = true;
				for (uint c = 0; c < 3; c++)
				{
					vi[c] = rd_be16 (r + dlp + 4 * (i + c)) + 1;
					ti[c] = rd_be16 (r + dlp + 4 * (i + c) + 2) + 1;
					if (vi[c] > nv || ti[c] > uvn)
						ok = false;
				}
				if (ok && tl + 120 < cap)
					tl += snprintf (t + tl, cap - tl, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", vi[0], ti[0], vi[0], vi[1], ti[1], vi[1], vi[2], ti[2], vi[2]);
			}
			File_t F;
			enumError err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (t, 1, tl, F.f) != tl)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", tl, dest);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				snprintf (ext, sizeof (ext), nm ? ".%u.skin.txt" : ".skin.txt", nm);
				beside_source_dest_ext (dest, sizeof (dest), arg, ext);
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (F.f && fwrite (sk, 1, sl, F.f) != sl)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", sl, dest);
				ResetFile (&F, opt_preserve);
			}
			FREE (t);
			FREE (sk);
			nm++;
			p = end - 1;
			dlp = uvp = 0;
			dln = uvn = 0;
		}
	}
}

// .wtxa key data (layout derived from the data; every clip of the Cook Wars sample that parses tiles the file exactly): after the name list comes {u32 bone count, f32 per bone (the
// bone's rest translation component), u32 bone count} and then bone count (+ 1 extra node) blocks of {u32 N keys, N records}; a record is 8 words {u32 frame, f32 quat x y z w,
// f32 pos x y z} or 11 words with a trailing f32 scale x y z (chosen per block by backtracking so that the chain of blocks ends exactly at the end of the file; frames strictly increase
// inside a block). Block i belongs to name i (the extra last block is a node without name) -- inferred from the header floats / positions, not proven.
static bool wtxid_anim_chain (const u8 *r, size_t sz, size_t k, uint b, uint nb, uint *rwv)
{
	if (b >= nb)
		return k == sz || (b == nb && k + 8 > sz);
	if (k + 4 > sz)
		return false;
	const u32 n = rd_be32 (r + k);
	if (!n || n > 4096)
		return false;
	for (uint rw = 11; rw >= 8; rw -= 3)
	{
		if (k + 4 + (size_t)n * rw * 4 > sz)
			continue;
		bool inc = true;
		for (u32 j = 1; j < n && inc; j++)
			inc = rd_be32 (r + k + 4 + 4 * rw * j) > rd_be32 (r + k + 4 + 4 * rw * (j - 1));
		if (!inc)
			continue;
		rwv[b] = rw;
		if (wtxid_anim_chain (r, sz, k + 4 + (size_t)n * rw * 4, b + 1, nb, rwv))
			return true;
	}
	return false;
}

static size_t wtxid_anim_keys (const u8 *r, size_t sz, char *t, size_t cap)
{
	const uint nb = rd_be32 (r + 0x104);
	if (!nb || nb > 400)
		return 0;
	size_t offs[400], q0 = 0x108;
	for (uint i = 0; i < nb; i++)
	{
		const u32 l = q0 + 4 <= sz ? rd_be32 (r + q0) : 0;
		if (!l || l > 64 || q0 + 4 + l > sz)
			return 0;
		offs[i] = q0, q0 += 4 + l;
	}
	const size_t p = q0 + 4 + 4 * (size_t)nb + 4;	// count, header floats, count
	if (p >= sz)
		return 0;
	uint rwv[402];
	uint nbl = nb + 1;
	if (!wtxid_anim_chain (r, sz, p, 0, nbl, rwv))
	{
		nbl = nb;
		if (!wtxid_anim_chain (r, sz, p, 0, nbl, rwv))
			return 0;
	}
	size_t tl = 0, k = p;
	for (uint b = 0; b < nbl; b++)
	{
		const u32 n = rd_be32 (r + k);
		const uint rw = rwv[b];
		char nm[48] = "(extra)";
		if (b < nb)
			snprintf (nm, sizeof (nm), "%.*s", (int)(rd_be32 (r + offs[b]) < 47 ? rd_be32 (r + offs[b]) : 47), (ccp)r + offs[b] + 4);
		tl += snprintf (t + tl, cap - tl, "track %u %s keys %u\n", b, nm, n);
		k += 4;
		for (u32 j = 0; j < n && tl + 400 < cap; j++, k += rw * 4)
		{
			float f[10];
			for (uint q = 0; q < rw - 1; q++)
			{
				const u32 v = rd_be32 (r + k + 4 + 4 * q);
				memcpy (&f[q], &v, 4);
			}
			tl += snprintf (t + tl, cap - tl, "%u quat %.5f %.5f %.5f %.5f pos %.5f %.5f %.5f", rd_be32 (r + k), f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
			if (rw == 11)
				tl += snprintf (t + tl, cap - tl, " scale %.5f %.5f %.5f", f[7], f[8], f[9]);
			t[tl++] = '\n';
		}
	}
	return tl;
}

static enumError extract_wtxid_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x88 || memcmp (raw, "WTXID", 5) || rd_be16 (raw + 6) != 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".txt");
	if (testmode)
	{
		fprintf (stdlog, "WOULD EXTRACT WTXID:%s -> TXT:%s\n", arg, dest);
		FREE (raw);
		return ERR_OK;
	}
	const size_t cap = 256 + sz * 3;
	char *t = MALLOC (cap);
	size_t tl = snprintf (t, cap, "WTXID kind %c size %zu\n", raw[5] >= 0x20 && raw[5] < 0x7f ? raw[5] : '?', sz);
	if (raw[5] == 'a' && sz > 0x108)
		tl += snprintf (t + tl, cap - tl, "animation frames %u fps %u bones %u (name list follows, in bone index order)\n", rd_be32 (raw + 0xfc), rd_be32 (raw + 0x100), rd_be32 (raw + 0x104));
	for (size_t i = 0x80; i + 4 < sz && tl + 300 < cap;)
	{
		size_t j = i;
		while (j < sz && (raw[j] == ' ' || raw[j] == '.' || raw[j] == '-' || raw[j] == '_' || (raw[j] >= '0' && raw[j] <= '9') || (raw[j] | 0x20) >= 'a' && (raw[j] | 0x20) <= 'z'))
			j++;
		if (j - i >= 4 && j - i < 200 && (j == sz || raw[j] == 0 || j - i >= 6))
		{
			tl += snprintf (t + tl, cap - tl, "%zx\t%.*s\n", i, (int)(j - i), (ccp)raw + i);
			i = j;
		}
		else
			i = j > i ? j : i + 1;
	}
	if (raw[5] == 'a' && sz > 0x108)
		tl += wtxid_anim_keys (raw, sz, t + tl, cap - tl);
	if (raw[5] == 'g')
		wtxid_meshes (arg, raw, sz);
	FREE (raw);
	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (t, 1, tl, F.f) != tl)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", tl, dest);
	ResetFile (&F, opt_preserve);
	FREE (t);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT WTXID:%s -> TXT:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
