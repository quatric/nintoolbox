// Momotarou Dentetsu 16 (Hudson) "message.bin": the game's text. Starts with a glyph catalogue (0x0110..0x0119, 0x0c 0x0f 0x00, 0x011a ...).
// Text is a stream of big endian 16 bit glyph codes 0x01xx / 0x02xx (the font pages); control bytes: 0x0c line break, 0x0c 0x0f 0x00 block
// break, 0x0a XX and 0x05 XX one byte commands (printed as {XX} tags). Known glyphs (order recovered from the DOL's character tables):
// 0x110 digits, 0x124 space, 0x125 '!', 0x128 A-Z, 0x142 a-z, 0x15c hiragana (81), 0x1ad katakana (81); everything else (kanji pages) is
// written as {gXXX}. Written as "<file>.txt".
static const char momo_hira[] = "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをんぁぃぅぇぉゃゅょっがぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽー";
static const char momo_kata[] = "アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲンァィゥェォャュョッガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポー";

static enumError extract_momomessage_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	static const u8 magic[] = { 1, 0x10, 1, 0x11, 1, 0x12, 1, 0x13, 1, 0x14, 1, 0x15, 1, 0x16, 1, 0x17, 1, 0x18, 1, 0x19, 0x0c, 0x0f, 0, 1, 0x1a };
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < sizeof (magic) + 2 || memcmp (raw, magic, sizeof (magic)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".txt");
	if (testmode)
	{
		fprintf (stdlog, "WOULD EXTRACT MOMOMESSAGE:%s -> TXT:%s\n", arg, dest);
		FREE (raw);
		return ERR_OK;
	}
	char *out = MALLOC (sz * 6 + 16);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	size_t o = 0;
	for (size_t i = 0; i < sz;)
	{
		const u8 b = raw[i];
		if (b == 0x0c)
		{
			if (i + 2 < sz && raw[i + 1] == 0x0f && raw[i + 2] == 0)
				out[o++] = '\n', out[o++] = '\n', i += 3;
			else
				out[o++] = '\n', i++;
		}
		else if ((b == 0x0a || b == 0x05) && i + 1 < sz)
			o += sprintf (out + o, "{%c%02x}", b == 0x0a ? 'a' : 'c', raw[i + 1]), i += 2;
		else if (b == 0 || b > 0x0b || i + 1 >= sz)
			o += sprintf (out + o, "{%02x}", b), i++;
		else
		{
			const uint g = b << 8 | raw[i + 1];
			i += 2;
			if (g >= 0x110 && g <= 0x119)
				out[o++] = '0' + (g - 0x110);
			else if (g == 0x124)
				out[o++] = ' ';
			else if (g == 0x125)
				out[o++] = '!';
			else if (g >= 0x128 && g < 0x128 + 26)
				out[o++] = 'A' + (g - 0x128);
			else if (g >= 0x142 && g < 0x142 + 26)
				out[o++] = 'a' + (g - 0x142);
			else if (g >= 0x15c && g < 0x15c + 81)
				memcpy (out + o, momo_hira + 3 * (g - 0x15c), 3), o += 3;
			else if (g >= 0x1ad && g < 0x1ad + 81)
				memcpy (out + o, momo_kata + 3 * (g - 0x1ad), 3), o += 3;
			else
				o += sprintf (out + o, "{g%03x}", g);
		}
	}
	FREE (raw);
	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (out, 1, o, F.f) != o)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", o, dest);
	ResetFile (&F, opt_preserve);
	FREE (out);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT MOMOMESSAGE:%s -> TXT:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
