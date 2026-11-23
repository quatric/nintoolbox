// "FMPP" images (".ppm" of Where's Wally: Fantastic Journey, also inside ".GWVideo" files), big-endian: {"FMPP", 00 11 01 00, u8 2, u8 has-alpha-plane, 8, 1, u32, three u16 width/height pairs (image size, size padded to 8x4 tiles, a third size),
// u32 unpacked size, u32 packed size, u32 header size 40}. The packed data is a FastLZ level 2 stream (as the leading bit 0x20 of its first byte says) that unpacks to: u32 id, u16 512, u16 512, u32 offsets of two 512 byte palette planes
// (plane 1: R,G of 256 entries, plane 2: 0,B), u32 size and u32 offset of the alpha image, u32 size and u32 offset of the colour image. The images are 8 bit, in 8x4 tiles; the colour image indexes the palette, the alpha image holds the alpha.
// Written as "<file>.d/image.tga" (32 bit, which the TGA decoder turns into a PNG) and "<file>.d/unpacked.bin". A frame whose sizes do not fit is left alone.
static bool fmpp_unpack (const u8 *in, size_t n, u8 *out, size_t outsz)
{
	if (!n || (in[0] >> 5) != 1)
		return false;
	const u8 *ip = in, *end = in + n;
	size_t op = 0;
	u32 ctrl = *ip++ & 31;
	for (;;)
	{
		if (ctrl >= 32)
		{
			u32 len = (ctrl >> 5) - 1;
			size_t ofs = (ctrl & 31) << 8;
			if (len == 6)
			{
				u32 code;
				do
				{
					if (ip >= end)
						return false;
					code = *ip++;
					len += code;
				} while (code == 255);
			}
			if (ip >= end)
				return false;
			u32 code = *ip++;
			size_t back = ofs + code + 1;
			if (code == 255 && ofs == (31 << 8))
			{
				if (ip + 2 > end)
					return false;
				back = (size_t)((ip[0] << 8) + ip[1]) + 8191 + 1;
				ip += 2;
			}
			len += 3;
			if (back > op || len > outsz - op)
				return false;
			for (u32 i = 0; i < len; i++, op++)
				out[op] = out[op - back];
		}
		else
		{
			const u32 run = ctrl + 1;
			if (run > (size_t)(end - ip) || run > outsz - op)
				return false;
			memcpy (out + op, ip, run);
			ip += run;
			op += run;
		}
		if (ip >= end)
			break;
		ctrl = *ip++;
	}
	return op == outsz;
}

static enumError extract_fmpp_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *out = 0, *tga = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".ppm") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "FMPP", 4) || rd_be32 (raw + 0x24) != 0x28)
		goto done;
	const u64 usz = rd_be32 (raw + 0x1c), psz = rd_be32 (raw + 0x20);
	const uint w = rd_be16 (raw + 0x10), h = rd_be16 (raw + 0x12), pw = rd_be16 (raw + 0x14), ph = rd_be16 (raw + 0x16);
	if (!w || !h || pw < w || ph < h || pw % 8 || ph % 4 || usz < 0x40 || usz > 0x10000000 || psz > sz - 0x28 || usz > 0x10000000)
		goto done;
	out = MALLOC (usz);
	if (!out || !fmpp_unpack (raw + 0x28, psz, out, usz))
		goto done;
	const u64 pal1 = rd_be32 (out + 8), pal2 = rd_be32 (out + 12), asz = rd_be32 (out + 0x10), aoff = rd_be32 (out + 0x14), csz = rd_be32 (out + 0x18),
		  coff = rd_be32 (out + 0x1c), plane = (u64)pw * ph;
	if (csz != plane || coff > usz || plane > usz - coff || pal1 + 512 > usz || pal2 + 512 > usz)
		goto done;
	const bool alpha = asz == plane && aoff <= usz && plane <= usz - aoff;
	tga = CALLOC (18 + (size_t)w * h * 4, 1);
	entries = CALLOC (2, sizeof (*entries));
	if (!tga || !entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	tga[2] = 2;
	tga[12] = w & 255, tga[13] = w >> 8, tga[14] = h & 255, tga[15] = h >> 8;
	tga[16] = 32;
	tga[17] = 0x28; // 8 alpha bits, origin at the top left
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const u64 p = ((u64)(y / 4) * (pw / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8, c = out[coff + p];
			u8 *d = tga + 18 + ((size_t)y * w + x) * 4;
			d[0] = out[pal2 + 2 * c + 1]; // B
			d[1] = out[pal1 + 2 * c + 1]; // G
			d[2] = out[pal1 + 2 * c]; // R
			d[3] = alpha ? out[aoff + p] : 255;
		}
	uint n = 0;
	if (OwnedEntryAdd (entries, n, "image.tga", tga, 18 + w * h * 4))
		n++;
	if (OwnedEntryAdd (entries, n, "unpacked.bin", out, (uint)usz))
		n++;
	err = write_owned_entries (arg, basedir, depth, "FMPP", entries, n);
	entries = 0;
done:
	if (entries)
		ResetOwnedEntries (entries, 2);
	FREE (tga);
	FREE (out);
	FREE (raw);
	return err;
}
