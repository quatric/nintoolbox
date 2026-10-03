#include "lib-pik1.h"
#include "lib-pck2.h"

// Defined in formats.inc, included after this file; forward-declared here
// because create_update.inc calls it directly (see the note near its other
// uses below).
static enumError extract_one_file (ccp arg, ccp basedir, uint depth);

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
		// NOTE: is_ext() in this TU compares from the LAST dot only, so
		// multi-dot sidecar suffixes need explicit suffix compares.
		if (create
			&& ((arg_len > 9 && !strcasecmp (arg + arg_len - 9, ".byml.yml"))
				|| (arg_len > 10 && !strcasecmp (arg + arg_len - 10, ".byml.yaml"))
				|| (arg_len > 10 && !strcasecmp (arg + arg_len - 10, ".byaml.yml"))
				|| (arg_len > 11 && !strcasecmp (arg + arg_len - 11, ".byaml.yaml"))
				|| (arg_len > 10 && !strcasecmp (arg + arg_len - 10, ".sbyml.yml"))
				|| (arg_len > 11 && !strcasecmp (arg + arg_len - 11, ".sbyml.yaml"))))
		{
			char y_dest[PATH_MAX];
			// Strip .yml (preferred) or the older .yaml sidecar suffix.
			const uint strip = is_ext (arg, ".yml") ? 4 : 5;
			char stripped[PATH_MAX];
			snprintf (stripped, sizeof (stripped), "%.*s", (int)(arg_len - strip), arg);
			if (opt_dest)
				SubstDest (y_dest, sizeof (y_dest), arg, opt_dest, 0,
					byml_dest_is_compressed (stripped) ? ".sbyml" : ".byml", false);
			else
				snprintf (y_dest, sizeof (y_dest), "%s", stripped);
			const enumError y_err = encode_byml_file (arg, y_dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BYML %s -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, y_dest);
			if (max_err < y_err)
				max_err = y_err;
			continue;
		}
		const bool is_aamp_sidecar = (arg_len > 9 && !strcasecmp (arg + arg_len - 9, ".aamp.yml"))
			|| (arg_len > 10 && !strcasecmp (arg + arg_len - 10, ".aamp.yaml"))
			|| (arg_len > 10 && !strcasecmp (arg + arg_len - 10, ".baamp.yml"))
			|| (arg_len > 11 && !strcasecmp (arg + arg_len - 11, ".baamp.yaml"))
			|| (arg_len > 11 && !strcasecmp (arg + arg_len - 11, ".bparam.yml"))
			|| (arg_len > 12 && !strcasecmp (arg + arg_len - 12, ".bparam.yaml"))
			|| (arg_len > 10 && !strcasecmp (arg + arg_len - 10, ".bgenv.yml"))
			|| (arg_len > 11 && !strcasecmp (arg + arg_len - 11, ".bgenv.yaml"))
			|| (arg_len > 11 && !strcasecmp (arg + arg_len - 11, ".bglpbd.yml"))
			|| (arg_len > 12 && !strcasecmp (arg + arg_len - 12, ".bglpbd.yaml"));

		if (create && is_aamp_sidecar)
		{
			char y_dest[PATH_MAX];
			if (opt_dest)
				SubstDest (y_dest, sizeof (y_dest), arg, opt_dest, 0, ".aamp", false);
			else
			{
				snprintf (y_dest, sizeof (y_dest), "%s", arg);
				// Strip .yml (preferred) or the older .yaml sidecar suffix.
				y_dest[arg_len - (is_ext (arg, ".yml") ? 4 : 5)] = 0;
			}
			const enumError y_err = encode_aamp_file (arg, y_dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE AAMP %s -> %s\n", verbose > 0 ? "\n" : "",
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
			// Keep editable sources when any nested rebuild remains incomplete.
			if (tree_err)
				continue;

			// Clean up all leftover .d folders and preview/companion artifacts from the tree
			cleanup_extracted_artifacts (source_dir, true, 0);

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
		if (create && ext && (!strcasecmp (ext, ".nsp") || !strcasecmp (ext, ".pfs0") || !strcasecmp (ext, ".hfs0")))
		{
			const bool is_hfs = !strcasecmp (ext, ".hfs0");
			enumError err = CreatePFS0Archive (source_dir, dest, is_hfs);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE %s %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", is_hfs ? "HFS0" : "PFS0", source_dir, dest);
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
		if (create && ext && (!strcasecmp (ext, ".darc") || !strcasecmp (ext, ".bcma")))
		{
			enumError err = create_darc_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE %s %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", !strcasecmp (ext, ".bcma") ? "BCMA" : "DARC",
					source_dir, dest);
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
		if (create && ext && !strcasecmp (ext, ".vff"))
		{
			enumError err = create_vff_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE VFF %s/ -> %s\n", verbose > 0 ? "\n" : "",
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
		if (create && ext && !strcasecmp (ext, ".sir0"))
		{
			enumError err = create_sir0_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE SIR0 %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".zar") || !strcasecmp (ext, ".gar")))
		{
			enumError err = create_gar_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE ZAR %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".arika"))
		{
			enumError err = create_arika_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE ARIKA %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".pk") || !strcasecmp (ext, ".lspk")))
		{
			enumError err = create_lspk_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE LSPK %s/ -> %s\n", verbose > 0 ? "\n" : "",
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
			&& (!strcasecmp (ext, ".dtls") || !strcasecmp (ext, ".ls") || !strcasecmp (ext, ".dt")))
		{
			enumError err = create_dtls_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE DTLS %s/ -> %s\n", verbose > 0 ? "\n" : "",
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
			&& (!strcasecmp (ext, ".glt") || !strcasecmp (ext, ".rlt")
				|| !strcasecmp (ext, ".ptlg")))
		{
			bool is_gc = !strcasecmp (ext, ".glt");
			enumError err = create_ptlg_dir (source_dir, dest, is_gc);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE PTLG (%s) %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", is_gc ? "GameCube" : "Wii", source_dir, dest);
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
		if (create && ext && (!strcasecmp (ext, ".pkg") || !strcasecmp (ext, ".gpkg")))
		{
			enumError err = create_gpkg_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE GPKG %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".gpak"))
		{
			enumError err = create_gpak_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE GPAK %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".pak"))
		{
			// .pak is shared by Call of Duty PAK0 sound archives (CRC-named
			// .dsp members) and 2D Boy GPAK files (ordinal file_%04u members).
			if (looks_like_cod_pak_dir (source_dir))
			{
				enumError err = create_cod_pak_dir (source_dir, dest);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sCREATE COD PAK0 %s/ -> %s\n", verbose > 0 ? "\n" : "",
						testmode ? "WOULD " : "", source_dir, dest);
				if (max_err < err)
					max_err = err;
				if (err <= ERR_WARNING && src_len > 2
					&& !strcasecmp (source_dir + src_len - 2, ".d") && !testmode)
					remove_dir_recursive (source_dir);
			}
			else if (looks_like_gpak_dir (source_dir))
			{
				enumError err = create_gpak_dir (source_dir, dest);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sCREATE GPAK %s/ -> %s\n", verbose > 0 ? "\n" : "",
						testmode ? "WOULD " : "", source_dir, dest);
				if (max_err < err)
					max_err = err;
				if (err <= ERR_WARNING && src_len > 2
					&& !strcasecmp (source_dir + src_len - 2, ".d") && !testmode)
					remove_dir_recursive (source_dir);
			}
			else
			{
				const enumError err = ERROR0 (
					ERR_INVALID_DATA, "No PAK0 or GPAK members found in: %s\n", source_dir);
				if (max_err < err)
					max_err = err;
			}
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".vcra"))
		{
			enumError err = create_vcra_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE VCRA %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".sharc") || !strcasecmp (ext, ".sharcfb"))
			&& LooksLikeSHARCDir (source_dir))
		{
			const bool fb = !strcasecmp (ext, ".sharcfb");
			enumError err = fb ? CreateSHARCFBFromDir (source_dir, dest)
							   : CreateSHARCFromDir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE %s %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", fb ? "SHARCFB" : "SHARC", source_dir, dest);
			if (max_err < err)
				max_err = err;
			// compiling keeps the directory: it is the editable SHARC source
			if (!fb && err <= ERR_WARNING && src_len > 2
				&& !strcasecmp (source_dir + src_len - 2, ".d") && !testmode)
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
		if (create && ext
			&& (!strcasecmp (ext, ".dat") || !strcasecmp (ext, ".rflres")
				|| !strcasecmp (ext, ".rfl") || !strcasecmp (ext, ".fflres")
				|| !strcasecmp (ext, ".ffl") || !strcasecmp (ext, ".cflres")
				|| !strcasecmp (ext, ".cfl") || !strcasecmp (ext, ".aflres")
				|| !strcasecmp (ext, ".afl") || !strcasecmp (ext, ".nflres")
				|| !strcasecmp (ext, ".nfl") || !strcasecmp (ext, ".nfl_res"))
			&& looks_like_rflres_dir (source_dir))
		{
			const bool be = strcasecmp (ext, ".cfl") && strcasecmp (ext, ".cflres")
				&& strcasecmp (ext, ".afl") && strcasecmp (ext, ".aflres")
				&& strcasecmp (ext, ".nfl") && strcasecmp (ext, ".nflres")
				&& strcasecmp (ext, ".nfl_res");
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
		if (create && ext && !strcasecmp (ext, ".bea") && looks_like_bea_dir (source_dir))
		{
			enumError err = create_bea_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BEA %s/ -> %s\n", verbose > 0 ? "\n" : "",
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
		if (create && ext && (!strcasecmp (ext, ".nus3audio") || !strcasecmp (ext, ".nus3bank")))
		{
			enumError err = create_nus3audio_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE NUS3AUDIO %s/ -> %s\n", verbose > 0 ? "\n" : "",
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
			enumError err = create_rst_dir (source_dir, dest, !OptionUsed[OPT_NO_COMPRESS]);
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
		if (create && ext
			&& (!strcasecmp (ext, ".byml") || !strcasecmp (ext, ".byaml")
				|| byml_dest_is_compressed (dest)))
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
		if (create && ext && (!strcasecmp (ext, ".bglpbd") || (is_ext (dest, "bglpbd.szs")))
			&& (is_ext (arg, ".bfres") || is_ext (arg, ".szs") || is_ext (arg, ".glb")
				|| is_ext (arg, ".dae")))
		{
			// Generating AGL light probes from course model bounds
			u8 *mdata = NULL;
			size_t msize = 0;
			enumError err = LoadFileAlloc (arg, 0, 0, &mdata, &msize, 0, 0, 0, false);
			if (!err && mdata)
			{
				model_t *mdl = NULL;
				bool is_sw = is_ext (dest, ".szs");
				if (msize >= 4 && !memcmp (mdata, "FRES", 4))
				{
					mdl = ParseBFRES (mdata, msize);
					if (!mdl)
					{
						mdl = ParseBFRESSwitch (mdata, msize);
						if (mdl)
							is_sw = true;
					}
				}
				else if (is_ext (arg, ".glb") || is_ext (arg, ".dae"))
				{
					mdl = ParseGLB (mdata, msize);
				}

				if (mdl)
				{
					bglpbd_file_t pfile;
					err = CreateBGLPBD_FromModel (&pfile, mdl, NULL, is_sw);
					if (!err)
					{
						u8 *bin = NULL;
						size_t bin_sz = 0;
						err = EncodeBGLPBD (&pfile, &bin, &bin_sz);
						if (!err && bin)
						{
							szs_file_t out_szs;
							InitializeSZS (&out_szs);
							out_szs.data = bin;
							out_szs.size = bin_sz;
							err = SaveSZS (&out_szs, dest, opt_overwrite > 0, is_sw);
							out_szs.data = NULL;
							ResetSZS (&out_szs);
							FREE (bin);
						}
					}
					ResetBGLPBD (&pfile);
					FreeModel (mdl);
				}
				FREE (mdata);
			}
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BGLPBD (Model) %s -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && (!strcasecmp (ext, ".bglpbd") || !strcasecmp (ext, ".szs"))
			&& is_ext (arg, ".txt"))
		{
			// Importing Unity light probe text file to bglpbd
			u8 *tdata = NULL;
			size_t tsize = 0;
			enumError err = LoadFileAlloc (arg, 0, 0, &tdata, &tsize, 0, 0, 0, false);
			if (!err && tdata)
			{
				bglpbd_file_t pfile;
				bool is_sw
					= !strcasecmp (ext, ".szs") || (opt_dest && strcasestr (opt_dest, ".szs"));
				err = CreateBGLPBD_FromUnityText (&pfile, (const char *)tdata, tsize, NULL, is_sw);
				if (!err)
				{
					u8 *bin = NULL;
					size_t bin_sz = 0;
					err = EncodeBGLPBD (&pfile, &bin, &bin_sz);
					if (!err && bin)
					{
						szs_file_t out_szs;
						InitializeSZS (&out_szs);
						out_szs.data = bin;
						out_szs.size = bin_sz;
						err = SaveSZS (&out_szs, dest, opt_overwrite > 0, is_sw);
						out_szs.data = NULL;
						ResetSZS (&out_szs);
						FREE (bin);
					}
				}
				ResetBGLPBD (&pfile);
				FREE (tdata);
			}
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE BGLPBD (Unity TXT) %s -> %s\n",
					verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
			if (max_err < err)
				max_err = err;
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext
			&& (!strcasecmp (ext, ".aamp") || !strcasecmp (ext, ".baamp")
				|| !strcasecmp (ext, ".bparam") || !strcasecmp (ext, ".bgenv")
				|| !strcasecmp (ext, ".bglpbd")))
		{
			enumError err = encode_aamp_file (arg, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE AAMP %s -> %s\n", verbose > 0 ? "\n" : "",
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
		if (create && ext && !strcasecmp (ext, ".zdat"))
		{
			enumError err = create_zdat_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE ZDAT %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".rzpk"))
		{
			enumError err = create_rzpk_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE RZPK %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
			ResetSetupParam (&sp);
			continue;
		}
		if (create && ext && !strcasecmp (ext, ".one"))
		{
			enumError err = create_one_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE ONE %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
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
			enumError err = create_bin_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE %s %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", ndcube_pac_dest (dest) ? "PAC (NDCUBE)" : "MPBIN",
					source_dir, dest);
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

		if (create && ext && (!strcasecmp (ext, ".eff") || !strcasecmp (ext, ".effn")))
		{
			enumError err = create_effn_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE EFFN %s/ -> %s\n", verbose > 0 ? "\n" : "",
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
			&& (!strcasecmp (ext, ".ptcl") || !strcasecmp (ext, ".vfxb")
				|| !strcasecmp (ext, ".eset")))
		{
			enumError err = create_pctl_dir (source_dir, dest);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sCREATE VFXB %s/ -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", source_dir, dest);
			if (max_err < err)
				max_err = err;
			if (err <= ERR_WARNING && src_len > 2 && !strcasecmp (source_dir + src_len - 2, ".d")
				&& !testmode)
				remove_dir_recursive (source_dir);
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
	if (!path)
		return false;
	// Control characters are never a real member name, and Windows refuses
	// them in file names outright ("Invalid argument"). Seen as misread RST
	// names ("\x02", twice) in Excitebike: World Rally's common.res, where
	// the duplicate then aborted the rest of the archive on every platform.
	for (const u8 *p = (const u8 *)path; *p; p++)
		if (*p < 0x20 || *p == 0x7f)
			return false;
	const size_t plen = strlen (path);
	if (plen >= 3 && !strcmp (path + plen - 3, "/.."))
		return false;
	if (strchr (path, ':'))
		return false;
	return *path && path[0] != '/' && !strchr (path, '\\') && strncmp (path, "../", 3)
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
	if (!path || !*path || path[0] == '/' || path[0] == '\\' || !valid_utf8 (path)
		|| strchr (path, ':'))
		return false;
	if (!strcmp (path, ".") || !strcmp (path, ".."))
		return false;
	const size_t plen = strlen (path);
	if (plen >= 3 && (!strcmp (path + plen - 3, "/..") || !strcmp (path + plen - 3, "\\..")))
		return false;
	if (strstr (path, "/../") || strstr (path, "\\..\\") || strstr (path, "\\../")
		|| strstr (path, "/..\\"))
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
