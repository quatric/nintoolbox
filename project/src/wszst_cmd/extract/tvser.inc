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
	// PARK version 113: the first fields follow the park writer FUN_8037f1fc (tags LevelNumber, GameTime, Money, Marketing, Research, Attractions, ... are only debug labels there, the stream
	// carries just the values). Every writer call is exactly one token: LevelNumber 1 int; GameTime 3 floats; Money 142 tokens {3 ints, float (0.01), byte, 4 x 10 ints, 2 ints,
	// 5 graphed values of {16 floats, 2 bytes, bool}}; Marketing 9 {bool, float, int, short, 3 ints, bool, int}; Research 13 ints / bool; then Attractions: count, followed by entries (each
	// preceded by the number of skipped slots) of 0xf0 byte objects, then Groups, Guests, Staff, Memories, GraphedValues, Demographics, ParkAudio, ... (token 0 = version). Checked on the park saves:
	// the last graphed value of Money ends exactly at token 146 ("... 15 2 1"), token 169 is the attraction count (80 in Explorerville) and uninitialised ints show as -842150451 (0xCDCDCDCD).
	if (!memcmp (t, "PARK\n113 ", 9) && nt > 170)
		tl += snprintf (t + tl, cap - tl, "# fields: token 0 version, 1 LevelNumber, 2-4 GameTime (3 floats), 5-146 Money, 147-155 Marketing, 156-168 Research, 169 Attractions count (then the entries)\n");
	// ATTR (versions 110 and up): the attraction entry writer FUN_8032a874 (the same entry is stored in PARK attraction lists) emits, one token per call: kind (4 bit), 8 small bit fields, a vector of
	// 4 floats, 3 more bit fields, then by kind: 6 (ride on a track) the TRAK writer FUN_80214c00 {id, 3 floats, 4 bit field, flag, node count N, N node tokens} + 5 tokens (2 floats, int, 2 bytes,
	// FUN_80351fd8) + 3 floats + 5 bit + flag; 8 (minigolf grid) width, height, width x height bytes, 3 floats; 7: 3 floats; then a counted byte list, 10 bit fields, 9 floats, a counted signed byte list,
	// kind 6: 3 x 3 bytes, the name as 32 x u16 character codes, 3 x u16, a 32 bit flag word and a flag. Checked on the ATTR files of versions 110-112 (the token counts match exactly for 117 of 120
	// kind 6 files and the two kind 8 files); older versions lack one token and a few kind 6 files differ (not understood).
	if (!memcmp (t, "ATTR\n", 5) && nt > 40 && iv[0] >= 110 && (iv[1] == 6 || iv[1] == 7 || iv[1] == 8))
	{
		char fl[1400];
		size_t fn = snprintf (fl, sizeof (fl), "# fields: 0 version, 1 kind, 2-9 bit fields, 10-13 vector (4 floats), 14-16 bit fields");
		uint p = 17;
		bool ok = true;
#define SEG(n, txt) do { const uint n_ = (n); if (ok && n_ < 4096 && p + n_ <= nt && fn + 120 < sizeof (fl)) { fn += snprintf (fl + fn, sizeof (fl) - fn, ", %u-%u " txt, p, p + n_ - 1); p += n_; } else ok = false; } while (0)
#define CNT(txt) do { if (ok && p < nt && iv[p] >= 0 && iv[p] < 256 && p + 1 + (uint)iv[p] <= nt) { const uint c_ = (uint)iv[p]; fn += snprintf (fl + fn, sizeof (fl) - fn, ", %u count + %u " txt, p, c_); p += 1 + c_; } else ok = false; } while (0)
		const int kind = iv[1];
		if (kind == 6)
		{
			SEG (1, "track id");
			SEG (3, "track position (3 floats)");
			SEG (2, "track bit field, flag");
			if (ok && p < nt && iv[p] >= 0 && iv[p] < 1000 && p + 1 + (uint)iv[p] <= nt)
			{
				fn += snprintf (fl + fn, sizeof (fl) - fn, ", %u node count + %u node tokens", p, (uint)iv[p]);
				p += 1 + (uint)iv[p];
			}
			else
				ok = false;
			SEG (5, "2 floats, int, 2 bytes");
			SEG (3, "3 floats");
			SEG (2, "bit field, flag");
		}
		else if (kind == 8 && iv[17] >= 0 && iv[18] >= 0 && iv[17] < 64 && iv[18] < 64)
		{
			const uint gw = (uint)iv[17] * (uint)iv[18];
			SEG (2, "grid size");
			SEG (gw, "grid bytes");
			SEG (3, "3 floats");
		}
		else if (kind == 7)
			SEG (3, "3 floats");
		else
			ok = false;
		CNT ("bytes");
		SEG (10, "bit fields");
		SEG (9, "9 floats");
		CNT ("signed bytes");
		if (kind == 6)
			SEG (9, "3 x 3 bytes");
		SEG (32, "name (32 x u16)");
		SEG (5, "3 x u16, flag word, flag");
		if (ok && p + 1 == nt)
			tl += snprintf (t + tl, cap - tl, "%s\n", fl);
#undef SEG
#undef CNT
	}
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
