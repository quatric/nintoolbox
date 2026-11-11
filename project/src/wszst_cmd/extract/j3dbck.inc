// J3D joint animation "BCK" ("J3D1bck1" + ANK1 chunk; GameCube/Wii, big endian). ANK1: u8 loop mode, u8 angle multiplier, u16 duration, u16 joint count,
// u16 scale/rotation/translation table sizes, u32 offsets of {joints, scale floats, rotation s16, translation floats} (relative to the chunk).
// Every joint has 9 tracks (scale, rotation, translation for x, y, z) of {u16 key count, u16 table index, u16 tangent type}; a single key is a constant,
// otherwise keys are {time, value, tangent in[, tangent out]} (3 values, or 4 when the tangent type is 1) in the track's table type.
// Rotation values are s16 where 0x8000 = 180 degrees << angle multiplier. Written as "<name>.csv": joint,channel,time,value (rotations in degrees).
static enumError extract_j3dbck_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bck") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x40 && !memcmp (raw, "J3D1bck1", 8) && !memcmp (raw + 0x20, "ANK1", 4))
	{
		const u8 *a = raw + 0x20;
		const size_t asz = rd_be32 (a + 4);
		const uint mult = a[9], dur = a[10] << 8 | a[11], nj = a[12] << 8 | a[13];
		const uint ns = a[14] << 8 | a[15], nr = a[16] << 8 | a[17], nt = a[18] << 8 | a[19];
		const u32 jo = rd_be32 (a + 20), so = rd_be32 (a + 24), ro = rd_be32 (a + 28), to = rd_be32 (a + 32);
		if (asz >= 0x24 && 0x20 + asz <= sz && jo + (size_t)nj * 0x36 <= asz && so + (size_t)ns * 4 <= asz && ro + (size_t)nr * 2 <= asz
			&& to + (size_t)nt * 4 <= asz && nj <= 4096)
		{
			static const char *const chn[9] = {"scale_x", "rot_x", "trans_x", "scale_y", "rot_y", "trans_y", "scale_z", "rot_z", "trans_z"};
			bcsv_buf_t b = {0, 0, 0};
			char tmp[128];
			int n = snprintf (tmp, sizeof (tmp), "joint,channel,time,value\n# duration %u loop %u\n", dur, a[8]);
			bcsv_put (&b, tmp, n);
			bool ok = true;
			for (uint j = 0; j < nj && ok; j++)
				for (uint c = 0; c < 9 && ok; c++)
				{
					const u8 *t = a + jo + j * 0x36 + c * 6;
					const uint cnt = t[0] << 8 | t[1], idx = t[2] << 8 | t[3], tt = t[4] << 8 | t[5];
					const uint kind = c % 3;	// 0 scale float, 1 rotation s16, 2 translation float
					const uint limit = kind == 0 ? ns : kind == 1 ? nr : nt;
					const uint stride = tt == 1 ? 4 : 3;
					const uint need = cnt == 1 ? 1 : cnt * stride;
					if (idx + need > limit)
					{
						ok = false;
						break;
					}
					for (uint k = 0; k < (cnt ? cnt : 0); k++)
					{
						const u8 *tab = a + (kind == 0 ? so : kind == 1 ? ro : to);
						double time = 0, val;
						const uint at = cnt == 1 ? idx : idx + k * stride;
#define TV(i) (kind == 1 ? (double)(int16_t)(tab[(i) * 2] << 8 | tab[(i) * 2 + 1]) : ({ u32 u_ = rd_be32 (tab + (i) * 4); float f_; memcpy (&f_, &u_, 4); (double)f_; }))
						val = TV (cnt == 1 ? at : at + 1);
						if (cnt != 1)
							time = TV (at);
#undef TV
						if (kind == 1)
							val = val * 180.0 / 32768.0 * (double)(1u << mult);
						n = snprintf (tmp, sizeof (tmp), "%u,%s,%.6g,%.9g\n", j, chn[c], time, val);
						bcsv_put (&b, tmp, n);
					}
				}
			if (ok && b.p)
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
					fprintf (stdlog, "%s%sEXTRACT J3D-BCK:%s -> %s/%s%s (%u joints)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, nj);
				err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, (u8 *)b.p, (uint)b.n);
			}
			FREE (b.p);
		}
	}
	FREE (raw);
	return err;
}
