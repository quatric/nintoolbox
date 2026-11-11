// Proteus engine "SDASSETF" bundles (".ast", I Spy / Goosebumps HorrorLand, Wii): every BMAP chunk holds one GX texture (CMPR, RGBA8, or C4/C8 with an RGB5A3
// CLUT child). Each texture is wrapped in a synthetic TPL and decoded to "<file>.d/<name>_<index>.png" in decode mode (xx). The structure listing
// ("<file>.d/<file>.txt", wii_text_manifests.inc) is written as well, because this handler runs before it and would otherwise hide it.
#include "lib-ispyspookymansion.h"

typedef struct proteus_ctx_t
{
	ccp arg, basedir, dest;
	uint n;
	enumError err;
} proteus_ctx_t;

static int proteus_texture_cb (const SpookyAstTexture_t *t, void *vctx)
{
	proteus_ctx_t *c = vctx;
	const bool pal = t->kind == 0x08 || t->kind == 0x09;
	const uint npal = pal ? (uint)(t->clut_size / 2) : 0;
	if (pal && (!npal || npal > 256))
		return 0;
	size_t need = (size_t)t->width * t->height;
	need = t->kind == 0x0e || t->kind == 0x08 ? need / 2 : t->kind == 0x06 ? need * 4 : need;

	const uint pal_off = 0x40, pal_len = (npal * 2 + 31) & ~31u;
	const uint data_off = pal ? pal_off + pal_len : 0x40;
	u8 *tpl = CALLOC (1, data_off + need);
	if (!tpl)
	{
		c->err = ERR_OUT_OF_MEMORY;
		return 1;
	}
	u8 *p = tpl;
#define PUT32(v) do { const uint v_ = (v); *p++ = v_ >> 24; *p++ = v_ >> 16; *p++ = v_ >> 8; *p++ = v_; } while (0)
	PUT32 (0x0020af30); PUT32 (1); PUT32 (0x0c);
	PUT32 (0x14); PUT32 (pal ? 0x30 : 0);
	*p++ = t->height >> 8; *p++ = t->height; *p++ = t->width >> 8; *p++ = t->width;
	PUT32 (t->kind); PUT32 (data_off); PUT32 (0); PUT32 (0); PUT32 (1); PUT32 (1); PUT32 (0);
	if (pal)
	{
		p = tpl + 0x30;
		*p++ = npal >> 8; *p++ = npal; *p++ = 0; *p++ = 0;
		PUT32 (2); PUT32 (pal_off);
		memcpy (tpl + pal_off, t->clut, npal * 2);
	}
#undef PUT32
	memcpy (tpl + data_off, t->pixels, need);

	// file name: the texture name with path separators replaced; repeated names get the index appended
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", t->name[0] ? t->name : "texture");
	for (char *s = name; *s; s++)
		if (*s == '/' || *s == '\\' || *s == ':')
			*s = '_';
	char tmp[PATH_MAX], png[PATH_MAX], tmp_png[PATH_MAX], fn[PATH_MAX];
	snprintf (fn, sizeof (fn), "%s.%03u.tmp.tpl", name, c->n);
	snprintf (tmp, sizeof (tmp), "%s/%s%s", c->dest, c->basedir ? c->basedir : "", fn);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	snprintf (png, sizeof (png), "%s/%s%s_%03u.png", c->dest, c->basedir ? c->basedir : "", name, c->n);
	c->n++;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE PROTEUS TEXTURE:%s %s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", c->arg, name, t->width, t->height, png);
	if (!testmode)
	{
		enumError err = shade_write (c->arg, c->dest, c->basedir, fn, tpl, data_off + (uint)need);
		if (!err)
			err = decode_image_if_possible (tmp);
		if (!err && rename (tmp_png, png))
			err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", png);
		unlink (tmp);
		if (err)
			c->err = err;
	}
	FREE (tpl);
	return 0;
}

static enumError extract_proteus_ast_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((export_count <= 0 && !opt_decode) || !is_ext (arg, ".ast"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (!IsSpookyAst (raw, sz, sz))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	proteus_ctx_t ctx = { arg, basedir, dest, 0, ERR_OK };
	const int n = SpookyAstForEachTexture (raw, sz, proteus_texture_cb, &ctx);
	FREE (raw);
	// no textures: leave the file to the text manifest handler
	if (n <= 0)
		return ERR_NOTHING_TO_DO;
	const enumError mf = extract_wii_text_manifest_file (arg, basedir, depth);
	return ctx.err ? ctx.err : mf == ERR_NOTHING_TO_DO ? ERR_OK : mf;
}
