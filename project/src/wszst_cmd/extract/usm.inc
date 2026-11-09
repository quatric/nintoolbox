// CRI "USM" movie container (Sofdec 2; Wii games ship them as .usm): a sequence of chunks {tag "CRID" / "@SFV" video / "@SFA" audio / "@ALP"
// alpha, u32be size, u8 0, u8 payload offset, u16be padding, u8 channel, 2 reserved, u8 type (0 data, 1 header @UTF table, 2 end,
// 3 seek), u32 frame time, u32 frame rate, 8 reserved}; payload = bytes [8 + offset, 8 + size - padding). The data chunks of each stream
// are joined: video -> "video<N>.m1v" (MPEG-1 elementary stream; may be masked in encrypted files, written as stored), audio -> "audio<N>.adx".
typedef struct usm_stream_t { u32 tag; u8 ch; u8 *buf; size_t size, cap; } usm_stream_t;

static enumError extract_usm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "CRID", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	usm_stream_t st[32];
	uint ns = 0;
	memset (st, 0, sizeof (st));
	enumError err = ERR_OK;
	size_t p = 0;
	while (p + 32 <= sz)
	{
		const u32 csz = rd_be32 (raw + p + 4);
		const uint off = raw[p + 9], pad = rd_be16 (raw + p + 10), ch = raw[p + 12], type = raw[p + 15] & 3;
		if (csz < 0x18 || p + 8 + (u64)csz > sz || 8 + off + pad > 8 + csz)
			break;
		const bool v = !memcmp (raw + p, "@SFV", 4), a = !memcmp (raw + p, "@SFA", 4), al = !memcmp (raw + p, "@ALP", 4);
		if ((v || a || al) && type == 0)
		{
			const u32 tag = rd_be32 (raw + p);
			uint k;
			for (k = 0; k < ns && (st[k].tag != tag || st[k].ch != ch); k++)
				;
			if (k == ns)
			{
				if (ns == 32)
					goto next;
				st[ns].tag = tag;
				st[ns++].ch = ch;
			}
			const size_t n = csz - off - pad;
			if (st[k].size + n > st[k].cap)
			{
				st[k].cap = (st[k].size + n) * 2;
				st[k].buf = REALLOC (st[k].buf, st[k].cap);
				if (!st[k].buf)
				{
					err = ERR_OUT_OF_MEMORY;
					goto done;
				}
			}
			memcpy (st[k].buf + st[k].size, raw + p + 8 + off, n);
			st[k].size += n;
		}
	next:
		p += 8 + csz;
	}
	if (!ns || p < 0x40)
	{
		err = ERR_NOTHING_TO_DO;
		goto done;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT USM:%s (%u streams) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ns, dest);
	for (uint k = 0; k < ns && !err && !testmode; k++)
	{
		char name[32];
		snprintf (name, sizeof (name), st[k].tag == 0x40534641 ? "audio%u.adx" : st[k].tag == 0x40534656 ? "video%u.m1v" : "alpha%u.m1v", st[k].ch);
		err = shade_write (arg, dest, basedir, name, st[k].buf, st[k].size);
	}
done:
	for (uint k = 0; k < ns; k++)
		FREE (st[k].buf);
	FREE (raw);
	return err;
}
