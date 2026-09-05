
static const sizeof_info_t *sizeof_info_szs[] = { sizeof_info_szs_list, sizeof_info_bmg, 0 };

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command test			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_test_options ()
{
	printf ("\nOptions (compatibility: %s; format: hex=dec):\n", PrintOptCompatible ());

	printf ("  test:        %16x = %12d\n", testmode, testmode);
	printf ("  verbose:     %16x = %12d\n", verbose, verbose);
	printf ("  warn modes:  %16llx = \"%s\"\n", (u64)WARN_MODE, GetWarnMode ());
	printf ("  force:       %16x = %12d (kmp=%d)\n", force_count, force_count, force_kmp);
	printf ("  sort:        %16x = %12d\n", opt_sort, opt_sort);
	printf ("  width:       %16x = %12d\n", opt_width, opt_width);
	printf ("  escape-char: %16x = %12d\n", escape_char, escape_char);
	printf ("  align:       %16x = %12d\n", opt_align, opt_align);
	printf ("  align-u8:    %16x = %12d\n", opt_align_u8, opt_align_u8);
	printf ("  align-lta:   %16x = %12d\n", opt_align_lta, opt_align_lta);
	printf ("  align-pack:  %16x = %12d\n", opt_align_pack, opt_align_pack);
	printf ("  align-brres: %16x = %12d\n", opt_align_brres, opt_align_brres);
	printf ("  align-breff: %16x = %12d\n", opt_align_breff, opt_align_breff);
	printf ("  align-breft: %16x = %12d\n", opt_align_breft, opt_align_breft);
	printf ("  tiny:        %16x = %12d\n", opt_tiny, opt_tiny);
	printf ("  compression:          mode %2d, level %d ==> %d\n", opt_compr_mode, opt_compr,
		GetComprByFF (fform_compr, 0));
	printf ("  recurse:     %16x = %12d\n", opt_recurse, opt_recurse);
	printf ("  cmpr-default:         valid=%d, RGB565: %04x %04x, RGB: %06x %06x\n", opt_cmpr_valid,
		be16 (opt_cmpr_def), be16 (opt_cmpr_def + 2), RGB565_to_RGB (be16 (opt_cmpr_def)),
		RGB565_to_RGB (be16 (opt_cmpr_def + 2)));
	printf ("  n-mipmaps:   %16x = %12d\n", opt_n_images - 1, opt_n_images - 1);
	printf ("  max-mipmaps: %16x = %12d\n", opt_max_images - 1, opt_max_images - 1);
	printf ("  mipmap-size: %16x = %12d\n", opt_min_mipmap_size, opt_min_mipmap_size);
	printf ("  mipmaps:     %16x = %12d\n", opt_mipmaps, opt_mipmaps);
	printf ("  fast-mipmaps:%16x = %12d\n", fast_resize_enabled, fast_resize_enabled);
	printf ("  analyse-mode:%16x = %12d\n", opt_analyze_mode, opt_analyze_mode);
	printf ("  set-flags:   %16x = %12d\n", set_flags, set_flags);
	printf ("  kcl modes:   %16llx = \"%s\"\n", (u64)KCL_MODE, GetKclMode ());

	printf ("  kmp modes:   %16llx = \"%s\"\n", (u64)KMP_MODE, GetKmpMode ());
	printf ("  kmp tform:   %16x = \"%s\"\n", KMP_TFORM, GetKmpTform ());
	if (speed_mod_active)
		printf ("  kmp speed modifier:      %04x ~ %5.3f\n", speed_mod_val, speed_mod_factor);
	if (opt_n_laps)
		printf ("  kmp:stgi laps:%15x = %12d\n", opt_n_laps, opt_n_laps);
	printf ("  mdl modes:   %16x = \"%s\"\n", MDL_MODE, GetMdlMode ());
	printf ("  pat modes:   %16x = \"%s\"\n", PAT_MODE, GetPatMode ());

	if (have_patch_count)
		printf ("  patch count: %16x = %12d\n", have_patch_count, have_patch_count);
	printf ("  patch files: %16x = \"%s\"\n", PATCH_FILE_MODE, GetFileClassInfo ());
	if (opt_slot)
		printf ("  slot:        %16x = \"%s\"\n", opt_slot, PrintSlotMode (opt_slot));

	DumpTransformationOpt ();

	if (opt_tracks)
		DumpTrackList (0, 0, 0);
	if (opt_arenas)
		DumpArenaList (0, 0, 0);

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_test ()
{
	SetPatchFileModeReadonly ();

#if 1 || !defined(TEST) // test options

	return cmd_test_options ();

#elif 1

	u8 *p1 = MALLOC (10);
	u8 *p2 = CALLOC (10, 20);
	u8 *p3 = MALLOC (3000);
	DUMP_TRACE_ALLOC (stderr);
	p1[-1] = 'a';
	FREE (p1);
	FREE (p2);
	FREE (p3);
	DUMP_TRACE_ALLOC (stderr);
	return ERR_OK;

#elif 1

	ParamList_t *param;
	for (param = first_param; param; param = param->next)
	{
		SetupParam_t sp;
		InitializeSetupParam (&sp);
		enumError err = ScanSetupParam (&sp, true, param->arg, 0, 0, false);
		printf ("err=%d, ff=%s, pt-dir=%d : %s\n", err, GetNameFF (sp.fform_file, sp.fform_arch),
			sp.have_pt_dir, param->arg);
		ResetSetupParam (&sp);
	}
	return ERR_OK;

#elif 1

	const u64 base = 0x0807060504030201ull;
	u64 val;
	char buf[16];

	const endian_func_t *endian = &be_func;
	for (;;)
	{
		putchar ('\n');

		memset (buf, 0, sizeof (buf));
		endian->wr16 (buf, base);
		HexDump16 (stdout, 0, 0x16, buf, sizeof (buf));
		val = endian->rd16 (buf);
		ASSERT_MSG (val == (u16)base, "%llx %llx\n", val, base);

		memset (buf, 0, sizeof (buf));
		endian->wr24 (buf, base);
		HexDump16 (stdout, 0, 0x24, buf, sizeof (buf));
		val = endian->rd24 (buf);
		ASSERT_MSG (val == ((u32)base & 0xffffff), "%llx %llx\n", val, base);

		memset (buf, 0, sizeof (buf));
		endian->wr32 (buf, base);
		HexDump16 (stdout, 0, 0x32, buf, sizeof (buf));
		val = endian->rd32 (buf);
		ASSERT_MSG (val == (u32)base, "%llx %llx\n", val, base);

		memset (buf, 0, sizeof (buf));
		endian->wr48 (buf, base);
		HexDump16 (stdout, 0, 0x48, buf, sizeof (buf));
		val = endian->rd48 (buf);
		ASSERT_MSG (val == (base & 0xffffffffffff), "%llx %llx\n", val, base);

		memset (buf, 0, sizeof (buf));
		endian->wr64 (buf, base);
		HexDump16 (stdout, 0, 0x64, buf, sizeof (buf));
		val = endian->rd64 (buf);
		ASSERT_MSG (val == base, "%llx %llx\n", val, base);

		if (endian == &le_func)
			break;
		endian = &le_func;
	}
	putchar ('\n');
	return ERR_OK;

#elif 1

	ParamList_t *param;
	for (param = first_param; param; param = param->next)
	{
		ccp arg = param->arg;
		char buf[1000];
		ccp subpath = SplitSubPath (buf, sizeof (buf), arg);
		if (!subpath)
			printf ("NOT FOUND: |%s|\n", buf);
		else if (!*subpath)
			printf ("REAL FILE: |%s|\n", buf);
		else
			printf ("SUB PATH:  |%s|%s|\n", buf, subpath);
	}
	return ERR_OK;

#endif
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command ui-check		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError cmd_ui_check ()
{
	SetupPager ();

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	const bool have_dump = long_count >= 1;
	const bool have_header = !have_dump && !brief_count && print_header;

	int sep_len = 0;
	if (have_header)
	{
		int fw_file = 4;
		ccp *ptr = plist.field, *end;
		for (end = ptr + plist.used; ptr < end; ptr++)
		{
			const int fw = strlen8 (*ptr);
			if (fw_file < fw)
				fw_file = fw;
		}
		sep_len = (fw_file + 23) * 3;
		printf ("\n%s Type       L Korean  File\n%s%.*s%s\n", colset->heading, colset->heading,
			sep_len, ThinLine300_3, colset->reset);
	}

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadSZS (&szs, arg, false, opt_ignore > 0, true);
		if (err > ERR_WARNING || err == ERR_NOT_EXISTS)
			continue;

		DecompressSZS (&szs, true, 0);

		if (have_dump)
		{
			u_usec_t dur = -GetTimerUSec ();
			ui_check_t uc;
			UiCheck (&uc, &szs);
			dur += GetTimerUSec ();

			printf ("%c%c ",
				uc.is_ui >= OFFON_ON	   ? 'U'
					: uc.is_ui > OFFON_OFF ? 'u'
										   : '-',
				uc.is_korean >= OFFON_ON	   ? 'K'
					: uc.is_korean > OFFON_OFF ? 'k'
											   : '-');

			for (ui_type_t t = 1; t < UIT__N; t++)
			{
				uint mask = 1 << t;
				putchar (uc.possible & mask ? ui_type_char[t] : '-');
			}

			printf (" %c%c %6lluµs  %s\n", ui_type_char[uc.ui_type], uc.ui_lang, dur, arg);
		}
		else
		{
			ui_check_t uc;
			UiCheck (&uc, &szs);

			ccp col = GetUiTypeColor (colout, uc.type);
			printf ("%s %-10s %c %-6s  %s%s\n", col, ui_type_name[uc.ui_type], uc.ui_lang,
				uc.is_korean > OFFON_AUTO ? "Korean" : "-", arg, *col ? colout->reset : "");
		}
	}

	if (have_header)
	{
		if (plist.used >= 3)
			printf ("%s%.*s%s\n\n", colset->heading, sep_len, ThinLine300_3, colset->reset);
		else
			putchar ('\n');
	}

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command autoadd			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError exec_autoadd (UsedFileFILE_t *used, // vector with used files
	ccp source // filename of source file
)
{
	DASSERT (used);
	DASSERT (source);
	PRINT ("exec_autoadd() sizeof(used)==%zu\n", sizeof (*used));

	char path_buf[PATH_MAX];

	szs_file_t szs;
	InitializeSZS (&szs);
	enumError err = LoadSZS (&szs, source, true, opt_ignore > 0, true);

	if (verbose >= 0 || testmode)
	{
		fprintf (
			stdlog, "%sANALYZE %s:%s\n", verbose > 0 ? "\n" : "", GetNameFF_SZS (&szs), szs.fname);
		fflush (stdlog);
	}

	if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
	{
		CollectFilesSZS (&szs, true, 0, -1, SORT_NONE);

		int idx;
		for (idx = 0; idx < N_DB_FILE_FILE; idx++)
		{
			if (used->d[idx])
				continue;

			const DbFileFILE_t *ptr = DbFileFILE + idx;
			if (!DBF_ARCH_SUPPORT (ptr->flags))
				continue;

			szs_subfile_t *file, *file_end = szs.subfile.list + szs.subfile.used;
			for (file = szs.subfile.list; file < file_end; file++)
			{
				ccp fpath = file->path;
				if (fpath[0] == '.' && fpath[1] == '/')
					fpath += 2;
				if (!strcmp (fpath, ptr->file))
				{
					used->d[idx] = 2;
					ccp path = PathCatPP (path_buf, sizeof (path_buf), opt_dest, ptr->file);
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "  %sADD %7u, %s:%s\n", testmode ? "WOULD " : "",
							file->size, GetNameFF (0, ptr->fform), path);

					File_t F;
					CreateFileOpt (&F, true, path, testmode, 0);
					if (F.f)
					{
						if (opt_preserve)
							SetFileAttrib (&F.fatt, &szs.fatt, 0);
						size_t wstat = fwrite (szs.data + file->offset, 1, file->size, F.f);
						if (wstat != file->size)
							err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n",
								file->size, path);
					}
					ResetFile (&F, opt_preserve);
				}
			}
		}
		fflush (stdout);
	}

	ResetSZS (&szs);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError find_autoadd (UsedFileFILE_t *used, // vector with used files
	ccp source_dir, // filename of source directory
	ccp name, // name of while without extension
	ccp slot // NULL or slot prefix
)
{
	DASSERT (used);
	DASSERT (source_dir);
	DASSERT (name);

	struct stat st;
	char pathbuf[PATH_MAX], fname[50];

	int
	try
		;
	for (try = 0;; try ++)
	{
		switch (try)
		{
			case 0:
				snprintf (fname, sizeof (fname), "%s", name);
				break;

			case 1:
				snprintf (fname, sizeof (fname), "%s_d", name);
				break;

			case 2:
				if (!slot)
					*fname = 0;
				else
					snprintf (fname, sizeof (fname), "%s-%s", slot, name);

				break;

			case 3:
				if (!slot)
					*fname = 0;
				else
					snprintf (fname, sizeof (fname), "%s-%s_d", slot, name);
				break;

			default:
				return ERR_OK;
		}

		if (*fname)
		{
			ccp path = PathCatPPE (pathbuf, sizeof (pathbuf), source_dir, fname, ".szs");
			PRINT ("-> %s\n", path);
			if (!stat (path, &st) && S_ISREG (st.st_mode))
				return exec_autoadd (used, path);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_autoadd ()
{
	SetPatchFileModeReadonly ();

	char path_buf[PATH_MAX];
	const config_t *config = GetConfig ();
	CheckOptDest (config->autoadd_path, true);

	if (!opt_mkdir && !ExistDirectory (opt_dest, 0))
		return ERROR0 (ERR_CANT_CREATE_DIR, "Directory does not exist: %s\n", opt_dest);
	opt_mkdir = true;
	autoadd_destination = opt_dest;

	if (!n_param)
		opt_overwrite = false;

	if (!n_param || verbose >= 0)
		fprintf (stdlog, "\nCURRENT AUTO-ADD PATH: %s\n", opt_dest);

	if (!n_param || verbose >= 1)
	{
		const StringField_t *sf = GetAutoaddList ();
		int i;
		ccp *str = sf->field;
		for (i = 0; i < sf->used; i++, str++)
			fprintf (stdlog, "       SEARCH PATH[%d]: %s\n", i, *str);
	}

	//--- create file list

	UsedFileFILE_t used;
	memset (&used, 0, sizeof (used));
	uint n_invalid = 0;

	if (!opt_overwrite || !n_param)
	{
		if (verbose >= 2)
			fprintf (stdlog, "\nLIST OF FILES IN %s\n", opt_dest);

		int count = 0;
		const DbFileFILE_t *ptr;
		for (ptr = DbFileFILE; ptr->file; ptr++)
			if (DBF_ARCH_SUPPORT (ptr->flags))
			{
				ccp path = PathCatPP (path_buf, sizeof (path_buf), opt_dest, ptr->file);
				struct stat st;
				if (!stat (path, &st) && S_ISREG (st.st_mode))
				{
					used.d[ptr - DbFileFILE] = 1;
					if (verbose >= 2)
					{
						count++;
						fprintf (stdlog, "  %-5s %s\n", GetNameFF (0, ptr->fform), ptr->file);
					}

					if (verbose >= 1)
					{
						DASSERT (ptr->ref < N_DB_FILE_REF_FILE);
						const s16 ref = DbFileRefFILE[ptr->ref];
						noPRINT ("%zd -> %d/%d : %d : %s\n", ptr - DbFileFILE, ptr->ref, ptr->group,
							ref, ptr->file);
						if (ref >= 0)
						{
							DASSERT (ref < N_DB_FILE);
							const DbFile_t *db = DbFile + ref;
							DASSERT (db->sha1 < N_DB_FILE_SHA1);
							const u8 *sha1 = DbFileSHA1[db->sha1].sha1;

							sha1_size_hash_t ss;
							GetSSByFile (&ss, path, 0);
							if (memcmp (sha1, ss.hash, sizeof (ss.hash)))
							{
								n_invalid++;
								used.d[ptr - DbFileFILE] = 2;
								HexDump (stdout, 0, 0, 4, 20, sha1, 20);
								HexDump (stdout, 0, 0, 4, 20, ss.hash, 20);
							}
						}
					}
				}
			}
		if (count)
			fprintf (stdlog, "  => %u of %zu files found.\n", count, sizeof (used));
	}

	//--- iterate parameters

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		if (!ExistDirectory (arg, 0))
		{
			enumError err = exec_autoadd (&used, arg);
			if (max_err < err)
				max_err = err;
		}
		else
		{
			const DbFileSZS_t *db;
			for (db = DbFileSZS; db->szs_name; db++)
				if ((db->type & (FLT_OLD | FLT_STD)) == FLT_STD)
					find_autoadd (&used, arg, db->szs_name, db->idx < 0 ? 0 : db->id + 1);
		}
	}

	ResetStringField (&plist);

	//--- print missing list

	if (verbose >= 1)
	{
		fprintf (stdlog, "\nLIST OF MISSED FILES IN %s\n", opt_dest);

		int count = 0;
		const DbFileFILE_t *ptr;
		for (ptr = DbFileFILE; ptr->file; ptr++)
			if (!used.d[ptr - DbFileFILE] && DBF_ARCH_SUPPORT (ptr->flags))
			{
				count++;
				fprintf (stdlog, "  %-5s %s\n", GetNameFF (0, ptr->fform), ptr->file);
			}
		if (count)
			fprintf (stdlog, "  => %u of %zu files missed.\n", count, sizeof (used));
	}

	//--- print invalid list

	if (n_invalid)
	{
		fprintf (stdlog, "\nLIST OF INVALID FILES IN %s\n", opt_dest);

		const DbFileFILE_t *ptr;
		for (ptr = DbFileFILE; ptr->file; ptr++)
			if (used.d[ptr - DbFileFILE] == 2)
				fprintf (stdlog, "  %-5s %s\n", GetNameFF (0, ptr->fform), ptr->file);
		fprintf (stdlog, "  => %u of %zu files invalid.\n", n_invalid, sizeof (used));
	}

	if (verbose >= 1)
		fputs ("\n", stdlog);

	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command files			///////////////
///////////////////////////////////////////////////////////////////////////////

static int files_func (ccp fname, // name of current file without '.szs' extension
	ccp id, // unique ID of current file
	FileList_t mode, // mode of current file
	const TrackInfo_t *ti, // NULL or related track info
	void *param // user defined parameter
)
{
	char name[BMG_MSG_BUF_SIZE];
	name[0] = 0;
	const bmg_item_t *bi = 0;

	if (ti && opt_load_bmg.item_used)
	{
		bi = FindItemBMG (&opt_load_bmg, ti->track_id + MID_LE_TRACK_BEG);
		if (!bi || !bi->len)
		{
			bi = FindItemBMG (&opt_load_bmg, ti->track_id + MID_CT_TRACK_BEG);
			if (!bi || !bi->len)
			{
				bi = FindItemBMG (&opt_load_bmg, ti->bmg_mid);
				if (bi && !bi->len)
					bi = 0;
			}
		}

		if (bi)
		{
			PrintString16BMG (name, sizeof (name) - 10, bi->text, bi->len, BMG_UTF8_MAX, 0, 0);
			if (!strchr (name, '['))
			{
				const uint pos = strlen (name);
				snprintf (name + pos, sizeof (name) - pos, " [%c%02u]",
					mode & FLT_ARENA ? 'a' : 'r', ti->def_slot);
			}
		}
	}

	if (pipe_count)
	{
		if (verbose > 0)
			printf ("%s|0x%02x|%s|%s\n", id, mode, fname, name);
		else
			printf ("%s|%s|%s\n", id, fname, name);
	}
	else
	{
		if (verbose > 0)
			printf ("%-5s 0x%02x  ", id, mode);
		else
			printf ("%-5s ", id);

		if (*name)
			printf ("%-21s %s\n", fname, name);
		else
			printf ("%s\n", fname);
	}
	return 1;
}

//-----------------------------------------------------------------------------

static enumError cmd_tracks ()
{
	SetPatchFileModeReadonly ();

	u32 mode = 0;
	if (first_param)
	{
		ScanNumU32 (first_param->arg, 0, &mode, 0, FLT_M_ALL);
		mode &= FLT_M_ALL;
	}

	if (!mode)
	{
		mode = FLT_TRACK | FLT_ARENA | FLT_STD;

		if (all_count > 1)
			mode = FLT_M_ALL;
		else if (all_count)
			mode |= FLT_D;

		if (long_count)
		{
			mode |= FLT_OTHER;
			if (long_count > 1)
				mode |= FLT_OLD;
		}
	}

	IterateTrackFiles (mode, files_func, 0);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command scancache		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_scancache ()
{
	if (opt_cache)
		AddParam (opt_cache);

	if (n_param != 1)
		return ERROR0 (ERR_SYNTAX, "Exact one parameter (directory) expected.\n");

	szs_cache_dir = first_param->arg;
	if (!IsDirectory (szs_cache_dir, false))
		return ERROR0 (ERR_SEMANTIC, "Directory expected: %s\n", szs_cache_dir);

	bool force = false;
	parallel_count = 0;
	if (opt_fast)
	{
		LoadSZSCache ();
		force = true;
	}
	else
		ScanSZSCache (first_param->arg, opt_purge);

	return SaveSZSCache (force);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command export			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_export ()
{
	SetPatchFileModeReadonly ();
	DefineDefaultParserFunc ();
	return ExportHelper ("");
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command sizeof			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_sizeof ()
{
	sizeof_info_order_t order;
	switch (GetSortMode (opt_sort, 0, SORT_NONE))
	{
		case SORT_INAME:
			order = SIZEOF_ORDER_NAME;
			break;
		case SORT_SIZE:
			order = SIZEOF_ORDER_SIZE;
			break;
		default:
			order = SIZEOF_ORDER_NONE;
			break;
	}

	SetupPager ();
	PrintMode_t pm = { .fout = stdout, .cout = colout, .debug = logging };
	ArgManager_t am = { .force_case = LOUP_LOWER };
	for (ParamList_t *param = first_param; param; param = param->next)
		AppendArgManager (&am, param->arg, 0, false);

	const sizeof_info_t **si_list;
	if (brief_count)
		si_list = sizeof_info_szs;
	else
	{
		AddListToSizeofInfoMgr (sizeof_info_szs);
		si_list = GetSizeofInfoMgrList ();
	}

	ListSizeofInfo (&pm, si_list, &am, order);

	ClosePager ();
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _CODE			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_code ()
{
	SetPatchFileModeReadonly ();
	stdlog = stderr;

	if (!n_param)
		AddParam ("-");

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		if (verbose)
			fprintf (stderr, "CODE %s\n", arg);
		File_t F;
		if (!OpenFILE (&F, true, arg, false, true))
		{
			while (!feof (F.f) && !ferror (F.f))
			{
				uint size = fread (iobuf, 1, sizeof (iobuf), F.f);
				u8 *src = (u8 *)iobuf, *end = src + size;
				while (src < end)
					*src++ ^= 0xdc;
				fwrite (iobuf, 1, size, stdout);
			}
		}
		ResetFile (&F, false);
	}

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _RECODE			///////////////
///////////////////////////////////////////////////////////////////////////////

#ifndef HAVE_WIIMM_EXT

enumError cmd_recode ()
{
	ERROR0 (ERR_NOT_IMPLEMENTED, "Command _RECODE not implemented in this version!\n");
	ExitFixed (ERR_NOT_IMPLEMENTED);
}

#endif

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _SUBFILE		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError cmd_subfile ()
{
	if (!first_param)
		return ERR_OK;

	SubDir_t dir;
	InitializeSubDir (&dir);

	NORMALIZE_FILENAME_PARAM (first_param);

	szs_file_t szs;
	InitializeSZS (&szs);
	enumError err = LoadSZS (&szs, first_param->arg, false, opt_ignore > 0, true);
	if (!err)
	{
		DecompressSZS (&szs, true, 0);
#ifdef TEST
		{
			printf ("SZS: %p %zu / %p %zu\n", szs.cdata, szs.csize, szs.data, szs.size);

			DumpInfoContainer (stdlog, collog, 2, "SZS:  ", &szs.container, 0x20);
			szs_file_t szs2;
			InitializeSubSZS (&szs2, &szs, 4, 12, FF_UNKNOWN, 0, false);
			printf ("SZS2: %p %zu / %p %zu\n", szs2.cdata, szs2.csize, szs2.data, szs2.size);
			DumpInfoContainer (stdlog, collog, 2, "SZS1: ", &szs.container, 0x20);
			DumpInfoContainer (stdlog, collog, 2, "SZS2: ", &szs2.container, 0x20);
			ResetSZS (&szs2);
			DumpInfoContainer (stdlog, collog, 2, "SZS:  ", &szs.container, 0x20);
		}
#endif
		DASSERT (!szs.file_size || szs.file_size >= szs.size);
		have_patch_count -= 1000000;
		PRINT ("EXTRACT/%s[%s]: %s\n", __FUNCTION__, GetNameFF_SZS (&szs), szs.fname);
		err = ExtractFilesSZS (&szs, 0, false, &dir, 0);
		have_patch_count += 1000000;

		if (opt_dest && *opt_dest)
		{
			DumpSubDir (stdout, 2, &dir);
			SaveSubFiles (&dir, opt_dest);
		}

		SubFile_t *sf = FindSubFile (&dir, MemByString ("3DModels(NW4R)/course"));
		if (sf)
		{
			printf ("** %s found [%p,0x%x] **\n", sf->fname, sf->data, sf->size);
#if 1
			// ccp err = RenameForSlot42MDL(sf->data,sf->size,ContainerSZS(&szs));
			ccp err = RenameForSlot42MDL (sf->data, sf->size, 0);
			if (err)
				printf ("\e[35;1m %s \e[0m\n", err);
			printf (">> SAVE %p,0x%x\n", sf->data, sf->size);
			SaveFile ("_mdl.tmp", 0, FM_OVERWRITE | FM_TOUCH, sf->data, sf->size, 0);
#else
			mdl_t mdl;
			ScanMDL (&mdl, true, sf->data, sf->size, ContainerSZS (&szs), 0);
			SetupSlot42Materials ();
			AppendMDL (&mdl, &slot42_mdl);
			SaveRawMDL (&mdl, "_mdl.tmp", false);
			ResetMDL (&mdl);
#endif
		}

		szs_file_t szs2;
		InitializeSZS (&szs2);
		enumError err = CreateSZS (&szs2, 0, 0, &dir, 0, 0, verbose > 0 ? UINT_MAX : 0, false);
		if (!err)
			SaveSZS (&szs2, "_szs.tmp", true, false);
		ResetSZS (&szs2);
	}
	ResetSubDir (&dir);
	ResetSZS (&szs);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _TESTNORM		///////////////
///////////////////////////////////////////////////////////////////////////////

void PrintNorm (const szs_norm_t *norm)
{
	if (!norm || !norm->f)
		return;
}

///////////////////////////////////////////////////////////////////////////////

void SetupNormByOptions (szs_norm_t *norm)
{
	DASSERT (norm);
	memset (norm, 0, sizeof (*norm));
	memset (norm->modified, '-', SZI__N);
}

///////////////////////////////////////////////////////////////////////////////

enumError cmd_testnorm ()
{
	if (!first_param)
		return ERR_OK;

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
///////////////			command BRSUB			///////////////
///////////////////////////////////////////////////////////////////////////////
// [[check_brsub_t]]

typedef struct check_brsub_t
{
	szs_file_t *szs; // pointer to valid SZS
	int mode; // 0:warnings, 1:+hints, 2:+info

	bool header_printed; // true: header with filename printed
	bool print_sep; // true: print separator
	int sep_fw; // field width of separator
	uint sep_count; // number of printed lines after last sep

	enumError max_err; // error code
	ccp brres_path; // path of current BRRES
	uint line_count; // number of printed status lines
} check_brsub_t;

///////////////////////////////////////////////////////////////////////////////

static int check_brsub (struct szs_iterator_t *it, // iterator struct with all infos
	bool term // true: termination hint
)
{
	DASSERT (it);
	check_brsub_t *cb = (check_brsub_t *)it->param;
	DASSERT (cb);

	if (term)
		cb->brres_path = 0;
	else if (it->size >= 0x0c)
	{
		u8 *data = it->szs->data + it->off;
		const file_format_t fform = GetByMagicFF (data, it->size, it->size);
		if (fform == FF_BRRES)
		{
			cb->brres_path = it->path;
			cb->print_sep = print_header;
		}
		else if (cb->brres_path && IsBRSUB (fform))
		{
			char stat_buf[50], vers_buf[50], ns_buf[50];
			const int version = it->endian->rd32 (data + 8);
			const brsub_info_t *bi = GetInfoBRSUB (fform, version);
			const brsub_info_t *bic = GetCorrectBRSUB (fform);
			bool print_status = true;
			enumError err = ERR_OK;

			if (!bi)
			{
				err = ERR_INVALID_DATA;
				snprintf (stat_buf, sizeof (stat_buf), "%sUnknown%s", colout->fatal, colout->reset);
			}
			else
			{
				switch (bi->warn)
				{
					case BIMD_OK:
						err = ERR_INVALID_DATA;
						print_status = cb->mode > 1;
						snprintf (stat_buf, sizeof (stat_buf), "%sOk     %s", colout->success,
							colout->reset);
						break;

					case BIMD_INFO:
					case BIMD_HINT:
						err = ERR_INVALID_VERSION;
						print_status = cb->mode > 0;
						snprintf (stat_buf, sizeof (stat_buf), "%sUnusual%s", colout->hint,
							colout->reset);
						break;

					case BIMD_FAIL:
						err = ERR_INVALID_DATA;
						snprintf (stat_buf, sizeof (stat_buf), "%sFail   %s", colout->warn,
							colout->reset);
						break;

					default:
						// case BIMD_FATAL:
						err = ERR_INVALID_DATA;
						snprintf (
							stat_buf, sizeof (stat_buf), "%sFreeze %s", colout->bad, colout->reset);
						break;
				}
			}

			if (cb->max_err < err)
				cb->max_err = err;

			if (print_status)
			{
				if (bic && version != bic->good_version)
					snprintf (vers_buf, sizeof (vers_buf), "%s%3d!%s", colout->hint, version,
						colout->reset);
				else
					snprintf (vers_buf, sizeof (vers_buf), "%3d ", version);

				const int n_sect = GetGenericSectionNumBRSUB (data, it->size, it->endian);
				if (!bic || n_sect != bic->good_sect)
					snprintf (
						ns_buf, sizeof (ns_buf), "%s%2d!%s", colout->fatal, n_sect, colout->reset);
				else
					snprintf (ns_buf, sizeof (ns_buf), "%2d ", n_sect);

				if (!cb->header_printed)
				{
					cb->header_printed = true;
					cb->line_count = 999;
					if (print_header)
						printf ("\n%s%s:%s%s\n"
								"%s%.*s\n"
								"%s Status  Type Ver N/s Filename of BRRES and sub file%s\n",
							colout->caption, GetNameFF_SZS (cb->szs), cb->szs->fname, colout->reset,
							colout->heading, cb->sep_fw, ThinLine300_3, colout->heading,
							colout->reset);
					else
						printf ("\n%s>%s:%s%s\n", colout->caption, GetNameFF_SZS (cb->szs),
							cb->szs->fname, colout->reset);
				}

				if (cb->print_sep && cb->line_count > 2)
				{
					cb->print_sep = false;
					cb->line_count = 0;
					printf (
						"%s%.*s%s\n", colout->heading, cb->sep_fw, ThinLine300_3, colout->reset);
				}

				cb->sep_count++;
				cb->line_count++;
				printf (" %s %-3s %s %s %s / %s\n", stat_buf,
					GetNameFF (0, bic ? bic->fform1 : fform), vers_buf, ns_buf, cb->brres_path,
					it->path);
			}
		}
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

enumError cmd_brsub ()
{
	if (!n_param)
	{
		const int fw = 3 * 34;
		if (print_header)
			printf ("\n"
					"%s%.*s\n"
					"%s file format vers n(sect) warning%s\n",
				colout->heading, fw, ThinLine300_3, colout->heading, colout->reset);

		file_format_t last_ff = 0;
		const brsub_info_t *bi;
		for (bi = brsub_info; bi->fform1; bi++)
		{
			if (print_header && last_ff != bi->fform1)
			{
				last_ff = bi->fform1;
				if (print_header)
					printf ("%s%.*s%s\n", colout->heading, fw, ThinLine300_3, colout->reset);
			}

			printf (" %-3s %-7s %3d %6u   ", GetNameFF (0, bi->fform1),
				bi->fform2 ? GetNameFF (0, bi->fform2) : "-", bi->version, bi->n_sect);
			switch (bi->warn)
			{
				case BIMD_OK:
					fputs ("-\n", stdout);
					break;

				case BIMD_INFO:
				case BIMD_HINT:
					printf ("%sUNUSUAL%s\n", colout->hint, colout->reset);
					break;

				case BIMD_FAIL:
					printf ("%sFAIL%s\n", colout->warn, colout->reset);
					break;

				default:
					// case BIMD_FATAL:
					printf ("%sFREEZE%s\n", colout->bad, colout->reset);
					break;
			}
		}

		if (print_header)
			printf ("%s%.*s%s\n\n", colout->heading, fw, ThinLine300_3, colout->reset);

		return ERR_OK;
	}

	//--- have param => check BRSUB versions

	szs_file_t szs;
	InitializeSZS (&szs);
	uint line_count = 0;
	enumError max_err = ERR_OK;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		ResetSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);

		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;
		if (max_err < err)
			max_err = err;

		check_brsub_t cb;
		memset (&cb, 0, sizeof (cb));
		cb.szs = &szs;
		cb.mode = long_count;
		cb.sep_fw = 3 * 99;
		IterateFilesParSZS (&szs, check_brsub, &cb, false, false, false, -1, -1, SORT_NONE);
		line_count += cb.line_count;

		if (print_header && cb.header_printed)
			printf ("%s%.*s%s\n", colout->heading, cb.sep_fw, ThinLine300_3, colout->reset);

		if (max_err < cb.max_err)
			max_err = cb.max_err;
	}

	if (line_count)
		putchar ('\n');

	ResetStringField (&plist);
	ResetSZS (&szs);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command list			///////////////
///////////////////////////////////////////////////////////////////////////////

static int list_func (struct szs_iterator_t *it, // iterator struct with all infos
	bool term // true: termination hint
)
{
	DASSERT (it);
	DASSERT (it->szs);
	if (term)
		return 0;

	ccp col_reset = "";
	if (colorize_stdout)
	{
		if (it->is_dir)
		{
			fputs (colset->cmd, stdout);
			col_reset = colset->reset;
		}
		else if (it->has_subfiles)
		{
			fputs (colset->heading, stdout);
			col_reset = colset->reset;
		}
	}

	FixIteratorExt (it);
	const int indent = 2 * it->recurse_level;

	if (long_count > 1)
	{
		if (it->is_dir)
			printf ("      -       -        -  -      -  %.*s%s%s\n", indent, indent_msg, it->path,
				col_reset);
		else
		{
			char vers_buf[50];
			PrintVersionSZS (vers_buf, sizeof (vers_buf), it);

			const u8 *data = it->szs->data + it->off;
			printf ("%7x %7x %8u  %-4s %s %.*s%s%s\n", it->off, it->size, it->size,
				PrintID (data, it->size < 4 ? it->size : 4, 0), vers_buf, indent, indent_msg,
				it->path, col_reset);
		}
	}
	else if (long_count > 0)
	{
		if (it->is_dir)
			printf ("       -  -     %.*s%s%s\n", indent, indent_msg, it->path, col_reset);
		else
			printf ("%8u  %-4s  %.*s%s%s\n", it->size,
				PrintID (it->szs->data + it->off, it->size < 4 ? it->size : 4, 0), indent,
				indent_msg, it->path, col_reset);
	}
	else
		printf ("%.*s%s%s\n", indent, indent_msg, it->path, col_reset);

	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError list_sarc_file (ccp arg)
{
	u8 *data = 0;
	size_t file_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false);
	if (err)
		return err;
	const nfmt_type_t ftype = DetectNintendoFormat (data, file_size, arg).type;
	if (file_size > UINT_MAX || (ftype != NFMT_SARC && ftype != NFMT_BFMA))
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_t sarc;
	err = ScanSARC (&sarc, data, file_size);
	if (err)
	{
		FREE (data);
		return err;
	}
	if (print_header)
		printf ("\n%s> %u file%s of %s-endian SARC:%s %s\n", colout->stat_line, sarc.n_entries,
			sarc.n_entries == 1 ? "" : "s", sarc.big_endian ? "big" : "little", arg, colout->reset);
	for (uint i = 0; i < sarc.n_entries; i++)
	{
		ccp name;
		const u8 *entry;
		uint size;
		err = GetSARCEntry (&sarc, i, &name, &entry, &size);
		if (err)
			break;
		if (long_count)
			printf ("%8u  %-4s %s\n", size, PrintID (entry, size < 4 ? size : 4, 0), name);
		else
			puts (name);
	}
	FREE (data);
	return err;
}

static enumError list_ncer_file (ccp arg)
{
	u8 *data = 0;
	size_t file_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false);
	if (err)
		return err;
	if (file_size > UINT_MAX || DetectNintendoFormat (data, file_size, arg).type != NFMT_NCER)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_ncer_t ncer;
	err = ScanNCER (&ncer, data, file_size);
	if (err)
	{
		FREE (data);
		return ERR_INVALID_DATA;
	}
	if (print_header)
		printf ("\n%s> %u cell%s of NCER:%s %s\n", colout->stat_line, ncer.n_cells,
			ncer.n_cells == 1 ? "" : "s", colout->reset, arg);
	for (uint i = 0; i < ncer.n_cells; i++)
	{
		uint n_obj = 0;
		const u8 *oam = 0;
		err = GetNCERCell (&ncer, i, &n_obj, &oam);
		if (err)
			break;
		printf ("cell %u: %u OBJ%s\n", i, n_obj, n_obj == 1 ? "" : "s");
		if (long_count)
			for (uint j = 0; j < n_obj; j++, oam += 6)
			{
				const u16 a0 = le16 (oam), a1 = le16 (oam + 2), a2 = le16 (oam + 4);
				const int x = (a1 & 0x100) ? (a1 & 0x1ff) - 0x200 : a1 & 0x1ff;
				const int y = (a0 & 0x80) ? (a0 & 0xff) - 0x100 : a0 & 0xff;
				printf ("  obj %-3u xy=(%d,%d) tile=%u shape=%u size=%u attr=%04x/%04x/%04x\n", j,
					x, y, a2 & 0x3ff, a0 >> 14, a1 >> 14, a0, a1, a2);
			}
	}
	FREE (data);
	return err;
}

static enumError list_nanr_file (ccp arg)
{
	u8 *data = 0;
	size_t file_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false);
	if (err)
		return err;
	if (file_size > UINT_MAX || DetectNintendoFormat (data, file_size, arg).type != NFMT_NANR)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_nanr_t nanr;
	err = ScanNANR (&nanr, data, file_size);
	if (err)
	{
		FREE (data);
		return ERR_INVALID_DATA;
	}
	if (print_header)
		printf ("\n%s> %u animation%s, %u frame%s of NANR:%s %s\n", colout->stat_line,
			nanr.n_animations, nanr.n_animations == 1 ? "" : "s", nanr.n_frames,
			nanr.n_frames == 1 ? "" : "s", colout->reset, arg);
	for (uint i = 0; i < nanr.n_animations; i++)
	{
		uint n_frames = 0;
		const u8 *frames = 0;
		err = GetNANRAnimation (&nanr, i, &n_frames, &frames);
		if (err)
			break;
		printf ("animation %u: %u frame%s\n", i, n_frames, n_frames == 1 ? "" : "s");
		if (long_count)
			for (uint j = 0; j < n_frames; j++, frames += 8)
			{
				const uint data_off = le32 (frames);
				printf ("  frame %-3u cell=%u duration=%u data=%x\n", j,
					le16 (nanr.frame_data + data_off), le16 (frames + 4), data_off);
			}
	}
	FREE (data);
	return err;
}

static enumError list_brfnt_file (ccp arg)
{
	u8 *data = 0;
	size_t file_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false);
	if (err)
		return err;
	const nfmt_type_t type
		= file_size <= UINT_MAX ? DetectNintendoFormat (data, file_size, arg).type : NFMT_UNKNOWN;
	if (type != NFMT_BRFNT && type != NFMT_BRFNA)
	{
		FREE (data);
		return ERR_NOTHING_TO_DO;
	}
	if (file_size < 0x10 || (data[4] != 0xfe || data[5] != 0xff))
	{
		FREE (data);
		return ERR_INVALID_DATA;
	}
	const uint declared = be32 (data + 8), n_sections = be16 (data + 14);
	if (declared > file_size || !n_sections)
	{
		FREE (data);
		return ERR_INVALID_DATA;
	}
	if (print_header)
		printf ("\n%s> %s font with %u section%s:%s %s\n", colout->stat_line,
			GetNintendoFormatName (type), n_sections, n_sections == 1 ? "" : "s", colout->reset,
			arg);
	uint off = 0x10;
	for (uint i = 0; i < n_sections; i++)
	{
		if (off > declared - 8)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		const uint size = be32 (data + off + 4);
		if (size < 8 || size > declared - off)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		printf ("section %-2u %-4s size=%x", i, PrintID (data + off, 4, 0), size);
		if (!memcmp (data + off, "TGLP", 4) && size >= 0x20)
			printf (" glyph=%ux%u sheets=%u format=%u cells=%ux%u image=%ux%u", data[off + 8],
				data[off + 9], be16 (data + off + 0x10), be16 (data + off + 0x12),
				be16 (data + off + 0x14), be16 (data + off + 0x16), be16 (data + off + 0x18),
				be16 (data + off + 0x1a));
		putchar ('\n');
		off += size;
	}
	FREE (data);
	return err;
}

static enumError cmd_list (int long_level)
{
	SetupPager ();
	SetPatchFileModeReadonly ();

	if (opt_recurse < 0)
		opt_recurse = 0;

	if (long_level > 0)
	{
		RegisterOptionByIndex (&InfoUI_wszst, OPT_LONG, long_level, false);
		long_count += long_level;
	}

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError sarc_err = list_sarc_file (arg);
		if (sarc_err != ERR_NOTHING_TO_DO)
		{
			if (sarc_err > ERR_WARNING)
			{
				ResetStringField (&plist);
				return sarc_err;
			}
			continue;
		}
		enumError ncer_err = list_ncer_file (arg);
		if (ncer_err != ERR_NOTHING_TO_DO)
		{
			if (ncer_err > ERR_WARNING)
			{
				ResetStringField (&plist);
				return ncer_err;
			}
			continue;
		}
		enumError nanr_err = list_nanr_file (arg);
		if (nanr_err != ERR_NOTHING_TO_DO)
		{
			if (nanr_err > ERR_WARNING)
			{
				ResetStringField (&plist);
				return nanr_err;
			}
			continue;
		}
		enumError font_err = list_brfnt_file (arg);
		if (font_err != ERR_NOTHING_TO_DO)
		{
			if (font_err > ERR_WARNING)
			{
				ResetStringField (&plist);
				return font_err;
			}
			continue;
		}

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		CollectFilesSZS (&szs, true, -1, -1, SORT_NONE);
		if (opt_norm || need_norm > 0)
			NormalizeSZS (&szs);

		if (print_header)
			printf ("\n%s> %u director%s + %u file%s of %s:%s %s\n", colout->stat_line, szs.n_dirs,
				szs.n_dirs == 1 ? "y" : "ies", szs.n_files, szs.n_files == 1 ? "" : "s",
				GetNameFF_SZS (&szs), arg, colout->reset);

		int fw_name = szs.fw_files, seplen = 0;
		if (long_count >= 3)
			fw_name++;
		if (fw_name < 17)
			fw_name = 17;

		if (print_header)
		{
			static ccp f_or_d = "file or directory";
			if (long_count > 2)
				seplen = PrintFileHeadSZS (fw_name) * 3;
			else if (long_count > 1)
			{
				seplen = (fw_name + 37) * 3;
				printf ("\n%soff/hex siz/hex size/dec magic vers %s\n%s%.*s%s\n", colout->heading,
					f_or_d, colout->heading, seplen, ThinLine300_3, colout->reset);
			}
			else if (long_count > 0)
			{
				seplen = (fw_name + 17) * 3;
				printf ("\n%ssize/dec  magic %s\n%s%.*s%s\n", colout->heading, f_or_d,
					colout->heading, seplen, ThinLine300_3, colout->reset);
			}
			else
				putchar ('\n');
		}

		if (long_count > 2)
			IterateFilesParSZS (
				&szs, PrintFileSZS, 0, false, true, false, opt_recurse, opt_cut, opt_sort);
		else
			IterateFilesParSZS (
				&szs, list_func, 0, false, false, false, opt_recurse, opt_cut, opt_sort);

		if (print_header)
		{
			if (seplen && szs.n_dirs + szs.n_files > 2)
				printf ("%s%.*s%s\n\n", colout->heading, seplen, ThinLine300_3, colout->reset);
			else
				putchar ('\n');
		}
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command name-ref		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_name_ref ()
{
	stdlog = stderr;
	const SortMode_t std_sort = opt_sort == SORT_INAME ? SORT_INAME : SORT_BRRES;

	raw_data_t raw;
	InitializeRawData (&raw);

	enumError cmd_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError err = LoadRawData (&raw, false, arg, "course_model.brres", opt_ignore > 0, 0);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
		{
			ResetRawData (&raw);
			return err;
		}

		if (raw.fform != FF_BRRES && !IsBRSUB (raw.fform))
		{
			printf ("No BRRES family -> ignore %s:%s\n", GetNameFF (raw.fform, 0), raw.fname);
			continue;
		}

		szs_file_t szs;
		AssignSZS (&szs, true, raw.data, raw.data_size, false, raw.fform, raw.fname);
		raw.fname = NULL;

		name_ref_t nr;
		err = CreateNameRef (&nr, true, &szs, brief_count);
		if (err)
			return err;

		if (long_count > 0)
		{
			ListNameRef (stdout, 0, &nr, std_sort);
			ListNameRef (stdout, 0, &nr, SORT_OFFSET);
			if (long_count > 1)
				ListNameRef (stdout, 0, &nr, SORT_NONE);
		}
		else
			ListNameRef (stdout, 0, &nr, opt_sort);

		ResetNameRef (&nr);
	}

	ResetStringField (&plist);
	ResetRawData (&raw);
	return cmd_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command ilist			///////////////
///////////////////////////////////////////////////////////////////////////////

static int ilist_func (struct szs_iterator_t *it, // iterator struct with all infos
	bool term // true: termination hint
)
{
	DASSERT (it);
	DASSERT (it->szs);
	if (term)
		return 0;

	if (!it->is_dir)
	{
		const u8 *data = it->szs->data + it->off;
		// [[analyse-magic]]
		PrintImage (data, it->size, it->path, 0, 2 * it->recurse_level, long_count,
			!it->has_subfiles || GetByMagicFF (data, it->size, it->size) == FF_BREFF);
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_ilist (int long_level)
{
	SetPatchFileModeReadonly ();

	if (opt_recurse < 0)
		opt_recurse = 0;

	if (long_level > 0)
	{
		RegisterOptionByIndex (&InfoUI_wszst, OPT_LONG, long_level, false);
		long_count += long_level;
	}

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		if (opt_norm || need_norm > 0)
			NormalizeSZS (&szs);

		if (print_header)
			PrintImageHead (0, long_count);
		else if (n_param > 1)
			printf ("\n* Files of %s\n", arg);

		IterateFilesParSZS (&szs, ilist_func, 0, false, false, false, -1, 0, opt_sort);
		ResetSZS (&szs);
	}

	if (print_header)
		putchar ('\n');

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command memory			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct dumpmem_t
{
	szs_file_t *szs; // the container
	u8 *data_base; // pointer to base of complete file
	uint last_off; // offset of last analyzed data
	uint last_end; // end of last analyzed data
	bool print_abs_off; // true: print absolute offset

} dumpmem_t;

///////////////////////////////////////////////////////////////////////////////

static void DumpUnused (dumpmem_t *dm, // dump info
	uint offset, // offset of object to analyze and dump
	file_format_t fform, // NULL for last entry or fileformat
	uint recurse_level // recurse level
)
{
	DASSERT (dm);

	recurse_level *= 2;
	if (fform != FF_INVALID)
	{
		if (dm->print_abs_off)
			printf ("%7x ", offset);

		ccp ff = GetNameFF (fform, 0);

		if (dm->last_end < offset)
			printf ("%*s%-6.6s %6x ", recurse_level, "", ff, offset - dm->last_end);
		else if (dm->last_end > offset)
			printf ("%*s%-6.6s %6x-", recurse_level, "", ff, dm->last_end - offset);
		else
			printf ("%*s%-6.6s      - ", recurse_level, "", ff);
	}
	else if (dm->last_end < offset)
	{
		if (dm->print_abs_off)
			printf ("%7x ", offset);

		printf ("%*s-      %6x\n", recurse_level, "", offset - dm->last_end);
	}
	else if (dm->last_end > offset)
	{
		if (dm->print_abs_off)
			printf ("%7x ", offset);

		printf ("%*s-      %6x-\n", recurse_level, "", dm->last_end - offset);
	}

	dm->last_off = dm->last_end = offset;
}

///////////////////////////////////////////////////////////////////////////////

static int memory_func (struct szs_iterator_t *it, // iterator struct with all infos
	bool term // true: termination hint
)
{
	DASSERT (it);
	DASSERT (it->szs);
	DASSERT (it->param);

	dumpmem_t *dm = it->param;

	if (term)
	{
		const uint abs_off = it->szs->data - dm->data_base + it->szs->size;
		noPRINT ("TERM: %x %x %x\n", dm->last_off, dm->last_end, abs_off);
		DumpUnused (dm, abs_off, FF_INVALID, it->recurse_level);
	}
	else if (!it->is_dir)
	{
		if (!it->client_int)
		{
			noPRINT ("LAST: %x %x\n", dm->last_off, dm->last_end);
			it->client_int++;
			dm->last_end = dm->last_off;
		}

		const u8 *data = it->szs->data + it->off;
		// [[analyse-magic]]
		file_format_t fform = GetByMagicFF (data, it->size, it->size);
		const uint abs_off = it->szs->data - dm->data_base + it->off;
		DumpUnused (dm, abs_off, fform, it->recurse_level);
		printf (" %6x .. %6x %6x  %s\n", it->off, it->off + it->size, it->size, it->path);

		dm->last_off = abs_off;
		dm->last_end = abs_off + it->size;
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_memory ()
{
	SetPatchFileModeReadonly ();

	if (opt_recurse < 0)
		opt_recurse = 0;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		if (opt_norm || need_norm > 0)
			NormalizeSZS (&szs);

		dumpmem_t dm;
		memset (&dm, 0, sizeof (dm));
		dm.szs = &szs;
		dm.data_base = szs.data;
		dm.print_abs_off = long_count > 0;

		if (print_header)
		{
			printf ("\n* memory dump of %s:%s\n", GetNameFF_SZS (&szs), arg);

			fputs (dm.print_abs_off ? "\nabs off " : "\n", stdout);
			printf ("type    unused  begin ..    end   size  file name\n"
					"%.80s\n",
				Minus300);
		}

		IterateFilesParSZS (
			&szs, memory_func, &dm, false, false, false, opt_recurse, opt_cut, SORT_OFFSET);
		ResetSZS (&szs);
	}

	if (print_header)
		putchar ('\n');

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command dump			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_dump ()
{
	SetPatchFileModeReadonly ();

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		if (opt_norm || need_norm > 0)
			NormalizeSZS (&szs);

		if (szs.fform_arch == FF_BRRES || IsBRSUB (szs.fform_arch))
		{
			printf ("\n* Dump structure of %s:%s\n", GetNameFF_SZS (&szs), arg);
			DumpStructureBRRES (stdout, &szs);
		}
		else if (!opt_ignore)
			ERROR0 (ERR_WARNING, "No BRRES file -> ignored: %s\n", arg);
		ResetSZS (&szs);
	}
	putchar ('\n');

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command sha1			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_sha1 ()
{
	stdlog = stderr;
	enumError max_err = ERR_OK;
	SetPatchFileModeReadonly ();
	SetupCoding64 (opt_coding64, opt_db64 ? ENCODE_BASE64URL : ENCODE_BASE64);

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);
		if (max_err < err)
			max_err = err;
		if (err > ERR_WARNING || err == ERR_NOT_EXISTS)
		{
			if (opt_db64)
			{
				// fputs("--------------------------------",stdout);
				putchar ('-');
				if (long_count)
					fputs ("  -1 -1 0.000  -", stdout);
				if (brief_count)
					putchar ('\n');
				else
					printf ("  %s\n", arg);
			}

			if (err == ERR_NOT_EXISTS || opt_ignore)
				continue;
			return err;
		}

		if (opt_norm || need_norm > 0 || opt_rm_aiparam)
			NormalizeSZS (&szs);

		char checksum[100];
		CreateSSChecksumBySZS (checksum, sizeof (checksum), &szs);

		if (opt_verify)
		{
			const uint clen = strlen (checksum);
			ccp fname = strrchr (arg, '/');
			fname = fname ? fname + 1 : arg;
			bool found = false;
			for (;;)
			{
				ccp pos = strstr (fname, checksum);
				if (!pos)
					break;

				if (pos == arg || pos[-1] == '.' || pos[-1] == '-')
				{
					char ch = pos[clen];
					if (!ch || ch == '.' || ch == '-')
					{
						found = true;
						break;
					}
				}

				fname = pos + 1; // search more
			}

			if (!found)
			{
				if (max_err < ERR_DIFFER)
					max_err = ERR_DIFFER;
				if (verbose >= 0)
					printf ("Checksum differ: %s\n", arg);
			}
			else if (verbose > 0)
				printf ("Checksum ok:     %s\n", arg);
		}
		else
		{
			if (!opt_db64)
			{
				if (long_count > 1)
				{
					struct tm *tm = localtime (&szs.fatt.mtime.tv_sec);
					char timbuf[40];
					strftime (timbuf, sizeof (timbuf), "%F %T ", tm);
					fputs (timbuf, stdout);
				}
				if (long_count)
					printf ("%9zu,", szs.size);
			}

			fputs (checksum, stdout);

			if (opt_db64 && long_count)
			{
				int ckpt0_count = -1; // number of LC in CKPT
				int lap_count = 3; // STGI lap counter
				float speed_factor = 1.0; // STGI speed factor
				ccp slot_info = CreateSlotInfo (&szs);

				FindSpecialFilesSZS (&szs, false);
				if (szs.course_kmp_data)
				{
					kmp_t kmp;
					InitializeKMP (&kmp);
					err = ScanKMP (&kmp, false, szs.course_kmp_data, szs.course_kmp_size, 0);
					if (err <= ERR_WARNING)
					{
						const uint n_ckpt = kmp.dlist[KMP_CKPT].used;
						if (n_ckpt)
						{
							const kmp_ckpt_entry_t *ckpt
								= (kmp_ckpt_entry_t *)kmp.dlist[KMP_CKPT].list;
							uint i;
							ckpt0_count = 0;
							for (i = 0; i < n_ckpt; i++, ckpt++)
								if (!ckpt->mode)
									ckpt0_count++;
						}

						const kmp_stgi_entry_t *stgi = (kmp_stgi_entry_t *)kmp.dlist[KMP_STGI].list;
						if (kmp.dlist[KMP_STGI].used > 0)
						{
							lap_count = stgi->lap_count;

							if (stgi->speed_mod)
								speed_factor = SpeedMod2float (stgi->speed_mod);
						}
					}
					ResetKMP (&kmp);
				}

				printf ("  %d %d %5.3f  %s", ckpt0_count, lap_count, speed_factor, slot_info);
			}

			if (brief_count)
				putchar ('\n');
			else
				printf ("  %s\n", arg);
		}
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command analyze			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_analyze ()
{
	SetupPager ();

	static ccp def_path = "\1P/\1F\1?T";
	CheckOptDest ("-", false);
	char dest[PATH_MAX];
	enumError max_err = ERR_OK;
	if (!strcmp (opt_dest, "-"))
		fputc ('\n', stdout);

	analyze_param_t ap = { 0 };
	InitializeSZS (&ap.szs);
	InitializeFile (&ap.fo);
	SetupPrintScriptByOptions (&ap.ps);

	patch_action_log_disabled++;
	CheckTextureRefSZS (&ap.szs, 0);

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		ResetSZS (&ap.szs);
		enumError err = LoadCreateSZS (&ap.szs, arg, true, opt_ignore > 0, true);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
		{
			if (max_err < err)
				max_err = err;
			continue;
		}

		if (!ap.fo.f)
			SubstDest (
				dest, sizeof (dest), arg, opt_dest, def_path, GetExtFF (script_fform, 0), false);

		if (verbose > 0)
		{
			fprintf (stdlog, "%sANALYZE %s:%s => %s:%s\n", verbose > 0 ? "\n" : "",
				GetNameFF (ap.szs.fform_file, ap.szs.fform_arch), ap.szs.fname,
				GetNameFF (script_fform, 0), dest);
			fflush (stdlog);
		}

		if (err >= ERR_WARNING)
		{
			if (max_err < err)
				max_err = err;
			continue;
		}

		if (!ap.fo.f)
		{
			enumError err = CreateFile (&ap.fo, false, dest, FM_STDIO | FM_OVERWRITE);
			if (err)
			{
				max_err = err;
				break;
			}
			ap.ps.f = ap.fo.f;
			PrintScriptHeader (&ap.ps);
		}

		ap.fname = arg;

		//--- analyze by file format

		switch (ap.szs.fform_arch)
		{
			case FF_U8:
			case FF_WU8:
				err = ExecAnalyzeSZS (&ap);
				break;
			case FF_LE_BIN:
				err = ExecAnalyzeLECODE (&ap);
				break;

			default:
				PrintHeaderAP (&ap, "");
				PrintFooterAP (&ap, false, 0, "File format not supported.");
				break;
		}

		if (!script_array)
		{
			PrintScriptFooter (&ap.ps);
			ap.ps.f = 0;
			ResetFile (&ap.fo, 0);
		}
	}

	PrintScriptFooter (&ap.ps);

	ResetStringField (&plist);
	ResetPrintScript (&ap.ps);
	ResetFile (&ap.fo, 0);
	ResetSZS (&ap.szs);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command split			///////////////
///////////////////////////////////////////////////////////////////////////////

static void print_split_val (PrintScript_t *ps, ccp name, mem_t val)
{
	DASSERT (ps);
	DASSERT (name);

	if (val.ptr && val.len)
	{
		char buf[1000];
		PrintEscapedString (buf, sizeof (buf), val.ptr, val.len, CHMD_UTF8, '"', 0);
		PrintScriptVars (ps, 0, "%s=\"%s\"\n", name, buf);
	}
	else
		PrintScriptVars (ps, 0, "%s=\"\"\n", name);
}

//-----------------------------------------------------------------------------

static enumError cmd_split ()
{
	static ccp def_path = "\1P/\1F\1?T";
	CheckOptDest ("-", false);
	char dest[PATH_MAX];
	enumError max_err = ERR_OK;
	if (opt_split <= 0 && !strcmp (opt_dest, "-"))
		fputc ('\n', stdout);

	split_filename_t spf;
	InitializeSPF (&spf);

	print_split_par_t psp = { .format = opt_printf, .split_level = opt_split, .u_use_list = true };

	File_t fo;
	InitializeFile (&fo);

	PrintScript_t ps;
	SetupPrintScriptByOptions (&ps);

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		if (!fo.f)
			SubstDest (
				dest, sizeof (dest), arg, opt_dest, def_path, GetExtFF (script_fform, 0), false);

		if (!fo.f)
		{
			enumError err = CreateFile (&fo, false, dest, FM_STDIO | FM_OVERWRITE);
			if (err)
			{
				max_err = err;
				break;
			}
			ps.f = fo.f;
			PrintScriptHeader (&ps);
		}

		const u_nsec_t start_nsec = GetTimerNSec ();
		AnalyseSPF (&spf, false, arg, 0, CPM_LINK, opt_plus);
		const u_nsec_t duration_nsec = GetTimerNSec () - start_nsec;

		if (opt_split > 0)
		{
			exmem_t res = PrintSPF (0, &spf, &psp);
			printf ("%s\n", res.data.ptr);
			FreeExMem (&res);
			continue;
		}

		PutScriptVars (&ps, 1, 0);

		print_split_val (&ps, "directory", spf.directory);
		print_split_val (&ps, "source", spf.source);

		print_split_val (&ps, "file_name", spf.f_name);
		print_split_val (&ps, "file_d", spf.f_d);
		print_split_val (&ps, "file_ext", spf.f_ext);

		print_split_val (&ps, "normed", spf.norm);
		print_split_val (&ps, "plus", spf.plus);
		PrintScriptVars (&ps, 0, "plus_order=%d\n", spf.plus_order);
		print_split_val (&ps, "boost", spf.boost);

		print_split_val (&ps, "game1", spf.game1);
		print_split_val (&ps, "game2", spf.game2);
		print_split_val (&ps, "game", spf.game);
		PrintScriptVars (&ps, 0, "game_order=%d\ngame1_color=%d\ngame2_color=%d\n", spf.game_order,
			spf.game1_color, spf.game2_color);

		print_split_val (&ps, "name", spf.name);
		print_split_val (&ps, "extra", spf.extra);
		print_split_val (&ps, "version", spf.version);
		print_split_val (&ps, "authors", spf.authors);
		print_split_val (&ps, "editors", spf.editors);
		print_split_val (&ps, "attribs", spf.attribs);
		PrintScriptVars (&ps, 0, "attrib_order=%d\n", spf.attrib_order);

		PrintScriptVars (&ps, 0, "distrib_flags=\"%u %s\"\n", spf.distrib_flags,
			PrintDTA (spf.distrib_flags, false));
		PrintScriptVars (
			&ps, 0, "le_flags=\"%u %s\"\n", spf.le_flags, PrintLEFL8 (spf.le_flags, false));
		print_split_val (&ps, "le_group", spf.le_group);

		const mkw_category_t *tcat = GetCategory (spf.track_cat, MKW_CAT_UNKNOWN);
		if (tcat)
			PrintScriptVars (
				&ps, 0, "track_cat=\"%u %s %u\"\n", spf.track_cat, tcat->name, tcat->mode);
		else
			PrintScriptVars (&ps, 0, "track_cat=\"%u - 0\"\n", spf.track_cat);

		PrintScriptVars (&ps, 0, "lap_count=%u\n", spf.lap_count);
		if (spf.speed_factor > 0.0)
			PrintScriptVars (&ps, 0, "speed_factor=%5.3f\n", spf.speed_factor);
		else
			PutScriptVars (&ps, 0, "speed_factor=0\n");

		if (opt_printf)
		{
			exmem_t res = PrintSPF (0, &spf, &psp);
			print_split_val (&ps, "printf", res.data);
			FreeExMem (&res);
		}

		PrintScriptVars (&ps, 2, "duration_nsec=%llu\n", duration_nsec);

		fflush (ps.f);
		if (!script_array)
		{
			PrintScriptFooter (&ps);
			ps.f = 0;
			ResetFile (&fo, 0);
		}
	}

	PrintScriptFooter (&ps);

	ResetStringField (&plist);
	ResetPrintScript (&ps);
	ResetFile (&fo, 0);
	ResetPSP (&psp);
	ResetSPF (&spf);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command is-texture		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_is_texture ()
{
	disable_checks++;

	int max_err = 0;
	szs_file_t szs;
	InitializeSZS (&szs);
	patch_action_log_disabled++;

	uint max_fw = 0;
	struct
	{
		FastBuf_t b;
		char space[500];
	} fbuf;
	if (long_count)
		InitializeFastBuf (&fbuf, sizeof (fbuf));

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		ResetSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);
		if (err > ERR_WARNING)
		{
			if (long_count)
			{
				AppendFastBuf (&fbuf.b, "-1=err", 7);
				if (max_fw < 7)
					max_fw = 7;
			}
			if (max_err < err)
				max_err = err;
			continue;
		}

		ccp texture_info = 0;
		PrepareCheckTextureSZS (&szs);
		CheckTextureRefSZS (&szs, &texture_info);

		if (long_count)
		{
			ccp x = texture_info ? texture_info : "-1";
			const int len = strlen (x);
			AppendFastBuf (&fbuf.b, x, len + 1);
			if (max_fw < len)
				max_fw = len;
		}
		else
			printf ("%s\n", texture_info ? texture_info : "-1");
	}

	if (long_count)
	{
		ccp par = fbuf.b.buf;
		for (int argi = 0; argi < plist.used; argi++)
		{
			printf ("%-*s : %s\n", max_fw, par, plist.field[argi]);
			par += strlen (par) + 1;
		}
		ResetFastBuf (&fbuf.b);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command features		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_features ()
{
	static ccp def_path = "\1P/\1F\1?T";
	CheckOptDest ("-", false);
	if (!strcmp (opt_dest, "-"))
		SetupPager ();

	char dest[PATH_MAX];
	enumError max_err = ERR_OK;
	if (!strcmp (opt_dest, "-"))
		fputc ('\n', stdout);

	szs_file_t szs;
	InitializeSZS (&szs);

	analyze_szs_t as;
	InitializeAnalyzeSZS (&as);

	features_szs_t fs;
	InitializeFeaturesSZS (&fs);
	const int comments = brief_count > 0 ? -brief_count : long_count;

	File_t fo;
	InitializeFile (&fo);

	PrintScript_t ps;
	SetupPrintScriptByOptions (&ps);
	ps.eq_tabstop = 3;
	if (ps.fform == PSFF_UNKNOWN && comments >= 0)
		ps.fform = PSFF_ASSIGN;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		ResetSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
		{
			if (max_err < err)
				max_err = err;
			continue;
		}

		if (!fo.f)
			SubstDest (
				dest, sizeof (dest), arg, opt_dest, def_path, GetExtFF (script_fform, 0), false);

		if (verbose > 0)
		{
			fprintf (stdlog, "%sANALYZE %s:%s => %s:%s\n", verbose > 0 ? "\n" : "",
				GetNameFF (szs.fform_file, szs.fform_arch), szs.fname, GetNameFF (script_fform, 0),
				dest);
			fflush (stdlog);
		}

		//--- analyse szs

		AnalyzeSZS (&as, false, &szs, arg);
		SetupFeaturesSZS (&fs, &as.have, false);

		//--- status

		if (err >= ERR_WARNING)
		{
			if (max_err < err)
				max_err = err;
			continue;
		}

		if (!fo.f)
		{
			enumError err = CreateFile (&fo, false, dest, FM_STDIO | FM_OVERWRITE);
			if (err)
			{
				max_err = err;
				break;
			}
			ps.f = fo.f;
			PrintScriptHeader (&ps);
		}

		//--- name attributes ('dest' can be used now)

		PrintEscapedString (dest, sizeof (dest), arg, -1, CHMD_UTF8, '"', 0);

		//--- print result

		PrintScriptVars (&ps, 1, "file=\"%s\"\n", dest);
		PrintFeaturesSZS (
			&ps, &fs, false, comments, 1, long_count, opt_fmodes_include, opt_fmodes_exclude);
		PutScriptVars (&ps, 2, 0);

		if (!script_array)
		{
			PrintScriptFooter (&ps);
			ps.f = 0;
			ResetFile (&fo, 0);
		}
	}

	PrintScriptFooter (&ps);

	ResetStringField (&plist);
	ResetPrintScript (&ps);
	ResetFile (&fo, 0);
	ResetFeaturesSZS (&fs);
	ResetAnalyzeSZS (&as);
	ResetSZS (&szs);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command distribution		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanDistribFile (DistributionInfo_t *dinf, ccp fname, int ignore, bool assume_arena)
{
	DASSERT (dinf);
	if (ignore && IsDirectory (fname, true))
		return ERR_OK;

	szs_file_t szs;
	InitializeSZS (&szs);
	enumError err = LoadSZS (&szs, fname, true, ignore > 0, true);

	if (err > ERR_WARNING || err == ERR_NOT_EXISTS)
	{
		ResetSZS (&szs);
		return err == ERR_NOT_EXISTS || ignore ? ERR_OK : err;
	}

	// [[lta]] [[2do]]
	// [[lfl]] [[2do]]
	if (szs.fform_arch != FF_U8 && szs.fform_arch != FF_WU8)
	{
		ResetSZS (&szs);
		if (ignore < 2)
			return ERROR0 (ERR_INVALID_DATA, "Not a SZS ot WBZ file: %s\n", fname);
		return ERR_OK;
	}

	if (opt_norm || need_norm > 0)
		NormalizeExSZS (&szs, false, false, false);

	if (verbose > 0)
		printf ("Read %s\n", fname);

	//--- cut filename

	char fname_buf[1000];

	ccp slash = strrchr (fname, '/');
	if (slash)
		fname = slash + 1;

	StringCopyS (fname_buf, sizeof (fname_buf), fname);
	fname = fname_buf;

	char *src, *dest = strrchr (fname_buf, '.');
	if (dest && strlen (dest) <= 4)
		*dest = 0;

	src = dest = fname_buf;
	while (*src)
	{
		if (!memcmp (src, "ː", 2))
		{
			src += 2;
			*dest++ = ':';
		}
		else if (!memcmp (src, "%3a", 3))
		{
			src += 3;
			*dest++ = ':';
		}
		else
			*dest++ = *src++;
	}
	*dest = 0;

	//--- create checksum

	char checksum[100];
	CreateSSChecksumBySZS (checksum, sizeof (checksum), &szs);
	ResetSZS (&szs);

	//--- save result

	uint slot = FindSlotByTranslation (&dinf->translate, fname, checksum);
	if (slot)
	{
		uint num;
		char *next = ScanSlot (&num, fname, false);
		if (num == slot && (uchar)*next <= ' ')
			fname = next;
		else
		{
			num = strtoul (fname, &next, 10);
			if (num == slot && (uchar)*next <= ' ')
				fname = next;
		}

		while (*fname == ' ')
			fname++;
	}

	if (assume_arena)
		slot += DISTRIBUTION_ARENA_DELTA;

	PRINT (">>> slot=%d[%d,%d] %s\n", slot, IsValidDistribArena (slot), IsValidDistribTrack (slot),
		fname);

	if (IsValidDistribTrack (slot))
		AppendParamField (dinf->track + slot, fname, false, slot, STRDUP (checksum));
