// AiLive LiveMove classifier databases (".lmc": Top Spin 3/4, Don King Boxing; little endian): u32 file size, then a serialised object stream: tagged records whose type and
// member names are u16-length-prefixed ASCII strings ("readableHeader:", "gaussianXMotionClassifierPair", "nameMap", gesture names such as "ThrowBall", "motion",
// "matrixDynamic<float>") interleaved with float matrices (the trained models). Only the string skeleton is exported: "<name>.txt" with "offset string" lines (the
// gesture names and classifier layout); the float model data is not interpreted.
static enumError extract_ailive_lmc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".lmc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x40 && rd_le32 (raw) == sz && !memcmp (raw + 0x13, "readableHeader:", 15))
	{
		char *txt = CALLOC (sz * 2 + 64, 1);
		if (txt)
		{
			size_t o = 0, cnt = 0;
			for (size_t i = 0x10; i + 6 < sz;)
			{
				const size_t l = raw[i] | raw[i + 1] << 8;
				bool ok = l >= 4 && l <= 160 && i + 2 + l <= sz;
				for (size_t k = 0; ok && k < l; k++)
				{
					const u8 c = raw[i + 2 + k];
					if (c < 0x20 || c > 0x7e)
						if (!(c == '\n' && k + 1 < l) && !(k + 1 == l && !c))
							ok = false;
				}
				if (ok)
				{
					o += (size_t)sprintf (txt + o, "%06zx ", i + 2);
					for (size_t k = 0; k < l && raw[i + 2 + k]; k++)
						txt[o++] = raw[i + 2 + k] == '\n' ? ' ' : (char)raw[i + 2 + k];
					txt[o++] = '\n';
					cnt++;
					i += 2 + l;
				}
				else
					i++;
			}
			if (cnt)
			{
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".txt");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT AILIVE-LMC:%s (%zu strings) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest, name);
				err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)o);
			}
			FREE (txt);
		}
	}
	FREE (raw);
	return err;
}
