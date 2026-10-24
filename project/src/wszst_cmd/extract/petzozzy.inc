#include "lib-model-glb.h"
#include <zlib.h>
// Petz / Magma engine level containers ".ozzy" (Petz Sports, Wii): u32 0, then chunks {u32 raw size (<= 0x4000), u32
// zlib size, zlib data} and a 4 byte trailer. The inflated stream is an "LV3D" scene of big-endian, byte packed records
// (no padding): "MESH" nodes name a "GEOM" (u32 version 3/4, u32 vertex count, u32 0, u32 flags, then one attribute
// block per stream {u32 count, u32 0, u8 stride, u8 0, u8 semantic, u8 type, u8 components, u8 0, u8 fraction bits,
// u32 byte size, data}: semantic 1 position (f32 x3, s16 x3 or s8 x3), 2 normal (s8 x3), 3 colour (u8 x4), 5 texcoord
// (u8 x2 or s16 x2); fixed point values are divided by 2^fraction bits), the GX display list {u32 size, u8 0x90|fmt, u16
// vertex count, interleaved vertices} that repeats the same triangle list, and the diffuse texture path. "TEXT" records
// followed by a "TTPL" wrapper (u32 size + Wii TPL) carry the textures. Written as "level.bin", "<texture>.ttpl" and,
// in decode mode, "models.glb" with one mesh per GEOM.
typedef struct petz_mat_t
{
	char path[128];
	char base[64];
} petz_mat_t;

static float petz_fixed (const u8 *p, uint type_size, uint frac)
{
	const float scale = 1.0f / (float)(1u << (frac > 31 ? 31 : frac));
	switch (type_size)
	{
	case 1: return (float)(s8)p[0] * scale;
	case 2: return (float)(s16)rd_be16 (p) * scale;
	default: return 0.0f;
	}
}

static bool petz_str (const u8 *d, size_t sz, size_t p, char *out, size_t out_size, size_t *end)
{
	if (p + 4 > sz)
		return false;
	const u32 n = rd_be32 (d + p);
	if (!n || n >= out_size || p + 4 + n > sz)
		return false;
	for (u32 i = 0; i < n; i++)
		if (d[p + 4 + i] < 0x20 || d[p + 4 + i] > 0x7e)
			return false;
	memcpy (out, d + p + 4, n);
	out[n] = 0;
	if (end)
		*end = p + 4 + n;
	return true;
}

static void petz_basename (char *out, size_t out_size, ccp path)
{
	ccp b = strrchr (path, '/');
	snprintf (out, out_size, "%s", b ? b + 1 : path);
}

// Appends a mesh (unindexed triangle list, takes ownership of the arrays) and its material slot.
static bool petz_finish_mesh (const u8 *d, size_t sz, size_t p, ccp tex, vec3_t *pos, vec3_t *nrm, vec2_t *uv, u32 nv,
	model_t *model, uint index, petz_mat_t **mats, uint *nmats)
{
	mesh_t *meshes = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*meshes));
	vertex_t *verts = CALLOC (nv, sizeof (*verts));
	int *tri_mat = CALLOC (nv / 3 + 1, sizeof (*tri_mat));
	if (!meshes || !verts || !tri_mat)
	{
		if (meshes)
			model->meshes = meshes;
		FREE (pos);
		FREE (nrm);
		FREE (uv);
		FREE (verts);
		FREE (tri_mat);
		return false;
	}
	model->meshes = meshes;
	mesh_t *mesh = meshes + model->num_meshes++;
	memset (mesh, 0, sizeof (*mesh));
	mesh->positions = pos;
	mesh->num_positions = nv;
	mesh->normals = nrm;
	mesh->num_normals = nrm ? nv : 0;
	mesh->texcoords = uv;
	mesh->num_texcoords = uv ? nv : 0;
	for (u32 i = 0; i < nv; i++)
	{
		verts[i].position_idx = i;
		verts[i].normal_idx = nrm ? (int)i : -1;
		verts[i].texcoord_idx = uv ? (int)i : -1;
		verts[i].tangent_idx = verts[i].matrix_idx = -1;
		verts[i].color_idx[0] = verts[i].color_idx[1] = -1;
		for (int x = 0; x < 7; x++)
			verts[i].extra_texcoord_idx[x] = -1;
	}
	mesh->vertices = verts;
	mesh->num_vertices = nv;
	mesh->triangle_materials = tri_mat;

	// geometry name from the MESH node just before the tag
	char name[128] = "";
	for (size_t b = p; b > 4 && p - b < 0x100; b--)
		if (!memcmp (d + b, "MESH", 4))
		{
			char path[128];
			if (petz_str (d, sz, b + 12, path, sizeof (path), 0))
				petz_basename (name, sizeof (name), path);
			break;
		}
	if (!*name)
		snprintf (name, sizeof (name), "geom%u", index);
	snprintf (mesh->name, sizeof (mesh->name), "%s", name);

	uint mi = 0;
	for (; mi < *nmats; mi++)
		if (!strcmp ((*mats)[mi].path, tex))
			break;
	if (mi == *nmats)
	{
		petz_mat_t *nm = REALLOC (*mats, (*nmats + 1) * sizeof (**mats));
		if (!nm)
			return true;
		*mats = nm;
		memset (nm + *nmats, 0, sizeof (*nm));
		snprintf (nm[*nmats].path, sizeof (nm[*nmats].path), "%s", tex);
		petz_basename (nm[*nmats].base, sizeof (nm[*nmats].base), tex);
		(*nmats)++;
	}
	mesh->material_idx = (int)mi;
	return true;
}

// Parse the GEOM at "d + p" (pointing at the tag); appends a mesh to model.
static bool petz_geom (const u8 *d, size_t sz, size_t p, model_t *model, uint index, petz_mat_t **mats, uint *nmats)
{
	if (p + 20 > sz)
		return false;
	const u32 ver = rd_be32 (d + p + 4), nv = rd_be32 (d + p + 8);
	if ((ver != 3 && ver != 4) || !nv || nv > 0x40000 || nv % 3 || rd_be32 (d + p + 12))
		return false;
	size_t q = p + 20;
	vec3_t *pos = 0, *nrm = 0;
	vec2_t *uv = 0;
	bool bad = false;
	while (q + 19 <= sz && rd_be32 (d + q) == nv && !rd_be32 (d + q + 4))
	{
		const uint st = d[q + 8], sem = d[q + 10], frac = d[q + 14];
		const u32 bytes = rd_be32 (d + q + 15);
		if (!st || bytes != nv * st || q + 19 + bytes > sz)
			break;
		const u8 *data = d + q + 19;
		if (sem == 1 && !pos && (st == 12 || st == 6 || st == 3))
		{
			pos = CALLOC (nv, sizeof (*pos));
			if (!pos)
				bad = true;
			for (u32 i = 0; !bad && i < nv; i++)
			{
				const u8 *v = data + i * st;
				if (st == 12)
				{
					u32 w[3] = { rd_be32 (v), rd_be32 (v + 4), rd_be32 (v + 8) };
					memcpy (&pos[i].x, &w[0], 4);
					memcpy (&pos[i].y, &w[1], 4);
					memcpy (&pos[i].z, &w[2], 4);
				}
				else
				{
					const uint cs = st / 3;
					pos[i].x = petz_fixed (v, cs, frac);
					pos[i].y = petz_fixed (v + cs, cs, frac);
					pos[i].z = petz_fixed (v + 2 * cs, cs, frac);
				}
			}
		}
		else if (sem == 2 && !nrm && st == 3)
		{
			nrm = CALLOC (nv, sizeof (*nrm));
			if (!nrm)
				bad = true;
			for (u32 i = 0; !bad && i < nv; i++)
			{
				nrm[i].x = petz_fixed (data + i * 3, 1, frac);
				nrm[i].y = petz_fixed (data + i * 3 + 1, 1, frac);
				nrm[i].z = petz_fixed (data + i * 3 + 2, 1, frac);
			}
		}
		else if (sem == 5 && !uv && (st == 2 || st == 4))
		{
			uv = CALLOC (nv, sizeof (*uv));
			if (!uv)
				bad = true;
			for (u32 i = 0; !bad && i < nv; i++)
			{
				const uint cs = st / 2;
				const u8 *v = data + i * st;
				if (cs == 1)
				{
					uv[i].u = (float)v[0] / (float)(1u << frac);
					uv[i].v = (float)v[1] / (float)(1u << frac);
				}
				else
				{
					uv[i].u = petz_fixed (v, 2, frac);
					uv[i].v = petz_fixed (v + 2, 2, frac);
				}
			}
		}
		q += 19 + bytes;
	}
	if (bad || !pos)
	{
		FREE (pos);
		FREE (nrm);
		FREE (uv);
		return false;
	}

	// display list header, then the texture path after the list
	size_t end = q;
	char tex[128] = "";
	if (q + 7 <= sz && (d[q + 4] & 0xf8) == 0x90 && rd_be16 (d + q + 5) == nv)
	{
		end = q + 4 + rd_be32 (d + q);
		size_t e2 = end;
		if (end + 8 <= sz)
		{
			// skip zero padding, then {u32 count, u32 len, path}
			while (e2 + 8 < sz && e2 < end + 64 && d[e2] == 0 && !(e2 + 8 <= sz && rd_be32 (d + e2) == 1))
				e2++;
			if (e2 + 8 <= sz && rd_be32 (d + e2) == 1)
				petz_str (d, sz, e2 + 4, tex, sizeof (tex), 0);
		}
	}

	return petz_finish_mesh (d, sz, p, tex, pos, nrm, uv, nv, model, index, mats, nmats);
}

// Skinned meshes "SMSH" / "FSMH": {tag, u32 version, u32, u32, ...}, a list of 19 byte attribute records {u8 stride, 0,
// semantic, 0, components, 14 zero bytes} (1 position, 2 normal, 5 texcoord; f32), then the GX display list {u32 size,
// u8 0x90|fmt, u16 vertex count, vertices of {u8 matrix index, f32 position, [f32 normal], [f32 texcoord]}}. Exported
// as the bind pose (the first display list; no skin).
static bool petz_smsh (const u8 *d, size_t sz, size_t p, model_t *model, uint index, petz_mat_t **mats, uint *nmats)
{
	size_t q = p + 0x24;
	for (; q + 19 < sz && q < p + 0x60; q++)
		if (d[q] == 12 && !d[q + 1] && d[q + 2] == 1 && !d[q + 3] && d[q + 4] == 3)
			break;
	if (q + 19 >= sz || q >= p + 0x60)
		return false;
	const size_t q0 = q;
	bool has_nrm = false, has_uv = false;
	uint vsz = 1;
	while (q + 19 <= sz && d[q] && !d[q + 1] && !d[q + 3] && (d[q + 2] == 1 || d[q + 2] == 2 || d[q + 2] == 5))
	{
		vsz += d[q];
		has_nrm |= d[q + 2] == 2;
		has_uv |= d[q + 2] == 5;
		q += 19;
	}
	// all display lists of the file (one per bone batch), concatenated into one triangle list
	vec3_t *pos = 0, *nrm = 0;
	vec2_t *uv = 0;
	size_t total = 0, cap = 0;
	size_t e = q0, limit = q + 16;
	while (e + 7 < sz)
	{
		const u32 size = rd_be32 (d + e);
		const uint op = d[e + 4], nv = rd_be16 (d + e + 5);
		if ((op & 0xf8) == 0x90 && nv && nv % 3 == 0 && size == ((3 + (u64)nv * vsz + 31) & ~31u)
			&& e + 7 + (size_t)nv * vsz <= sz)
		{
			if (total + nv > cap)
			{
				cap = (total + nv) * 2;
				vec3_t *np = REALLOC (pos, cap * sizeof (*np));
				if (np)
					pos = np;
				vec3_t *nn = has_nrm ? REALLOC (nrm, cap * sizeof (*nn)) : 0;
				if (has_nrm && nn)
					nrm = nn;
				vec2_t *nu = has_uv ? REALLOC (uv, cap * sizeof (*nu)) : 0;
				if (has_uv && nu)
					uv = nu;
				if (!np || (has_nrm && !nn) || (has_uv && !nu))
				{
					FREE (pos);
					FREE (nrm);
					FREE (uv);
					return false;
				}
			}
			for (uint i = 0; i < nv; i++)
			{
				const u8 *v = d + e + 7 + (size_t)i * vsz + 1;
				u32 w[8];
				for (uint k = 0; k < 8 && 4 * k + 5 <= vsz; k++)
					w[k] = rd_be32 (v + 4 * k);
				memcpy (&pos[total + i].x, &w[0], 4);
				memcpy (&pos[total + i].y, &w[1], 4);
				memcpy (&pos[total + i].z, &w[2], 4);
				uint k = 3;
				if (has_nrm)
				{
					memcpy (&nrm[total + i].x, &w[k], 4);
					memcpy (&nrm[total + i].y, &w[k + 1], 4);
					memcpy (&nrm[total + i].z, &w[k + 2], 4);
					k += 3;
				}
				if (has_uv)
				{
					memcpy (&uv[total + i].u, &w[k], 4);
					memcpy (&uv[total + i].v, &w[k + 1], 4);
				}
			}
			total += nv;
			e += 4 + size;
			limit = e + 0x100;
			continue;
		}
		if (e >= limit)
			break;
		e++;
	}
	if (!total)
	{
		FREE (pos);
		FREE (nrm);
		FREE (uv);
		return false;
	}
	return petz_finish_mesh (d, sz, p, "", pos, nrm, uv, (u32)total, model, index, mats, nmats);
}

static enumError extract_petzozzy_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ozzy"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t rsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &rsz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (rsz < 16 || rd_be32 (raw))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = 1 << 20, sz = 0, p = 4;
	u8 *d = MALLOC (cap);
	while (d && p + 8 <= rsz)
	{
		const u32 rs = rd_be32 (raw + p), cs = rd_be32 (raw + p + 4);
		if (!rs || rs > 0x4000 || !cs || p + 8 + cs > rsz)
			break;
		if (sz + rs > cap)
		{
			cap *= 2;
			u8 *nd = REALLOC (d, cap);
			if (!nd)
			{
				FREE (d);
				d = 0;
				break;
			}
			d = nd;
		}
		uLongf out = rs;
		if (uncompress (d + sz, &out, raw + p + 8, cs) != Z_OK || out != rs)
			break;
		sz += rs;
		p += 8 + cs;
	}
	FREE (raw);
	if (!d || !sz || p + 4 > rsz + 4 || p < 12)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	model_t *model = CALLOC (1, sizeof (*model));
	petz_mat_t *mats = 0;
	uint nmats = 0, ngeom = 0;
	uint ntex = 0, cap_entries = 2;
	nintendo_sarc_entry_t *entries = CALLOC (cap_entries, sizeof (*entries));
	if (!model || !entries)
	{
		FREE (model);
		FREE (entries);
		FREE (d);
		return ERR_OUT_OF_MEMORY;
	}
	bool ok = OwnedEntryAdd (entries, 0, "level.bin", d, sz);
	uint n_entries = ok ? 1 : 0;
	const bool want_glb = export_count > 0 || opt_decode;

	for (size_t i = 0; i + 8 < sz; i++)
	{
		if (d[i] == 'G' && !memcmp (d + i, "GEOM", 4) && want_glb)
		{
			if (petz_geom (d, sz, i, model, ngeom, &mats, &nmats))
				ngeom++;
		}
		else if (want_glb && (d[i] == 'S' || d[i] == 'F') && (!memcmp (d + i, "SMSH", 4) || !memcmp (d + i, "FSMH", 4)))
		{
			if (petz_smsh (d, sz, i, model, ngeom, &mats, &nmats))
				ngeom++;
		}
		else if (d[i] == 'T' && !memcmp (d + i, "TTPL", 4))
		{
			const u32 ts = rd_be32 (d + i + 4);
			char name[128], base[64];
			size_t t = i;
			while (t > 0 && i - t < 0x100 && memcmp (d + t, "TEXT", 4))
				t--;
			if (ts < 0x40 || i + 8 + ts > sz || memcmp (d + t, "TEXT", 4)
				|| !petz_str (d, sz, t + 8, name, sizeof (name), 0))
				continue;
			size_t nl = strlen (name);
			if (nl > 3 && !strcmp (name + nl - 3, "txl"))
				name[nl - 3] = 0;
			petz_basename (base, sizeof (base), name);
			char fn[96];
			snprintf (fn, sizeof (fn), "%s.ttpl", base);
			bool dup = false;
			for (uint e = 0; e < n_entries; e++)
				if (!strcmp (entries[e].name, fn))
					dup = true;
			if (dup)
				continue;
			if (n_entries == cap_entries)
			{
				cap_entries *= 2;
				nintendo_sarc_entry_t *ne = REALLOC (entries, cap_entries * sizeof (*entries));
				if (!ne)
					break;
				entries = ne;
				memset (entries + n_entries, 0, (cap_entries - n_entries) * sizeof (*entries));
			}
			if (OwnedEntryAdd (entries, n_entries, fn, d + i, 8 + ts))
			{
				n_entries++;
				ntex++;
			}
		}
	}

	enumError err = ERR_OUT_OF_MEMORY;
	if (ok)
	{
		// write the files first so the TTPL members are decoded to PNG before the model embeds them
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		err = write_owned_entries (arg, basedir, depth, "PETZ-OZZY", entries, n_entries);
		if (!err && want_glb && !testmode && model->num_meshes)
		{
			model->materials = CALLOC (nmats ? nmats : 1, sizeof (*model->materials));
			if (model->materials)
			{
				model->num_materials = nmats ? nmats : 1;
				for (uint m = 0; m < model->num_materials; m++)
				{
					material_t *mt = model->materials + m;
					snprintf (mt->name, sizeof (mt->name), "%s", nmats && *mats[m].base ? mats[m].base : "material");
					mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = mt->diffuse[3] = 1.0f;
					if (nmats && *mats[m].base)
					{
						snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s.ttpl", mats[m].base);
						mt->num_textures = 1;
					}
				}
				char glb[PATH_MAX];
				snprintf (glb, sizeof (glb), "%s/%smodels.glb", dest, basedir ? basedir : "");
				ExportModelToGLB (model, glb);
				if (verbose >= 0)
					fprintf (stdlog, "%sEXTRACT PETZ-OZZY:%s -> GLB:%s (%u meshes)\n", verbose > 0 ? "\n" : "", arg,
						glb, (uint)model->num_meshes);
			}
		}
	}
	else
		FREE (entries);
	(void)ntex;
	FreeModel (model);
	FREE (mats);
	FREE (d);
	return err;
}

static bool petz_has_magic (ccp arg, ccp magic)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return false;
	u8 head[4];
	const bool ok = fread (head, 1, 4, f) == 4 && !memcmp (head, magic, 4);
	fclose (f);
	return ok;
}

// Skeleton of a Petz skinned mesh ("SMSH" / "FSMH"): a table of 88-byte node records {u32 id hash, f32 scale[3], f32
// translation[3], f32 rotation[4] (quaternion), f32 scale[3], f32 translation[3], f32 rotation[4] (a second pose), u32
// child count}, stored depth first, so the child counts alone give the hierarchy (they sum to N - 1). The id hashes are
// the ones of the sibling "*_mact" action table; the bone names behind them are not stored anywhere in the files.
static float petz_f32 (const u8 *p)
{
	const u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static bool petz_skel_node (const u8 *r)
{
	for (uint k = 0; k < 3; k++)
	{
		const float sc = petz_f32 (r + 4 + 4 * k);
		if (!(sc > 0.999f && sc < 1.001f))
			return false;
	}
	if (rd_be32 (r + 84) > 255)
		return false;
	for (uint q = 0; q < 2; q++)
	{
		const float x = petz_f32 (r + 28 + q * 40), y = petz_f32 (r + 32 + q * 40), z = petz_f32 (r + 36 + q * 40),
			    w = petz_f32 (r + 40 + q * 40);
		const float n = x * x + y * y + z * z + w * w;
		if (!(n > 0.98f && n < 1.02f))
			return false;
	}
	return true;
}

static size_t petz_skel_find (const u8 *d, size_t sz, uint *count)
{
	for (size_t i = 0x20; i + 88 * 2 <= sz; i += 4)
	{
		if (!petz_skel_node (d + i))
			continue;
		long pending = 1;
		uint n = 0;
		while (pending > 0 && i + 88 * (n + 1) <= sz && petz_skel_node (d + i + 88 * n))
		{
			pending += (long)rd_be32 (d + i + 88 * n + 84) - 1;
			n++;
		}
		if (!pending && n >= 2)
		{
			*count = n;
			return i;
		}
	}
	return 0;
}

static enumError petz_skel_write (ccp arg, const u8 *d, size_t sz)
{
	uint n = 0;
	const size_t base = petz_skel_find (d, sz, &n);
	if (!base)
		return ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.skeleton.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PETZ-SKEL:%s (%u nodes) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			FILE *f = F.f;
			int parent[256], left[256], depth = 0;
			fprintf (f, "# Petz skeleton, %u nodes (depth first), node table at 0x%zx\n"
				    "# index parent hash | scale | translation | rotation xyzw | second pose: scale | translation | rotation xyzw | children\n",
				n, base);
			for (uint i = 0; i < n; i++)
			{
				const u8 *r = d + base + 88 * i;
				while (depth > 0 && !left[depth - 1])
					depth--;
				const int par = depth ? parent[depth - 1] : -1;
				if (depth)
					left[depth - 1]--;
				fprintf (f, "%u %d %08x", i, par, rd_be32 (r));
				for (uint k = 0; k < 20; k++)
					fprintf (f, "%s%.6g", k == 0 || k == 3 || k == 6 || k == 10 || k == 13 || k == 16 ? " | " : " ", petz_f32 (r + 4 + 4 * k));
				fprintf (f, " | children %u\n", rd_be32 (r + 84));
				const uint c = rd_be32 (r + 84);
				if (c && depth < 256)
				{
					parent[depth] = i;
					left[depth++] = c;
				}
			}
		}
		const enumError ce = ResetFile (&F, opt_preserve);
		err = err ? err : ce;
	}
	return err;
}

// Standalone Petz "MESH" resource (u32 1, u32 1, geometry path, u32 texture count, texture path, material colours) or
// bare "GEOM" file -> "<name>.d/model.glb" (+ the diffuse TTPL, decoded to PNG). The paths are relative to the folder
// that holds "3d/".
static enumError extract_petzmesh_file (ccp arg, ccp basedir, uint depth)
{
	const bool is_mesh = petz_has_magic (arg, "MESH");
	const bool is_skin = petz_has_magic (arg, "SMSH") || petz_has_magic (arg, "FSMH");
	if (!is_mesh && !is_skin && !petz_has_magic (arg, "GEOM"))
		return ERR_NOTHING_TO_DO;
	const bool want_glb = export_count > 0 || opt_decode;
	if (!want_glb && !is_skin)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t rsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &rsz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (is_skin)
	{
		const enumError serr = petz_skel_write (arg, raw, rsz);
		if (serr != ERR_NOTHING_TO_DO && (serr || !want_glb))
		{
			FREE (raw);
			return serr;
		}
		if (!want_glb)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	char geom_path[128] = "", tex_path[128] = "", root[PATH_MAX] = "";
	u8 *geom = 0;
	size_t gsz = 0;
	u8 *tex = 0;
	size_t tsz = 0;
	if (is_mesh)
	{
		size_t e = 0;
		if (rsz < 16 || rd_be32 (raw + 4) != 1 || !petz_str (raw, rsz, 12, geom_path, sizeof (geom_path), &e))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		if (e + 4 <= rsz && rd_be32 (raw + e) >= 1)
			petz_str (raw, rsz, e + 4, tex_path, sizeof (tex_path), 0);
		// root = text before ".../3d/mesh/"
		ccp m = strstr (arg, "3d/mesh/");
		if (!m)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		snprintf (root, sizeof (root), "%.*s", (int)(m - arg), arg);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s%s", root, geom_path);
		if (LoadFileAlloc (path, 0, 0, &geom, &gsz, 0, 0, 0, false) || !geom)
		{
			FREE (raw);
			FREE (geom);
			return ERR_NOTHING_TO_DO;
		}
		if (*tex_path)
		{
			snprintf (path, sizeof (path), "%s%stxl", root, tex_path);
			if (LoadFileAlloc (path, 0, 0, &tex, &tsz, 0, 0, 0, false) || !tex || tsz < 0x40 || memcmp (tex, "TTPL", 4))
			{
				FREE (tex);
				tex = 0;
				tsz = 0;
			}
		}
		FREE (raw);
	}
	else
	{
		geom = raw;
		gsz = rsz;
	}

	model_t *model = CALLOC (1, sizeof (*model));
	petz_mat_t *mats = 0;
	uint nmats = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (model && (is_skin ? petz_smsh (geom, gsz, 0, model, 0, &mats, &nmats) : petz_geom (geom, gsz, 0, model, 0, &mats, &nmats))
		&& model->num_meshes == 1)
	{
		char base[64], name[96];
		petz_basename (base, sizeof (base), tex_path);
		ccp slash = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", slash ? slash + 1 : arg);
		snprintf (model->meshes[0].name, sizeof (model->meshes[0].name), "%s", name);
		model->materials = CALLOC (1, sizeof (*model->materials));
		model->num_materials = model->materials ? 1 : 0;
		model->meshes[0].material_idx = 0;
		if (model->materials)
		{
			snprintf (model->materials[0].name, sizeof (model->materials[0].name), "%s", *base ? base : "material");
			model->materials[0].diffuse[0] = model->materials[0].diffuse[1] = model->materials[0].diffuse[2]
				= model->materials[0].diffuse[3] = 1.0f;
		}
		nintendo_sarc_entry_t *entries = CALLOC (2, sizeof (*entries));
		uint n = 0;
		char fn[96];
		snprintf (fn, sizeof (fn), "%s.ttpl", *base ? base : "texture");
		bool ok = entries != 0;
		if (ok && tex && *base && OwnedEntryAdd (entries, n, fn, tex, tsz))
		{
			n++;
			if (model->materials)
			{
				snprintf (model->materials[0].textures[0], sizeof (model->materials[0].textures[0]), "%s", fn);
				model->materials[0].num_textures = 1;
			}
		}
		if (ok && n == 0)
		{
			// no texture: the model is the only output
			FREE (entries);
			char glb[PATH_MAX];
			beside_source_dest_ext (glb, sizeof (glb), arg, ".glb");
			if (testmode)
				fprintf (stdlog, "WOULD EXTRACT PETZ-MESH:%s -> GLB:%s\n", arg, glb);
			else
			{
				ExportModelToGLB (model, glb);
				if (verbose >= 0)
					fprintf (stdlog, "%sEXTRACT PETZ-MESH:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, glb);
			}
			err = ERR_OK;
		}
		else if (ok)
		{
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			err = write_owned_entries (arg, basedir, depth, "PETZ-MESH", entries, n);
			if (!err && !testmode)
			{
				char glb[PATH_MAX];
				snprintf (glb, sizeof (glb), "%s/%smodel.glb", dest, basedir ? basedir : "");
				ExportModelToGLB (model, glb);
				if (verbose >= 0)
					fprintf (stdlog, "%sEXTRACT PETZ-MESH:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, glb);
			}
		}
		else
			FREE (entries);
	}
	FreeModel (model);
	FREE (mats);
	FREE (geom);
	FREE (tex);
	return err;
}
