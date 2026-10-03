// Atlus Nintendo DS / 3DS Archive System (*.ndx, *.idx, *.bin;
// Radiant Historia, Shin Megami Tensei, Etrian Odyssey; Nintendo DS / 3DS)
// See lib-atlusndx.h. Members are extracted hierarchically from the payload.

typedef struct atlus_extract_ctx_t
{
	const u8 *ndx;
	size_t ndx_size;
	const u8 *idx;
	size_t idx_size;
	FILE *fbin;
	u64 bin_size;
	ccp dest;
	ccp source_file;
	u32 count;
	enumError err;
} atlus_extract_ctx_t;

static void extract_atlus_node (atlus_extract_ctx_t *ctx, u32 dir_off, const char *rel_dir)
{
	if (ctx->err || dir_off + 2 > ctx->ndx_size)
		return;

	const u16 count = (u16)ctx->ndx[dir_off] | ((u16)ctx->ndx[dir_off + 1] << 8);
	size_t pos = dir_off + 2;

	for (uint i = 0; i < count && !ctx->err; i++)
	{
		if (pos + 6 > ctx->ndx_size)
		{
			ctx->err = ERR_INVALID_DATA;
			return;
		}
		const u16 nlen = (u16)ctx->ndx[pos] | ((u16)ctx->ndx[pos + 1] << 8);
		pos += 2;
		if (pos + nlen + 4 > ctx->ndx_size || nlen == 0 || nlen >= 256)
		{
			ctx->err = ERR_INVALID_DATA;
			return;
		}
		char name[256];
		memcpy (name, ctx->ndx + pos, nlen);
		name[nlen] = '\0';
		pos += nlen;

		const u32 child_off = (u32)ctx->ndx[pos] | ((u32)ctx->ndx[pos + 1] << 8) |
		                      ((u32)ctx->ndx[pos + 2] << 16) | ((u32)ctx->ndx[pos + 3] << 24);
		pos += 4;

		char subpath[PATH_MAX];
		if (rel_dir && *rel_dir)
			snprintf (subpath, sizeof (subpath), "%s/%s", rel_dir, name);
		else
			snprintf (subpath, sizeof (subpath), "%s", name);

		if (child_off != 0)
		{
			extract_atlus_node (ctx, child_off, subpath);
		}
		else
		{
			u32 bin_off = 0, bin_sz = 0;
			bool found = false;
			if (ctx->idx && ctx->idx_size)
				found = AtlusIdxLookup (ctx->idx, ctx->idx_size, subpath, &bin_off, &bin_sz);

			if (!found || (u64)bin_off + bin_sz > ctx->bin_size)
				continue;

			ctx->count++;

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/%s", ctx->dest, subpath);

			if (verbose > 0 || testmode)
				fprintf (stdlog, "%s  -> %s (%u bytes)\n",
				         testmode ? "WOULD EXTRACT" : "EXTRACT", out_path, bin_sz);

			if (!testmode)
			{
				File_t F;
				enumError ferr = CreateFileOpt (&F, true, out_path, false, ctx->source_file);
				if (!ferr && F.f && !fseeko (ctx->fbin, (off_t)bin_off, SEEK_SET))
				{
					u8 *buf = MALLOC (1 << 20);
					u32 left = bin_sz;
					while (buf && left)
					{
						const size_t n = fread (buf, 1, left < (1 << 20) ? left : (1 << 20), ctx->fbin);
						if (!n || fwrite (buf, 1, n, F.f) != n)
						{
							ferr = FILEERROR1 (&F, ERR_WRITE_FAILED,
							                  "Writing %u bytes failed: %s\n", bin_sz, out_path);
							break;
						}
						left -= (u32)n;
					}
					FREE (buf);
				}
				ResetFile (&F, opt_preserve);
				if (ferr)
				{
					ctx->err = ferr;
					return;
				}
			}
		}
	}
}

static void get_atlus_sibling_path (char *out, size_t out_size, ccp arg, ccp new_ext)
{
	char base[PATH_MAX];
	snprintf (base, sizeof (base), "%s", arg);
	char *dot = strrchr (base, '.');
	char *slash = strrchr (base, '/');
	if (dot && (!slash || dot > slash))
		*dot = 0;
	snprintf (out, out_size, "%s%s", base, new_ext);
}

static enumError extract_atlusndx_file (ccp arg, ccp basedir, uint depth)
{
	// Check for companion NDX if given .bin
	if (is_ext (arg, ".bin"))
	{
		char sibling_ndx[PATH_MAX];
		get_atlus_sibling_path (sibling_ndx, sizeof (sibling_ndx), arg, ".ndx");
		if (!access (sibling_ndx, F_OK))
			return extract_atlusndx_file (sibling_ndx, basedir, depth);
		return ERR_NOTHING_TO_DO;
	}

	u8 *ndx_raw = 0;
	size_t ndx_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &ndx_raw, &ndx_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (ndx_size < 16 || !IsAtlusNdx (ndx_raw, (uint)ndx_size, (u64)ndx_size))
	{
		FREE (ndx_raw);
		return ERR_NOTHING_TO_DO;
	}

	char sibling_bin[PATH_MAX];
	get_atlus_sibling_path (sibling_bin, sizeof (sibling_bin), arg, ".bin");
	FILE *fbin = fopen (sibling_bin, "rb");
	if (!fbin)
	{
		FREE (ndx_raw);
		return ERR_NOTHING_TO_DO;
	}

	fseeko (fbin, 0, SEEK_END);
	const u64 bin_size = (u64)ftello (fbin);
	rewind (fbin);

	char sibling_idx[PATH_MAX];
	get_atlus_sibling_path (sibling_idx, sizeof (sibling_idx), arg, ".idx");
	u8 *idx_raw = 0;
	size_t idx_size = 0;
	LoadFileAlloc (sibling_idx, 0, 0, &idx_raw, &idx_size, 0, 0, 0, false);

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ATLUS-NDX:%s -> %s/\n",
		         verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	atlus_extract_ctx_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	ctx.ndx = ndx_raw;
	ctx.ndx_size = ndx_size;
	ctx.idx = idx_raw;
	ctx.idx_size = idx_size;
	ctx.fbin = fbin;
	ctx.bin_size = bin_size;
	ctx.dest = dest;
	ctx.source_file = arg;

	extract_atlus_node (&ctx, 0, "");

	if (idx_raw)
		FREE (idx_raw);
	fclose (fbin);
	FREE (ndx_raw);

	err = ctx.err;
	if (!err && !testmode && ctx.count > 0)
		extract_tree_complete (dest, depth + 1);

	return err ? err : (ctx.count > 0 ? ERR_OK : ERR_NOTHING_TO_DO);
}
