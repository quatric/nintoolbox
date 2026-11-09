// Pangya (Super Swing Golf) model ".pet" / ".mpet": big endian chunk list {char tag[4], u32 size (including the 8 byte head)} ("BONE" skeleton,
// "MESH", ...); the MESH chunk holds, from +0x1c, a "grff" container {u32 0x20, "ffrg" "\0rmg", u32 size, u32 5, u32 chunk count (+0x14), ...} with
// 16 byte chunk headers from +0x20 {tag stored reversed, u32 count, u32 element size, u32 offset} (chunks "vbd" vertex bytes, "vbh" vertex buffers
// {flags, size, offset}, "ibd" / "ibh" u16 indices and their {size, offset}, "ved" vertex elements {offset, type (2 = float3, 1 = float2), usage
// (3 << 16 normal, 5 << 16 texcoord)}, "prm" primitives, found by tracing the game's MESH loader in Ghidra). A 0x20 byte primitive is {.., u16 index count
// (+6), u16 vertex count (+8), u16 vertex stride (+0xa), u16 0x98 (GX triangle strip, +0xc), u16 triangle count (+0xe), u16 index buffer (+0x10, 0xffff =
// none), u16 vertex buffer (+0x12)}. Positions come from the usage-0 element, normals from usage 3, texcoords from the first usage-5 element.
// Written as "model.obj" (one object per primitive; node transforms and skinning are not applied, textures are not linked) plus, for rigged
// models, "skeleton.txt" (bone names from the BONE chunk, parents from "mdl", local matrices from "trns").
// skeleton of a rigged model: "BONE" name table {u16 count (+0xc), entries {u32 name offset, u16 index, u16 length} from +0x10}; "mdl" 32 byte nodes
// (parent index in word 2, 0xffff = none) and "trns" 4x4 local float matrices (translation in the last row), one per bone -> "skeleton.txt"
static enumError pet_write_skeleton (ccp arg, ccp basedir, const u8 *raw, size_t sz, size_t B, const u32 *mdl, const u32 *trns, bool *wrote)
{
	*wrote = false;
	if (sz < 0x20 || memcmp (raw, "BONE", 4) || mdl[1] != 32 || trns[1] != 64)
		return ERR_OK;
	const uint nb = rd_be16 (raw + 0xc);
	if (nb < 2 || nb != mdl[0] || nb != trns[0] || 0x10 + 8 * (size_t)nb > sz || B + mdl[2] + 32 * (size_t)nb > sz || B + trns[2] + 64 * (size_t)nb > sz)
		return ERR_OK;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (testmode)
		return ERR_OK;
	char *sk = MALLOC (nb * 400 + 64);
	if (!sk)
		return ERR_OUT_OF_MEMORY;
	size_t su = (size_t)sprintf (sk, "# index parent name | 4x4 local matrix (row major, translation in row 4)\n");
	for (uint i = 0; i < nb; i++)
	{
		const u8 *e = raw + 0x10 + 8 * (size_t)i;
		const u32 no = rd_be32 (e), nl = rd_be16 (e + 6);
		const u32 par = rd_be32 (raw + B + mdl[2] + 32 * (size_t)i + 8);
		su += (size_t)sprintf (sk + su, "%u %d ", i, par >= nb ? -1 : (int)par);
		if ((size_t)no + nl <= sz)
			for (uint c = 0; c < nl && raw[no + c]; c++)
				sk[su++] = (char)raw[no + c];
		sk[su++] = ' ', sk[su++] = '|';
		for (uint c = 0; c < 16; c++)
		{
			const u32 w = rd_be32 (raw + B + trns[2] + 64 * (size_t)i + 4 * c);
			float f;
			memcpy (&f, &w, 4);
			su += (size_t)sprintf (sk + su, " %g", (double)f);
		}
		sk[su++] = '\n';
	}
	const enumError err = shade_write (arg, dest, basedir, "skeleton.txt", (u8 *)sk, (uint)su);
	FREE (sk);
	*wrote = !err;
	return err;
}

static enumError extract_pangyapet_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".pet") && !is_ext (arg, ".mpet"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	char *obj = 0;
	size_t B = 0;
	bool skel = false;
	for (size_t p = 0; p + 8 <= sz;)
	{
		const u32 cs = rd_be32 (raw + p + 4);
		if (!memcmp (raw + p, "MESH", 4))
		{
			B = p + 0x1c;
			break;
		}
		if (cs < 8)
			break;
		p += cs;
	}
	if (!B || B + 0x30 > sz || memcmp (raw + B + 4, "ffrg\0rmg", 8))
		goto done;
	{
		const uint nch = rd_be32 (raw + B + 0x14);
		if (nch < 2 || nch > 64 || B + 0x20 + 16 * (size_t)nch > sz)
			goto done;
		u32 ch[9][3];
		static const char *const want[9] = { "vbd", "vbh", "ibd", "ibh", "ved", "prm", "msh", "mdl", "trns" };
		bool has[9] = { 0 };
		for (uint i = 0; i < nch; i++)
		{
			const u8 *h = raw + B + 0x20 + 16 * (size_t)i;
			char t[5] = { (char)h[3], (char)h[2], (char)h[1], (char)h[0], 0 };
			for (uint w = 0; w < 9; w++)
				if (!strcmp (t, want[w]))
				{
					ch[w][0] = rd_be32 (h + 4), ch[w][1] = rd_be32 (h + 8), ch[w][2] = rd_be32 (h + 12);
					has[w] = true;
				}
		}
		if (has[7] && has[8])
		{
			err = pet_write_skeleton (arg, basedir, raw, sz, B, ch[7], ch[8], &skel);
			if (err)
				goto done;
			if (skel)
				err = ERR_OK;
			else
				err = ERR_NOTHING_TO_DO;
		}
		if (!has[0] || !has[1] || !has[4] || !has[5] || ch[1][1] != 12 || ch[5][1] != 0x20 || ch[4][1] != 12)
			goto done;
		// vertex elements
		int opos = -1, onrm = -1, ouv = -1;
		for (uint k = 0; k < ch[4][0] && k < 16; k++)
		{
			const u8 *e = raw + B + ch[4][2] + 12 * (size_t)k;
			if (B + ch[4][2] + 12 * (size_t)(k + 1) > sz || (rd_be32 (e) >> 16) == 0xff)
				break;
			const u32 off = rd_be32 (e), ty = rd_be32 (e + 4), us = rd_be32 (e + 8);
			if ((us >> 16) == 0 && ty == 2 && opos < 0)
				opos = (int)off;
			else if ((us >> 16) == 3 && ty == 2 && onrm < 0)
				onrm = (int)off;
			else if ((us >> 16) == 5 && ((us >> 8) & 0xff) == 0 && ty == 1 && ouv < 0)
				ouv = (int)off;
		}
		if (opos < 0)
			goto done;
		size_t cap = 1 << 16, used = 0;
		obj = MALLOC (cap);
		if (!obj)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		bool ok = true;
#define PET_EMIT(...) \
	do { \
		for (;;) { \
			const int w_ = snprintf (obj + used, cap - used, __VA_ARGS__); \
			if (w_ >= 0 && (size_t)w_ < cap - used) { used += (size_t)w_; break; } \
			char *nb_ = REALLOC (obj, cap * 2); \
			if (!nb_) { ok = false; break; } \
			obj = nb_; cap *= 2; \
		} \
	} while (0)
		uint ntri = 0, vbase = 1;
		const size_t vbd = B + ch[0][2];
		if (!(B + ch[0][2] <= sz && ch[0][0] <= sz - vbd))
			goto bad;
		for (uint k = 0; k < ch[5][0] && ok; k++)
		{
			const u8 *pr = raw + B + ch[5][2] + 32 * (size_t)k;
			if (B + ch[5][2] + 32 * (size_t)(k + 1) > sz)
				goto bad;
			uint ic = rd_be16 (pr + 6);
			const uint vc = rd_be16 (pr + 8), st = rd_be16 (pr + 10), cmd = rd_be16 (pr + 12), ii = rd_be16 (pr + 0x10),
				   vi = rd_be16 (pr + 0x12);
			if (!vc || !st || cmd != 0x98 || vi >= ch[1][0] || (int)st < opos + 12)
				goto bad;
			const u8 *vh = raw + B + ch[1][2] + 12 * (size_t)vi;
			if (B + ch[1][2] + 12 * (size_t)(vi + 1) > sz)
				goto bad;
			const u32 vsz = rd_be32 (vh + 4), voff = rd_be32 (vh + 8);
			if (vsz != (u32)vc * st || (u64)voff + vsz > ch[0][0])
				goto bad;
			const u8 *vd = raw + vbd + voff;
			const u8 *ix = 0;
			if (ii != 0xffff)
			{
				if (!has[2] || !has[3] || ii >= ch[3][0])
					goto bad;
				const u8 *ih = raw + B + ch[3][2] + 8 * (size_t)ii;
				const u32 isz = rd_be32 (ih), ioff = rd_be32 (ih + 4);
				if (isz != (u32)ic * 2 || (u64)ioff + isz > ch[2][0] || B + ch[2][2] + (size_t)ioff + isz > sz)
					goto bad;
				ix = raw + B + ch[2][2] + ioff;
			}
			else
				ic = vc;
			PET_EMIT ("o prim%u\n", k);
			for (uint v = 0; v < vc && ok; v++)
			{
				float f[3];
				for (uint c = 0; c < 3; c++)
				{
					const u32 b = rd_be32 (vd + (size_t)v * st + opos + 4 * c);
					memcpy (&f[c], &b, 4);
					if (!isfinite (f[c]))
						f[c] = 0;
				}
				PET_EMIT ("v %.5f %.5f %.5f\n", f[0], f[1], f[2]);
			}
			if (ouv >= 0)
				for (uint v = 0; v < vc && ok; v++)
				{
					float f[2];
					for (uint c = 0; c < 2; c++)
					{
						const u32 b = rd_be32 (vd + (size_t)v * st + ouv + 4 * c);
						memcpy (&f[c], &b, 4);
						if (!isfinite (f[c]))
							f[c] = 0;
					}
					PET_EMIT ("vt %.5f %.5f\n", f[0], 1.0f - f[1]);
				}
			if (onrm >= 0)
				for (uint v = 0; v < vc && ok; v++)
				{
					float f[3];
					for (uint c = 0; c < 3; c++)
					{
						const u32 b = rd_be32 (vd + (size_t)v * st + onrm + 4 * c);
						memcpy (&f[c], &b, 4);
						if (!isfinite (f[c]))
							f[c] = 0;
					}
					PET_EMIT ("vn %.4f %.4f %.4f\n", f[0], f[1], f[2]);
				}
			for (uint t = 0; t + 2 < ic && ok; t++)
			{
				const uint a = ix ? rd_be16 (ix + 2 * t) : t, b = ix ? rd_be16 (ix + 2 * t + 2) : t + 1, c = ix ? rd_be16 (ix + 2 * t + 4) : t + 2;
				if (a >= vc || b >= vc || c >= vc)
					goto bad;
				if (a == b || b == c || a == c)
					continue;
				if (!memcmp (vd + (size_t)a * st + opos, vd + (size_t)b * st + opos, 12) || !memcmp (vd + (size_t)b * st + opos, vd + (size_t)c * st + opos, 12)
					|| !memcmp (vd + (size_t)a * st + opos, vd + (size_t)c * st + opos, 12))
					continue;		// strip joins repeat positions
				const uint p0 = a, p1 = (t & 1) ? c : b, p2 = (t & 1) ? b : c;
#define PET_V(i) vbase + (i), ouv >= 0 ? vbase + (i) : 0, onrm >= 0 ? vbase + (i) : 0
				if (ouv >= 0 && onrm >= 0)
					PET_EMIT ("f %u/%u/%u %u/%u/%u %u/%u/%u\n", PET_V (p0), PET_V (p1), PET_V (p2));
				else if (ouv >= 0)
					PET_EMIT ("f %u/%u %u/%u %u/%u\n", vbase + p0, vbase + p0, vbase + p1, vbase + p1, vbase + p2, vbase + p2);
				else
					PET_EMIT ("f %u %u %u\n", vbase + p0, vbase + p1, vbase + p2);
				ntri++;
			}
			vbase += vc;
		}
		if (!ok || !ntri)
			goto bad;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PANGYA-PET:%s (%u primitives, %u triangles) -> %s/model.obj\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
				arg, ch[5][0], ntri, dest);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, "model.obj", (u8 *)obj, (uint)used);
	}
	goto done;
bad:
	err = skel ? ERR_OK : ERR_NOTHING_TO_DO;
done:
	FREE (obj);
	FREE (raw);
	return err;
}
