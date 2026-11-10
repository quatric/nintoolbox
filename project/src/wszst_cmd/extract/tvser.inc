// Thrillville: Off the Rails serialised objects (".prk" "PARK" park save, ".tbp" "TRAK" track piece, ".atr" "ATTR" attraction), magic + a packed token stream written by the game's
// stream class (vtable 0x805bd118 in the DOL, encoder 0x80389c80). Token by first byte: 00-7f signed 7 bit int; 80-bf 4 byte float (30 bits: low 6 bits of the first byte, then
// 8/8/8 bits LE; sign at bit 29, exponent - 96 in 6 bits at 23.., 23 bit mantissa); c0-cf signed 12 bit (low nibble + 1 byte << 4); d0-df signed 20 bit; e0-ef signed 28 bit; f0 / f1 32 bit
// LE; f2 / f3 false / true; f4-f7 signed 10 bit milli fixed point (/1000); f8 raw LE float32; fa u8; fb u16 LE; fc string (chars until one with bit 7 set, which carries the last char);
// ff end marker. The first token is a version (109 for ATTR / TRAK, 113 for PARK). The fields' meaning follows the game's own order (PARK: LevelNumber, GameTime, Money,
// Marketing, Research, Attractions, Groups, Guests, ...) and is not annotated; the stream is written as "<file>.txt" one token per space, 24 per line (`f=` floats, `m=` milli floats,
// `"..."` strings, `;` end marker); text stored as arrays of character codes (6+ printable integer tokens, mostly letters) is also listed behind the stream as `# token N: "text"` lines.
static enumError extract_tvser_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!(is_ext (arg, ".prk") || is_ext (arg, ".tbp") || is_ext (arg, ".atr")) || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 8 || (memcmp (raw, "PARK", 4) && memcmp (raw, "TRAK", 4) && memcmp (raw, "ATTR", 4)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".txt");
	if (testmode)
	{
		fprintf (stdlog, "WOULD EXTRACT %.4s:%s -> TXT:%s\n", raw, arg, dest);
		FREE (raw);
		return ERR_OK;
	}
	const size_t cap = sz * 24 + 256;
	char *t = MALLOC (cap);
	size_t tl = snprintf (t, cap, "%.4s\n", raw);
	uint nt = 0, bad = 0;
	int *iv = MALLOC ((sz + 1) * sizeof (int));	// integer value of every token (INT_MIN for the other kinds), to find text stored as arrays of character codes
	size_t i = 4;
#define SX(v, b) ((int32_t)((u32)(v) << (32 - (b))) >> (32 - (b)))
	while (i < sz && tl + 64 < cap)
	{
		const u8 c = raw[i];
		char tok[320];
		size_t need = c < 0x80 ? 1 : c < 0xc0 ? 4 : c < 0xd0 ? 2 : c < 0xe0 ? 3 : c < 0xf0 ? 4 : c <= 0xf1 ? 5 : c <= 0xf3 ? 1 : c <= 0xf7 ? 2 : c == 0xf8 ? 5 : c == 0xfa ? 2 : c == 0xfb ? 3 : 1;
		if (i + need > sz && c != 0xfc && c != 0xf8)
			break;
		if (c < 0x80)
			snprintf (tok, sizeof (tok), "%d", SX (c, 7)), i++;
		else if (c < 0xc0)
		{
			const u32 v = (c & 0x3f) | raw[i + 1] << 6 | raw[i + 2] << 14 | (u32)raw[i + 3] << 22;
			const u32 bits = ((v >> 29) & 1) << 31 | (((v >> 23) & 0x3f) + 96) << 23 | (v & 0x7fffff);
			float f;
			memcpy (&f, &bits, 4);
			snprintf (tok, sizeof (tok), "f=%.9g", f), i += 4;
		}
		else if (c < 0xd0)
			snprintf (tok, sizeof (tok), "%d", SX ((c & 15) | raw[i + 1] << 4, 12)), i += 2;
		else if (c < 0xe0)
			snprintf (tok, sizeof (tok), "%d", SX ((c & 15) | raw[i + 1] << 4 | raw[i + 2] << 12, 20)), i += 3;
		else if (c < 0xf0)
			snprintf (tok, sizeof (tok), "%d", SX ((c & 15) | raw[i + 1] << 4 | raw[i + 2] << 12 | (u32)raw[i + 3] << 20, 28)), i += 4;
		else if (c <= 0xf1)
			snprintf (tok, sizeof (tok), "%s%d", c == 0xf0 ? "" : "u", (int)(raw[i + 1] | raw[i + 2] << 8 | raw[i + 3] << 16 | (u32)raw[i + 4] << 24)), i += 5;
		else if (c <= 0xf3)
			snprintf (tok, sizeof (tok), "%s", c == 0xf3 ? "true" : "false"), i++;
		else if (c <= 0xf7)
			snprintf (tok, sizeof (tok), "m=%.3f", SX ((c & 3) | raw[i + 1] << 2, 10) / 1000.0), i += 2;
		else if (c == 0xf8 && i + 5 <= sz && raw[i + 1] != 0xff)
		{
			const u32 bits = raw[i + 1] | raw[i + 2] << 8 | raw[i + 3] << 16 | (u32)raw[i + 4] << 24;
			float f;
			memcpy (&f, &bits, 4);
			snprintf (tok, sizeof (tok), "f=%.9g", f), i += 5;
		}
		else if (c == 0xfa)
			snprintf (tok, sizeof (tok), "u8:%u", raw[i + 1]), i += 2;
		else if (c == 0xfb)
			snprintf (tok, sizeof (tok), "u16:%u", raw[i + 1] | raw[i + 2] << 8), i += 3;
		else if (c == 0xfc)
		{
			size_t j = i + 1, k = 0;
			tok[k++] = '"';
			while (j < sz && !(raw[j] & 0x80) && k < sizeof (tok) - 4)
				tok[k++] = raw[j] >= 0x20 && raw[j] < 0x7f && raw[j] != '"' ? raw[j] : '?', j++;
			if (j < sz && k < sizeof (tok) - 4)
				tok[k++] = (raw[j] & 0x7f) >= 0x20 && (raw[j] & 0x7f) != '"' ? raw[j] & 0x7f : '?', j++;
			tok[k++] = '"';
			tok[k] = 0;
			i = j;
		}
		else if (c == 0xff || c == 0xf8)
			snprintf (tok, sizeof (tok), c == 0xff ? ";" : "f8"), i++;
		else
			snprintf (tok, sizeof (tok), "?%02x", c), i++, bad++;
		{
			char *e;
			const long v = strtol (tok, &e, 10);
			iv[nt] = !*e && *tok && *tok != 'u' ? (int)v : INT_MIN;
		}
		tl += snprintf (t + tl, cap - tl, "%s%s", nt % 24 ? " " : (nt ? "\n" : ""), tok);
		nt++;
	}
	tl += snprintf (t + tl, cap - tl, "\n");
	// character arrays: runs of 6 or more integer tokens that are printable ASCII codes, mostly letters (e.g. "%PreBuiltOFFTHERAILS"); listed as "# token N: text"
	for (uint a = 0, b; a < nt; a = b)
	{
		for (b = a; b < nt && iv[b] >= 32 && iv[b] < 127; b++)
			;
		if (b - a >= 6)
		{
			uint al = 0;
			for (uint k = a; k < b; k++)
				al += (iv[k] | 0x20) >= 'a' && (iv[k] | 0x20) <= 'z';
			if (al * 10 >= (b - a) * 7 && tl + (b - a) + 64 < cap)
			{
				tl += snprintf (t + tl, cap - tl, "# token %u: \"", a);
				for (uint k = a; k < b; k++)
					t[tl++] = iv[k];
				tl += snprintf (t + tl, cap - tl, "\"\n");
			}
		}
		if (b == a)
			b++;
	}
	FREE (iv);
	FREE (raw);
	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (t, 1, tl, F.f) != tl)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", tl, dest);
	ResetFile (&F, opt_preserve);
	FREE (t);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT SERIAL:%s -> TXT:%s (%u tokens%s)\n", verbose > 0 ? "\n" : "", arg, dest, nt, bad ? ", unknown bytes present" : "");
	return err;
}
