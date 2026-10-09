static enumError extract_one_file_inner (ccp arg, ccp basedir, uint depth)
{
	if (is_ho_asset_name (arg))
	{
		const enumError terr = extract_ho_texture_file (arg, basedir, depth);
		return terr == ERR_NOTHING_TO_DO ? ERR_OK : terr;
	}
	// Asobo BigFile members ("hash.Class_Z" plus ".lnk" link headers) are
	// opaque engine data apart from bitmaps.
	if ((is_ext (arg, ".lnk") || (strlen (arg) > 2 && !strcasecmp (arg + strlen (arg) - 2, "_Z")))
		&& !is_ext (arg, ".bitmap_z") && !is_ext (arg, ".sound_z") && !is_ext (arg, ".mesh_z"))
		return ERR_OK;
	// Grip .res sections other than textures are opaque object data; keep the
	// magic-less sniffers from unpacking them into junk ".d" folders.
	{
		ccp slash = strrchr (arg, '/');
		if (slash && slash - arg > 6 && !memcmp (slash - 6, ".res.d", 6) && !is_ext (arg, ".surf"))
			return ERR_OK;
	}
	// Muramasa (Wii) ".otb"/".nsi" tables have loose, magic-less-looking
	// internal count/offset fields that a generic archive/table sniffer
	// further down this function can mistake for its own format (observed:
	// ".otb" spuriously matched as an unknown/generic member archive). Give
	// the extension-gated Muramasa probes first refusal, before any of the
	// generic sniffers below get a chance to misfire on them.
	// Retro PAK v2 has a strict header; claim it before the loose ".pak" sniffers.
	enumError err = extract_retropak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_muramasa_otb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_muramasa_nsi_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_muramasa_fcmp_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Ubisoft ABE bigfiles are unpacked in full; the Red Steel 2 manifest below is the fallback.
	err = extract_abebf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wiiresarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_packv2_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_arcb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_iga_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mrqz_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_fcat_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ttpl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_tplc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_texl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_strt_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_fuax_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_stgs_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wbl10n_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_schl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wiitex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ptex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gfmc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gct0_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Red Steel 2 (Wii) formats -- see lib-redsteel2.h.
	err = extract_redsteel2_abe_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_redsteel2_rel_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Zack & Wiki: Quest for Barbaros' Treasure (Wii) formats -- see
	// lib-zackwiki.h.
	err = extract_zackwiki_tm2_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_zackwiki_ppg_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_zackwiki_tsb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_zackwiki_whd_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_zackwiki_mds_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_zackwiki_ssd_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Banpresto/Now Production tagged resource table (Crayon Shin-chan:
	// Saikyou Kazoku Kasukabe King, Wii) -- see lib note in
	// extract_hgpk_pak_file(). Reached directly as a disc ".pak" file, and
	// as the ".hgpk" file decompress_nintendo_file3()'s MTXZ branch (below)
	// produces from a zlib-wrapped ".mz" asset.
	err = extract_bermuda_pki_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_hgpk_pak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	char pbase_buf[PATH_MAX] = "";
	ccp pbase = opt_dest && *opt_dest ? opt_dest : basedir;
	if (pbase)
	{
		snprintf (pbase_buf, sizeof (pbase_buf), "%s", pbase);
		const uint n = strlen (pbase_buf);
		if (n && pbase_buf[n - 1] != '/' && n + 1 < sizeof (pbase_buf))
		{
			pbase_buf[n] = '/';
			pbase_buf[n + 1] = 0;
		}
		pbase = pbase_buf;
	}

	// This manifest is emitted by every native archive extraction. It is an
	// input to CREATE, not another container to peel during recursive XX.
	ccp arg_name = strrchr (arg, '/');
	arg_name = arg_name ? arg_name + 1 : arg;
	if (!strcmp (arg_name, "wszst-setup.txt"))
		return ERR_OK;
	if (depth && (!strncmp (arg_name, "image.", 6) || !strncmp (arg_name, "mipmap-", 7)))
		return ERR_OK;

	// ExciteBots font textures can begin with 00 01 00 01, which is also a
	// plausible object-flow version/count pair. Extension plus the strict
	// texture-header/size validation must therefore get first refusal before
	// generic format/pass-through probes classify them as OBFLOW.
	err = extract_artetex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_tex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bmg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ctxb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bntx_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = decode_wimg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pfctex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bfttf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bnvib_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nacp_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pfs0_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nro_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nso_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cnmt_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_npdm_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_xci_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_art_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_zip_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Containers the main tools cannot open natively are passed through
	// to external unpackers (wit, ndstool, ctrtool, sharpii).  The strong
	// variant runs FIRST, claimed by header signature only: it neither
	// reads the whole file (so disc images above --max-file-size reach
	// wit instead of failing the native probes) nor steals any file the
	// native decoders can handle (extension-only claims are left below).
	if (!opt_no_passthrough)
	{
		char staged_dir[PATH_MAX] = "";
		enumError pas_err = PassthruExtractStrong (arg, pbase, staged_dir, sizeof (staged_dir));
		if (pas_err != ERR_NOTHING_TO_DO)
			return pas_err == ERR_OK && *staged_dir ? extract_tree_complete (staged_dir, depth + 1)
													: pas_err;
	}
	err = ExtractSmashARC (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractIQIPack (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_sarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_storybook_one_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bigf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	if (is_littlest_pet_shop_rpk (arg))
		return ERR_OK;

	err = extract_abe_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Stream conversion must precede generic archive sniffers.
	if (is_ext (arg, ".sad") || is_ext (arg, ".sadl"))
	{
		err = decompress_nintendo_file (arg);
		if (err != ERR_NOTHING_TO_DO)
			return err;
	}
	err = extract_iear_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pck2_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pikarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_alar_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_arc0_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gfa_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rz_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_res_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rst_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_arika_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_thp_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_namco_sszl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_namco_vcra_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_arcv_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Bully: Scholarship Edition's collision.pak shares the ".pak" extension
	// with several weaker, extension-driven probes below (extract_pac_file
	// and friends), which would otherwise claim it first and silently
	// produce nothing. Its own gate is strictly content-based (declared
	// size, a fully chained offset/size table, and every member starting
	// with a real GTA-family COL FourCC), so it is safe to try ahead of
	// those weaker heuristics.
	err = ExtractCollPakArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_pac_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gpkg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cod_pak0_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gpak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bns_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Retro Studios RPAK has a decoder but no lossless encoder yet.  Leaving
	// it opaque is essential: expanding it into a .d directory makes an
	// unchanged retail game appear editable even though CREATE cannot put its
	// entry table, compression flags, and alignment back byte-for-byte.
	// Keep the original .pak in the staged tree until the paired encoder exists.
	if (is_rpak_file (arg))
		return ERR_OK;
	err = extract_mpr_pack_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_jarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_lspk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_fsys_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_warc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_sfzdat_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cpk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ipk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dig_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_agi_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_blpkg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_goliath_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_twl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wsi_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_xpk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bcbig_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nsgh_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cpac_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_l5pac_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cingwpf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_jupiterpck_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_treasuremrg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_atlusndx_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pckg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_natbin_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bcdspi_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dbkmap_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rbark_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nibm_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rcf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rso_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wadh_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_hog_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_hvp_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rkv_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pdabra_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_threedf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_shadebin_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_shadebln_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_spr2df_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_eangc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_spooktxf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ndp_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_txg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_eabnk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ealoc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dspwrap_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wiiadpcm_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nlzlib_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mtarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_capcomtex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_capcaps_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_asurasfx_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_redspark_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_retrotxtr_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_csmp_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nloc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_eashpg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_refpack_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nub_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_spt_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pwbundle_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_fst_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_catcar_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_termpod_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_jadebig_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_thqpack_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_lpak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ho_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_asobo_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_vblank_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_fsb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_kraw_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dcx_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dct_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dsp_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pk2_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_xtd_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_xmd_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_was_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dksoundbank_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dkzf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_afs_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rtdp_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cxd_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_fpk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = ExtractCtrPack (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCtrResource (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_thor_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ttarch_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nif_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gripres_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gripsurf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rev_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bltex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_asobo_mesh_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_asobo_sound_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_asobo_bitmap_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_avalanche_tex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bj_tx_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bj_mtm_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bj_audio_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_torus_hnk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pub_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rez_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bombshell_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bttrb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_toshi_ttl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_toshi_tkl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_fbc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gct_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gcg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_blact_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wta_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bg4_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cram_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_sumostz_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_sa01_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_hwlegends_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rflres_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bea_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Check the complete MDR chunk headers and wrapped G1T signature before
	// the less distinctive MPBIN and ATB structures.
	err = ExtractMDRArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractG1TArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_mpbin_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_zmb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_atb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ptd_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_lzbin_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_hbdf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_effn_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pctl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mii_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_msh_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_csb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_hsf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_g1m_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_glg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_can_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mod_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nud_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ninja_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dc2model_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_numshb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mpr_cmdl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wmb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ttmodel_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nut_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dtls_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = ExtractXPCKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractZTABArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPVOLArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSTPKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractF9ResArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractZLARCArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGARArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMKGPDXPacArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTMPKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractNXARCArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractAPAKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPACArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPKZArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractVIBSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPGDATArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractWTAArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGFLXPackArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGFModelPackArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGFPackageArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGFPAKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractBARSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractFSBArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCSPakArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSBTArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGDSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCHXArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMapBadArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTXDArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractIdeImgArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractAGRArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractBamArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCSFontArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCSCsiArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractNLGDictArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSANIMArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTXTGArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractROMFSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractXTXArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTVOLArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMTXTArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractNUS3AudioArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSIR0Archive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPERSFile (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractZDATArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractRZPKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_sfx_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = ExtractVFFArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCamelotTexBank (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPTLGArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMIOArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nccarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ccf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_sequence_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = decode_romc_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_darc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_sar_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_narc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_at7_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ctpk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gsh_shaders (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_imet_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nds_banner_metadata (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wibn_metadata (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_smdh_metadata (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nitro_sprite_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cfnt_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_cfnt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = decode_brfnt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = decode_bflyt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = decode_byml_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = export_kcl_glb_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_msbt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_ue4_pak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_prc_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_rstb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_aamp_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_sharc_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bnsh_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nushdb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_numatb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nusktb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nufxlb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_numdlb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nuhlpb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nulstb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nurpdb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nuanmb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gfmotion_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_gf1motion_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mtmrl_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_chnk_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_magma_fat_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_voice_song_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_voice_amc_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_voice_ams_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_voice_palcat_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_voice_palseq_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dogisland_wds_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dogisland_wdb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dogisland_yobj_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dogisland_pms_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dogisland_mtt_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dogisland_cprm_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dogisland_script_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_dogisland_mpq_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mtmfx_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mtmod_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_mbn_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_xmb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_adjb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_bfsha_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_effn_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_pctl_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_wtb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_trpak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_cnut_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_xmsg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nwr_levelinfo_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nwr_animtiles_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nsmbw_chk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_nsmbw_tex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_kpbin_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_chans_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Last of the archive scanners: Metroid: Samus Returns has no magic at
	// all, so it only gets a look after every signature-bearing format has
	// declined the file.
	err = extract_metroidsr_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = decode_image_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Covers a standalone .bfres passed directly as a single argument (not
	// reached by extract_bfres_textures_tree()'s pre-pass, which only walks
	// directory/archive-shaped extractions). Always returns ERR_NOTHING_TO_DO
	// -- see its own comment for why it must not short-circuit this chain.
	extract_bfres_textures (arg, basedir, depth);
	extract_bcres_textures (arg, basedir, depth);
	// The HAL .dat/.usd probes are different: they DO claim the file. Those
	// archives are not directory-style containers, so once identified there is
	// nothing left below for the chain to do -- and the generic SZS/U8
	// extractor at the end would only fail them with ERR_FILE_ALREADY_EXISTS.
	err = extract_hsd_textures (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_halbank_textures (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = export_model_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_bfres_switch_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = convert_brsar_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// A native archive's extracted BRSUB members (TEX0/PLT0/CHR0/etc., and
	// MDL0 while the outer two-pass export is deferred) are already the leaf
	// resources we want. Do not cut those detached leaves again when walking
	// an archive's freshly completed output directory.
	{
		// GetByMagicFF deliberately requires at least 0x20 bytes before it
		// classifies BRSUB resources; shorter probes avoid mistaking the small
		// extraction metadata fragments for real TEX0/MDL0/etc. records.
		u8 head[0x40];
		FILE *f = fopen (arg, "rb");
		const size_t got = f ? fread (head, 1, sizeof (head), f) : 0;
		if (f)
			fclose (f);
		const file_format_t leaf_ff = GetByMagicFF (head, (uint)got, 0);
		if ((got >= 0x20 && IsBRSUB (leaf_ff)) || leaf_ff == FF_PLT0 || leaf_ff == FF_PNG
			|| leaf_ff == FF_BREFT_IMG || (GetAttribFF (leaf_ff) & FFT_TEXT))
			return ERR_OK;
	}

	// Raw Nintendo codecs are not U8/SZS archives, but users expect the
	// regular extraction front end to handle them as a single extracted file.
	// This also makes a compressed asset found inside an extracted tree usable
	// without switching to a separate command.
	char decompressed_path[PATH_MAX];
	bool decompressed_is_final = false;
	err = decompress_nintendo_file3 (
		arg, decompressed_path, sizeof (decompressed_path), &decompressed_is_final, false);
	if (err == ERR_OK)
	{
		// Every check above (SARC/GFA/PAC/DARC, BRFNT/BFLYT auto-decode,
		// model export) ran against the still-compressed bytes and correctly
		// declined. decompress_nintendo_file2() wrote the decompressed
		// payload to its own destination path (== 'arg' only when 'arg'
		// already lives under the --dest tree; a genuinely different path
		// otherwise -- recursing on 'arg' itself would just re-read the
		// still-compressed source forever and hit the depth cap). Re-run the
		// same dispatch chain against that destination now that it might
		// actually be one of those -- confirmed necessary on a real retail
		// 3DS disc, where every DARC-bundled layout family ships
		// LZ11-compressed (extract_darc_file() never saw a "darc" magic
		// before this, only the compressed bytes in front of it). The
		// decompressed payload can never itself start with the same codec's
		// magic byte again, so this terminates in one extra hop in practice;
		// the depth cap only guards against a corrupt/adversarial file.
		const enumError sub_err = depth < 32
			? extract_one_file (decompressed_path, basedir, depth + 1)
			: ERR_FILE_TOO_BIG;

		// If nothing further recognised the decompressed payload as a real
		// container (extract_one_file() left it sitting there untouched,
		// same file it wrote above), it's just an inert dump kept for
		// inspection -- the same class of bug as extract_mii_file()'s
		// glb/png previews: leaving it on disk lets a later CREATE sweep it
		// into the rebuilt archive as bogus new content. Discard it unless
		// the caller explicitly asked to keep raw dumps.
		// ... but not when the payload is itself the deliverable. A .bfwav
		// decodes to a finished WAV, which no container probe claims, so
		// this discard used to delete every decoded wave the extractor
		// produced -- BFWAV/BCWAV/BRWAV conversion never left a file behind.
		//
		// ... and not when --auto was given either. --auto's own advertised
		// purpose ("recursively decode recognised raw Nintendo compression
		// streams found in the extracted tree") is to KEEP exactly this
		// class of output; cmd_extract()'s single-archive path already
		// honours it that way via auto_decompress_tree(). But XX/XDECODE/
		// XEXPORT/XALL given a plain directory (not an archive file) never
		// go through cmd_extract()'s per-archive branch at all -- they walk
		// straight into extract_tree()/extract_one_file(), which passes
		// depth+1 for every single regular file found by ordinary directory
		// recursion (see extract_tree()). So depth>0 here is true for
		// essentially every loose file in a real, already-unpacked game
		// tree, not just for a payload actually unwrapped from inside a
		// nested container -- which silently discarded every decoded
		// LZ10/LZ11/etc. leaf asset (raw NDS tile/graphics data, etc.) that
		// didn't also happen to parse as some further container format.
		if (!opt_export_raw && !OptionUsed[OPT_AUTO] && !decompressed_is_final && depth > 0)
		{
			struct stat st_leftover;
			if (!stat (decompressed_path, &st_leftover) && S_ISREG (st_leftover.st_mode))
				unlink (decompressed_path);
		}
		return sub_err;
	}
	if (err != ERR_NOTHING_TO_DO)
		return err;
	// Extension-only pass-through claims (.nds/.cia/.3ds/.cci/.cxi/
	// .wad/.app) run only after every native probe declined the file, so
	// a container misnamed as, say, a WAD never masks a real SZS archive.
	if (!opt_no_passthrough)
	{
		char staged_dir[PATH_MAX] = "";
		enumError pas_err = PassthruExtract (arg, pbase, staged_dir, sizeof (staged_dir));
		if (pas_err != ERR_NOTHING_TO_DO)
			return pas_err == ERR_OK && *staged_dir ? extract_tree_complete (staged_dir, depth + 1)
													: pas_err;
	}
	// Do not use a shallow magic or filename filter to decide whether a
	// nested member deserves the generic parser. Every format-specific probe
	// above has already had first refusal; this final path lets LoadCreateSZS()
	// run its complete structural validation for every remaining member.
	// A FourCC, ID string, or codec byte is only a candidate marker -- those
	// markers collide in real game data, while valid formats can keep their
	// identifying data away from offset zero. At recursive depth an unclaimed
	// or invalid member is quietly ignored below, so this is both exhaustive
	// and safe.

	szs_file_t szs;
	InitializeSZS (&szs);
	err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, false);
	if (err)
	{
		ResetSZS (&szs);

		// Last-resort fallback, tried only once the real U8/SARC/etc. parser
		// above has declined the file outright: an otherwise-opaque blob may
		// still carry self-describing embedded resources worth pulling out
		// (see extract_embedded_nw4r_file()'s own comment). This must run
		// AFTER LoadCreateSZS(), not before -- a legitimate U8 archive (e.g.
		// a Wii layout .arc bundling a BRLYT/BRLAN pair as regular SARC
		// members) contains exactly the byte patterns this scanner looks
		// for, so running it first stole real archives -- including their
		// textures -- and left only the two raw layout blobs it knows how
		// to name, silently dropping everything else in the archive.
		enumError nw4r_err = extract_embedded_nw4r_file (arg, basedir, depth);
		if (nw4r_err != ERR_NOTHING_TO_DO)
			return nw4r_err;

		return depth > 0 ? ERR_OK : err;
	}
	if (!err)
	{
		DASSERT (!szs.file_size || szs.file_size >= szs.size);

		if (szs.size >= 4 && !memcmp (szs.data, "SARC", 4))
		{
			err = extract_sarc_mem (arg, basedir, depth, szs.data, szs.size);
			ResetSZS (&szs);
			return err;
		}
		if (szs.size >= 4 && !memcmp (szs.data, "CTPK", 4))
		{
			err = extract_ctpk_mem (arg, basedir, depth, szs.data, szs.size);
			ResetSZS (&szs);
			return err;
		}
		if (szs.size >= 4 && (!memcmp (szs.data, "NARC", 4) || !memcmp (szs.data, "CRAN", 4)))
		{
			err = extract_narc_mem (arg, basedir, depth, szs.data, szs.size);
			ResetSZS (&szs);
			return err;
		}

		if (analyze_fname && IsBRSUB (szs.fform_arch))
			AnalyzeBRSUB (&szs, szs.data, szs.size, arg);

		if (szs.fform_arch == FF_SDAT || szs.fform_arch == FF_BCSAR || szs.fform_arch == FF_BFSAR)
		{
			ResetSZS (&szs);
			return depth > 0 ? ERR_OK : ERR_NOTHING_TO_DO;
		}

		if (!IsArchiveFF (szs.fform_arch) && !IsBRSUB (szs.fform_arch))
		{
			ResetSZS (&szs);
			return depth > 0 ? ERR_OK : ERR_NOTHING_TO_DO;
		}

		have_patch_count -= 1000000;
		PRINT ("EXTRACT/%s[%s]: %s\n", __FUNCTION__, GetNameFF_SZS (&szs), szs.fname);
		err = ExtractFilesSZS (&szs, 0, false, 0, basedir);
		have_patch_count += 1000000;
		if (err <= ERR_WARNING)
		{
			// ExtractFilesSZS writes nested archives as ordinary files. Peel that
			// completed output immediately; relying on readdir() to notice a new
			// sibling directory is filesystem/order dependent and missed 1,700
			// ACCF MDL0s stored inside the Str/Brres archives.
			char dest[PATH_MAX];
			get_extract_dest (dest, sizeof (dest), &szs);
			ccp saved_dest = opt_dest;
			const int saved_export_count = export_count;
			opt_dest = 0;
			export_count = 0;
			enumError sub_err = extract_tree (dest, depth + 1);
			export_count = saved_export_count;
			opt_dest = saved_dest;
			if (err < sub_err)
				err = sub_err;
		}
		if (err <= ERR_WARNING && export_count > 0)
		{
			char dest[PATH_MAX];
			get_extract_dest (dest, sizeof (dest), &szs);
			ccp saved_dest = opt_dest;
			opt_dest = 0; // each exported model belongs beside its own source
			// BFRES FTEX textures need to already be on disk before the search
			// root is indexed below -- see extract_bfres_textures_tree()'s own
			// comment for why this can't just be folded into export_models_
			// tree()'s per-file loop like the other decoders there.
			extract_bfres_textures_tree (dest, 0);
			// Even a single BRRES can declare run-time/EFB texture resources that
			// have no TEX0 payload. Enable the completed archive directory as the
			// lookup root so COLLADA only emits images that were actually decoded.
			SetDAETextureSearchRoot (dest);
			enumError model_err = export_models_tree (dest, 0);
			SetDAETextureSearchRoot (0);
			opt_dest = saved_dest;
			if (err < model_err)
				err = model_err;
		}
		if (err <= ERR_WARNING && OptionUsed[OPT_AUTO])
		{
			char dest[PATH_MAX];
			get_extract_dest (dest, sizeof (dest), &szs);
			ccp saved_dest = opt_dest;
			opt_dest = 0; // each decoded file belongs beside its own source
			enumError auto_err = auto_decompress_tree (dest, 0);
			opt_dest = saved_dest;
			if (err < auto_err)
				err = auto_err;
		}
	}
	ResetSZS (&szs);
	return depth && err ? ERR_OK : err;
}
