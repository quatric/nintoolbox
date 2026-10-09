// Ubisoft ".gwb" + ".gwd" sound bank (Monster 4x4: World Circuit, Wii). The small .gwb (< 0x2000 bytes)
// lists the subsongs, the sibling .gwd holds their headerless DSP-ADPCM:
//   v6/v7 @+0x00, file id be32 @+1, subsong count be32 @+5 (v6) / +9 (v7), then per subsong:
//   u8 flags (0x02 mono, 0x09/0x0a stereo), id be32, (v7: be32 2, stream offset, stream size, be32 5),
//   then one 0x4a-byte record per channel {loop flag, rate, loop start/end/end/start nibbles, 0, then the
//   0x2e bytes of a standard DSP header from +0x1c (coefs ... loop history)}. v6 streams start at the
//   channel's first nibble, v7 interleave 0x4000, v6 stereo interleave = distance of the start nibbles.
// Every channel becomes a standard "NNNN[_chN].dsp".
static enumError extract_gwb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gwb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *body = 0;
	size_t sz = 0, bsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ver = sz > 0x10 ? raw[0] : 0;
	const size_t cnt_off = ver == 6 ? 5 : 9;
	if ((ver != 6 && ver != 7) || sz > 0x2000 || rd_be32 (raw + 1) > 0x400 || cnt_off + 4 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = rd_be32 (raw + cnt_off);
	char *gwd = STRDUP (arg);
	if (!gwd || count < 1 || count > 4000)
	{
		FREE (gwd);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	memcpy (gwd + strlen (gwd) - 3, "gwd", 3);
	if (LoadFileAlloc (gwd, 0, 0, &body, &bsz, 0, 0, 0, false) || !body)
	{
		memcpy (gwd + strlen (gwd) - 3, "GWD", 3);
		if (LoadFileAlloc (gwd, 0, 0, &body, &bsz, 0, 0, 0, false) || !body)
		{
			FREE (body);
			FREE (gwd);
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	FREE (gwd);
	nintendo_sarc_entry_t *entries = CALLOC (count * 2, sizeof (*entries));
	uint n = 0;
	size_t off = cnt_off + 4;
	bool good = entries != 0;
	for (uint s = 0; s < count && good; s++)
	{
		if (off + 0x5 + 0x4a > sz)
		{
			good = false;
			break;
		}
		const uint type = raw[off], ch = type & 8 ? 2 : 1;
		if (type != 0x0a && type != 0x09 && type != 0x02)
		{
			good = false;
			break;
		}
		off += 5;
		size_t so = 0, ss = 0, il = 0x4000;
		if (ver == 7)
		{
			if (off + 0x10 > sz)
			{
				good = false;
				break;
			}
			so = rd_be32 (raw + off + 4);
			ss = rd_be32 (raw + off + 8);
			off += 0x10;
		}
		if (off + 0x4a * (size_t)ch > sz)
		{
			good = false;
			break;
		}
		const u8 *h = raw + off;
		const u32 rate = rd_be32 (h + 4), st = rd_be32 (h + 0x14), ed = rd_be32 (h + 0x10);
		if (ver == 6)
		{
			so = (st - 2) / 2;
			ss = ((size_t)(ed - st - 2) / 2) * ch;
			il = ch == 2 ? ((size_t)rd_be32 (h + 0x4a + 0x14) - st) / 2 : 0x4000;
		}
		const size_t per = ss / ch / 8 * 8, ns = per / 8 * 14;
		if (!per || !il || !rate || so + ss > bsz || st < 2)
		{
			good = false;
			break;
		}
		const size_t full = per / il, rem = per % il, rem_stride = full ? rem : il;
		bool loop = rd_be32 (h) == 1;
		// loop points as vgmstream derives them (bytes of the whole stream -> samples)
		size_t ls = ((size_t)rd_be32 (h + 8) - 2) / 2 - so, le = ((size_t)rd_be32 (h + 0x0c) / 2 - so) * ch;
		ls = ls / ch / 8 * 14;
		le = le / ch / 8 * 14;
		if (rd_be32 (h + 8) < 2 + so * 2 || rd_be32 (h + 0x0c) / 2 < so || ls >= le || le > ns)
			loop = false;
		for (uint c = 0; c < ch && good; c++)
		{
			u8 *out = CALLOC (1, 0x60 + per);
			if (!out)
			{
				good = false;
				break;
			}
			const u8 *hc = h + 0x4a * c;
			wr_be32 (out, (u32)ns);
			wr_be32 (out + 4, (u32)(per * 2));
			wr_be32 (out + 8, rate);
			wr_be16 (out + 0x0c, loop);
			wr_be32 (out + 0x10, loop ? (u32)(ls / 14 * 16 + 2 + ls % 14) : 2);
			wr_be32 (out + 0x14, loop ? (u32)((le - 1) / 14 * 16 + 2 + (le - 1) % 14) : (u32)(per * 2 - 1));
			wr_be32 (out + 0x18, 2);
			memcpy (out + 0x1c, hc + 0x1c, 0x2e);
			for (size_t i = 0; i < full; i++)
				memcpy (out + 0x60 + i * il, body + so + (i * ch + c) * il, il);
			if (rem)
				memcpy (out + 0x60 + full * il, body + so + full * ch * il + c * rem_stride, rem);
			out[0x3e] = 0;
			out[0x3f] = out[0x60];
			char name[24];
			if (ch == 1)
				snprintf (name, sizeof (name), "%04u.dsp", s);
			else
				snprintf (name, sizeof (name), "%04u_ch%u.dsp", s, c);
			good = OwnedEntryAdd (entries, n, name, out, 0x60 + per);
			FREE (out);
			if (good)
				n++;
		}
		off += 0x4a * (size_t)ch;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "GWB", entries, n);
	else
		FREE (entries);
	FREE (body);
	FREE (raw);
	return err;
}
