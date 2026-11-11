// JSystem "BCSV" table (Super Mario Galaxy 1/2, Zelda Twilight Princess, Pikmin 2 era games): {u32 entry count, u32 field count, u32 data offset,
// u32 entry size}, then field records {u32 hash, u32 mask, u16 offset, u8 shift, u8 type} and fixed size entries. Field types: 0 s32, 1 char[32],
// 2 f32, 3 u32, 4 s16, 5 s8, 6 u32 offset into the string pool that follows the entries. Values are (raw & mask) >> shift. Field names are only stored as
// hashes (h = h * 31 + c); well known names are restored from a built in list. Big endian (GameCube/Wii) and little endian (Switch ports) are accepted.
// The table is written beside the source as "<name>.csv" whose header cells are "name:type".
typedef struct bcsv_buf_t { char *p; size_t n, cap; } bcsv_buf_t;

static void bcsv_put (bcsv_buf_t *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap)
	{
		b->cap = (b->n + n + 1) * 2 + 4096;
		b->p = REALLOC (b->p, b->cap);
	}
	if (b->p)
	{
		memcpy (b->p + b->n, s, n);
		b->n += n;
	}
}

static void bcsv_put_cell (bcsv_buf_t *b, const char *s, size_t n)
{
	bool q = false;
	for (size_t i = 0; i < n; i++)
		if (s[i] == ',' || s[i] == '"' || s[i] == '\n' || s[i] == '\r')
			q = true;
	if (!q)
	{
		bcsv_put (b, s, n);
		return;
	}
	bcsv_put (b, "\"", 1);
	for (size_t i = 0; i < n; i++)
	{
		if (s[i] == '"')
			bcsv_put (b, "\"", 1);
		bcsv_put (b, s + i, 1);
	}
	bcsv_put (b, "\"", 1);
}

static u32 bcsv_hash (const char *s)
{
	u32 h = 0;
	for (; *s; s++)
		h = h * 31 + (u32)(int)(signed char)*s;
	return h;
}

static ccp bcsv_known_name (u32 hash)
{
	static const char *const names[] = {
		"name", "Name", "ID", "id", "no", "No", "type", "Type", "kind", "Kind", "index", "Index", "label", "Label", "param", "Param",
		"StageName", "ScenarioNo", "ZoneName", "ObjName", "PosX", "PosY", "PosZ", "DirX", "DirY", "DirZ", "SclX", "SclY", "SclZ",
		"pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z", "scale_x", "scale_y", "scale_z", "ModelName", "Message", "MessageId",
		"ClassName", "ParamScale", "Rate", "Num", "Count", "Value", "Flag", "Frame", "Speed", "Size", "Radius", "Height", "Width",
		"Color", "Priority", "Comment", "Description", "FileName", "Path", "Archive", "Resource", "Level", "Hp", "Attack", "Defense",
		"ItemID", "ItemNo", "ObjId", "Obj_arg0", "Obj_arg1", "Obj_arg2", "Obj_arg3", "Obj_arg4", "Obj_arg5", "Obj_arg6", "Obj_arg7",
		"SW_APPEAR", "SW_A", "SW_B", "SW_DEAD", "SW_AWAKE", "CameraSetId", "CastId", "ViewGroupId", "ShapeModelNo", "MapPartsRotateSpeed",
		"MapPartsRotateAxis", "MapPartsRotateType", "MapPartsMoveSpeed", "GroupId", "Layer", "LayerName", "UnitName", "AreaName",
		"PlanetName", "GalaxyName", "ScenarioName", "PowerStarId", "CometType", "Appear", "Condition", "Pos", "Dir", "Scale", 0};
	for (uint i = 0; names[i]; i++)
		if (bcsv_hash (names[i]) == hash)
			return names[i];
	return 0;
}

static enumError extract_bcsv_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bcsv") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	for (int le = 0; le < 2 && sz >= 16 && err == ERR_NOTHING_TO_DO; le++)
	{
#define R32(p) (le ? (u32)(p)[0] | (u32)(p)[1] << 8 | (u32)(p)[2] << 16 | (u32)(p)[3] << 24 : rd_be32 (p))
#define R16(p) (le ? (uint)(p)[0] | (uint)(p)[1] << 8 : (uint)(p)[0] << 8 | (uint)(p)[1])
		const u32 ne = R32 (raw), nf = R32 (raw + 4), doff = R32 (raw + 8), esz = R32 (raw + 12);
		if (nf == 0 || nf > 512 || esz == 0 || esz > 0x10000 || ne > 0x1000000 || 16 + (size_t)nf * 12 > doff || doff > sz
			|| (size_t)ne * esz > sz - doff)
			continue;
		bool ok = true;
		for (uint f = 0; f < nf && ok; f++)
		{
			const u8 *fp = raw + 16 + f * 12;
			ok = fp[11] <= 6 && R16 (fp + 8) < esz;
		}
		if (!ok)
			continue;
		const size_t pool = doff + (size_t)ne * esz;
		bcsv_buf_t b = {0, 0, 0};
		char tmp[512];
		for (uint f = 0; f < nf; f++)
		{
			const u8 *fp = raw + 16 + f * 12;
			const u32 hash = R32 (fp);
			ccp nm = bcsv_known_name (hash);
			int n;
			if (nm)
				n = snprintf (tmp, sizeof (tmp), "%s%s:%u", f ? "," : "", nm, fp[11]);
			else
				n = snprintf (tmp, sizeof (tmp), "%s0x%08X:%u", f ? "," : "", hash, fp[11]);
			bcsv_put (&b, tmp, n);
		}
		bcsv_put (&b, "\n", 1);
		for (u32 e = 0; e < ne; e++)
		{
			for (uint f = 0; f < nf; f++)
			{
				const u8 *fp = raw + 16 + f * 12;
				const u32 mask = R32 (fp + 4);
				const uint off = R16 (fp + 8), shift = fp[10], type = fp[11];
				const u8 *v = raw + doff + (size_t)e * esz + off;
				const size_t room = esz - off;
				int n = 0;
				if (f)
					bcsv_put (&b, ",", 1);
				if (type == 1 || type == 6)
				{
					const char *s = 0;
					size_t maxl = 0;
					if (type == 1)
					{
						s = (const char *)v;
						maxl = room < 32 ? room : 32;
					}
					else if (room >= 4)
					{
						const size_t so = pool + R32 (v);
						if (so < sz)
						{
							s = (const char *)raw + so;
							maxl = sz - so;
						}
					}
					size_t l = 0;
					while (s && l < maxl && s[l])
						l++;
					bcsv_put_cell (&b, s ? s : "", l);
					continue;
				}
				if (type == 2 && room >= 4)
				{
					const u32 u = R32 (v);
					float fl;
					memcpy (&fl, &u, 4);
					n = snprintf (tmp, sizeof (tmp), "%.9g", (double)fl);
				}
				else if (type == 4 && room >= 2)
					n = snprintf (tmp, sizeof (tmp), "%d", (int)(int16_t)((R16 (v) & mask) >> shift));
				else if (type == 5)
					n = snprintf (tmp, sizeof (tmp), "%d", (int)(int8_t)((v[0] & mask) >> shift));
				else if (room >= 4)
				{
					const u32 u = (R32 (v) & mask) >> shift;
					n = type == 0 ? snprintf (tmp, sizeof (tmp), "%d", (int)u) : snprintf (tmp, sizeof (tmp), "%u", u);
				}
				if (n > 0)
					bcsv_put (&b, tmp, n);
			}
			bcsv_put (&b, "\n", 1);
		}
		if (b.p)
		{
			char stem[PATH_MAX], fn[PATH_MAX], dest[PATH_MAX];
			const char *sl = strrchr (arg, '/');
			snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (stem, '.');
			if (dot)
				*dot = 0;
			snprintf (fn, sizeof (fn), "%s.csv", stem);
			beside_source_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT BCSV:%s -> %s/%s%s (%u entries, %u fields)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, ne, nf);
			err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, (u8 *)b.p, (uint)b.n);
		}
		FREE (b.p);
#undef R32
#undef R16
	}
	FREE (raw);
	return err;
}
