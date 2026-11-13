// Rockstar Leeds animation keyframes (Manhunt 2 Wii "ANPK" packs in ".ifp" and "strmanim_wii.bin"; big endian). Layout (as documented by the Manhunt modding community and
// checked on the disc files): "ANPK" u32 animation count, then per animation "NAME" {u32 length, name}, u32 bone count, u32 chunk size, f32 duration (seconds), per bone
// "SEQT" {u16 bone id, u8 frame type, u16 frame count, u16 start time (/2048*30)}, for frame type 3 four s16 (/2048) of direction data, then per frame: a u16 time (/2048*30; only when the
// start time is 0, and not for the first frame of type 3), s16 x 4 rotation quaternion (/4096) for types 1-3 and s16 x 3 position (/2048) for types 2-3; each bone ends with an f32
// last-frame time. After the bones come {u32 header size, u32, u32 entry size, u32 entry count} and the entries (events, skipped). Written as one ".csv" per animation:
// bone, frame, time, qx, qy, qz, qw, px, py, pz (empty where a frame type has no such channel).
static int mh2anim_s16 (const u8 *p)
{
	return (short)rd_be16 (p);
}

static bool mh2anim_one (sumo_buf_t *b, const u8 *c, size_t cs, size_t *pp, char *name, size_t namesz)
{
	size_t p = *pp;
	if (p + 16 > cs || memcmp (c + p, "NAME", 4))
		return false;
	const size_t nl = rd_be32 (c + p + 4);
	if (!nl || nl > 120 || p + 8 + nl + 16 > cs)
		return false;
	snprintf (name, namesz, "%.*s", (int)nl, (ccp)c + p + 8);
	p += 8 + nl;
	const uint nb = rd_be32 (c + p);
	if (nb > 400)
		return false;
	p += 12; // bone count, chunk size, duration
	sumo_printf (b, "bone,frame,time,qx,qy,qz,qw,px,py,pz\n");
	for (uint k = 0; k < nb; k++)
	{
		if (p + 11 > cs || memcmp (c + p, "SEQT", 4))
			return false;
		const uint id = rd_be16 (c + p + 4), type = c[p + 6], frames = rd_be16 (c + p + 7), start = rd_be16 (c + p + 9);
		p += 11;
		if (type < 1 || type > 3 || frames > 20000)
			return false;
		if (type == 3)
			p += 8;
		else if (!start)
			p -= 2;
		double t = 0;
		for (uint f = 0; f < frames; f++)
		{
			const size_t need = ((!start && !(f == 0 && type == 3)) ? 2 : 0) + (type < 3 ? 8 : 0) + (type > 1 ? 6 : 0);
			if (p + need > cs)
				return false;
			double ft;
			if (!start)
			{
				if (!(f == 0 && type == 3))
				{
					ft = rd_be16 (c + p) / 2048.0 * 30;
					p += 2;
				}
				else
					ft = 0;
				t += ft;
			}
			else
			{
				double st = start / 2048.0 * 30;
				t = f / 2048.0 * 30 + (st < 1 ? 1 : st) - 1;
			}
			sumo_printf (b, "%u,%u,%.4f", id, f, t);
			if (type < 3)
			{
				sumo_printf (b, ",%.5f,%.5f,%.5f,%.5f", mh2anim_s16 (c + p) / 4096.0, mh2anim_s16 (c + p + 2) / 4096.0, mh2anim_s16 (c + p + 4) / 4096.0, mh2anim_s16 (c + p + 6) / 4096.0);
				p += 8;
			}
			else
				sumo_printf (b, ",,,,");
			if (type > 1)
			{
				sumo_printf (b, ",%.5f,%.5f,%.5f", mh2anim_s16 (c + p) / 2048.0, mh2anim_s16 (c + p + 2) / 2048.0, mh2anim_s16 (c + p + 4) / 2048.0);
				p += 6;
			}
			else
				sumo_printf (b, ",,,");
			sumo_printf (b, "\n");
		}
		p += 4; // last frame time
	}
	if (p + 16 > cs)
		return false;
	const size_t esz = rd_be32 (c + p + 8), ne = rd_be32 (c + p + 12);
	p += 16;
	if (ne && (esz > 4096 || p + esz * ne > cs))
		return false;
	*pp = p + esz * ne;
	return true;
}

// parses "ANPK" u32 count + animations at c[p]; adds one csv entry per animation; returns the number of animations
static uint mh2anim_pack (nintendo_sarc_entry_t **entries, uint *n, uint *cap, const u8 *c, size_t cs, size_t *pp, ccp prefix)
{
	size_t p = *pp;
	if (p + 8 > cs || memcmp (c + p, "ANPK", 4))
		return 0;
	uint cnt = rd_be32 (c + p + 4), done = 0;
	p += 8;
	while (cnt--)
	{
		sumo_buf_t b = { 0, 0, 0, true };
		char name[128];
		size_t q = p;
		if (!mh2anim_one (&b, c, cs, &q, name, sizeof name) || !b.ok)
		{
			FREE (b.p);
			break;
		}
		for (char *k = name; *k; k++)
			if (!isalnum ((u8)*k) && *k != '_' && *k != '-')
				*k = '_';
		char path[PATH_MAX];
		snprintf (path, sizeof path, "%s%04u_%.80s.csv", prefix, *n, name);
		if (*n == *cap)
		{
			*cap = *cap ? 2 * *cap : 64;
			*entries = REALLOC (*entries, *cap * sizeof (**entries));
			if (!*entries)
			{
				FREE (b.p);
				return done;
			}
			memset (*entries + *n, 0, (*cap - *n) * sizeof (**entries));
		}
		if (!OwnedEntryAdd (*entries, *n, path, (u8 *)b.p, (uint)b.n))
		{
			FREE (b.p);
			break;
		}
		(*n)++;
		done++;
		FREE (b.p);
		p = q;
	}
	*pp = p;
	return done;
}

static enumError extract_mh2_anpk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".anpk"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 32 || memcmp (c, "ANPK", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	uint n = 0, cap = 0;
	size_t p = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (mh2anim_pack (&entries, &n, &cap, c, cs, &p, "") && n)
	{
		err = write_owned_entries (arg, basedir, depth, "MH2-ANPK", entries, n);
		entries = 0;
	}
	if (entries)
		ResetOwnedEntries (entries, n);
	FREE (c);
	return err;
}
