// "MOEG" (3D geometry of the Battle of Giants engine, about 1,500 files in "3d/geometry" of each game; extension-less): "MOEG", u32 size of the rest, then a raw Nintendo DS GX display list: little endian words of up to 4 packed command bytes followed by their parameter words (checked: all 1,456 geometry files of Battle of Giants Fight for Survival parse to the end with only known commands). Commands: 0x40 BEGIN_VTXS (0 triangles, 1 quads, 2 triangle strip, 3 quad strip), 0x41 END_VTXS, 0x22 TEXCOORD (2 * s16, 12.4 fixed), 0x23 VTX_16 (4.12 fixed), 0x24 VTX_10 (4.6), 0x25 / 0x26 / 0x27 VTX_XY / XZ / YZ, 0x28 VTX_DIFF; the others are skipped by their parameter count (colors, normals, matrix commands). Written as "<file>.d/<name>.obj" with one vertex per emitted corner (faces are interleaved with the vertices); matrix commands (MTX_SCALE, translate, push/pop) are ignored, so parts that the game places with them stay at the origin; the texture coordinates are in texels (not divided by the texture size, which is not stored here).
static const u8 moeg_plen[0x73] = { [0x10] = 1, [0x12] = 1, [0x13] = 1, [0x14] = 1, [0x16] = 16, [0x17] = 12, [0x18] = 16, [0x19] = 12, [0x1a] = 9, [0x1b] = 3, [0x1c] = 3, [0x20] = 1, [0x21] = 1, [0x22] = 1, [0x23] = 2, [0x24] = 1, [0x25] = 1, [0x26] = 1,
	[0x27] = 1, [0x28] = 1, [0x29] = 1, [0x2a] = 1, [0x2b] = 1, [0x30] = 1, [0x31] = 1, [0x32] = 1, [0x33] = 1, [0x34] = 32, [0x40] = 1, [0x50] = 1, [0x60] = 1, [0x70] = 3, [0x71] = 2, [0x72] = 1 };

static bool moeg_known (uint c)
{
	return c == 0 || c == 0x11 || c == 0x15 || c == 0x41 || (c < sizeof (moeg_plen) && moeg_plen[c]);
}

static void moeg_tri (sumo_buf_t *b, uint a, uint c, uint d)
{
	sumo_printf (b, "f %u/%u %u/%u %u/%u\n", a, a, c, c, d, d);
}

static enumError extract_moeg_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	sumo_buf_t b = { 0, 0, 0, true };
	(void)depth;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 12 || memcmp (raw, "MOEG", 4) || (size_t)rd_le32 (raw + 4) + 8 != sz || (sz & 3))
		goto done;
	int vx = 0, vy = 0, vz = 0, ts = 0, tt = 0;
	uint nv = 0, mode = 99, pn = 0, pi[4];
	for (size_t p = 8; p < sz;)
	{
		const u8 *cmd = raw + p;
		p += 4;
		for (uint k = 0; k < 4; k++)
		{
			const uint c = cmd[k];
			if (!c)
				continue;
			if (!moeg_known (c))
				goto done;
			const uint np = c == 0x11 || c == 0x15 || c == 0x41 ? 0 : moeg_plen[c];
			if (p + 4 * (size_t)np > sz)
				goto done;
			const u8 *a = raw + p;
			p += 4 * (size_t)np;
			bool vtx = true;
			switch (c)
			{
				case 0x40: mode = rd_le32 (a), pn = 0, vtx = false; break;
				case 0x41: mode = 99, vtx = false; break;
				case 0x22: ts = (int16_t)rd_le16 (a), tt = (int16_t)rd_le16 (a + 2), vtx = false; break;
				case 0x23: vx = (int16_t)rd_le16 (a), vy = (int16_t)rd_le16 (a + 2), vz = (int16_t)rd_le16 (a + 4); break;
				case 0x24:
				{
					const u32 w = rd_le32 (a);
					vx = ((int)(w & 0x3ff) << 22 >> 22) * 64, vy = ((int)(w >> 10 & 0x3ff) << 22 >> 22) * 64, vz = ((int)(w >> 20 & 0x3ff) << 22 >> 22) * 64;
					break;
				}
				case 0x25: vx = (int16_t)rd_le16 (a), vy = (int16_t)rd_le16 (a + 2); break;
				case 0x26: vx = (int16_t)rd_le16 (a), vz = (int16_t)rd_le16 (a + 2); break;
				case 0x27: vy = (int16_t)rd_le16 (a), vz = (int16_t)rd_le16 (a + 2); break;
				case 0x28:
				{
					const u32 w = rd_le32 (a);
					vx += (int)(w & 0x3ff) << 22 >> 22, vy += (int)(w >> 10 & 0x3ff) << 22 >> 22, vz += (int)(w >> 20 & 0x3ff) << 22 >> 22;
					break;
				}
				default: vtx = false; break;
			}
			if (!vtx || mode > 3)
				continue;
			sumo_printf (&b, "v %.6f %.6f %.6f\nvt %.4f %.4f\n", vx / 4096.0, vy / 4096.0, vz / 4096.0, ts / 16.0, tt / 16.0);
			pi[pn++ & 3] = ++nv;
			if (mode == 0 && pn == 3)
				moeg_tri (&b, pi[0], pi[1], pi[2]), pn = 0;
			else if (mode == 1 && pn == 4)
				moeg_tri (&b, pi[0], pi[1], pi[2]), moeg_tri (&b, pi[0], pi[2], pi[3]), pn = 0;
			else if (mode == 2 && pn >= 3)
			{
				const uint x = pi[(pn - 3) & 3], y = pi[(pn - 2) & 3], z = pi[(pn - 1) & 3];
				if (pn & 1)
					moeg_tri (&b, x, y, z);
				else
					moeg_tri (&b, y, x, z);
			}
			else if (mode == 3 && pn >= 4 && !(pn & 1))
			{
				const uint w0 = pi[(pn - 4) & 3], w1 = pi[(pn - 3) & 3], w2 = pi[(pn - 2) & 3], w3 = pi[(pn - 1) & 3];
				moeg_tri (&b, w0, w1, w3);
				moeg_tri (&b, w0, w3, w2);
			}
		}
	}
	if (!nv || !b.ok)
		goto done;
	if (b.ok && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s.obj", sl ? sl + 1 : arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MOEG:%s (%u vertices) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nv, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
done:
	FREE (b.p);
	FREE (raw);
	return err;
}
