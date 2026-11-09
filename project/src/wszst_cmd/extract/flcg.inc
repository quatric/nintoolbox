// Grasshopper Manufacture "FLCG" static collision (No More Heroes ".GCL", e.g. STG_HI/AREA040COL.GCL; layout after the public "GHM File Docs"
// notes): big endian apart from the version word {"FLCG", u32 1 (little endian), 0, u16 models, u16 materials, u32 model table, u32 material
// table}. Models are linked records (next offset at +0x1c, 0x50 apart in practice) {char name[8], ..., float origin[3] (+0x20), u32 data offset (+0x38)}; the data record {u32 ?, u32 triangle
// offset, u32 ?, u32 triangle count, 6 floats bounding box} points to 48 byte triangles {u32 next, 0, u32 material offset, float v0[3], v1[3],
// v2[3]}; the material records (32 bytes, name first) are only names here. Written as "model.obj" (one object per model, one usemtl per triangle).
static enumError extract_flcg_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	char *obj = 0;
	if (sz < 0x40 || memcmp (raw, "FLCG", 4) || rd_le32 (raw + 4) != 1)
		goto done;
	{
		const uint nm = rd_be16 (raw + 12), nmat = rd_be16 (raw + 14);
		const u32 om = rd_be32 (raw + 16), omat = rd_be32 (raw + 20);
		if (!nm || nm > 4096 || nmat > 4096 || (u64)om + 60 > sz || (u64)omat + 32 * (u64)nmat > sz)
			goto done;
		size_t cap = 1 << 16, used = 0;
		obj = MALLOC (cap);
		if (!obj)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		bool ok = true;
#define F_EMIT(...) \
	do { \
		for (;;) { \
			const int w_ = snprintf (obj + used, cap - used, __VA_ARGS__); \
			if (w_ >= 0 && (size_t)w_ < cap - used) { used += (size_t)w_; break; } \
			char *nb_ = REALLOC (obj, cap * 2); \
			if (!nb_) { ok = false; break; } \
			obj = nb_; cap *= 2; \
		} \
	} while (0)
		uint ntri = 0, vb = 1;
		u32 mofs = om;
		for (uint m = 0; m < nm && ok; m++)
		{
			if ((u64)mofs + 60 > sz)
				goto bad;
			const u8 *mo = raw + mofs;
			mofs = rd_be32 (mo + 0x1c);
			const u32 dofs = rd_be32 (mo + 0x38);
			if ((u64)dofs + 40 > sz)
				goto bad;
			const u32 to = rd_be32 (raw + dofs + 4), nt = rd_be32 (raw + dofs + 12);
			if (nt > 0x1000000 || (u64)to + 48 * (u64)nt > sz)
				goto bad;
			float org[3];
			for (uint k = 0; k < 3; k++)
			{
				const u32 b = rd_be32 (mo + 0x20 + 4 * k);
				memcpy (&org[k], &b, 4);
			}
			F_EMIT ("o %.8s\n", (ccp)mo);
			u32 lastmat = 0xffffffff;
			for (uint t = 0; t < nt && ok; t++)
			{
				const u8 *tr = raw + to + 48 * (size_t)t;
				const u32 mat = rd_be32 (tr + 8);
				if (mat != lastmat)
				{
					lastmat = mat;
					if ((u64)mat + 8 <= sz && mat >= omat && mat < omat + 32 * (u32)nmat)
						F_EMIT ("usemtl %.8s\n", (ccp)raw + mat);
				}
				for (uint k = 0; k < 3; k++)
				{
					float v[3];
					for (uint c = 0; c < 3; c++)
					{
						const u32 b = rd_be32 (tr + 12 + 12 * k + 4 * c);
						memcpy (&v[c], &b, 4);
					}
					F_EMIT ("v %.5f %.5f %.5f\n", v[0] + org[0], v[1] + org[1], v[2] + org[2]);
				}
				F_EMIT ("f %u %u %u\n", vb, vb + 1, vb + 2);
				vb += 3;
				ntri++;
			}
		}
		if (!ok || !ntri)
			goto bad;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT FLCG:%s (%u models, %u triangles) -> %s/model.obj\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, nm, ntri, dest);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, "model.obj", (u8 *)obj, (uint)used);
	}
	goto done;
bad:
	err = ERR_NOTHING_TO_DO;
done:
	FREE (obj);
	FREE (raw);
	return err;
}
