// Athletics-engine animation ".ANM" (International Athletics / Summer Athletics style "ath" engine, pairs with ".MSH" / ".TEX"):
// big endian {u16 version 0x0403, u32 size (= file size - 2), char node[32], u32 frames, u32 nodes, u32 rate, u32 quaternion
// bytes, u32 position bytes}, then quaternions (4 floats x,y,z,w), positions (3 floats) and frames * nodes pairs {u16 index,
// u16 flag} (frame major; the flag meaning is not resolved). Written as "anim.json" in the "<file>.d" directory.
static enumError extract_athanm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".anm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x3a || sz > 0x10000000 || rd_be16 (raw) != 0x0403
		|| rd_be32 (raw + 2) != sz - 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nfr = rd_be32 (raw + 0x26), nn = rd_be32 (raw + 0x2a), rate = rd_be32 (raw + 0x2e), qb = rd_be32 (raw + 0x32),
		pb = rd_be32 (raw + 0x36);
	const size_t qo = 0x3a, po = qo + (size_t)qb, ko = po + (size_t)pb;
	if ((qb & 15) || (pb % 12) || ko > sz || (u64)nfr * nn * 4 != sz - ko)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char name[33];
	memcpy (name, raw + 6, 32);
	name[32] = 0;
	njm_out_t o = { MALLOC (1 << 16), 0, 1 << 16 };
	if (!o.b)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	njm_put (&o, "{\n\"node\": \"%s\",\n\"frames\": %u,\n\"nodes\": %u,\n\"rate\": %u,\n\"quats\": [", name, nfr, nn, rate);
	for (uint i = 0; i < qb / 16; i++)
		njm_put (&o, "%s[%g,%g,%g,%g]", i ? "," : "", njm_f (raw + qo + 16 * i), njm_f (raw + qo + 16 * i + 4),
			njm_f (raw + qo + 16 * i + 8), njm_f (raw + qo + 16 * i + 12));
	njm_put (&o, "],\n\"positions\": [");
	for (uint i = 0; i < pb / 12; i++)
		njm_put (&o, "%s[%g,%g,%g]", i ? "," : "", njm_f (raw + po + 12 * i), njm_f (raw + po + 12 * i + 4), njm_f (raw + po + 12 * i + 8));
	njm_put (&o, "],\n\"keys\": [");
	for (uint f = 0; f < nfr; f++)
	{
		njm_put (&o, "%s\n[", f ? "," : "");
		for (uint k = 0; k < nn; k++)
		{
			const u8 *p = raw + ko + 4 * ((size_t)f * nn + k);
			njm_put (&o, "%s[%u,%u]", k ? "," : "", rd_be16 (p), rd_be16 (p + 2));
		}
		njm_put (&o, "]");
	}
	njm_put (&o, "\n]\n}\n");
	enumError err = ERR_OK;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ATH-ANM:%s (%u nodes, %u frames) -> %s/anim.json\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, nn, nfr, dest);
	if (!testmode)
	{
		if (!o.b)
			err = ERR_OUT_OF_MEMORY;
		else
			err = shade_write (arg, dest, basedir, "anim.json", (u8 *)o.b, (uint)o.used);
	}
	FREE (o.b);
	FREE (raw);
	return err;
}
