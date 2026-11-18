// CRI "CVM" images ("*.cvm", Sonic Riders - Shooting Star Story "Image1.cvm"): {"CVMH", ... "ROFSROFSBLD Ver.x" header} padded to 0x1800, followed by a plain ISO9660 ("CRI ROFS") image. The ISO base is the position where "CD001" appears at base+0x8001
// (0x1800 for CVM, 0 for a bare ISO named *.cvm). The directory tree is walked (version suffixes ";1" stripped) and every file is written to "<file>.d/<path>".
typedef struct cvm_ctx_t
{
	FILE *f;
	u64 base;
	ccp arg, dest;
	uint depth, files;
	enumError err;
} cvm_ctx_t;

static void cvm_walk (cvm_ctx_t *c, u32 lba, u32 size, const char *rel, uint level)
{
	if (level > 16 || !size || size > 0x4000000 || c->err)
		return;
	u8 *dir = MALLOC (size);
	if (fseeko (c->f, (off_t)(c->base + (u64)lba * 2048), SEEK_SET) || fread (dir, 1, size, c->f) != size)
	{
		FREE (dir);
		return;
	}
	u32 p = 0;
	while (p + 34 <= size && !c->err)
	{
		const uint rl = dir[p];
		if (!rl)
		{
			p = (p / 2048 + 1) * 2048;
			continue;
		}
		if (p + rl > size || rl < 34)
			break;
		const u8 *r = dir + p;
		const uint nl = r[32];
		if (33 + nl <= rl && !(nl == 1 && r[33] <= 1))
		{
			char name[256];
			uint k = 0;
			for (; k < nl && k < 200; k++)
			{
				const char ch = (char)r[33 + k];
				name[k] = ch == '/' || ch == '\\' || (u8)ch < 0x20 ? '_' : ch;
			}
			name[k] = 0;
			char *sc = strchr (name, ';');
			if (sc)
				*sc = 0;
			if (!name[0] || !strcmp (name, ".") || !strcmp (name, ".."))
				name[0] = '_', name[1] = 0;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s%s%s", rel, rel[0] ? "/" : "", name);
			const u32 el = rd_le32 (r + 2), es = rd_le32 (r + 10);
			if (r[25] & 2)
				cvm_walk (c, el, es, path, level + 1);
			else
			{
				char out[PATH_MAX];
				snprintf (out, sizeof (out), "%s/%s", c->dest, path);
				File_t F;
				c->err = CreateFileOpt (&F, true, out, false, c->arg);
				if (!c->err && F.f && fseeko (c->f, (off_t)(c->base + (u64)el * 2048), SEEK_SET) == 0)
				{
					static u8 buf[1 << 20];
					u32 left = es;
					while (left && !c->err)
					{
						const size_t n = left < sizeof (buf) ? left : sizeof (buf);
						if (fread (buf, 1, n, c->f) != n || fwrite (buf, 1, n, F.f) != n)
							c->err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", out);
						left -= (u32)n;
					}
				}
				ResetFile (&F, opt_preserve);
				c->files++;
			}
		}
		p += rl;
	}
	FREE (dir);
}

static enumError extract_cvm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".cvm"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 hdr[2048];
	u64 base = 0;
	bool found = false;
	static const u64 bases[2] = { 0x1800, 0 };
	for (uint i = 0; i < 2 && !found; i++)
		if (!fseeko (f, (off_t)(bases[i] + 0x8000), SEEK_SET) && fread (hdr, 1, 2048, f) == 2048 && hdr[0] == 1 && !memcmp (hdr + 1, "CD001", 5))
		{
			base = bases[i];
			found = true;
		}
	if (found)
	{
		cvm_ctx_t c;
		memset (&c, 0, sizeof (c));
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		c.f = f; c.base = base; c.arg = arg; c.dest = dest; c.depth = depth; c.err = ERR_OK;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CVM:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		if (!testmode)
		{
			cvm_walk (&c, rd_le32 (hdr + 156 + 2), rd_le32 (hdr + 156 + 10), "", 0);
			err = c.err;
			if (!err && !c.files)
				err = ERR_NOTHING_TO_DO;
			if (!err)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
		else
			err = ERR_OK;
	}
	fclose (f);
	return err;
}
