	else if (IsValidDistribArena (slot))
		AppendParamField (
			dinf->arena + slot - DISTRIBUTION_ARENA_DELTA, fname, false, slot, STRDUP (checksum));
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_distribution ()
{
	SetPatchFileModeReadonly ();
	SetupCoding64 (opt_coding64, opt_db64 ? ENCODE_BASE64URL : ENCODE_BASE64);

	DistributionInfo_t dinf;
	InitializeDistributionInfo (&dinf, true);

	char slotname[20];

	//--- define result file

	if (!first_param || !first_param->arg || !*first_param->arg)
		return ERROR0 (ERR_MISSING_PARAM, "Definition of result file missed (first parameter)!\n");

	ParamList_t *param;
	char result[PATH_MAX], path[PATH_MAX];

	if (IsDirectory (first_param->arg, false))
	{
		PathCatPP (result, sizeof (result), first_param->arg, DEFAULT_DISTIBUTION_FNAME);
		param = first_param;
	}
	else
	{
		char *dest = StringCopyS (result, sizeof (result) - 4, first_param->arg);
		if (dest < result + 4 || strcasecmp (dest - 4, ".txt"))
			strcpy (dest, ".txt");
		param = first_param->next;
	}

	//--- setup slot translation

	int i;
	for (i = 0; i < MKW_N_ARENAS; i++)
	{
		const TrackInfo_t *ti = arena_info + i;
		DefineSlotTranslation (&dinf.translate, true, ti->def_slot, ti->track_fname);
		DefineSlotTranslation (&dinf.translate, true, ti->def_slot, ti->name_en);
		DefineSlotTranslation (&dinf.translate, true, ti->def_slot, ti->name_de);
	}

	for (i = 0; i < MKW_N_TRACKS; i++)
	{
		const TrackInfo_t *ti = track_info + i;
		DefineSlotTranslation (&dinf.translate, false, ti->def_slot, ti->track_fname);
		DefineSlotTranslation (&dinf.translate, false, ti->def_slot, ti->name_en);
		DefineSlotTranslation (&dinf.translate, false, ti->def_slot, ti->name_de);
	}

	bool result_found = false;
	for (i = 0; i < source_list.used; i++)
	{
		ccp arg = source_list.field[i];
		if (!i && !strcmp (arg, "0"))
			ResetParamField (&dinf.translate);
		else if (!result_found && !strcmp (result, arg))
		{
			result_found = true;
			ScanSlotTranslation (&dinf.translate, &dinf.ld, arg, false);
		}
		else
			ScanSlotTranslation (&dinf.translate, 0, arg, false);
	}

	if (!result_found)
		ScanSlotTranslation (&dinf.translate, &dinf.ld, result, true);

	AddParamDistributionInfo (&dinf, false);

	//--- logging

	if (logging >= 1)
	{
		fputs ("---------------  translation  ---------------\n", stdlog);

		uint next_slot = 0;
		while (next_slot < INT_MAX)
		{
			uint slot = next_slot;
			next_slot = INT_MAX;

			uint i;
			const ParamFieldItem_t *it = dinf.translate.field;
			for (i = 0; i < dinf.translate.used; i++, it++)
			{
				if (it->num == slot)
				{
					if (slot < DISTRIBUTION_ARENA_DELTA)
						fprintf (stdlog, "%4u. %3u.%u  %s\n", i + 1, it->num / 10, it->num % 10,
							it->key);
					else
					{
						snprintf (slotname, sizeof (slotname), "A%u.%u",
							(it->num - DISTRIBUTION_ARENA_DELTA) / 10, it->num % 10);
						fprintf (stdlog, "%4u. %5s  %s\n", i + 1, slotname, it->key);
					}
				}
				else if (it->num > slot && it->num < next_slot)
					next_slot = it->num;
			}
		}

		fputs ("----------------  parameter  ----------------\n", stdlog);

		uint i;
		const ParamFieldItem_t *it = dinf.ld.dis_param.field;
		for (i = 0; i < dinf.ld.dis_param.used; i++, it++)
			fprintf (stdlog, "%4u. %-20s = %s\n", i + 1, it->key, (ccp)it->data);

		fputs ("---------------------------------------------\n", stdlog);
	}

	//--- iterate files & directories

	if (verbose == 0)
		printf ("Scan SZS files\n");

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		bool assume_arena = false;
		ccp ptr = arg;
		for (;;)
		{
			ptr = strchr (ptr, '/');
			if (!ptr)
				break;
			ptr++;
			if (!strncasecmp (ptr, "arena", 5))
			{
				assume_arena = true;
				PRINT ("ASSUME ARENA: %s\n", arg);
				break;
			}
		}

		if (IsDirectory (arg, false))
		{
			DIR *dir = opendir (arg);
			if (dir)
			{
				for (;;)
				{
					struct dirent *dent = readdir (dir);
					if (!dent)
						break;

					if (*dent->d_name != '.')
					{
						PathCatPP (path, sizeof (path), arg, dent->d_name);
#if HAVE_WIIMM_EXT // [[2do]] by option
						if (!strstr (path, ",clan]") && !strstr (path, ",head="))
#endif
						{
							enumError err = ScanDistribFile (&dinf, path, 2, assume_arena);
							if (err)
								return err;
						}
					}
				}
				closedir (dir);
			}
		}
		else
		{
			enumError err = ScanDistribFile (&dinf, arg, opt_ignore, assume_arena);
			if (err)
				return err;
		}
	}

	//--- print results

	if (verbose >= 0)
		printf ("Create file %s\n", result);

	StringCat2S (path, sizeof (path), result, ".bak");
	rename (result, path);

	FILE *f = fopen (result, "wb");
	if (!f)
		ERROR1 (ERR_CANT_CREATE, "Can't create file: %s\n", result);

	CreateDistribLD (f, &dinf.ld, true);

	//--- battle arenas

	int slot, slot10 = -1, count = 0;
	for (slot = 0; slot < MAX_DISTRIBUTION_ARENA; slot++)
	{
		ParamField_t *pf = dinf.arena + slot;
		if (pf->used)
		{
			count++;
			if (slot10 != slot / 10)
			{
				slot10 = slot / 10;
				fputs ("\r\n", f);
			}

			if (slot)
				snprintf (slotname, sizeof (slotname), "A%u.%u", slot10, slot % 10);
			else
				StringCopyS (slotname, sizeof (slotname), "arena");

			uint i;
			ParamFieldItem_t *it = pf->field;
			for (i = 0; i < pf->used; i++, it++)
				fprintf (f, "%s %5s  %s\r\n", (ccp)it->data, slotname, it->key);
		}
	}
	if (count)
		fputs ("\r\n", f);

	//--- racing tracks

	slot10 = -1;
	count = 0;

	for (slot = 0; slot < MAX_DISTRIBUTION_TRACK; slot++)
	{
		ParamField_t *pf = dinf.track + slot;
		if (pf->used)
		{
			count++;
			if (slot10 != slot / 10)
			{
				slot10 = slot / 10;
				fputs ("\r\n", f);
			}

			char slotname[10];
			if (slot)
				snprintf (slotname, sizeof (slotname), "%3u.%u", slot10, slot % 10);
			else
				StringCopyS (slotname, sizeof (slotname), "  ---");

			uint i;
			ParamFieldItem_t *it = pf->field;
			for (i = 0; i < pf->used; i++, it++)
				fprintf (f, "%s %s  %s\r\n", (ccp)it->data, slotname, it->key);
		}
	}
	if (count)
		fputs ("\r\n", f);
	fclose (f);

	ResetStringField (&plist);
	ResetDistributionInfo (&dinf);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command diff			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError diff_files (ccp fname1, ccp fname2)
{
	if (verbose > 0)
		printf ("DIFF %s : %s\n", fname1, fname2);

	szs_file_t szs1, szs2;
	InitializeSZS (&szs1);
	InitializeSZS (&szs2);

	enumError err = LoadCreateSZS (&szs1, fname1, true, false, true);
	if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
		err = LoadCreateSZS (&szs2, fname2, true, false, true);
	if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
		err = DiffSZS (
			&szs1, &szs2, opt_recurse < 0 ? 0 : opt_recurse, opt_cut ? 1 : -1, verbose < 0);

	if (verbose >= -1 && err == ERR_DIFFER)
		printf ("Content differ: %s : %s\n", fname1, fname2);
	else if (verbose >= 0 && err == ERR_OK)
		printf ("Content identical: %s : %s\n", fname1, fname2);

	ResetSZS (&szs1);
	ResetSZS (&szs2);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError diff_dest ()
{
	enumError max_err = ERR_OK;
	ParamList_t *param;
	for (param = first_param; param; param = param->next)
	{
		NORMALIZE_FILENAME_PARAM (param);

		char dest[PATH_MAX];
		SubstDest (dest, sizeof (dest), param->arg, opt_dest, "%F", 0, false);
		const enumError err = diff_files (param->arg, dest);
		if (max_err < err)
			max_err = err;
		if (err > ERR_WARNING || err && verbose < -1)
			break;
	}
	return max_err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_diff ()
{
	SetPatchFileModeReadonly ();

	if (opt_dest)
		return diff_dest ();

	if (n_param != 2)
		return ERROR0 (ERR_SYNTAX, "Exact 2 sources expected if --dest is not set.\n");

	ASSERT (first_param);
	ASSERT (first_param->next);
	return diff_files (first_param->arg, first_param->next->arg);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command check			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_check ()
{
	SetPatchFileModeReadonly ();

	patch_action_log_disabled++;
	if (KCL_MODE & KCLMD_DROP_AUTO)
		SetKclMode (KCL_MODE | KCLMD_DROP_UNUSED);

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);

		const bool check_verbose = verbose > 0 || long_count > 0;
		CheckMode_t mode = GetCheckMode (false, brief_count > 0, verbose < 0, check_verbose);
		if (verbose >= 0 || testmode)
		{
			ColorSet_t col;
			SetupColorSet (&col, stdlog);
			mode = GetCheckMode (true, brief_count > 0, false, check_verbose);
			fprintf (stdlog, "\n%sCHECK %s:%s%s\n", col.heading, GetNameFF_SZS (&szs), szs.fname,
				col.reset);
			fflush (stdlog);
		}

		if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
		{
			switch (szs.fform_arch)
			{
				case FF_KCL:
				case FF_KCL_TXT:
				case FF_WAV_OBJ:
				case FF_SKP_OBJ:
				{
					kcl_t kcl;
					err = ScanKCL (&kcl, true, szs.data, szs.size, true, mode);
					ResetKCL (&kcl);
				}
				break;

				case FF_KMP:
				case FF_KMP_TXT:
				{
					kmp_t kmp;
					err = ScanKMP (&kmp, true, szs.data, szs.size, mode);
					ResetKMP (&kmp);
				}
				break;

				case FF_U8:
				case FF_WU8:
					if (CheckSZS (&szs, mode, global_check_mode))
						err = ERR_DIFFER;
					break;

				case FF_BRRES:
					if (CheckBRRES (&szs, mode, 0))
						err = ERR_DIFFER;
					break;

				default:
					ERROR0 (ERR_INVALID_FFORM, "CHECK doesn't support file format '%s': %s\n",
						GetNameFF_SZS (&szs), arg);
					break;
			}
			fflush (stdout);
		}

		if (max_err < err)
			max_err = err;
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command slot			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_slots ()
{
	SetPatchFileModeReadonly ();

	KCL_MODE |= KCLMD_SILENT;
	if (KCL_MODE & KCLMD_DROP_AUTO)
		SetKclMode (KCL_MODE | KCLMD_DROP_UNUSED);

	const ColorMode_t colmode = GetFileColorized (stdout);
	ccp col_minus = GetTextMode (colmode, TTM_BOLD | TTM_RED);
	ccp col_plus = GetTextMode (colmode, TTM_BOLD | TTM_GREEN);
	ccp col_misc = GetTextMode (colmode, TTM_BOLD | TTM_CYAN);
	ccp col_reset = GetTextMode (colmode, TTM_RESET);

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);
		if (err)
		{
			if (max_err < err)
				max_err = err;
			ResetSZS (&szs);
			continue;
		}
		if (opt_norm || need_norm > 0)
			NormalizeSZS (&szs);
		// PatchSZS(&szs);

		int slot[MKW_N_TRACKS];
		int required_slot;
		const int stat = FindSlotsSZS (&szs, slot, &required_slot);

		noPRINT ("IS_ARENA=%d\n", szs.is_arena);
		if (szs.is_arena >= ARENA_FOUND)
		{
			if (brief_count)
				printf ("%sARENA%s     : %s\n", col_misc, col_reset, arg);
			else
				printf ("    %s ARENA %s     : %s\n", col_misc, col_reset, arg);
		}
		else
		{
			if (brief_count)
			{
				uint i, count = 0;
				for (i = 0; i < MKW_N_TRACKS; i++)
					if (slot[i] && slot[i] != stat)
					{
						ccp col = slot[i] < 0 ? col_minus : slot[i] > 0 ? col_plus : col_misc;
						printf ("%s%c%u%u", col,
							count		   ? ','
								: stat < 0 ? '+'
										   : '-',
							i / 4 + 1, i % 4 + 1);
						count++;
					}

				while (count++ < 3)
					fputs ("   ", stdout);
			}
			else
			{
				uint i, count = 0;
				for (i = 0; i < MKW_N_TRACKS; i++)
					if (slot[i] != stat)
					{
						ccp col = slot[i] < 0 ? col_minus : slot[i] > 0 ? col_plus : col_misc;
						printf (" %s%c%u.%u", col,
							slot[i] < 0		  ? '-'
								: slot[i] > 0 ? '+'
											  : '?',
							i / 4 + 1, i % 4 + 1);
						count++;
					}
					else if (required_slot != 31 && (i == 13 || i == 20 || i == 21))
					{
						fputs ("     ", stdout);
						count++;
					}

				while (count++ < 3)
					fputs ("     ", stdout);
			}
			printf ("%s : %s\n", col_reset, arg);
		}
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command isarena			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_is_arena ()
{
	SetPatchFileModeReadonly ();

	if (brief_count || verbose < 0)
		print_header = false;

	if (print_header)
		printf ("\n%sstatus   filename%s\n%s%.237s%s\n", colout->heading, colout->reset,
			colout->heading, ThinLine300_3, colout->reset);

	int count = 0;
	enumError max_err = ERR_OK;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, true);

		ccp status = 0, color = 0;

		if (err == ERR_NOT_EXISTS)
		{
			if (opt_ignore)
				err = ERR_OK;
			else
			{
				status = "!NO-FILE";
				color = colout->bad;
			}
		}
		else if (err)
		{
			status = "!ERROR";
			color = colout->bad;
		}
		else
		{
			FindSpecialFilesSZS (&szs, false);
			if (!szs.course_kmp_data)
			{
				status = "!NO-KMP";
				color = colout->bad;
			}
			else
			{
				kmp_t kmp;
				InitializeKMP (&kmp);
				err = ScanKMP (&kmp, false, szs.course_kmp_data, szs.course_kmp_size, 0);
				if (err)
				{
					status = "!KMP-ERR";
					color = colout->bad;
				}
				else
				{
					const IsArena_t is_arena = IsArenaKMP (&kmp);
					switch (is_arena)
					{
						case ARENA_NONE:
							err = ERR_DIFFER;
							status = "-RACE";
							color = colout->b_orange;
							break;

						case ARENA_MAYBE:
							err = ERR_DIFFER;
							status = "?MAYBE";
							color = colout->b_yellow;
							break;

						case ARENA_FOUND:
							status = "+ARENA";
							color = colout->b_green;
							break;

						case ARENA_DISPATCH:
							status = "+DISPATCH";
							color = colout->b_green;
							break;

						case ARENA__N:
							break;
					}

					if (!status)
					{
						status = "!UNKNOWN";
						color = colout->bad;
					}
				}
				ResetKMP (&kmp);
			}
		}

		if (status)
		{
			count++;
			if (verbose >= 0)
			{
				DASSERT (color);
				if (brief_count)
					printf ("%s%s%s\n", color, status, colout->reset);
				else
					printf ("%s%-9s%s %s\n", color, status, colout->reset, arg);
			}
		}

		ResetSZS (&szs);
		if (max_err < err)
			max_err = err;
	}

	if (print_header)
		putchar ('\n');

	ResetStringField (&plist);
	return count ? max_err : ERR_NOTHING_TO_DO;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command patch			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool ScanByExtFF (file_format_t *ff_arch, file_format_t *ff_compr, ccp fname)
{
	DASSERT (ff_arch);
	DASSERT (ff_compr);
	*ff_arch = FF_UNKNOWN;
	*ff_compr = FF_UNKNOWN;

	ccp point = strrchr (fname, '.');
	if (!point)
		return false;

	++point;
	if (!strcmp (point, "szs"))
	{
		*ff_arch = FF_U8;
		*ff_compr = FF_YAZ0;
		return true;
	}

	if (!strcmp (point, "wbz"))
	{
		*ff_arch = FF_WU8;
		*ff_compr = FF_BZ;
		return true;
	}

	if (!strcmp (point, "bz2") || !strcmp (point, "bzip2"))
	{
		*ff_arch = FF_U8;
		*ff_compr = FF_BZIP2;
		return true;
	}

	if (!strcmp (point, "wlz"))
	{
		*ff_arch = FF_WU8;
		*ff_compr = FF_LZ;
		return true;
	}

	if (!strcmp (point, "lzma"))
	{
		*ff_arch = FF_U8;
		*ff_compr = FF_LZMA;
		return true;
	}

#if 0 // [[xz+]]
    if (!strcmp(point,"xz"))
    {
	*ff_arch  = FF_U8;
	*ff_compr = FF_XZ;
	return true;
    }
#endif

	if (!strcmp (point, "u8"))
	{
		*ff_arch = FF_U8;
		return true;
	}

	if (!strcmp (point, "wu8"))
	{
		*ff_arch = FF_WU8;
		return true;
	}

	if (!strcmp (point, "xyz"))
	{
		*ff_arch = FF_XYZ;
		return true;
	}

	if (!strcmp (point, "lta"))
	{
		*ff_arch = FF_LTA;
		return true;
	}

	if (!strcmp (point, "lfl"))
	{
		*ff_arch = FF_LFL;
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////

// old [[container]] will be replaced by Container_t & ContainerData_t
#define PRINT_DATA                                                                                 \
	PRINT ("##### %s() #%u: cd=%p,%zu,%d / d=%p,%zu,%d / c=%p,%d #####\n", __FUNCTION__, __LINE__, \
		szs.cdata, szs.csize, szs.cdata_alloced, szs.data, szs.size, szs.data_alloced,             \
		szs.old_container ? szs.old_container->data : 0,                                           \
		szs.old_container ? szs.old_container->ref_count : -1)

//-----------------------------------------------------------------------------

struct cmd_patch_search_t
{
	int count;
	enumError max_err;
};

//-----------------------------------------------------------------------------

static enumError patch_file_helper (ccp fname // file name of source
)
{
	PRINT0 ("patch_file_helper(%s)\n", fname);

	exmem_t orig = { 0 }, data = { 0 };

	szs_file_t szs;
	InitializeSZS (&szs);
	enumError err = LoadSZS (&szs, fname, false, opt_ignore > 0, true);

	if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
	{
		const bool was_compressed = szs.cdata != 0;
		if (was_compressed)
		{
			DecompressSZS (&szs, false, 0);
			DASSERT (szs.cdata);
			DASSERT (szs.cdata_alloced);
			orig = ExMemMove (szs.cdata, szs.csize);
			szs.cdata = 0;
			szs.cdata_alloced = false;
			data = ExMemDup (szs.data, szs.size);
		}
		else
		{
			DASSERT (szs.data);
			data = ExMemDup (szs.data, szs.size);
#if 1
			orig = data;
			orig.is_alloced = false;
#else
			orig = ExMemDup (szs.data, szs.size);
#endif
		}

		const ccp src_format = GetNameFF_SZS (&szs);
		const bool convert_wu8
			= opt_fform == FF_WU8 || opt_fform == FF_UNKNOWN && szs.fform_arch == FF_WU8;
		PRINT ("CONVERT_WU8=%d, FF=%s\n", convert_wu8, src_format);

		DecodeWU8 (&szs);

		// szs.allow_ext_data = opt_norm;
		szs.allow_ext_data = true;

		bool dirty = PatchSZS (&szs);
		// xBINGO;
		PRINT0 ("dirty=%d, opt_norm=%d, need_norm=%d, opt_auto_add=%d, opt_cup_icons=%s\n", dirty,
			opt_norm, need_norm, opt_auto_add, opt_cup_icons);

		if ((opt_norm || need_norm || opt_auto_add || opt_cup_icons || dirty)
			&& NormalizeSZS (&szs))
		{
			dirty = true;
		}
		if (OptionUsed[OPT_MINIMAP] && AutoAdjustMinimap (&szs))
			dirty = true;

		if (convert_wu8)
		{
			err = EncodeWU8 (&szs);
			if (err)
				goto abort;
		}

		//--- dest file

		char dest[PATH_MAX];
		if (opt_fform == FF_U8 || opt_fform == FF_WU8)
		{
			SubstDest (dest, sizeof (dest), fname, opt_dest && *opt_dest ? opt_dest : "\1P/\1N\1?T",
				"\1N\1?T", GetExtFF (szs.fform_file, opt_fform), false);
		}
		else if (opt_dest && *opt_dest)
		{
			SubstDest (dest, sizeof (dest), fname, opt_dest, 0,
				GetExtFF (szs.fform_file, szs.fform_arch), false);
		}
		else
			StringCopyS (dest, sizeof (dest), fname);

		//--- check dirty

		file_format_t ff_file = was_compressed ? GetNewCompressionSZS (&szs) : szs.fform_file;
		const ccp dest_format = GetNameFF (ff_file, szs.fform_current);
		const bool src_dest_diff = strcmp (src_format, dest_format) || strcmp (dest, fname);

		if (!dirty)
			dirty = data.data.len != szs.size || memcmp (data.data.ptr, szs.data, szs.size);

		if (!dirty && was_compressed)
		{
			CompressSZS (&szs, 0, true);
			dirty = orig.data.len != szs.csize || memcmp (orig.data.ptr, szs.cdata, szs.csize);
			FreeExMem (&orig);
			orig = ExMemByS (szs.cdata, szs.csize);
		}

		if (!dirty && (!src_dest_diff || opt_no_copy))
			goto abort;

		//--- compression and SZS cache

		if (dirty || src_dest_diff)
		{
			if (was_compressed)
			{
				CompressSZS (&szs, 0, true);
				FreeExMem (&orig);
				orig = ExMemByS (szs.cdata, szs.csize);
			}
			else
			{
				FreeExMem (&orig);
				orig = ExMemByS (szs.data, szs.size);
			}
		}

		if (dirty || src_dest_diff)
		{
			if (verbose >= 0 || testmode)
			{
				if (src_dest_diff)
					fprintf (stdlog, "%s%s%s %s:%s -> %s:%s\n", testmode ? "WOULD " : "",
						opt_norm	? "NORMALIZE"
							: dirty ? "PATCH"
									: "COPY ",
						szs.aiparam_removed ? " [-AIParam]" : "", src_format, fname, dest_format,
						dest);
				else
					fprintf (stdlog, "%s%s%s %s:%s\n", testmode ? "WOULD " : "",
						opt_norm ? "NORMALIZE" : "PATCH", szs.aiparam_removed ? " [-AIParam]" : "",
						dest_format, dest);
				fflush (stdlog);
			}

			File_t F;
			err = CreateFileOpt (&F, true, dest, testmode, fname);
			if (F.f)
			{
				SetFileAttrib (&F.fatt, &szs.fatt, 0);
				size_t wstat = fwrite (orig.data.ptr, 1, orig.data.len, F.f);
				if (wstat != orig.data.len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED,
						"Writing %u bytes failed [%zu bytes written]: %s\n", orig.data.len, wstat,
						dest);
			}
			ResetFile (&F, opt_preserve);
			if (!err && opt_remove_src)
				RemoveSource (fname, dest, verbose >= 0, testmode);
		}
		else if (verbose > 0)
		{
			fprintf (stdlog, "ALREADY NORMALIZED: %s:%s\n", src_format, fname);
			fflush (stdlog);
		}

		LinkCacheData (szs.cache_fname, dest, orig.data.ptr, orig.data.len);
	}

abort:;
	FreeExMem (&orig);
	FreeExMem (&data);
	ResetSZS (&szs);
	return parallel_count > 0 && err == ERR_NOTHING_TO_DO ? ERR_OK : err;
}

//-----------------------------------------------------------------------------

static enumError cmd_patch ()
{
#if HAVE_PRINT
	file_format_t dest_ff_arch;
	file_format_t dest_ff_compr;
	const bool dest_ff_valid = ScanByExtFF (&dest_ff_arch, &dest_ff_compr, opt_dest);
	PRINT ("fform(dest) = %s [valid=%d]\n", GetNameFF (dest_ff_compr, dest_ff_arch), dest_ff_valid);
#endif

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		const enumError err = patch_file_helper (arg);
		if (max_err < err)
			max_err = err;
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command normalize		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_normalize ()
{
	RegisterOptionByIndex (&InfoUI_wszst, OPT_NORM, 1, false);
	opt_norm = true;
	return cmd_patch ();
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command copy			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_copy ()
{
	RegisterOptionByIndex (&InfoUI_wszst, OPT_OVERWRITE, 1, false);
	opt_overwrite = true;

	if (!opt_dest)
	{
		if (!first_param)
			return ERROR0 (ERR_MISSING_PARAM, "Missing destination parameter!\n");

		ParamList_t *param;
		for (param = first_param; param->next; param = param->next)
			;
		ASSERT (param);
		ASSERT (!param->next);
		SetDest (param->arg, false);
		param->arg = 0;
	}

	return cmd_patch ();
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command duplicate		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_duplicate ()
{
	RegisterOptionByIndex (&InfoUI_wszst, OPT_OVERWRITE, 1, false);
	opt_overwrite = true;

	//--- check parameters

	if (!first_param)
		return ERROR0 (ERR_MISSING_PARAM, "Missing source file!\n");

	if (!opt_dest)
	{
		if (!first_param->next)
			return ERROR0 (ERR_MISSING_PARAM, "Missing destination parameter!\n");

		SetDest (first_param->next->arg, false);
		n_param--;
	}

	if (n_param != 1)
		return ERROR0 (ERR_SEMANTIC, "Exact 1 source file expected!\n");

	NORMALIZE_FILENAME_PARAM (first_param);
	ccp suffix, dest1 = opt_dest, dest2;
	{
		ccp slash = strrchr (dest1, '/');
		if (!slash)
			slash = dest1;

		char *pos = strchr (slash, '@');
		if (pos)
		{
			*pos = 0;
			dest2 = pos + 1;
			while (*dest2 == ' ')
				dest2++;
			suffix = !*dest2 || *dest2 == '.' ? "" : " ";
			while (pos > dest1 && pos[-1] == ' ')
				*--pos = 0;
		}
		else
		{
			pos = strrchr (slash, '.');
			if (pos)
			{
				*pos = 0;
				dest2 = pos + 1;
				suffix = ".";
			}
			else
				dest2 = suffix = EmptyString;
		}
	}

	//--- open source

	szs_file_t szs;
	InitializeSZS (&szs);
	enumError err = LoadSZS (&szs, first_param->arg, false, opt_ignore > 0, true);
	if (err)
		return err;

	const bool was_compressed = szs.cdata != 0;
	if (was_compressed)
		DecompressSZS (&szs, false, 0);

	const ccp fform = GetNameFF_SZS (&szs);
	if (verbose >= 0)
	{
		fprintf (stdlog, "READ %s:%s\n", fform, first_param->arg);
		fflush (stdlog);
	}

	if (szs.fform_current != FF_U8 && szs.fform_current != FF_WU8)
		// [[lta]] [[2do]]
		// [[lfl]] [[2do]]
		return ERROR0 (ERR_INVALID_DATA, "A track file (SZS or WBZ) expected!\n");

	force_lex_test = true; // force patch to insert LEX/TEST
	PatchSZS (&szs);

	//--- find KMP

	FindSpecialFilesSZS (&szs, false);
	if (!szs.course_kmp_data)
		return ERROR0 (ERR_INVALID_DATA, "KMP not found → abort: %s\n", first_param->arg);

	kmp_t kmp;
	err = ScanKMP (&kmp, true, szs.course_kmp_data, szs.course_kmp_size, 0);

	//--- find LEX/TEST

	if (!szs.course_lex_data)
		return ERROR0 (ERR_INTERNAL, 0);

	lex_element_t *lex_test
		= FindLexElement ((lex_header_t *)szs.course_lex_data, szs.course_lex_size, LEXS_TEST);
	if (!lex_test)
		return ERROR0 (ERR_INTERNAL, 0);

	kmp_ana_pflag_t ap;
	AnalysePFlagScenarios (&ap, &kmp, opt_gamemodes);

	char buf[10];
	const int fw = snprintf (buf, sizeof (buf), "%u", ap.n_res);

	uint ri;
	const kmp_ana_pflag_res_t *res;
	for (ri = 0, res = ap.res_list; ri < ap.n_res; ri++, res++)
	{
		memcpy (lex_test->data, &res->lex_test, sizeof (res->lex_test));
		char fname[PATH_MAX];
		snprintf (fname, sizeof (fname), "%s {%02u,%s}%s%s", dest1, res->version + 1, res->name,
			suffix, dest2);
		if (verbose >= 0 || testmode > 0)
		{
			fprintf (stdlog, "%sCREATE %*u/%u %s:%s\n", testmode > 0 ? "WOULD " : "", fw, ri + 1,
				ap.n_res, fform, fname);
			fflush (stdlog);
		}

		if (was_compressed)
		{
			ClearCompressedSZS (&szs);
			CompressSZS (&szs, 0, false);
		}

		SaveSZS (&szs, fname, true, was_compressed);
	}

	ResetKMP (&kmp);
	ResetSZS (&szs);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command minimap			///////////////
///////////////////////////////////////////////////////////////////////////////

static void print_mmap (ccp title, float3 *a, float3 *b)
{
	DASSERT (title);
	DASSERT (a);
	DASSERT (b);

	printf (
		"%-13s %9.1f %9.1f %9.1f  %20.1f %9.1f %9.1f\n", title, a->x, a->y, a->z, b->x, b->y, b->z);
}

///////////////////////////////////////////////////////////////////////////////

static void print_matrix (ccp title, float *a, float *b)
{
	DASSERT (title);
	DASSERT (a);
	DASSERT (b);

	uint i;
	for (i = 0; i < 3; i++)
	{
		printf ("%s-Matrix-%c: %9.1f %9.1f %9.1f %9.1f   %9.1f %9.1f %9.1f %9.1f\n", title, 'X' + i,
			a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
		a += 4;
		b += 4;
	}
}

///////////////////////////////////////////////////////////////////////////////

static void print_data (mdl_minimap_t *mmap, // valid minimap record
	mdl_sect1_t *root, // NULL or valid 'root' data
	mdl_sect1_t *ld, // valid 'posLD' data
	mdl_sect1_t *ru, // valid 'posRU' data
	uint mode, // general print mode
	uint modified // 0:none, 1:translate, 2:all, 3:all+minimum
)
{
	if (modified != 1)
	{
		printf ("Idx+Id+Flags: %9u %9u %#9x %21u %9u %#9x\n", ld->index, ld->id, ld->flags,
			ru->index, ru->id, ru->flags);
		print_mmap ("Scale:", &ld->scale, &ru->scale);
		print_mmap ("Rotation", &ld->rotate, &ru->rotate);
	}

	print_mmap ("Translation:", &ld->translate, &ru->translate);
	if (mmap->rec_trans_valid && (!modified || mode > 0))
		print_mmap (" Recommend.:", &mmap->rec_trans_ld, &mmap->rec_trans_ru);

	if (mode > 1 && !modified)
	{
		print_mmap ("Minimum:", &ld->minimum, &ru->minimum);
		print_mmap ("Maximum:", &ld->maximum, &ru->maximum);
	}

	if (mode > 0)
	{
		putchar ('\n');
		print_matrix ("Tra", ld->trans_matrix.v, ru->trans_matrix.v);
		putchar ('\n');
		print_matrix ("Inv", ld->inv_matrix.v, ru->inv_matrix.v);
	}
	printf ("%.95s\n", Minus300);

	if (mode > 2 && !modified || modified > 2)
	{
		printf ("Vertex-Min: %11.1f %9.1f %9.1f\n", mmap->min.x, mmap->min.y, mmap->min.z);
		printf ("Vertex-Max: %11.1f %9.1f %9.1f\n", mmap->max.x, mmap->max.y, mmap->max.z);
		if (mmap->root)
		{
			printf ("Root-Min:   %11.1f %9.1f %9.1f\n", root->minimum.x, root->minimum.y,
				root->minimum.z);
			printf ("Root-Max:   %11.1f %9.1f %9.1f\n", root->maximum.x, root->maximum.y,
				root->maximum.z);
		}
		printf ("%.95s\n", Minus300);
	}
}

///////////////////////////////////////////////////////////////////////////////

static void center_mmap (uint idx, float3 *a, float3 *b)
{
	DASSERT (idx < 3);
	DASSERT (a);
	DASSERT (b);

	const float new_val = (a->v[idx] - b->v[idx]) / 2;
	a->v[idx] = +new_val;
	b->v[idx] = -new_val;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_minimap ()
{
	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadSZS (&szs, arg, false, opt_ignore > 0, false);

		if (err <= ERR_WARNING && err != ERR_NOT_EXISTS)
		{
			if (verbose >= 0)
				fprintf (stdlog, "\nMinimap data of %s:%s\n", GetNameFF_SZS (&szs), arg);

			const bool was_compressed = szs.cdata != 0;
			if (was_compressed)
				DecompressSZS (&szs, true, 0);

			bool dirty = false;
			mdl_minimap_t mmap;
			if (FindMinimapData (&mmap, &szs))
			{
				DASSERT (mmap.posLD);
				DASSERT (mmap.posRU);
				PRINT ("MMAP FOUND!\n");

				mdl_sect1_t root, ld, ru;
				ntoh_MDLs1 (&root, mmap.root);
				ntoh_MDLs1 (&ld, mmap.posLD);
				ntoh_MDLs1 (&ru, mmap.posRU);

				if (verbose >= 0)
				{
					printf ("\n%53s%42s\n%.95s\n", "______________left-down_______________",
						"_______________right-up_______________", Minus300);
					print_data (&mmap, &root, &ld, &ru, long_count, 0);
				}

				if (transform_active || have_set_scale || have_set_rot || have_set_value
					|| OptionUsed[OPT_TOUCH] || OptionUsed[OPT_AUTO] || OptionUsed[OPT_XCENTER]
					|| OptionUsed[OPT_YCENTER] || OptionUsed[OPT_ZCENTER] || OptionUsed[OPT_CENTER]
					|| set_flags != M1 (set_flags))
				{
					uint modified = 1;
					if (OptionUsed[OPT_AUTO] && mmap.rec_trans_valid)
					{
						modified = 2;
						ClearMDLs1 (&ld, false);
						ld.translate = mmap.rec_trans_ld;
						ClearMDLs1 (&ru, false);
						ru.translate = mmap.rec_trans_ru;
					}

					if (set_flags != M1 (set_flags))
					{
						modified = 2;
						ld.flags = ru.flags = set_flags;
					}

					if (have_set_scale)
					{
						modified = 2;
						ld.scale.x = ru.scale.x = set_scale.x;
						ld.scale.y = ru.scale.y = set_scale.y;
						ld.scale.z = ru.scale.z = set_scale.z;
					}

					if (have_set_rot)
					{
						modified = 2;
						ld.rotate.x = ru.rotate.x = set_rot.x;
						ld.rotate.y = ru.rotate.y = set_rot.y;
						ld.rotate.z = ru.rotate.z = set_rot.z;
					}

					if (have_set_value & 1)
					{
						ld.translate.x = set_value_min.x;
						ru.translate.x = set_value_max.x;
					}
					if (have_set_value & 2)
					{
						ld.translate.y = set_value_min.y;
						ru.translate.y = set_value_max.y;
					}
					if (have_set_value & 4)
					{
						ld.translate.z = set_value_max.z;
						ru.translate.z = set_value_min.z;
					}

					if (OptionUsed[OPT_XCENTER] || OptionUsed[OPT_CENTER])
						center_mmap (0, &ld.translate, &ru.translate);
					if (OptionUsed[OPT_YCENTER] || OptionUsed[OPT_CENTER])
						center_mmap (1, &ld.translate, &ru.translate);
					if (OptionUsed[OPT_ZCENTER] || OptionUsed[OPT_CENTER])
						center_mmap (2, &ld.translate, &ru.translate);

					TransformPosFloat3D (ld.translate.v, 1, 0);
					TransformPosFloat3D (ru.translate.v, 1, 0);
					CalcMatrixMDLs1 (&ld);
					CalcMatrixMDLs1 (&ru);

					hton_MDLs1 (&ld, &ld);
					hton_MDLs1 (&ru, &ru);
					dirty = memcmp (mmap.posLD, &ld, sizeof (ld))
						|| memcmp (mmap.posRU, &ru, sizeof (ru));

#if 1
					if (mmap.root)
					{
						root.minimum = mmap.min;
						root.maximum = mmap.max;

#if 0 // workaround fails
	  // workaround for a bug in KMP modifier
			if ( root.minimum.x > root.minimum.z )
			     root.minimum.x = root.minimum.z;
			if ( root.maximum.x < root.maximum.z )
			     root.maximum.x = root.maximum.z;
#endif

						hton_MDLs1 (&root, &root);
						if (memcmp (mmap.root, &root, sizeof (root)))
						{
							memcpy (mmap.root, &root, sizeof (root));
							modified = 3;
							dirty = true;
						}
						ntoh_MDLs1 (&root, &root);
					}
#else
					if (mmap.root
						&& (memcmp (&root.minimum, &mmap.min, sizeof (root.minimum))
							|| memcmp (&root.maximum, &mmap.max, sizeof (root.maximum))))
					{
						root.minimum = mmap.min;
						root.maximum = mmap.max;

						hton_MDLs1 (mmap.root, &root);
						modified = 3;
						dirty = true;
					}
#endif

					if (dirty)
					{
						memcpy (mmap.posLD, &ld, sizeof (ld));
						memcpy (mmap.posRU, &ru, sizeof (ru));
						ntoh_MDLs1 (&ld, &ld);
						ntoh_MDLs1 (&ru, &ru);
						if (verbose >= 0)
							print_data (&mmap, &root, &ld, &ru, long_count, modified);
					}
				}
			}

			if (dirty)
			{
				if (opt_norm || need_norm > 0)
					NormalizeSZS (&szs);

				if (verbose >= 0 || testmode)
				{
					fprintf (stdlog, "%sPATCH MINIMAP %s:%s\n", testmode ? "WOULD " : "",
						GetNameFF_SZS (&szs), arg);
					fflush (stdlog);
				}

				if (!testmode)
					SaveSZS (&szs, szs.fname, true, was_compressed);
			}
		}
		fflush (stdlog);

		if (max_err < err)
			max_err = err;
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command compress		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError EncodeZlib (
	u8 **dest, uint *dest_size, const u8 *src, size_t src_size, bool raw_deflate)
{
	if (!dest || !dest_size || !src)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;
	uLongf bound = compressBound ((uLong)src_size) + 64;
	u8 *buf = MALLOC (bound);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	int windowBits = raw_deflate ? -15 : 15;
	if (deflateInit2 (&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY)
		!= Z_OK)
	{
		FREE (buf);
		return ERR_INVALID_DATA;
	}
	strm.next_in = (Bytef *)src;
	strm.avail_in = (uInt)src_size;
	strm.next_out = buf;
	strm.avail_out = (uInt)bound;
	int ret = deflate (&strm, Z_FINISH);
	deflateEnd (&strm);
	if (ret != Z_STREAM_END)
	{
		FREE (buf);
		return ERR_INVALID_DATA;
	}
	*dest = buf;
	*dest_size = (uint)strm.total_out;
	return ERR_OK;
}

static enumError DecodeZlib (
	u8 **dest, uint *dest_size, const u8 *src, size_t src_size, bool raw_deflate)
{
	if (!dest || !dest_size || !src)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;
	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	int windowBits = raw_deflate ? -15 : 15;
	if (inflateInit2 (&strm, windowBits) != Z_OK)
		return ERR_INVALID_DATA;

	size_t out_cap = src_size * 4 + 1024;
	u8 *buf = MALLOC (out_cap);
	if (!buf)
	{
		inflateEnd (&strm);
		return ERR_OUT_OF_MEMORY;
	}

	strm.next_in = (Bytef *)src;
	strm.avail_in = (uInt)src_size;
	strm.next_out = buf;
	strm.avail_out = (uInt)out_cap;

	while (1)
	{
		int ret = inflate (&strm, Z_NO_FLUSH);
		if (ret == Z_STREAM_END)
			break;
		if (ret != Z_OK && ret != Z_BUF_ERROR)
		{
			FREE (buf);
			inflateEnd (&strm);
			return ERR_INVALID_DATA;
		}
		if (strm.avail_out == 0)
		{
			size_t new_cap = out_cap * 2;
			u8 *new_buf = REALLOC (buf, new_cap);
			if (!new_buf)
			{
				FREE (buf);
				inflateEnd (&strm);
				return ERR_OUT_OF_MEMORY;
			}
			buf = new_buf;
			strm.next_out = buf + out_cap;
			strm.avail_out = (uInt)(new_cap - out_cap);
			out_cap = new_cap;
		}
	}
	*dest_size = (uint)strm.total_out;
	inflateEnd (&strm);
	*dest = buf;
	return ERR_OK;
}

static enumError compress_nintendo_file (ccp arg)
{
	if (!opt_dest)
		return ERR_NOTHING_TO_DO;
	ccp ext = strrchr (opt_dest, '.');
	if (ext && !strcasecmp (ext, ".wux"))
	{
		char dest[PATH_MAX];
		SubstDest (dest, sizeof (dest), arg, opt_dest, ext, ext, false);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sCOMPRESS WUX:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dest);
		if (testmode)
			return ERR_OK;
		return wux_compress (arg, dest) ? ERR_OK : ERR_WRITE_FAILED;
	}
	if (!ext
		|| (strcasecmp (ext, ".lz10") && strcasecmp (ext, ".lz11") && strcasecmp (ext, ".cmp")
			&& strcasecmp (ext, ".rl") && strcasecmp (ext, ".yay0") && strcasecmp (ext, ".ash")
			&& strcasecmp (ext, ".ash0") && strcasecmp (ext, ".lzh8") && strcasecmp (ext, ".lh") && strcasecmp (ext, ".qlz")
			&& strcasecmp (ext, ".at7") && strcasecmp (ext, ".at7p") && strcasecmp (ext, ".blz")
			&& strcasecmp (ext, ".huff4") && strcasecmp (ext, ".huff8") && strcasecmp (ext, ".huff")
			&& strcasecmp (ext, ".stpl") && strcasecmp (ext, ".camelot") && strcasecmp (ext, ".rnc")
			&& strcasecmp (ext, ".rnc1") && strcasecmp (ext, ".rnc2") && strcasecmp (ext, ".romc")
			&& strcasecmp (ext, ".fzip") && strcasecmp (ext, ".zlib") && strcasecmp (ext, ".deflate")
			&& strcasecmp (ext, ".mvdk")))
		return ERR_NOTHING_TO_DO;
	u8 *data = 0, *packed = 0;
	size_t file_size = 0;
	uint packed_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &data, &file_size, 0, 0, 0, false);
	if (!err && file_size > UINT_MAX)
		err = ERR_FILE_TOO_BIG;
	if (!err)
		err = !strcasecmp (ext, ".rl") ? EncodeNintendoRL (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".ash") || !strcasecmp (ext, ".ash0")
			? EncodeASH0 (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".yay0") ? EncodeYay0 (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".lzh8") || !strcasecmp (ext, ".lh")
			? EncodeLZH8 (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".qlz")	 ? EncodeQuickLZ (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".at7") || !strcasecmp (ext, ".at7p")
			? EncodeAT7 (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".blz") ? EncodeBLZ (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".huff4")
			? EncodeNintendoHuff (&packed, &packed_size, data, file_size, true)
			: !strcasecmp (ext, ".huff8") || !strcasecmp (ext, ".huff")
			? EncodeNintendoHuff (&packed, &packed_size, data, file_size, false)
			: !strcasecmp (ext, ".stpl") || !strcasecmp (ext, ".camelot")
			? EncodeCamelot (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".romc") ? EncodeRomC (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".rnc") || !strcasecmp (ext, ".rnc1") || !strcasecmp (ext, ".rnc2")
			? EncodeRNC (&packed, &packed_size, data, file_size, !strcasecmp (ext, ".rnc1") ? 1 : 2)
			: !strcasecmp (ext, ".fzip") ? EncodeFZIP (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".zlib")
			? EncodeZlib (&packed, &packed_size, data, file_size, false)
			: !strcasecmp (ext, ".deflate")
			? EncodeZlib (&packed, &packed_size, data, file_size, true)
			: !strcasecmp (ext, ".mvdk") ? EncodeMVDK (&packed, &packed_size, data, file_size)
			: !strcasecmp (ext, ".cmp") || !strcasecmp (ext, ".lz11")
			? EncodeLZ10LZ11 (&packed, &packed_size, data, file_size, true)
			: EncodeLZ10LZ11 (&packed, &packed_size, data, file_size, false);
	FREE (data);
	if (err)
	{
		FREE (packed);
		return err;
	}
	char dest[PATH_MAX];
	SubstDest (dest, sizeof (dest), arg, opt_dest, ext, ext, false);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sCOMPRESS %s:%s -> RAW:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "",
			!strcasecmp (ext, ".rl")										  ? "RL"
				: !strcasecmp (ext, ".ash") || !strcasecmp (ext, ".ash0")	  ? "ASH0"
				: !strcasecmp (ext, ".yay0")								  ? "Yay0"
				: !strcasecmp (ext, ".lzh8") || !strcasecmp (ext, ".lh")	  ? "LH"
				: !strcasecmp (ext, ".qlz")									  ? "QuickLZ"
				: !strcasecmp (ext, ".at7") || !strcasecmp (ext, ".at7p")	  ? "AT7"
				: !strcasecmp (ext, ".blz")									  ? "BLZ"
				: !strcasecmp (ext, ".huff4")								  ? "Huffman4"
				: !strcasecmp (ext, ".huff8") || !strcasecmp (ext, ".huff")	  ? "Huffman8"
				: !strcasecmp (ext, ".stpl") || !strcasecmp (ext, ".camelot") ? "Camelot"
				: !strcasecmp (ext, ".romc")								  ? "romc"
				: !strcasecmp (ext, ".rnc") || !strcasecmp (ext, ".rnc1")
					|| !strcasecmp (ext, ".rnc2")
				? "RNC"
				: !strcasecmp (ext, ".fzip")	? "FZIP"
				: !strcasecmp (ext, ".zlib")	? "Zlib"
				: !strcasecmp (ext, ".deflate") ? "Deflate"
				: !strcasecmp (ext, ".mvdk")	? "MVDK"
				: !strcasecmp (ext, ".cmp")		? "CMP"
				: !strcasecmp (ext, ".lz11")	? "LZ11"
												: "LZ10",
			arg, dest);
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (packed, 1, packed_size, F.f) != packed_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", packed_size, dest);
		ResetFile (&F, opt_preserve);
	}
