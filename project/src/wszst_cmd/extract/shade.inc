// Shade / Level-5 "Inazuma Eleven Strikers" containers.
//
// ShadeLz: optional 12-byte header {FC AA 55 A7, u32 size, u32 stored_size}
// (dat.bin members are headerless) then control bytes:
//   1xxyyyyy yyyyyyyy      copy x+4 bytes from y bytes back
//   011xxxxx               copy x bytes from the previous distance
//   0100xxxx / 0101xxxx xx run of byte (x+4 times, 12-bit count if bit 4)
//   000xxxxx / 001xxxxx xx literal run of x bytes
//
// grp/scn/scn_sh/ui/dat/strap ".bin": u32 count, pad, mult, shift, mask;
//   count * u32 at 0x14, offset = (v >> shift) * pad, size = (v & mask) * mult
//   (rounded up to pad). Members are ShadeLz data.
// mcb0.bln / mcb1.bln: mcb0 holds 4096 * {u32 id, u32 offset, u32 size};
//   mcb1 at that offset holds {u32 archive, u32 archive_offset, u32 size} +
//   ShadeLz data, repeated until the end of the range or a 0x7fff marker.

static uint shade_lz_size (const u8 *in, size_t n)
{
	u64 total = 0;
	for (size_t i = 0; i < n;)
	{
		const uint f = in[i++];
		size_t len;
		if (f & 0x80)
		{
			len = ((f >> 5) & 3) + 4;
			i++;
		}
		else if ((f & 0x60) == 0x60)
			len = f & 0x1f;
		else if (f & 0x40)
		{
			if (f & 0x10)
			{
				if (i >= n)
					return 0;
				len = (((size_t)(f & 0xf) << 8) | in[i++]) + 4;
			}
			else
				len = (f & 0xf) + 4;
			i++;
		}
		else
		{
			if (f & 0x20)
			{
				if (i >= n)
					return 0;
				len = ((size_t)(f & 0x1f) << 8) | in[i++];
			}
			else
				len = f;
			i += len;
		}
		total += len;
		if (total > (1u << 29))
			return 0;
	}
	return (uint)total;
}

// Returns a malloc'ed buffer or NULL.
static u8 *shade_lz_decode (const u8 *in, size_t n, uint *out_size)
{
	size_t i = 0;
	uint size;
	if (n >= 12 && !memcmp (in, "\xfc\xaa\x55\xa7", 4))
	{
		size = rd_le32 (in + 4);
		i = 12;
	}
	else
		size = shade_lz_size (in, n);
	if (!size || size > (1u << 29))
		return 0;
	u8 *out = MALLOC (size);
	if (!out)
		return 0;
	uint o = 0, prev = 0;
	while (o < size)
	{
		if (i >= n)
			goto bad;
		const uint f = in[i++];
		uint len = 0;
		if (f & 0x80)
		{
			if (i >= n)
				goto bad;
			len = ((f >> 5) & 3) + 4;
			prev = ((f & 0x1f) << 8) | in[i++];
		}
		else if ((f & 0x60) == 0x60)
			len = f & 0x1f;
		else if (f & 0x40)
		{
			if (f & 0x10)
			{
				if (i >= n)
					goto bad;
				len = (((f & 0xf) << 8) | in[i++]) + 4;
			}
			else
				len = (f & 0xf) + 4;
			if (i >= n || len > size - o)
				goto bad;
			memset (out + o, in[i++], len);
			o += len;
			continue;
		}
		else
		{
			if (f & 0x20)
			{
				if (i >= n)
					goto bad;
				len = ((f & 0x1f) << 8) | in[i++];
			}
			else
				len = f;
			if (len > n - i || len > size - o)
				goto bad;
			memcpy (out + o, in + i, len);
			i += len;
			o += len;
			continue;
		}
		// back reference
		if (len > size - o || !prev || prev > o)
			goto bad;
		for (uint k = 0; k < len; k++, o++)
			out[o] = out[o - prev];
	}
	*out_size = size;
	return out;
bad:
	FREE (out);
	return 0;
}

static enumError shade_write (ccp arg, ccp dest, ccp basedir, ccp name, const u8 *data, uint size)
{
	if (testmode)
		return ERR_OK;
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
	File_t F;
	enumError err = CreateFileOpt (&F, true, path, false, arg);
	if (F.f && size && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
	ResetFile (&F, opt_preserve);
	return err;
}

// One member: decode when ShadeLz decodes, else store as-is.
static enumError shade_member (ccp arg, ccp dest, ccp basedir, ccp name, const u8 *p, size_t n)
{
	uint dsz = 0;
	u8 *dec = shade_lz_decode (p, n, &dsz);
	const enumError err = dec ? shade_write (arg, dest, basedir, name, dec, dsz)
							   : shade_write (arg, dest, basedir, name, p, (uint)n);
	FREE (dec);
	return err;
}

static enumError extract_shadebin_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strcmp (base, "grp.bin") && strcmp (base, "scn.bin") && strcmp (base, "scn_sh.bin")
		&& strcmp (base, "ui.bin") && strcmp (base, "dat.bin") && strcmp (base, "strap.bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = raw_size >= 0x14 ? rd_le32 (raw) : 0, pad = raw_size >= 0x14 ? rd_le32 (raw + 4) : 0;
	const uint mult = raw_size >= 0x14 ? rd_le32 (raw + 8) : 0, shift = raw_size >= 0x14 ? rd_le32 (raw + 12) : 99;
	const uint mask = raw_size >= 0x14 ? rd_le32 (raw + 16) : 0;
	if (!count || count > (raw_size - 0x14) / 4 || !pad || (pad & (pad - 1)) || pad > 0x100000 || !mult
		|| shift > 24 || mask != (1u << shift) - 1)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SHADE-BIN:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	enumError err = ERR_OK;
	uint n = 0;
	for (uint i = 0; i < count && !err; i++)
	{
		const u32 v = rd_le32 (raw + 0x14 + 4 * (size_t)i);
		const u64 off = (u64)(v >> shift) * pad;
		u64 size = (u64)(v & mask) * mult;
		size = (size + pad - 1) & ~(u64)(pad - 1);
		if (!size)
			size = pad;
		if (off >= raw_size)
			continue;
		if (size > raw_size - off)
			size = raw_size - off;
		char name[32];
		snprintf (name, sizeof (name), "%04u.bin", i);
		err = shade_member (arg, dest, basedir, name, raw + off, (size_t)size);
		n++;
	}
	FREE (raw);
	if (!err && !n)
		return ERR_NOTHING_TO_DO;
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

static enumError extract_shadebln_file (ccp arg, ccp basedir, uint depth)
{
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strcmp (base, "mcb0.bln"))
		return ERR_NOTHING_TO_DO;
	char other[PATH_MAX];
	snprintf (other, sizeof (other), "%s", arg);
	other[strlen (other) - 5] = '1';
	u8 *m0 = 0, *m1 = 0;
	size_t s0 = 0, s1 = 0;
	if (LoadFileAlloc (arg, 0, 0, &m0, &s0, 0, 0, 0, false) || LoadFileAlloc (other, 0, 0, &m1, &s1, 0, 0, 0, false)
		|| !m0 || !m1 || s0 % 12)
	{
		FREE (m0);
		FREE (m1);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SHADE-BLN:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, dest);
	enumError err = ERR_OK;
	uint n = 0;
	for (size_t e = 0; e + 12 <= s0 && !err; e += 12)
	{
		const u64 off = rd_le32 (m0 + e + 4), size = rd_le32 (m0 + e + 8);
		if (!size || off >= s1)
			continue;
		const u64 end = off + size < s1 ? off + size : s1;
		u64 p = off;
		for (uint k = 0; p + 12 <= end && rd_le32 (m1 + p) != 0x7fff && !err; k++)
		{
			const u64 sz = rd_le32 (m1 + p + 8);
			if (sz > end - p - 12)
				break;
			char name[64];
			snprintf (name, sizeof (name), "%04u/%03u_a%u_%x.bin", (uint)(e / 12), k, rd_le32 (m1 + p),
				rd_le32 (m1 + p + 4));
			err = shade_member (arg, dest, basedir, name, m1 + p + 12, (size_t)sz);
			n++;
			p += 12 + sz;
		}
	}
	FREE (m0);
	FREE (m1);
	if (!err && !n)
		return ERR_NOTHING_TO_DO;
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
