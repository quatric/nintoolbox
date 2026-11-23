// Shark 3D pack files (".s3d_pack" of Aya and the Cubes of Light and other WiiWare games): the text "shark3d_packstream_ident", a version string and three big-endian u64 values start the file; the data from 0x1000 on is a sequence of 0x1000-byte pages
// whose last 4 bytes are a checksum. Their payload bytes are run through a fixed 256 byte substitution table (an involution, the pack reader's own table). The u64 values are the offset of an index behind the entries, the end of the data and 24; in the
// substituted stream the entries start at 24: {path with every byte inverted, NUL, u8 type, u7 little-endian length of the payload with the top bit set on the last byte, payload}. Type 1 is stored, type 2 and 3 are zlib streams and type 0 is an empty file.
static const u8 shark3d_sbox[256] =
{
	0x9f, 0xf0, 0xeb, 0x2d, 0xc9, 0x67, 0x69, 0xa2, 0x72, 0xb5, 0x53, 0xb9, 0x3a, 0xab, 0x2f, 0x99,
	0x83, 0x3c, 0xec, 0x9c, 0x73, 0xb7, 0x50, 0x6a, 0x87, 0x88, 0x7f, 0x4f, 0x45, 0x25, 0x5a, 0x75,
	0x30, 0x89, 0x92, 0xc4, 0xea, 0x1d, 0x54, 0x8f, 0xfc, 0xff, 0xa5, 0x64, 0xc0, 0x03, 0xd8, 0x0e,
	0x20, 0x96, 0x48, 0xa8, 0xfe, 0x66, 0xa9, 0xdf, 0x91, 0xf7, 0x0c, 0x4d, 0x11, 0xd5, 0x80, 0x68,
	0x4c, 0xb2, 0xb0, 0xe2, 0x5c, 0x1c, 0xce, 0xf9, 0x32, 0xda, 0xc5, 0x95, 0x40, 0x3b, 0xba, 0x1b,
	0x16, 0xaf, 0x8e, 0x0a, 0x26, 0x98, 0x6d, 0xb1, 0xd0, 0x8b, 0x1e, 0xb8, 0x44, 0xfa, 0xcd, 0x86,
	0xa3, 0x9a, 0xc2, 0xa7, 0x2b, 0x8c, 0x35, 0x05, 0x3f, 0x06, 0x17, 0xf5, 0x84, 0x56, 0xc1, 0xf6,
	0xfb, 0xc6, 0x08, 0x14, 0xca, 0x1f, 0xee, 0xf4, 0xe0, 0xdc, 0xbb, 0x7e, 0x85, 0xcc, 0x7b, 0x1a,
	0x3e, 0xd9, 0x90, 0x10, 0x6c, 0x7c, 0x5f, 0x18, 0x19, 0x21, 0x9e, 0x59, 0x65, 0xed, 0x52, 0x27,
	0x82, 0x38, 0x22, 0xb3, 0xbf, 0x4b, 0x31, 0xf1, 0x55, 0x0f, 0x61, 0xd3, 0x13, 0xa4, 0x8a, 0x00,
	0xe6, 0xb6, 0x07, 0x60, 0x9d, 0x2a, 0xe9, 0x63, 0x33, 0x36, 0xf3, 0x0d, 0xfd, 0xd7, 0xe4, 0x51,
	0x42, 0x57, 0x41, 0x93, 0xcb, 0x09, 0xa1, 0x15, 0x5b, 0x0b, 0x4e, 0x7a, 0xe7, 0xc3, 0xd4, 0x94,
	0x2c, 0x6e, 0x62, 0xbd, 0x23, 0x4a, 0x71, 0xf8, 0xd1, 0x04, 0x74, 0xb4, 0x7d, 0x5e, 0x46, 0xf2,
	0x58, 0xc8, 0xdd, 0x9b, 0xbe, 0x3d, 0xe8, 0xad, 0x2e, 0x81, 0x49, 0xe1, 0x79, 0xd2, 0xe3, 0x37,
	0x78, 0xdb, 0x43, 0xde, 0xae, 0xef, 0xa0, 0xbc, 0xd6, 0xa6, 0x24, 0x02, 0x12, 0x8d, 0x76, 0xe5,
	0x01, 0x97, 0xcf, 0xaa, 0x77, 0x6b, 0x6f, 0x39, 0xc7, 0x47, 0x5d, 0x70, 0x28, 0xac, 0x34, 0x29
};

static enumError extract_shark3dpack_file (ccp arg, ccp basedir, uint depth)
{
	static const char ident[] = "shark3d_packstream_ident";
	u8 magic[sizeof (ident)];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, sizeof (magic), f) == sizeof (magic) && !memcmp (magic, ident, sizeof (magic));
	if (f)
		fclose (f);
	if (!hit)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *x = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	uint cap = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x2000)
		goto done;
	x = MALLOC (sz);
	if (!x)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	u64 xl = 0;
	for (u64 o = 0x1000; o < sz; o += 0x1000)
	{
		const u64 n = sz - o < 0x1000 ? (sz - o > 4 ? sz - o - 4 : 0) : 0x1000 - 4;
		for (u64 i = 0; i < n; i++)
			x[xl + i] = shark3d_sbox[raw[o + i]];
		xl += n;
	}
	const u64 end = xl >= 24 ? (u64)rd_be32 (x + 4) : 0; // the high halves of the u64 values are 0
	if (end < 24 || end > xl)
		goto done;
	for (int pass = 0; pass < 2; pass++)
	{
		uint n = 0;
		u64 p = 24;
		while (p < end)
		{
			const u8 *nul = memchr (x + p, 0, end - p);
			if (!nul || nul == x + p || nul - (x + p) >= PATH_MAX || nul + 3 > x + end)
				break;
			char name[PATH_MAX];
			const uint nl = nul - (x + p);
			for (uint i = 0; i < nl; i++)
				name[i] = (char)~x[p + i];
			name[nl] = 0;
			const u8 type = nul[1];
			u64 q = nul + 2 - x, len = 0;
			uint shift = 0;
			while (q < end && shift < 35)
			{
				const u8 b = x[q++];
				len |= (u64)(b & 0x7f) << shift;
				shift += 7;
				if (b & 0x80)
					break;
			}
			if (q > end || len > end - q)
				break;
			if (pass)
			{
				u8 *buf = 0;
				u64 us = 0;
				bool ok = false;
				if (type == 1 || type == 0)
				{
					buf = MALLOC (len ? len : 1);
					if (buf)
						memcpy (buf, x + q, us = len), ok = true;
				}
				else if (type == 2 || type == 3)
				{
					u64 bsz = len * 4 + 256;
					buf = MALLOC (bsz);
					z_stream z;
					memset (&z, 0, sizeof (z));
					if (buf && inflateInit (&z) == Z_OK)
					{
						z.next_in = x + q;
						z.avail_in = len;
						int r;
						do
						{
							if (z.total_out == bsz)
							{
								u8 *nb = REALLOC (buf, bsz *= 2);
								if (!nb)
									break;
								buf = nb;
							}
							z.next_out = buf + z.total_out;
							z.avail_out = bsz - z.total_out;
							r = inflate (&z, Z_NO_FLUSH);
						} while (r == Z_OK);
						ok = r == Z_STREAM_END;
						us = z.total_out;
						inflateEnd (&z);
					}
				}
				if (ok && valid_sarc_path (name) && OwnedEntryAdd (entries, n, name, buf, (uint)us))
					n++;
				FREE (buf);
			}
			else
				n++;
			p = q + len;
		}
		if (!pass)
		{
			if (!n)
				break;
			cap = n;
			entries = CALLOC (cap, sizeof (*entries));
			if (!entries)
			{
				err = ERR_OUT_OF_MEMORY;
				goto done;
			}
		}
		else if (n)
		{
			err = write_owned_entries (arg, basedir, depth, "S3DPACK", entries, n);
			entries = 0;
		}
	}
done:
	if (entries)
		ResetOwnedEntries (entries, cap);
	FREE (x);
	FREE (raw);
	return err;
}
