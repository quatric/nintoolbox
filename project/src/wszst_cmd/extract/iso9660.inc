// ISO 9660 CD / DVD images (".iso", ".img", ".bin" with "CD001" at 0x8001; Joliet names preferred, Rock Ridge "NM" names otherwise): the directory tree is extracted to
// "<file>.d/<path>"; version suffixes ";1" are dropped. Wii / GameCube discs and other consoles' images are not touched (they lack the "CD001" signature).
typedef struct iso_ctx_t { const u8 *d; size_t n; bool joliet; ccp arg, dest, basedir; uint files; enumError err; } iso_ctx_t;

static void iso_walk (iso_ctx_t *c, uint lba, uint len, const char *prefix, uint level)
{
	const size_t base = (size_t)lba * 2048;
	if (level > 24 || base + len > c->n || len > 0x40000000)
		return;
	for (size_t p = base; p < base + len && c->err == ERR_OK;)
	{
		const uint rl = c->d[p];
		if (!rl)
		{
			p = (p / 2048 + 1) * 2048;
			continue;
		}
		if (p + rl > c->n || rl < 34)
			break;
		const u8 *r = c->d + p;
		const uint ext = r[2] | r[3] << 8 | r[4] << 16 | (uint)r[5] << 24, dlen = r[10] | r[11] << 8 | r[12] << 16 | (uint)r[13] << 24, nl = r[32];
		char name[512];
		size_t k = 0;
		if (c->joliet)
			for (uint i = 0; i + 1 < nl && k < sizeof (name) - 4; i += 2)
			{
				const uint u = r[33 + i] << 8 | r[34 + i];
				if (u < 0x80) name[k++] = u;
				else if (u < 0x800) { name[k++] = 0xc0 | u >> 6; name[k++] = 0x80 | (u & 63); }
				else { name[k++] = 0xe0 | u >> 12; name[k++] = 0x80 | (u >> 6 & 63); name[k++] = 0x80 | (u & 63); }
			}
		else
			for (uint i = 0; i < nl && k < sizeof (name) - 1; i++)
				name[k++] = r[33 + i];
		name[k] = 0;
		const bool dir = (r[25] & 2) != 0;
		const bool dots = !c->joliet ? (nl == 1 && (r[33] == 0 || r[33] == 1)) : (nl == 1 && (r[33] == 0 || r[33] == 1));
		if (!dots)
		{
			if (!c->joliet)
			{
				// Rock Ridge alternate name in the system use area
				size_t su = 33 + nl + ((nl & 1) ? 0 : 1);
				while (su + 5 <= rl)
				{
					if (r[su] == 'N' && r[su + 1] == 'M' && r[su + 2] >= 5 && su + r[su + 2] <= rl && !(r[su + 4] & 0x0e))
					{
						k = r[su + 2] - 5;
						memcpy (name, r + su + 5, k < sizeof (name) - 1 ? k : sizeof (name) - 1);
						name[k < sizeof (name) - 1 ? k : sizeof (name) - 1] = 0;
						break;
					}
					if (!r[su + 2]) break;
					su += r[su + 2];
				}
				char *sc = strrchr (name, ';');
				if (sc) *sc = 0;
				size_t l = strlen (name);
				if (l > 1 && name[l - 1] == '.') name[l - 1] = 0;
			}
			else
			{
				char *sc = strrchr (name, ';');
				if (sc) *sc = 0;
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s%s%s", prefix, name, dir ? "/" : "");
			for (char *q = path; *q; q++)
				if (*q == '\\') *q = '/';
			if (unixarch_safe (name))
			{
				if (dir)
					iso_walk (c, ext, dlen, path, level + 1);
				else if ((size_t)ext * 2048 + dlen <= c->n)
				{
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sEXTRACT ISO9660:%s -> %s/%s%s (%u bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", c->arg, c->dest, c->basedir ? c->basedir : "", path, dlen);
					c->err = shade_write (c->arg, c->dest, c->basedir, path, c->d + (size_t)ext * 2048, dlen);
					c->files++;
				}
			}
		}
		p += rl;
	}
}

static enumError extract_iso9660_file (ccp arg, ccp basedir, uint depth)
{
	if ((!is_ext (arg, ".iso") && !is_ext (arg, ".img") && !is_ext (arg, ".bin")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x8800 && !memcmp (raw + 0x8001, "CD001", 5) && raw[0x8000] == 1)
	{
		iso_ctx_t c = {raw, sz, false, arg, 0, basedir, 0, ERR_OK};
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		c.dest = dest;
		const u8 *root = raw + 0x8000 + 156;
		// Joliet supplementary volume descriptor (escape sequence %/@, %/C or %/E)
		for (size_t v = 0x8800; v + 2048 <= sz && raw[v] != 255 && !memcmp (raw + v + 1, "CD001", 5); v += 2048)
			if (raw[v] == 2 && raw[v + 88] == '%' && raw[v + 89] == '/' && (raw[v + 90] == '@' || raw[v + 90] == 'C' || raw[v + 90] == 'E'))
			{
				root = raw + v + 156;
				c.joliet = true;
				break;
			}
		const uint lba = root[2] | root[3] << 8 | root[4] << 16 | (uint)root[5] << 24, len = root[10] | root[11] << 8 | root[12] << 16 | (uint)root[13] << 24;
		iso_walk (&c, lba, len, "", 0);
		err = c.err ? c.err : c.files ? ERR_OK : ERR_NOTHING_TO_DO;
	}
	FREE (raw);
	return err;
}
