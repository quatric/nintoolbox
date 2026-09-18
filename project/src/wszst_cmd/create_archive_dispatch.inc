//
///////////////////////////////////////////////////////////////////////////////
///////////////			command create			///////////////
///////////////////////////////////////////////////////////////////////////////

// ".bin" spans several creators, so the destination decides: an existing
// Nd Cube Wii U "PAC\0" file gets rebuilt as that container (byte-exact via
// create_pac_dir), anything else stays the Hudson MPBIN table format.
static enumError create_bin_dir (ccp source, ccp dest)
{
	if (ndcube_pac_dest (dest))
		return create_pac_dir (source, dest);
	return create_mpbin_dir (source, dest);
}

static bool looks_like_f9res_dir (ccp source)
{
	DIR *dir = opendir (source);
	if (!dir)
		return false;
	struct dirent *ent;
	bool found = false;
	while ((ent = readdir (dir)) != 0)
	{
		if (ent->d_name[0] == '.')
			continue;
		if (strlen (ent->d_name) >= 9 && ent->d_name[4] == '_')
		{
			found = true;
			break;
		}
	}
	closedir (dir);
	return found;
}

static enumError create_effn_dir (ccp source, ccp dest)
{
	return CreateEFFNArchive (source, dest);
}

static enumError create_pctl_dir (ccp source, ccp dest)
{
	return CreatePCTLArchive (source, dest);
}

static enumError create_archive_from_dir (ccp source_dir, ccp dest, int known_lz_type)
{
	ccp ext = strrchr (dest, '.');
	if (!ext)
		ext = "";

	if (!strcasecmp (ext, ".eff") || !strcasecmp (ext, ".effn"))
		return create_effn_dir (source_dir, dest);
	if (!strcasecmp (ext, ".ptcl") || !strcasecmp (ext, ".vfxb") || !strcasecmp (ext, ".eset"))
		return create_pctl_dir (source_dir, dest);

	if (!strcasecmp (ext, ".sdat"))
		return PassthruPack (source_dir, dest);

	// Audio and video formats with extracted preview trees must NOT be repacked into U8 archives
	if (!strcasecmp (ext, ".brsar") || !strcasecmp (ext, ".sdat") || !strcasecmp (ext, ".thp")
		|| !strcasecmp (ext, ".moflex") || !strcasecmp (ext, ".mo") || !strcasecmp (ext, ".brstm")
		|| !strcasecmp (ext, ".bcstm") || !strcasecmp (ext, ".bfstm") || !strcasecmp (ext, ".bns")
		|| !strcasecmp (ext, ".ast") || !strcasecmp (ext, ".dsp"))
	{
		return ERR_NOTHING_TO_DO;
	}

	// .lz is a wrapper around an archive rather than a standalone passthrough
	// format. Let the generic archive path below rebuild its decoded content.
	const bool is_raw_lz_ext = !strcasecmp(ext,".lz") || !strcasecmp(ext,".lz10")
		|| !strcasecmp(ext,".lz11");
	enumError pas_err = is_raw_lz_ext ? ERR_NOTHING_TO_DO : PassthruPack (source_dir, dest);
	if (pas_err != ERR_NOTHING_TO_DO)
		return pas_err;

	const bool sarc_le = (!strcasecmp (ext, ".sarcle") || !strcasecmp (ext, ".le")
		|| (strlen (dest) >= 8 && !strcasecmp (ext - 5, ".sarc.le")));

	const bool is_sarc = !strcasecmp (ext, ".sarc") || !strcasecmp (ext, ".bfma")
		|| (strlen (dest) >= 10 && !strcasecmp (dest + strlen (dest) - 10, ".sarc.fzip"))
		|| sarc_le;
	if (is_sarc)
		return create_sarc_dir (source_dir, dest, !sarc_le);
	if (!strcasecmp (ext, ".narc"))
		return create_narc_dir (source_dir, dest, true);
	if (!strcasecmp (ext, ".darc") || !strcasecmp (ext, ".bcma"))
		return create_darc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pac") || !strcasecmp (ext, ".pcs"))
		return create_pac_dir (source_dir, dest);
	if (!strcasecmp (ext, ".warc")
		|| (strlen (dest) >= 10 && !strcasecmp (dest + strlen (dest) - 10, ".warc.fzip")))
		return create_warc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".bg4"))
		return create_bg4_dir (source_dir, dest);
	if (!strcasecmp (ext, ".sa01"))
		return create_sa01_dir (source_dir, dest);
	if (!strcasecmp (ext, ".ca01"))
		return create_ca01_dir (source_dir, dest);
	if (!strcasecmp (ext, ".cram"))
		return create_cram_dir (source_dir, dest);
	if (!strcasecmp (ext, ".fsys"))
		return create_fsys_dir (source_dir, dest);
	if (!strcasecmp (ext, ".xc") || !strcasecmp (ext, ".xpck"))
		return create_xpck_dir (source_dir, dest);
	if (!strcasecmp (ext, ".ztab") || !strcasecmp (ext, ".tab"))
		return create_ztab_dir (source_dir, dest);
	if (!strcasecmp (ext, ".mdr"))
		return create_mdr_dir (source_dir, dest);
	if (!strcasecmp (ext, ".vff"))
		return create_vff_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pvol"))
		return create_pvol_dir (source_dir, dest);
	if (!strcasecmp (ext, ".stpk") || !strcasecmp (ext, ".srd"))
		return create_stpk_dir (source_dir, dest);
	if (!strcasecmp (ext, ".res") && looks_like_f9res_dir (source_dir))
		return create_f9res_dir (source_dir, dest);
	if (!strcasecmp (ext, ".zlarc"))
		return create_zlarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".jarc"))
		return create_jarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".gpak"))
		return create_gpak_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pak"))
	{
		// .pak is shared by Call of Duty PAK0 sound archives (CRC-named .dsp
		// members) and 2D Boy GPAK files (ordinal file_%04u members); pick
		// the builder from the tree's shape.
		if (looks_like_cod_pak_dir (source_dir))
			return create_cod_pak_dir (source_dir, dest);
		if (looks_like_gpak_dir (source_dir))
			return create_gpak_dir (source_dir, dest);
		return ERROR0 (ERR_INVALID_DATA,
			"No PAK0 or GPAK members found in: %s\n", source_dir);
	}
	if (!strcasecmp (ext, ".vcra"))
		return create_vcra_dir (source_dir, dest);
	if (!strcasecmp (ext, ".apak"))
		return create_apak_dir (source_dir, dest);
	if (!strcasecmp (ext, ".nxarc"))
		return create_nxarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pkz"))
		return create_pkz_dir (source_dir, dest);
	if (!strcasecmp (ext, ".tmpk") || !strcasecmp (ext, ".pack"))
		return create_tmpk_dir (source_dir, dest);
	if (!strcasecmp (ext, ".vibs"))
		return create_vibs_dir (source_dir, dest);
	if (!strcasecmp (ext, ".mtxt"))
		return create_mtxt_dir (source_dir, dest);
	if (!strcasecmp (ext, ".mkgpdx"))
		return create_mkgpdx_dir (source_dir, dest);
	if (!strcasecmp (ext, ".pkg") || !strcasecmp (ext, ".gpkg"))
		return create_gpkg_dir (source_dir, dest);
	if (!strcasecmp (ext, ".sharcfb") && LooksLikeSHARCDir (source_dir))
		return CreateSHARCFBFromDir (source_dir, dest);
	if (!strcasecmp (ext, ".sharc") && LooksLikeSHARCDir (source_dir))
		return CreateSHARCFromDir (source_dir, dest);
	if (!strcasecmp (ext, ".gsh"))
		return create_gsh_dir (source_dir, dest);
	if (!strcasecmp (ext, ".dat") && looks_like_sfzdat_dir (source_dir))
		return create_sfzdat_dir (source_dir, dest);
	if (!strcasecmp (ext, ".zdat"))
		return create_zdat_dir (source_dir, dest);
	if (!strcasecmp (ext, ".rzpk"))
		return create_rzpk_dir (source_dir, dest);
	if (!strcasecmp (ext, ".one"))
		return create_one_dir (source_dir, dest);
	if ((!strcasecmp (ext, ".dat") || !strcasecmp (ext, ".rflres") || !strcasecmp (ext, ".rfl")
		|| !strcasecmp (ext, ".fflres") || !strcasecmp (ext, ".ffl")
		|| !strcasecmp (ext, ".cflres") || !strcasecmp (ext, ".cfl")
		|| !strcasecmp (ext, ".aflres") || !strcasecmp (ext, ".afl")
		|| !strcasecmp (ext, ".nflres") || !strcasecmp (ext, ".nfl") || !strcasecmp (ext, ".nfl_res")) && looks_like_rflres_dir (source_dir))
	{
		const bool be = strcasecmp (ext, ".cfl") && strcasecmp (ext, ".cflres")
			&& strcasecmp (ext, ".afl") && strcasecmp (ext, ".aflres")
			&& strcasecmp (ext, ".nfl") && strcasecmp (ext, ".nflres") && strcasecmp (ext, ".nfl_res");
		return create_rflres_dir (source_dir, dest, be);
	}
	if (!strcasecmp (ext, ".bea") && looks_like_bea_dir (source_dir))
		return create_bea_dir (source_dir, dest);
	if (!strcasecmp (ext, ".mpbin") && looks_like_mpbin_dir (source_dir))
		return create_mpbin_dir (source_dir, dest);
	if (!strcasecmp (ext, ".atb") && looks_like_atb_dir (source_dir))
		return create_atb_dir (source_dir, dest);
	if (!strcasecmp (ext, ".lzbin") && looks_like_lzbin_dir (source_dir))
		return create_lzbin_dir (source_dir, dest);
	if (!strcasecmp (ext, ".gfa"))
		return create_gfa_dir (source_dir, dest);
	if (!strcasecmp (ext, ".nus3audio") || !strcasecmp (ext, ".nus3bank"))
		return create_nus3audio_dir (source_dir, dest);
	if (!strcasecmp (ext, ".rarc"))
		return create_rarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".bcsar") || !strcasecmp (ext, ".csar"))
		return create_sar_dir (source_dir, dest, "CSAR");
	if (!strcasecmp (ext, ".bfsar") || !strcasecmp (ext, ".fsar"))
		return create_sar_dir (source_dir, dest, "FSAR");
	if (!strcasecmp (ext, ".bcwar") || !strcasecmp (ext, ".cwar"))
		return create_sar_dir (source_dir, dest, "CWAR");
	if (!strcasecmp (ext, ".bfwar") || !strcasecmp (ext, ".fwar"))
		return create_sar_dir (source_dir, dest, "FWAR");
	if (!strcasecmp (ext, ".bcgrp") || !strcasecmp (ext, ".cgrp"))
		return create_sar_dir (source_dir, dest, "CGRP");
	if (!strcasecmp (ext, ".bfgrp") || !strcasecmp (ext, ".fgrp"))
		return create_sar_dir (source_dir, dest, "FGRP");
	if (!strcasecmp (ext, ".car") || !strcasecmp (ext, ".res") || !strcasecmp (ext, ".trk")
		|| !strcasecmp (ext, ".lvl") || !strcasecmp (ext, ".rst"))
		return create_rst_dir (source_dir, dest, !OptionUsed[OPT_NO_COMPRESS]);
	if (!strcasecmp (ext, ".ctpk"))
		return create_ctpk_dir (source_dir, dest);
	if (!strcasecmp (ext, ".ccf"))
		return create_ccf_dir (source_dir, dest);
	if (!strcasecmp (ext, ".nccarc"))
		return create_nccarc_dir (source_dir, dest);
	if (!strcasecmp (ext, ".at7") || !strcasecmp (ext, ".at7p") || !strcasecmp (ext, ".at7x"))
		return create_at7_dir (source_dir, dest);
	if (!strcasecmp (ext, ".idx"))
		return create_hwl_dir (source_dir, dest);
	if (!strcasecmp (ext, ".bin"))
		return create_bin_dir (source_dir, dest);
	if (!strcasecmp (ext, ".big"))
		return create_bigf_dir (source_dir, dest);
	if (!strcasecmp (ext, ".arc") && looks_like_arcv_dir (source_dir))
		return create_arcv_dir (source_dir, dest);

	// Default: U8/Yaz0/BRRES/ARC archive via CreateSZS. If the source is a
	// raw Nintendo LZ stream, rebuild its inner archive and restore the same
	// LZ10/LZ11 wrapper instead of replacing it with bare archive bytes.
	bool lz_wrapped = known_lz_type == 0x10 || known_lz_type == 0x11;
	bool lz11 = known_lz_type == 0x11;
	ccp dest_ext = strrchr (dest, '.');
	const bool ash_wrapped = dest_ext && (!strcasecmp(dest_ext,".ash") || !strcasecmp(dest_ext,".ash0"));
	if (!lz_wrapped && dest_ext && (!strcasecmp (dest_ext, ".lz") || !strcasecmp (dest_ext, ".lz10")
			|| !strcasecmp (dest_ext, ".lz11")))
	{
		// Default before opening the file. CREATE can truncate its destination
		// before reaching this helper, and Wii's unsuffixed .lz convention is LZ11.
		lz_wrapped = true;
		lz11 = strcasecmp(dest_ext,".lz10") != 0;
		u8 type = 0;
		FILE *in = fopen (dest, "rb");
		if (in && fread (&type, 1, 1, in) == 1 && (type == 0x10 || type == 0x11))
		{
			lz11 = type == 0x11;
		}
		if (in)
			fclose (in);
		// A direct CREATE may have already opened an existing destination for
		// output.  The explicit suffix still conveys the two unambiguous cases;
		// plain .lz is conventionally LZ11 for these Wii resources.
	}
	SetupParam_t sp;
	InitializeSetupParam (&sp);
	ScanSetupParam (&sp, true, source_dir, SZS_SETUP_FILE, 0, true);

	szs_file_t szs;
	InitializeSZS (&szs);
	enumError err
		= CreateSZS (&szs, dest, source_dir, 0, &sp, 0, verbose > 0 ? UINT_MAX : 0, false);
	if (err <= ERR_WARNING && !szs.unchanged && err != ERR_NOT_EXISTS)
	{
		File_t F;
		CreateFILE (&F, true, dest, testmode, false, true, false, false);
		if (F.f)
		{
			SetFileAttrib (&F.fatt, &szs.fatt, 0);
			const u8 *data = szs.cdata ? szs.cdata : szs.data;
			const size_t size = szs.cdata ? szs.csize : szs.size;
			u8 *packed = 0;
			uint packed_size = 0;
			if (ash_wrapped)
				err = EncodeASH0 (&packed, &packed_size, data, size);
			else if (lz_wrapped)
				err = EncodeLZ10LZ11 (&packed, &packed_size, data, size, lz11);
			if (err <= ERR_WARNING)
			{
				const u8 *output = packed ? packed : data;
				const size_t output_size = packed ? packed_size : size;
				const size_t wstat = fwrite (output, 1, output_size, F.f);
				if (wstat != output_size)
					err = FILEERROR1 (
						&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", output_size, dest);
			}
			FREE (packed);
		}
		ResetFile (&F, opt_preserve);
		LinkCacheSZS (&szs, dest);
	}
	ResetSZS (&szs);
	ResetSetupParam (&sp);
	return err;
}

static const char *cand_archive_exts[] = { ".wbfs", ".iso", ".ciso", ".wdf", ".wia", ".gcz", ".gcm",
	".gca", ".nds", ".srl", ".dsi", ".3ds", ".cia", ".cxi", ".ncch", ".app", ".wad", ".wud", ".wux",
	".rpx", ".rpl", ".nsp", ".xci", ".nca", ".szs", ".carc", ".arc", ".brres", ".sarc", ".narc",
	".darc", ".pac", ".pcs", ".gfa", ".rarc", ".warc", ".bcsar", ".bfsar", ".bcwar", ".bfwar",
	".bcgrp", ".bfgrp", ".rst", ".car", ".res", ".trk", ".lvl", ".wu8", ".wbz", ".wlz", ".bntx",
	".bcres", ".bfres", ".bch", ".big", ".xc", ".xpck", ".ztab", ".tab", ".mdr", ".vff", ".pvol",
	".srd", ".stpk", ".zlarc", ".zdat", ".rzpk", ".one", ".ptcl", ".eff", ".effn", ".vfxb", ".eset", 0 };

// A number of retail Wii titles keep otherwise ordinary BRRES/U8 resources
// in a raw Nintendo LZ10/LZ11 stream, conventionally named "*.lz".  The
// extracted directory is named after the decompressed member (for example
// `foo.brres.d` beside `foo.brres.lz`), so this list is deliberately kept
// separate from archive suffixes: the compression wrapper must be restored
// after rebuilding, not mistaken for the archive's own format.
static const char *cand_lz_exts[] = { ".lz", ".lz10", ".lz11", 0 };
static const char *cand_ash_exts[] = { ".ash", ".ash0", 0 };

// stage_dir_of() names a media passthrough's staging directory after the
// source's stem (extension stripped: "attract.thp" -> "attract.d"), unlike
// every archive format above where the directory keeps the full name
// ("foo.brres.d"). None of the candidate lists above ever try these
// extensions, so target_file/target_exists never resolved for a THP/
// Mobiclip preview directory -- the ".thp"/".moflex"/".mo"/".mods" re-encode
// check further down (which needs target_file's extension) was unreachable,
// silently dropping an edited preview on CREATE.
static const char *cand_media_exts[] = { ".thp", ".moflex", ".mo", ".mods", 0 };

static bool is_archive_dir_name (ccp dir, size_t len)
{
	struct stat st;
	// Check for FST / NDS / 3DS / WiiU / Switch / WAD structural markers inside dir
	char check_path[PATH_MAX];
	snprintf (check_path, sizeof (check_path), "%s/setup.txt", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/DATA/sys/main.dol", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/sys/main.dol", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/arm9.bin", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/romfs", dir);
	if (stat (check_path, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	snprintf (check_path, sizeof (check_path), "%s/exefs", dir);
	if (stat (check_path, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	snprintf (check_path, sizeof (check_path), "%s/code", dir);
	if (stat (check_path, &st) == 0 && S_ISDIR (st.st_mode))
		return true;
	// hacbrewpack's Switch NSP repack branch requires control/control.nacp;
	// that's what the Control NCA's second extraction pass under
	// passthru_archive() actually produces -- a plain "content" dir (the
	// previous check here) is never created by any extraction path.
	snprintf (check_path, sizeof (check_path), "%s/control/control.nacp", dir);
	if (stat (check_path, &st) == 0 && S_ISREG (st.st_mode))
		return true;
	snprintf (check_path, sizeof (check_path), "%s/00000000.app", dir);
	if (stat (check_path, &st) == 0)
		return true;
	// wit's XEXTRACT (x-wad.c) layout: cert.bin/tik.bin/tmd.bin plus each
	// decrypted content as "%08x.app" -- content IDs rarely start at 0, so
	// the 00000000.app check above alone misses most real WADs.
	snprintf (check_path, sizeof (check_path), "%s/tmd.bin", dir);
	if (stat (check_path, &st) == 0)
		return true;
	snprintf (check_path, sizeof (check_path), "%s/cert.bin", dir);
	if (stat (check_path, &st) == 0)
		return true;

	if (len < 4 || strcasecmp (dir + len - 2, ".d") != 0)
		return false;
	ccp last_slash = strrchr (dir, '/');
	ccp fname = last_slash ? last_slash + 1 : dir;
	size_t flen = strlen (fname);
	if (flen < 4 || strcasecmp (fname + flen - 2, ".d") != 0)
		return false;

	// Check if wszst-setup.txt exists inside the .d directory
	char setup_path[PATH_MAX];
	snprintf (setup_path, sizeof (setup_path), "%s/%s", dir, SZS_SETUP_FILE);
	if (stat (setup_path, &st) == 0 && S_ISREG (st.st_mode))
		return true;

	char base[PATH_MAX];
	snprintf (base, sizeof (base), "%.*s", (int)(flen - 2), fname);
	ccp ext = strrchr (base, '.');
	if (ext)
	{
		for (int c = 0; cand_archive_exts[c]; c++)
		{
			if (!strcasecmp (ext, cand_archive_exts[c]))
				return true;
		}
	}

	// Check if there is a sibling container file next to this .d directory
	char parent[PATH_MAX];
	if (last_slash)
		snprintf (parent, sizeof (parent), "%.*s", (int)(last_slash - dir), dir);
	else
		snprintf (parent, sizeof (parent), ".");

	for (int c = 0; cand_archive_exts[c]; c++)
	{
		char cand[PATH_MAX];
		snprintf (cand, sizeof (cand), "%s/%s%s", parent, base, cand_archive_exts[c]);
		if (stat (cand, &st) == 0 && S_ISREG (st.st_mode))
			return true;
	}

	return false;
}

static void cleanup_extracted_artifacts (ccp root, bool is_root, uint depth)
{
	// Unlike repack_tree_bottom_up() (its sibling recursive tree-walker just
	// above), this one had no depth cap at all: a directory tree nested more
	// than a few thousand levels deep -- trivially producible, malicious or
	// not -- recursed until the stack blew up. Cap it the same way.
	if (depth > 32)
		return;
	DIR *dir = opendir (root);
	if (!dir)
		return;
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
		const size_t nlen = strlen (de->d_name);

		if (S_ISDIR (st.st_mode))
		{
			if (nlen > 2 && !strcasecmp (de->d_name + nlen - 2, ".d"))
			{
				remove_dir_recursive (path);
			}
			else
			{
				cleanup_extracted_artifacts (path, false, depth + 1);
			}
		}
		else if (S_ISREG (st.st_mode))
		{
			if (!strcmp (de->d_name, "wszst-setup.txt"))
			{
				// The root directory's own setup file is still needed by the
				// caller (cmd_create's ScanSetupParam, right after this call)
				// to recover the original archive/compression format -- e.g.
				// "file-format = U8" for an uncompressed source .arc. Deleting
				// it here defaulted every repack of that root to YAZ0-compressed
				// output regardless of what the source actually was. Nested
				// .d dirs' own setup files were already consumed by
				// repack_tree_bottom_up(), so those are still safe to remove.
				if (!is_root)
					unlink (path);
				continue;
			}
			// Companion model files (.glb, .dae)
			if (nlen > 4
				&& (!strcasecmp (de->d_name + nlen - 4, ".glb")
					|| !strcasecmp (de->d_name + nlen - 4, ".dae")))
			{
				static const char *m_exts[] = { ".brres", ".bmd", ".bch", ".bcres", ".bfres",
					".mdl0", ".hsf", ".msh", ".mod", ".glg", ".rlg", 0 };
				const size_t stem_len = strlen (path) - 4;
				bool did_unlink = false;
				for (int k = 0; m_exts[k]; k++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%.*s%s", (int)stem_len, path, m_exts[k]);
					struct stat st_m;
					if (stat (cand, &st_m) == 0 && S_ISREG (st_m.st_mode))
					{
						unlink (path);
						did_unlink = true;
						break;
					}
				}
				// KCL exports append rather than replace the extension
				// (e.g. "foo.kcl" -> "foo.kcl.glb", like "foo.kcl.obj"), so the
				// stem itself -- not stem+".kcl" -- is the candidate source.
				if (!did_unlink && stem_len > 4
					&& !strcasecmp (path + stem_len - 4, ".kcl"))
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%.*s", (int)stem_len, path);
					struct stat st_m;
					if (stat (cand, &st_m) == 0 && S_ISREG (st_m.st_mode))
						unlink (path);
				}
			}
			// Companion XML files
			if (nlen > 9
				&& (!strcasecmp (de->d_name + nlen - 9, ".ncer.xml")
					|| !strcasecmp (de->d_name + nlen - 9, ".nanr.xml")))
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - 4), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			// BFLYT/BCLYT/BRLYT/BRLAN layouts and animations decode to *.xml.
			if (nlen > 10 && (!strcasecmp (de->d_name + nlen - 10, ".bflyt.xml")
				|| !strcasecmp (de->d_name + nlen - 10, ".bflan.xml")
				|| !strcasecmp (de->d_name + nlen - 10, ".bclyt.xml")
				|| !strcasecmp (de->d_name + nlen - 10, ".bclan.xml")
				|| !strcasecmp (de->d_name + nlen - 10, ".brlyt.xml")
				|| !strcasecmp (de->d_name + nlen - 10, ".brlan.xml")))
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - 4), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			// Companion YAML files
			if ((nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".byml.yml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".byml.yaml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".byaml.yml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".byaml.yaml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".sbyml.yml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".sbyml.yaml")))
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - (is_ext (path, ".yml") ? 4 : 5)), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			if ((nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".aamp.yml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".aamp.yaml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".baamp.yml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".baamp.yaml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".bparam.yml"))
				|| (nlen > 12 && !strcasecmp (de->d_name + nlen - 12, ".bparam.yaml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".bgenv.yml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".bgenv.yaml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".bglpbd.yml"))
				|| (nlen > 12 && !strcasecmp (de->d_name + nlen - 12, ".bglpbd.yaml")))
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - (is_ext (path, ".yml") ? 4 : 5)), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			// Companion TXT files
			if (nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".txt")
				&& strcmp (de->d_name, "setup.txt") != 0)
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s", (int)(strlen (path) - 4), path);
				struct stat st_c;
				if (stat (cand, &st_c) == 0 && S_ISREG (st_c.st_mode))
					unlink (path);
			}
			// Companion PNG files that have corresponding native image files
			if (nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".png"))
			{
				char stem[PATH_MAX];
				snprintf (stem, sizeof (stem), "%.*s", (int)(strlen (path) - 4), path);
				ccp stem_ext = strrchr (stem, '.');
				bool has_target = false;
				if (stem_ext
					&& (!strcasecmp (stem_ext, ".tpl") || !strcasecmp (stem_ext, ".tex0")
						|| !strcasecmp (stem_ext, ".tex") || !strcasecmp (stem_ext, ".tm0") || !strcasecmp (stem_ext, ".bti")
						|| !strcasecmp (stem_ext, ".bflim") || !strcasecmp (stem_ext, ".bclim")
						|| !strcasecmp (stem_ext, ".ncgr") || !strcasecmp (stem_ext, ".nclr")
						|| !strcasecmp (stem_ext, ".bntx") || !strcasecmp (stem_ext, ".dsb")
						|| !strcasecmp (stem_ext, ".plt0") || !strcasecmp (stem_ext, ".breft")))
				{
					struct stat st_t;
					if (stat (stem, &st_t) == 0 && S_ISREG (st_t.st_mode))
						has_target = true;
				}
				if (!has_target)
				{
					static const char *cand_img_exts[]
						= { ".tpl", ".tex0", ".tm0", ".bti", ".bflim", ".bclim", ".ncgr", ".nclr", ".bntx",
							  ".dsb", ".plt0", ".breft", ".tex", ".art", ".img", 0 };
					for (int k = 0; cand_img_exts[k]; k++)
					{
						char cand[PATH_MAX];
						snprintf (cand, sizeof (cand), "%s%s", stem, cand_img_exts[k]);
						struct stat st_t;
						if (stat (cand, &st_t) == 0 && S_ISREG (st_t.st_mode))
						{
							has_target = true;
							break;
						}
					}
				}
				if (has_target)
					unlink (path);
			}
		}
	}
	closedir (dir);
}

// A Wii disc/ROM staging tree (wit/ndstool/ctrtool/hactool output) is packed
// by its companion tool, never as a generic archive.  If CREATE is pointed
// one level too high, the bottom-up walk below would otherwise "repack" the
// staging dir into a brand-new U8 file and delete the tree, leaving the
// companion with no valid source.  Refuse to invent a container for it.
static bool is_passthrough_staging_dir (ccp dir)
{
	struct stat st;
	char check[PATH_MAX];
	snprintf (check, sizeof (check), "%s/DATA/sys/main.dol", dir);
	if (!stat (check, &st) && S_ISREG (st.st_mode))
		return true;
	snprintf (check, sizeof (check), "%s/sys/main.dol", dir);
	if (!stat (check, &st) && S_ISREG (st.st_mode))
		return true;
	snprintf (check, sizeof (check), "%s/arm9.bin", dir);
	if (!stat (check, &st) && S_ISREG (st.st_mode))
		return true;
	snprintf (check, sizeof (check), "%s/romfs", dir);
	if (!stat (check, &st) && S_ISDIR (st.st_mode))
		return true;
	snprintf (check, sizeof (check), "%s/exefs", dir);
	if (!stat (check, &st) && S_ISDIR (st.st_mode))
		return true;
	snprintf (check, sizeof (check), "%s/control/control.nacp", dir);
	if (!stat (check, &st) && S_ISREG (st.st_mode))
		return true;
	snprintf (check, sizeof (check), "%s/tmd.bin", dir);
	if (!stat (check, &st) && S_ISREG (st.st_mode))
		return true;
	snprintf (check, sizeof (check), "%s/cert.bin", dir);
	if (!stat (check, &st) && S_ISREG (st.st_mode))
		return true;
	snprintf (check, sizeof (check), "%s/setup.txt", dir);
	if (!stat (check, &st) && S_ISREG (st.st_mode))
		return true;
	return false;
}

// Unwrapped size of a Wii banner member (meta/icon.bin etc.): the IMD5/LZ77
// payload size when wrapped, the file size when stored plain.  Missing files
// fall back to the recorded size so one absent member cannot break the
// whole banner rebuild.
static uint wii_banner_member_size (ccp path, uint fallback)
{
	u8 *data = 0;
	size_t size = 0;
	if (LoadFileAlloc (path, 0, 0, &data, &size, UINT_MAX, 2, 0, false) || !data)
		return fallback;
	uint result = fallback;
	u8 *unwrapped = 0;
	uint unwrapped_size = 0;
	if (!UnwrapWiiBannerFile (&unwrapped, &unwrapped_size, 0, data, (uint)size))
		result = unwrapped_size;
	else if (size <= UINT_MAX)
		result = (uint)size;
	FREE (unwrapped);
	FREE (data);
	return result;
}

// Wii channel banners (opening.bnr: IMET over U8 over IMD5/LZ77 members)
// decode to a plain U8 tree but must be re-wrapped on repack: a bare U8
// write silently drops the channel titles and the integrity hashes, and
// balloons members that were LZ-compressed.  Rebuilds the inner U8 to a
// temp file, then re-wraps it with the existing target's own wrapper.
// Returns ERR_NOTHING_TO_DO if TARGET_FILE is not a banner wrapper, so the
// caller falls through to the generic archive path.
static enumError repack_wii_banner_dir (ccp source_d, ccp target_file)
{
	u8 *orig = 0;
	size_t orig_size = 0;
	if (LoadFileAlloc (target_file, 0, 0, &orig, &orig_size, UINT_MAX, 2, 0, false) || !orig
		|| orig_size > UINT_MAX)
	{
		FREE (orig);
		return ERR_NOTHING_TO_DO;
	}
	const uint osize = (uint)orig_size;
	const bool want_imet = IsIMET (orig, osize);
	const bool want_imd5 = !want_imet && IsIMD5 (orig, osize);
	const bool want_lz = !want_imet && !want_imd5 && osize > 8 && !memcmp (orig, "LZ77", 4);
	if (!want_imet && !want_imd5 && !want_lz)
	{
		FREE (orig);
		return ERR_NOTHING_TO_DO;
	}

	if (testmode)
	{
		if (verbose >= 0)
			fprintf (stdlog, "WOULD REPACK %s %s/ -> %s\n",
				want_imet ? "IMET" : want_imd5 ? "IMD5" : "LZ77", source_d, target_file);
		FREE (orig);
		return ERR_OK;
	}

	char tmp[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.tmp-u8", target_file);
	enumError err = create_archive_from_dir (source_d, tmp, 0);
	if (err > ERR_WARNING)
	{
		FREE (orig);
		return err;
	}
	u8 *u8data = 0;
	size_t u8size = 0;
	err = LoadFileAlloc (tmp, 0, 0, &u8data, &u8size, UINT_MAX, 2, 0, false);
	remove (tmp);
	if (err || !u8data || u8size > UINT_MAX)
	{
		FREE (u8data);
		FREE (orig);
		return err ? err : ERR_CANT_CREATE;
	}

	u8 *out = 0;
	uint out_size = 0;
	if (want_imet)
	{
		char member[PATH_MAX];
		imet_t tmpl;
		uint icon_sz = 0, banner_sz = 0, sound_sz = 0;
		if (!ScanIMET (&tmpl, orig, osize))
		{
			icon_sz = tmpl.icon_size;
			banner_sz = tmpl.banner_size;
			sound_sz = tmpl.sound_size;
			ResetIMET (&tmpl);
		}
		snprintf (member, sizeof (member), "%s/meta/icon.bin", source_d);
		icon_sz = wii_banner_member_size (member, icon_sz);
		snprintf (member, sizeof (member), "%s/meta/banner.bin", source_d);
		banner_sz = wii_banner_member_size (member, banner_sz);
		snprintf (member, sizeof (member), "%s/meta/sound.bin", source_d);
		sound_sz = wii_banner_member_size (member, sound_sz);
		err = CreateIMET (&out, &out_size, u8data, (uint)u8size,
			orig, osize, icon_sz, banner_sz, sound_sz);
	}
	else
	{
		const u8 *body = orig;
		uint body_size = osize;
		if (want_imd5)
		{
			body = orig + IMD5_SIZE;
			body_size = osize - IMD5_SIZE;
		}
		const bool compress = body_size > 8 && !memcmp (body, "LZ77", 4);
		bool lz11 = false;
		if (compress)
		{
			if (body[4] != 0x10 && body[4] != 0x11)
			{
				FREE (u8data);
				FREE (orig);
				return ERR_INVALID_DATA;
			}
			lz11 = body[4] == 0x11;
		}
		err = WrapWiiBannerFile (&out, &out_size, u8data, (uint)u8size, compress, lz11);
	}
	FREE (u8data);
	FREE (orig);
	if (err > ERR_WARNING)
		return err;
	err = SaveFILE (target_file, 0, true, out, out_size, 0);
	FREE (out);
	if (err > ERR_WARNING)
		return err;
	if (verbose >= 0)
		fprintf (stdlog, "REPACK %s %s/ -> %s\n",
			want_imet ? "IMET" : want_imd5 ? "IMD5" : "LZ77", source_d, target_file);
	remove_dir_recursive (source_d);
	return ERR_OK;
}

static enumError repack_tree_bottom_up (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;

	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;

	enumError max_err = ERR_OK;
	struct dirent *de;

	// 1. Recurse into subdirectories first (depth-first / bottom-up)
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
		{
			if (max_err < ERR_FILE_TOO_BIG)
				max_err = ERR_FILE_TOO_BIG;
			continue;
		}
		struct stat st;
		if (lstat (path, &st))
			continue;
		if (S_ISDIR (st.st_mode))
		{
			// Switch hactool staging deliberately exposes the selected Program
			// NCA twice: once as the canonical top-level romfs/exefs tree used
			// by hacbrewpack, and once under its hexadecimal *.nca.d mirror.
			// Never recurse into that mirror when the canonical tree exists;
			// doing so independently rebuilds every nested SZS before the real
			// NSP pack even begins.
			const size_t dlen = strlen(de->d_name);
			if (dlen > 6 && !strcasecmp(de->d_name+dlen-6,".nca.d"))
			{
				char canonical_romfs[PATH_MAX];
				snprintf(canonical_romfs,sizeof(canonical_romfs),"%s/romfs",root);
				if (IsDirectory(canonical_romfs,false))
					continue;
			}
			enumError err = repack_tree_bottom_up (path, depth + 1);
			if (max_err < err)
				max_err = err;
		}
	}
	rewinddir (dir);

	// 2. Process modified files & .d directories in this directory
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
		{
			if (max_err < ERR_FILE_TOO_BIG)
				max_err = ERR_FILE_TOO_BIG;
			continue;
		}
		struct stat st;
		if (lstat (path, &st))
			continue;
		const size_t nlen = strlen (de->d_name);

		// Check for .png files with corresponding native image files
		if (S_ISREG (st.st_mode) && nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".png"))
		{
			char target_img[PATH_MAX] = { 0 };
			struct stat st_target;
			bool found_target = false;

			// 1. Check direct prefix, e.g. "foo.tpl.png" -> "foo.tpl", "bar.tex0.png" -> "bar.tex0"
			char stem[PATH_MAX];
			snprintf (stem, sizeof (stem), "%.*s", (int)(strlen (path) - 4), path);
			ccp stem_ext = strrchr (stem, '.');
			if (stem_ext
				&& (!strcasecmp (stem_ext, ".tpl") || !strcasecmp (stem_ext, ".tex0")
					|| !strcasecmp (stem_ext, ".tex") || !strcasecmp (stem_ext, ".tm0") || !strcasecmp (stem_ext, ".bti")
					|| !strcasecmp (stem_ext, ".bflim") || !strcasecmp (stem_ext, ".bclim")
					|| !strcasecmp (stem_ext, ".ncgr") || !strcasecmp (stem_ext, ".nclr")
					|| !strcasecmp (stem_ext, ".bntx") || !strcasecmp (stem_ext, ".dsb")
					|| !strcasecmp (stem_ext, ".plt0") || !strcasecmp (stem_ext, ".breft")))
			{
				if (stat (stem, &st_target) == 0 && S_ISREG (st_target.st_mode))
				{
					snprintf (target_img, sizeof (target_img), "%s", stem);
					found_target = true;
				}
			}

			// 2. Check candidate extensions, e.g. "foo.png" -> "foo.tpl", "foo.tex0", "foo.bti",
			// etc.
			if (!found_target)
			{
				static const char *cand_img_exts[] = { ".tpl", ".tex0", ".tex", ".tm0", ".bti", ".bflim", ".bclim",
					".ncgr", ".nclr", ".bntx", ".dsb", ".plt0", ".breft", ".art", ".img", 0 };
				for (int k = 0; cand_img_exts[k]; k++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%s%s", stem, cand_img_exts[k]);
					if (stat (cand, &st_target) == 0 && S_ISREG (st_target.st_mode))
					{
						snprintf (target_img, sizeof (target_img), "%s", cand);
						found_target = true;
						break;
					}
				}
			}

			if (found_target)
			{
				time_t ref_img_mtime = st_target.st_mtime;
				struct stat st_s;
				char s_path[PATH_MAX];
				snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
				if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_img_mtime)
					ref_img_mtime = st_s.st_mtime;

				// Only re-encode if the PNG was actually modified by the user
				// Extraction preserves the native file timestamp on its PNG preview.
				// A two-second grace period used to suppress genuine edits made right
				// after extraction, so compare directly: unchanged previews retain
				// the exact source timestamp, while any later save is actionable.
				if (st.st_mtime > ref_img_mtime)
				{
					enumError err = encode_image_from_png (path, target_img);
					if (err <= ERR_WARNING)
					{
						// Keep the edited preview's timestamp on the native result. The
						// parent archive uses that timestamp to decide whether its .d
						// tree changed; SaveTEX() otherwise stamps the result with
						// "now", which legacy-tree normalization can mistake for a
						// decoder by-product and reset before the parent sees it.
						struct utimbuf edited_time = { st.st_atime, st.st_mtime };
						utime (target_img, &edited_time);
						if (verbose >= 0)
							fprintf (stdlog, "REPACK IMAGE %s -> %s\n", path, target_img);
					}
					else if (max_err < err)
						max_err = err;
				}
				else if (verbose >= 0)
					// This PNG is being discarded without ever reaching the native
					// texture -- if it was actually a genuine user edit whose mtime
					// didn't end up newer than the native file/setup.txt (e.g. a
					// re-extract ran after the edit and refreshed those timestamps),
					// the edit is now silently lost with no other trace. Log it so
					// that scenario is at least visible instead of completely silent.
					fprintf (stdlog, "DISCARD PNG preview (not newer than %s): %s\n",
						target_img, path);
				unlink (path);
			}
		}

		// Check for .glb or .dae files with sibling models
		const bool is_dae_file = nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".dae");
		const bool is_glb_file = nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".glb");
		if (S_ISREG (st.st_mode) && (is_dae_file || is_glb_file))
		{
			bool handled = false;
			bool found_sibling = false; // a recognised parent model exists, even if left untouched
										// (unmodified preview)
			static const char *model_exts[] = { ".brres", ".bmd", ".bch", ".bcres", ".bfres",
				".mdl0", ".hsf", ".msh", ".mod", ".glg", ".rlg", ".csb", 0 };

			// wmdlt DECODE names a multi-model archive's per-model exports
			// "<archive-stem>.<modelname>.glb" (see wmdlt.c: "%s.%s.glb"). A plain
			// "<glb-stem><native-ext>" lookup then never matches the single shared
			// native sibling file (e.g. "foo.brres"), so every model past the first
			// looked unowned and fell through to the "create a new Switch BFRES"
			// fallback below -- silently turning BRRES models into bogus BFRES files.
			// Try the direct stem first (unchanged single-model behaviour), and only
			// if that finds nothing, retry with the ".<modelname>" suffix stripped.
			char glb_stem[PATH_MAX];
			snprintf (glb_stem, sizeof (glb_stem), "%.*s", (int)(strlen (path) - 4), path);
			const char *model_bases[3] = { glb_stem, 0, 0 };
			char glb_multi_stem[PATH_MAX];
			ccp glb_last_dot = strrchr (glb_stem, '.');
			if (glb_last_dot && glb_last_dot != glb_stem)
			{
				snprintf (glb_multi_stem, sizeof (glb_multi_stem), "%.*s",
					(int)(glb_last_dot - glb_stem), glb_stem);
				model_bases[1] = glb_multi_stem;
			}

			for (int b = 0; model_bases[b] && !handled && !found_sibling; b++)
			for (int k = 0; model_exts[k]; k++)
			{
				char parent_model[PATH_MAX];
				snprintf (parent_model, sizeof (parent_model), "%s%s", model_bases[b],
					model_exts[k]);
				struct stat st_m;
				// retail Switch collision is Zstandard-compressed: accept
				// the .zst twin when the plain stem is absent (EncodeCSB
				// re-compresses automatically for a .zst destination)
				if ((stat (parent_model, &st_m) || !S_ISREG (st_m.st_mode))
					&& !strcasecmp (model_exts[k], ".csb"))
				{
					snprintf (parent_model, sizeof (parent_model), "%s%s.zst",
						model_bases[b], model_exts[k]);
				}
				if (!stat (parent_model, &st_m) && S_ISREG (st_m.st_mode))
				{
					found_sibling = true;
					time_t ref_m_mtime = st_m.st_mtime;
					struct stat st_s;
					char s_path[PATH_MAX];
					snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
					if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_m_mtime)
						ref_m_mtime = st_s.st_mtime;

					if (st.st_mtime > ref_m_mtime + 2)
					{
						if (!strcasecmp (model_exts[k], ".hsf"))
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								enumError enc = EncodeModelToHSF (mdl, parent_model);
								if (enc <= ERR_WARNING && verbose >= 0)
									fprintf (stdlog, "REPACK HSF %s -> %s\n", path, parent_model);
								else if (max_err < enc)
									max_err = enc;
								FreeModel (mdl);
							}
							unlink (path);
							handled = true;
							break;
						}
						if (!strcasecmp (model_exts[k], ".msh"))
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								enumError enc = EncodeExciteMSH (mdl, parent_model);
								if (enc <= ERR_WARNING && verbose >= 0)
									fprintf (stdlog, "REPACK MSH %s -> %s\n", path, parent_model);
								else if (max_err < enc)
									max_err = enc;
								FreeModel (mdl);
							}
							unlink (path);
							handled = true;
							break;
						}
						if (!strcasecmp (model_exts[k], ".csb"))
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								// keep the source layout (Color Splash is
								// big-endian, everything else little-endian)
								bool csb_big = false;
								u8 *prev = 0;
								size_t prev_size = 0;
								if (!LoadFileAlloc (parent_model, 0, 0, &prev, &prev_size,
										0, 0, 0, false)
									&& prev_size <= UINT_MAX && prev_size)
								{
									const u8 *pdata = prev;
									uint psize = (uint)prev_size;
									u8 *pdecomp = 0;
									uint pdecomp_size = 0;
									if (IsZSTD (prev, psize) > 0
										&& DecodeZSTD (&pdecomp, &pdecomp_size, prev,
											psize) == ERR_OK
										&& pdecomp_size)
									{
										pdata = pdecomp;
										psize = pdecomp_size;
									}
									csb_t prev_csb;
									if (!ScanCSB (&prev_csb, pdata, psize))
										csb_big = prev_csb.big_endian;
									FreeCSB (&prev_csb);
									FREE (pdecomp);
								}
								FREE (prev);
								enumError enc
									= EncodeCSB (mdl, parent_model, csb_big, false);
								if (enc <= ERR_WARNING && verbose >= 0)
									fprintf (stdlog, "REPACK CSB %s -> %s\n", path, parent_model);
								else if (max_err < enc)
									max_err = enc;
								FreeModel (mdl);
							}
							unlink (path);
							handled = true;
							break;
						}
						if (!strcasecmp (model_exts[k], ".mod"))
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								enumError enc = EncodeExciteMOD (mdl, parent_model);
								if (enc <= ERR_WARNING && verbose >= 0)
									fprintf (stdlog, "REPACK MOD %s -> %s\n", path, parent_model);
								else if (max_err < enc)
									max_err = enc;
								FreeModel (mdl);
							}
							unlink (path);
							handled = true;
							break;
						}
						raw_data_t parent_raw;
						InitializeRawData (&parent_raw);
						if (LoadRawData (&parent_raw, false, parent_model, 0, false, 0) == ERR_OK)
						{
							model_t *mdl = ParseGLBFile (path);
							if (mdl)
							{
								uint8_t *injected = 0;
								size_t inj_size = 0;
								if (InjectDAEIntoModel (parent_raw.data, parent_raw.data_size, mdl,
										&injected, &inj_size)
									&& injected)
								{
									SaveFILE (parent_model, 0, true, injected, (uint)inj_size, 0);
									FREE (injected);
									unlink (path);
									handled = true;
									if (verbose >= 0)
										fprintf (
											stdlog, "REPACK INJECT %s -> %s\n", path, parent_model);
								}
								FreeModel (mdl);
							}
							ResetRawData (&parent_raw);
						}
					}
					if (handled)
						break;
				}
			}

			// Fallback: no sibling model file found; create a new Switch BFRES.
			// Only do this in a directory our own decoder never touched, e.g. a
			// hand-authored dir containing just "model.dae". A directory that
			// carries our hash-cache marker was produced/managed by wszst's own
			// extract/pack pipeline (any container format, not just Switch); an
			// orphan .glb there is just a read-only preview the decoder emitted,
			// not something to repack -- synthesizing a bogus Switch BFRES for
			// it corrupts the (possibly non-Switch) container and pointlessly
			// redoes work the decoder already did.
			// wszst-setup.txt marks the top of a tree our own extract/xexport
			// pipeline produced. It sits at the root of that tree, which may be
			// an ancestor of the directory actually being packed (e.g. CREATE
			// is pointed straight at a "<Group>(NW4R)" subfolder inside a
			// bigger extracted BRRES) -- walk upward looking for it.
			bool decoder_managed_dir = false;
			{
				char anc[PATH_MAX];
				snprintf (anc, sizeof (anc), "%s", root);
				for (int up = 0; up < 16 && anc[0]; up++)
				{
					char marker[PATH_MAX];
					snprintf (marker, sizeof (marker), "%s/%s", anc, SZS_SETUP_FILE);
					struct stat st_marker;
					if (stat (marker, &st_marker) == 0)
					{
						decoder_managed_dir = true;
						break;
					}
					char cache_marker[PATH_MAX];
					snprintf (cache_marker, sizeof (cache_marker), "%s/%s", anc, SZS_HASH_CACHE_FILE);
					struct stat st_cache;
					if (stat (cache_marker, &st_cache) == 0)
					{
						decoder_managed_dir = true;
						break;
					}
					ccp slash = strrchr (anc, '/');
					if (!slash || slash == anc)
						break;
					anc[slash - anc] = 0;
				}
			}
			if (!handled && !found_sibling && !decoder_managed_dir)
			{
				char bfres_path[PATH_MAX];
				snprintf (
					bfres_path, sizeof (bfres_path), "%.*s.bfres", (int)(strlen (path) - 4), path);
				struct stat st_bfres;
				if (stat (bfres_path, &st_bfres) != 0 || !S_ISREG (st_bfres.st_mode))
				{
					model_t *mdl = ParseGLBFile (path);
					if (mdl)
					{
						uint8_t *created = 0;
						size_t created_size = 0;
						if (CreateSwitchBFRES (mdl, &created, &created_size) && created)
						{
							SaveFILE (bfres_path, 0, true, created, (uint)created_size, 0);
							FREE (created);
							if (verbose >= 0)
								fprintf (stdlog, "REPACK CREATE %s -> %s\n", path, bfres_path);
							unlink (path);
						}
						FreeModel (mdl);
					}
				}
			}
			else if (!handled && !found_sibling && decoder_managed_dir)
			{
				// Genuine orphan case: our own decoder emitted this .glb/.dae as a
				// read-only preview (no repackable sibling exists, e.g. a Mii
				// reference like "mii_000.rsd" that has no embedded model at all).
				// It must not fall through to the generic directory packer, which
				// would otherwise silently stuff it into the archive as bogus new
				// content -- bloating the container and shifting/duplicating
				// entries the game never expected. Delete it; a future EXTRACT
				// regenerates the same preview on demand.
				unlink (path);
				if (verbose > 0)
					fprintf (stdlog, "SKIP PREVIEW %s\n", path);
			}
		}

		// Check for .ncer.xml or .nanr.xml or .byml.yaml files
		if (S_ISREG (st.st_mode) && nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".ncer.xml"))
		{
			char target_ncer[PATH_MAX];
			snprintf (target_ncer, sizeof (target_ncer), "%.*s", (int)(strlen (path) - 4), path);
			struct stat st_ncer;
			time_t ref_x_mtime = 0;
			if (stat (target_ncer, &st_ncer) == 0)
				ref_x_mtime = st_ncer.st_mtime;
			struct stat st_s;
			char s_path[PATH_MAX];
			snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
			if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_x_mtime)
				ref_x_mtime = st_s.st_mtime;

			if (ref_x_mtime == 0 || st.st_mtime > ref_x_mtime + 2)
			{
				enumError err = create_ncer_xml (path, target_ncer);
				if (err <= ERR_WARNING && verbose >= 0)
					fprintf (stdlog, "REPACK NCER XML %s -> %s\n", path, target_ncer);
				else if (max_err < err)
					max_err = err;
			}
			unlink (path);
		}
		if (S_ISREG (st.st_mode) && nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".nanr.xml"))
		{
			char target_nanr[PATH_MAX];
			snprintf (target_nanr, sizeof (target_nanr), "%.*s", (int)(strlen (path) - 4), path);
			struct stat st_nanr;
			time_t ref_x_mtime = 0;
			if (stat (target_nanr, &st_nanr) == 0)
				ref_x_mtime = st_nanr.st_mtime;
			struct stat st_s;
			char s_path[PATH_MAX];
			snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
			if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_x_mtime)
				ref_x_mtime = st_s.st_mtime;

			if (ref_x_mtime == 0 || st.st_mtime > ref_x_mtime + 2)
			{
				enumError err = create_nanr_xml (path, target_nanr);
				if (err <= ERR_WARNING && verbose >= 0)
					fprintf (stdlog, "REPACK NANR XML %s -> %s\n", path, target_nanr);
				else if (max_err < err)
					max_err = err;
			}
			unlink (path);
		}
		if (S_ISREG (st.st_mode) && ((nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".byml.yml"))
			|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".byml.yaml"))
			|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".byaml.yml"))
			|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".byaml.yaml"))
			|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".sbyml.yml"))
			|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".sbyml.yaml"))))
		{
			char target_byml[PATH_MAX];
			snprintf (target_byml, sizeof (target_byml), "%.*s",
				(int)(strlen (path) - (is_ext (path, ".yml") ? 4 : 5)), path);
			struct stat st_byml;
			time_t ref_x_mtime = 0;
			if (stat (target_byml, &st_byml) == 0)
				ref_x_mtime = st_byml.st_mtime;
			struct stat st_s;
			char s_path[PATH_MAX];
			snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
			if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_x_mtime)
				ref_x_mtime = st_s.st_mtime;

			if (ref_x_mtime == 0 || st.st_mtime > ref_x_mtime + 2)
			{
				enumError err = encode_byml_file (path, target_byml);
				if (err <= ERR_WARNING && verbose >= 0)
					fprintf (stdlog, "REPACK BYML %s -> %s\n", path, target_byml);
				else if (max_err < err)
					max_err = err;
			}
			unlink (path);
		}
		if (S_ISREG (st.st_mode)
			&& ((nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".aamp.yml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".aamp.yaml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".baamp.yml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".baamp.yaml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".bparam.yml"))
				|| (nlen > 12 && !strcasecmp (de->d_name + nlen - 12, ".bparam.yaml"))
				|| (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".bgenv.yml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".bgenv.yaml"))
				|| (nlen > 11 && !strcasecmp (de->d_name + nlen - 11, ".bglpbd.yml"))
				|| (nlen > 12 && !strcasecmp (de->d_name + nlen - 12, ".bglpbd.yaml"))))
		{
			char target_aamp[PATH_MAX];
			snprintf (target_aamp, sizeof (target_aamp), "%.*s",
				(int)(strlen (path) - (is_ext (path, ".yml") ? 4 : 5)), path);
			struct stat st_aamp;
			time_t ref_x_mtime = 0;
			if (stat (target_aamp, &st_aamp) == 0)
				ref_x_mtime = st_aamp.st_mtime;
			struct stat st_s;
			char s_path[PATH_MAX];
			snprintf (s_path, sizeof (s_path), "%s/%s", root, SZS_SETUP_FILE);
			if (stat (s_path, &st_s) == 0 && st_s.st_mtime > ref_x_mtime)
				ref_x_mtime = st_s.st_mtime;

			if (ref_x_mtime == 0 || st.st_mtime > ref_x_mtime + 2)
			{
				enumError err = encode_aamp_file (path, target_aamp);
				if (err <= ERR_WARNING && verbose >= 0)
					fprintf (stdlog, "REPACK AAMP %s -> %s\n", path, target_aamp);
				else if (max_err < err)
					max_err = err;
			}
			unlink (path);
		}

		// Check for .d directories (e.g. foo.brres.d, foo.szs.d, foo.wbfs.d, bowling.d, local.d,
		// etc.)
		if (S_ISDIR (st.st_mode) && nlen > 2 && !strcasecmp (de->d_name + nlen - 2, ".d"))
		{
			char target_file[PATH_MAX];
			snprintf (target_file, sizeof (target_file), "%.*s", (int)(strlen (path) - 2), path);

			struct stat st_target;
			bool target_exists
				= (stat (target_file, &st_target) == 0 && S_ISREG (st_target.st_mode));
			if (!target_exists)
			{
				// Native decoders operate on a decompressed "*.bin" sidecar, but
				// the real sibling is normally the pre-decompression name.  Resolve
				// "foo.nsbtx.bin.d" back to "foo.nsbtx" before trying to invent
				// candidates such as "foo.nsbtx.bin.nsbtx".
				const size_t target_len = strlen(target_file);
				if (target_len > 4 && !strcasecmp(target_file + target_len - 4, ".bin"))
				{
					char raw_target[PATH_MAX];
					snprintf(raw_target,sizeof(raw_target),"%.*s",(int)target_len-4,target_file);
					if (stat(raw_target,&st_target) == 0 && S_ISREG(st_target.st_mode))
					{
						snprintf(target_file,sizeof(target_file),"%s",raw_target);
						target_exists = true;
					}
				}
				for (int c = 0; cand_archive_exts[c]; c++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%s%s", target_file, cand_archive_exts[c]);
					if (stat (cand, &st_target) == 0 && S_ISREG (st_target.st_mode))
					{
						snprintf (target_file, sizeof (target_file), "%s", cand);
						target_exists = true;
						break;
					}
				}
				for (int c = 0; !target_exists && cand_media_exts[c]; c++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%s%s", target_file, cand_media_exts[c]);
					if (stat (cand, &st_target) == 0 && S_ISREG (st_target.st_mode))
					{
						snprintf (target_file, sizeof (target_file), "%s", cand);
						target_exists = true;
					}
				}
				for (int c = 0; !target_exists && cand_lz_exts[c]; c++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%s%s", target_file, cand_lz_exts[c]);
					if (stat (cand, &st_target) == 0 && S_ISREG (st_target.st_mode))
					{
						snprintf (target_file, sizeof (target_file), "%s", cand);
						target_exists = true;
					}
				}
				for (int c = 0; !target_exists && cand_ash_exts[c]; c++)
				{
					char cand[PATH_MAX];
					snprintf(cand,sizeof(cand),"%s%s",target_file,cand_ash_exts[c]);
					if (stat(cand,&st_target) == 0 && S_ISREG(st_target.st_mode))
					{
						snprintf(target_file,sizeof(target_file),"%s",cand);
						target_exists = true;
					}
				}
				// Directory names often omit the inner archive suffix: foo.d can
				// correspond to foo.arc.lz, not merely foo.lz. Try each known
				// archive suffix under the raw LZ wrapper as well.
				for (int a = 0; !target_exists && cand_archive_exts[a]; a++)
					for (int l = 0; !target_exists && cand_lz_exts[l]; l++)
					{
						char cand[PATH_MAX];
						snprintf(cand,sizeof(cand),"%s%s%s",target_file,cand_archive_exts[a],cand_lz_exts[l]);
						if (stat(cand,&st_target) == 0 && S_ISREG(st_target.st_mode))
						{
							snprintf(target_file,sizeof(target_file),"%s",cand);
							target_exists = true;
						}
					}
				for (int a = 0; !target_exists && cand_archive_exts[a]; a++)
					for (int x = 0; !target_exists && cand_ash_exts[x]; x++)
					{
						char cand[PATH_MAX];
						snprintf(cand,sizeof(cand),"%s%s%s",target_file,cand_archive_exts[a],cand_ash_exts[x]);
						if (stat(cand,&st_target) == 0 && S_ISREG(st_target.st_mode))
						{
							snprintf(target_file,sizeof(target_file),"%s",cand);
							target_exists = true;
						}
					}
			}

			// Check wszst-setup.txt inside path to deduce format if not found
			if (!target_exists)
			{
				SetupParam_t sp;
				InitializeSetupParam (&sp);
				if (ScanSetupParam (&sp, true, path, SZS_SETUP_FILE, 0, true) == ERR_OK)
				{
					ccp sp_ext = GetExtFF (sp.fform_file, sp.fform_arch);
					if (sp_ext && *sp_ext)
					{
						char cand[PATH_MAX];
						snprintf (cand, sizeof (cand), "%s%s", target_file, sp_ext);
						snprintf (target_file, sizeof (target_file), "%s", cand);
						if (stat (target_file, &st_target) == 0 && S_ISREG (st_target.st_mode))
							target_exists = true;
					}
				}
				ResetSetupParam (&sp);
			}

			// Only repack if an existing target container exists, or if this .d folder
			// is recognized as an archive directory
			if (!target_exists && !is_archive_dir_name (path, strlen (path)))
				continue;

			// A companion-tool staging tree is never a generic archive, even
			// though is_archive_dir_name() claims it (see
			// is_passthrough_staging_dir()).  Repacking it here would invent
			// a bogus container and delete the tree the companion needs.
			if (!target_exists && is_passthrough_staging_dir (path))
			{
				if (verbose >= 0)
					fprintf (stdlog, "SKIP STAGING %s/ (needs explicit --dest)\n", path);
				continue;
			}

			// Media extraction produces one editable MP4 preview. If its content is
			// newer than the source, send it back through mobipeg using settings
			// recovered from the source; otherwise discard the preview directory.
			ccp t_ext = strrchr (target_file, '.');
			if (t_ext
				&& (!strcasecmp (t_ext, ".thp") || !strcasecmp (t_ext, ".moflex")
					|| !strcasecmp (t_ext, ".mo") || !strcasecmp (t_ext, ".mods")))
			{
				DIR *md = opendir (path); struct dirent *me; char preview[PATH_MAX] = {0};
				while (md && (me = readdir (md)))
				{
					size_t ml = strlen (me->d_name);
					if (ml > 4 && !strcasecmp (me->d_name + ml - 4, ".mp4"))
						{ snprintf (preview, sizeof (preview), "%s/%s", path, me->d_name); break; }
				}
				if (md) closedir (md);
				struct stat ps;
				const bool media_changed = *preview && target_exists && !stat (preview, &ps)
					&& ps.st_mtime > st_target.st_mtime;
				if (media_changed)
				{
					enumError err = PassthruReencodeMedia (preview, target_file);
					if (err == ERR_NOTHING_TO_DO)
					{
						ERROR0 (ERR_WARNING,
							"edited media retained because mobipeg is unavailable: %s", preview);
						if (max_err < ERR_WARNING) max_err = ERR_WARNING;
						continue;
					}
					if (err > ERR_WARNING)
					{
						if (max_err < err) max_err = err;
						continue; // keep the edited preview so a failed encode loses nothing
					}
				}
				remove_dir_recursive (path);
				continue;
			}

			// Audio previews and archives are not generic U8 directories.
			if (t_ext
				&& (!strcasecmp (t_ext, ".brsar") || !strcasecmp (t_ext, ".sdat")
					|| !strcasecmp (t_ext, ".brstm")
					|| !strcasecmp (t_ext, ".bcstm") || !strcasecmp (t_ext, ".bfstm")
					|| !strcasecmp (t_ext, ".bns") || !strcasecmp (t_ext, ".ast")
					|| !strcasecmp (t_ext, ".dsp")))
			{
				remove_dir_recursive (path);
				continue;
			}

			time_t ref_mtime = 0;
			struct stat st_setup;
			char setup_path[PATH_MAX];
			snprintf (setup_path, sizeof (setup_path), "%s/%s", path, SZS_SETUP_FILE);
			if (stat (setup_path, &st_setup) == 0)
				ref_mtime = st_setup.st_mtime;
			else if (target_exists)
			{
				ref_mtime = st_target.st_mtime;

				// normalize_legacy_tree_mtimes() folds in the one-time clock skew
				// between a container and decoder output it never got a
				// wszst-setup.txt marker for (GFA/PAC/SARC/etc. extraction doesn't
				// write one). Without a guard this reruns on every CREATE and
				// re-stamps every member's mtime down to ref_mtime each time --
				// which also erases a genuine edit made shortly (<600s) after the
				// last build, so the very next CREATE sees "nothing newer" and
				// silently skips rebuilding a sub-archive that did change. Run it
				// at most once per tree and leave a marker behind.
				char baseline_marker[PATH_MAX];
				snprintf (baseline_marker, sizeof (baseline_marker), "%s/%s",
					path, SZS_MTIME_BASELINE_FILE);
				struct stat st_marker;
				if (stat (baseline_marker, &st_marker) != 0)
				{
					normalize_legacy_tree_mtimes(path,ref_mtime);
					FILE *mf = fopen (baseline_marker, "w");
					if (mf)
						fclose (mf);
				}
			}

			bool need_rebuild
				= !target_exists || (ref_mtime > 0 ? is_dir_newer_than (path, ref_mtime) : true);

			if (need_rebuild)
			{
				int lz_type = 0;
				if (target_exists)
				{
					FILE *in = fopen(target_file,"rb");
					if (in)
					{
						u8 type = 0;
						if (fread(&type,1,1,in) == 1 && (type == 0x10 || type == 0x11))
							lz_type = type;
						fclose(in);
					}
				}
				// Wii banner wrappers (IMET / IMD5+"LZ77") decode to a plain
				// U8 tree: re-wrap the rebuilt U8 instead of writing it bare.
				enumError err = repack_wii_banner_dir (path, target_file);
				if (err == ERR_NOTHING_TO_DO)
				{
					err = create_archive_from_dir (path, target_file, lz_type);
					if (err <= ERR_WARNING)
					{
						if (verbose >= 0)
							fprintf (stdlog, "REPACK %s/ -> %s\n", path, target_file);
						remove_dir_recursive (path);
					}
					else if (max_err < err)
						max_err = err;
				}
				else if (max_err < err)
					max_err = err;
			}
			else
			{
				// Target is already up to date; clean up the leftover .d directory
				remove_dir_recursive (path);
			}
		}
	}
	closedir (dir);
	return max_err;
}
