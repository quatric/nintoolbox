// Nintendo GameCube/Wii/Switch streamed audio (".ast", magic "STRM" big endian or "MRTS" little endian: Pikmin 2, Mario Kart Double Dash!!,
// Super Mario Galaxy, Pac-Man Vs.) decoded to "<name>.wav". Layout after vgmstream's ast.c / blocked_ast.c: u32 data size at 4 (file = size + 0x40), u16 codec
// at 8 (0 AFC, 1 PCM16 BE), u16 bits at 0xa (16), u16 channels at 0xc, u16 loop flag at 0xe, rate 0x10, samples 0x14, loop 0x18/0x1c; from 0x40 on blocks
// "BLCK" + u32 per-channel size + 0x18 padding, followed by every channel's data back to back. AFC frames are 9 bytes (scale/index byte + 16 nibbles).
static const s16 nast_afc_coef[16][2] = { { 0, 0 }, { 2048, 0 }, { 0, 2048 }, { 1024, 1024 }, { 4096, -2048 }, { 3584, -1536 }, { 3072, -1024 },
	{ 4608, -2560 }, { 4200, -2248 }, { 4800, -2300 }, { 5120, -3072 }, { 2048, -2048 }, { 1024, -1024 }, { -1024, 1024 }, { -1024, 0 }, { -2048, 0 } };

static enumError extract_nintendo_ast_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ast") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	const bool be = sz > 0x44 && !memcmp (raw, "STRM", 4);
	if (sz > 0x44 && (be || !memcmp (raw, "MRTS", 4)))
	{
		#define R32(p) (be ? (u32)((p)[0] << 24 | (p)[1] << 16 | (p)[2] << 8 | (p)[3]) : rd_le32 (p))
		#define R16(p) (be ? (uint)((p)[0] << 8 | (p)[1]) : (uint)((p)[0] | (p)[1] << 8))
		const uint codec = raw[8] << 8 | raw[9], ch = R16 (raw + 0xc), rate = R32 (raw + 0x10), frames = R32 (raw + 0x14);
		if ((uint64_t)R32 (raw + 4) + 0x40 == sz && codec <= 1 && ch >= 1 && ch <= 8 && rate && frames && frames < 0x8000000
			&& !memcmp (raw + 0x40, "BLCK", 4))
		{
			pcm = CALLOC ((size_t)frames * ch, sizeof (s16));
			int h1[8] = { 0 }, h2[8] = { 0 };
			uint done[8] = { 0 };
			size_t off = 0x40;
			while (pcm && off + 0x20 <= sz && !memcmp (raw + off, "BLCK", 4))
			{
				const size_t bs = R32 (raw + off + 4);
				const size_t data = off + 0x20;
				if (data + bs * ch > sz)
					break;
				for (uint c = 0; c < ch; c++)
				{
					const u8 *p = raw + data + bs * c;
					if (codec == 1)
					{
						for (size_t i = 0; i + 1 < bs && done[c] < frames; i += 2)
							pcm[(size_t)done[c]++ * ch + c] = (s16)(p[i] << 8 | p[i + 1]);
						continue;
					}
					for (size_t f = 0; f + 9 <= bs; f += 9)
					{
						const int scale = 1 << (p[f] >> 4);
						const int c1 = nast_afc_coef[p[f] & 15][0], c2 = nast_afc_coef[p[f] & 15][1];
						for (uint i = 0; i < 16 && done[c] < frames; i++)
						{
							int nib = i & 1 ? p[f + 1 + i / 2] & 15 : p[f + 1 + i / 2] >> 4;
							nib = nib >= 8 ? nib - 16 : nib;
							int s = ((nib * scale) << 11) + c1 * h1[c] + c2 * h2[c];
							s >>= 11;
							s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
							h2[c] = h1[c];
							h1[c] = s;
							pcm[(size_t)done[c]++ * ch + c] = (s16)s;
						}
					}
				}
				off = data + bs * ch;
			}
			uint got = frames;
			for (uint c = 0; c < ch; c++)
				if (done[c] < got)
					got = done[c];
			if (got)
			{
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".wav");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE NINTENDO-AST:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, got);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *w = nds_strm_make_wav (pcm, got, ch, rate, &wn);
					err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
					FREE (w);
				}
			}
		}
		#undef R32
		#undef R16
	}
	FREE (pcm);
	FREE (raw);
	return err;
}
