
static enumError cmd_create (bool create)
{
	static const char dest_fname[] = "\1P/\1N\1?T";
	CheckOptDest (dest_fname, false);
	const bool save_auto_add = opt_auto_add;
	enumError max_err = ERR_OK;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		opt_auto_add = save_auto_add; // restore auto-add settings

		ccp arg = plist.field[argi];
		const uint arg_len = strlen (arg);
		if (create && arg_len > 9 && !strcasecmp (arg + arg_len - 9, ".ncer.xml"))
		{
			char xml_dest[PATH_MAX];
			if (opt_dest)
				SubstDest (xml_dest, sizeof (xml_dest), arg, opt_dest, 0, ".ncer", false);
			else
			{
				snprintf (xml_dest, sizeof (xml_dest), "%s", arg);
				xml_dest[arg_len - 4] = 0; // remove only .xml, retain .ncer
			}
			const enumError xml_err = create_ncer_xml (arg, xml_dest);
			if (max_err < xml_err)
				max_err = xml_err;
			continue;
		}
		if (create && arg_len > 9 && !strcasecmp (arg + arg_len - 9, ".nanr.xml"))
		{
			char xml_dest[PATH_MAX];
			if (opt_dest)
				SubstDest (xml_dest, sizeof (xml_dest), arg, opt_dest, 0, ".nanr", false);
			else
			{
				snprintf (xml_dest, sizeof (xml_dest), "%s", arg);
				xml_dest[arg_len - 4] = 0;
			}
			const enumError xml_err = create_nanr_xml (arg, xml_dest);
			if (max_err < xml_err)
				max_err = xml_err;
			continue;
		}
		if (create && arg_len > 10 && !strcasecmp (arg + arg_len - 10, ".byml.yaml"))
		{
			char y_dest[PATH_MAX];
			if (opt_dest)
				SubstDest (y_dest, sizeof (y_dest), arg, opt_dest, 0, ".byml", false);
			else
			{
				snprintf (y_dest, sizeof (y_dest), "%s", arg);
				y_dest[arg_len - 5] = 0;
			}
			const enumError y_err = encode_byml_file (arg, y_dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BYML %s -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, y_dest);
			if (max_err < y_err)
				max_err = y_err;
			continue;
		}
		int src_len = strlen (arg);
		while (src_len > 0 && arg[src_len - 1] == '/')
			src_len--;
		((char *)arg)[src_len] = 0;
		char source_dir[PATH_MAX];
		snprintf (source_dir, sizeof (source_dir), "%s", arg);

		if (create && IsDirectory (source_dir, false))
		{
			const bool is_arch_dir = is_archive_dir_name (source_dir, src_len);
			const bool explicit_dest
				= (opt_dest && *opt_dest && strcmp (opt_dest, dest_fname) != 0);
			enumError tree_err = repack_tree_bottom_up (source_dir, 0);
			if (max_err < tree_err)
				max_err = tree_err;

			// Clean up all leftover .d folders and preview/companion artifacts from the tree
			cleanup_extracted_artifacts (source_dir, true);

			// If it's a general directory tree and no explicit --dest was given, tree repacking is
			// done.
			if (!is_arch_dir && !explicit_dest)
				continue;
		}

		SetupParam_t sp;
		InitializeSetupParam (&sp);
		ScanSetupParam (&sp, true, arg, SZS_SETUP_FILE, 0, true);

		char dest[PATH_MAX];
		SubstDest (dest, sizeof (dest), arg, opt_dest, dest_fname,
			GetExtFF (sp.fform_file, sp.fform_arch), false);

		// If no explicit dest, check if a sibling container exists or if source is a disc/ROM
		// directory
		if ((!opt_dest || !*opt_dest || !strcmp (opt_dest, dest_fname))
			&& IsDirectory (source_dir, false))
		{
			char raw_stem[PATH_MAX];
			snprintf (raw_stem, sizeof (raw_stem), "%.*s",
				src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d") ? (int)(src_len - 2)
																			: (int)src_len,
				source_dir);

			struct stat st_check;
			bool found_cand = false;

			// Check if raw_stem itself is already an existing target file
			if (stat (raw_stem, &st_check) == 0 && S_ISREG (st_check.st_mode))
			{
				snprintf (dest, sizeof (dest), "%s", raw_stem);
				found_cand = true;
			}
			else
			{
				for (int c = 0; cand_archive_exts[c]; c++)
				{
					char cand[PATH_MAX];
					snprintf (cand, sizeof (cand), "%s%s", raw_stem, cand_archive_exts[c]);
					if (stat (cand, &st_check) == 0 && S_ISREG (st_check.st_mode))
					{
						snprintf (dest, sizeof (dest), "%s", cand);
						found_cand = true;
						break;
					}
				}
			}
			if (!found_cand)
			{
				ccp stem_ext = strrchr (raw_stem, '.');
				if (stem_ext
					&& (!strcasecmp (stem_ext, ".wbfs") || !strcasecmp (stem_ext, ".iso")
						|| !strcasecmp (stem_ext, ".nds") || !strcasecmp (stem_ext, ".srl")
						|| !strcasecmp (stem_ext, ".dsi") || !strcasecmp (stem_ext, ".wad")
						|| !strcasecmp (stem_ext, ".wux")))
				{
					snprintf (dest, sizeof (dest), "%s", raw_stem);
				}
				else
				{
					char check_f[PATH_MAX];
					snprintf (check_f, sizeof (check_f), "%s/DATA/sys/main.dol", source_dir);
					if (stat (check_f, &st_check) == 0)
						snprintf (dest, sizeof (dest), "%s.wbfs", raw_stem);
					else
					{
						snprintf (check_f, sizeof (check_f), "%s/sys/main.dol", source_dir);
						if (stat (check_f, &st_check) == 0)
							snprintf (dest, sizeof (dest), "%s.wbfs", raw_stem);
						else
						{
							snprintf (check_f, sizeof (check_f), "%s/arm9.bin", source_dir);
							if (stat (check_f, &st_check) == 0)
								snprintf (dest, sizeof (dest), "%s.nds", raw_stem);
							else
							{
								// wit's XEXTRACT WAD layout: cert.bin/tik.bin/
								// tmd.bin/footer.bin plus "%08x.app" contents;
								// content IDs rarely start at 0 so don't rely
								// on 00000000.app alone.
								snprintf (check_f, sizeof (check_f), "%s/tmd.bin", source_dir);
								if (stat (check_f, &st_check) == 0)
									snprintf (dest, sizeof (dest), "%s.wad", raw_stem);
								else
								{
									snprintf (check_f, sizeof (check_f), "%s/cert.bin", source_dir);
									if (stat (check_f, &st_check) == 0)
										snprintf (dest, sizeof (dest), "%s.wad", raw_stem);
									else
									{
										// Switch NSP layout (hactool's -x --pfs0dir=
										// dump, second-pass-identified): exefs/romfs
										// plus control/control.nacp, which the
										// hacbrewpack repack branch requires. Check
										// this before the bare romfs/exefs check
										// below, since 3DS output never has a
										// "control" subdir.
										char control_nacp[PATH_MAX];
										snprintf (control_nacp, sizeof (control_nacp),
											"%s/control/control.nacp", source_dir);
										if (stat (control_nacp, &st_check) == 0)
											snprintf (dest, sizeof (dest), "%s.nsp", raw_stem);
										else
										{
											// ctrtool 3DS layout: --exefsdir/--romfsdir
											// trees directly under source_dir.
											char exefs_d[PATH_MAX], romfs_d[PATH_MAX];
											snprintf (
												exefs_d, sizeof (exefs_d), "%s/exefs", source_dir);
											snprintf (
												romfs_d, sizeof (romfs_d), "%s/romfs", source_dir);
											struct stat st_dir;
											if ((stat (exefs_d, &st_dir) == 0
													&& S_ISDIR (st_dir.st_mode))
												|| (stat (romfs_d, &st_dir) == 0
													&& S_ISDIR (st_dir.st_mode)))
												snprintf (dest, sizeof (dest), "%s.cia", raw_stem);
										}
									}
								}
							}
						}
					}
				}
			}
		}

		enumError pas_err = PassthruPack (source_dir, dest);
		if (create && pas_err != ERR_NOTHING_TO_DO)
		{
			if (max_err < pas_err)
				max_err = pas_err;
			ResetSetupParam (&sp);
			continue;
		}

		ccp ext = strrchr (dest, '.');
		const bool sarc_le = ext
			&& (!strcasecmp (ext, ".sarcle")
				|| !strcasecmp (ext, ".le") && strlen (dest) >= 8
					&& !strcasecmp (ext - 5, ".sarc.le"));
		const bool is_sarc = ext
			&& (!strcasecmp (ext, ".sarc") || !strcasecmp (ext, ".bfma")
				|| (strlen (dest) >= 10 && !strcasecmp (dest + strlen (dest) - 10, ".sarc.fzip"))
				|| sarc_le);
		if (create && is_sarc)
		{
			enumError err = create_sarc_dir (source_dir, dest, !sarc_le);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE %s SARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", sarc_le ? "little-endian" : "big-endian", source_dir,
					dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".narc"))
		{
			enumError err = create_narc_dir (source_dir, dest, true);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE NARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".darc"))
		{
			enumError err = create_darc_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE DARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".pac") || !strcasecmp (ext, ".pcs")))
		{
			enumError err = create_pac_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE PAC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		const bool is_warc = ext
			&& (!strcasecmp (ext, ".warc")
				|| (strlen (dest) >= 10 && !strcasecmp (dest + strlen (dest) - 10, ".warc.fzip")));
		if (create && is_warc)
		{
			enumError err = create_warc_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE WARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".bg4"))
		{
			enumError err = create_bg4_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BG4 %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".sa01"))
		{
			enumError err = create_sa01_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE SA01 %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".ca01"))
		{
			enumError err = create_ca01_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE CA01 %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".cram"))
		{
			enumError err = create_cram_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE CRAM %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".fsys"))
		{
			enumError err = create_fsys_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE FSYS %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".xpck") || !strcasecmp (ext, ".xc")))
		{
			enumError err = create_xpck_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE XPCK %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".ztab") || !strcasecmp (ext, ".tab")))
		{
			enumError err = create_ztab_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE ZTAB %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".mdr"))
		{
			enumError err = create_mdr_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE MDR %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".pvol"))
		{
			enumError err = create_pvol_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE PVOL %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".stpk") || !strcasecmp (ext, ".srd")))
		{
			enumError err = create_stpk_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE STPK %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".res") && looks_like_f9res_dir (source_dir))
		{
			enumError err = create_f9res_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE RES %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".zlarc"))
		{
			enumError err = create_zlarc_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE ZLARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".apak")))
		{
			enumError err = create_apak_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE APAK %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".nxarc")))
		{
			enumError err = create_nxarc_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE NXARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".pkz")))
		{
			enumError err = create_pkz_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE PKZ %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".tmpk") || !strcasecmp (ext, ".pack")))
		{
			enumError err = create_tmpk_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE TMPK %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".vibs")))
		{
			enumError err = create_vibs_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE VIBS %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".jarc"))
		{
			enumError err = create_jarc_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE JARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".mtxt"))
		{
			enumError err = create_mtxt_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE MTXT %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".mkgpdx"))
		{
			enumError err = create_mkgpdx_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE MKGPDX PAC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".gsh"))
		{
			enumError err = create_gsh_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE GSH %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".sze"))
		{
			enumError err = create_sze_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE SZE %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".dat") && looks_like_sfzdat_dir (source_dir))
		{
			enumError err = create_sfzdat_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE DAT %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".dat") || !strcasecmp (ext, ".rflres") || !strcasecmp (ext, ".rfl")
			|| !strcasecmp (ext, ".fflres") || !strcasecmp (ext, ".ffl")
			|| !strcasecmp (ext, ".cflres") || !strcasecmp (ext, ".cfl")
			|| !strcasecmp (ext, ".aflres") || !strcasecmp (ext, ".afl")
			|| !strcasecmp (ext, ".nflres") || !strcasecmp (ext, ".nfl") || !strcasecmp (ext, ".nfl_res"))
			&& looks_like_rflres_dir (source_dir))
		{
			const bool be = strcasecmp (ext, ".cfl") && strcasecmp (ext, ".cflres")
				&& strcasecmp (ext, ".afl") && strcasecmp (ext, ".aflres")
				&& strcasecmp (ext, ".nfl") && strcasecmp (ext, ".nflres") && strcasecmp (ext, ".nfl_res");
			enumError err = create_rflres_dir (source_dir, dest, be);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE MIIRES %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".gfa"))
		{
			enumError err = create_gfa_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE GFA %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".big"))
		{
			enumError err = create_bigf_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BIGF %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".arc") && looks_like_arcv_dir (source_dir))
		{
			enumError err = create_arcv_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE ARCV %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".rarc"))
		{
			enumError err = create_rarc_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE RARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".bcsar") || !strcasecmp (ext, ".csar")))
		{
			enumError err = create_sar_dir (source_dir, dest, "CSAR");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BCSAR %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".bfsar") || !strcasecmp (ext, ".fsar")))
		{
			enumError err = create_sar_dir (source_dir, dest, "FSAR");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BFSAR %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".bcwar") || !strcasecmp (ext, ".cwar")))
		{
			enumError err = create_sar_dir (source_dir, dest, "CWAR");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BCWAR %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".bfwar") || !strcasecmp (ext, ".fwar")))
		{
			enumError err = create_sar_dir (source_dir, dest, "FWAR");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BFWAR %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".bcgrp") || !strcasecmp (ext, ".cgrp")))
		{
			enumError err = create_sar_dir (source_dir, dest, "CGRP");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BCGRP %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".bfgrp") || !strcasecmp (ext, ".fgrp")))
		{
			enumError err = create_sar_dir (source_dir, dest, "FGRP");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BFGRP %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext
			&& (!strcasecmp (ext, ".car") || !strcasecmp (ext, ".res") || !strcasecmp (ext, ".trk")
				|| !strcasecmp (ext, ".lvl") || !strcasecmp (ext, ".rst")))
		{
			enumError err = create_rst_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE RST %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".byml") || !strcasecmp (ext, ".byaml")))
		{
			enumError err = encode_byml_file (arg, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BYML %s -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".ctpk"))
		{
			enumError err = create_ctpk_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE CTPK %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".ccf"))
		{
			enumError err = create_ccf_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE CCF %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".nccarc"))
		{
			enumError err = create_nccarc_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE NCCARC %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext
			&& (!strcasecmp (ext, ".at7") || !strcasecmp (ext, ".at7p")
				|| !strcasecmp (ext, ".at7x")))
		{
			enumError err = create_at7_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE AT7 %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".idx"))
		{
			enumError err = create_hwl_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE HWL %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".bin"))
		{
			enumError err = create_mpbin_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE MPBIN %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext
			&& (!strcasecmp (ext, ".rseq") || !strcasecmp (ext, ".brseq")
				|| !strcasecmp (ext, ".cseq") || !strcasecmp (ext, ".bcseq")
				|| !strcasecmp (ext, ".fseq") || !strcasecmp (ext, ".bfseq")
				|| !strcasecmp (ext, ".sseq") || !strcasecmp (ext, ".bms")
				|| !strcasecmp (ext, ".bmc")))
		{
			enumError err = encode_sequence_file (arg, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE SEQ %s -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = CreateSZS (&szs, dest, arg, 0, &sp, 0, verbose > 0 ? UINT_MAX : 0, false);

		//--- create file

		if ((verbose >= 0 || testmode) && !szs.unchanged)
		{
			if (create)
				fprintf (stdlog, "%s%sCREATE %s/ -> %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, GetNameFF_SZS (&szs), dest);
			else
				fprintf (stdlog, "%s%sENCODE %s/\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg);
			fflush (stdlog);
		}
		// szs.unchanged: CreateSZS() already logged "(unchanged, skipped)"
		// itself and left 'dest' untouched -- must not fall into the
		// CreateFileOpt()+fwrite() below, which would truncate it to 0 bytes

		if (create && !szs.unchanged && err <= ERR_WARNING && err != ERR_NOT_EXISTS)
		{
			File_t F;
			CreateFileOpt (&F, true, dest, testmode, dest);
			if (F.f)
			{
				SetFileAttrib (&F.fatt, &szs.fatt, 0);
				const u8 *data = szs.cdata ? szs.cdata : szs.data;
				const size_t size = szs.cdata ? szs.csize : szs.size;
				const size_t wstat = fwrite (data, 1, size, F.f);
				if (wstat != size)
					err = FILEERROR1 (
						&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", size, dest);
			}
			ResetFile (&F, opt_preserve);
			LinkCacheSZS (&szs, dest);
		}

		if ((err <= ERR_WARNING || szs.unchanged) && src_len > 2
			&& !strcasecmp (source_dir + src_len - 2, ".d") && !testmode)
			remove_dir_recursive (source_dir);

		if (max_err < err)
			max_err = err;
		ResetSZS (&szs);
		ResetSetupParam (&sp);
	}

	ResetStringField (&plist);
	opt_auto_add = save_auto_add; // restore auto-add settings
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command update			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct update_param_t
{
	ccp source; // source path
	bool update_sub; // true: update sub archives
	bool encode; // true: encode files if possible
	int recurse_level; // current recurse level
	int indent; // indention of log messages
} update_param_t;

//-----------------------------------------------------------------------------

static enumError job_update (szs_file_t *szs, // valid szs file
	int recurse_level // recursion level
);

///////////////////////////////////////////////////////////////////////////////

static int update_func (struct szs_iterator_t *it, // iterator struct with all infos
	bool term // true: termination hint
)
{
	if (term)
		return 0;

	DASSERT (it);
	DASSERT (it->param);
	szs_file_t *szs = it->szs;
	DASSERT (szs);
	update_param_t *up = it->param;
	DASSERT (up);
	DASSERT (up->source);

	//--- ignore directories

	if (it->is_dir)
		return 0;

	//--- setup path

	ccp local_path = it->path;
	if (*local_path == '.' && local_path[1] == '/')
		local_path += 2;

	char path_buf[PATH_MAX];
	ccp path = PathCatPP (path_buf, sizeof (path_buf), up->source, local_path);

	u8 *data = szs->data + it->off;
	// [[analyse-magic]]
	file_format_t fform = GetByMagicFF (data, it->size, it->size);

	//--- [2do] encode files first

	//--- read updated file

	struct stat st;
	if (!stat (path, &st))
	{
		if (!S_ISREG (st.st_mode))
		{
			if (!opt_ignore)
				ERROR0 (ERR_WARNING, "No regular file: %s\n", path);
		}
		else if (st.st_size != it->size)
		{
			if (!opt_ignore)
				ERROR0 (ERR_WARNING, "File size mismatch: have %llu, need %u: %s\n",
					(u64)st.st_size, it->size, path);
		}
		else
		{
			if (verbose > 1)
				fprintf (stdlog, "%*s- LOAD %s\n", up->indent, "", path);
			LoadFILE (path, 0, 0, data, it->size, 0, 0, false);
		}
	}

	//--- update sub archives first

	if (up->update_sub && IsArchiveFF (fform))
	{
		szs_file_t subszs; // [[2do]] [[subfile]]
		InitializeSZS (&subszs);
		subszs.data = data;
		subszs.size = it->size;
		subszs.fname = path;
		subszs.fform_file = fform;
		subszs.fform_arch = fform;
		job_update (&subszs, up->recurse_level + 1);
		subszs.fname = EmptyString;
		ResetSZS (&subszs);
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError job_update (szs_file_t *szs, // valid szs file
	int recurse_level // recursion level
)
{
	DASSERT (szs);
	MEM_CHECK;
	PRINT ("JOB UPDATE: %d/%d %s\n", recurse_level, opt_recurse, szs->fname);

	char source[PATH_MAX];
	StringCat2S (source, sizeof (source), szs->fname, ".d");
	if (!recurse_level && (opt_source || !IsDirectory (source, false)))
	{
		ccp my_source = opt_source;
		if (!my_source)
		{
			switch (szs->fform_arch)
			{
				case FF_U8:
				case FF_WU8:
					// [[lta]] [[2do]]
					// [[lfl]] [[2do]]
				case FF_RARC:
				case FF_PACK:
				case FF_RKC:
					// case FF_BRRES:
					my_source = "\1P/\1N.d/";
					break;

				default:
					my_source = "\1P/\1F.d/";
					break;
			}
		}
		SubstDest (source, sizeof (source), szs->fname, my_source, 0, 0, true);
	}

	const int indent = 2 * recurse_level;
	if (verbose >= 0 && !recurse_level || verbose > 0 || testmode)
		fprintf (stdlog, "%s%*s- UPDATE %s:%s <- %s\n", verbose > 1 ? "\n" : "", indent, "",
			GetNameFF_SZS (szs), szs->fname, source);

	update_param_t up;
	memset (&up, 0, sizeof (up));
	up.source = source;
	up.encode = !opt_no_encode;
	up.recurse_level = recurse_level;
	up.update_sub = recurse_level < opt_recurse;
	up.indent = indent + 2;

	IterateFilesParSZS (szs, update_func, &up, false, false, false, 0, -1, SORT_NONE);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_update ()
{
	if (opt_source && !*opt_source)
		opt_source = 0;
	if (opt_dest && !*opt_dest)
		opt_dest = 0;
	opt_mkdir = true;

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, false);
		if (!err)
			err = job_update (&szs, 0);
		if (max_err < err)
			max_err = err;

		if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
		{
			char dest[PATH_MAX];
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, 0, false);

			if (verbose >= 0 || testmode)
			{
				fprintf (stdlog, "%s%sCREATE %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", GetNameFF_SZS (&szs), dest);
				fflush (stdlog);
			}

			File_t F;
			CreateFileOpt (&F, true, dest, testmode, arg);
			if (F.f)
			{
				if (IsCompressedFF (szs.fform_file))
					CompressSZS (&szs, 0, true);
				SetFileAttrib (&F.fatt, &szs.fatt, 0);
				const u8 *data = szs.cdata ? szs.cdata : szs.data;
				const size_t size = szs.cdata ? szs.csize : szs.size;
				const size_t wstat = fwrite (data, 1, size, F.f);
				if (wstat != size)
					err = FILEERROR1 (
						&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", size, dest);
			}
			ResetFile (&F, opt_preserve);
		}

		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command extract			///////////////
///////////////////////////////////////////////////////////////////////////////

// Rejects malformed UTF-8 (macOS's filesystem refuses to create a file
// whose name isn't valid UTF-8 -- EILSEQ). Seen on real container entry
// names (Excite Truck .car/.trk archives, a Kirby's Epic Yarn BRRES
// texture name) carrying a raw byte from some other 8-bit encoding
// (Shift-JIS/Windows-1252) instead. Not a security check like the rest of
// this function -- just filesystem compatibility -- but living here means
// every caller's existing "invalid name" fallback handles it for free.
// Strict per RFC 3629 -- a structural "looks like a 2/3/4-byte lead byte
// with the right continuation bytes" check alone isn't enough: 0xC0/0xC1
// are shaped exactly like valid 2-byte leads but are permanently reserved
// (overlong encodings of plain ASCII), and 0xED can lead a 3-byte sequence
// that encodes a UTF-16 surrogate half, also forbidden. Both slipped past
// the first version of this check and reached CreateFile() unsanitized
// (confirmed on a real Wii Fit BRRES sub-object name containing a literal
// 0xC0 0x8C), which macOS's filesystem then rejected with EILSEQ anyway --
// so the fallback this function feeds never actually fired for those.
static bool valid_utf8 (ccp s)
{
	const u8 *p = (const u8 *)s;
	while (*p)
	{
		const u8 c = *p;
		if (c < 0x80)
		{
			p++;
			continue;
		}
		int len;
		u8 lo2 = 0x80, hi2 = 0xbf;
		if (c >= 0xc2 && c <= 0xdf)
			len = 2;
		else if (c == 0xe0)
		{
			len = 3;
			lo2 = 0xa0;
		}
		else if (c >= 0xe1 && c <= 0xec)
			len = 3;
		else if (c == 0xed)
		{
			len = 3;
			hi2 = 0x9f;
		}
		else if (c >= 0xee && c <= 0xef)
			len = 3;
		else if (c == 0xf0)
		{
			len = 4;
			lo2 = 0x90;
		}
		else if (c >= 0xf1 && c <= 0xf3)
			len = 4;
		else if (c == 0xf4)
		{
			len = 4;
			hi2 = 0x8f;
		}
		else
			return false; // includes the reserved 0xC0/0xC1 leads
		if (p[1] < lo2 || p[1] > hi2)
			return false;
		for (int i = 2; i < len; i++)
			if ((p[i] & 0xc0) != 0x80)
				return false;
		p += len;
	}
	return true;
}

static bool valid_sarc_path (ccp path)
{
	return path && *path && path[0] != '/' && !strchr (path, '\\') && strncmp (path, "../", 3)
		&& strcmp (path, "..") && !strstr (path, "/../") && valid_utf8 (path);
}

// AT7 (Capcom MT Framework) stores each member under a single flat name that
// may use a backslash as a namespace separator (e.g. "Fade\fade_in",
// "effect\an\com01"). valid_sarc_path() rejects all backslashes, which is
// correct for the SARC family but wrongly flags every Capcom atlas. On both
// POSIX and Windows a lone backslash inside the name cannot escape the
// extraction root -- it is either a literal filename character (POSIX) or, at
// worst on Windows, a nested subdirectory still *within* dest. So we relax the
// backslash ban but keep every real traversal guard: no leading separator
// (absolute path), and no ".." component in either separator form, so a forged
// "..\..\evil" or "../evil" can never climb out of the output directory.
static bool valid_at7_path (ccp path)
{
	if (!path || !*path || path[0] == '/' || path[0] == '\\' || !valid_utf8 (path))
		return false;
	if (!strcmp (path, ".") || !strcmp (path, ".."))
		return false;
	if (strstr (path, "/../") || strstr (path, "\\..\\")
		|| strstr (path, "\\../") || strstr (path, "/..\\"))
		return false;
	if (!strncmp (path, "../", 3) || !strncmp (path, "..\\", 3))
		return false;
	return true;
}

// Forward declared so SARC/PAC/GFA extraction can recurse into their own
// output directory once all members are written -- without this, anything
// nested inside those container types (e.g. the .brres models inside a real
// Kirby's Epic Yarn disc's .gfa archives) was written to disk but never fed
// back through the XX pipeline, so it never got textures decoded or a DAE
// exported. Pass-through staging (wit/ndstool/etc.) already did this via
// extract_tree(); the three native container extractors below did not.
static enumError extract_tree (ccp root, uint depth);
static enumError extract_tree_complete (ccp root, uint depth);
static enumError decode_image_if_possible (ccp arg);
static void beside_source_dest (char *dest, uint dest_size, ccp arg);

// Disney/Pixar Toy Story 3 (Wii) stores virtually all of its game data in
// ordinary ZIP archives.  The game calls the streaming bundles ".tszip",
// but their on-disk representation is still a regular PKZIP central
// directory.  Keep this reader here instead of spawning an unzip command:
// XX must work on a complete disc image on installations which only have the
// SZS tools available, and extracted members need to re-enter XX's recursive
// decoder pipeline.
static inline uint zip_le16 (const u8 *p)
{
	return p[0] | (uint)p[1] << 8;
}

static inline u32 zip_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static enumError extract_zip_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".zip") && !is_ext (arg, ".tszip"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	// The end-of-central-directory record is followed only by its (at most
	// 64 KiB) comment.  Scanning backwards also permits self-extracting ZIPs.
	const size_t begin = raw_size > 0x10016 ? raw_size - 0x10016 : 0;
	const u8 *eocd = 0;
	for (size_t pos = raw_size >= 22 ? raw_size - 22 : 0; raw_size >= 22 && pos >= begin; pos--)
	{
		if (!memcmp (raw + pos, "PK\x05\x06", 4) && pos + 22 + zip_le16 (raw + pos + 20) == raw_size)
		{
			eocd = raw + pos;
			break;
		}
		if (!pos)
			break;
	}
	if (!eocd || zip_le16 (eocd + 4) || zip_le16 (eocd + 6)
		|| zip_le16 (eocd + 8) != zip_le16 (eocd + 10))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint n_entries = zip_le16 (eocd + 10);
	const u32 cd_size = zip_le32 (eocd + 12), cd_off = zip_le32 (eocd + 16);
	if (cd_off == 0xffffffff || cd_size == 0xffffffff || cd_off > raw_size
		|| cd_size > raw_size - cd_off)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO; // ZIP64 is deliberately not guessed at.
	}

	const u8 *cd = raw + cd_off, *cd_end = cd + cd_size;
	const u8 *entry = cd;
	for (uint i = 0; i < n_entries; i++)
	{
		if (entry > cd_end || (size_t)(cd_end - entry) < 46 || memcmp (entry, "PK\x01\x02", 4))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		const size_t entry_size = 46u + zip_le16 (entry + 28) + zip_le16 (entry + 30)
			+ zip_le16 (entry + 32);
		if (entry_size > (size_t)(cd_end - entry))
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		entry += entry_size;
	}
	if (entry != cd_end)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZIP:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	entry = cd;
	for (uint i = 0; !err && i < n_entries; i++)
	{
		const uint name_len = zip_le16 (entry + 28), extra_len = zip_le16 (entry + 30);
		const uint flags = zip_le16 (entry + 8), method = zip_le16 (entry + 10);
		const u32 compressed_size = zip_le32 (entry + 20), uncompressed_size = zip_le32 (entry + 24);
		const u32 local_off = zip_le32 (entry + 42);
		const char *name = (ccp)(entry + 46);

		// Directories are implied by file paths. Reject encrypted, ZIP64 and
		// unsupported-compression members rather than emitting corrupt bytes.
		if (name_len >= PATH_MAX || !name_len || name[name_len-1] == '/')
			goto next_zip_entry;
		char rel[PATH_MAX];
		memcpy (rel, name, name_len);
		rel[name_len] = 0;
		if (!valid_sarc_path (rel) || flags & 1 || local_off == 0xffffffff
			|| compressed_size == 0xffffffff || uncompressed_size == 0xffffffff
			|| (method != 0 && method != 8) || local_off > raw_size
			|| raw_size - local_off < 30 || memcmp (raw + local_off, "PK\x03\x04", 4))
			goto next_zip_entry;

		const uint local_name_len = zip_le16 (raw + local_off + 26);
		const uint local_extra_len = zip_le16 (raw + local_off + 28);
		const size_t data_off = (size_t)local_off + 30 + local_name_len + local_extra_len;
		if (data_off > raw_size || compressed_size > raw_size - data_off)
			goto next_zip_entry;

		if (!testmode)
		{
			u8 *out = 0;
			if (method == 0)
			{
				if (compressed_size != uncompressed_size)
					goto next_zip_entry;
				out = MALLOC (uncompressed_size ? uncompressed_size : 1);
				if (uncompressed_size)
					memcpy (out, raw + data_off, uncompressed_size);
			}
			else
			{
				out = MALLOC (uncompressed_size ? uncompressed_size : 1);
				z_stream zs = { 0 };
				zs.next_in = raw + data_off;
				zs.avail_in = compressed_size;
				zs.next_out = out;
				zs.avail_out = uncompressed_size;
				if (inflateInit2 (&zs, -MAX_WBITS) != Z_OK || inflate (&zs, Z_FINISH) != Z_STREAM_END
					|| zs.total_out != uncompressed_size || inflateEnd (&zs) != Z_OK)
				{
					FREE (out);
					goto next_zip_entry;
				}
			}

			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", rel);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && uncompressed_size && fwrite (out, 1, uncompressed_size, F.f) != uncompressed_size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", uncompressed_size, path);
			ResetFile (&F, opt_preserve);
			FREE (out);
		}

next_zip_entry:
		entry += 46u + name_len + extra_len + zip_le16 (entry + 32);
	}
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// True only while extract_tree_complete()'s own first pass (raw extraction,
// export_count temporarily zeroed) is running. HSF embeds and decodes its
// own textures as a side effect of the very same call that writes the model
// (see lib-hsf.c's DecodeHSF()), so converting a .hsf during this first pass
// -- before SetDAETextureSearchRoot() has indexed anything -- means every
// texture lookup misses and the model comes out untextured, even though the
// referenced PNG gets written to disk moments later by that same call.
// extract_hsf_file() checks this flag to defer to the second, properly-
// indexed pass (export_models_tree(), which calls it again once this flag
// is back off) instead of converting immediately. A plain single-file
// `wszst EXTRACT foo.hsf`/`XDECODE foo.hsf` never sets this flag at all
// (extract_one_file() is called directly, never through
// extract_tree_complete()), so that already-verified-working standalone
// path is unaffected.
static bool g_in_hsf_deferred_pass = false;

// SubstDest() with a NULL/empty 'dest' param just echoes 'arg' back
// unchanged -- it returns before ever touching the "\1P/\1N" pattern (see
// its own early-return). extract_tree()'s recursion into pass-through
// staged files deliberately passes opt_dest=0 ("each decoded file belongs
// beside its own source"), so every one of SARC/PAC/GFA's "\1P/\1N"
// SubstDest() calls degenerated to "extract into a directory with the
// exact same path as the source file itself" -- a guaranteed "Not a
// directory" write failure the moment the first member is written. Only
// caught because a real Kirby's Epic Yarn WBFS exercises this exact path
// (2329 of 2342 on-disc .gfa archives go through here); the opt_dest-set
// case this was originally tested with never triggers SubstDest()'s
// early-return, so it looked fine in isolation.
static void beside_source_dest (char *dest, uint dest_size, ccp arg)
{
	if (opt_dest)
		SubstDest (dest, dest_size, arg, opt_dest, "\1P/\1N", 0, false);
	else
		snprintf (dest, dest_size, "%s.d", arg);
}

// Like beside_source_dest(), but for formats that decode to a single sibling
// file (e.g. "foo.tex" -> "foo.png") rather than a member-extraction
// directory: swaps the source's own extension for 'new_ext' instead of
// appending ".d".
static void beside_source_dest_ext (char *dest, uint dest_size, ccp arg, ccp new_ext)
{
	if (opt_dest)
	{
		SubstDest (dest, dest_size, arg, opt_dest, "\1P/\1N", 0, false);
		return;
	}
	char base[PATH_MAX];
	StringCopyS (base, sizeof (base), arg);
	char *dot = strrchr (base, '.');
	char *slash = strrchr (base, '/');
	if (dot && (!slash || dot > slash))
		*dot = 0;
	snprintf (dest, dest_size, "%s%s", base, new_ext);
}

// Sonic and the Secret Rings / Sonic and the Black Knight use a small
// big-endian ONE archive around PRS-compressed assets.  It has no magic:
// header[1] is always the 0x10 entry-table offset and header[3] is 0 (Secret
// Rings) or -1 (Black Knight).  Keep detection deliberately structural so a
// random file named .one is not opened as an archive.
static bool decode_storybook_prs (u8 *out, uint out_size, const u8 *in, uint in_size)
{
	if (!out || !out_size || !in || !in_size)
		return false;

	uint ip = 0, op = 0;
	int n_bits = 9;
	u8 control = in[ip++];
	for (;;)
	{
		#define PRS_BIT() \
			( --n_bits == 0 ? ( ip >= in_size ? -1 : ( control = in[ip++], n_bits = 8, control & 1 ) ) \
				: control & 1 )
		int bit = PRS_BIT();
		if (bit < 0)
			return false;
		control >>= 1;
		if (bit)
		{
			if (ip >= in_size || op >= out_size)
				return false;
			out[op++] = in[ip++];
			continue;
		}

		bit = PRS_BIT();
		if (bit < 0)
			return false;
		control >>= 1;
		int offset, length;
		if (bit)
		{
			if (ip + 2 > in_size)
				return false;
			const uint word = in[ip] | in[ip + 1] << 8;
			ip += 2;
			if (!word)
				return op == out_size;
			length = word & 7;
			offset = (int)(word >> 3) - 0x2000;
			if (!length)
			{
				if (ip >= in_size)
					return false;
				length = in[ip++] + 1;
			}
			else
				length += 2;
		}
		else
		{
			int b0 = PRS_BIT();
			if (b0 < 0)
				return false;
			control >>= 1;
			int b1 = PRS_BIT();
			if (b1 < 0 || ip >= in_size)
				return false;
			control >>= 1;
			length = (b0 << 1 | b1) + 2;
			offset = (int)in[ip++] - 0x100;
		}
		if (offset >= 0 || (uint)-offset > op || length <= 0 || (uint)length > out_size - op)
			return false;
		while (length--)
			out[op] = out[op + offset], op++;
		#undef PRS_BIT
	}
}

static enumError extract_storybook_one_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".one"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (raw_size < 16 || raw_size > UINT_MAX)
		goto cleanup;
	const uint n_entries = be32 (raw), table = be32 (raw + 4), data = be32 (raw + 8), marker = be32 (raw + 12);
	if (!n_entries || n_entries > (raw_size - 16) / 48 || table != 16 || (marker != 0 && marker != UINT_MAX)
		|| data < table + n_entries * 48 || data > raw_size)
		goto cleanup;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT Sonic Storybook ONE:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_entries, dest);
	err = ERR_OK;
	for (uint i = 0; !err && i < n_entries; i++)
	{
		const u8 *entry = raw + table + i * 48;
		char name[33];
		memcpy (name, entry, 32);
		name[32] = 0;
		const uint offset = be32 (entry + 36), size = be32 (entry + 40), out_size = be32 (entry + 44);
		if (!valid_sarc_path (name) || !*name || offset < data || offset > raw_size || size > raw_size - offset
			|| !out_size || out_size > opt_max_file_size)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		if (testmode)
			continue;
		u8 *out = MALLOC (out_size);
		if (!decode_storybook_prs (out, out_size, raw + offset, size))
		{
			FREE (out);
			err = ERR_INVALID_DATA;
			break;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (out, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", out_size, path);
		ResetFile (&F, opt_preserve);
		FREE (out);
	}
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
cleanup:
	FREE (raw);
	return err;
}

// Ubisoft ABE BigFiles are used by Rabbids Go Home (Wii) for the game's
// actual data.  They are not the older BIG/BUG Jade archives: their index is
// a pair of linked tables and individual members may carry a 32-byte engine
// header (including an LZO1X block stream).  Keep this reader deliberately
// stream based -- RGH.BF alone is almost 1 GiB, so loading it through the SZS
// path would needlessly hit the generic maximum-file-size limit.
static u32 get_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static bool read_abe_at (FILE *f, u64 off, void *buf, size_t size)
{
	return off <= LONG_MAX && !fseek (f, (long)off, SEEK_SET) && fread (buf, 1, size, f) == size;
}

static bool valid_abe_name (ccp name)
{
	return name && *name && !strstr (name, "..") && !strchr (name, '/') && !strchr (name, '\\');
}

static enumError write_abe_member (FILE *in, u64 off, u32 size, ccp path, ccp source)
{
	File_t out;
	enumError err = CreateFileOpt (&out, true, path, false, source);
	if (err || !out.f)
		return err;
	u8 buf[0x10000];
	if (!read_abe_at (in, off, buf, 0))
		err = ERR_READ_FAILED;
	for (u32 left = size; !err && left;)
	{
		const size_t want = left < sizeof(buf) ? left : sizeof(buf);
		const size_t got = fread (buf, 1, want, in);
		if (got != want || fwrite (buf, 1, got, out.f) != got)
			err = ERR_WRITE_FAILED;
		left -= got;
	}
	ResetFile (&out, opt_preserve);
	return err;
}

// EA's BIGF archives use a big-endian, variable-length table.  Littlest Pet
// Shop (Wii) keeps all of its game assets in these .big files; the largest
// one is well above the normal in-memory archive limit, so keep extraction
// streamed just like the ABE reader below.  Each table record is
// { u32 offset, u32 size, NUL-terminated path }, and the header's final u32
// marks the first payload byte (and therefore the end of the table).
static enumError extract_bigf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 header[16];
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOT_EXISTS;
	if (!read_abe_at (in, 0, header, sizeof(header)) || memcmp (header, "BIGF", 4))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}

	if (fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_READ_FAILED;
	}
	const long end = ftell (in);
	const u64 file_size = end < 0 ? 0 : (u64)end;
	// BIGF's total-size field is the one little-endian field in this variant;
	// its member table remains big-endian.  (Littlest Pet Shop's data.big is
	// 947,545 bytes: header bytes 59 75 0e 00.)
	const u32 archive_size = get_le32(header + 4);
	const u32 n_entries = be32(header + 8);
	const u32 data_off = be32(header + 12);
	if (!file_size || archive_size != file_size || !n_entries || n_entries > 0x100000
		|| data_off < sizeof(header) || data_off > file_size)
	{
		fclose (in);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof(dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT EA BIGF:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	enumError err = ERR_OK;
	u64 table_pos = sizeof(header);
	for (u32 i = 0; !err && i < n_entries; i++)
	{
		u8 entry[8];
		if (table_pos > data_off - sizeof(entry) || !read_abe_at(in, table_pos, entry, sizeof(entry)))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		table_pos += sizeof(entry);
		const u64 member_off = be32(entry);
		const u32 member_size = be32(entry + 4);
		char name[PATH_MAX];
		uint name_len = 0;
		for (;;)
		{
			u8 ch;
			if (table_pos >= data_off || name_len + 1 >= sizeof(name)
				|| !read_abe_at(in, table_pos++, &ch, 1))
			{
				err = ERR_INVALID_DATA;
				break;
			}
			name[name_len++] = ch;
			if (!ch)
				break;
		}
		if (err)
			break;
		if (!valid_sarc_path(name) || member_off < data_off || member_off > file_size
			|| member_size > file_size - member_off
			|| (opt_max_file_size && member_size > opt_max_file_size))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf(path, sizeof(path), "%s/%s", dest, name);
			err = write_abe_member(in, member_off, member_size, path, arg);
		}
	}
	// Retail BIGFs terminate their variable records with the four-byte L234
	// marker; DATA_OFF points just past it rather than directly after the last
	// filename.
	if (!err && table_pos != data_off)
	{
		u8 marker[4];
		if (data_off - table_pos < sizeof(marker) || !read_abe_at(in, table_pos, marker, sizeof(marker))
			|| memcmp(marker, "L234", sizeof(marker)))
			err = ERR_INVALID_DATA;
		for (u64 pos = table_pos + sizeof(marker); !err && pos < data_off; pos++)
		{
			u8 pad;
			if (!read_abe_at(in, pos, &pad, 1) || pad)
				err = ERR_INVALID_DATA;
		}
	}
	fclose(in);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete(dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

// Littlest Pet Shop's BIGF members include engine RPK resource packs.  Their
// 0x15fb/STRM signature is not the unrelated Retro Studios RPAK format, and
// they have no decoder yet.  Mark them as opaque leaves so XX preserves the
// extracted, named packs instead of handing them to the generic SZS reader,
// which otherwise creates a bogus .d directory and reports invalid data.
static bool is_littlest_pet_shop_rpk (ccp arg)
{
	if (!is_ext(arg, ".rpk"))
		return false;
	u8 head[12];
	FILE *f = fopen(arg, "rb");
	const size_t got = f ? fread(head, 1, sizeof(head), f) : 0;
	if (f)
		fclose(f);
	return got == sizeof(head) && head[0] == 0x15 && head[1] == 0xfb
		&& !memcmp(head + 7, "STRM\0", 5);
}

static enumError extract_abe_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 header[0x40];
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOT_EXISTS;
	if (!read_abe_at (in, 0, header, sizeof(header)) || memcmp (header, "ABE\\0", 4)
		|| get_le32(header+4) != 4)
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}

	const u32 ntab = get_le32(header+0x0c);
	const u32 nfile = get_le32(header+0x2c);
	u64 tab = get_le32(header+0x18);
	if (!ntab || !nfile || ntab > 64 || !tab)
	{
		fclose (in);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof(dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ABE:%s (%u files) -> %s/\\n", verbose > 0 ? "\\n" : "",
			testmode ? "WOULD " : "", arg, nfile, dest);

	enumError err = ERR_OK;
	u32 emitted = 0;
	for (u32 ti = 0; !err && ti < ntab && emitted < nfile; ti++)
	{
		u8 th[12];
		if (!read_abe_at(in,tab,th,sizeof(th))) { err = ERR_INVALID_DATA; break; }
		const u32 count = get_le32(th);
		const u32 link = get_le32(th+4);
		const u32 next = get_le32(th+8);
		if (count > nfile - emitted || tab > UINT_MAX - 12ULL - (u64)count * 0x80)
			{ err = ERR_INVALID_DATA; break; }
		for (u32 i = 0; !err && i < count; i++, emitted++)
		{
			u8 entry[0x80], engine[0x20];
			if (!read_abe_at(in,tab+12ULL+(u64)i*0x80,entry,sizeof(entry)))
				{ err = ERR_INVALID_DATA; break; }
			char name[65];
			memcpy(name,entry,64); name[64] = 0;
			if (!valid_abe_name(name))
				{ err = ERR_INVALID_DATA; break; }
			const u64 data_off = get_le32(entry+0x68);
			char path[PATH_MAX];
			snprintf(path,sizeof(path),"%s/%s",dest,name);
			if (testmode)
				continue;
			if (!read_abe_at(in,data_off,engine,sizeof(engine)))
				{ err = ERR_INVALID_DATA; break; }
			const u32 packed = get_le32(engine);
			const u32 original = get_le32(engine+4);
			const u32 type = get_le32(engine+12);
			if (type == 0 && original && packed <= UINT_MAX - 32)
			{
				if (packed == original)
					err = write_abe_member(in,data_off+32,original,path,arg);
				else
				{
					u8 *src = MALLOC(packed), *dec = 0; uint dec_size = 0;
					if (!src || !read_abe_at(in,data_off+32,src,packed)) err = ERR_INVALID_DATA;
					else err = DecodeLZO1XGrow(&dec,&dec_size,src,packed);
					if (!err && dec_size != original) err = ERR_INVALID_DATA;
					if (!err) { File_t out; err=CreateFileOpt(&out,true,path,false,arg); if (out.f) { if (fwrite(dec,1,dec_size,out.f)!=dec_size) err=ERR_WRITE_FAILED; ResetFile(&out,opt_preserve); } }
					FREE(src); FREE(dec);
				}
			}
			else if (type == 2 && original)
				err = write_abe_member(in,data_off+32,original,path,arg);
			else if (!type && !original)
				err = ERR_OK;
			else if (type == 4 && original)
			{
				u8 count_buf[4];
				if (!read_abe_at(in,data_off+32,count_buf,sizeof(count_buf))) { err=ERR_INVALID_DATA; break; }
				const u32 chunks = get_le32(count_buf);
				if (!chunks || chunks > 0x10000) { err=ERR_INVALID_DATA; break; }
				u32 *sizes = MALLOC((size_t)chunks*sizeof(*sizes));
				if (!sizes || !read_abe_at(in,data_off+36,sizes,(size_t)chunks*sizeof(*sizes))) err=ERR_INVALID_DATA;
				File_t out = {0};
				if (!err) err=CreateFileOpt(&out,true,path,false,arg);
				u64 pos = data_off + 36ULL + (u64)chunks*4;
				u32 left = original;
				for (u32 j=0; !err && j<chunks && left; j++)
				{
					const u32 packed_size=get_le32((u8*)sizes+j*4);
					const u32 want=left < 0x3fffc ? left : 0x3fffc;
					if (!packed_size)
					{
						u8 *raw=MALLOC(want);
						if (!raw || !read_abe_at(in,pos,raw,want) || fwrite(raw,1,want,out.f)!=want)
							err=ERR_WRITE_FAILED;
						FREE(raw);
					}
					else
					{
						u8 *src=MALLOC(packed_size), *dec=0; uint dec_size=0;
						if (!src || !read_abe_at(in,pos,src,packed_size)) err=ERR_INVALID_DATA;
						else err=DecodeLZO1XGrow(&dec,&dec_size,src,packed_size);
						if (!err && (dec_size != want || fwrite(dec,1,dec_size,out.f)!=dec_size)) err=ERR_WRITE_FAILED;
						FREE(src); FREE(dec);
					}
					pos += packed_size ? packed_size : want;
					left -= want;
				}
				if (!err && left) err=ERR_INVALID_DATA;
				if (out.f) ResetFile(&out,opt_preserve);
				FREE(sizes);
			}
			else
				// Type 3 is a cross-BigFile shortcut. Keep its engine record so
				// callers can resolve it against the companion archive.
				err = write_abe_member(in,data_off,packed,path,arg);
		}
		if (link == 0 && next != UINT_MAX) tab = next;
		else if (ti + 1 < ntab) { err = ERR_INVALID_DATA; break; }
	}
	fclose(in);
	if (!err && emitted != nfile)
		err = ERR_INVALID_DATA;
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete(dest,depth+1);
		if (err < sub) err = sub;
	}
	return err;
}

static enumError extract_sarc_mem (ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size)
{
	if (!raw || raw_size < 0x20 || raw_size > UINT_MAX || memcmp (raw, "SARC", 4))
		return ERR_NOTHING_TO_DO;
	nintendo_sarc_t sarc;
	enumError err = ScanSARC (&sarc, raw, raw_size);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, sarc.n_entries, dest);
	for (uint i = 0; !err && i < sarc.n_entries; i++)
	{
		ccp name = 0;
		const u8 *data = 0;
		uint size = 0;
		err = GetSARCEntry (&sarc, i, &name, &data, &size);
		char auto_name[PATH_MAX];
		if (!err && (!name || !*name))
		{
			ccp ext = ".bin";
			if (size >= 4)
			{
				if (!memcmp (data, "CTPK", 4))
					ext = ".ctpk";
				else if (!memcmp (data, "CGFX", 4))
					ext = ".cgfx";
				else if (!memcmp (data, "CLYT", 4))
					ext = ".bclyt";
				else if (!memcmp (data, "CLAN", 4))
					ext = ".bclan";
				else if (!memcmp (data, "BCTR", 4))
					ext = ".bctr";
				else if (!memcmp (data, "BSEQ", 4))
					ext = ".bseq";
				else if (!memcmp (data, "DVLB", 4))
					ext = ".dvlb";
				else if (!memcmp (data, "SPBD", 4))
					ext = ".spbd";
				else if (size >= 0x28
					&& (!memcmp (data + size - 0x28, "CLIM", 4)
						|| !memcmp (data + size - 0x28, "FLIM", 4)))
					ext = ".bclim";
			}
			snprintf (auto_name, sizeof (auto_name), "file_%04u%s", i, ext);
			name = auto_name;
		}
		if (!valid_sarc_path (name))
		{
			// A named-but-unsafe entry (path traversal, or a name with
			// invalid UTF-8 bytes macOS's filesystem refuses -- see
			// valid_utf8()'s comment) used to abort every remaining entry
			// in the archive. Give it a synthetic name instead, same as
			// the "no name at all" case just above.
			snprintf (auto_name, sizeof (auto_name), "file_%04u.bin", i);
			name = auto_name;
		}
		if (testmode)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
	}
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_sarc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sarc") && !is_ext (arg, ".szs") && !is_ext (arg, ".lyarc")
		&& !is_ext (arg, ".arc") && !is_ext (arg, ".pack") && !is_ext (arg, ".bin")
		&& !is_ext (arg, ".fzip") && !is_ext (arg, ".bfma"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	if (raw_size >= 8 && !memcmp (raw, "FZIP", 4))
	{
		u8 *dec = 0;
		uint dec_sz = 0;
		if (DecodeFZIP (&dec, &dec_sz, raw, (uint)raw_size) == ERR_OK && dec)
		{
			FREE (raw);
			raw = dec;
			raw_size = dec_sz;
		}
	}
	err = extract_sarc_mem (arg, basedir, depth, raw, raw_size);
	FREE (raw);
	return err;
}

static enumError extract_ctpk_mem (ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size)
{
	if (!raw || raw_size < 0x20 || raw_size > UINT_MAX || memcmp (raw, "CTPK", 4))
		return ERR_NOTHING_TO_DO;
	nintendo_ctpk_t ctpk;
	enumError err = ScanCTPK (&ctpk, raw, raw_size);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CTPK:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, ctpk.n_entries, dest);

	for (uint i = 0; !err && i < ctpk.n_entries; i++)
	{
		nintendo_ctpk_entry_t entry;
		err = GetCTPKEntry (&ctpk, i, &entry);
		if (err || testmode)
			continue;

		u8 *rgba = 0;
		uint w = 0, h = 0;
		err = DecodeCTPKTexture_RGBA (&rgba, &w, &h, &entry);
		if (err)
			continue;

		char sub_name[PATH_MAX];
		if (entry.name[0])
		{
			ccp slash = strrchr (entry.name, '/');
			ccp bslash = strrchr (entry.name, '\\');
			ccp base = slash && bslash ? (slash > bslash ? slash + 1 : bslash + 1)
				: slash				   ? slash + 1
				: bslash			   ? bslash + 1
									   : entry.name;
			char clean_base[PATH_MAX];
			snprintf (clean_base, sizeof (clean_base), "%s", base);
			char *dot = strrchr (clean_base, '.');
			if (dot)
				*dot = 0;
			snprintf (sub_name, sizeof (sub_name), "%s.png", clean_base);
		}
		else
		{
			snprintf (sub_name, sizeof (sub_name), "tex_%03u.png", i);
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, basedir ? basedir : "", sub_name);

		Image_t img;
		InitializeIMG (&img);
		const uint xw = EXPAND8 (w), xh = EXPAND8 (h);
		u8 *padded = xw == w && xh == h ? rgba : CALLOC (1, xw * xh * 4);
		if (padded != rgba)
		{
			for (uint y = 0; y < h; y++)
				memcpy (padded + y * xw * 4, rgba + y * w * 4, w * 4);
			FREE (rgba);
		}
		img.data = padded;
		img.data_alloced = true;
		img.data_size = xw * xh * 4;
		img.width = w;
		img.xwidth = xw;
		img.height = h;
		img.xheight = xh;
		img.iform = img.info_iform = IMG_X_RGB;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &le_func;

		err = SavePNG (&img, false, 0, out_path, 0, 0, opt_overwrite > 0, 0);
		ResetIMG (&img);
	}
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_ctpk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ctpk") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	err = extract_ctpk_mem (arg, basedir, depth, raw, raw_size);
	FREE (raw);
	return err;
}

// Extract a Brawl PAC archive ("ARC\0"). Retail entries have no filename,
// only a numeric type/index/group, so they use a descriptive generated name.
// Archives made by CreatePAC may carry its path-safe basename extension in
// reserved header bytes; retaining it makes our create/extract cycle lossless.
static const char *pac_type_name (u16 type)
{
	switch (type)
	{
		case 1:
			return "MiscData";
		case 2:
			return "ModelData";
		case 3:
			return "TextureData";
		case 4:
			return "AnimationData";
		case 5:
			return "SceneData";
		case 6:
			return "Type6";
		case 7:
			return "GroupedArchive";
		case 8:
			return "EffectData";
		default:
			return "Unknown";
	}
}

static enumError extract_pac_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pac") && !is_ext (arg, ".pcs") && !is_ext (arg, ".arc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	if (raw_size < 4 || memcmp (raw, "ARC\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	pac_t pac;
	err = ScanPAC (&pac, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAC:%s (%u entries, name=%s) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, pac.n_entries, pac.name, dest);

	for (uint i = 0; !err && i < pac.n_entries; i++)
	{
		const pac_entry_t *e = pac.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		bool use_name = e->name[0] && strcmp (e->name, ".") && strcmp (e->name, "..");
		for (uint j = 0; use_name && j < pac.n_entries; j++)
			if (j != i && !strcmp (e->name, pac.entries[j].name))
				use_name = false;
		if (use_name)
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", e->name);
		else
			snprintf (path, sizeof (path), "%s/%s%04u_%02u.%s.bin", dest, basedir ? basedir : "",
				e->index, e->group_index, pac_type_name (e->type));
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetPAC (&pac);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Namco/Tose's ARCV archive, used by Pac-Man Party (Wii).  Despite the
// familiar .arc extension this is neither Nintendo U8 nor Brawl PAC.  The
// little-endian header is "ARCV", member count and total archive size,
// followed by count { offset, size, crc32 } records.  There are deliberately
// no member names in this variant, so retain the on-disk ordinal and infer a
// useful extension only when the embedded file has a known magic.
static ccp arcv_member_ext (const u8 *data, uint size)
{
	if (size < 4)
		return ".bin";
	if (!memcmp (data, "bres", 4))
		return ".brres";
	if (!memcmp (data, "RSEQ", 4))
		return ".rseq";
	if (!memcmp (data, "RSTM", 4))
		return ".rstm";
	if (!memcmp (data, "RWAV", 4))
		return ".rwav";
	if (!memcmp (data, "RARC", 4))
		return ".rarc";
	if (!memcmp (data, "ARC\\0", 4))
		return ".pac";
	return ".bin";
}

static enumError extract_arcv_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".arc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 12 || raw_size > UINT_MAX || memcmp (raw, "ARCV", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint n_entries = le32 (raw + 4);
	const uint archive_size = le32 (raw + 8);
	if (!n_entries || n_entries > (raw_size - 12) / 12 || archive_size != raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint table_end = 12 + n_entries * 12;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *entry = raw + 12 + i * 12;
		const uint off = le32 (entry);
		const uint size = le32 (entry + 4);
		if (off < table_end || off > raw_size || size > raw_size - off)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ARCV:%s (%u unnamed entries) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_entries, dest);

	for (uint i = 0; !err && i < n_entries; i++)
	{
		const u8 *entry = raw + 12 + i * 12;
		const uint off = le32 (entry);
		const uint size = le32 (entry + 4);
		const u8 *data = raw + off;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sfile_%04u%s", dest, basedir ? basedir : "", i,
			arcv_member_ext (data, size));
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && size && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
	}

	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Namco Museum Remix / Megamix (Wii) resources are commonly wrapped twice:
// an SSZL LZSS0 stream contains a VCRA archive.  The format is documented by
// aluigi's namco_museum.bms and, unlike the unrelated ARCV format above,
// VCRA retains a 56-byte name for each member.  Detect by magic rather than
// extension: the disc uses both .lzs files and extensionless resources.
static bool decode_namco_lzss0 (u8 *out, uint out_size, const u8 *in, uint in_size)
{
	u8 ring[4096];
	memset (ring, 0, sizeof (ring)); // LZSS0's init_chr is NUL, not space.
	uint r = 4096 - 18, ip = 0, op = 0;
	uint flags = 0;
	while (op < out_size)
	{
		if (!(flags & 0x100))
		{
			if (ip >= in_size)
				return false;
			flags = in[ip++] | 0xff00; // flags are consumed least-significant bit first.
		}
		if (flags & 1)
		{
			if (ip >= in_size)
				return false;
			out[op++] = ring[r] = in[ip++];
			r = (r + 1) & 0xfff;
		}
		else
		{
			if (ip + 1 >= in_size)
				return false;
			uint p = in[ip++];
			const uint b = in[ip++];
			p |= (b & 0xf0) << 4;
			uint n = (b & 0x0f) + 3;
			if (n > out_size - op)
				return false;
			while (n--)
			{
				out[op++] = ring[r] = ring[p++ & 0xfff];
				r = (r + 1) & 0xfff;
			}
		}
		flags >>= 1;
	}
	return true;
}

static enumError extract_namco_sszl_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || raw_size > UINT_MAX || memcmp (raw, "SSZL", 4)
		|| le32 (raw + 4) || le32 (raw + 8) > raw_size - 16 || !le32 (raw + 12))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint zsize = le32 (raw + 8), usize = le32 (raw + 12);
	u8 *decoded = MALLOC (usize);
	if (!decoded || !decode_namco_lzss0 (decoded, usize, raw + 16, zsize))
	{
		FREE (decoded);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SSZL:%s (%u -> %u bytes) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, zsize, usize, dest);
	enumError err = ERR_OK;
	if (!testmode)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", "payload.vcra");
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (decoded, 1, usize, F.f) != usize)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", usize, path);
		ResetFile (&F, opt_preserve);
	}
	FREE (decoded);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub) err = sub;
	}
	return err;
}

static enumError extract_namco_vcra_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || raw_size > UINT_MAX || memcmp (raw, "VCRA", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint n_entries = le32 (raw + 4), archive_size = le32 (raw + 8);
	const bool has_crc = le32 (raw + 12) != 0;
	const uint table = has_crc ? 12 : 64;
	const uint entry_size = has_crc ? 44 : 64;
	const uint name_size = has_crc ? 32 : 56;
	const uint name_offset = has_crc ? 12 : 8;
	if (!n_entries || n_entries > (raw_size - table) / entry_size || archive_size != raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint table_end = table + n_entries * entry_size;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *entry = raw + table + i * entry_size;
		const uint off = le32 (entry), size = le32 (entry + 4);
		if (off < table_end || off > raw_size || size > raw_size - off)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT VCRA:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);
	enumError err = ERR_OK;
	for (uint i = 0; !err && i < n_entries; i++)
	{
		const u8 *entry = raw + table + i * entry_size;
		char name[57];
		memcpy (name, entry + name_offset, name_size);
		name[name_size] = 0;
		if (!valid_sarc_path (name))
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		if (testmode)
			continue;
		const uint off = le32 (entry), size = le32 (entry + 4);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (raw + off, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
	}
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub) err = sub;
	}
	return err;
}

// Gorilla Games' ".pkg" archive (Bonsai Barber and presumably this studio's
// other WiiWare titles). No container magic -- detection is entirely
// structural inside ScanGPKG() (zlib-decompress + header/table sanity), so
// this only pre-filters by extension to avoid even trying on files that
// couldn't plausibly be one.
static enumError extract_gpkg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pkg"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	gpkg_t pkg;
	err = ScanGPKG (&pkg, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PKG:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pkg.n_entries, dest);

	for (uint i = 0; !err && i < pkg.n_entries; i++)
	{
		const gpkg_entry_t *e = pkg.entries + i;
		if (testmode || !e->data)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", e->name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetGPKG (&pkg);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Call of Duty: Black Ops / Modern Warfare 3 (Wii) sound.pak. The PAK0
// table has no filenames: each member is identified by a CRC and stored at
// DATASTART + OFFSET * MULTIPLIER. The payloads are raw Nintendo DSP audio.
static enumError extract_cod_pak0_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || raw_size < 20 || memcmp (raw, "PAK0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint n_entries = le32 (raw + 8);
	const uint multiplier = le32 (raw + 12);
	const uint data_start = le32 (raw + 16);
	if (!n_entries || !multiplier || n_entries > (raw_size - 20) / 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t table_end = 20 + (size_t)n_entries * 12;
	if (data_start < table_end || data_start > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Validate every member before creating any output. Apart from avoiding
	// partial extraction of corrupt archives, 64-bit arithmetic prevents a
	// crafted OFFSET * MULTIPLIER from wrapping back into the table.
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *entry = raw + 20 + i * 12;
		const uint64_t off = (uint64_t)le32 (entry + 4) * multiplier + data_start;
		const uint size = le32 (entry + 8);
		if (off > raw_size || size > raw_size - off)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT COD PAK0:%s (%u DSP entries) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_entries, dest);

	char stem[PATH_MAX];
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	snprintf (stem, sizeof (stem), "%s", base);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	if (!*stem)
		snprintf (stem, sizeof (stem), "sound");

	for (uint i = 0; !err && i < n_entries; i++)
	{
		const u8 *entry = raw + 20 + i * 12;
		const uint crc = le32 (entry);
		const uint64_t off = (uint64_t)le32 (entry + 4) * multiplier + data_start;
		const uint size = le32 (entry + 8);
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s_0x%08x.dsp", dest,
			basedir ? basedir : "", stem, crc);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && size && fwrite (raw + off, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
	}

	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// 2D Boy's "master.pak" (World of Goo). No filenames are stored in the
// format at all (only a 32-bit hash), so members are extracted under
// ordinal names -- same convention already used for FSYS archives whose
// entries are all literally named "(null)" (see extract_fsys_file() below).
static enumError extract_gpak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	gpak_t pak;
	err = ScanGPAK (&pak, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		const gpak_entry_t *e = pak.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sfile_%04u.bin", dest, basedir ? basedir : "", i);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetGPAK (&pak);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_bns_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bns"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	bns_t bns;
	err = ScanBNS (&bns, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BNS:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, bns.n_entries, dest);

	for (uint i = 0; !err && i < bns.n_entries; i++)
	{
		const bns_entry_t *e = bns.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sfile_%04u.bin", dest, basedir ? basedir : "", i);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetBNS (&bns);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_rpak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	rpak_t pak;
	err = ScanRPAK (&pak, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RPAK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		const rpak_entry_t *e = pak.entries + i;
		if (testmode)
			continue;

		// Real games embed non-ASCII "magic" bytes here as raw small integers
		// rather than a genuine 4-char tag (Disney Epic Mickey's own .pak
		// files, same Retro Studios engine as DKCR, have entries like this).
		// '/' (0x2F) is inside the naive printable-ASCII range but is a path
		// separator -- letting it through embeds an unintended subdirectory
		// in the destination path and corrupts extraction (confirmed: caused
		// a real "Can't create file ... Is a directory" failure on Epic
		// Mickey). Exclude it explicitly rather than only checking 0x20-0x7e.
		char ext[5];
		for (uint k = 0; k < 4; k++)
		{
			const u8 c = (u8)(e->magic >> (24 - 8 * k));
			ext[k] = c >= 0x20 && c < 0x7f && c != '/' ? (char)tolower (c) : '_';
		}
		ext[4] = 0;

		u8 *dec = 0;
		uint dec_size = 0;
		bool decompress_failed = false;
		if (e->compressed)
		{
			dec = DecompressRPAKEntry (e->data, e->size, &dec_size);
			decompress_failed = !dec;
		}
		const u8 *out_data = dec ? dec : e->data;
		const uint out_size = dec ? dec_size : e->size;

		// A block that fails to decompress (confirmed on a real Metroid Prime
		// 3 .pak: its CMPD blocks use some other, unidentified codec, not the
		// zlib DecompressRPAKEntry() expects -- the block header lies about
		// compression the same way as a genuinely zlib entry, but the payload
		// itself doesn't parse as zlib) is still raw, still-compressed data of
		// unknown real format -- writing it under the type-guessed extension
		// (e.g. ".cmdl") is actively wrong and dangerous: something downstream
		// keyed off that extension/magic can misidentify and mishandle it
		// (observed: routed to the Switch NCA tool 'hactool', which then fails
		// loudly on data that was never an NCA). Use a plain, inert extension
		// instead so nothing downstream tries to interpret the content.
		ccp out_ext = decompress_failed ? "bin" : ext;

		// (id_hi,id_lo) is not guaranteed unique across all .pak files -- a
		// real Disney Epic Mickey archive has 26 colliding pairs, all sharing
		// tiny non-ASCII "magic" values that fall back to the same "____"
		// extension. The entry index is always unique, so it's included
		// unconditionally rather than only as a collision fallback.
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%04u_%08x%08x.%s", dest, basedir ? basedir : "", i,
			e->id_hi, e->id_lo, out_ext);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && out_size && fwrite (out_data, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", out_size, path);
		ResetFile (&F, opt_preserve);
		FREE (dec);
	}

	ResetRPAK (&pak);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Extract Ganbarion JARC / jCMP archives (Wii Fit U, One Piece, etc.)
static enumError extract_jarc_file (ccp arg, ccp basedir, uint depth)
{
	bool is_jarc_ext = is_ext (arg, ".jarc") || is_ext (arg, ".jcmp");

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	bool is_jcmp_sig = !memcmp (raw, "jCMP", 4) || !memcmp (raw, "JCMP", 4);
	bool is_jarc_sig = !memcmp (raw, "jARC", 4) || !memcmp (raw, "JARC", 4);
	if (!is_jarc_ext && !is_jcmp_sig && !is_jarc_sig)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	jarc_t jarc;
	err = ScanJARC (&jarc, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT JARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, jarc.n_entries, dest);

	for (uint i = 0; !err && i < jarc.n_entries; i++)
	{
		const jarc_entry_t *e = jarc.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", e->name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && e->data && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetJARC (&jarc);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Extract Mistwalker's "foo.pk"/"foo.pkh" archive pair (The Last Story).
// Triggers on the ".pk" file; the sibling ".pkh" table (same basename, next
// to it) must exist or this isn't a real LSPK pair. A third sibling,
// ".pfs", carries filenames but is deliberately not read -- see the format
// comment on ScanLSPK() in lib-nintendo.h.
static enumError extract_lspk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pk"))
		return ERR_NOTHING_TO_DO;

	// Sibling-*input* lookup: must stay next to 'arg' on disk regardless of
	// --dest (which only ever renames the *output*) -- beside_source_dest()
	// and beside_source_dest_ext() are the wrong tool here since they defer
	// to opt_dest.
	char pkh_path[PATH_MAX];
	{
		char base[PATH_MAX];
		const uint len = snprintf (base, sizeof (base), "%s", arg);
		if (len >= sizeof (base))
			return ERR_NOTHING_TO_DO;
		char *dot = strrchr (base, '.');
		char *slash = strrchr (base, '/');
		if (dot && (!slash || dot > slash))
			*dot = 0;
		snprintf (pkh_path, sizeof (pkh_path), "%s.pkh", base);
	}
	struct stat st;
	if (stat (pkh_path, &st) != 0 || !S_ISREG (st.st_mode))
		return ERR_NOTHING_TO_DO;

	u8 *pk_raw = 0;
	size_t pk_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &pk_raw, &pk_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (pk_size > UINT_MAX)
	{
		FREE (pk_raw);
		return ERR_FILE_TOO_BIG;
	}

	u8 *pkh_raw = 0;
	size_t pkh_size = 0;
	err = LoadFileAlloc (pkh_path, 0, 0, &pkh_raw, &pkh_size, 0, 0, 0, false);
	if (err)
	{
		FREE (pk_raw);
		return ERR_NOTHING_TO_DO;
	}
	if (pkh_size > UINT_MAX)
	{
		FREE (pk_raw);
		FREE (pkh_raw);
		return ERR_FILE_TOO_BIG;
	}

	lspk_t pak;
	err = ScanLSPK (&pak, pkh_raw, pkh_size, pk_raw, pk_size);
	if (err)
	{
		FREE (pk_raw);
		FREE (pkh_raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT LSPK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		const lspk_entry_t *e = pak.entries + i;
		if (testmode)
			continue;

		u8 *dec = 0;
		uint dec_size = 0;
		const u8 *out_data = e->data;
		uint out_size = e->size;

		// Compression is per-archive in the real format, but detecting it
		// per-entry from the payload's own magic byte is equivalent and
		// avoids a separate whole-archive pre-scan: 0x10/0x11 is Nintendo
		// LZ10/LZ11 (this codebase's own decoder), 0x78 is a plain zlib
		// stream, anything else (including dec_size==0, i.e. "not
		// compressed" per the .pkh table) is stored as-is.
		if (e->dec_size && e->size >= 1)
		{
			if ((e->data[0] == 0x10 || e->data[0] == 0x11)
				&& DecodeLZ10LZ11 (&dec, &dec_size, e->data, e->size) == ERR_OK)
			{
				out_data = dec;
				out_size = dec_size;
			}
			else if (e->data[0] == 0x78)
			{
				u8 *zdec = 0;
				uint zdec_size = 0;
				if (DecodeZlibGrow (&zdec, &zdec_size, e->data, e->size) == ERR_OK)
				{
					dec = zdec;
					out_data = dec;
					out_size = zdec_size;
				}
			}
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%08x.bin", dest, basedir ? basedir : "", e->hash);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && out_size && fwrite (out_data, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", out_size, path);
		ResetFile (&F, opt_preserve);
		FREE (dec);
	}

	ResetLSPK (&pak);
	FREE (pk_raw);
	FREE (pkh_raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Last-resort fallback: scan an otherwise-unrecognized, sizable file for
// embedded self-describing NW4R RLYT/RLAN resources (see ScanEmbeddedNW4R()
// in lib-nintendo.h for why -- We Ski's DATA/files/SKI.DAT, a fully custom
// Namco engine format with no discoverable container of its own). Gated to
// files over 1MB (anything smaller is either already handled by a real
// extractor above, or too small to be worth a full-file byte scan) and
// requires at least one hit, so the common case (a file with no embedded
// NW4R resources at all) costs one full-file scan and nothing else.
static enumError extract_embedded_nw4r_file (ccp arg, ccp basedir, uint depth)
{
	struct stat st;
	if (stat (arg, &st) != 0 || st.st_size < 0x100000)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 2, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nw4r_embedded_t found;
	ScanEmbeddedNW4R (&found, raw, raw_size);
	if (!found.n_entries)
	{
		ResetEmbeddedNW4R (&found);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog,
			"%s%sEXTRACT embedded NW4R:%s (%u resources found in unrecognized data) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, found.n_entries, dest);

	for (uint i = 0; !err && i < found.n_entries; i++)
	{
		const nw4r_embedded_entry_t *e = found.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s_%04u.%s", dest, basedir ? basedir : "",
			e->is_brlan ? "rlan" : "rlyt", i, e->is_brlan ? "brlan" : "brlyt");
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetEmbeddedNW4R (&found);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Extract a Genius Sonority FSYS archive (Pokémon Colosseum/XD/Battle
// Revolution).  Battle Revolution v2 archives commonly give every member the
// literal name "(null)", so stable ordinal filenames are intentional; the
// recursive dispatcher identifies children by their actual magic.
static uint fsys_be32 (const u8 *p)
{
	return (uint)p[0] << 24 | (uint)p[1] << 16 | (uint)p[2] << 8 | p[3];
}

static bool decode_fsys_lzss (u8 *out, uint out_size, const u8 *in, uint in_size)
{
	if (in_size < 16 || memcmp (in, "LZSS", 4) || fsys_be32 (in + 4) != out_size
		|| fsys_be32 (in + 8) < 16 || fsys_be32 (in + 8) > in_size)
		return false;
	const u8 *ip = in + 16, *end = in + fsys_be32 (in + 8);
	u8 ring[4096];
	memset (ring, 0, sizeof (ring));
	uint op = 0, rp = 4096 - 18, flags = 0;
	while (op < out_size)
	{
		if (!(flags & 0x100))
		{
			if (ip >= end)
				return false;
			flags = 0xff00 | *ip++;
		}
		if (flags & 1)
		{
			if (ip >= end)
				return false;
			ring[rp] = out[op++] = *ip++;
			rp = (rp + 1) & 4095;
		}
		else
		{
			if (end - ip < 2)
				return false;
			uint pos = *ip++;
			const uint b = *ip++;
			pos |= (b & 0xf0) << 4;
			uint count = (b & 15) + 3;
			while (count-- && op < out_size)
			{
				ring[rp] = out[op++] = ring[pos];
				pos = (pos + 1) & 4095;
				rp = (rp + 1) & 4095;
			}
		}
		flags >>= 1;
	}
	return true;
}

static enumError extract_fsys_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x40 || memcmp (raw, "FSYS", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint n_files = fsys_be32 (raw + 12), table = fsys_be32 (raw + 24);
	if (n_files > 65536 || table > raw_size || raw_size - table < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint file_list = fsys_be32 (raw + table);
	if (file_list > raw_size || n_files > (raw_size - file_list) / 4)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT FSYS:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_files, dest);
	for (uint i = 0; !err && i < n_files; i++)
	{
		const uint ent = fsys_be32 (raw + file_list + i * 4);
		if (ent > raw_size || raw_size - ent < 40)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		const uint offset = fsys_be32 (raw + ent + 4), size = fsys_be32 (raw + ent + 8);
		const uint flags = fsys_be32 (raw + ent + 12), csize = fsys_be32 (raw + ent + 20);
		const uint stored = flags & 0x80000000 ? csize : size;
		if (offset > raw_size || stored > raw_size - offset || size > 0x40000000)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		if (testmode)
			continue;
		u8 *data = raw + offset;
		u8 *decoded = 0;
		if (flags & 0x80000000)
		{
			decoded = MALLOC (size);
			if (!decoded || !decode_fsys_lzss (decoded, size, data, stored))
			{
				FREE (decoded);
				err = ERR_INVALID_DATA;
				break;
			}
			data = decoded;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sfile_%04u.bin", dest, basedir ? basedir : "", i);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && size && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
		FREE (decoded);
	}
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

//-----------------------------------------------------------------------------
// Ports of aluigi's QuickBMS scripts for six flat Nintendo archives. All six
// scanners hand back a malloc-owned entry list (name + payload both owned,
// because several of them decompress their members), so they share one
// writer below.
//-----------------------------------------------------------------------------

static enumError write_owned_entries (ccp arg, ccp basedir, uint depth, ccp label,
	nintendo_sarc_entry_t *entries, uint n_entries)
{
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", label, arg, n_entries, dest);

	enumError err = ERR_OK;
	for (uint i = 0; !err && i < n_entries; i++)
	{
		// Same fallback the Arika loop uses: never abort a whole archive
		// over one unusable member name, substitute a synthetic one.
		ccp name = entries[i].name;
		char safe_name[32];
		if (!valid_sarc_path (name))
		{
			snprintf (safe_name, sizeof (safe_name), "%04u.bin", i);
			name = safe_name;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entries[i].size
			&& fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", entries[i].size, path);
		ResetFile (&F, opt_preserve);
	}
	ResetOwnedEntries (entries, n_entries);

	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

// Star Fox Zero DAT ("DAT\0", Wii U), ported from star_fox_zero_dat.bms.
static enumError extract_sfzdat_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || raw_size < 32 || memcmp (raw, "DAT\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanSFZDAT (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "DAT", entries, n_entries);
}

// BG4 ("BG4\0", Mario & Luigi: Paper Jam, 3DS), from mario_luigi_paper.bms.
static enumError extract_bg4_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || raw_size < 16 || memcmp (raw, "BG4\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanBG4 (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "BG4", entries, n_entries);
}

// Xenoblade Chronicles 3D "cram" .arc (3DS), from xenoblade_arc.bms.
static enumError extract_cram_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || raw_size < 16 || memcmp (raw, "cram", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanCramARC (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "cram", entries, n_entries);
}

// Mii Maker (Wii U) "SA01" and amiibo Settings (3DS) "CA01", from
// mii_maker.bms / amiibo.bms. Every accepted outer wrapper has to decode to
// an inner SA01/CA01 image, so the heuristic bare-size+zlib form can't fire
// on an unrelated zlib file.
static enumError extract_sa01_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || raw_size < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *inner = 0;
	uint inner_size = 0;
	const enumError derr = DecodeSA01Container (&inner, &inner_size, raw, (uint)raw_size);
	FREE (raw);
	if (derr || !inner)
	{
		FREE (inner);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanSA01 (&entries, &n_entries, inner, inner_size);
	const bool named = !memcmp (inner, "SA01", 4);
	FREE (inner);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, named ? "SA01" : "CA01", entries, n_entries);
}

// Hyrule Warriors Legends (3DS) .idx/.bin pair, from
// hyrule_warriors_legends.bms. Neither half has a magic, so this is gated on
// the ".idx" extension plus an existing sibling ".bin" -- and ScanHWLegends
// then rejects the pair outright unless every table record addresses a real,
// in-bounds region of that .bin. Visiting the .bin directly does nothing
// (its extension never matches), so members are never extracted twice.
static enumError extract_hwlegends_file (ccp arg, ccp basedir, uint depth)
{
	const uint len = (uint)strlen (arg);
	if (len < 5 || strcasecmp (arg + len - 4, ".idx"))
		return ERR_NOTHING_TO_DO;

	char bin_path[PATH_MAX];
	snprintf (bin_path, sizeof (bin_path), "%.*s%s", (int)(len - 4), arg,
		arg[len - 3] >= 'a' && arg[len - 3] <= 'z' ? ".bin" : ".BIN");

	u8 *idx = 0, *bin = 0;
	size_t idx_size = 0, bin_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &idx, &idx_size, 0, 0, 0, false) || !idx
		|| LoadFileAlloc (bin_path, 0, 0, &bin, &bin_size, 0, 0, 0, false) || !bin
		|| idx_size > UINT_MAX || bin_size > UINT_MAX)
	{
		FREE (idx);
		FREE (bin);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr
		= ScanHWLegends (&entries, &n_entries, idx, (uint)idx_size, bin, (uint)bin_size);
	FREE (idx);
	FREE (bin);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "HWL-idx", entries, n_entries);
}

// Metroid: Samus Returns (3DS), from metroid_sr_3ds.bms. Headerless and
// nameless: ScanMetroidSR carries the whole (deliberately strict)
// self-consistency test, and this is wired in last so any format with an
// actual signature gets first refusal.
static enumError extract_metroidsr_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX || raw_size < 24)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	const enumError serr = ScanMetroidSR (&entries, &n_entries, raw, (uint)raw_size);
	FREE (raw);
	if (serr || !n_entries)
		return ERR_NOTHING_TO_DO;
	return write_owned_entries (arg, basedir, depth, "MSR", entries, n_entries);
}

// Extract a Game & Wario WARC archive ("WARC" magic, Wii U). Unlike PAC,
// entries do carry real filenames (plus a single flat folder-path prefix
// baked in by ScanWARC), so this writes them out directly rather than
// synthesising a name.
static enumError extract_warc_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	if (raw_size >= 8 && !memcmp (raw, "FZIP", 4))
	{
		u8 *dec = 0;
		uint dec_sz = 0;
		if (DecodeFZIP (&dec, &dec_sz, raw, (uint)raw_size) == ERR_OK && dec)
		{
			FREE (raw);
			raw = dec;
			raw_size = dec_sz;
		}
	}
	if (raw_size < 4 || memcmp (raw, "WARC", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	warc_t warc;
	err = ScanWARC (&warc, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT WARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, warc.n_entries, dest);

	for (uint i = 0; !err && i < warc.n_entries; i++)
	{
		const warc_entry_t *e = warc.entries + i;
		if (!e->name || testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", e->name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetWARC (&warc);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Extract an NCCARC container (WarioWare: Touched!). Gated on both the
// ".nccarc"/".nccarc_c.bin" extension AND ScanNCCARC's structural checks --
// there's no magic to key off (see the long comment at ScanNCCARC's
// definition), so extension-gating keeps this from firing on unrelated
// files that might coincidentally look like a monotonic offset table.
// Decodes an N64 Virtual Console "romc"-compressed ROM to a plain .z64
// sibling file. Gated on the literal bare filename "romc" -- that's the
// real filename found inside a real retail Kirby 64 (USA) VC WAD's emulator
// app content (no extension, no magic to key off; see the long comment
// above DecodeRomC() in lib-vc.c for the full verification detail). A
// same-generation Yoshi's Story (USA) VC WAD stores its ROM under the name
// "rom" with NO compression at all, so that name is passed through
// untouched by this handler -- decompressing an already-raw ROM would be
// wrong, and there's nothing here to decode it as anyway (real N64 header
static enumError extract_ccf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ccf"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	ccf_t ccf;
	err = ScanCCF (&ccf, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CCF:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, ccf.n_entries, dest);

	for (uint i = 0; !err && i < ccf.n_entries; i++)
	{
		const ccf_entry_t *e = ccf.entries + i;
		if (testmode)
			continue;

		u8 *decomp = 0;
		uint decomp_size = 0;
		err = DecodeCCFEntry (&decomp, &decomp_size, e);
		if (err)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "",
			e->name[0] ? e->name : "file.bin");
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && decomp_size && fwrite (decomp, 1, decomp_size, F.f) != decomp_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", decomp_size, path);
		ResetFile (&F, opt_preserve);
		FREE (decomp);
	}

	ResetCCF (&ccf);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// magic 0x80371240, not romc's "size-in-4MB-units" header).
static enumError decode_romc_if_possible (ccp arg)
{
	const ccp base = strrchr (arg, '/');
	if (strcmp (base ? base + 1 : arg, "romc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	u8 *dest = 0;
	uint dest_size = 0;
	err = DecodeRomC (&dest, &dest_size, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO; // e.g. the undiscovered romchu/Huffman variant

	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s.z64", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE romc:%s -> Z64:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, path);
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && dest_size && fwrite (dest, 1, dest_size, F.f) != dest_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dest_size, path);
		ResetFile (&F, opt_preserve);
	}
	FREE (dest);
	return err;
}

// Extract a Monster Games .tex GX texture (Excite Truck / ExciteBots) to a
// sibling PNG. No stored pixel format -- ScanTEX() recovers it (and the
// dimensions) via mip-chain-consistency scoring; see lib-excite.c.
static enumError extract_tex_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".tex"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	excite_tex_t tex;
	err = ScanTEX (&tex, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TEX:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, tex.width, tex.height, dest);

	if (testmode)
		ResetExciteTEX (&tex);
	else
	{
		u8 *rgba = tex.rgba;
		tex.rgba = 0; // ownership transfers to SaveDecodedRGBAToPNG
		err = SaveDecodedRGBAToPNG (rgba, tex.width, tex.height, &le_func, dest, 0, opt_overwrite);
		ResetExciteTEX (&tex);
	}
	return err;
}

// Export each Latte program embedded in a shader-only Gfx2/GSH container to
// a semantic listing with a lossless raw section. Reassembling the RAW fields
// reproduces the complete program, including clauses and unknown opcodes/bits.
static enumError extract_gsh_shaders (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gsh"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	gtx_t gtx;
	err = ScanGTX (&gtx, raw, (uint)raw_size);
	if (err || !gtx.n_shaders)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	static const ccp names[] = { "vertex", "pixel", "geometry", "compute" };
	uint written = 0;
	for (uint i = 0; i < gtx.n_shaders; i++)
	{
		const gtx_shader_t *s = gtx.shaders + i;
		if (!s->program || !s->program->data_size)
			continue;
		char *text = 0;
		if (DisassembleLatteCF (&text, s->program->data, s->program->data_size))
			continue;
		// Refuse to emit a listing unless the bundled assembler proves that it
		// reproduces the complete program byte-for-byte.
		u8 *check = 0;
		uint check_size = 0;
		if (AssembleLatteCF (&check, &check_size, text) || check_size != s->program->data_size
			|| memcmp (check, s->program->data, check_size))
		{
			FREE (check);
			FREE (text);
			err = ERR_INVALID_DATA;
			continue;
		}
		FREE (check);
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.%s-%u.latte", arg, names[s->stage], i);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%sEXTRACT GSH:%s -> LATTE:%s\n", testmode ? "WOULD " : "", arg, dest);
		if (!testmode)
		{
			File_t F;
			InitializeFile (&F);
			enumError ferr = CreateFileOpt (&F, true, dest, false, arg);
			if (!ferr && F.f && fwrite (text, 1, strlen (text), F.f) != strlen (text))
				ferr = ERR_WRITE_FAILED;
			ResetFile (&F, false);
			if (ferr && err < ferr)
				err = ferr;
			else
				written++;
		}
		else
			written++;
		FREE (text);
	}
	ResetGTX (&gtx);
	FREE (raw);
	return written ? err : ERR_NOTHING_TO_DO;
}

static enumError extract_sze_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sze"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 32)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *dec = 0;
	uint dec_size = 0;
	err = DecodeSZE (&dec, &dec_size, raw, (uint)raw_size, 0);
	FREE (raw);
	if (err || !dec || !dec_size)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SZE:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		nintendo_sarc_t sarc;
		enumError serr = ScanSARC (&sarc, dec, dec_size);
		if (!serr && sarc.n_entries)
		{
			for (uint i = 0; i < sarc.n_entries; i++)
			{
				ccp name = 0;
				const u8 *edata = 0;
				uint esize = 0;
				if (GetSARCEntry (&sarc, i, &name, &edata, &esize) == ERR_OK)
				{
					char path[PATH_MAX];
					snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name ? name : "unknown.bin");
					File_t F;
					CreateFileOpt (&F, true, path, false, arg);
					if (F.f && esize)
						fwrite (edata, 1, esize, F.f);
					ResetFile (&F, opt_preserve);
				}
			}
		}
		else
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%sdecrypted.szs", dest, basedir ? basedir : "");
			File_t F;
			CreateFileOpt (&F, true, path, false, arg);
			if (F.f)
				fwrite (dec, 1, dec_size, F.f);
			ResetFile (&F, opt_preserve);
		}
	}

	FREE (dec);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

// Mii resource archives (RFL_Res.dat & co) store several of their 18
// categories as raw BTI textures (see mii_arc_names in lib-rflres.c: eye,
// eyebrow, face_tex, glass_tex, mole, mouth, mustache, nline_tex, cap_tex).
// The rest are proprietary GX-polygon model blobs with no known decoder, so
// extraction dumps every entry as a raw ".bin" for round-tripping regardless
// of category -- but a lone ".bin" is unusable for actually looking at a
// texture. Probe each entry with the existing BTI decoder and, only for the
// ones that are genuinely valid BTI images, additionally save a viewable
// ".png" companion beside the raw ".bin" (same append-style precedent as the
// KCL ".kcl.glb" export: the round-trip source stays untouched).
static void export_rflres_tex_png_if_possible (ccp bin_path)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (bin_path, 0, 0, &raw, &raw_size, 0, 0, 0, false) || raw_size < sizeof (bti_header_t))
	{
		FREE (raw);
		return;
	}

	if (IsValidBTI (raw, (uint)raw_size, 0, 0) >= VALID_ERROR)
	{
		FREE (raw);
		return;
	}

	Image_t img;
	InitializeIMG (&img);
	const enumError err = AssignIMG (&img, -1, raw, (uint)raw_size, 0, false, &be_func, bin_path);
	if (!err)
	{
		char png_path[PATH_MAX];
		const size_t len = strlen (bin_path);
		if (len > 4 && !strcasecmp (bin_path + len - 4, ".bin"))
			snprintf (png_path, sizeof (png_path), "%.*s.png", (int)(len - 4), bin_path);
		else
			snprintf (png_path, sizeof (png_path), "%s.png", bin_path);
		SaveIMG (&img, FF_PNG, 0, 0, png_path, true);
	}
	ResetIMG (&img);
	FREE (raw);
}

static enumError extract_rflres_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 8 || raw_size > 0x10000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanRFLRes (&entries, &n_entries, raw, (uint)raw_size);
	if (err || !entries || !n_entries)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MIIRES:%s -> %s/ (%u files)\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest, n_entries);

	if (!testmode)
	{
		for (uint i = 0; i < n_entries; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
			File_t F;
			CreateFileOpt (&F, true, path, false, arg);
			const bool wrote_file = F.f != 0;
			if (F.f && entries[i].size)
				fwrite (entries[i].data, 1, entries[i].size, F.f);
			ResetFile (&F, opt_preserve);
			if (wrote_file)
				export_rflres_tex_png_if_possible (path);
		}
	}

	ResetOwnedEntries (entries, n_entries);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}

static enumError extract_mii_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 40 || raw_size > 4096)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (!IsMiiData (raw, (uint)raw_size, arg))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (!opt_export_miis)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest_png[PATH_MAX];
	char dest_glb[PATH_MAX];
	beside_source_dest_ext (dest_png, sizeof (dest_png), arg, ".png");
	beside_source_dest_ext (dest_glb, sizeof (dest_glb), arg, ".glb");

	u8 *png_data = 0;
	uint png_size = 0;
	u8 *glb_data = 0;
	uint glb_size = 0;

	RenderMiiPNG (&png_data, &png_size, raw, (uint)raw_size, 512);
	RenderMiiGLB (&glb_data, &glb_size, raw, (uint)raw_size);

	if (!png_data && !glb_data)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MII:   %s -> %s%s%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, png_data ? dest_png : "", (png_data && glb_data) ? " + " : "", glb_data ? dest_glb : "");

	if (!testmode)
	{
		if (png_data)
		{
			File_t F;
			CreateFileOpt (&F, true, dest_png, false, arg);
			if (F.f && png_size)
				fwrite (png_data, 1, png_size, F.f);
			ResetFile (&F, opt_preserve);
		}
		if (glb_data)
		{
			File_t F;
			CreateFileOpt (&F, true, dest_glb, false, arg);
			if (F.f && glb_size)
				fwrite (glb_data, 1, glb_size, F.f);
			ResetFile (&F, opt_preserve);
		}
	}

	FREE (png_data);
	FREE (glb_data);
	FREE (raw);
	return ERR_OK;
}

// Extract a Monster Games .art/.img GUI image to a sibling PNG. Same GX
// pixel data as .tex but a single mip level and a zeroed footer (see
// ScanART), so dimensions/format come from tile-seam continuity alone.
// Colour+stencil pairs (see README) are detected and recombined into one
// proper RGBA image by ScanART itself, so this function always just writes
// out whatever it returns.
static enumError extract_art_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".art") && !is_ext (arg, ".img"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	excite_tex_t tex;
	err = ScanART (&tex, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ART:%s (%ux%u) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, tex.width, tex.height, dest);

	if (testmode)
		ResetExciteTEX (&tex);
	else
	{
		u8 *rgba = tex.rgba;
		tex.rgba = 0;
		err = SaveDecodedRGBAToPNG (rgba, tex.width, tex.height, &le_func, dest, 0, opt_overwrite);
		ResetExciteTEX (&tex);
	}
	return err;
}

// Extract a Monster Games PMsh collision resource to a sibling COLLADA .dae.
// PMsh has no four-byte magic, so this is gated on the ".msh" extension and
// DecodeExciteMSH's exact section-size and triangle-index validation.
static enumError extract_msh_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".msh"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MSH:%s -> GLB:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
		err = DecodeExciteMSH (raw, (uint)raw_size, dest);
	FREE (raw);
	return err;
}

// Extract a Monster Games "3LDN"/"2LDN" .mod model to a sibling GLB model file
static enumError extract_mod_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mod"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");

	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT MOD:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	err = DecodeExciteMOD (raw, (uint)raw_size, dest);
	FREE (raw);
	if (err)
		return err;

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT MOD:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}

static enumError extract_hsf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hsf"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 8 || memcmp (raw, "HSFV037", 7))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	if (g_in_hsf_deferred_pass)
	{
		FREE (raw);
		return ERR_OK;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");

	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT HSF:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	err = DecodeHSF (raw, (uint)raw_size, dest);
	FREE (raw);
	if (err)
		return err;

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT HSF:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}

static enumError extract_nud_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nud"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || (!IsNUD (raw, raw_size)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT NUD:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	model_t *model = ParseNUD (raw, raw_size);
	FREE (raw);
	if (!model)
		return ERR_INVALID_DATA;

	ExportModelToGLB (model, dest);
	FreeModel (model);

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT NUD:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}

static enumError extract_numshb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".numshb"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || (!IsSSBH (raw, raw_size)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT NUMSHB:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	model_t *model = ParseNUMSHB (raw, raw_size);
	FREE (raw);
	if (!model)
		return ERR_INVALID_DATA;

	ExportModelToGLB (model, dest);
	FreeModel (model);

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT NUMSHB:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}

static enumError extract_nut_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nut") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || (!IsNUT (raw, raw_size)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nut_t nut;
	err = ScanNUT (&nut, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NUT (Smash 4):%s (%u textures) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nut.n_textures, dest);

	for (uint i = 0; !err && i < nut.n_textures; i++)
	{
		if (testmode)
			continue;

		u8 *tex_data = 0;
		size_t tex_sz = 0;
		char ext[16] = ".bin";
		enumError terr = ExtractNUTTexture (&nut, i, &tex_data, &tex_sz, ext, sizeof (ext));
		if (terr || !tex_data || tex_sz == 0)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s%s", dest, basedir ? basedir : "", nut.textures[i].name, ext);

		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (tex_data, 1, tex_sz, F.f) != tex_sz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", tex_sz, path);
		ResetFile (&F, opt_preserve);
		FREE (tex_data);
	}

	ResetNUT (&nut);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_dtls_file (ccp arg, ccp basedir, uint depth)
{
	ccp fname = FindFilename (arg, 0);
	if (!fname || (!strstr (fname, "ls00") && !strstr (fname, "ls01") && !is_ext (arg, ".ls")))
		return ERR_NOTHING_TO_DO;

	u8 *ls_raw = 0;
	size_t ls_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &ls_raw, &ls_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dt_path[PATH_MAX];
	snprintf (dt_path, sizeof (dt_path), "%s", arg);
	char *ls_pos = strstr (dt_path, "ls00");
	if (ls_pos)
		memcpy (ls_pos, "dt00", 4);
	else
	{
		ls_pos = strstr (dt_path, "ls01");
		if (ls_pos)
			memcpy (ls_pos, "dt01", 4);
		else
		{
			char *dot = strrchr (dt_path, '.');
			if (dot)
				snprintf (dot, sizeof (dt_path) - (dot - dt_path), ".dt");
		}
	}

	u8 *dt_raw = 0;
	size_t dt_size = 0;
	LoadFileAlloc (dt_path, 0, 0, &dt_raw, &dt_size, 0, 0, 0, false);

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	err = ScanDTLS (&entries, &n_entries, ls_raw, (uint)ls_size, dt_raw, (uint)dt_size);
	if (err)
	{
		FREE (ls_raw);
		if (dt_raw) FREE (dt_raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DTLS:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	if (!testmode)
	{
		for (uint i = 0; i < n_entries; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
			File_t F;
			if (!CreateFileOpt (&F, true, path, false, arg) && F.f)
			{
				if (entries[i].size && entries[i].data)
					fwrite (entries[i].data, 1, entries[i].size, F.f);
				ResetFile (&F, opt_preserve);
			}
			FREE ((void *)entries[i].name);
		}
	}

	FREE (entries);
	FREE (ls_raw);
	if (dt_raw) FREE (dt_raw);
	return ERR_OK;
}

static enumError extract_nccarc_file (ccp arg, ccp basedir, uint depth)
{
	// Matches "foo.nccarc" (uncompressed) and "foo.nccarc_c.bin" (this
	// fork's own LZSS layer already decompressed it to .bin) but not the
	// still-compressed "foo.nccarc_c" sitting beside it.
	if (!is_ext (arg, ".nccarc") && !(is_ext (arg, ".bin") && strstr (arg, ".nccarc_c.bin")))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}

	nccarc_t nc;
	err = ScanNCCARC (&nc, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NCCARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, nc.n_entries, dest);

	for (uint i = 0; !err && i < nc.n_entries; i++)
	{
		const nccarc_entry_t *e = nc.entries + i;
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%04u%s.bin", dest, basedir ? basedir : "", i,
			e->flag ? "_f" : "");
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetNCCARC (&nc);
	FREE (raw);
	return err;
}

// Extract a Good-Feel archive (GFAC).  The whole payload is one GFCP-
// compressed blob; ScanGFA decompresses it and returns the member table.
// Entries with size 0 are directory markers: the reference tooling treats
// each as the parent directory for the entries that follow it.
static enumError extract_gfa_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gfa") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	if (raw_size < 4 || memcmp (raw, "GFAC", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	gfa_t gfa;
	err = ScanGFA (&gfa, raw, raw_size);
	FREE (raw);
	if (err)
		return err;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GFA:%s (%u entries, %s) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, gfa.n_entries, gfa.compression == 1 ? "BPE" : "LZ10",
			dest);

	ccp subdir = "";
	for (uint i = 0; !err && i < gfa.n_entries; i++)
	{
		const gfa_entry_t *e = gfa.entries + i;
		if (!e->size)
		{
			// directory marker
			subdir = e->name;
			continue;
		}
		char rel[PATH_MAX];
		if (subdir && *subdir)
			snprintf (rel, sizeof (rel), "%s/%s", subdir, e->name);
		else
			snprintf (rel, sizeof (rel), "%s", e->name);
		if (!valid_sarc_path (rel))
			// Same fallback as the SARC/RST/Arika loops: don't abort every
			// remaining entry over one unsafe/non-UTF8 name.
			snprintf (rel, sizeof (rel), "file_%04u.bin", i);
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", rel);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (gfa.blob + e->offset, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetGFA (&gfa);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_rst_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 head[64];
	size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 64 || (memcmp (head, "0TSR", 4) && memcmp (head, "RST0", 4)))
		return ERR_NOTHING_TO_DO;

	char toc_path[PATH_MAX];
	snprintf (toc_path, sizeof (toc_path), "%s", arg);
	char *dot = strrchr (toc_path, '.');
	if (dot)
		snprintf (dot, sizeof (toc_path) - (dot - toc_path), ".toc");
	else
		snprintf (toc_path + strlen (toc_path), sizeof (toc_path) - strlen (toc_path), ".toc");

	u8 *toc_data = 0;
	uint toc_size = 0;
	FILE *ftoc = fopen (toc_path, "rb");
	if (ftoc)
	{
		fseek (ftoc, 0, SEEK_END);
		toc_size = (uint)ftell (ftoc);
		fseek (ftoc, 0, SEEK_SET);
		toc_data = MALLOC (toc_size);
		if (fread (toc_data, 1, toc_size, ftoc) != toc_size)
		{
			FREE (toc_data);
			toc_data = 0;
			toc_size = 0;
		}
		fclose (ftoc);
	}

	// WiiWare bundles the otherwise sibling .toc files inside tocres.res.
	if (!toc_data)
	{
		char tocres_path[PATH_MAX];
		ccp slash = strrchr (arg, '/');
		snprintf (tocres_path, sizeof (tocres_path), "%.*stocres.res",
			slash ? (int)(slash - arg + 1) : 0, arg);
		u8 *index_data = 0;
		size_t index_size = 0;
		if (!LoadFileAlloc (tocres_path, 0, 0, &index_data, &index_size, 0, 0, 0, false))
		{
			nintendo_sarc_entry_t *toc_entries = 0;
			uint n_toc_entries = 0;
			if (!ExtractRST (&toc_entries, &n_toc_entries, index_data, (uint)index_size, 0, 0))
			{
				ccp base = slash ? slash + 1 : arg;
				char wanted[PATH_MAX];
				snprintf (wanted, sizeof (wanted), "%s", base);
				char *ext = strrchr (wanted, '.');
				if (ext)
					snprintf (ext, sizeof (wanted) - (ext - wanted), ".toc");
				for (uint i = 0; i < n_toc_entries; i++)
					if (!strcasecmp (toc_entries[i].name, wanted))
					{
						toc_size = toc_entries[i].size;
						toc_data = MALLOC (toc_size);
						memcpy (toc_data, toc_entries[i].data, toc_size);
						break;
					}
				for (uint i = 0; i < n_toc_entries; i++)
				{
					FREE ((void *)toc_entries[i].name);
					FREE ((void *)toc_entries[i].data);
				}
				FREE (toc_entries);
			}
			FREE (index_data);
		}
	}

	u8 *car_data = 0;
	size_t car_size_st = 0;
	enumError lerr = LoadFileAlloc (arg, 0, 0, &car_data, &car_size_st, 0, 0, 0, false);
	if (lerr || !car_data)
	{
		FREE (toc_data);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	enumError err
		= ExtractRST (&entries, &n_entries, car_data, (uint)car_size_st, toc_data, toc_size);
	FREE (car_data);
	FREE (toc_data);

	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RST:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	for (uint i = 0; !err && i < n_entries; i++)
	{
		// An unsafe/empty name (seen on real Excite Truck .car archives --
		// some TOC entries resolve to an empty string, likely internal/
		// placeholder table slots rather than real path-traversal attempts)
		// used to abort the *entire* archive's extraction on the first
		// occurrence, silently dropping every entry after it. Give it a
		// synthetic index-based name instead and keep going, same as the
		// RPAK collision fallback -- no data lost, and genuinely malicious
		// paths (../, absolute, embedded backslash) still can't escape
		// 'dest' since they still get this same fallback rather than being
		// used verbatim.
		ccp name = entries[i].name;
		char safe_name[32];
		if (!valid_sarc_path (name))
		{
			snprintf (safe_name, sizeof (safe_name), "%04u.bin", i);
			name = safe_name;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", entries[i].size, path);
		ResetFile (&F, opt_preserve);
	}

	for (uint i = 0; i < n_entries; i++)
	{
		FREE ((void *)entries[i].name);
		FREE ((void *)entries[i].data);
	}
	FREE (entries);

	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Arika archive pair: INFO.DAT (obfuscated directory) beside GAME.DAT (the
// payload). Neither file has a magic signature to key off, so detection is
// by filename only, same convention arika.bms itself uses -- this also
// naturally skips GAME.DAT when the tree walker visits it directly (its
// basename never matches "INFO.DAT").
static enumError extract_arika_file (ccp arg, ccp basedir, uint depth)
{
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strcasecmp (base, "INFO.DAT"))
		return ERR_NOTHING_TO_DO;

	u8 *info_data = 0;
	size_t info_size_st = 0;
	enumError lerr = LoadFileAlloc (arg, 0, 0, &info_data, &info_size_st, 0, 0, 0, false);
	if (lerr || !info_data || info_size_st < 0x30 || info_size_st > UINT_MAX)
	{
		FREE (info_data);
		return ERR_NOTHING_TO_DO;
	}

	// Sibling GAME.DAT, preserving this file's own case for the name.
	char game_path[PATH_MAX];
	snprintf (game_path, sizeof (game_path), "%.*s%s", (int)(base - arg), arg,
		base[0] >= 'a' && base[0] <= 'z' ? "game.dat" : "GAME.DAT");

	u8 *game_data = 0;
	size_t game_size_st = 0;
	lerr = LoadFileAlloc (game_path, 0, 0, &game_data, &game_size_st, 0, 0, 0, false);
	if (lerr || !game_data || game_size_st > UINT_MAX)
	{
		FREE (info_data);
		FREE (game_data);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	enumError err = ExtractArika (
		&entries, &n_entries, info_data, (uint)info_size_st, game_data, (uint)game_size_st);
	FREE (info_data);
	FREE (game_data);

	if (err || !n_entries)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT Arika:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	for (uint i = 0; !err && i < n_entries; i++)
	{
		// Same fallback as the RST loop above: don't abort the whole
		// archive over one unsafe/empty entry name, give it a synthetic
		// name instead.
		ccp name = entries[i].name;
		char safe_name[32];
		if (!valid_sarc_path (name))
		{
			snprintf (safe_name, sizeof (safe_name), "%04u.bin", i);
			name = safe_name;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entries[i].size
			&& fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", entries[i].size, path);
		ResetFile (&F, opt_preserve);
	}

	for (uint i = 0; i < n_entries; i++)
	{
		FREE ((void *)entries[i].name);
		FREE ((void *)entries[i].data);
	}
	FREE (entries);

	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_thp_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 head[48];
	size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 48 || memcmp (head, "THP\0", 4))
		return ERR_NOTHING_TO_DO;

	u8 *thp_data = 0;
	size_t thp_size_st = 0;
	enumError lerr = LoadFileAlloc (arg, 0, 0, &thp_data, &thp_size_st, 0, 0, 0, false);
	if (lerr || !thp_data || thp_size_st < 48)
	{
		FREE (thp_data);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n_entries = 0;
	enumError err = ExtractTHP (&entries, &n_entries, thp_data, (uint)thp_size_st);
	FREE (thp_data);
	if (err || !n_entries)
	{
		if (entries)
		{
			for (uint i = 0; i < n_entries; i++)
			{
				FREE ((void *)entries[i].name);
				FREE ((void *)entries[i].data);
			}
			FREE (entries);
		}
		return err ? err : ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT THP:%s (%u frames/audio) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	for (uint i = 0; !err && i < n_entries; i++)
	{
		if (!valid_sarc_path (entries[i].name))
		{
			err = ERROR0 (ERR_INVALID_DATA, "Unsafe THP entry path: %s\n", entries[i].name);
			break;
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
		if (testmode)
		{
			if (verbose >= 1)
				fprintf (stdlog, "  %s  %s\n", "WOULD WRITE", path);
			continue;
		}

		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", entries[i].size, path);
		ResetFile (&F, opt_preserve);
	}

	for (uint i = 0; i < n_entries; i++)
	{
		FREE ((void *)entries[i].name);
		FREE ((void *)entries[i].data);
	}
	FREE (entries);

	return err;
}

static enumError extract_narc_mem (ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size)
{
	if (!raw || raw_size < 16 || raw_size > UINT_MAX)
		return ERR_NOTHING_TO_DO;
	if (memcmp (raw, "NARC", 4) && memcmp (raw, "CRAN", 4))
		return ERR_NOTHING_TO_DO;

	narc_t narc;
	enumError err = ScanNARC (&narc, raw, raw_size);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, narc.n_entries, dest);

	for (uint i = 0; !err && i < narc.n_entries; i++)
	{
		const narc_entry_t *e = narc.entries + i;
		if (!e->size)
			continue;
		char auto_name[PATH_MAX];
		ccp name = e->name;
		if (!name || !*name)
		{
			ccp ext = ".bin";
			if (e->offset + e->size <= narc.fimg_size)
			{
				const u8 *sub_d = narc.fimg_data + e->offset;
				if (e->size >= 4)
				{
					if (!memcmp (sub_d, "BY", 2) || !memcmp (sub_d, "YB", 2))
						ext = ".byml";
					else if (!memcmp (sub_d, "CTPK", 4))
						ext = ".ctpk";
					else if (!memcmp (sub_d, "CGFX", 4))
						ext = ".cgfx";
					else if (!memcmp (sub_d, "CLYT", 4))
						ext = ".bclyt";
					else if (!memcmp (sub_d, "CLAN", 4))
						ext = ".bclan";
					else if (!memcmp (sub_d, "DVLB", 4))
						ext = ".dvlb";
					else if (!memcmp (sub_d, "BMD0", 4))
						ext = ".nsbmd";
					else if (!memcmp (sub_d, "BTX0", 4))
						ext = ".nsbtx";
					else if (e->size >= 0x28
						&& (!memcmp (sub_d + e->size - 0x28, "CLIM", 4)
							|| !memcmp (sub_d + e->size - 0x28, "FLIM", 4)))
						ext = ".bclim";
				}
			}
			snprintf (auto_name, sizeof (auto_name), "file_%04u%s", i, ext);
			name = auto_name;
		}

		if (!valid_sarc_path (name))
		{
			snprintf (auto_name, sizeof (auto_name), "file_%04u.bin", i);
			name = auto_name;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->offset + e->size <= narc.fimg_size
			&& fwrite (narc.fimg_data + e->offset, 1, e->size, F.f) != e->size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetNARC (&narc);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_narc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".narc") && !is_ext (arg, ".carc") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	err = extract_narc_mem (arg, basedir, depth, raw, raw_size);
	FREE (raw);
	return err;
}

// Extract a DARC archive ("darc" magic) -- the 3DS/NW4C counterpart of SARC,
// found bundling a game's layout family into one romfs file (see the format
// comment above darc_t in lib-nintendo.h). Unlike PAC/GFA's flat member
// list, DARC's tree can nest arbitrarily deep (parent-index/end-index per
// entry), so this walks it with an explicit directory stack rather than
// GFA's single "current subdir" variable -- a nested dir ending resets to
// whatever directory is now on top of the stack, not to the root.
static enumError extract_darc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".darc") && !is_ext (arg, ".arc") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	if (raw_size < 4 || memcmp (raw, "darc", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	darc_t darc;
	err = ScanDARC (&darc, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DARC:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, darc.n_entries, dest);

	// stack[k].path is the assembled relative path of the directory at
	// depth k (root = ""); stack[k].end is that directory's end-index.
	enum
	{
		MAX_DARC_DEPTH = 64
	};
	struct
	{
		char path[PATH_MAX];
		uint end;
	} stack[MAX_DARC_DEPTH];
	uint sp = 0;
	stack[0].path[0] = 0;
	stack[0].end = darc.n_entries;

	for (uint i = 1; !err && i < darc.n_entries; i++)
	{
		while (sp > 0 && i >= stack[sp].end)
			sp--;
		const darc_entry_t *e = darc.entries + i;

		if (e->is_dir)
		{
			// Entry 1 is commonly a "." alias of the root (per GBATEK); real
			// subfolders never have an empty or "." name.
			if (!*e->name || !strcmp (e->name, "."))
				continue;
			if (sp + 1 >= MAX_DARC_DEPTH || e->end_or_size > darc.n_entries)
			{
				err = ERROR0 (ERR_INVALID_DATA, "DARC nesting too deep or corrupt: %s\n", arg);
				break;
			}
			char rel[PATH_MAX];
			if (*stack[sp].path)
				snprintf (rel, sizeof (rel), "%s/%s", stack[sp].path, e->name);
			else
				snprintf (rel, sizeof (rel), "%s", e->name);
			if (!valid_sarc_path (rel))
			{
				err = ERROR0 (ERR_INVALID_DATA, "Unsafe DARC entry path: %s\n", rel);
				break;
			}
			sp++;
			snprintf (stack[sp].path, sizeof (stack[sp].path), "%s", rel);
			stack[sp].end = e->end_or_size;
			continue;
		}

		char rel[PATH_MAX];
		if (*stack[sp].path)
			snprintf (rel, sizeof (rel), "%s/%s", stack[sp].path, e->name);
		else
			snprintf (rel, sizeof (rel), "%s", e->name);
		if (!valid_sarc_path (rel))
		{
			err = ERROR0 (ERR_INVALID_DATA, "Unsafe DARC entry path: %s\n", rel);
			break;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", rel);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->end_or_size
			&& fwrite (raw + e->parent_or_offset, 1, e->end_or_size, F.f) != e->end_or_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->end_or_size, path);
		ResetFile (&F, opt_preserve);
	}

	ResetDARC (&darc);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Extract a Sound Archive file (.bcsar, .bfsar, .bcwar, .bfwar, .bcgrp, .bfgrp, .bin, .arc).
static enumError extract_sar_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bcsar") && !is_ext (arg, ".bfsar") && !is_ext (arg, ".bcwar")
		&& !is_ext (arg, ".bfwar") && !is_ext (arg, ".bcgrp") && !is_ext (arg, ".bfgrp")
		&& !is_ext (arg, ".csar") && !is_ext (arg, ".fsar") && !is_ext (arg, ".cwar")
		&& !is_ext (arg, ".fwar") && !is_ext (arg, ".cgrp") && !is_ext (arg, ".fgrp")
		&& !is_ext (arg, ".bin") && !is_ext (arg, ".arc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	if (raw_size < 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	sound_archive_t sar;
	err = ScanSoundArchive (&sar, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", sar.magic, arg, sar.n_entries, dest);

	enumError last_write_err = ERR_OK;
	for (uint i = 0; i < sar.n_entries; i++)
	{
		const sar_file_entry_t *e = sar.entries + i;
		if (!e->data || !e->size)
			continue;
		if (testmode)
			continue;

		char filename[PATH_MAX];
		if (e->name && *e->name)
		{
			char clean_name[128];
			size_t k = 0;
			for (size_t j = 0; e->name[j] && k + 1 < sizeof (clean_name); j++)
			{
				unsigned char c = (unsigned char)e->name[j];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
					|| c == '_' || c == '-' || c == '.')
					clean_name[k++] = (char)c;
				else
					clean_name[k++] = '_';
			}
			clean_name[k] = 0;
			if (k > 0)
				snprintf (filename, sizeof (filename), "%04u_%s%s", e->file_id, clean_name, e->ext);
			else
				snprintf (filename, sizeof (filename), "%04u%s", e->file_id, e->ext);
		}
		else
			snprintf (filename, sizeof (filename), "%04u%s", e->file_id, e->ext);

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", filename);
		File_t F;
		enumError ferr = CreateFileOpt (&F, true, path, false, arg);
		if (!ferr && F.f && e->size && fwrite (e->data, 1, e->size, F.f) != e->size)
			ferr
				= FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		ResetFile (&F, opt_preserve);
		if (ferr && !last_write_err)
			last_write_err = ferr;
	}
	err = last_write_err;

	ResetSoundArchive (&sar);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// AT7 container archive extraction (Pokémon Mystery Dungeon WiiWare data*.bin / data*.raw /
// data*.at7). Either uncompressed raw data or compressed (AT7P/AT7X/AT7E) stream. Table starts at
// offset 0 with 28-byte (0x1C) records: uint32_be file_offset, uint32_be file_size, char
// file_name[20]. Ends when file_offset == 0 or record matches namesEnd.
static enumError extract_at7_file (ccp arg, ccp basedir, uint depth)
{
	// Unlike every sibling extract_*_file(), AT7 has no fixed extension, so
	// this runs against every file reaching extract_one_file() -- including
	// multi-gigabyte pass-through sources (e.g. a Switch Program NCA). Cap
	// the load with LoadFileAlloc's own max_size so those are rejected
	// cheaply instead of fully read into memory first. The raw EFBIG errno
	// (27) must never be returned here: it aliases ERU_WARNING in this
	// file's own enumError enum, which silently aborts the whole XX
	// pipeline with no message -- confirmed live on Super Mario Odyssey's
	// 5.5 GB Program NCA, which never reached the hactool pass-through
	// because of this.
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, UINT_MAX, 2, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 28)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *decomp = 0;
	uint decomp_size = 0;
	if (!memcmp (raw, "AT7P", 4) || !memcmp (raw, "AT7X", 4))
	{
		err = DecodeAT7 (&decomp, &decomp_size, raw, (uint)raw_size);
		FREE (raw);
		if (err)
			return ERR_NOTHING_TO_DO;
		raw = decomp;
		raw_size = decomp_size;
		if (raw_size < 28)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}

	// Check if raw is an AT7 table of contents
	uint n_entries = 0;
	uint cur = 0;
	while (cur + 28 <= raw_size)
	{
		uint off = be32 (raw + cur);
		uint sz = be32 (raw + cur + 4);
		if (!off)
			break; // End of table
		if (off < 28 || (u64)off + sz > raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		// Check filename characters
		bool valid_name = false;
		for (uint k = 8; k < 28; k++)
		{
			u8 ch = raw[cur + k];
			if (!ch)
			{
				if (k > 8)
					valid_name = true;
				break;
			}
			if (ch < 0x20 || ch > 0x7E)
				break;
		}
		if (!valid_name)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		n_entries++;
		cur += 28;
	}

	if (!n_entries)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT AT7:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	cur = 0;
	for (uint i = 0; !err && i < n_entries; i++, cur += 28)
	{
		uint off = be32 (raw + cur);
		uint sz = be32 (raw + cur + 4);
		char name[24] = { 0 };
		memcpy (name, raw + cur + 8, 20);
		name[20] = 0;
		if (!valid_at7_path (name))
		{
			err = ERROR0 (ERR_INVALID_DATA, "Unsafe AT7 entry path: %s\n", name);
			break;
		}
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && sz && fwrite (raw + off, 1, sz, F.f) != sz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", sz, path);
		ResetFile (&F, opt_preserve);
	}

	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static void decompress_mpbin_lzss (u8 *out, uint out_size, const u8 *src, uint src_size)
{
	u8 window[1024] = { 0 };
	uint win_off = 958;
	uint dest_off = 0;
	uint src_off = 0;
	int code_word = 0;
	while (dest_off < out_size && src_off < src_size)
	{
		if ((code_word & 0x100) == 0)
		{
			if (src_off >= src_size)
				break;
			code_word = src[src_off++] | 0xFF00;
		}
		if (code_word & 1)
		{
			if (src_off >= src_size)
				break;
			u8 val = src[src_off++];
			out[dest_off++] = val;
			window[win_off] = val;
			win_off = (win_off + 1) % 1024;
		}
		else
		{
			if (src_off + 1 >= src_size)
				break;
			u8 b1 = src[src_off++];
			u8 b2 = src[src_off++];
			uint offset = ((b2 & 0xC0) << 2) | b1;
			uint copy_len = (b2 & 0x3F) + 3;
			for (uint i = 0; i < copy_len && dest_off < out_size; i++)
			{
				u8 val = window[(offset + i) % 1024];
				window[win_off] = val;
				win_off = (win_off + 1) % 1024;
				out[dest_off++] = val;
			}
		}
		code_word >>= 1;
	}
}

static void decompress_mpbin_slide (u8 *out, uint out_size, const u8 *src, uint src_size)
{
	uint dest_off = 0;
	uint src_off = 0;
	u32 code_word = 0;
	int bits_left = 0;
	while (dest_off < out_size && src_off < src_size)
	{
		if (bits_left == 0)
		{
			if (src_off + 4 > src_size)
				break;
			code_word = ((u32)src[src_off] << 24) | ((u32)src[src_off + 1] << 16)
				| ((u32)src[src_off + 2] << 8) | src[src_off + 3];
			src_off += 4;
			bits_left = 32;
		}
		if (code_word & 0x80000000)
		{
			if (src_off >= src_size)
				break;
			out[dest_off++] = src[src_off++];
		}
		else
		{
			if (src_off + 1 >= src_size)
				break;
			u8 b1 = src[src_off++];
			u8 b2 = src[src_off++];
			uint dist_back = (((b1 & 0x0F) << 8) | b2) + 1;
			uint copy_len = ((b1 & 0xF0) >> 4) + 2;
			if (copy_len == 2)
			{
				if (src_off >= src_size)
					break;
				copy_len = src[src_off++] + 18;
			}
			for (uint i = 0; i < copy_len && dest_off < out_size; i++)
			{
				u8 val = (dist_back > dest_off) ? 0 : out[dest_off - dist_back];
				out[dest_off++] = val;
			}
		}
		code_word <<= 1;
		bits_left--;
	}
}

static void decompress_mpbin_rle (u8 *out, uint out_size, const u8 *src, uint src_size)
{
	uint dest_off = 0;
	uint src_off = 0;
	while (dest_off < out_size && src_off < src_size)
	{
		u8 code_byte = src[src_off++];
		uint rep_len = code_byte & 0x7F;
		if (code_byte & 0x80)
		{
			for (uint i = 0; i < rep_len && dest_off < out_size && src_off < src_size; i++)
				out[dest_off++] = src[src_off++];
		}
		else
		{
			if (src_off >= src_size)
				break;
			u8 rep_byte = src[src_off++];
			for (uint i = 0; i < rep_len && dest_off < out_size; i++)
				out[dest_off++] = rep_byte;
		}
	}
}

static void decompress_mpbin_inflate (u8 *out, uint out_size, const u8 *src, uint comp_size)
{
	z_stream stream;
	memset (&stream, 0, sizeof (stream));
	stream.avail_in = comp_size;
	stream.next_in = (Bytef *)src;
	stream.avail_out = out_size;
	stream.next_out = (Bytef *)out;
	if (inflateInit2 (&stream, 15 + 32) == Z_OK)
	{
		inflate (&stream, Z_FINISH);
		inflateEnd (&stream);
	}
}

static enumError extract_mpbin_file (ccp arg, ccp basedir, uint depth)
{
	ccp ext = strrchr (arg, '.');
	if (!ext || strcasecmp (ext, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return err;
	if (raw_size < 8 || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u32 num_files = ((u32)raw[0] << 24) | ((u32)raw[1] << 16) | ((u32)raw[2] << 8) | raw[3];
	if (num_files < 1 || num_files > 4096 || 4 + 4 * num_files > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u32 first_off = ((u32)raw[4] << 24) | ((u32)raw[5] << 16) | ((u32)raw[6] << 8) | raw[7];
	if (first_off < 4 + 4 * num_files || first_off >= raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	for (uint i = 0; i < num_files; i++)
	{
		u32 off = ((u32)raw[4 + 4 * i] << 24) | ((u32)raw[4 + 4 * i + 1] << 16)
			| ((u32)raw[4 + 4 * i + 2] << 8) | raw[4 + 4 * i + 3];
		if (off + 8 > raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		u32 ctype = ((u32)raw[off + 4] << 24) | ((u32)raw[off + 5] << 16) | ((u32)raw[off + 6] << 8)
			| raw[off + 7];
		if (ctype != 0 && ctype != 1 && ctype != 2 && ctype != 3 && ctype != 4 && ctype != 5
			&& ctype != 7)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MPBIN:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, num_files, dest);

	for (uint i = 0; !err && i < num_files; i++)
	{
		u32 off = ((u32)raw[4 + 4 * i] << 24) | ((u32)raw[4 + 4 * i + 1] << 16)
			| ((u32)raw[4 + 4 * i + 2] << 8) | raw[4 + 4 * i + 3];
		u32 uncomp_size = ((u32)raw[off] << 24) | ((u32)raw[off + 1] << 16)
			| ((u32)raw[off + 2] << 8) | raw[off + 3];
		u32 ctype = ((u32)raw[off + 4] << 24) | ((u32)raw[off + 5] << 16) | ((u32)raw[off + 6] << 8)
			| raw[off + 7];

		if (uncomp_size == 0)
			continue;
		if (uncomp_size > 256 * 1024 * 1024)
		{
			err = ERR_FILE_TOO_BIG;
			break;
		}

		if (testmode)
			continue;

		u8 *dest_buf = CALLOC (1, uncomp_size + 16);
		if (!dest_buf)
		{
			err = ENOMEM;
			break;
		}

		switch (ctype)
		{
			case 0:
				if (off + 8 + uncomp_size <= raw_size)
					memcpy (dest_buf, raw + off + 8, uncomp_size);
				break;
			case 1:
				decompress_mpbin_lzss (
					dest_buf, uncomp_size, raw + off + 8, (uint)(raw_size - (off + 8)));
				break;
			case 2:
			case 3:
			case 4:
				if (off + 12 <= raw_size)
					decompress_mpbin_slide (
						dest_buf, uncomp_size, raw + off + 12, (uint)(raw_size - (off + 12)));
				break;
			case 5:
				decompress_mpbin_rle (
					dest_buf, uncomp_size, raw + off + 8, (uint)(raw_size - (off + 8)));
				break;
			case 7:
				if (off + 16 <= raw_size)
				{
					u32 comp_size = ((u32)raw[off + 12] << 24) | ((u32)raw[off + 13] << 16)
						| ((u32)raw[off + 14] << 8) | raw[off + 15];
					decompress_mpbin_inflate (dest_buf, uncomp_size, raw + off + 16, comp_size);
				}
				break;
			default:
				break;
		}

		ccp sub_ext = "dat";
		if (uncomp_size >= 7 && !memcmp (dest_buf, "HSFV037", 7))
			sub_ext = "hsf";
		else if (uncomp_size >= 16 && dest_buf[12] == 0 && dest_buf[13] == 0 && dest_buf[14] == 0
			&& dest_buf[15] == 20)
			sub_ext = "atb";
		else if (uncomp_size >= 4 && !memcmp (dest_buf, "ARC\0", 4))
			sub_ext = "pac";
		else if (uncomp_size >= 4 && !memcmp (dest_buf, "darc", 4))
			sub_ext = "darc";
		else if (uncomp_size >= 4 && !memcmp (dest_buf, "SARC", 4))
			sub_ext = "sarc";

		char path[PATH_MAX];
		snprintf (
			path, sizeof (path), "%s/%sfile%03u.%s", dest, basedir ? basedir : "", i, sub_ext);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (dest_buf, 1, uncomp_size, F.f) != uncomp_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", uncomp_size, path);
		ResetFile (&F, opt_preserve);
		FREE (dest_buf);
	}

	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

// Export the structural half of a Switch ("NX") BFRES container as XML.
// Wii U BFRES (ParseBFRES() in lib-bfres.c) is big-endian, version 3.x, and
// self-relative offsets; Switch reuses the "FRES" magic but is little-endian
// and every offset is *absolute* from the start of the file -- a completely
// different, undocumented-in-tree layout.
//
// This decode was originally reverse engineered by hand against exactly one
// sample (~/Downloads/Male.bfres, BFRES version 9) and shipped with known,
// documented gaps. Testing it against real Nintendo Switch retail data --
// Super Mario Odyssey's RomFS, ~3400 .bfres files inside ObjectData/*.szs --
// showed it was in fact broken on that data: every Odyssey .bfres is BFRES
// **version 8**, and the FMDL/FSHP/FMAT field layout genuinely differs
// between version 8 and version 9+ (not just the "is_v8" FSHP-array-vs-dict
// guess the old code made) -- names, shape names and material names all
// silently came back empty on real data.
//
// This decode was rewritten by porting the verified, from-source struct
// layouts of KillzXGaming's BfresLibrary (MIT, the library Switch Toolbox
// itself uses: https://github.com/KillzXGaming/BfresLibrary), specifically
// ModelParser.cs/ShapeParser.cs/MaterialParser.cs/VertexBufferParser.cs and
// ResFileParser.cs, rather than continuing to guess-and-check against raw
// bytes. Verified against 10 real files: ~/Downloads/Male.bfres (v9, the
// original sample -- name "TopL", 2 shapes, 2 materials, still correct) and
// 9 different Odyssey ObjectData/*.szs samples pulled from very different
// parts of the game (AirBubble, CityMan, CityWorldUnderground..., Fukuwarai...,
// LakeWorldHomeWater001, MarioKing2D, NokonokoNpc, SandWorldHomeMeganeStep002,
// SkyDark, Special2WorldLavaKeyMoveParts001 -- all v8): FMDL/FSHP/FMAT names
// now resolve correctly on every one of them (e.g. AirBubble.bfres ->
// FMDL "AirBubble", shape "AirBubble__LifeBubble_Mat", material
// "LifeBubble_Mat" -- previously all three came back empty).
//
// Root cause of the old bug: BfresLibrary's loader reads a *version*-gated
// header prologue before the FMDL/FSHP/FMAT/FVTX name field -- a plain
// 4-byte flags word for version>=9, but a 12-byte "header block" (offset
// u32 + size s64, both unused/legacy) for version<9. The old code's offsets
// were tuned to the 4-byte (v9) case, so on real v8 data every subsequent
// field -- name, shape/FVTX pointers, material pointers, attribute array --
// was read 8 bytes short of where it actually is. This rewrite computes the
// FMDL/FSHP/FMAT/FVTX field offsets from the file's own version field
// (main header +0x08, major = bits 16..31) instead of hardcoding one
// version's layout, so both the v8 (Odyssey/2017-era) and v9/v10 (later
// titles) shapes are handled the same way. See bfres_switch_hdr_extra()
// and the field-offset math in extract_bfres_switch_manifest() below for
// the exact per-version byte layout, which mirrors BfresLibrary's
// ModelParser.Read()/ShapeParser.Read()/MaterialParser.Load() sequential
// field order 1:1.
//
// What is still NOT done, and NOT claimed to be fixed: the actual
// vertex/index *data* location. BfresLibrary's ResFileParser reads a
// `BufferInfo` offset near the end of the main header, and that struct's
// own +0x08 field is documented (in BfresLibrary's C# source) to hold the
// absolute file offset of the shared buffer pool all per-shape index/
// vertex data lives in. An attempt was made to port that field read here
// too (mirroring ResFileParser.Load()'s exact field sequence up to
// `BufferInfo`), but it did NOT check out against real data: on
// AirBubble.bfres the computed struct address landed in a run of zero
// bytes, not a plausible `unk/Size/BufferOffset` triple. Either an earlier
// field in that same sequence (the anim-dict block, MemoryPool, or the
// version>=9 32-byte reserved block) has a wrong size for real retail
// files, or BfresLibrary's C# `ResFileParser` takes a conditional branch
// (e.g. the version>=10 external-flags/external-GPU handling seen in its
// source) that shifts things around in a way not accounted for here. Per
// this fork's "don't ship an unverified guess" rule, that attempt was
// pulled rather than shipped as a buffer-pool-base="..." field that looked
// plausible but wasn't checked. Locating the buffer pool -- and then the
// much larger job of unpacking per-attribute component formats
// (GX2AttribFormat-style float16/snorm8/unorm10_10_10_2/etc, a table this
// fork doesn't have yet) and walking mesh index buffers/submeshes/LODs --
// remains a real, open gap; see PLAN.md.
static const char *rel_bfres_switch_string (const u8 *d, size_t size, s64 off)
{
	if (off < 2 || (size_t)off + 2 > size)
		return NULL;
	const uint len = le16 (d + off);
	if ((size_t)off + 2 + len > size)
		return NULL;
	return (const char *)(d + off + 2);
}

// Bytes consumed by the version-gated prologue that precedes the first
// LoadString()'d name field in FMDL/FSHP/FMAT/FVTX sections: a plain u32
// flags word for version>=9, or a legacy 12-byte "header block" (u32
// offset + s64 size, both unused) for version<9. See ModelParser.Read()
// et al in BfresLibrary -- every one of these section readers starts with
// this same version check before the first LoadString().
static inline uint bfres_switch_hdr_extra (uint vmajor)
{
	return vmajor >= 9 ? 4 : 12;
}

static enumError extract_sequence_file (ccp arg, ccp basedir, uint depth)
{
	ccp ext = strrchr (arg, '.');
	if (!ext)
		return ERR_NOTHING_TO_DO;

	if (strcasecmp (ext, ".rseq") && strcasecmp (ext, ".brseq") && strcasecmp (ext, ".cseq")
		&& strcasecmp (ext, ".bcseq") && strcasecmp (ext, ".fseq") && strcasecmp (ext, ".bfseq")
		&& strcasecmp (ext, ".sseq") && strcasecmp (ext, ".bms") && strcasecmp (ext, ".bmc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 4)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	seq_format_t fmt = DetectSequenceFormat (raw, raw_size);
	if (fmt == SEQ_FMT_UNKNOWN)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest_txt[PATH_MAX], dest_mid[PATH_MAX];
	if (opt_dest)
	{
		bool is_dir = (opt_dest[strlen (opt_dest) - 1] == '/') || IsDirectory (opt_dest, 0);
		if (is_dir)
		{
			SubstDest (dest_txt, sizeof (dest_txt), arg, opt_dest, "%N.txt", ".txt", false);
			SubstDest (dest_mid, sizeof (dest_mid), arg, opt_dest, "%N.mid", ".mid", false);
		}
		else
		{
			SubstDest (dest_txt, sizeof (dest_txt), arg, opt_dest, 0, ".txt", false);
			size_t tlen = strlen (dest_txt);
			if (tlen > 4 && !strcasecmp (dest_txt + tlen - 4, ".txt"))
				snprintf (dest_mid, sizeof (dest_mid), "%.*s.mid", (int)(tlen - 4), dest_txt);
			else if (tlen > 4 && !strcasecmp (dest_txt + tlen - 4, ".mid"))
			{
				snprintf (dest_mid, sizeof (dest_mid), "%s", dest_txt);
				snprintf (dest_txt, sizeof (dest_txt), "%.*s.txt", (int)(tlen - 4), dest_mid);
			}
			else
				snprintf (dest_mid, sizeof (dest_mid), "%s.mid", dest_txt);
		}
	}
	else
	{
		snprintf (dest_txt, sizeof (dest_txt), "%s.txt", arg);
		snprintf (dest_mid, sizeof (dest_mid), "%s.mid", arg);
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s -> %s / %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetSequenceFormatName (fmt), arg, dest_txt, dest_mid);

	if (!testmode)
	{
		char *text = 0;
		size_t text_size = 0;
		if (DisassembleSequence (&text, &text_size, raw, raw_size) == ERR_OK && text)
		{
			File_t F;
			CreateFileOpt (&F, true, dest_txt, false, arg);
			if (F.f && fwrite (text, 1, text_size, F.f) != text_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", text_size, dest_txt);
			ResetFile (&F, opt_preserve);
			FREE (text);
		}

		u8 *midi = 0;
		size_t midi_size = 0;
		if (SequenceToMIDI (&midi, &midi_size, raw, raw_size) == ERR_OK && midi)
		{
			File_t F;
			CreateFileOpt (&F, true, dest_mid, false, arg);
			if (F.f && fwrite (midi, 1, midi_size, F.f) != midi_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", midi_size, dest_mid);
			ResetFile (&F, opt_preserve);
			FREE (midi);
		}
	}

	FREE (raw);
	return err;
}

static enumError extract_ue4_pak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pak") && !is_ext (arg, ".bin") && !is_ext (arg, ".ue4"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsUE4Pak (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	ue4_pak_t pak;
	err = ScanUE4Pak (&pak, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT UE4-PAK (Mario & Luigi: Brothership):%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		if (testmode)
			continue;

		u8 *entry_data = 0;
		size_t entry_size = 0;
		enumError eerr = ExtractUE4PakEntry (&pak, i, &entry_data, &entry_size);
		if (eerr)
			continue;

		const char *fname = pak.entries[i].filename;
		while (*fname == '/' || *fname == '\\' || !strncmp (fname, "../", 3) || !strncmp (fname, "..\\", 3))
		{
			if (*fname == '/' || *fname == '\\')
				fname++;
			else
				fname += 3;
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", fname);

		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entry_size > 0 && fwrite (entry_data, 1, entry_size, F.f) != entry_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", entry_size, path);
		ResetFile (&F, opt_preserve);
		FREE (entry_data);
	}

	ResetUE4Pak (&pak);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}



static enumError extract_smash_arc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".arc") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsSmashArc (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	smash_arc_t arc;
	err = ScanSmashArc (&arc, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT Smash Ultimate data.arc:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, arc.n_files, dest);

	for (uint i = 0; !err && i < arc.n_files; i++)
	{
		if (testmode)
			continue;

		u8 *file_data = 0;
		size_t file_sz = 0;
		enumError aerr = ExtractSmashArcEntry (&arc, i, &file_data, &file_sz);
		if (aerr || !file_data)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", arc.files[i].filename);

		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && file_sz > 0 && fwrite (file_data, 1, file_sz, F.f) != file_sz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", file_sz, path);
		ResetFile (&F, opt_preserve);
		FREE (file_data);
	}

	ResetSmashArc (&arc);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_prc_manifest (ccp arg)
{
	if (!is_ext (arg, ".prc") && !is_ext (arg, ".param"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsPRC (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char *xml = 0;
	err = DecodePRC_XML (&xml, raw, raw_size);
	FREE (raw);
	if (err || !xml)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PRC XML:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (xml);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (xml, 1, strlen (xml), F.f) != strlen (xml))
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (xml);
	return err;
}

static enumError extract_cnut_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".cnut") && !is_ext (arg, ".nut") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsCNUT (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	cnut_t cnut;
	err = ScanCNUT (&cnut, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *disasm = 0;
	size_t disasm_len = 0;
	err = DisassembleCNUT (&cnut, &disasm, &disasm_len);
	if (err || !disasm)
	{
		ResetCNUT (&cnut);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".nut", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.nut", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CNUT (Wii Party):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (disasm);
		ResetCNUT (&cnut);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (disasm, 1, disasm_len, F.f) != disasm_len)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing disassembly failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (disasm);

	// Also extract strings table if there are string literals
	char *strings_txt = 0;
	size_t str_len = 0;
	if (ExtractCNUTStrings (&cnut, &strings_txt, &str_len) == ERR_OK && strings_txt && str_len > 0)
	{
		char str_dest[PATH_MAX];
		snprintf (str_dest, sizeof (str_dest), "%s", dest);
		char *ext = strrchr (str_dest, '.');
		if (ext && !strcmp (ext, ".nut"))
			snprintf (ext, sizeof (str_dest) - (ext - str_dest), ".txt");
		else
			snprintf (str_dest + strlen (str_dest), sizeof (str_dest) - strlen (str_dest), ".txt");

		File_t F_str;
		if (CreateFileOpt (&F_str, true, str_dest, false, arg) == ERR_OK)
		{
			if (F_str.f)
				fwrite (strings_txt, 1, str_len, F_str.f);
			ResetFile (&F_str, opt_preserve);
		}
		FREE (strings_txt);
	}

	ResetCNUT (&cnut);
	return err;
}

static enumError extract_xmsg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".xmsg") && !is_ext (arg, ".bin") && !is_ext (arg, ".xml"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsXMSG (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	xmsg_t xmsg;
	err = ScanXMSG (&xmsg, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *xml = 0;
	size_t xml_len = 0;
	err = ExtractXMSGXml (&xmsg, &xml, &xml_len);
	if (err || !xml)
	{
		ResetXMSG (&xmsg);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".xml", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT XMSG (Wii Party):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (xml);
		ResetXMSG (&xmsg);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (xml, 1, xml_len, F.f) != xml_len)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (xml);

	// Also extract human-readable text dump
	char *txt = 0;
	size_t txt_len = 0;
	if (ExtractXMSGText (&xmsg, &txt, &txt_len) == ERR_OK && txt && txt_len > 0)
	{
		char txt_dest[PATH_MAX];
		snprintf (txt_dest, sizeof (txt_dest), "%s", dest);
		char *ext = strrchr (txt_dest, '.');
		if (ext && !strcmp (ext, ".xml"))
			snprintf (ext, sizeof (txt_dest) - (ext - txt_dest), ".txt");
		else
			snprintf (txt_dest + strlen (txt_dest), sizeof (txt_dest) - strlen (txt_dest), ".txt");

		File_t F_txt;
		if (CreateFileOpt (&F_txt, true, txt_dest, false, arg) == ERR_OK)
		{
			if (F_txt.f)
				fwrite (txt, 1, txt_len, F_txt.f);
			ResetFile (&F_txt, opt_preserve);
		}
		FREE (txt);
	}

	ResetXMSG (&xmsg);
	return err;
}

static enumError extract_nwr_levelinfo_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsNWRLevelInfo (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nwr_levelinfo_t li;
	err = ScanNWRLevelInfo (&li, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *txt = 0;
	size_t txt_len = 0;
	err = DumpNWRLevelInfoText (&li, &txt, &txt_len);
	if (err || !txt)
	{
		ResetNWRLevelInfo (&li);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NWR-LEVELINFO (Newer SMBW):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (txt, 1, txt_len, F.f) != txt_len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing LevelInfo text failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (txt);
	ResetNWRLevelInfo (&li);
	return err;
}

static enumError extract_nwr_animtiles_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsNWRAnimTiles (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nwr_animtiles_t at;
	err = ScanNWRAnimTiles (&at, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *txt = 0;
	size_t txt_len = 0;
	err = DumpNWRAnimTilesText (&at, &txt, &txt_len);
	if (err || !txt)
	{
		ResetNWRAnimTiles (&at);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NWR-ANIMTILES (Newer SMBW):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (txt, 1, txt_len, F.f) != txt_len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing AnimTiles text failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (txt);
	ResetNWRAnimTiles (&at);
	return err;
}

static enumError extract_nsmbw_chk_file (ccp arg, ccp basedir, uint depth)
{
	if (!strstr (arg, "d_bgchk_") && !strstr (arg, "_chk.bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsNSMBWChk (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nsmbw_tileset_t ts;
	memset (&ts, 0, sizeof (ts));
	memcpy (ts.beh, raw, 2048);
	ts.have_beh = true;
	FREE (raw);

	char *txt = 0;
	size_t txt_len = 0;
	err = DumpNSMBWBehaviour (&ts, &txt, &txt_len, FindFilename (arg, 0));
	if (err || !txt)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NSMBW-CHK (Collision Behaviour):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (txt, 1, txt_len, F.f) != txt_len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing Behaviour text failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (txt);
	return err;
}

static enumError extract_nsmbw_tex_file (ccp arg, ccp basedir, uint depth)
{
	if (!strstr (arg, "_tex.bin") && !strstr (arg, "_tex.bin.LZ") && !strstr (arg, "_tex.bin.LH"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	u8 *decoded = NULL;
	uint decoded_size = 0;

	if (raw_size == 524288)
	{
		decoded = raw;
		decoded_size = (uint)raw_size;
		raw = NULL;
	}
	else if (raw_size > 4 && (raw[0] == 0x11 || raw[0] == 0x10 || raw[0] == 0x40))
	{
		if (raw[0] == 0x40)
			err = DecodeLZH8 (&decoded, &decoded_size, raw, raw_size);
		else
			err = DecodeLZ10LZ11 (&decoded, &decoded_size, raw, raw_size);
		FREE (raw);
		if (err || decoded_size != 524288 || !decoded)
		{
			FREE (decoded);
			return ERR_NOTHING_TO_DO;
		}
	}
	else
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *rgba = MALLOC (1024 * 256 * 4);
	if (!rgba)
	{
		FREE (decoded);
		return ERR_OUT_OF_MEMORY;
	}
	DecodeRGB4A3 (rgba, decoded, decoded_size);
	FREE (decoded);

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".png", 0, false);
	else
	{
		snprintf (dest, sizeof (dest), "%s", arg);
		char *ext = strstr (dest, "_tex.bin");
		if (ext)
			snprintf (ext, sizeof (dest) - (ext - dest), "_tex.png");
		else
			snprintf (dest + strlen (dest), sizeof (dest) - strlen (dest), ".png");
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NSMBW-TEX (1024x256 RGB5A3):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		Image_t img;
		InitializeIMG (&img);
		img.data = rgba;
		img.data_alloced = true;
		img.data_size = 1024 * 256 * 4;
		img.width = 1024;
		img.xwidth = 1024;
		img.height = 256;
		img.xheight = 256;
		img.iform = img.info_iform = IMG_X_RGB;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &le_func;

		err = SavePNG (&img, false, 0, dest, 0, 0, opt_overwrite > 0, 0);
		ResetIMG (&img);
	}
	else
	{
		FREE (rgba);
	}

	return err;
}

static enumError extract_kpbin_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsKPBin (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	kpbin_t kp;
	err = ScanKPBin (&kp, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *txt = 0;
	size_t txt_len = 0;
	err = DumpKPBinText (&kp, &txt, &txt_len);
	if (err || !txt)
	{
		ResetKPBin (&kp);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", 0, false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT KPBIN (Koopatlas Map):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (txt, 1, txt_len, F.f) != txt_len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing Koopatlas map text failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (txt);

	// Also write JSON representation
	char *json = 0;
	size_t json_len = 0;
	if (DumpKPBinJson (&kp, &json, &json_len) == ERR_OK && json)
	{
		char jdest[PATH_MAX];
		if (opt_dest && (strstr (opt_dest, ".json") || strstr (opt_dest, ".kpmap")))
		{
			SubstDest (jdest, sizeof (jdest), arg, opt_dest, 0, 0, false);
		}
		else
		{
			snprintf (jdest, sizeof (jdest), "%s", arg);
			char *dot = strrchr (jdest, '.');
			if (dot)
				snprintf (dot, sizeof (jdest) - (dot - jdest), ".json");
			else
				snprintf (jdest + strlen (jdest), sizeof (jdest) - strlen (jdest), ".json");
		}

		if (!testmode)
		{
			File_t Fj;
			if (CreateFileOpt (&Fj, true, jdest, false, arg) == ERR_OK)
			{
				if (Fj.f)
					fwrite (json, 1, json_len, Fj.f);
				ResetFile (&Fj, opt_preserve);
			}
		}
		FREE (json);
	}

	ResetKPBin (&kp);
	return err;
}

static enumError extract_chans_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsChannelScript (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	chans_script_t cs;
	err = ScanChannelScript (&cs, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;

	char *dump = 0;
	size_t dump_len = 0;
	err = DumpChannelScriptAll (&cs, &dump, &dump_len);
	if (err || !dump)
	{
		ResetChannelScript (&cs);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	char jdest[PATH_MAX];
	if (opt_dest)
	{
		bool is_dir = (opt_dest[strlen (opt_dest) - 1] == '/') || IsDirectory (opt_dest, 0);
		if (is_dir)
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, "%N.txt", ".txt", false);
			SubstDest (jdest, sizeof (jdest), arg, opt_dest, "%N.js", ".js", false);
		}
		else
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
			size_t dlen = strlen (dest);
			if (dlen > 4 && !strcasecmp (dest + dlen - 4, ".txt"))
				snprintf (jdest, sizeof (jdest), "%.*s.js", (int)(dlen - 4), dest);
			else if (dlen > 3 && !strcasecmp (dest + dlen - 3, ".js"))
			{
				snprintf (jdest, sizeof (jdest), "%s", dest);
				snprintf (dest, sizeof (dest), "%.*s.txt", (int)(dlen - 3), jdest);
			}
			else
				snprintf (jdest, sizeof (jdest), "%s.js", dest);
		}
	}
	else
	{
		snprintf (dest, sizeof (dest), "%s.txt", arg);
		snprintf (jdest, sizeof (jdest), "%s.js", arg);
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CHANS (ChannelScript):%s -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (dump, 1, dump_len, F.f) != dump_len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing ChannelScript dump failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (dump);

	// Also write decompiled script as .js
	char *js = 0;
	size_t js_len = 0;
	if (DumpChannelScriptDecompiled (&cs, &js, &js_len) == ERR_OK && js)
	{
		if (!testmode)
		{
			File_t Fj;
			if (CreateFileOpt (&Fj, true, jdest, false, arg) == ERR_OK)
			{
				if (Fj.f)
					fwrite (js, 1, js_len, Fj.f);
				ResetFile (&Fj, opt_preserve);
			}
		}
		FREE (js);
	}

	ResetChannelScript (&cs);
	return err;
}

static enumError extract_bfres_switch_manifest (ccp arg)
{
	if (!is_ext (arg, ".bfres") && !is_ext (arg, ".fres"))
		return ERR_NOTHING_TO_DO;

	u8 *d = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &d, &size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (size < 0xF0 || memcmp (d, "FRES", 4) || le16 (d + 0x0C) != 0xFEFF)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	const uint version = le32 (d + 0x08);
	const uint vmajor = (version >> 16) & 0xFFFF;

	const s64 fmdl_arr = (s64)le64 (d + 0x28);
	// numModel sits right before the model dict-values are read; its
	// absolute offset depends on the same version-gated 32-byte reserved
	// block as everything else (see ResFileParser.Load()).
	const uint n_fmdl_off = vmajor >= 9 ? 0xDC : 0xBC;
	uint n_fmdl = (size_t)n_fmdl_off + 2 <= size ? le16 (d + n_fmdl_off) : 0;
	if (!n_fmdl && fmdl_arr > 0 && (size_t)fmdl_arr + 4 <= size
		&& !memcmp (d + fmdl_arr, "FMDL", 4))
		n_fmdl = 1; // model-count field missing/zero but a real FMDL is there -- be permissive
	if (!n_fmdl || fmdl_arr <= 0 || (size_t)fmdl_arr + 0x78 > size
		|| memcmp (d + fmdl_arr, "FMDL", 4))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BFRES(Switch) structure:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (testmode)
	{
		FREE (d);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (!F.f)
	{
		FREE (d);
		return err;
	}

	const s64 fmdl = fmdl_arr;
	const uint fhdr = bfres_switch_hdr_extra (vmajor); // FMDL prologue size
	s64 p = fmdl + 4 + fhdr;
	const s64 name_field = p;
	p += 8;
	/* path_field */ p += 8;
	/* skel_field */ p += 8;
	/* vtxarr_field */ p += 8;
	const s64 shapes_val_field = p;
	p += 8;
	const s64 shapes_dict_field = p;
	p += 8;
	s64 mat_val_field, mat_dict_field;
	if (vmajor == 9)
	{
		mat_val_field = p;
		p += 8;
		p += 8; // unused padding/alternate-dict field (see ModelParser.Read())
		mat_dict_field = p;
		p += 8;
	}
	else
	{
		mat_val_field = p;
		p += 8;
		mat_dict_field = p;
		p += 8;
		if (vmajor >= 10)
			p += 8; // shader-assign offset (v10+ only)
	}
	/* userdata_val */ p += 8;
	/* userdata_dict */ p += 8;
	/* userptr */ p += 8;
	/* numVertexBuffer */ p += 2;
	const s64 numShape_off = p;
	p += 2;
	const s64 numMat_off = p;
	p += 2;

	(void)shapes_dict_field, (void)mat_dict_field;
	const s64 shapes_val
		= (size_t)shapes_val_field + 8 <= size ? (s64)le64 (d + shapes_val_field) : -1;
	const s64 mat_val = (size_t)mat_val_field + 8 <= size ? (s64)le64 (d + mat_val_field) : -1;
	const char *mname = rel_bfres_switch_string (d, size, (s64)le64 (d + name_field));

	fprintf (F.f,
		"<?xml version=\"1.0\"?>\n"
		"<!-- Switch BFRES (version-major %u): FMDL/FSHP/FMAT name resolution "
		"is decoded and verified against real retail samples -- see the "
		"comment above extract_bfres_switch_manifest() in wszst.c. Vertex/"
		"index geometry data is NOT decoded (location not verified yet); "
		"this only exports the structure (names/materials/vertex-attribute "
		"layout) as XML. -->\n"
		"<bfres-switch name=\"%s\" version-major=\"%u\" fmdl-count=\"%u\">\n",
		vmajor, mname ? mname : "", vmajor, n_fmdl);

	const uint n_fshp = (size_t)numShape_off + 2 <= size ? le16 (d + numShape_off) : 0;
	const uint n_fmat = (size_t)numMat_off + 2 <= size ? le16 (d + numMat_off) : 0;

	fprintf (F.f, "  <shapes count=\"%u\">\n", n_fshp);
	s64 sh = shapes_val;
	for (uint i = 0; i < n_fshp && sh >= 0 && (size_t)sh + 0x20 <= size; i++)
	{
		if (memcmp (d + sh, "FSHP", 4))
			break;
		const uint shdr = bfres_switch_hdr_extra (vmajor);
		const s64 sname_off = sh + 4 + shdr;
		const s64 fvtx_ptr = sname_off + 8;
		const char *sname = rel_bfres_switch_string (d, size, (s64)le64 (d + sname_off));
		const s64 fvtx = (s64)le64 (d + fvtx_ptr);
		fprintf (F.f, "    <shape name=\"%s\">\n", sname ? sname : "");
		if (fvtx > 0 && (size_t)fvtx + 0x60 <= size && !memcmp (d + fvtx, "FVTX", 4))
		{
			const uint vhdr = bfres_switch_hdr_extra (vmajor);
			const s64 attr_arr = (s64)le64 (d + fvtx + 4 + vhdr);
			// numVertexAttrib/numBuffer/local-BufferOffset sit right after
			// the two dict-pair fields (StrideOffset/SizeOffset), padding
			// s64, at fvtx+4+vhdr+0x40 (see VertexBufferParser.Load()).
			const s64 counts_off = fvtx + 4 + vhdr + 0x40;
			const s32 vb_local_off
				= (size_t)counts_off + 4 <= size ? (s32)le32 (d + counts_off) : 0;
			const uint n_attr = (size_t)counts_off + 8 <= size ? d[counts_off + 4] : 0;
			for (uint a = 0; a < n_attr; a++)
			{
				const s64 ae = attr_arr + (s64)a * 16;
				if (ae < 0 || (size_t)ae + 16 > size)
					break;
				const char *aname = rel_bfres_switch_string (d, size, (s64)le64 (d + ae));
				fprintf (F.f,
					"      <attribute name=\"%s\" format=\"0x%x\" "
					"buffer-offset=\"%u\" buffer-index=\"%u\"/>\n",
					aname ? aname : "", le32 (d + ae + 8), le16 (d + ae + 12), d[ae + 14]);
			}
			(void)vb_local_off; // local offset into the (unresolved) buffer pool
		}
		fprintf (F.f, "    </shape>\n");

		// FSHP entries aren't fixed-stride (each carries a variable-size
		// bounding/skin-index tail), so the next one is found the same way
		// Python's brute-force reference decode did: scan forward for the
		// next literal "FSHP" magic.
		const u8 *next = i + 1 < n_fshp ? memmem (d + sh + 4, size - (sh + 4), "FSHP", 4) : NULL;
		sh = next ? next - d : -1;
	}
	fprintf (F.f, "  </shapes>\n");

	fprintf (F.f, "  <materials count=\"%u\">\n", n_fmat);
	if (mat_val > 0 && (size_t)mat_val + 0x20 <= size && !memcmp (d + mat_val, "FMAT", 4))
	{
		const uint mhdr = bfres_switch_hdr_extra (vmajor);
		const char *matname = rel_bfres_switch_string (d, size, (s64)le64 (d + mat_val + 4 + mhdr));
		fprintf (F.f, "    <material name=\"%s\"/>\n", matname ? matname : "");
	}
	fprintf (F.f, "  </materials>\n");
	fprintf (F.f, "</bfres-switch>\n");

	if (ferror (F.f) && !err)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (d);
	return err ? err : ERR_OK;
}

// Export the structural half of a 3DS/Wii U bitmap font (BCFNT/BFFNT) as
// XML. Wii's own font family (RFNT/RFNA, container magic "RFNT"/"RFNA") is
// already fully decoded to PNG via the TGLP branch in AssignIMG() --
// lib-image2.c. The 3DS/Wii U family reuses the exact same FINF/TGLP
// section shapes (confirmed field-for-field against hadashisora/NintyFont's
// from-source CFNT/FINF/TGLP reader for the container header and TGLP
// itself), but is NOT simply "BRFNT with a different container magic":
//   - 3DS uses magic "CFNT"; Wii U uses "FFNT" -- a real, different magic,
//     not a NintyFont naming quirk (NintyFont's "CFNT" reader is 3DS-only
//     and does not cover "FFNT" at all).
//   - The FINF pointer fields (ptrGlyph/ptrWidth/ptrMap) sit 4 bytes later
//     than NintyFont's declared struct says (FINF+0x14/+0x18/+0x1C, not
//     NintyFont's documented +0x10/+0x14/+0x18) -- caught by cross-checking
//     against two real retail Wii U .bffnt samples (DynaFont_NW_Demo.bffnt,
//     CafeStd_25.bffnt): NintyFont's offsets point at garbage, the +4
//     shifted ones land exactly on a real "TGLP"/"CWDH"/"CMAP" magic. This
//     fork's own decode uses the verified real offsets, not the reference
//     tool's.
//   - TGLP's sheetFormat is a 3DS/Cafe GPU texture-format id, a completely
//     different numbering from the Wii GX ids GetImageGeometry() (used by
//     the BRFNT decode path) already understands -- reusing that table
//     would silently decode the wrong pixel format. No 3DS/Cafe format
//     table exists in this fork yet, so pixel decode is left undone rather
//     than guessed; this only exports the verified structural fields
//     (cell/sheet geometry, sheet count/format id, pointers), the same
//     "don't ship an unverified guess" scope as the Switch BFRES manifest
//     above. No real .bcfnt sample was found to verify the 3DS side
//     specifically, but the container/TGLP shape is shared with .bffnt --
//     see PLAN.md.
static enumError extract_cfnt_manifest (ccp arg)
{
	if (!is_ext (arg, ".bffnt") && !is_ext (arg, ".bcfnt") && !is_ext (arg, ".ffnt")
		&& !is_ext (arg, ".cfnt"))
		return ERR_NOTHING_TO_DO;

	u8 *d = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &d, &size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	const bool is_cfnt = size >= 4 && !memcmp (d, "CFNT", 4);
	const bool is_ffnt = size >= 4 && !memcmp (d, "FFNT", 4);
	if ((!is_cfnt && !is_ffnt) || size < 0x14)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	const bool be = d[4] == 0xFE && d[5] == 0xFF; // BOM: FEFF=big, FFFE=little
	if (!be && !(d[4] == 0xFF && d[5] == 0xFE))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	} // neither BOM reading is valid

#define CF16(p) (be ? be16 (p) : le16 (p))
#define CF32(p) (be ? be32 (p) : le32 (p))

	const uint header_size = CF16 (d + 6);
	if (header_size < 0x14 || (size_t)header_size + 0x14 > size
		|| memcmp (d + header_size, "FINF", 4))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const size_t finf = header_size;
	const uint finf_len = CF32 (d + finf + 4);
	if ((finf_len != 0x1C && finf_len != 0x20) || finf + finf_len > size)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const uint font_type = d[finf + 8];

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s structure:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", is_cfnt ? "BCFNT" : "BFFNT", arg, dest);
	if (testmode)
	{
		FREE (d);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (!F.f)
	{
		FREE (d);
		return err;
	}
	fprintf (F.f,
		"<?xml version=\"1.0\"?>\n"
		"<!-- %s structure manifest. Pixel sheets are decoded separately to PNG "
		"by decode_cfnt_if_possible() via AssignIMG(NFMT_BCFNT). CTR format 0 "
		"(RGBA8 linear) is fully supported; formats 3/5/7/9/10 (RGB565/IA8/I8/"
		"IA4/I4) are decoded via the GX tile path (correct for linear data, "
		"potentially misordered for real retail CTR/Cafe hardware-tiled sheets). "
		"-->\n<%s font-type=\"%u\">\n",
		is_cfnt ? "BCFNT" : "BFFNT", is_cfnt ? "bcfnt" : "bffnt", font_type);

	if (font_type == 1 && finf_len >= 0x20 && (size_t)finf + 0x18 <= size)
	{
		const uint ptr_glyph = CF32 (d + finf + 0x14);
		const size_t tglp = (size_t)ptr_glyph - 8;
		if (ptr_glyph >= 8 && tglp + 0x20 <= size && !memcmp (d + tglp, "TGLP", 4))
		{
			fprintf (F.f,
				"  <tglp cell-width=\"%u\" cell-height=\"%u\" baseline=\"%u\" "
				"max-char-width=\"%u\" sheet-size=\"%u\" sheet-count=\"%u\" "
				"sheet-format=\"0x%x\" cells-per-row=\"%u\" cells-per-column=\"%u\" "
				"sheet-width=\"%u\" sheet-height=\"%u\" sheet-data-offset=\"0x%x\"/>\n",
				d[tglp + 8], d[tglp + 9], d[tglp + 10], d[tglp + 11], CF32 (d + tglp + 0xC),
				CF16 (d + tglp + 0x10), CF16 (d + tglp + 0x12), CF16 (d + tglp + 0x14),
				CF16 (d + tglp + 0x16), CF16 (d + tglp + 0x18), CF16 (d + tglp + 0x1A),
				CF32 (d + tglp + 0x1C));
		}
		else
			fprintf (F.f,
				"  <!-- fontType==1 (TGLP) but ptrGlyph-8 (0x%zx) "
				"isn't a valid TGLP -- truncated or unexpected file -->\n",
				tglp);
	}
	else
		fprintf (F.f,
			"  <!-- fontType==%u (bitmap/CGLP glyphs), not the TGLP "
			"case this fork decodes -->\n",
			font_type);

	fprintf (F.f, "</%s>\n", is_cfnt ? "bcfnt" : "bffnt");
#undef CF16
#undef CF32

	if (ferror (F.f) && !err)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (d);
	return err ? err : ERR_OK;
}

// Decode a Wii bitmap font (BRFNT/BRFNA, container magic "RFNT"/"RFNA") to
// PNG sheet(s) during "wszst XX". The pixel decode itself already exists --
// AssignIMG()'s TGLP branch in lib-image2.c fully understands it (see the
// comment above extract_cfnt_manifest()) -- but nothing in the XX/EXTRACT
// tree walk ever called it; only an explicit `wimgt DECODE some.brfnt` did,
// so a real font found inside an extracted disc was left as an undecoded
// raw member. Mirrors wimgt.c's own cmd_decode(): TGLP glyph sheets are
// independent atlases, not a mip chain, so each info_n_image record gets
// its own PNG.
static enumError decode_brfnt_if_possible (ccp arg)
{
	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);
	if (n_magic != sizeof (magic) || (memcmp (magic, "RFNT", 4) && memcmp (magic, "RFNA", 4)))
		return ERR_NOTHING_TO_DO;

	Image_t img;
	enumError max_err = LoadIMG (&img, true, arg, 0, false, true, false);
	if (max_err)
		return ERR_NOTHING_TO_DO;

	char dest_base[PATH_MAX];
	if (opt_dest)
		SubstDest (dest_base, sizeof (dest_base), arg, opt_dest, 0, ".png", false);
	else
		snprintf (dest_base, sizeof (dest_base), "%s.png", arg);

	const uint record_images
		= img.info_fform == FF_UNKNOWN && img.info_n_image > 1 ? img.info_n_image : 1;

	if (record_images <= 1)
	{
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE BRFNT:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest_base);
		if (!testmode)
		{
			const enumError s_err = SavePNG (&img, true, 0, dest_base, 0, 0, false, 0);
			if (s_err <= ERR_WARNING && max_err < s_err)
				max_err = s_err;
		}
		ResetIMG (&img);
		return max_err;
	}

	ResetIMG (&img);
	for (uint image_index = 0; image_index < record_images; image_index++)
	{
		const enumError load_err = LoadIMG (&img, true, arg, image_index, false, true, false);
		if (load_err)
			continue;

		char dest[PATH_MAX];
		ccp ext = strrchr (dest_base, '.');
		if (ext)
			snprintf (dest, sizeof (dest), "%.*s.sheet%03u%s", (int)(ext - dest_base), dest_base,
				image_index, ext);
		else
			snprintf (dest, sizeof (dest), "%s.sheet%03u.png", dest_base, image_index);

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE BRFNT:%s[%u] -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, image_index, dest);
		if (!testmode)
		{
			const enumError sheet_err = SavePNG (&img, true, 0, dest, 0, 0, false, 0);
			if (sheet_err <= ERR_WARNING && max_err < sheet_err)
				max_err = sheet_err;
		}
		ResetIMG (&img);
	}
	return max_err;
}

// Decode a 3DS/Wii U bitmap font (BCFNT/BFFNT) to PNG sheet(s) during
// "wszst XX". AssignIMG()'s NFMT_BCFNT branch in lib-image2.c handles the
// pixel decode. Mirrors decode_brfnt_if_possible() sheet-iteration logic:
// each TGLP glyph sheet is an independent atlas, so info_n_image > 1 gets
// one PNG per sheet.
static enumError decode_cfnt_if_possible (ccp arg)
{
	if (!is_ext (arg, ".bffnt") && !is_ext (arg, ".bcfnt") && !is_ext (arg, ".ffnt")
		&& !is_ext (arg, ".cfnt"))
		return ERR_NOTHING_TO_DO;

	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);
	if (n_magic != sizeof (magic) || (memcmp (magic, "CFNT", 4) && memcmp (magic, "FFNT", 4)))
		return ERR_NOTHING_TO_DO;

	Image_t img;
	enumError max_err = LoadIMG (&img, true, arg, 0, false, true, false);
	if (max_err)
		return ERR_NOTHING_TO_DO;

	char dest_base[PATH_MAX];
	if (opt_dest)
		SubstDest (dest_base, sizeof (dest_base), arg, opt_dest, 0, ".png", false);
	else
		snprintf (dest_base, sizeof (dest_base), "%s.png", arg);

	const uint record_images
		= img.info_fform == FF_UNKNOWN && img.info_n_image > 1 ? img.info_n_image : 1;

	if (record_images <= 1)
	{
		ccp tag = !memcmp (magic, "CFNT", 4) ? "BCFNT" : "BFFNT";
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE %s:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", tag, arg, dest_base);
		if (!testmode)
		{
			const enumError s_err = SavePNG (&img, true, 0, dest_base, 0, 0, false, 0);
			if (s_err <= ERR_WARNING && max_err < s_err)
				max_err = s_err;
		}
		ResetIMG (&img);
		return max_err;
	}

	ResetIMG (&img);
	for (uint image_index = 0; image_index < record_images; image_index++)
	{
		const enumError load_err = LoadIMG (&img, true, arg, image_index, false, true, false);
		if (load_err)
			continue;

		char dest[PATH_MAX];
		ccp ext = strrchr (dest_base, '.');
		if (ext)
			snprintf (dest, sizeof (dest), "%.*s.sheet%03u%s", (int)(ext - dest_base), dest_base,
				image_index, ext);
		else
			snprintf (dest, sizeof (dest), "%s.sheet%03u.png", dest_base, image_index);

		ccp tag = !memcmp (magic, "CFNT", 4) ? "BCFNT" : "BFFNT";
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE %s:%s[%u] -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", tag, arg, image_index, dest);
		if (!testmode)
		{
			const enumError sheet_err = SavePNG (&img, true, 0, dest, 0, 0, false, 0);
			if (sheet_err <= ERR_WARNING && max_err < sheet_err)
				max_err = sheet_err;
		}
		ResetIMG (&img);
	}
	return max_err;
}

// Convert a BFLYT/BCLYT/BRLYT/BRLAN layout or animation to text (.tflyt)
// during "wszst XX", mirroring what `wszst TEXT` already does for a single
// file (see cmd_convert()'s "Handle layout formats" branch a few hundred
// lines below). A layout/anim member found inside an extracted tree (e.g.
// a game's LZ-compressed romfs .bin -- already plain by the time this runs,
// decompress_nintendo_file() ran first) was previously left as an undecoded
// binary blob; nothing in the XX/EXTRACT tree walk called ScanBFLYT()/
// SaveTextBFLYT(). Only runs when export_count>0 (XX aliases to XEXPORT),
// matching the gate used by decode_brfnt_if_possible()/export_model_if_possible().
static enumError decode_bflyt_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);

	const bool is_lyt_magic = n_magic == sizeof (magic)
		&& (!memcmp (magic, "FLYT", 4) || !memcmp (magic, "CLYT", 4)
			|| !memcmp (magic, "RLYT", 4) || !memcmp (magic, "RLAN", 4));
	const bool is_lyt_ext = is_ext (arg, ".bflyt") || is_ext (arg, ".bclyt")
		|| is_ext (arg, ".brlyt") || is_ext (arg, ".brlan")
		|| is_ext (arg, ".flyt") || is_ext (arg, ".clyt");

	if (!is_lyt_magic && !is_lyt_ext)
		return ERR_NOTHING_TO_DO;

	raw_data_t raw;
	InitializeRawData (&raw);
	enumError err = LoadRawData (&raw, false, arg, 0, false, 0);
	if (err > ERR_WARNING)
	{
		ResetRawData (&raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw.fform != FF_BFLYT && raw.fform != FF_BCLYT && raw.fform != FF_BRLYT
		&& raw.fform != FF_BRLAN)
	{
		ResetRawData (&raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".tflyt", false);
	else
		snprintf (dest, sizeof (dest), "%s.tflyt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sCREATE/TEXT %s:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetNameFF (raw.fform, 0), arg, dest);
	if (testmode)
	{
		ResetRawData (&raw);
		return ERR_OK;
	}

	bflyt_t bflyt;
	InitializeBFLYT (&bflyt);
	err = ScanBFLYT (&bflyt, false, raw.data, raw.data_size);
	if (!err)
		err = SaveTextBFLYT (&bflyt, dest, true);
	ResetBFLYT (&bflyt);
	ResetRawData (&raw);
	return err ? err : ERR_OK;
}

// Export a KCL collision file to a viewable .kcl.glb companion during
// "wszst XX", alongside the existing .obj text-editing round trip (which
// SaveTextKCL/ScanTextKCL still own -- that pairing is unrelated to this
// export and is left untouched). KCL has no magic signature, so detection
// is by extension only, same as decode_bflyt_if_possible()'s is_lyt_ext path.
static enumError export_kcl_glb_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;
	if (!is_ext (arg, ".kcl"))
		return ERR_NOTHING_TO_DO;

	raw_data_t raw;
	InitializeRawData (&raw);
	enumError err = LoadRawData (&raw, false, arg, 0, false, 0);
	if (err > ERR_WARNING || raw.fform != FF_KCL)
	{
		ResetRawData (&raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".glb", false);
	else
		snprintf (dest, sizeof (dest), "%s.glb", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sCREATE/GLB %s:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetNameFF (raw.fform, 0), arg, dest);
	if (testmode)
	{
		ResetRawData (&raw);
		return ERR_OK;
	}

	kcl_t kcl;
	InitializeKCL (&kcl);
	kcl.fname = arg;
	err = ScanKCL (&kcl, false, raw.data, raw.data_size, false, global_check_mode);
	if (!err)
		err = SaveGlbKCL (&kcl, dest, true);
	kcl.fname = 0;
	ResetKCL (&kcl);
	ResetRawData (&raw);
	return err ? err : ERR_OK;
}

static enumError decode_byml_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;
	if (!is_ext (arg, ".byml") && !is_ext (arg, ".byaml") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *data = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (size < 16 || size > UINT_MAX)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	const nfmt_type_t type = DetectNintendoFormat (data, size, arg).type;
	if (type != NFMT_BYML)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".yaml", false);
	else
		snprintf (dest, sizeof (dest), "%s.yaml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE BYML:%s -> YAML:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (testmode)
	{
		FREE (data);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (!F.f)
	{
		FREE (data);
		return err;
	}
	err = DecodeBYML_YAML (F.f, data, size);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError decode_msbt_if_possible (ccp arg)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOT_EXISTS;
	u8 head[32];
	size_t got = fread (head, 1, sizeof (head), f);
	fclose (f);
	if (got < 32)
		return ERR_NOTHING_TO_DO;

	bool is_msbt = IsMSBT (head, (uint)got);
	bool is_msbp = IsMSBP (head, (uint)got);
	bool is_msbf = IsMSBF (head, (uint)got);
	if (!is_msbt && !is_msbp && !is_msbf)
		return ERR_NOTHING_TO_DO;

	u8 *data = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &size, 0, 0, 0, false);
	if (err || !data)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0,
			is_msbt		  ? ".tmsbt"
				: is_msbp ? ".tmsbp"
						  : ".tmsbf",
			false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "",
			is_msbt		  ? "MSBT"
				: is_msbp ? "MSBP"
						  : "MSBF",
			arg, dest);

	if (testmode)
	{
		FREE (data);
		return ERR_OK;
	}

	if (is_msbt)
	{
		msbt_file_t msbt;
		err = ScanMSBT (&msbt, data, (uint)size, arg);
		if (!err)
			err = SaveTextMSBT (&msbt, dest);
		ResetMSBT (&msbt);
	}
	else if (is_msbp)
	{
		msbp_file_t msbp;
		err = ScanMSBP (&msbp, data, (uint)size, arg);
		if (!err)
			err = SaveTextMSBP (&msbp, dest);
		ResetMSBP (&msbp);
	}
	else if (is_msbf)
	{
		msbf_file_t msbf;
		err = ScanMSBF (&msbf, data, (uint)size, arg);
		if (!err)
			err = SaveTextMSBF (&msbf, dest);
		ResetMSBF (&msbf);
	}

	FREE (data);
	return err;
}

// Export the structural half of a Nitro sprite set as XML.  NCGR/NCLR pixels
// remain ordinary wimgt inputs; the manifest records the exact OAM and frame
// values that connect those pixels to NCER/NANR cell/animation resources.
static enumError extract_nitro_sprite_manifest (ccp arg)
{
	if (!is_ext (arg, ".bncr") && !is_ext (arg, ".nanr") && !is_ext (arg, ".ncer")
		&& !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *data = 0;
	size_t file_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (file_size > UINT_MAX)
	{
		FREE (data);
		return ERR_FILE_TOO_BIG;
	}
	const nfmt_type_t type = DetectNintendoFormat (data, file_size, arg).type;
	if (type != NFMT_NCER && type != NFMT_NANR)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);

	if (!opt_overwrite && !access (dest, F_OK))
	{
		FREE (data);
		return ERR_OK;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s XML:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetNintendoFormatName (type), arg, dest);
	if (testmode)
	{
		FREE (data);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (!F.f)
	{
		FREE (data);
		return err;
	}
	if (type == NFMT_NCER)
	{
		nintendo_ncer_t ncer;
		err = ScanNCER (&ncer, data, file_size);
		if (err)
			err = ERR_INVALID_DATA;
		if (!err)
			fprintf (F.f, "<?xml version=\"1.0\"?>\n<ncer cells=\"%u\">\n", ncer.n_cells);
		for (uint i = 0; !err && i < ncer.n_cells; i++)
		{
			uint n_obj;
			const u8 *oam;
			err = GetNCERCell (&ncer, i, &n_obj, &oam);
			if (!err)
				fprintf (F.f, "  <cell index=\"%u\" objects=\"%u\">\n", i, n_obj);
			for (uint j = 0; !err && j < n_obj; j++, oam += 6)
				if (fprintf (F.f, "    <obj attr0=\"0x%04x\" attr1=\"0x%04x\" attr2=\"0x%04x\"/>\n",
						le16 (oam), le16 (oam + 2), le16 (oam + 4))
					< 0)
					err = ERR_WRITE_FAILED;
			if (!err && fprintf (F.f, "  </cell>\n") < 0)
				err = ERR_WRITE_FAILED;
		}
		if (!err && fprintf (F.f, "</ncer>\n") < 0)
			err = ERR_WRITE_FAILED;
	}
	else
	{
		nintendo_nanr_t nanr;
		err = ScanNANR (&nanr, data, file_size);
		if (err)
			err = ERR_INVALID_DATA;
		if (!err)
			fprintf (F.f, "<?xml version=\"1.0\"?>\n<nanr animations=\"%u\" frames=\"%u\">\n",
				nanr.n_animations, nanr.n_frames);
		for (uint i = 0; !err && i < nanr.n_animations; i++)
		{
			uint n_frames;
			const u8 *frames;
			err = GetNANRAnimation (&nanr, i, &n_frames, &frames);
			if (!err)
				fprintf (F.f, "  <animation index=\"%u\" frames=\"%u\">\n", i, n_frames);
			for (uint j = 0; !err && j < n_frames; j++, frames += 8)
			{
				const uint off = le32 (frames);
				if (fprintf (F.f, "    <frame cell=\"%u\" duration=\"%u\" data-offset=\"0x%x\"/>\n",
						le16 (nanr.frame_data + off), le16 (frames + 4), off)
					< 0)
					err = ERR_WRITE_FAILED;
			}
			if (!err && fprintf (F.f, "  </animation>\n") < 0)
				err = ERR_WRITE_FAILED;
		}
		if (!err && fprintf (F.f, "</nanr>\n") < 0)
			err = ERR_WRITE_FAILED;
	}
	if (ferror (F.f) && !err)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err ? err : ERR_OK;
}

// Load every CHR0 in one extracted "AnmChr(NW4R)" directory into `model`.
// Extracted BRRES members keep their raw resource name verbatim -- no
// ".chr0" extension is added (unlike, say, texture PNGs) -- so a sidecar
// ".txt" dump is the only thing to actually skip; everything else is a CHR0.
static void import_chr0_dir (model_t *model, const char *anm_dir)
{
	DIR *dp = opendir (anm_dir);
	if (!dp)
		return;
	struct dirent *de;
	while ((de = readdir (dp)))
	{
		if (is_ext (de->d_name, ".txt"))
			continue;
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char chr0_path[PATH_MAX];
		snprintf (chr0_path, sizeof (chr0_path), "%s/%s", anm_dir, de->d_name);
		u8 *cdata = 0;
		size_t csize = 0;
		if (LoadFileAlloc (chr0_path, 0, 0, &cdata, &csize, 0, 0, 0, false) == ERR_OK)
		{
			if (csize >= 4 && !memcmp (cdata, "CHR0", 4))
				ParseCHR0IntoModel (model, cdata, csize, de->d_name);
			FREE (cdata);
		}
	}
	closedir (dp);
}

// A BRRES's MDL0 lives under ".../3DModels(NW4R)/name"; CHR0 siblings
// extract to ".../AnmChr(NW4R)/*" right next to it when model and animation
// share one BRRES. Brawl's own fighter data instead splits a character into
// three sibling BRRES containers extracted as "NNNN_NN.ModelData.bin.d",
// "NNNN_NN.AnimationData.bin.d" and "...TextureData.bin.d" (see e.g.
// DATA/files/fighter/*/Fit*.pac), so also check the AnimationData.bin.d
// directory with the same numeric prefix, in the same parent, when the
// model's own directory is named *ModelData.bin.d.
static void import_chr0_siblings_for_mdl0 (model_t *model, const char *arg)
{
	char argcopy[PATH_MAX];
	snprintf (argcopy, sizeof (argcopy), "%s", arg);
	char *slash = strrchr (argcopy, '/');
	char *mdl_pos = slash ? strstr (argcopy, "3DModels(NW4R)") : NULL;
	if (!mdl_pos)
		return;
	const size_t prefix_len = mdl_pos - argcopy;

	char anm_dir[PATH_MAX];
	snprintf (anm_dir, sizeof (anm_dir), "%.*sAnmChr(NW4R)", (int)prefix_len, argcopy);
	import_chr0_dir (model, anm_dir);

	// prefix_len includes the trailing '/'; the BRRES-extraction directory
	// name itself is the path component just before it.
	if (prefix_len < 2)
		return;
	char brres_dir[PATH_MAX];
	snprintf (brres_dir, sizeof (brres_dir), "%.*s", (int)(prefix_len - 1), argcopy);
	char *dir_slash = strrchr (brres_dir, '/');
	char *dir_name = dir_slash ? dir_slash + 1 : brres_dir;
	char *model_tag = strstr (dir_name, "ModelData.bin.d");
	if (!model_tag || model_tag[strlen ("ModelData.bin.d")] != 0)
		return;

	char anim_brres_dir[PATH_MAX];
	snprintf (anim_brres_dir, sizeof (anim_brres_dir), "%.*sAnimationData.bin.d",
		(int)(model_tag - brres_dir), brres_dir);
	char anim_anm_dir[PATH_MAX];
	snprintf (anim_anm_dir, sizeof (anim_anm_dir), "%s/AnmChr(NW4R)", anim_brres_dir);
	import_chr0_dir (model, anim_anm_dir);
}

// Export a 3D model file (MDL0 from a BRRES, or a standalone NSBMD/BCRES/
// BCH/BFRES container) to DAE, mirroring what `wmdlt ENCODE -d out.dae`
// does standalone. XX/XEXPORT's extraction pipeline used to only decode
// BRRES down to its raw MDL0/texture/anim members -- the model geometry
// itself was written out but never converted, so a real disc's 3D models
// (e.g. Kirby's Epic Yarn's .gfa -> .brres -> 3DModels(NW4R)/*) never
// produced a .dae despite ParseMDL0()/ExportModelToDAE() already existing
// and being wired into wmdlt. Only runs when export_count>0 (XX aliases to
// XEXPORT, which sets it) so plain XDECODE/XALL keep their existing output.
static enumError export_model_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	// A recursive post-pass visits a complete extracted game tree. Probe the
	// four-byte signature before loading a file so unrelated sounds, scripts,
	// and archives are not needlessly read into memory just to be declined.
	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);
	const bool is_bmd_file = is_ext (arg, ".bmd");
	if (n_magic != sizeof (magic)
		|| (memcmp (magic, "BMD0", 4) && memcmp (magic, "CGFX", 4) && memcmp (magic, "BCH\0", 4)
			&& memcmp (magic, "FRES", 4) && memcmp (magic, "MDL0", 4) && !is_bmd_file))
		return ERR_NOTHING_TO_DO;

	u8 *data = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	model_t *model = 0;
	if ((size >= 4 && !memcmp (data, "BMD0", 4)) || is_bmd_file)
	{
		char dest_probe[PATH_MAX];
		if (opt_dest)
			SubstDest (dest_probe, sizeof (dest_probe), arg, opt_dest, 0, ".glb", false);
		else
			snprintf (dest_probe, sizeof (dest_probe), "%s.glb", arg);
		if (!testmode)
			ExportEarlyDSBMDTextures (data, size, dest_probe);
		model = ParseNSBMD (data, size);
	}
	else if (size >= 4 && !memcmp (data, "CGFX", 4))
	{
		char dest_probe[PATH_MAX];
		if (opt_dest)
			SubstDest (dest_probe, sizeof (dest_probe), arg, opt_dest, 0, ".glb", false);
		else
			snprintf (dest_probe, sizeof (dest_probe), "%s.glb", arg);
		if (!testmode)
			ExportBCRESTexturesFromData (data, size, dest_probe);
		model = ParseBCRES (data, size);
	}
	else if (size >= 4 && !memcmp (data, "BCH\0", 4))
	{
		char dest_probe[PATH_MAX];
		if (opt_dest)
			SubstDest (dest_probe, sizeof (dest_probe), arg, opt_dest, 0, ".glb", false);
		else
			snprintf (dest_probe, sizeof (dest_probe), "%s.glb", arg);
		if (!testmode)
			ExportBCHTexturesFromData (data, (uint)size, dest_probe);
		model = (model_t *)ParseBCH (data, (uint)size);
	}
	else if (size >= 4 && !memcmp (data, "FRES", 4))
	{
		model = ParseBFRES (data, size);
		if (!model)
			model = ParseBFRESSwitch (data, size);
	}
	else if (size >= 4 && !memcmp (data, "MDL0", 4))
	{
		model = ParseMDL0 (data, size);
		if (model)
			import_chr0_siblings_for_mdl0 (model, arg);
	}
	FREE (data);
	if (!model)
		return ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".glb", false);
	else
		snprintf (dest, sizeof (dest), "%s.glb", arg);
	if (opt_dest && !is_ext (dest, ".glb"))
	{
		FreeModel (model);
		return ERR_NOTHING_TO_DO;
	}
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXPORT MODEL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (!testmode)
		err = (ExportModelToGLB (model, dest))
			? ERROR0 (ERR_WRITE_FAILED, "Failed to write GLB: %s\n", dest)
			: ERR_OK;
	FreeModel (model);
	return err;
}

// Convert a BRSAR (Wii sound archive) found in an extracted tree to MIDI+SF2.
// wszst itself does not link vgmtrans (it needs cmake+glib, which the
// Makefile deliberately keeps out of the main tool -- see NO_VGMTRANS), so
// this shells out to the sibling `wbrsar` binary that statically links it,
// the same pass-through style lib-passthru.c already uses for ctrtool/
// ndstool/sharpii/wit. Without this, "wszst XX" on a tree containing a
// .brsar silently left it as a raw, unconverted member. Only runs when
// export_count>0 (XX aliases to XEXPORT) so plain XDECODE/XALL are unaffected.
static enumError convert_brsar_if_possible (ccp arg)
{
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	// Probe the four-byte signature before shelling out, same discipline as
	// export_model_if_possible() -- most files in a real tree are neither.
	u8 magic[4];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	const size_t n_magic = fread (magic, 1, sizeof (magic), probe);
	fclose (probe);

	bool is_rsar = (n_magic == sizeof (magic) && !memcmp (magic, "RSAR", 4));
	bool is_sdat = (n_magic == sizeof (magic) && !memcmp (magic, "SDAT", 4));
	bool is_sound = is_rsar || is_sdat;

	if (!is_sound)
		return ERR_NOTHING_TO_DO;

	// Prefer the wbrsar built alongside this binary (same install/build dir)
	// over whatever a bare PATH search might turn up first.
	char tool[PATH_MAX] = { 0 };
	char mypath[PATH_MAX];
	GetProgramPath (mypath, sizeof (mypath), true, 0);
	ccp slash = strrchr (mypath, '/');
	if (slash)
	{
		snprintf (tool, sizeof (tool), "%.*s/wbrsar", (int)(slash - mypath), mypath);
		if (access (tool, X_OK))
			*tool = 0;
	}
	if (!*tool)
	{
		ccp dirs = getenv ("PATH");
		while (dirs && *dirs && !*tool)
		{
			ccp end = strchr (dirs, ':');
			const uint len = end ? (uint)(end - dirs) : (uint)strlen (dirs);
			if (len)
			{
				char cand[PATH_MAX];
				snprintf (cand, sizeof (cand), "%.*s/wbrsar", (int)len, dirs);
				if (!access (cand, X_OK))
					snprintf (tool, sizeof (tool), "%s", cand);
			}
			dirs = end ? end + 1 : 0;
		}
	}
	if (!*tool)
		return ERROR0 (ERR_WARNING, "wbrsar not found; cannot convert %s to MIDI+SF2: %s\n",
			is_sdat || is_ext (arg, ".sdat") ? "SDAT" : "BRSAR", arg);

	// Same "<stem>.d" staging convention every other extractor in this
	// codebase uses (SARC/PAC/GFA/pass-through containers).
	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".d", false);
	else
		snprintf (dest, sizeof (dest), "%s.d", arg);

	if (!opt_overwrite && !access (dest, F_OK))
		return ERR_OK;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXPORT %s:%s -> %s (wbrsar)\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", is_sdat || is_ext (arg, ".sdat") ? "SDAT" : "BRSAR", arg,
			dest);
	if (testmode)
		return ERR_OK;

	char *const child_argv[] = { tool, (char *)arg, dest, 0 };
	const pid_t pid = fork ();
	if (pid < 0)
		return ERROR0 (ERR_CANT_CREATE, "fork() failed converting sound archive: %s\n", arg);
	if (pid == 0)
	{
		execv (tool, child_argv);
		_Exit (127); // execv failed
	}
	int status = 0;
	while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
		;
	if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
		return ERROR0 (ERR_WARNING, "wbrsar failed for %s\n", arg);
	return ERR_OK;
}

// Walk an extracted BRRES/SZS tree looking for 3D model members (MDL0 and
// friends) to export as DAE, and for BRSAR sound archives to convert to
// MIDI+SF2. ExtractFilesSZS() writes both kinds of member out as plain
// binary -- it has no notion of DAE/MIDI export -- so this is the piece
// that actually turns on "XX exports 3D models and sound banks found inside
// .brres/.szs trees" rather than just decoding them down to raw members.
static enumError export_models_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	enumError max_err = ERR_OK;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
		{
			max_err = ERR_FILE_TOO_BIG;
			continue;
		}
		struct stat st;
		if (lstat (path, &st))
		{
			if (max_err < ERR_NOT_EXISTS)
				max_err = ERR_NOT_EXISTS;
			continue;
		}
		enumError err = ERR_OK;
		if (S_ISDIR (st.st_mode))
			err = export_models_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
		{
			// decode_brfnt_if_possible()/decode_bflyt_if_possible() are also
			// called from extract_one_file(), but that call is a no-op for
			// anything reached through a nested SARC/PAC/GFA/DARC container:
			// extract_tree_complete() zeroes export_count for the duration of
			// its inner extract_tree() walk (see the comment there), and both
			// functions gate on export_count>0 the same way export_model_if_
			// possible() does. This deferred, complete-tree pass is where
			// export_count is genuinely restored, so it's the one that
			// actually reaches layout/font members bundled inside those
			// containers -- confirmed necessary on a real retail 3DS disc,
			// where DARC-bundled layout families (see extract_darc_file())
			// are exactly this case: 0 conversions without this, real output
			// with it.
			err = decode_brfnt_if_possible (path);
			const enumError cfnt_err = decode_cfnt_if_possible (path);
			if (cfnt_err != ERR_NOTHING_TO_DO && err < cfnt_err)
				err = cfnt_err;
			const enumError bflyt_err = decode_bflyt_if_possible (path);
			if (bflyt_err != ERR_NOTHING_TO_DO && err < bflyt_err)
				err = bflyt_err;
			const enumError byml_err = decode_byml_if_possible (path);
			if (byml_err != ERR_NOTHING_TO_DO && err < byml_err)
				err = byml_err;
			const enumError kcl_err = export_kcl_glb_if_possible (path);
			if (kcl_err != ERR_NOTHING_TO_DO && err < kcl_err)
				err = kcl_err;
			const enumError img_err = decode_image_if_possible (path);
			if (img_err != ERR_NOTHING_TO_DO && err < img_err)
				err = img_err;
			const enumError model_err = export_model_if_possible (path);
			if (model_err != ERR_NOTHING_TO_DO && err < model_err)
				err = model_err;
			const enumError brsar_err = convert_brsar_if_possible (path);
			if (brsar_err != ERR_NOTHING_TO_DO && err < brsar_err)
				err = brsar_err;
			// extract_hsf_file() deferred its own conversion here (see
			// g_in_hsf_deferred_pass) so it can run after SetDAETextureSearchRoot()
			// has actually indexed the textures HSF itself decodes as a side
			// effect of the very same DecodeHSF() call -- g_in_hsf_deferred_pass
			// is back off by now, so this call does the real work.
			const enumError hsf_err = extract_hsf_file (path, 0, depth);
			if (hsf_err != ERR_NOTHING_TO_DO && err < hsf_err)
				err = hsf_err;
		}
		if (err != ERR_NOTHING_TO_DO)
		{
			if (err > ERR_WARNING)
				err = ERR_WARNING;
			if (max_err < err)
				max_err = err;
		}
	}
	closedir (dir);
	return max_err;
}

static inline u16 bft_rb16 (const u8 *p)
{
	return (u16)p[0] << 8 | p[1];
}
static inline u32 bft_rb32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline s32 bft_rbs32 (const u8 *p)
{
	return (s32)bft_rb32 (p);
}
// Resolves a self-relative offset stored at ADDR, same convention as
// lib-bfres.c's own REL() macro (Wii U BFRES offsets are relative to the
// field that holds them).
#define BFT_REL(base, addr) ((size_t)(addr) + (size_t)bft_rbs32 ((base) + (addr)))

static const char *bft_rel_string (const u8 *d, size_t size, size_t at)
{
	if (at + 4 > size)
		return NULL;
	const size_t p = BFT_REL (d, at);
	if (p >= size)
		return NULL;
	for (size_t q = p; q < size; q++)
		if (!d[q])
			return (const char *)(d + p);
	return NULL;
}

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
		dest[0] = 0;

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

static enumError extract_bcres_textures (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bcres") && !is_ext (arg, ".cgfx") && !is_ext (arg, ".bcmdl")
		&& !is_ext (arg, ".bcfnt"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x20 || memcmp (raw, "CGFX", 4))
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
		dest[0] = 0;

	if (!testmode)
		ExportBCRESTexturesFromData (raw, raw_size, dest);
	FREE (raw);
	(void)basedir;
	(void)depth;
	return ERR_NOTHING_TO_DO;
}

static enumError extract_bcres_textures_tree (ccp root, uint depth)
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
			extract_bcres_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_bcres_textures (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// HAL Laboratory "sysdolphin" (HSD) .dat object archives -- Super Smash Bros.
// Melee, Kirby Air Ride and the "TV no Tomo" Wii channel. These carry no magic
// (identification is structural, see IsHSD()), so the file must be read before
// it can be declined; the extension gate keeps that cheap.
//
// Destination: textures land beside the .dat, which is exactly right for a
// file reached by the recursive tree pre-pass -- that tree already lives below
// the requested destination, and extract_tree_complete() deliberately clears
// opt_dest for the whole walk so every asset keeps its path. A .dat named
// directly on the command line has no such tree, so an explicit --dest must be
// honoured instead; opt_dest is only ever set at that top level. Exported PNG
// names carry the source's basename as a prefix, so a shared --dest directory
// cannot collide even for a whole batch of .dat files.
//
// Return value: ERR_OK once the file has been positively identified, else
// ERR_NOTHING_TO_DO. These archives are not directory-style containers, so
// letting the caller's chain continue past a positive identification only ends
// in a bogus "destination already exists" failure from the generic extractor.

static void hal_texture_dest (char *dest, uint dest_size, ccp arg)
{
	if (opt_dest && *opt_dest)
	{
		snprintf (dest, dest_size, "%s", opt_dest);
		uint n = strlen (dest);
		while (n > 1 && dest[n - 1] == '/')
			dest[--n] = 0;
		return;
	}

	snprintf (dest, dest_size, "%s", arg);
	char *slash = strrchr (dest, '/');
	if (slash)
		*slash = 0;
	else
		snprintf (dest, dest_size, ".");
}

static enumError extract_hsd_textures (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dat"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x40 || raw_size > 0x40000000 || !IsHSD (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	hal_texture_dest (dest, sizeof (dest), arg);
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;

	if (!testmode)
	{
		const int n = ExportHSDTexturesFromData (raw, (uint)raw_size, dest, base);
		if (n > 0 && verbose >= 0)
			fprintf (stdlog, "  - %u HSD texture%s extracted from %s -> %s/\n", n,
				n == 1 ? "" : "s", arg, dest);

		// Static JOBJ/DOBJ/POBJ geometry (see lib-hsd.h for scope: no
		// ENVELOPE/SHAPEANIM weighting or material/texture binding yet).
		char glb_path[PATH_MAX];
		snprintf (glb_path, sizeof (glb_path), "%s/%s.glb", dest, base);
		const int nm = ExportHSDModelFromData (raw, (uint)raw_size, glb_path);
		if (nm > 0 && verbose >= 0)
			fprintf (stdlog, "  - HSD model (%d mesh%s) extracted from %s -> %s\n", nm,
				nm == 1 ? "" : "es", arg, glb_path);
	}
	else if (verbose >= 0)
		fprintf (stdlog, "  - WOULD extract HSD textures/model from %s -> %s/\n", arg, dest);
	FREE (raw);
	(void)basedir;
	(void)depth;
	return ERR_OK;
}

static enumError extract_hsd_textures_tree (ccp root, uint depth)
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
			extract_hsd_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_hsd_textures (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// HAL Laboratory "A2" bank archives -- Kirby Air Ride's 2D/UI ".dat" files.
// Same extension as the sysdolphin archives above and equally magic-less, but
// a completely different (and much simpler) named-blob container, so it gets
// its own probe; IsHSD() and IsHALBank() are mutually exclusive on the real
// disc. Destination handling and return value follow extract_hsd_textures()
// above -- see its comment.

static enumError extract_halbank_textures (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dat") && !is_ext (arg, ".usd"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x18 || raw_size > 0x40000000 || !IsHALBank (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	hal_texture_dest (dest, sizeof (dest), arg);
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;

	if (!testmode)
	{
		const int n = ExportHALBankTexturesFromData (raw, (uint)raw_size, dest, base);
		if (n > 0 && verbose >= 0)
			fprintf (stdlog, "  - %u HAL bank texture%s extracted from %s -> %s/\n", n,
				n == 1 ? "" : "s", arg, dest);
	}
	else if (verbose >= 0)
		fprintf (stdlog, "  - WOULD extract HAL bank textures from %s -> %s/\n", arg, dest);
	FREE (raw);
	(void)basedir;
	(void)depth;
	return ERR_OK;
}

static enumError extract_halbank_textures_tree (ccp root, uint depth)
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
			extract_halbank_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_halbank_textures (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}

// HSF has no dedicated "textures only" decoder the way BFRES/BCRES/HSD/
// Halbank do above -- DecodeHSF() decodes its embedded textures to PNG as
// an inseparable side effect of writing the model itself (see lib-hsf.c).
// Deferring the real HSF->GLB/DAE conversion to export_models_tree() (see
// g_in_hsf_deferred_pass) is not enough on its own: SetDAETextureSearchRoot()
// takes its one static snapshot of every PNG under ROOT *before*
// export_models_tree() runs at all, so even a model's own just-decoded
// texture -- written by that very same phase-2 call -- still isn't in the
// index yet when dae_texture_path() looks for it moments later.
//
// So this walker calls extract_hsf_file() early, with g_in_hsf_deferred_pass
// still off, forcing every .hsf to actually convert (and thus decode its
// textures to disk) *before* the search root gets indexed. The GLB/DAE this
// produces is thrown away -- export_models_tree()'s later, properly-indexed
// pass overwrites it with the real, correctly-textured output -- so this
// does mean every real .hsf gets fully decoded twice. Verified worth it on
// a real Calling asset (Ecam0300etc.bin): 282 missed texture bindings in
// one file down to 0 after this pass runs.
static enumError extract_hsf_textures_tree (ccp root, uint depth)
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
			extract_hsf_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_hsf_file (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}

// Finish every extraction below ROOT before exporting any model. A staged
// disc/container can keep a model in one BRRES and its TEX0 in a later sibling
// archive; exporting while extract_tree() is still walking makes COLLADA image
// resolution depend on readdir/archive order. This wrapper is deliberately
// re-entrant: an outer two-pass run sets export_count to zero, so nested
// SARC/PAC/GFA/pass-through containers only decode and the outermost root owns
// the one final texture-index/export pass.
static enumError extract_tree_complete (ccp root, uint depth)
{
	ccp saved_dest = opt_dest;
	opt_dest = 0; // preserve each asset's path; a shared --dest would collide

	if (export_count <= 0)
	{
		enumError err = extract_tree (root, depth);
		opt_dest = saved_dest;
		return err;
	}

	const int saved_export_count = export_count;
	export_count = 0;
	const bool saved_hsf_deferred = g_in_hsf_deferred_pass;
	g_in_hsf_deferred_pass = true;
	enumError err = extract_tree (root, depth);
	g_in_hsf_deferred_pass = saved_hsf_deferred;
	export_count = saved_export_count;

	extract_bfres_textures_tree (root, depth);
	extract_bcres_textures_tree (root, depth);
	extract_hsd_textures_tree (root, depth);
	extract_halbank_textures_tree (root, depth);
	extract_hsf_textures_tree (root, depth);
	SetDAETextureSearchRoot (root);
	const enumError model_err = export_models_tree (root, depth);
	SetDAETextureSearchRoot (0);
	opt_dest = saved_dest;
	if (err < model_err)
		err = model_err;
	return err;
}

// Decode raw Nintendo streams found below an extracted archive.  Sources stay
// in place and their decoded payload is written beside them, which preserves
// the project tree for deterministic archive rebuilds.
static enumError auto_decompress_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	enumError max_err = ERR_OK;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
		{
			max_err = ERR_FILE_TOO_BIG;
			continue;
		}
		struct stat st;
		if (lstat (path, &st))
		{
			if (max_err < ERR_NOT_EXISTS)
				max_err = ERR_NOT_EXISTS;
			continue;
		}
		enumError err = ERR_OK;
		if (S_ISDIR (st.st_mode))
			err = auto_decompress_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			err = decompress_nintendo_file (path);
		if (err != ERR_NOTHING_TO_DO && max_err < err)
			max_err = err;
	}
	closedir (dir);
	return max_err;
}

static void get_extract_dest (char *dest, uint dest_size, const szs_file_t *szs)
{
	ccp pattern = opt_dest;
	if (!pattern)
		switch (szs->fform_arch)
		{
			case FF_U8:
			case FF_WU8:
			case FF_LTA:
			case FF_LFL:
			case FF_RARC:
			case FF_PACK:
			case FF_RKC:
				pattern = "\1P/\1N.d/";
				break;
			default:
				pattern = "\1P/\1F.d/";
				break;
		}
	SubstDest (dest, dest_size, szs->fname, pattern, 0, 0, true);
}

//
///////////////////////////////////////////////////////////////////////////////
