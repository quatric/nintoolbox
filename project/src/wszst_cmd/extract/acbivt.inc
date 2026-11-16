// ACB Total / Planet Basket (Wii, Virtualware / Planeta) "*.ivt" textures. 8-byte header + the pixels, scrambled with a short repeating XOR key:
//   byte 0 = base + log2(width) + 8 * log2(height) with base 165 for 32-bit and 101 for 24-bit pixels; byte 1 = key variant (0: 3-byte key 99 44 87,
//   1: 5-byte key ba 8c ab 9e 99, 2: 7-byte key b0 8f be 93 9a 8d b2); bytes 2..7 = a constant per variant (2f cd 1a 36 d8 2f / 2f 33 11 04 02 31 /
//   39 37 0e 35 11 1a). After XOR with the key the data is BGRA8 (width * height * 4 bytes) or BGR8 (width * height * 3 bytes), power-of-two sizes
//   (the blue / red order follows from the skin textures coming out blue with RGB order). Written as "<file>.png".
// (The keys were recovered from known plaintext: every image starts with transparent / opaque-black pixels. The few files with another size
// (4096 / 8192 byte payloads: 27 / 91 header values) use another pixel format and are left alone.)
static enumError extract_acbivt_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".ivt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	static const u8 k0[] = { 0x99, 0x44, 0x87 }, k1[] = { 0xba, 0x8c, 0xab, 0x9e, 0x99 }, k2[] = { 0xb0, 0x8f, 0xbe, 0x93, 0x9a, 0x8d, 0xb2 };
	static const u8 hdr[3][6] = { { 0x2f, 0xcd, 0x1a, 0x36, 0xd8, 0x2f }, { 0x2f, 0x33, 0x11, 0x04, 0x02, 0x31 }, { 0x39, 0x37, 0x0e, 0x35, 0x11, 0x1a } };
	if (sz < 16 || raw[1] > 2 || memcmp (raw + 2, hdr[raw[1]], 6))
		goto done;
	{
		const u8 *key = raw[1] == 0 ? k0 : raw[1] == 1 ? k1 : k2;
		const uint kl = raw[1] == 0 ? 3 : raw[1] == 1 ? 5 : 7;
		const u64 nb = sz - 8;
		uint bpp = 0;
		u64 n = 0;
		if (!(nb % 4) && !((nb / 4) & (nb / 4 - 1)))
			bpp = 4, n = nb / 4;
		else if (!(nb % 3) && !((nb / 3) & (nb / 3 - 1)))
			bpp = 3, n = nb / 3;
		else
			goto done;
		uint s = 0;
		while (((u64)1 << s) < n)
			s++;
		const int v = (int)raw[0] - (bpp == 4 ? 165 : 101), t = 8 * (int)s - v;
		if (t < 0 || t % 7)
			goto done;
		const uint lw = t / 7;
		if (lw > s || lw > 13 || s - lw > 13)
			goto done;
		const uint w = 1u << lw, h = 1u << (s - lw);
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.png", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT IVT:%s -> PNG:%s (%ux%u, %u bytes per pixel)\n", arg, dest, w, h, bpp);
			err = ERR_OK;
			goto done;
		}
		u8 *rgba = MALLOC ((size_t)n * 4);
		if (!rgba)
			goto done;
		for (size_t i = 0; i < (size_t)n; i++)
		{
			u8 px[4] = { 0, 0, 0, 255 };
			for (uint c = 0; c < bpp; c++)
			{
				const size_t o = i * bpp + c;
				px[c] = raw[8 + o] ^ key[o % kl];
			}
			rgba[i * 4] = px[2], rgba[i * 4 + 1] = px[1], rgba[i * 4 + 2] = px[0], rgba[i * 4 + 3] = px[3];
		}
		err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
		if (verbose >= 0 && !err)
			fprintf (stdlog, "%sEXTRACT IVT:%s -> PNG:%s (%ux%u)\n", verbose > 0 ? "\n" : "", arg, dest, w, h);
	}
done:
	FREE (raw);
	return err;
}
