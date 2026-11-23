// "sedl" (".sed", sound effect sequences, about 60 DS games) and "smdl" (".smd", songs): Procyon Studio DSE sequences, a MIDI like container (layout from the Project Pokemon "DSE SMDL Format" document by psy_commando): 0x40 byte header, "song" chunk (u16 tpqn at +0x12, u8 tracks at +0x16), "trk " chunks {tag, u32, u32, u32 length = bytes after the length field up to and including the first 0x98 event}, each starting with {u8 track, u8 channel, 2 pad} and events: 0x00..0x7f play note (velocity; the next byte is {bits 7-6 extra duration bytes, 5-4 octave change - 2, 3-0 note}, then 0-3 big endian duration bytes, zero = the last duration), 0x80..0x8f fixed pauses, 0x90..0x95 other pauses, 0x98 end, 0x99 loop point, 0xa0/0xa1 octave, 0xa4/0xa5 tempo (BPM), 0xac program, 0xe0 volume, 0xe3 expression, 0xe8 pan, 0xd7 pitch bend; the other events are skipped using the parameter length table from the document.
// A track is only converted when it parses to exactly its stored length and ends on a 0x98 ; effects that fail are skipped. Written as a type 1 "<file>.d/<name>.mid" (note = 12 * octave + key; loops are played once; "pause until release" waits for the longest held note).
static const u8 sedl_plen[0x70] = {
	0, 1, 1, 2, 3, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
	1, 1, 0, 0, 1, 1, 0, 0, 2, 1, 1, 0, 1, 0, 0, 3,
	0, 1, 1, 1, 2, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1,
	1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 2, 3, 2, 2, 2, 2, 0, 0, 1, 5, 4, 0, 1,
	1, 1, 3, 1, 5, 4, 0, 1, 1, 1, 3, 0, 5, 4, 0, 1,
	5, 4, 2, 3, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const u8 sedl_fixed[16] = { 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2 };

typedef struct sedl_ev_t
{
	u32 tick;
	u8 ord, a, b, c; // ord 0 note off / meta, 1 other, 2 note on; a status, b/c data
	u32 val;		 // tempo in microseconds per quarter note when a == 0xff
} sedl_ev_t;

static int sedl_cmp (const void *x, const void *y)
{
	const sedl_ev_t *p = x, *q = y;
	return p->tick != q->tick ? (p->tick < q->tick ? -1 : 1) : (int)p->ord - (int)q->ord;
}

static void sedl_vlq (u8 **w, u32 v)
{
	u8 t[5];
	int n = 0;
	t[n++] = v & 0x7f;
	while (v >>= 7)
		t[n++] = 0x80 | (v & 0x7f);
	while (n--)
		*(*w)++ = t[n];
}

// Parses one track into events; returns false when the track is not consistent.
static bool sedl_track (const u8 *t, uint len, sedl_ev_t **ev, uint *nev, uint *cap, uint chan)
{
	if (len < 5)
		return false;
	uint p = 4, lastpause = 0, lastdur = 0;
	u32 tick = 0, held = 0;
	int oct = 4;
	while (p < len)
	{
		const uint c = t[p++];
		uint pause = 0;
		bool is_pause = false;
		sedl_ev_t e = { tick, 1, 0, 0, 0, 0 };
		if (c < 0x80)
		{
			if (p >= len)
				return false;
			const uint nd = t[p++], nb = nd >> 6;
			if (p + nb > len)
				return false;
			uint dur = 0;
			for (uint k = 0; k < nb; k++)
				dur = dur << 8 | t[p++];
			if (nb)
				lastdur = dur;
			oct += (int)(nd >> 4 & 3) - 2;
			const uint key = nd & 15;
			if (key < 12)
			{
				const int note = 12 * oct + (int)key;
				if (note >= 0 && note < 128)
				{
					if (*nev + 2 > *cap)
					{
						if (*cap > 400000)
							return false;
						*cap = *cap ? 2 * *cap : 1024;
						*ev = REALLOC (*ev, *cap * sizeof (**ev));
						if (!*ev)
							return false;
					}
					(*ev)[(*nev)++] = (sedl_ev_t){ tick, 2, (u8)(0x90 | chan), (u8)note, (u8)c, 0 };
					(*ev)[(*nev)++] = (sedl_ev_t){ tick + lastdur, 0, (u8)(0x80 | chan), (u8)note, 0, 0 };
					if (tick + lastdur > held)
						held = tick + lastdur;
				}
			}
			continue;
		}
		if (c < 0x90)
		{
			pause = lastpause = sedl_fixed[c & 15];
			is_pause = true;
		}
		else
		{
			const uint pl = sedl_plen[c - 0x90];
			uint np = pl;
			if (c == 0xab)
				np = 1;
			else if (c == 0xcb || c == 0xf8)
				np = 2;
			const bool invalid = (c >= 0x96 && c <= 0x97) || c == 0x9a || c == 0x9b || c == 0x9f || c == 0xa2 || c == 0xa3 || c == 0xa6 || c == 0xa7 || c == 0xad || c == 0xae || (c >= 0xb7 && c <= 0xbb) || c == 0xbd || c == 0xc1 || c == 0xc2 || (c >= 0xc4 && c <= 0xca) || (c >= 0xcc && c <= 0xcf) || c == 0xd9 || c == 0xda || c == 0xde || c == 0xe6 || c == 0xeb || c == 0xee || c == 0xf4 || c == 0xf5 || c == 0xf7 || c >= 0xf9;
			if (invalid || p + np > len)
				return false;
			const u8 *a = t + p;
			p += np;
			switch (c)
			{
				case 0x90: pause = lastpause; is_pause = true; break;
				case 0x91: pause = lastpause = lastpause + a[0]; is_pause = true; break;
				case 0x92: pause = lastpause = a[0]; is_pause = true; break;
				case 0x93: pause = lastpause = rd_le16 (a); is_pause = true; break;
				case 0x94: pause = lastpause = a[0] | a[1] << 8 | a[2] << 16; is_pause = true; break;
				case 0x95: pause = held > tick ? held - tick : 0; is_pause = true; break;
				case 0x98: return p == len || (p < len && false);
				case 0xa0: oct = a[0]; break;
				case 0xa1: oct += (int8_t)a[0]; break;
				case 0xa4:
				case 0xa5:
					if (a[0])
						e.a = 0xff, e.val = 60000000u / a[0];
					break;
				case 0xac: e.a = 0xc0 | chan, e.b = a[0] & 0x7f; break;
				case 0xe0: e.a = 0xb0 | chan, e.b = 7, e.c = a[0] & 0x7f; break;
				case 0xe3: e.a = 0xb0 | chan, e.b = 11, e.c = a[0] & 0x7f; break;
				case 0xe8: e.a = 0xb0 | chan, e.b = 10, e.c = a[0] & 0x7f; break;
				default: break;
			}
			if (e.a)
			{
				if (*nev + 1 > *cap)
				{
					*cap = *cap ? 2 * *cap : 1024;
					*ev = REALLOC (*ev, *cap * sizeof (**ev));
					if (!*ev)
						return false;
				}
				(*ev)[(*nev)++] = e;
			}
		}
		if (is_pause)
			tick += pause;
		if (tick > 100000000u)
			return false;
	}
	return false; // no 0x98 inside the stored length
}

// Builds a type 1 MIDI from nt "trk " chunks starting at o; returns a malloc block or 0 when a track does not parse cleanly.
static u8 *sedl_midi (const u8 *raw, size_t sz, size_t o, uint nt, uint tpqn, size_t *outn)
{
	size_t cap = 64 + 64 * sz, mn = 14;
	u8 *mid = MALLOC (cap);
	sedl_ev_t *ev = 0;
	uint evcap = 0;
	if (!mid)
		return 0;
	memcpy (mid, "MThd\0\0\0\6\0\1", 10);
	mid[10] = 0;
	mid[11] = (u8)nt;
	mid[12] = (u8)(tpqn >> 8);
	mid[13] = (u8)tpqn;
	for (uint t = 0; t < nt; t++)
	{
		if (o + 20 > sz || memcmp (raw + o, "trk ", 4))
			goto fail;
		const uint len = rd_le32 (raw + o + 12);
		if (len > sz || o + 16 + len > sz)
			goto fail;
		uint nev = 0;
		if (!sedl_track (raw + o + 16, len, &ev, &nev, &evcap, raw[o + 17] & 15))
			goto fail;
		qsort (ev, nev, sizeof (*ev), sedl_cmp);
		u8 *w = mid + mn + 8;
		u32 last = 0;
		for (uint i = 0; i < nev && (size_t)(w - mid) + 16 < cap; i++)
		{
			sedl_vlq (&w, ev[i].tick - last);
			last = ev[i].tick;
			if (ev[i].a == 0xff)
			{
				*w++ = 0xff, *w++ = 0x51, *w++ = 3;
				*w++ = ev[i].val >> 16, *w++ = ev[i].val >> 8, *w++ = ev[i].val;
			}
			else
			{
				*w++ = ev[i].a, *w++ = ev[i].b;
				if ((ev[i].a & 0xf0) != 0xc0)
					*w++ = ev[i].c;
			}
		}
		*w++ = 0, *w++ = 0xff, *w++ = 0x2f, *w++ = 0;
		memcpy (mid + mn, "MTrk", 4);
		const u32 tl = (u32)(w - (mid + mn + 8));
		mid[mn + 4] = tl >> 24, mid[mn + 5] = tl >> 16, mid[mn + 6] = tl >> 8, mid[mn + 7] = tl;
		mn = (size_t)(w - mid);
		o += 16 + ((len + 3) & ~3u);
	}
	FREE (ev);
	*outn = mn;
	return mid;
fail:
	FREE (ev);
	FREE (mid);
	return 0;
}

// "sedl": u16 offset table at 0x40 (to the first offset), each offset a 0x30 byte info block {+2 u16 tpqn, +6 u8 tracks} followed by its "trk " chunks (one sound effect each, 0 entries skipped); "smdl": one song, info block = the "song" chunk at 0x40 (tpqn +0x12, tracks +0x16), tracks from 0x80.
static enumError extract_sedl_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	const bool se = sz > 0x90 && !memcmp (raw, "sedl", 4);
	if (!se && !(sz > 0x90 && !memcmp (raw, "smdl", 4) && !memcmp (raw + 0x40, "song", 4)))
		goto done;
	u32 offs[256], tp[256], ntr[256];
	uint n = 0;
	if (se)
	{
		uint first = 0xffff;
		for (uint i = 0; 0x40 + 2 * i + 2 <= first && 0x40 + 2 * i + 2 <= sz && n < 256; i++)
		{
			const uint v = rd_le16 (raw + 0x40 + 2 * i);
			if (!v)
				continue;
			if (v < first)
				first = v;
			if (v + 0x30 + 20 > sz)
				continue;
			offs[n] = v + 0x30, tp[n] = rd_le16 (raw + v + 2), ntr[n] = raw[v + 6];
			n++;
		}
	}
	else
		offs[0] = 0x80, tp[0] = rd_le16 (raw + 0x52), ntr[0] = raw[0x56], n = 1;
	entries = CALLOC (n ? n : 1, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	const char *sl = strrchr (arg, '/');
	uint k = 0;
	for (uint i = 0; i < n; i++)
	{
		if (!tp[i] || !ntr[i] || ntr[i] > 32)
			continue;
		size_t mn;
		u8 *mid = sedl_midi (raw, sz, offs[i], ntr[i], tp[i], &mn);
		char name[PATH_MAX];
		if (se)
			snprintf (name, sizeof (name), "%s_%03u.mid", sl ? sl + 1 : arg, i);
		else
			snprintf (name, sizeof (name), "%s.mid", sl ? sl + 1 : arg);
		if (mid && OwnedEntryAdd (entries, k, name, mid, (uint)mn))
			k++;
		FREE (mid);
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "SEDL", entries, k);
	else
		ResetOwnedEntries (entries, n ? n : 1);
done:
	FREE (raw);
	return err;
}
