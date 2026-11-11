// Proteus engine "SDASSETF" bundles (".ast", I Spy / Goosebumps HorrorLand, Wii): every BMAP chunk holds one GX texture (CMPR, RGBA8, or C4/C8 with an RGB5A3
// CLUT child). Each texture is wrapped in a synthetic TPL and decoded to "<file>.d/<name>_<index>.png" in decode mode (xx). The structure listing
// ("<file>.d/<file>.txt", wii_text_manifests.inc) is written as well, because this handler runs before it and would otherwise hide it.
#include "lib-ispyspookymansion.h"

typedef struct proteus_ctx_t
{
	ccp arg, basedir, dest;
	uint n;
	enumError err;
	ccp want; // when set only the texture of this name (case insensitive) is written, as "<want>.png"
	bool done;
} proteus_ctx_t;

static int proteus_texture_cb (const SpookyAstTexture_t *t, void *vctx)
{
	proteus_ctx_t *c = vctx;
	if (c->want && strcasecmp (c->want, t->name))
		return 0;
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
	snprintf (name, sizeof (name), "%s", c->want ? c->want : t->name[0] ? t->name : "texture");
	for (char *s = name; *s; s++)
		if (*s == '/' || *s == '\\' || *s == ':')
			*s = '_';
	char tmp[PATH_MAX], png[PATH_MAX], tmp_png[PATH_MAX], fn[PATH_MAX];
	snprintf (fn, sizeof (fn), "%s.%03u.tmp.tpl", name, c->n);
	snprintf (tmp, sizeof (tmp), "%s/%s%s", c->dest, c->basedir ? c->basedir : "", fn);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	if (c->want)
		snprintf (png, sizeof (png), "%s/%s%s.png", c->dest, c->basedir ? c->basedir : "", name);
	else
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
	if (c->want)
		c->done = true;
	return c->want ? 1 : 0; // a wanted texture is written once
}

// Models reference textures by name; the texture bundles (big-endian ".ast" files of the same directory) are searched for each name and written
// next to the GLB as "<name>.png", where the GLB exporter picks them up.
typedef struct proteus_model_ctx_t
{
	ccp arg, basedir, dest;
	enumError err;
	char written[64][64]; // texture names already handled
	uint n_written;
	uint n_models;
} proteus_model_ctx_t;

static void proteus_find_texture (proteus_model_ctx_t *mc, ccp texname)
{
	for (uint i = 0; i < mc->n_written; i++)
		if (!strcasecmp (mc->written[i], texname))
			return;
	if (mc->n_written < 64)
		snprintf (mc->written[mc->n_written++], 64, "%s", texname);
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", mc->arg);
	char *sl = strrchr (dir, '/');
	if (sl)
		*sl = 0;
	else
		snprintf (dir, sizeof (dir), ".");
	DIR *d = opendir (dir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir (d)))
	{
		const size_t n = strlen (de->d_name);
		if (n < 5 || strcasecmp (de->d_name + n - 4, ".ast"))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", dir, de->d_name);
		if (!strcmp (path, mc->arg))
			continue;
		u8 *raw = 0;
		size_t sz = 0;
		if (LoadFileAlloc (path, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		{
			FREE (raw);
			continue;
		}
		proteus_ctx_t ctx = { mc->arg, mc->basedir, mc->dest, 0, ERR_OK, texname, false };
		if (IsSpookyAst (raw, sz, sz))
			SpookyAstForEachTexture (raw, sz, proteus_texture_cb, &ctx);
		FREE (raw);
		if (ctx.done)
			break;
	}
	closedir (d);
}

static int proteus_model_cb (const char *name, model_t *model, void *vctx)
{
	proteus_model_ctx_t *mc = vctx;
	for (size_t i = 0; i < model->num_meshes; i++)
	{
		const int mi = model->meshes[i].material_idx;
		if (mi >= 0 && (size_t)mi < model->num_materials && model->materials[mi].num_textures)
			proteus_find_texture (mc, model->materials[mi].textures[0]);
	}
	char fn[PATH_MAX], path[PATH_MAX];
	snprintf (fn, sizeof (fn), "%s", name[0] ? name : "model");
	for (char *s = fn; *s; s++)
		if (*s == '/' || *s == '\\' || *s == ':')
			*s = '_';
	snprintf (path, sizeof (path), "%s/%s%s.glb", mc->dest, mc->basedir ? mc->basedir : "", fn);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PROTEUS MODEL:%s %s -> GLB:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", mc->arg, name, path);
	if (!testmode)
		ExportModelToGLB (model, path);
	mc->n_models++;
	return 0;
}

static enumError extract_proteus_ast_file (ccp arg, ccp basedir, uint depth)
{
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
	// the structure listing goes first: it creates the output directory
	const enumError mf = extract_wii_text_manifest_file (arg, basedir, depth);
	proteus_ctx_t ctx = { arg, basedir, dest, 0, ERR_OK, 0, false };
	const int n = SpookyAstForEachTexture (raw, sz, proteus_texture_cb, &ctx);
	proteus_model_ctx_t mctx;
	memset (&mctx, 0, sizeof (mctx));
	mctx.arg = arg;
	mctx.basedir = basedir;
	mctx.dest = dest;
	mctx.err = ERR_OK;
	const int nm = SpookyAstForEachModel (raw, sz, proteus_model_cb, &mctx);
	FREE (raw);
	// neither textures nor models: nothing to add to the structure listing
	if (n <= 0 && nm <= 0)
		return mf;
	return ctx.err ? ctx.err : mctx.err ? mctx.err : mf == ERR_NOTHING_TO_DO ? ERR_OK : mf;
}
