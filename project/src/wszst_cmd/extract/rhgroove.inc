#include "lib-tera.h"
#include "lib-zstd.h"
#include "lib-rhgroove.h"
// Rhythm Heaven Groove (Switch): ".cwv" DPCM sound -> "<name>.wav" (PCM16, loop in a 'smpl' chunk),
// ".wmb" tempo map -> "<file>.yml", ".tex" (zstd) texture atlas -> "<file>.png", ".spr" sprite set
// ->
// "<file>.yml". Each is validated by its footer/trailer/header first, so other games' files with
// the same extensions fall through to their own handlers.

static enumError rhg_write_wav (
	ccp arg, ccp basedir, const s16 *pcm, uint ch, uint rate, uint n, uint ls, uint le)
{
	const bool loop = le > ls && le <= n;
	const size_t bytes = (size_t)n * ch * 2, extra = loop ? 68 : 0;
	u8 *w = CALLOC (1, 44 + bytes + extra);
	if (!w)
		return ERR_OUT_OF_MEMORY;
#define W32(o, v)                                                                                  \
	do                                                                                             \
	{                                                                                              \
		const u32 v_ = (u32)(v);                                                                   \
		w[o] = (u8)v_;                                                                             \
		w[(o) + 1] = (u8)(v_ >> 8);                                                                \
		w[(o) + 2] = (u8)(v_ >> 16);                                                               \
		w[(o) + 3] = (u8)(v_ >> 24);                                                               \
	} while (0)
	memcpy (w, "RIFF", 4);
	W32 (4, 36 + bytes + extra);
	memcpy (w + 8, "WAVEfmt ", 8);
	W32 (16, 16);
	w[20] = 1;
	w[22] = (u8)ch;
	W32 (24, rate);
	W32 (28, rate * ch * 2);
	w[32] = (u8)(ch * 2);
	w[34] = 16;
	memcpy (w + 36, "data", 4);
	W32 (40, bytes);
	for (size_t i = 0; i < (size_t)n * ch; i++)
		w[44 + 2 * i] = (u8)pcm[i], w[45 + 2 * i] = (u8)((u16)pcm[i] >> 8);
	if (loop)
	{
		const size_t s = 44 + bytes;
		memcpy (w + s, "smpl", 4);
		W32 (s + 4, 60);
		W32 (s + 16, 1000000000u / rate); // sample period
		W32 (s + 20, 60); // unity note
		W32 (s + 36, 1); // one loop
		W32 (s + 52, ls);
		W32 (s + 56, le - 1); // inclusive end
	}
#undef W32
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dir (dest, sizeof (dest), arg);
	ccp sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot)
		strcpy (dot, ".wav");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE CWV:%s (%u ch, %u Hz, %u samples%s) -> WAV:%s/%s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, n,
			loop ? ", looped" : "", dest, name);
	enumError err
		= testmode ? ERR_OK : shade_write (arg, dest, basedir, name, w, (uint)(44 + bytes + extra));
	FREE (w);
	return err;
}

static enumError rhg_write_beside (
	ccp arg, ccp basedir, ccp label, ccp ext, const u8 *data, size_t size, ccp info)
{
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dir (dest, sizeof (dest), arg);
	ccp sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s%s", sl ? sl + 1 : arg, ext);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s%s -> %s/%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", label, arg, info ? info : "", dest, name);
	return testmode ? ERR_OK : shade_write (arg, dest, basedir, name, data, (uint)size);
}

static enumError extract_rhgroove_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool cwv = is_ext (arg, ".cwv"), wmb = is_ext (arg, ".wmb"), tex = is_ext (arg, ".tex"),
			   spr = is_ext (arg, ".spr");
	if ((!cwv && !wmb && !tex && !spr) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (cwv && IsRHGrooveCwv (raw, sz))
	{
		s16 *pcm = 0;
		uint ch, rate, n, ls, le;
		if (!DecodeRHGrooveCwv (raw, sz, &pcm, &ch, &rate, &n, &ls, &le))
			err = rhg_write_wav (arg, basedir, pcm, ch, rate, n, ls, le);
		FREE (pcm);
	}
	else if ((wmb && IsRHGrooveWmb (raw, sz)) || (spr && IsRHGrooveSpr (raw, sz)))
	{
		char *text = 0;
		size_t text_n = 0;
		if (!(wmb ? ConvertRHGrooveWmbToYaml : ConvertRHGrooveSprToYaml) (raw, sz, &text, &text_n))
			err = rhg_write_beside (
				arg, basedir, wmb ? "WMB" : "SPR", ".yml", (u8 *)text, text_n, 0);
		FREE (text);
	}
	else if (tex)
	{
		u8 *plain = raw;
		uint plain_n = (uint)sz;
		if (sz >= 4 && !memcmp (raw, "\x28\xb5\x2f\xfd", 4)
			&& DecodeZSTD (&plain, &plain_n, raw, (uint)sz))
			plain = 0;
		u8 *rgba = 0;
		uint w, h;
		if (plain && IsRHGrooveTex (plain, plain_n)
			&& !DecodeRHGrooveTex (plain, plain_n, &rgba, &w, &h))
		{
			u8 *png = 0;
			size_t png_n = 0;
			if (!EncodePngMem (rgba, w, h, 4, 8, &png, &png_n))
			{
				char info[32];
				snprintf (info, sizeof (info), " (%ux%u)", w, h);
				err = rhg_write_beside (arg, basedir, "RHG-TEX", ".png", png, png_n, info);
			}
			FREE (png);
		}
		FREE (rgba);
		if (plain != raw)
			FREE (plain);
	}
	FREE (raw);
	return err;
}
