// Octomania-style wavetable pair ".wt" + ".pcm" (Kidz Sports / London Taxi / Battle Rage / ... : "sound/roland/gm16pcm.wt", the
// console form of the Roland GM16.DLS bank). The .wt is big endian: six u32 section offsets {0x18, ..., end of file}; the
// last section holds one 16 byte record per sample {u32 rate (low 16 bits, high bits flags), u32 start (in samples), u32
// length (in samples), u32 0} into the companion ".pcm" (big endian s16 mono, consecutive samples). Every sample is written as
// "wave_NNNN.wav" in the "<file>.d" directory. The "...adpcm" variants (sample starts in nibbles, DSP-ADPCM frames, coefficients in a 46 byte per sample trailer after the
// last section) are decoded too. The instrument / key maps (the first sections) are written as "map.json".
static enumError extract_octowt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".wt"))
		return ERR_NOTHING_TO_DO;
	u8 *wt = 0, *pcm = 0;
	size_t wsz = 0, psz = 0;
	char pp[PATH_MAX];
	snprintf (pp, sizeof (pp), "%s", arg);
	const size_t pl = strlen (pp);
	if (pl < 4 || LoadFileAlloc (arg, 0, 0, &wt, &wsz, 0, 0, 0, false) || !wt || wsz < 0x18 || wsz > 0x4000000)
		goto bad;
	memcpy (pp + pl - 3, ".pcm", 5);
	if (LoadFileAlloc (pp, 0, 0, &pcm, &psz, 0, 0, 0, false) || !pcm || psz < 2)
		goto bad;
	{
		u32 o[6];
		for (uint i = 0; i < 6; i++)
			o[i] = rd_be32 (wt + 4 * i);
		if (o[0] != 0x18 || o[5] > wsz || o[4] >= o[5] || (o[5] - o[4]) % 16)
			goto bad;
		const uint n = (o[5] - o[4]) / 16;
		u64 next = 0;
		// ADPCM variant: 46 byte trailer record per sample {16 BE s16 DSP coefficients, 7 more shorts}; sample starts are in nibbles
		const bool adpcm = wsz - o[5] == 46 * (size_t)n;
		for (uint i = 0; i < n; i++)
		{
			const u8 *r = wt + o[4] + 16 * i;
			const u32 off = rd_be32 (r + 4), len = rd_be32 (r + 8), rate = rd_be32 (r) & 0xffff;
			if (!rate || len > 0x1000000)
				goto bad;
			if (adpcm)
			{
				if ((u64)off / 2 >= psz)
					goto bad;
			}
			else if (off != next || (u64)(off + len) * 2 > psz)
				goto bad;
			next = (u64)off + len;
		}
		enumError err = ERR_OK;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT OCTOMANIA-WT:%s (%u samples) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n,
				dest);
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *r = wt + o[4] + 16 * i;
			const u32 off = rd_be32 (r + 4), len = rd_be32 (r + 8), rate = rd_be32 (r) & 0xffff;
			const u32 bytes = len * 2;
			s16 coef[16];
			if (adpcm)
				for (uint k = 0; k < 16; k++)
					coef[k] = (s16)rd_be16 (wt + o[5] + 46 * (size_t)i + 2 * k);
			u8 *w = MALLOC (44 + (size_t)bytes);
			if (!w)
			{
				err = ERR_OUT_OF_MEMORY;
				break;
			}
			memcpy (w, "RIFF", 4);
			w[4] = (u8)(36 + bytes), w[5] = (u8)((36 + bytes) >> 8), w[6] = (u8)((36 + bytes) >> 16), w[7] = (u8)((36 + bytes) >> 24);
			memcpy (w + 8, "WAVEfmt ", 8);
			const u32 hdr[] = { 16, 0x00010001, rate, rate * 2, 0x00100002 };
			for (uint k = 0; k < 5; k++)
				w[16 + 4 * k] = (u8)hdr[k], w[17 + 4 * k] = (u8)(hdr[k] >> 8), w[18 + 4 * k] = (u8)(hdr[k] >> 16), w[19 + 4 * k] = (u8)(hdr[k] >> 24);
			memcpy (w + 36, "data", 4);
			w[40] = (u8)bytes, w[41] = (u8)(bytes >> 8), w[42] = (u8)(bytes >> 16), w[43] = (u8)(bytes >> 24);
			if (!adpcm)
				for (u32 k = 0; k < len; k++)
					w[44 + 2 * k] = pcm[2 * ((size_t)off + k) + 1], w[45 + 2 * k] = pcm[2 * ((size_t)off + k)];
			else
			{
				// DSP-ADPCM: 8 byte frames (header = predictor << 4 | scale, 14 nibbles)
				int h1 = 0, h2 = 0;
				for (u32 k = 0; k < len; k++)
				{
					const size_t fp = (size_t)off / 2 + (k / 14) * 8;
					const uint hd = fp < psz ? pcm[fp] : 0, ix = k % 14;
					const size_t bp = fp + 1 + ix / 2;
					const uint x = bp < psz ? pcm[bp] : 0;
					int q = ix & 1 ? x & 15 : x >> 4;
					if (q >= 8)
						q -= 16;
					const uint pr = (hd >> 4) & 7;
					int v = (int)((((s64)q << (hd & 15)) << 11) + 0x400 + (s64)coef[2 * pr] * h1 + (s64)coef[2 * pr + 1] * h2) >> 11;
					v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
					h2 = h1;
					h1 = v;
					w[44 + 2 * k] = (u8)v, w[45 + 2 * k] = (u8)(v >> 8);
				}
			}
			char name[32];
			snprintf (name, sizeof (name), "wave_%04u.wav", i);
			err = shade_write (arg, dest, basedir, name, w, 44 + bytes);
			FREE (w);
		}
		// instrument maps: section 0 = drum key maps (128 rows x 128 keys of u16 region ids, 0xffff = none; the non-empty rows are the kits,
		// e.g. rows 0, 8, 16 ...), section 1 = melodic maps (128 programs x 128 keys of region ids), section 2 = 24 byte regions
		// {u8 root key, u8 0, s16 tune, s16 volume, u16 ?, u32 loop start, u32 loop length, u32 layer, u32 sample (record of the last section)};
		// written as "map.json"
		if (!err && !testmode && o[1] - o[0] == 0x8000 && o[2] - o[1] == 0x8000 && (o[3] - o[2]) % 24 == 0)
		{
			njm_out_t j = { MALLOC (1 << 16), 0, 1 << 16 };
			if (j.b)
			{
				njm_put (&j, "{\"drum_kits\": {");
				bool first = true;
				for (uint r = 0; r < 128; r++)
				{
					const u8 *row = wt + o[0] + 256 * r;
					uint used = 0;
					for (uint k = 0; k < 128; k++)
						used += rd_be16 (row + 2 * k) != 0xffff;
					if (!used)
						continue;
					njm_put (&j, "%s\n \"%u\": [", first ? "" : ",", r);
					first = false;
					for (uint k = 0; k < 128; k++)
						njm_put (&j, "%s%d", k ? "," : "", rd_be16 (row + 2 * k) == 0xffff ? -1 : (int)rd_be16 (row + 2 * k));
					njm_put (&j, "]");
				}
				njm_put (&j, "},\n\"programs\": [");
				for (uint r = 0; r < 128; r++)
				{
					njm_put (&j, "%s\n [", r ? "," : "");
					for (uint k = 0; k < 128; k++)
						njm_put (&j, "%s%d", k ? "," : "", rd_be16 (wt + o[1] + 256 * r + 2 * k));
					njm_put (&j, "]");
				}
				njm_put (&j, "],\n\"regions\": [");
				for (uint r = 0; r < (o[3] - o[2]) / 24; r++)
				{
					const u8 *q = wt + o[2] + 24 * r;
					njm_put (&j, "%s\n {\"root\": %u, \"tune\": %d, \"volume\": %d, \"x\": %u, \"loop_start\": %u, \"loop_length\": %u, \"layer\": %u, \"wave\": %u}",
						r ? "," : "", q[0], (int)(s16)rd_be16 (q + 2), (int)(s16)rd_be16 (q + 4), rd_be16 (q + 6), rd_be32 (q + 8), rd_be32 (q + 12),
						rd_be32 (q + 16), rd_be32 (q + 20));
				}
				njm_put (&j, "]}\n");
				err = shade_write (arg, dest, basedir, "map.json", (u8 *)j.b, (uint)j.used);
				FREE (j.b);
			}
		}
		FREE (wt);
		FREE (pcm);
		return err;
	}
bad:
	FREE (wt);
	FREE (pcm);
	return ERR_NOTHING_TO_DO;
}
