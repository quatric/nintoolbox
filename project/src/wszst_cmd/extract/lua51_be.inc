// Big-endian Lua 5.1 bytecode (".compiledLuaWii", ".luac"/".lub" with header 1B 4C 75 61 51 00 and endianness flag 0; Ubisoft Sleepover Party): re-emitted byte-swapped as a
// standard little-endian chunk (flag 1, double numbers) that stock luac/unluac/luadec can read, written as "<name>.luac". Only the 4-byte int / 4-byte size_t / 4-byte instruction / 4-byte
// float number layout (Wii builds) is handled; numbers are widened to doubles.
typedef struct lua51_swap_t
{
	const u8 *s;
	size_t sz, p;
	u8 *o;
	size_t on;
	bool ok;
} lua51_swap_t;

static uint lua51_int (lua51_swap_t *w)
{
	if (w->p + 4 > w->sz)
	{
		w->ok = false;
		return 0;
	}
	const uint v = rd_be32 (w->s + w->p);
	w->p += 4;
	wr_le32 (w->o + w->on, v);
	w->on += 4;
	return v;
}

static void lua51_bytes (lua51_swap_t *w, size_t n, bool rev)
{
	if (!w->ok || n > w->sz - w->p || w->p > w->sz)
	{
		w->ok = false;
		return;
	}
	for (size_t i = 0; i < n; i++)
		w->o[w->on + i] = w->s[w->p + (rev ? n - 1 - i : i)];
	w->p += n;
	w->on += n;
}

static void lua51_string (lua51_swap_t *w)
{
	const uint n = lua51_int (w);
	if (w->ok && n)
		lua51_bytes (w, n, false);
}

static void lua51_func (lua51_swap_t *w, uint depth)
{
	if (depth > 64)
	{
		w->ok = false;
		return;
	}
	lua51_string (w);
	lua51_int (w);
	lua51_int (w);
	lua51_bytes (w, 4, false);
	uint n = lua51_int (w);
	if (n > w->sz)
		w->ok = false;
	for (uint i = 0; w->ok && i < n; i++)
		lua51_int (w);
	n = lua51_int (w);
	if (n > w->sz)
		w->ok = false;
	for (uint i = 0; w->ok && i < n; i++)
	{
		if (w->p >= w->sz)
		{
			w->ok = false;
			break;
		}
		const u8 t = w->s[w->p];
		lua51_bytes (w, 1, false);
		if (t == 1)
			lua51_bytes (w, 1, false);
		else if (t == 3)
		{
			if (w->p + 4 > w->sz)
				w->ok = false;
			else
			{
				const uint fv = rd_be32 (w->s + w->p);
				float f;
				memcpy (&f, &fv, 4);
				const double dv = (double)f;
				memcpy (w->o + w->on, &dv, 8);
				w->p += 4;
				w->on += 8;
			}
		}
		else if (t == 4)
			lua51_string (w);
		else if (t)
			w->ok = false;
	}
	n = lua51_int (w);
	if (n > w->sz)
		w->ok = false;
	for (uint i = 0; w->ok && i < n; i++)
		lua51_func (w, depth + 1);
	n = lua51_int (w);
	if (n > w->sz)
		w->ok = false;
	for (uint i = 0; w->ok && i < n; i++)
		lua51_int (w);
	n = lua51_int (w);
	if (n > w->sz)
		w->ok = false;
	for (uint i = 0; w->ok && i < n; i++)
	{
		lua51_string (w);
		lua51_int (w);
		lua51_int (w);
	}
	n = lua51_int (w);
	if (n > w->sz)
		w->ok = false;
	for (uint i = 0; w->ok && i < n; i++)
		lua51_string (w);
}

static enumError extract_lua51_be_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".compiledLuaWii") && !is_ext (arg, ".luac") && !is_ext (arg, ".lub"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	static const u8 hdr[] = { 0x1b, 'L', 'u', 'a', 0x51, 0x00, 0x00, 4, 4, 4, 4, 0 };
	if (sz > 32 && !memcmp (raw, hdr, sizeof hdr))
	{
		lua51_swap_t w = { raw, sz, sizeof hdr, CALLOC (1, sz + 64), sizeof hdr, true };
		if (w.o)
		{
			memcpy (w.o, hdr, sizeof hdr);
			w.o[6] = 1;
			w.o[10] = 8;
			lua51_func (&w, 0);
			if (w.ok && w.p == sz)
			{
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".luac");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sCONVERT LUA51-BE:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, name);
				err = shade_write (arg, dest, basedir, name, w.o, (uint)w.on);
			}
			FREE (w.o);
		}
	}
	FREE (raw);
	return err;
}
