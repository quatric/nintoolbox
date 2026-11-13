// Manhunt 2 (Wii) world geometry (".bsp", the unpacked "WRLD" of an MH2Z file, big endian), vertices only. The world references vertex buffers through 24-byte records {u16 id, u16 users,
// u32 size, u32 data offset, 2 x u32 0}; a buffer is an array of 16-byte entries {f32 x, y, z, 0}. Every buffer found this way is written as one "o" group of "v" lines to
// "<name>.obj". The polygon and index tables of the world are not decoded, so the OBJ holds points and no faces.
// position of vertex i, component k of a stream at q (vertex size vs, data starting al bytes after the 4-byte stream header): f32 (format 0x40) or s16 with the binary point given
// by the low nibble of the format byte
static float mh2w_pos (const u8 *c, size_t q, uint vs, uint al, uint i, int k)
{
	if (c[q + 2] == 0x40)
		return mh2inst_f (c + q + 4 + al + (size_t)vs * i + 4 * k, true);
	return (float)(s16)rd_be16 (c + q + 4 + al + (size_t)vs * i + 2 * k) / (float)(1u << (c[q + 2] & 15));
}

// checks a display list (GX draws 0x80 quads, 0x90 triangles, 0x98 strips, 0xa0 fans; zero bytes are padding) whose vertices have a constant size; the first u16 of each vertex is a
// position index below cnt. Returns the vertex size, or 0.
static uint mh2w_dl_stride (const u8 *c, size_t dl, size_t dsz, uint cnt)
{
	for (uint stride = 2; stride <= 26; stride++)
	{
		size_t o = dl;
		bool ok = true;
		uint draws = 0;
		while (o < dl + dsz && ok)
		{
			const u8 op = c[o];
			if (!op)
			{
				o++;
				continue;
			}
			if (op != 0x80 && op != 0x90 && op != 0x98 && op != 0xa0)
				break;
			const uint n = rd_be16 (c + o + 1);
			o += 3;
			if (!n || o + (size_t)n * stride > dl + dsz)
			{
				ok = false;
				break;
			}
			for (uint i = 0; i < n; i++)
				if (rd_be16 (c + o + (size_t)i * stride) >= cnt)
					ok = false;
			o += (size_t)n * stride;
			draws++;
		}
		if (ok && draws && o >= dl + dsz)
			return stride;
	}
	return 0;
}

static enumError extract_mh2_wrld_file (ccp arg, ccp basedir, uint depth)
{
	const bool isdff = is_ext (arg, ".dff");
	if (!isdff && !is_ext (arg, ".bsp"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x100 || memcmp (c, isdff ? "CLMP" : "WRLD", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	uint groups = 0;
	size_t lim = rd_be32 (c + 8);
	if (lim > cs || lim < 0x100)
		lim = cs;
	float bmin[3], bmax[3];
	for (int k = 0; k < 3; k++)
	{
		bmin[k] = isdff ? -1e4f : mh2inst_f (c + 0x30 + 4 * k, true) - 5.0f;
		bmax[k] = isdff ? 1e4f : mh2inst_f (c + 0x40 + 4 * k, true) + 5.0f;
	}
	// Sector position streams: {u16 count, format (0x40 f32 or 0x3n s16), 3 or 6 components} followed by the vertices. The sector's display lists are listed by 16-byte table
	// entries {u32 offset from the sector start (stream header - 0x38), u32 size, u32 0x01000000, u32 0x150100nn}; a display list starts 0x1c bytes after its offset. The
	// vertices of s16 streams start at an unknown 2-byte aligned offset: the one with the shortest distance between consecutive display list indices is used.
	size_t sq[512];
	uint scnt[512], svs[512], ns = 0;
	for (size_t q = 0x40; q + 4 <= lim && ns < 512; q += 4)
	{
		const uint cnt = rd_be16 (c + q);
		const uint fmt = c[q + 2];
		const uint comps = c[q + 3];
		const uint vs = fmt == 0x40 ? 12 : 2 * comps;
		if (cnt < 3 || (fmt != 0x40 && (fmt >> 4) != 3) || (comps != 3 && comps != 6) || (fmt == 0x40 && comps != 3) || q < 0x38 + 0x40
			|| rd_be32 (c + q - 0x38) != 0x00b749e0 || q + 4 + (size_t)cnt * vs > cs)
			continue;
		bool good = true;
		uint nonzero = 0;
		for (uint i = 0; i < cnt && good; i++)
			for (int k = 0; k < 3 && good; k++)
			{
				const float f = mh2w_pos (c, q, vs, 0, i, k);
				if (fmt == 0x40 && !(f >= bmin[k] && f <= bmax[k]))
					good = false;
				if (f != 0.0f)
					nonzero++;
			}
		if (!good || nonzero < cnt)
			continue;
		sq[ns] = q;
		scnt[ns] = cnt;
		svs[ns++] = vs;
		q += (size_t)cnt * vs;
	}
	uint vtotal = 0;
	for (uint si = 0; si < ns; si++)
	{
		const size_t q = sq[si];
		const uint cnt = scnt[si], vs = svs[si];
		const size_t base = q - 0x38;
		const size_t tend = si + 1 < ns ? sq[si + 1] : lim;
		size_t dls[256], dss[256];
		uint dst[256], nd = 0;
		for (size_t t = q + 4 + (size_t)cnt * vs; t + 16 <= tend && nd < 256; t += 4)
		{
			const bool marked = rd_be32 (c + t + 8) == 0x01000000 && (rd_be32 (c + t + 12) >> 8) == 0x150100;
			if (!marked && (rd_be32 (c + t + 8) || rd_be32 (c + t + 12)))
				continue;
			const size_t dl = base + rd_be32 (c + t) + 0x1c, dsz = rd_be32 (c + t + 4);
			if (!dsz || dsz % 32 || dl + dsz > cs)
				continue;
			bool dup = false;
			for (uint e = 0; e < nd; e++)
				if (dls[e] == dl)
					dup = true;
			if (dup)
				continue;
			const uint stride = mh2w_dl_stride (c, dl, dsz, cnt);
			if (!stride)
				continue;
			dls[nd] = dl;
			dss[nd] = dsz;
			dst[nd++] = stride;
		}
		uint al = 0;
		if (c[q + 2] != 0x40 && nd)
		{
			double best = 1e30;
			for (uint a2 = 0; a2 <= 10; a2 += 2)
			{
				if (q + 4 + a2 + (size_t)cnt * vs > cs)
					break;
				double sum = 0;
				uint npairs = 0;
				for (uint d = 0; d < nd && npairs < 20000; d++)
				{
					size_t o = dls[d];
					while (o < dls[d] + dss[d] && npairs < 20000)
					{
						const u8 op = c[o];
						if (!op)
						{
							o++;
							continue;
						}
						const uint n = rd_be16 (c + o + 1);
						o += 3;
						for (uint i = 0; i + 1 < n; i++)
						{
							const uint i0 = rd_be16 (c + o + (size_t)i * dst[d]), i1 = rd_be16 (c + o + (size_t)(i + 1) * dst[d]);
							double e = 0;
							for (int k = 0; k < 3; k++)
							{
								const double dd = (double)mh2w_pos (c, q, vs, a2, i0, k) - mh2w_pos (c, q, vs, a2, i1, k);
								e += dd * dd;
							}
							sum += sqrt (e);
							npairs++;
						}
						o += (size_t)n * dst[d];
					}
				}
				if (npairs && sum / npairs < best)
				{
					best = sum / npairs;
					al = a2;
				}
			}
		}
		sumo_printf (&b, "o sector%04u_%08x\n", groups++, (uint)q);
		for (uint i = 0; i < cnt; i++)
			sumo_printf (&b, "v %g %g %g\n", mh2w_pos (c, q, vs, al, i, 0), mh2w_pos (c, q, vs, al, i, 1), mh2w_pos (c, q, vs, al, i, 2));
		for (uint d = 0; d < nd; d++)
		{
			size_t o = dls[d];
			const uint stride = dst[d];
			while (o < dls[d] + dss[d])
			{
				const u8 op = c[o];
				if (!op)
				{
					o++;
					continue;
				}
				const uint n = rd_be16 (c + o + 1);
				o += 3;
				uint ix[4096];
				const uint m = n < 4096 ? n : 4096;
				for (uint i = 0; i < m; i++)
					ix[i] = vtotal + 1 + rd_be16 (c + o + (size_t)i * stride);
				o += (size_t)n * stride;
				if (op == 0x90)
					for (uint i = 0; i + 2 < m; i += 3)
						sumo_printf (&b, "f %u %u %u\n", ix[i], ix[i + 1], ix[i + 2]);
				else if (op == 0x98)
					for (uint i = 0; i + 2 < m; i++)
						sumo_printf (&b, "f %u %u %u\n", ix[i], i & 1 ? ix[i + 2] : ix[i + 1], i & 1 ? ix[i + 1] : ix[i + 2]);
				else if (op == 0xa0)
					for (uint i = 1; i + 1 < m; i++)
						sumo_printf (&b, "f %u %u %u\n", ix[0], ix[i], ix[i + 1]);
				else
					for (uint i = 0; i + 3 < m; i += 4)
						sumo_printf (&b, "f %u %u %u %u\n", ix[i], ix[i + 1], ix[i + 2], ix[i + 3]);
			}
		}
		vtotal += cnt;
	}
	u32 seen[512];
	uint nseen = 0;
	for (size_t o = 0x40; o + 16 <= lim; o += 4)
		for (int variant = 0; variant < 2; variant++)
		{
			const u32 w1 = rd_be32 (c + o + 4), w2 = rd_be32 (c + o + 8);
			const u32 size = variant ? w2 : w1, off = variant ? w1 : w2;
			if (!size || size % 16 || size > 0x400000 || off < 0x40 || (size_t)off + size > cs)
				continue;
			uint k2;
			for (k2 = 0; k2 < nseen && seen[k2] != off; k2++)
				;
			if (k2 < nseen || nseen == 512)
				continue;
			bool good = true;
			uint nonzero = 0;
			for (u32 i = 0; i < size && good; i += 16)
			{
				const u8 *v = c + off + i;
				if (rd_be32 (v + 12))
					good = false;
				for (int k = 0; k < 3 && good; k++)
				{
					const float f = mh2inst_f (v + 4 * k, true);
					if (!(f >= bmin[k] && f <= bmax[k]))
						good = false;
					if (f != 0.0f)
						nonzero++;
				}
			}
			if (!good || !nonzero)
				continue;
			seen[nseen++] = off;
			sumo_printf (&b, "o buffer%04u_%08x\n", groups++, off);
			for (u32 i = 0; i < size; i += 16)
				sumo_printf (&b, "v %g %g %g\n", mh2inst_f (c + off + i, true), mh2inst_f (c + off + i + 4, true), mh2inst_f (c + off + i + 8, true));
		}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && groups && b.n < 0xffffffffu)
	{
		char name[PATH_MAX];
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		strcat (name, ".obj");
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		if (entries && OwnedEntryAdd (entries, 0, name, (u8 *)b.p, (uint)b.n))
			err = write_owned_entries (arg, basedir, depth, isdff ? "MH2-DFF" : "MH2-WRLD", entries, 1);
		else
			FREE (entries);
	}
	else
		FREE (b.p);
	return err;
}
