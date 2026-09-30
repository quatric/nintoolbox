//
///////////////////////////////////////////////////////////////////////////////
///////////////			convert helper			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool ConvertHelper (const u8 *data, // valid data
	uint data_size, // size of 'data'
	ContainerData_t *cdata, // NULL or container data
	ccp src_fname, // filename of source
	ccp dest_fname, // filename of destination
	bool binary_dest, // output: false:text, true:binary
	enumError *err // not NULL: store error code here
)
{
	enumError temp_err;
	if (!err)
		err = &temp_err;
	*err = ERR_OK;

	// [[analyse-magic]]
	const file_format_t fform = GetByMagicFF (data, data_size, data_size);
	switch (fform)
	{
		case FF_BMG:
		case FF_BMG_TXT:
			FreeContainerData (cdata);
			{
				bmg_t bmg;
				*err = ScanBMG (&bmg, true, src_fname, data, data_size);
				if (*err)
					return true;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawXBMG (&bmg, dest_fname, true)
								  : SaveTextXBMG (&bmg, dest_fname, true);
				ResetBMG (&bmg);
			}
			return true;

		case FF_MSBT:
		case FF_MSBT_TXT:
			FreeContainerData (cdata);
			{
				if (fform == FF_MSBT)
				{
					msbt_file_t msbt;
					*err = ScanMSBT (&msbt, data, data_size, src_fname);
					if (!*err && !testmode)
						*err = SaveTextMSBT (&msbt, dest_fname);
					ResetMSBT (&msbt);
				}
				else
				{
					msbt_file_t msbt;
					*err = LoadTextMSBT (&msbt, src_fname);
					if (!*err && !testmode)
					{
						u8 *out_data = 0;
						uint out_size = 0;
						*err = CreateMSBT (&out_data, &out_size, &msbt);
						if (!*err && out_data)
						{
							File_t F;
							*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
							if (F.f)
								fwrite (out_data, 1, out_size, F.f);
							ResetFile (&F, opt_preserve);
							FREE (out_data);
						}
					}
					ResetMSBT (&msbt);
				}
			}
			return true;

		case FF_MSBP:
		case FF_MSBP_TXT:
			FreeContainerData (cdata);
			{
				if (fform == FF_MSBP)
				{
					msbp_file_t msbp;
					*err = ScanMSBP (&msbp, data, data_size, src_fname);
					if (!*err && !testmode)
						*err = SaveTextMSBP (&msbp, dest_fname);
					ResetMSBP (&msbp);
				}
				else
				{
					msbp_file_t msbp;
					*err = LoadTextMSBP (&msbp, src_fname);
					if (!*err && !testmode)
					{
						u8 *out_data = 0;
						uint out_size = 0;
						*err = CreateMSBP (&out_data, &out_size, &msbp);
						if (!*err && out_data)
						{
							File_t F;
							*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
							if (F.f)
								fwrite (out_data, 1, out_size, F.f);
							ResetFile (&F, opt_preserve);
							FREE (out_data);
						}
					}
					ResetMSBP (&msbp);
				}
			}
			return true;

		case FF_MSBF:
		case FF_MSBF_TXT:
			FreeContainerData (cdata);
			{
				if (fform == FF_MSBF)
				{
					msbf_file_t msbf;
					*err = ScanMSBF (&msbf, data, data_size, src_fname);
					if (!*err && !testmode)
						*err = SaveTextMSBF (&msbf, dest_fname);
					ResetMSBF (&msbf);
				}
				else
				{
					msbf_file_t msbf;
					*err = LoadTextMSBF (&msbf, src_fname);
					if (!*err && !testmode)
					{
						u8 *out_data = 0;
						uint out_size = 0;
						*err = CreateMSBF (&out_data, &out_size, &msbf);
						if (!*err && out_data)
						{
							File_t F;
							*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
							if (F.f)
								fwrite (out_data, 1, out_size, F.f);
							ResetFile (&F, opt_preserve);
							FREE (out_data);
						}
					}
					ResetMSBF (&msbf);
				}
			}
			return true;

		case FF_GH_ITEM:
		case FF_GH_IOBJ:
		case FF_GH_KART:
		case FF_GH_KOBJ:
		case FF_GH_ITEM_TXT:
		case FF_GH_IOBJ_TXT:
		case FF_GH_KART_TXT:
		case FF_GH_KOBJ_TXT:
			FreeContainerData (cdata);
			{
				geohit_t geohit;
				InitializeGEOHIT (&geohit, fform);
				geohit.fname = src_fname;
				*err = ScanGEOHIT (&geohit, false, data, data_size);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawGEOHIT (&geohit, dest_fname, true)
								  : SaveTextGEOHIT (&geohit, dest_fname, true, minimize_level);
				geohit.fname = 0;
				ResetGEOHIT (&geohit);
			}
			return true;

		case FF_KCL:
		case FF_KCL_TXT:
		case FF_WAV_OBJ:
		case FF_SKP_OBJ:
			FreeContainerData (cdata);
			{
				kcl_t kcl;
				InitializeKCL (&kcl);
				kcl.fname = src_fname;
				*err = ScanKCL (&kcl, false, data, data_size, true, global_check_mode);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawKCL (&kcl, dest_fname, true)
								  : SaveTextKCL (&kcl, dest_fname, true);
				kcl.fname = 0;
				ResetKCL (&kcl);
			}
			return true;

		case FF_ITEMSLT:
		case FF_ITEMSLT_TXT:
			FreeContainerData (cdata);
			{
				itemslot_t itemslot;
				InitializeITEMSLOT (&itemslot, fform);
				itemslot.fname = src_fname;
				*err = ScanITEMSLOT (&itemslot, false, data, data_size, src_fname);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawITEMSLOT (&itemslot, dest_fname, true)
								  : SaveTextITEMSLOT (&itemslot, dest_fname, true, minimize_level);
				itemslot.fname = 0;
				ResetITEMSLOT (&itemslot);
			}
			return true;

		case FF_KMG:
		case FF_KMG_TXT:
			FreeContainerData (cdata);
			{
				minigame_t minigame;
				InitializeMINIGAME (&minigame, fform);
				minigame.fname = src_fname;
				*err = ScanMINIGAME (&minigame, false, data, data_size);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawMINIGAME (&minigame, dest_fname, true)
								  : SaveTextMINIGAME (&minigame, dest_fname, true, minimize_level);
				minigame.fname = 0;
				ResetMINIGAME (&minigame);
			}
			return true;

		case FF_KMP:
		case FF_KMP_TXT:
			FreeContainerData (cdata);
			{
				kmp_t kmp;
				InitializeKMP (&kmp);
				kmp.fname = src_fname;
				*err = ScanKMP (&kmp, false, data, data_size, global_check_mode);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawKMP (&kmp, dest_fname, true)
								  : SaveTextKMP (&kmp, dest_fname, true);
				kmp.fname = 0;
				ResetKMP (&kmp);
			}
			return true;

		case FF_LEX:
		case FF_LEX_TXT:
			FreeContainerData (cdata);
			{
				lex_t lex;
				InitializeLEX (&lex);
				lex.fname = src_fname;
				*err = ScanLEX (&lex, false, data, data_size);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawLEX (&lex, dest_fname, true)
								  : SaveTextLEX (&lex, dest_fname, true);
				lex.fname = 0;
				ResetLEX (&lex);
			}
			return true;

		case FF_MDL:
		case FF_MDL_TXT: // [[2do]] add format to UI commands BINARY and TEXT
		{
			mdl_t mdl;
			InitializeMDL (&mdl);
			mdl.fname = src_fname;
#if USE_NEW_CONTAINER_MDL
			*err = ScanMDL (&mdl, false, data, data_size, cdata, global_check_mode);
#else
			*err = ScanMDL (&mdl, false, data, data_size, 0, global_check_mode);
#endif
			if (*err)
				return false;
			*err = testmode	  ? ERR_OK
				: binary_dest ? SaveRawMDL (&mdl, dest_fname, true)
							  : SaveTextMDL (&mdl, dest_fname, true);
			mdl.fname = 0;
			ResetMDL (&mdl);
		}
			return true;

		case FF_OBJFLOW:
		case FF_OBJFLOW_TXT:
			FreeContainerData (cdata);
			{
				objflow_t objflow;
				InitializeOBJFLOW (&objflow);
				objflow.fname = src_fname;
				*err = ScanOBJFLOW (&objflow, false, data, data_size);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawOBJFLOW (&objflow, dest_fname, true)
								  : SaveTextOBJFLOW (&objflow, dest_fname, true, minimize_level);
				objflow.fname = 0;
				ResetOBJFLOW (&objflow);
			}
			return true;

		case FF_PAT:
		case FF_PAT_TXT:
		{
			pat_t pat;
			InitializePAT (&pat);
			pat.fname = src_fname;
#if USE_NEW_CONTAINER_PAT
			*err = ScanPAT (&pat, false, data, data_size, cdata, global_check_mode);
#else
			*err = ScanPAT (&pat, false, data, data_size, 0, global_check_mode);
#endif
			if (*err)
				return false;
			*err = testmode	  ? ERR_OK
				: binary_dest ? SaveRawPAT (&pat, dest_fname, true)
							  : SaveTextPAT (&pat, dest_fname, true);
			pat.fname = 0;
			ResetPAT (&pat);
		}
			return true;

		case FF_CHR:
		case FF_CHR_TXT:
			FreeContainerData (cdata);
			{
				chr0_t chr;
				InitializeCHR0 (&chr);
				*err = fform == FF_CHR_TXT ? ScanTextCHR0 (&chr, false, src_fname)
										   : ScanRawCHR0 (&chr, false, data, data_size);
				if (*err)
				{
					ResetCHR0 (&chr);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawCHR0 (&chr, dest_fname, true)
								  : SaveTextCHR0 (&chr, dest_fname, true);
				ResetCHR0 (&chr);
			}
			return true;

		case FF_SRT:
		case FF_SRT_TXT:
			FreeContainerData (cdata);
			{
				srt0_t srt;
				InitializeSRT0 (&srt);
				*err = fform == FF_SRT_TXT ? ScanTextSRT0 (&srt, false, src_fname)
										   : ScanRawSRT0 (&srt, false, data, data_size);
				if (*err)
				{
					ResetSRT0 (&srt);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawSRT0 (&srt, dest_fname, true)
								  : SaveTextSRT0 (&srt, dest_fname, true);
				ResetSRT0 (&srt);
			}
			return true;

		case FF_VIS:
		case FF_VIS_TXT:
			FreeContainerData (cdata);
			{
				vis0_t vis;
				InitializeVIS0 (&vis);
				*err = fform == FF_VIS_TXT ? ScanTextVIS0 (&vis, false, src_fname)
										   : ScanRawVIS0 (&vis, false, data, data_size);
				if (*err)
				{
					ResetVIS0 (&vis);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawVIS0 (&vis, dest_fname, true)
								  : SaveTextVIS0 (&vis, dest_fname, true);
				ResetVIS0 (&vis);
			}
			return true;

		case FF_SCN:
		case FF_SCN_TXT:
			FreeContainerData (cdata);
			{
				scn0_t scn;
				InitializeSCN0 (&scn);
				*err = fform == FF_SCN_TXT ? ScanTextSCN0 (&scn, false, src_fname)
										   : ScanRawSCN0 (&scn, false, data, data_size);
				if (*err)
				{
					ResetSCN0 (&scn);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawSCN0 (&scn, dest_fname, true)
								  : SaveTextSCN0 (&scn, dest_fname, true);
				ResetSCN0 (&scn);
			}
			return true;

		case FF_SHP:
		case FF_SHP_TXT:
			FreeContainerData (cdata);
			{
				shp0_t shp;
				InitializeSHP0 (&shp);
				*err = fform == FF_SHP_TXT ? ScanTextSHP0 (&shp, false, src_fname)
										   : ScanRawSHP0 (&shp, false, data, data_size);
				if (*err)
				{
					ResetSHP0 (&shp);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawSHP0 (&shp, dest_fname, true)
								  : SaveTextSHP0 (&shp, dest_fname, true);
				ResetSHP0 (&shp);
			}
			return true;

		case FF_CLR:
		case FF_CLR_TXT:
			FreeContainerData (cdata);
			{
				clr0_t clr;
				InitializeCLR0 (&clr);
				*err = fform == FF_CLR_TXT ? ScanTextCLR0 (&clr, false, src_fname)
										   : ScanRawCLR0 (&clr, false, data, data_size);
				if (*err)
				{
					ResetCLR0 (&clr);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawCLR0 (&clr, dest_fname, true)
								  : SaveTextCLR0 (&clr, dest_fname, true);
				ResetCLR0 (&clr);
			}
			return true;

		case FF_SOMA_OBP:
			FreeContainerData (cdata);
			if (!binary_dest)
			{
				File_t F;
				*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
				if (!*err && F.f)
					*err = DecodeSomaObp_Text (F.f, data, data_size, data_size);
				ResetFile (&F, opt_preserve);
				return true;
			}
			return false;

		case FF_SOMA_BGP:
			FreeContainerData (cdata);
			if (!binary_dest)
			{
				File_t F;
				*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
				if (!*err && F.f)
					*err = DecodeSomaBgp_Text (F.f, data, data_size, data_size);
				ResetFile (&F, opt_preserve);
				return true;
			}
			return false;

		case FF_SOMA_PCS:
			FreeContainerData (cdata);
			if (!binary_dest)
			{
				File_t F;
				*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
				if (!*err && F.f)
					*err = DecodeSomaPcs_Text (F.f, data, data_size);
				ResetFile (&F, opt_preserve);
				return true;
			}
			return false;

		case FF_BFLYT:
		case FF_BCLYT:
		case FF_BRLYT:
		case FF_BRLAN:
		case FF_BFLYT_TXT:
		case FF_BCLYT_TXT:
		case FF_SHDVAR:
			FreeContainerData (cdata);
			{
				bflyt_t bflyt;
				InitializeBFLYT (&bflyt);
				*err = ScanBFLYT (&bflyt, false, data, data_size);
				if (!*err)
					*err = testmode	  ? ERR_OK
						: binary_dest ? SaveRawBFLYT (&bflyt, dest_fname, true)
									  : SaveXMLBFLYT (&bflyt, dest_fname, true);
				ResetBFLYT (&bflyt);
			}
			return true;

		default:
			break;
	}

	*err = ERR_WRONG_FILE_TYPE;
	return false;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command binary/text		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_convert (bool binary) // cmd_binary() cmd_text()
{
	static ccp def_path = "\1P/\1N\1?T";
	CheckOptDest (def_path, false);
	char dest[PATH_MAX];
	enumError max_err = ERR_OK;

	raw_data_t raw;
	InitializeRawData (&raw);

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		enumError err = LoadRawData (&raw, false, arg, 0, opt_ignore > 0, 0);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
		{
			ResetRawData (&raw);
			return err;
		}

		const bool is_bflyt = raw.fform == FF_BFLYT || raw.fform == FF_BCLYT
			|| raw.fform == FF_BRLYT || raw.fform == FF_BRLAN || raw.fform == FF_SHDVAR;
		file_format_t ff_dest = binary ? GetBinFF (raw.fform) : GetTextFF (raw.fform);
		// Layout's former text type is the legacy .tflyt syntax.  Route normal
		// TEXT conversion through the XML path instead.
		if (!binary && is_bflyt)
			ff_dest = FF_UNKNOWN;

		// Handle layout formats (BFLYT / BCLYT / BRLYT / BRLAN / SHDVAR)
		if (!ff_dest
			&& (raw.fform == FF_BFLYT || raw.fform == FF_BCLYT || raw.fform == FF_BRLYT
				|| raw.fform == FF_BRLAN || raw.fform == FF_SHDVAR))
		{
			if (binary)
			{
				ccp def_ext = raw.fform == FF_SHDVAR ? "\\1P/\\1N.svt" : "\\1P/\\1N.bflyt";
				ccp ext = raw.fform == FF_SHDVAR ? ".svt" : ".bflyt";
				SubstDest (dest, sizeof (dest), arg, opt_dest, def_ext, ext, false);
			}
			else
				SubstDest (dest, sizeof (dest), arg, opt_dest, "\\1P/\\1N.xml", ".xml", false);
			if (verbose >= 0)
			{
				fprintf (stdlog, "%sCREATE/TEXT %s:%s => %s\n", verbose > 0 ? "\n" : "",
					GetNameFF (raw.fform, 0), arg, dest);
				fflush (stdlog);
			}
			if (!testmode)
			{
				bflyt_t bflyt;
				InitializeBFLYT (&bflyt);
				err = ScanBFLYT (&bflyt, false, raw.data, raw.data_size);
				if (!err)
					err = binary ? SaveRawBFLYT (&bflyt, dest, true)
								 : SaveXMLBFLYT (&bflyt, dest, true);
				ResetBFLYT (&bflyt);
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (!ff_dest && raw.data_size >= 16
			&& (!memcmp (raw.data, "BY", 2) || !memcmp (raw.data, "YB", 2)))
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, "\\1P/\\1N.yaml", ".yaml", false);
			if (verbose >= 0)
			{
				fprintf (
					stdlog, "%sCREATE/TEXT BYML:%s => %s\n", verbose > 0 ? "\n" : "", arg, dest);
				fflush (stdlog);
			}
			if (!testmode)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (!F.f)
					err = ERR_WRITE_FAILED;
				else
				{
					ccp ext = strrchr (dest, '.');
					if (ext && !strcasecmp (ext, ".xml"))
						err = DecodeBYML_XML (F.f, raw.data, raw.data_size);
					else if (ext && !strcasecmp (ext, ".json"))
						err = DecodeBYML_JSON (F.f, raw.data, raw.data_size);
					else
						err = DecodeBYML_YAML (F.f, raw.data, raw.data_size);
					ResetFile (&F, opt_preserve);
				}
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (!ff_dest && (raw.fform == FF_XB || IsXB (raw.data, raw.data_size)))
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, "\\1P/\\1N.xml", ".xml", false);
			if (verbose >= 0)
			{
				fprintf (stdlog, "%sCREATE/TEXT XB:%s => %s\n", verbose > 0 ? "\n" : "", arg, dest);
				fflush (stdlog);
			}
			if (!testmode)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (!F.f)
					err = ERR_WRITE_FAILED;
				else
				{
					err = DecodeXB (F.f, raw.data, raw.data_size);
					ResetFile (&F, opt_preserve);
				}
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (!ff_dest && (raw.fform == FF_MPMESS || IsMPMESS (raw.data, raw.data_size)))
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, "\\1P/\\1N.txt", ".txt", false);
			if (verbose >= 0)
			{
				fprintf (
					stdlog, "%sCREATE/TEXT MPMESS:%s => %s\n", verbose > 0 ? "\n" : "", arg, dest);
				fflush (stdlog);
			}
			if (!testmode)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (!F.f)
					err = ERR_WRITE_FAILED;
				else
				{
					err = DecodeMPMESS_Text (F.f, raw.data, raw.data_size);
					ResetFile (&F, opt_preserve);
				}
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (!ff_dest
			&& (raw.fform == FF_MPBOARD || IsMPBoard (raw.data, raw.data_size)
				|| IsSMPBoard (raw.data, raw.data_size) || IsMP10Board (raw.data, raw.data_size)))
		{
			ccp out_ext = (opt_dest && strcasestr (opt_dest, ".csv")) ? ".csv" : ".txt";
			char def_pat[32];
			snprintf (def_pat, sizeof (def_pat), "\\1P/\\1N%s", out_ext);
			SubstDest (dest, sizeof (dest), arg, opt_dest, def_pat, out_ext, false);
			if (verbose >= 0)
			{
				fprintf (
					stdlog, "%sCREATE/TEXT MPBOARD:%s => %s\n", verbose > 0 ? "\n" : "", arg, dest);
				fflush (stdlog);
			}
			if (!testmode)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (!F.f)
					err = ERR_WRITE_FAILED;
				else if (IsSMPBoard (raw.data, raw.data_size))
				{
					smp_board_t smp;
					err = ScanSMPBoard (&smp, raw.data, raw.data_size);
					if (!err)
					{
						if (!strcmp (out_ext, ".csv"))
							err = DecodeSMPBoard_CSV (F.f, &smp);
						else
							err = DecodeSMPBoard_Text (F.f, raw.data, raw.data_size);
					}
					ResetSMPBoard (&smp);
					ResetFile (&F, opt_preserve);
				}
				else if (IsMP10Board (raw.data, raw.data_size))
				{
					mp10_board_t mp10;
					err = ScanMP10Board (&mp10, raw.data, raw.data_size);
					if (!err)
					{
						if (!strcmp (out_ext, ".csv"))
							err = DecodeMP10Board_CSV (F.f, &mp10);
						else
							err = DecodeMP10Board_Text (F.f, &mp10);
					}
					ResetMP10Board (&mp10);
					ResetFile (&F, opt_preserve);
				}
				else
				{
					mp_board_t board;
					err = ScanMPBoard (&board, raw.data, raw.data_size, 0);
					if (!err)
					{
						if (!strcmp (out_ext, ".csv"))
							err = DecodeMPBoard_CSV (F.f, &board);
						else
							err = DecodeMPBoard_Text (F.f, &board);
					}
					ResetMPBoard (&board);
					ResetFile (&F, opt_preserve);
				}
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (!ff_dest && (raw.fform == FF_BGLPBD || IsBGLPBD (raw.data, raw.data_size)))
		{
			ccp d_ext = opt_dest ? strrchr (opt_dest, '.') : NULL;
			if (d_ext && !strcasecmp (d_ext, ".png"))
			{
				bglpbd_file_t probe_file;
				err = ScanBGLPBD (&probe_file, raw.data, raw.data_size);
				if (!err)
				{
					char prefix[PATH_MAX];
					snprintf (prefix, sizeof (prefix), "%.*s", (int)(d_ext - opt_dest), opt_dest);
					err = DumpBGLPBD_Images (&probe_file, prefix, opt_overwrite > 0);
				}
				ResetBGLPBD (&probe_file);
			}
			else
			{
				SubstDest (dest, sizeof (dest), arg, opt_dest, "\1P/\1N.yml", ".yml", false);
				if (verbose >= 0)
				{
					fprintf (stdlog, "%sCREATE/TEXT BGLPBD:%s => %s\n", verbose > 0 ? "\n" : "",
						arg, dest);
					fflush (stdlog);
				}
				if (!testmode)
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest, false, arg);
					if (!F.f)
						err = ERR_WRITE_FAILED;
					else
					{
						ccp ext = strrchr (dest, '.');
						if (ext && !strcasecmp (ext, ".json"))
							err = DecodeAAMP_JSON (F.f, raw.data, raw.data_size);
						else
							err = DecodeAAMP_YAML (F.f, raw.data, raw.data_size);
						ResetFile (&F, opt_preserve);
					}
				}
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (!ff_dest && raw.data_size >= 12 && !memcmp (raw.data, "AAMP", 4))
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, "\\1P/\\1N.yml", ".yml", false);
			if (verbose >= 0)
			{
				fprintf (
					stdlog, "%sCREATE/TEXT AAMP:%s => %s\n", verbose > 0 ? "\n" : "", arg, dest);
				fflush (stdlog);
			}
			if (!testmode)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (!F.f)
					err = ERR_WRITE_FAILED;
				else
				{
					ccp ext = strrchr (dest, '.');
					if (ext && !strcasecmp (ext, ".json"))
						err = DecodeAAMP_JSON (F.f, raw.data, raw.data_size);
					else
						err = DecodeAAMP_YAML (F.f, raw.data, raw.data_size);
					ResetFile (&F, opt_preserve);
				}
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (binary && !ff_dest
			&& (is_ext (arg, ".yml") || is_ext (arg, ".yaml") || is_ext (arg, ".json")))
		{
			char b_dest[PATH_MAX];
			SubstDest (b_dest, sizeof (b_dest), arg, opt_dest, "\\1P/\\1N.aamp", ".aamp", false);
			ccp b_ext = strrchr (b_dest, '.');
			if (b_ext
				&& (!strcasecmp (b_ext, ".aamp") || !strcasecmp (b_ext, ".baamp")
					|| !strcasecmp (b_ext, ".bparam") || !strcasecmp (b_ext, ".bgenv")
					|| !strcasecmp (b_ext, ".bglpbd")))
			{
				if (verbose >= 0)
				{
					fprintf (stdlog, "%sCREATE/BINARY AAMP:%s => %s\n", verbose > 0 ? "\n" : "",
						arg, b_dest);
					fflush (stdlog);
				}
				if (!testmode)
					err = encode_aamp_file (arg, b_dest);
				if (err && max_err < err)
					max_err = err;
				continue;
			}
		}

		if (!ff_dest)
		{
			ERROR0 (
				ERR_WARNING, "File format %s not supported: %s\n", GetNameFF (raw.fform, 0), arg);
			if (max_err < ERR_WARNING)
				max_err = ERR_WARNING;
			continue;
		}

		SubstDest (dest, sizeof (dest), arg, opt_dest, def_path, GetExtFF (0, ff_dest), false);

		if (verbose >= 0)
		{
			fprintf (stdlog, "%sCREATE/%s %s:%s => %s:%s\n", verbose > 0 ? "\n" : "",
				binary ? "BINARY" : "TEXT", GetNameFF (raw.fform, 0), arg, GetNameFF (ff_dest, 0),
				dest);
			fflush (stdlog);
		}

		SetupContainerRawData (&raw);
		ConvertHelper (
			raw.data, raw.data_size, LinkContainerData (&raw.container), arg, dest, binary, &err);
		if (err)
		{
			ERROR0 (err, 0);
			if (max_err < err)
				max_err = err;
		}
	}

	ResetStringField (&plist);
	ResetRawData (&raw);
	return max_err;
}
