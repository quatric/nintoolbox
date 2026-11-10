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

// Cook Wars skeleton: per bone {u32 word, u32 name length, name, f32 quat x y z w, f32 pos x y z (local, rest pose)}, in parent-first order; the u32 behind a record's floats holds in its low 16 bits
// the number of descendants of THAT bone (the bones following it that belong to its subtree), so the parent of bone k is the innermost earlier bone j with j < k <= j + descendants(j). Verified by
// posing the rest pose: head over the pelvis, symmetric arms, feet at the floor, bounding box matching the mesh (the Biped puts the legs under Spine, offsets compensate).
// Fills local quats / positions / parents; returns the bone count (0 when the records are not contiguous).
static uint wtxid_skeleton (const u8 *r, size_t sz, const size_t *boff, uint nbn, float (*q)[4], float (*pp)[3], int *par)
{
	uint desc[256];
	// record size: 7 floats {quat, pos} or 10 floats (+ scale xyz, all ~1: the "new" costume models) plus the u32 word; taken from the distance of the first two names
	if (nbn < 2 || nbn > 256)
		return 0;
	const size_t tail = boff[1] - (boff[0] + 4 + rd_be32 (r + boff[0]));
	if (tail != 32 && tail != 44)
		return 0;
	for (uint i = 0; i < nbn; i++)
	{
		const size_t e = boff[i] + 4 + rd_be32 (r + boff[i]);
		if (e + tail > sz || (i + 1 < nbn && boff[i + 1] != e + tail))
			return 0;
		for (uint k = 0; k < 7; k++)
		{
			const u32 v = rd_be32 (r + e + 4 * k);
			memcpy (k < 4 ? &q[i][k] : &pp[i][k - 4], &v, 4);
		}
		desc[i] = rd_be32 (r + e + tail - 4) & 0xffff;
	}
	int st[256], ends[256], sp = 0;
	for (uint k = 0; k < nbn; k++)
	{
		while (sp && ends[sp - 1] <= (int)k)
			sp--;
		par[k] = sp ? st[sp - 1] : -1;
		st[sp] = k, ends[sp++] = k + 1 + (int)(k + 1 < nbn ? desc[k] : 0);
	}
	return nbn;
}

// skinned glTF of one Cook Wars mesh: the skeleton (nodes with local TRS, children), one skin whose inverse bind matrices are the inverses of the rest pose world transforms, JOINTS_0 / WEIGHTS_0
// (the four strongest weights of a vertex, renormalised). fl: 8 floats per corner, jw: 4 joints + 4 weights (as 8 floats) per corner.
static char *wtxid_gltf_skinned (const float *fl, const float *jw, uint nvx, uint nj, float (*q)[4], float (*pp)[3], const int *par, const u8 *r, const size_t *boff, size_t *len)
{
	const uint nb1 = nvx * 32, nb2 = nvx * 8, nb3 = nvx * 16, nb4 = nj * 64, nbytes = nb1 + nb2 + nb3 + nb4;
	u8 *buf = CALLOC (nbytes + 4, 1);
	memcpy (buf, fl, nb1);
	float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
	for (uint i = 0; i < nvx; i++)
	{
		for (uint k = 0; k < 3; k++)
		{
			if (fl[8 * i + k] < mn[k]) mn[k] = fl[8 * i + k];
			if (fl[8 * i + k] > mx[k]) mx[k] = fl[8 * i + k];
		}
		u16 *j = (u16 *)(buf + nb1) + 4 * i;
		float *w = (float *)(buf + nb1 + nb2) + 4 * i;
		for (uint k = 0; k < 4; k++)
			j[k] = (u16)jw[8 * i + k], w[k] = jw[8 * i + 4 + k];
	}
	// world rest transforms -> inverse bind (column major 4x4 of the rigid inverse)
	float (*R)[9] = CALLOC (nj, sizeof (*R)), (*P)[3] = CALLOC (nj, sizeof (*P));
	for (uint k = 0; k < nj; k++)
	{
		const float x = q[k][0], y = q[k][1], z = q[k][2], w = q[k][3];
		const float m[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
		if (par[k] < 0)
		{
			memcpy (R[k], m, sizeof (m));
			memcpy (P[k], pp[k], sizeof (P[k]));
		}
		else
		{
			const float *a = R[par[k]];
			for (uint i = 0; i < 3; i++)
			{
				for (uint c = 0; c < 3; c++)
					R[k][3 * i + c] = a[3 * i] * m[c] + a[3 * i + 1] * m[3 + c] + a[3 * i + 2] * m[6 + c];
				P[k][i] = P[par[k]][i] + a[3 * i] * pp[k][0] + a[3 * i + 1] * pp[k][1] + a[3 * i + 2] * pp[k][2];
			}
		}
		float *ib = (float *)(buf + nb1 + nb2 + nb3) + 16 * k;
		for (uint c = 0; c < 3; c++)
			for (uint i = 0; i < 3; i++)
				ib[4 * c + i] = R[k][3 * c + i];	// R^T, column c row i = R[c][i]
		for (uint i = 0; i < 3; i++)
			ib[12 + i] = -(R[k][i] * P[k][0] + R[k][3 + i] * P[k][1] + R[k][6 + i] * P[k][2]);
		ib[15] = 1;
	}
	FREE (R);
	FREE (P);
	const size_t cap = 4096 + (size_t)nj * 400 + ((size_t)nbytes + 2) / 3 * 4;
	char *t = MALLOC (cap);
	size_t tl = snprintf (t, cap, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[");
	for (uint k = 0; k < nj; k++)
		if (par[k] < 0)
			tl += snprintf (t + tl, cap - tl, "%u,", k);
	tl += snprintf (t + tl, cap - tl, "%u]}],\"nodes\":[", nj);
	for (uint k = 0; k < nj; k++)
	{
		tl += snprintf (t + tl, cap - tl, "{\"name\":\"%.*s\",\"translation\":[%.7g,%.7g,%.7g],\"rotation\":[%.7g,%.7g,%.7g,%.7g]", (int)rd_be32 (r + boff[k]), (ccp)r + boff[k] + 4, pp[k][0], pp[k][1], pp[k][2], q[k][0], q[k][1], q[k][2], q[k][3]);
		bool first = true;
		for (uint c = 0; c < nj; c++)
			if (par[c] == (int)k)
				tl += snprintf (t + tl, cap - tl, "%s%u", first ? ",\"children\":[" : ",", c), first = false;
		tl += snprintf (t + tl, cap - tl, "%s},", first ? "" : "]");
	}
	tl += snprintf (t + tl, cap - tl, "{\"mesh\":0,\"skin\":0}],\"skins\":[{\"inverseBindMatrices\":3,\"joints\":[");
	for (uint k = 0; k < nj; k++)
		tl += snprintf (t + tl, cap - tl, "%s%u", k ? "," : "", k);
	tl += snprintf (t + tl, cap - tl,
		"]}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":4,\"WEIGHTS_0\":5},\"mode\":4}]}],"
		"\"accessors\":[{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\",\"min\":[%.6g,%.6g,%.6g],\"max\":[%.6g,%.6g,%.6g]},"
		"{\"bufferView\":0,\"byteOffset\":12,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},"
		"{\"bufferView\":0,\"byteOffset\":24,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"},"
		"{\"bufferView\":3,\"componentType\":5126,\"count\":%u,\"type\":\"MAT4\"},"
		"{\"bufferView\":1,\"componentType\":5123,\"count\":%u,\"type\":\"VEC4\"},"
		"{\"bufferView\":2,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"}],"
		"\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u,\"byteStride\":32,\"target\":34962},"
		"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962},"
		"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":34962},"
		"{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}],"
		"\"buffers\":[{\"byteLength\":%u,\"uri\":\"data:application/octet-stream;base64,",
		nvx, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], nvx, nvx, nj, nvx, nvx, nb1, nb1, nb2, nb1 + nb2, nb3, nb1 + nb2 + nb3, nb4, nbytes);
	tl = shade_b64 (t, tl, buf, nbytes);
	tl += snprintf (t + tl, cap - tl, "\"}]}\n");
	FREE (buf);
	*len = tl;
	return t;
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
			if (!err)
			{
				// static glTF next to the OBJ (corner list from the same arrays)
				float *fl = CALLOC (dln * 8 + 8, sizeof (float)), *jw = CALLOC (dln * 8 + 8, sizeof (float));
				size_t *vo = CALLOC (nv + 1, sizeof (size_t));	// byte offset of every vertex record
				for (uint i = 0, qo = p + 4; i < nv; i++)
					vo[i] = qo, qo += 25 + 6 * (size_t)r[qo + 24];
				uint nvx = 0;
				for (uint i = 0; i + 2 < dln; i += 3)
				{
					bool ok = true;
					for (uint c = 0; c < 3; c++)
						ok = ok && rd_be16 (r + dlp + 4 * (i + c)) < nv && rd_be16 (r + dlp + 4 * (i + c) + 2) < uvn;
					if (!ok)
						continue;
					for (uint c = 0; c < 3; c++, nvx++)
					{
						const uint vi = rd_be16 (r + dlp + 4 * (i + c)), ti = rd_be16 (r + dlp + 4 * (i + c) + 2);
						const size_t qq = vo[vi];
						for (uint k = 0; k < 6; k++)
						{
							const u32 v = rd_be32 (r + qq + 4 * k);
							memcpy (&fl[8 * nvx + k], &v, 4);
						}
						const u32 ua = rd_be32 (r + uvp + 8 * (size_t)ti), ub = rd_be32 (r + uvp + 4 + 8 * (size_t)ti);
						float u, v;
						memcpy (&u, &ua, 4);
						memcpy (&v, &ub, 4);
						fl[8 * nvx + 6] = u, fl[8 * nvx + 7] = 1.0f - v;
						{
							// the four strongest skin weights of the vertex (bone index, weight), renormalised
							const uint ke = r[qq + 24];
							float tot = 0.f;
							for (uint a = 0; a < 4; a++)
							{
								int best = -1;
								float bw = 0.f;
								for (uint e = 0; e < ke; e++)
								{
									const u32 wv = rd_be32 (r + qq + 27 + 6 * e);
									float w;
									memcpy (&w, &wv, 4);
									bool used = false;
									for (uint b2 = 0; b2 < a; b2++)
										used = used || (uint)jw[8 * nvx + b2] == rd_be16 (r + qq + 25 + 6 * e);
									if (!used && w > bw && rd_be16 (r + qq + 25 + 6 * e) < 256)
										bw = w, best = rd_be16 (r + qq + 25 + 6 * e);
								}
								if (best >= 0)
									jw[8 * nvx + a] = (float)best, jw[8 * nvx + 4 + a] = bw, tot += bw;
							}
							if (tot > 0.f)
								for (uint a = 0; a < 4; a++)
									jw[8 * nvx + 4 + a] /= tot;
							else
								jw[8 * nvx + 4] = 1.f;
						}
					}
				}
				if (nvx)
				{
					size_t gl;
					char *g = shade_gltf_tris (fl, nvx, &gl);
					char gdest[PATH_MAX];
					snprintf (ext, sizeof (ext), nm ? ".%u.gltf" : ".gltf", nm);
					beside_source_dest_ext (gdest, sizeof (gdest), arg, ext);
					File_t G;
					CreateFileOpt (&G, true, gdest, false, arg);
					if (G.f)
						fwrite (g, 1, gl, G.f);
					ResetFile (&G, opt_preserve);
					FREE (g);
				}
				if (nvx && nbn)
				{
					float (*bq)[4] = CALLOC (nbn, sizeof (*bq)), (*bp)[3] = CALLOC (nbn, sizeof (*bp));
					int bpar[256];
					if (wtxid_skeleton (r, sz, boff, nbn, bq, bp, bpar))
					{
						size_t gl;
						char *g = wtxid_gltf_skinned (fl, jw, nvx, nbn, bq, bp, bpar, r, boff, &gl);
						char gdest[PATH_MAX];
						snprintf (ext, sizeof (ext), nm ? ".%u.skin.gltf" : ".skin.gltf", nm);
						beside_source_dest_ext (gdest, sizeof (gdest), arg, ext);
						File_t G;
						CreateFileOpt (&G, true, gdest, false, arg);
						if (G.f)
							fwrite (g, 1, gl, G.f);
						ResetFile (&G, opt_preserve);
						FREE (g);
					}
					FREE (bq);
					FREE (bp);
				}
				FREE (vo);
				FREE (jw);
				FREE (fl);
			}
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
