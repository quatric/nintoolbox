// Decodes every FTEX subfile in a Wii U BFRES to a sibling "<name>.png",
// so material texture-ref names (bound in lib-bfres.c's ParseBFRES, as
// plain names -- not pixel data) resolve through this fork's existing
// DAE-texture-search index (SetDAETextureSearchRoot()/dae_texture_path()
// in lib-model-dae.c), the same mechanism BRRES's TEX0->PNG textures
// already rely on. FTEX's header is the identical GX2Surface layout GTX
// uses (see lib-gtx.c) plus a few FTEX-specific trailer fields; verified
// against mk8.tockdom.com's FTEX doc page byte-for-byte. Switch BFRES
// (little-endian, keeps textures in a separate embedded BNTX) is rejected
// by the same magic/BOM/version check ParseBFRES itself uses.
//
// Always returns ERR_NOTHING_TO_DO, even on success: unlike every other
// handler in the sequential dispatch chain it's also wired into, this one
// doesn't "own" .bfres files -- export_model_if_possible() still needs to
// run on the very same file right after this, so returning ERR_OK there
// would wrongly short-circuit that chain and silently drop every model.
static enumError extract_bfres_textures (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bfres") && !is_ext (arg, ".fres"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x60 || memcmp (raw, "FRES", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s", arg);
	char *slash = strrchr (dest, '/');
	if (slash)
		*slash = 0;
	else
		snprintf (dest, sizeof (dest), ".");

	// Switch BFRES (FRES with BOM 0xFEFF at offset 0x0C): decode embedded BNTX texture containers
	if (raw_size >= 0x10 && (u16)(raw[0x0C] | (raw[0x0D] << 8)) == 0xFEFF)
	{
		const u8 *p = raw;
		while (p && (size_t)(p - raw) < raw_size)
		{
			const u8 *bntx_ptr = memmem (p, raw_size - (size_t)(p - raw), "BNTX", 4);
			if (!bntx_ptr)
				break;
			const size_t bntx_offset = bntx_ptr - raw;
			const uint bntx_avail = (uint)(raw_size - bntx_offset);

			bntx_t bntx;
			if (!ScanBNTX (&bntx, bntx_ptr, bntx_avail))
			{
				for (uint i = 0; i < bntx.n_textures; i++)
				{
					ccp tname = bntx.textures[i].name;
					if (!tname || !*tname)
						continue;
					char path[PATH_MAX];
					snprintf (path, sizeof (path), "%s%s%s%s.png", dest, *dest ? "/" : "",
						basedir ? basedir : "", tname);

					if (!opt_overwrite && !access (path, F_OK))
						continue;

					u8 *rgba = 0;
					uint w = 0, h = 0;
					if (!DecodeBNTX_RGBA (&rgba, &w, &h, &bntx, i) && rgba)
					{
						if (verbose >= 0 || testmode)
							fprintf (stdlog, "%s%sEXTRACT BNTX:%s[%s] -> PNG:%s\n",
								verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, tname,
								path);
						if (!testmode)
							SaveDecodedRGBAToPNG (rgba, w, h, &le_func, path, 0, false);
						else
							FREE (rgba);
					}
				}
				ResetBNTX (&bntx);
			}
			p = bntx_ptr + 4;
		}
		(void)depth;
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (bft_rb16 (raw + 8) != 0xFEFF || raw[4] != 3)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u8 *d = raw;
	const uint16_t n_ftex = bft_rb16 (d + 0x52); // file-counts[1]
	if (!n_ftex)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t grp = BFT_REL (d, 0x24); // file-offsets[1]
	if (grp + 8 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint32_t entries = bft_rb32 (d + grp + 4);
	if (!entries || entries > 0x10000 || grp + 8 + (size_t)(entries + 1) * 16 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Unlike beside_source_dest()'s "<name>.d/" convention (right for a
	// container's own members), these PNGs need to land in the SAME
	// directory as the .bfres itself: export_model_if_possible() writes
	// the DAE right beside the source file (arg+".dae", no subdirectory),
	// and dae_texture_path()'s search-index match requires the model and
	// its textures to share a directory below the search root -- a PNG one
	// level down in a "<name>.d/" folder fails that check and the texture
	// silently drops from the DAE.

	for (uint32_t i = 0; i < entries && i < n_ftex; i++)
	{
		const size_t te = grp + 8 + (size_t)(i + 1) * 16;
		if (te + 16 > raw_size)
			break;
		const size_t ft = BFT_REL (d, te + 12);
		if (ft + 0xC0 > raw_size || memcmp (d + ft, "FTEX", 4))
			continue;

		const char *tname = bft_rel_string (d, raw_size, ft + 0xA8);
		if (!tname || !*tname)
			continue;

		const uint32_t dim = bft_rb32 (d + ft + 0x04);
		const uint32_t width = bft_rb32 (d + ft + 0x08);
		const uint32_t height = bft_rb32 (d + ft + 0x0C);
		const uint32_t format = bft_rb32 (d + ft + 0x18);
		const uint32_t img_size = bft_rb32 (d + ft + 0x24);
		const uint32_t tile_mode = bft_rb32 (d + ft + 0x34);
		const uint32_t swizzle = bft_rb32 (d + ft + 0x38);
		const uint32_t pitch = bft_rb32 (d + ft + 0x40);
		const s32 data_off_s = bft_rbs32 (d + ft + 0xB0);
		if (!width || !height || !img_size || !data_off_s)
			continue;
		const size_t data_off = (size_t)(ft + 0xB0) + (size_t)data_off_s;
		if (data_off + img_size > raw_size)
			continue;

		u8 *rgba = 0;
		uint w = 0, h = 0;
		if (DecodeGX2Surface_RGBA (&rgba, &w, &h, dim, width, height, format, tile_mode, pitch,
				swizzle, d + data_off, img_size))
			continue; // unsupported tile mode/format -- not this fork's problem to guess at

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s%s%s%s.png", dest, *dest ? "/" : "",
			basedir ? basedir : "", tname);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT FTEX:%s[%s] -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, tname, path);
		if (!testmode)
			SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, false);
		else
			FREE (rgba);
	}

	// Archive-level resource census: the header's 12 dictionary slots register
	// not just the FMDL (slot 0) / FTEX (slot 1) objects but the animation-class
	// ones too -- FSKA (skeletal), FSHU (shader param), FTXP, FVIS (visibility),
	// FSHA (shape), FSCN (scene). Printed only when that extra catalogue is
	// non-empty, so plain geometry/texture archives stay quiet.
	bfres_archive_t arch;
	if (ParseBFRESArchive (raw, raw_size, &arch))
	{
		bool interesting = false;
		for (uint8_t i = 0; i < arch.n_slots; i++)
			if (memcmp (arch.slots[i].magic, "FMDL", 4) && memcmp (arch.slots[i].magic, "FTEX", 4))
				interesting = true;
		if (interesting)
		{
			fprintf (stdlog, "%s%sRESOURCES:%s[%s]", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, arch.name);
			for (uint8_t i = 0; i < arch.n_slots; i++)
			{
				bool printable = true;
				for (uint8_t j = 0; j < 4; j++)
					if (arch.slots[i].magic[j] < 32 || arch.slots[i].magic[j] > 126)
						printable = false;
				if (printable)
					fprintf (stdlog, " %.*s=%u", 4, arch.slots[i].magic,
						(unsigned)arch.slots[i].count_dict);
			}
			fprintf (stdlog, "\n");
		}
	}

	// Animation entry bodies (FSKA/FSHU/FTXP/FVIS/FSHA/FSCN): one compact
	// per-class summary line per file. Entry layouts per the NintendoWare
	// G3D SDK headers; see ParseBFRESAnims() in lib-bfres.c.
	{
		bfres_anim_entry_t *anims = NULL;
		const int n_anims = ParseBFRESAnims (raw, raw_size, &anims);
		if (n_anims > 0 && anims)
		{
			static const char *const classes[6]
				= { "FSKA", "FSHU", "FTXP", "FVIS", "FSHA", "FSCN" };
			fprintf (stdlog, "%s%sANIM:%s[%s]", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
				arg, arch.name);
			for (int ci = 0; ci < 6; ci++)
			{
				uint n = 0, fmin = 0, fmax = 0, nc = 0, ok = 0;
				for (int i = 0; i < n_anims; i++)
					if (!memcmp (anims[i].cls, classes[ci], 4))
					{
						n++;
						if (anims[i].frames >= 0 && (!fmax || (uint)anims[i].frames < fmin))
							fmin = (uint)anims[i].frames;
						if (anims[i].frames >= 0 && (uint)anims[i].frames > fmax)
							fmax = (uint)anims[i].frames;
						nc += anims[i].n_curve;
						ok += anims[i].n_curve_ok;
					}
				if (n)
					fprintf (stdlog, " %s n=%u frames=%u-%u curves=%u/%u", classes[ci], n, fmin,
						fmax, ok, nc);
			}
			fprintf (stdlog, "\n");
			FREE (anims);
		}
	}

	(void)depth;
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}

// A dedicated pre-pass, walking the same shape as export_models_tree() but
// run BEFORE SetDAETextureSearchRoot() rather than interleaved with it.
// SetDAETextureSearchRoot() indexes every PNG under ROOT in one synchronous
// scan; a texture PNG that extract_bfres_textures() only writes *during*
// export_models_tree()'s own later walk would miss that index entirely
// (built too early to see it) and the model would export untextured. FTEX
// extraction has no such ordering dependency on any other model format, so
// it gets its own earlier, dedicated pass instead of being folded into
// export_models_tree()'s per-file loop like the other decoders there.
static enumError extract_bfres_textures_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
			continue;
		struct stat st;
		if (lstat (path, &st))
			continue;
		if (S_ISDIR (st.st_mode))
			extract_bfres_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_bfres_textures (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}
