// Purr Pals "*.rsc" resource containers: big-endian {"RSC0", u32 version 2, u32 block count, u32 header size 0x20, u32 total size}, then blocks {char tag[4], u32 block length incl. header, u32 name hash, ...}.
// Tags: TEX0 (u32 flags, u16 fmt|mips code 0x17xx = RGBA8 / 0x18xx = CMPR, u16 w, u16 h, u32 data size, u32 data offset 0x20; mip chain follows the top level), GRP0 (u32 hash, flags, child count, child offset 0x20, then child blocks), STR0, SND0, ANM0, MDL0, COL0, BIN0, FNT0.
// TEX0 top levels become single-image TPLs ("NNN_TEX0_<hash>.tpl"); every other block is written raw to "NNN_<tag>_<hash>.bin" and GRP0 children go to a "NNN_GRP0_<hash>" subdirectory.
static enumError rsc0_write_dsp (const u8 *pl, size_t plen, uint rate, ccp path, ccp arg)
{
	const uint frames = (uint)((plen - 0x40) / 8);
	u8 *dsp = CALLOC (1, 0x60 + (size_t)frames * 8);
	wr_be32 (dsp, frames * 14);
	wr_be32 (dsp + 4, frames * 16);
	wr_be32 (dsp + 8, rate);
	wr_be32 (dsp + 0x10, 2);
	wr_be32 (dsp + 0x14, frames * 16 - 1);
	wr_be32 (dsp + 0x18, 2);
	memcpy (dsp + 0x1c, pl, 0x20);
	dsp[0x3f] = pl[0x40];
	memcpy (dsp + 0x60, pl + 0x40, (size_t)frames * 8);
	File_t F;
	enumError err = CreateFileOpt (&F, true, path, false, arg);
	if (F.f && fwrite (dsp, 1, 0x60 + (size_t)frames * 8, F.f) != 0x60 + (size_t)frames * 8)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing DSP failed: %s\n", path);
	ResetFile (&F, opt_preserve);
	FREE (dsp);
	return err;
}

static enumError rsc0_blocks (const u8 *d, size_t sz, size_t p, size_t end, uint cnt, uint idx0, ccp dest, ccp arg, uint depth, size_t *soff)
{
	enumError err = ERR_OK;
	for (uint i = 0; !err && i < cnt && p + 0x20 <= end; i++)
	{
		const u32 len = be32 (d + p + 4);
		if (len < 0x20 || p + len > end)
			break;
		const u8 *b = d + p;
		char path[PATH_MAX];
		if (!memcmp (b, "TEX0", 4))
		{
			const uint code = be16 (b + 0x10), w = be16 (b + 0x14), h = be16 (b + 0x16), dsz = be32 (b + 0x18), doff = be32 (b + 0x1c);
			const uint fmt = code >> 8 == 0x17 ? 6 : code >> 8 == 0x18 ? 14 : 0;
			const size_t dl = fmt == 6 ? (size_t)w * h * 4 : (size_t)w * h / 2;
			if (fmt && w >= 8 && h >= 8 && doff == 0x20 && dsz >= dl && 0x20 + dl <= len)
			{
				u8 *tpl = MALLOC (0x60 + dl);
				memset (tpl, 0, 0x60);
				wr_be32 (tpl, 0x0020af30);
				wr_be32 (tpl + 4, 1);
				wr_be32 (tpl + 8, 0x0c);
				wr_be32 (tpl + 0x0c, 0x14);
				tpl[0x14] = h >> 8;
				tpl[0x15] = h;
				tpl[0x16] = w >> 8;
				tpl[0x17] = w;
				wr_be32 (tpl + 0x18, fmt);
				wr_be32 (tpl + 0x1c, 0x60);
				wr_be32 (tpl + 0x28, 1);
				wr_be32 (tpl + 0x2c, 1);
				memcpy (tpl + 0x60, b + 0x20, dl);
				snprintf (path, sizeof (path), "%s/%03u_TEX0_%08x.tpl", dest, idx0 + i, be32 (b + 8));
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (tpl, 1, 0x60 + dl, F.f) != 0x60 + dl)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", 0x60 + dl, path);
				ResetFile (&F, opt_preserve);
				FREE (tpl);
				p += len;
				continue;
			}
		}
		if (!memcmp (b, "SND0", 4) && be32 (b + 0x0c) == 0x200000 && len == 0x60 && soff && *soff)
		{
			// streamed sound: the DSP data follows the block table of this file, each stream 0x20-aligned
			const size_t size = be32 (b + 0x10), off = *soff;
			*soff = (off + size + 0x1f) & ~(size_t)0x1f;
			if (be32 (b + 0x14) == 0x01100201 && size > 0x48 && off + size <= sz)
			{
				snprintf (path, sizeof (path), "%s/%03u_SND0_%08x.dsp", dest, idx0 + i, be32 (b + 8));
				err = rsc0_write_dsp (d + off, size, be16 (b + 0x18), path, arg);
				p += len;
				continue;
			}
		}
		if (!memcmp (b, "SND0", 4) && be32 (b + 0x0c) == 0x100000 && be32 (b + 0x14) == 0x01100201 && len > 0x20 + 0x48)
		{
			snprintf (path, sizeof (path), "%s/%03u_SND0_%08x.dsp", dest, idx0 + i, be32 (b + 8));
			err = rsc0_write_dsp (b + 0x20, len - 0x20, be16 (b + 0x18), path, arg);
			p += len;
			continue;
		}
		if (!memcmp (b, "GRP0", 4) && len >= 0x30 && be32 (b + 0x14) == 0x20 && depth < 8)
		{
			snprintf (path, sizeof (path), "%s/%03u_GRP0_%08x", dest, idx0 + i, be32 (b + 8));
			err = rsc0_blocks (d, sz, p + 0x20, p + len, be32 (b + 0x10), 0, path, arg, depth + 1, 0);
			p += len;
			continue;
		}
		snprintf (path, sizeof (path), "%s/%03u_%.4s_%08x.bin", dest, idx0 + i, (ccp)b, be32 (b + 8));
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (b + 0x20, 1, len - 0x20, F.f) != len - 0x20)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len - 0x20, path);
		ResetFile (&F, opt_preserve);
		p += len;
	}
	return err;
}

static enumError extract_rsc0_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".rsc"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x40 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz || memcmp (d, "RSC0", 4) || be32 (d + 16) > (u32)sz)
			goto done;
		const bool dirmode = be32 (d + 12) != 0x20;
		uint nblk = 0;
		if (dirmode)
		{
			nblk = be32 (d + 8);
			if (!nblk || nblk > 100000 || 0x20 + (u64)nblk * 16 > (u64)sz)
				goto done;
			for (uint i = 0; i < nblk; i++)
			{
				const u8 *e = d + 0x20 + i * 16;
				if ((u64)be32 (e + 8) + be32 (e + 12) > (u64)sz || be32 (e + 12) < 0x20 || e[0] < 'A' || e[0] > 'Z')
					goto done;
			}
		}
		for (size_t q = 0x20; !dirmode && q + 0x20 <= (size_t)sz;)
		{
			const u32 l = be32 (d + q + 4);
			if (l < 0x20 || q + l > (size_t)sz || d[q] < 'A' || d[q] > 'Z')
				break;
			q += l;
			nblk++;
			if (q == (size_t)sz)
				break;
		}
		if (!nblk)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RSC0:%s (%u blocks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nblk, dest);
		err = ERR_OK;
		if (!testmode)
		{
			if (dirmode)
			{
				for (uint i = 0; !err && i < nblk; i++)
				{
					const u8 *e = d + 0x20 + i * 16;
					err = rsc0_blocks (d, (size_t)sz, be32 (e + 8), (size_t)be32 (e + 8) + be32 (e + 12), 1, i, dest, arg, 0, 0);
				}
			}
			else
			{
				size_t soff = be32 (d + 0x10) < (u32)sz ? be32 (d + 0x10) : 0;
				err = rsc0_blocks (d, (size_t)sz, 0x20, (size_t)sz, nblk, 0, dest, arg, 0, &soff);
			}
			if (!err)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
