#include "lib-brstm.h"
// Nintendo NW4R/NW4F/NW4C streams -- BRSTM ("RSTM", Wii), BFSTM ("FSTM", Wii U / Switch), BCSTM ("CSTM", 3DS): decoded natively to
// "<name>.wav" (every channel, loop start/end kept in a 'smpl' chunk) with DecodeBRSTM() instead of handing the file to mobipeg.
// Streams this decoder cannot read (unknown codec, Switch Opus, ...) return NOTHING_TO_DO so the media pass-through still gets them.
static enumError extract_brstm_stream_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!(is_ext (arg, ".brstm") || is_ext (arg, ".bfstm") || is_ext (arg, ".bcstm") || is_ext (arg, ".rstm"))
		|| (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x40 && (!memcmp (raw, "RSTM", 4) || !memcmp (raw, "FSTM", 4) || !memcmp (raw, "CSTM", 4)))
	{
		brstm_audio_t a;
		memset (&a, 0, sizeof (a));
		if (!DecodeBRSTM (&a, raw, sz) && a.channels >= 1 && a.n_samples > 0 && a.sample_rate > 0)
		{
			const uint ch = a.channels, rate = a.sample_rate;
			const size_t ns = (size_t)a.n_samples, bytes = ns * ch * 2;
			const bool loop = a.loop && a.loop_start >= 0 && a.loop_start < (s64)ns;
			const size_t extra = loop ? 68 : 0;
			u8 *w = MALLOC (44 + bytes + extra);
			if (w)
			{
#define W32(o, v) do { w[o] = (v); w[(o) + 1] = (v) >> 8; w[(o) + 2] = (v) >> 16; w[(o) + 3] = (v) >> 24; } while (0)
				memcpy (w, "RIFF", 4);
				W32 (4, (u32)(36 + bytes + extra));
				memcpy (w + 8, "WAVEfmt ", 8);
				W32 (16, 16);
				w[20] = 1; w[21] = 0; w[22] = ch; w[23] = 0;
				W32 (24, rate);
				W32 (28, rate * ch * 2);
				w[32] = ch * 2; w[33] = 0; w[34] = 16; w[35] = 0;
				memcpy (w + 36, "data", 4);
				W32 (40, (u32)bytes);
				for (size_t i = 0; i < ns; i++)
					for (uint c = 0; c < ch; c++)
					{
						const u16 v = (u16)a.pcm[c][i];
						w[44 + 2 * (i * ch + c)] = v;
						w[45 + 2 * (i * ch + c)] = v >> 8;
					}
				if (loop)
				{
					u8 *s = w + 44 + bytes;
					memset (s, 0, 68);
					memcpy (s, "smpl", 4);
					W32 (4 + (s - w), 60);
					W32 (8 + 8 + (s - w), 1000000000u / rate);
					W32 (8 + 12 + (s - w), 60);
					W32 (8 + 28 + (s - w), 1);
					W32 (8 + 36 + 8 + (s - w), (u32)a.loop_start);
					W32 (8 + 36 + 12 + (s - w), (u32)ns - 1);
				}
#undef W32
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot)
					strcpy (dot, ".wav");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE %s:%s (%u ch, %u Hz, %zu samples, %s%s) -> WAV\n", verbose > 0 ? "\n" : "",
						testmode ? "WOULD " : "", a.variant == BRSTM_VARIANT_FSTM ? "BFSTM" : a.variant == BRSTM_VARIANT_CSTM ? "BCSTM" : "BRSTM",
						arg, ch, rate, ns, a.is_adpcm ? "DSP-ADPCM" : "PCM16", loop ? ", loop" : "");
				err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, w, (uint)(44 + bytes + extra));
				FREE (w);
			}
		}
		FreeBRSTMAudio (&a);
	}
	FREE (raw);
	return err;
}
