// Ubisoft ABE BigFiles are used by Rabbids Go Home (Wii) for the game's
// actual data.  They are not the older BIG/BUG Jade archives: their index is
// a pair of linked tables and individual members may carry a 32-byte engine
// header (including an LZO1X block stream).  Keep this reader deliberately
// stream based -- RGH.BF alone is almost 1 GiB, so loading it through the SZS
// path would needlessly hit the generic maximum-file-size limit.
static u32 get_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static bool read_abe_at (FILE *f, u64 off, void *buf, size_t size)
{
	return off <= LONG_MAX && !fseek (f, (long)off, SEEK_SET) && fread (buf, 1, size, f) == size;
}

static bool valid_abe_name (ccp name)
{
	return name && *name && !strstr (name, "..") && !strchr (name, '/') && !strchr (name, '\\');
}

static enumError write_abe_member (FILE *in, u64 off, u32 size, ccp path, ccp source)
{
	File_t out;
	enumError err = CreateFileOpt (&out, true, path, false, source);
	if (err || !out.f)
		return err;
	u8 buf[0x10000];
	if (!read_abe_at (in, off, buf, 0))
		err = ERR_READ_FAILED;
	for (u32 left = size; !err && left;)
	{
		const size_t want = left < sizeof (buf) ? left : sizeof (buf);
		const size_t got = fread (buf, 1, want, in);
		if (got != want || fwrite (buf, 1, got, out.f) != got)
			err = ERR_WRITE_FAILED;
		left -= got;
	}
	ResetFile (&out, opt_preserve);
	return err;
}
