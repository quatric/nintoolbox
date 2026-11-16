// CRI Atom cue-sheet banks (".csb": Pandora's Tower, Ghost Squad; Wii), big-endian "@UTF" tables. The top table "TBLCSB" has rows {name, ttype, utf} whose
// "utf" blobs are nested @UTF tables: "INFO", "CUE", "SYNTH", "SOUND_ELEMENT" (table "TBLSDL": one row per waveform {name, data (blob), ...}), "ISAAC",
// "VOICE_LIMIT_GROUP". Each waveform's data blob is again an @UTF table, here "ADPCM_WII" {sfreq, nsmpl, nch, lpflg, data (blob of DSP-ADPCM frames), header
// (blob of the 0x60 byte DSP header)}. Header + data make a complete ".dsp" file, written as "<last part of the wave name>.dsp" (repeated names get "~<n>")
// in "<file>.d"; the DSP handler turns them into WAV. The cue / synth tables (names, volumes, envelopes) are not decoded.
typedef struct cricsb_utf_t
{
	const u8 *d;
	size_t sz, base;
	u32 ro, so, dofs, name;
	uint nc, rs, nr;
	struct { u32 name; u8 flag; size_t cpos; uint size; size_t rpos; } c[24];
} cricsb_utf_t;

static uint cricsb_utf_tsize (uint t)
{
	switch (t)
	{
	case 0: case 1: return 1;
	case 2: case 3: return 2;
	case 4: case 5: case 8: case 0xa: return 4;
	case 6: case 7: case 0xb: return 8;
	}
	return 0;
}

static bool cricsb_utf_open (cricsb_utf_t *u, const u8 *d, size_t sz, size_t base)
{
	memset (u, 0, sizeof (*u));
	if (base + 0x20 > sz || memcmp (d + base, "@UTF", 4))
		return false;
	const size_t end = base + 8 + (size_t)rd_be32 (d + base + 4);
	if (end > sz)
		return false;
	u->d = d, u->sz = end, u->base = base;
	const u8 *h = d + base + 8;
	u->ro = rd_be16 (h + 2) + (u32)base + 8, u->so = rd_be32 (h + 4) + (u32)base + 8, u->dofs = rd_be32 (h + 8) + (u32)base + 8, u->name = rd_be32 (h + 12);
	u->nc = rd_be16 (h + 16), u->rs = rd_be16 (h + 18), u->nr = rd_be32 (h + 20);
	if (u->nc > 24 || u->ro > end || u->so > end || u->dofs > end || (u64)u->ro + (u64)u->rs * u->nr > end)
		return false;
	size_t p = base + 8 + 0x18;
	uint rp = 0;
	for (uint i = 0; i < u->nc; i++)
	{
		if (p + 5 > end)
			return false;
		const u8 f = d[p++];
		u->c[i].flag = f;
		if (f & 0x10)
			u->c[i].name = rd_be32 (d + p), p += 4;
		else
			u->c[i].name = 0xffffffff;
		const uint t = f & 15, sz2 = cricsb_utf_tsize (t), st = f >> 5;
		if (!sz2)
			return false;
		u->c[i].size = sz2;
		if (st == 3)
			u->c[i].cpos = p, p += sz2;
		else if (st == 2)
			u->c[i].rpos = rp, rp += sz2, u->c[i].cpos = 0;
		else
			u->c[i].cpos = 0;
	}
	return rp <= u->rs;
}

static ccp cricsb_utf_str (const cricsb_utf_t *u, u32 off)
{
	return u->so + off < u->sz ? (ccp)u->d + u->so + off : "";
}

static int cricsb_utf_col (const cricsb_utf_t *u, ccp name)
{
	for (uint i = 0; i < u->nc; i++)
		if (u->c[i].name != 0xffffffff && !strcmp (cricsb_utf_str (u, u->c[i].name), name))
			return (int)i;
	return -1;
}

// the raw bytes of a stored / constant column cell (0 when the column is absent / defaulted)
static const u8 *cricsb_utf_cell (const cricsb_utf_t *u, int col, uint row)
{
	if (col < 0)
		return 0;
	const uint st = u->c[col].flag >> 5;
	if (st == 3)
		return u->d + u->c[col].cpos;
	if (st == 2)
		return u->d + u->ro + (size_t)u->rs * row + u->c[col].rpos;
	return 0;
}

static bool cricsb_utf_blob (const cricsb_utf_t *u, int col, uint row, size_t *pos, size_t *len)
{
	const u8 *c = cricsb_utf_cell (u, col, row);
	if (!c || (u->c[col].flag & 15) != 0xb)
		return false;
	const u64 o = rd_be32 (c), l = rd_be32 (c + 4);
	if (u->dofs + o + l > u->sz)
		return false;
	*pos = u->dofs + (size_t)o, *len = (size_t)l;
	return true;
}

static enumError extract_cricsb_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".csb") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	cricsb_utf_t top, el;
	if (!cricsb_utf_open (&top, raw, sz, 0) || strcmp (cricsb_utf_str (&top, top.name), "TBLCSB"))
		goto done;
	{
		const int cn = cricsb_utf_col (&top, "name"), cu = cricsb_utf_col (&top, "utf");
		size_t ep = 0, el_len = 0;
		bool found = false;
		for (uint r = 0; r < top.nr && cn >= 0 && cu >= 0; r++)
			if (!strcmp (cricsb_utf_str (&top, rd_be32 (cricsb_utf_cell (&top, cn, r))), "SOUND_ELEMENT") && cricsb_utf_blob (&top, cu, r, &ep, &el_len))
				found = true;
		if (!found || !cricsb_utf_open (&el, raw, sz, ep))
			goto done;
		const int en = cricsb_utf_col (&el, "name"), ed = cricsb_utf_col (&el, "data");
		if (en < 0 || ed < 0)
			goto done;
		// collect the waveforms that are Wii ADPCM
		uint good = 0;
		for (uint r = 0; r < el.nr; r++)
		{
			size_t bp, bl;
			cricsb_utf_t w;
			if (cricsb_utf_blob (&el, ed, r, &bp, &bl) && cricsb_utf_open (&w, raw, sz, bp) && !strcmp (cricsb_utf_str (&w, w.name), "ADPCM_WII"))
				good++;
		}
		if (!good)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CSB:%s (%u waveforms) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, good, dest);
		err = ERR_OK;
		for (uint r = 0; r < el.nr && !err && !testmode; r++)
		{
			size_t bp, bl, dp, dl, hp, hl;
			cricsb_utf_t w;
			if (!cricsb_utf_blob (&el, ed, r, &bp, &bl) || !cricsb_utf_open (&w, raw, sz, bp) || strcmp (cricsb_utf_str (&w, w.name), "ADPCM_WII"))
				continue;
			if (!cricsb_utf_blob (&w, cricsb_utf_col (&w, "data"), 0, &dp, &dl) || !cricsb_utf_blob (&w, cricsb_utf_col (&w, "header"), 0, &hp, &hl) || hl != 0x60)
				continue;
			ccp nm = cricsb_utf_str (&el, rd_be32 (cricsb_utf_cell (&el, en, r)));
			ccp sl = strrchr (nm, '/');
			char base[200];
			snprintf (base, sizeof (base), "%s", sl ? sl + 1 : nm);
			for (char *c = base; *c; c++)
				if ((u8)*c < 0x20 || *c == '\\' || *c == ':')
					*c = '_';
			if (!base[0])
				snprintf (base, sizeof (base), "wave%03u", r);
			uint dup = 0;
			for (uint j = 0; j < r; j++)
			{
				size_t bp2, bl2;
				if (!cricsb_utf_blob (&el, ed, j, &bp2, &bl2))
					continue;
				ccp nm2 = cricsb_utf_str (&el, rd_be32 (cricsb_utf_cell (&el, en, j)));
				ccp sl2 = strrchr (nm2, '/');
				if (!strcmp (sl2 ? sl2 + 1 : nm2, sl ? sl + 1 : nm))
					dup++;
			}
			char name[256];
			if (dup)
				snprintf (name, sizeof (name), "%s~%u.dsp", base, dup);
			else
				snprintf (name, sizeof (name), "%s.dsp", base);
			u8 *buf = MALLOC (0x60 + dl);
			if (!buf)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			memcpy (buf, raw + hp, 0x60);
			memcpy (buf + 0x60, raw + dp, dl);
			err = shade_write (arg, dest, basedir, name, buf, (uint)(0x60 + dl));
			FREE (buf);
		}
		if (!err && !testmode)
		{
			enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}
