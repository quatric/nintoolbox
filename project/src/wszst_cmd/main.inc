
//
///////////////////////////////////////////////////////////////////////////////
///////////////                   check options                 ///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError CheckOptions (int argc, char **argv, bool is_env)
{
	TRACE ("CheckOptions(%d,%p,%d) optind=%d\n", argc, argv, is_env, optind);

	optind = 0;
	int err = 0;
	ccp filter_bmg = 0;

	for (;;)
	{
		const int opt_stat = getopt_long (argc, argv, OptionShort, OptionLong, 0);
		if (opt_stat == -1)
			break;

		RegisterOptionByName (&InfoUI_wszst, opt_stat, 1, is_env);

		switch ((enumGetOpt)opt_stat)
		{
			case GO__ERR:
				err++;
				break;

			case GO_VERSION:
				version_exit ();
			case GO_HELP:
				help_exit (false);
			case GO_XHELP:
				help_exit (true);
			case GO_CONFIG:
				opt_config = optarg;
				break;
			case GO_YDEBUG:
				enable_ydebug++;
				break;
			case GO_ALLOW_ALL:
				allow_all = true;
				break;
			case GO_COMPATIBLE:
				err += ScanOptCompatible (optarg);
				break;
			case GO_WIDTH:
				err += ScanOptWidth (optarg);
				break;
			case GO_MAX_WIDTH:
				err += ScanOptMaxWidth (optarg);
				break;
			case GO_NO_PAGER:
				opt_no_pager = true;
				break;
			case GO_ZERO:
				opt_zero++;
				break;
			case GO_QUIET:
				verbose = verbose > -1 ? -1 : verbose - 1;
				break;
			case GO_VERBOSE:
				verbose = verbose < 0 ? 0 : verbose + 1;
				break;
			case GO_LOGGING:
				logging++;
				break;
			case GO_EXT_ERRORS:
				ext_errors++;
				break;
			case GO_TIMING:
				log_timing++;
				break;
			case GO_WARN:
				err += ScanOptWarn (optarg);
				break;
			case GO_DE:
				use_de = true;
				break;
			case GO_CT_CODE:
				ctcode_enabled = true;
				break;
			case GO_LE_CODE:
				lecode_enabled = true;
				break; // optional argument ignored
			case GO_LE_04X:
				lecode_04x = true;
				break;
			case GO_COLORS:
				err += ScanOptColorize (0, optarg, 0);
				break;
			case GO_NO_COLORS:
				opt_colorize = COLMD_OFF;
				break;

			case GO_CHDIR:
				err += ScanOptChdir (optarg);
				break;
			case GO_CONST:
				err += ScanOptConst (optarg);
				break;
			case GO_TOUCH:
				break;
			case GO_AUTO:
				break;
			case GO_LOAD_PREFIX:
				err += ScanOptLoadPrefix (optarg);
				break;
			case GO_LOAD_CATEGORY:
				err += ScanOptLoadCategory (optarg);
				break;
			case GO_PLUS:
				opt_plus = optarg;
				break;
			case GO_SPLIT:
				opt_split = str2l (optarg, 0, 10);
				break;
			case GO_PRINTF:
				opt_printf = optarg;
				break;
			case GO_SET_FLAGS:
				err += ScanOptSetFlags (optarg);
				break;
			case GO_SET_SCALE:
				err += ScanOptSetScale (optarg);
				break;
			case GO_SET_ROT:
				err += ScanOptSetRot (optarg);
				break;
			case GO_SET_X:
				err += ScanOptSet (0, optarg);
				break;
			case GO_SET_Y:
				err += ScanOptSet (1, optarg);
				break;
			case GO_SET_Z:
				err += ScanOptSet (2, optarg);
				break;
			case GO_XCENTER:
				break;
			case GO_YCENTER:
				break;
			case GO_ZCENTER:
				break;
			case GO_CENTER:
				break;
			case GO_SCALE:
				err += ScanOptScale (optarg);
				break;
			case GO_SHIFT:
				err += ScanOptShift (optarg);
				break;
			case GO_XSS:
				err += ScanOptXSS (0, optarg);
				break;
			case GO_YSS:
				err += ScanOptXSS (1, optarg);
				break;
			case GO_ZSS:
				err += ScanOptXSS (2, optarg);
				break;
			case GO_ROT:
				err += ScanOptRotate (optarg);
				break;
			case GO_XROT:
				err += ScanOptXRotate (0, optarg);
				break;
			case GO_YROT:
				err += ScanOptXRotate (1, optarg);
				break;
			case GO_ZROT:
				err += ScanOptXRotate (2, optarg);
				break;
			case GO_YPOS:
				err += ScanOptYPos (optarg);
				break;
			case GO_TRANSLATE:
				err += ScanOptTranslate (optarg);
				break;
			case GO_NULL:
				force_transform |= 1;
				break;
			case GO_NEXT:
				err += NextTransformation (false);
				break;
			case GO_ASCALE:
				err += ScanOptAScale (optarg);
				break;
			case GO_AROT:
				err += ScanOptARotate (optarg);
				break;
			case GO_TFORM_SCRIPT:
				err += ScanOptTformScript (optarg);
				break;
			case GO_RM_GOBJ:
				err += ScanOptRmGobj (optarg);
				break;
			case GO_BATTLE:
				err += ScanOptBattle (optarg);
				break;
			case GO_EXPORT_FLAGS:
				err += ScanOptExportFlags (optarg);
				break;
			case GO_ROUTE_OPTIONS:
				err += ScanOptRouteOptions (optarg);
				break;
			case GO_WIM0:
				err += ScanOptWim0 (optarg);
				break;
			case GO_SLOT:
				err += ScanOptSlot (optarg);
				break;
			case GO_LOAD_KCL:
				err += ScanOptLoadKcl (optarg);
				break;
			case GO_KCL:
				err += ScanOptKcl (optarg);
				break;
			case GO_KCL_FLAG:
				err += ScanOptKclFlag (optarg);
				break;
			case GO_KCL_SCRIPT:
				err += ScanOptKclScript (optarg);
				break;
			case GO_TRI_AREA:
				err += ScanOptTriArea (optarg);
				break;
			case GO_TRI_HEIGHT:
				err += ScanOptTriHeight (optarg);
				break;
			case GO_FLAG_FILE:
				opt_flag_file = optarg;
				break;
			case GO_XTRIDATA:
				opt_xtridata = optarg ? str2ul (optarg, 0, 10) : 1;
				break;
			case GO_KMP:
				err += ScanOptKmp (optarg);
				break;
			case GO_N_LAPS:
				err += ScanOptNLaps (optarg);
				break;
			case GO_SPEED_MOD:
				err += ScanOptSpeedMod (optarg);
				break;
			case GO_KTPT2:
				err += ScanOptKtpt2 (optarg);
				break;
			case GO_TFORM_KMP:
				err += ScanOptTformKmp (optarg);
				break;
			case GO_GAMEMODES:
				err += ScanOptGamemodes (optarg);
				break;
			case GO_REPAIR_XPF:
				err += ScanOptRepairXPF (optarg);
				break;

			case GO_MDL:
				err += ScanOptMdl (optarg);
				break;
			case GO_MINIMAP:
				break;
			case GO_PAT:
				err += ScanOptPat (optarg);
				break;
			case GO_PATCH_FILES:
				err += ScanOptPatchFiles (optarg);
				break;

			case GO_KMG_LIMIT:
				err += ScanOptKmgLimit (optarg);
				break;
			case GO_KMG_COPY:
				err += ScanOptKmgCopy (optarg);
				break;

			case GO_LT_CLEAR:
				opt_lt_clear = true;
				break;
			case GO_LT_ONLINE:
				err += ScanOptLtOnline (optarg);
				break;
			case GO_LT_N_PLAYERS:
				err += ScanOptLtNPlayers (optarg);
				break;
			case GO_LT_COND_BIT:
				err += ScanOptLtCondBit (optarg);
				break;
			case GO_LT_GAME_MODE:
				err += ScanOptLtGameMode (optarg);
				break;
			case GO_LT_ENGINE:
				err += ScanOptLtEngine (optarg);
				break;
			case GO_LT_RANDOM:
				err += ScanOptLtRandom (optarg);
				break;
			case GO_LEX_FEATURES:
				opt_lex_features = true;
				break;
			case GO_LEX_RM_FEAT:
				opt_lex_rm_features = true;
				break;
			case GO_LEX_PURGE:
				opt_lex_purge = true;
				break;

			case GO_UTF_8:
				use_utf8 = true;
				break;
			case GO_NO_UTF_8:
				use_utf8 = false;
				break;

			case GO_TEST:
				testmode++;
				break;
			case GO_FORCE:
				force_count++;
				break;
			case GO_REPAIR_MAGICS:
				err += ScanOptRepairMagic (optarg);
				break;
			case GO_TINY:
				err += ScanOptTiny (optarg);
				break;

			case GO_ANALYZE:
				analyze_fname = optarg;
				break;
			case GO_ANALYZE_MODE:
				err += ScanOptAnalyzeMode (optarg);
				break;

#if OPT_OLD_NEW
			case GO_OLD:
				opt_new = opt_new > 0 ? -1 : opt_new - 1;
				break;
			case GO_STD:
				opt_new = 0;
				break;
			case GO_NEW:
				opt_new = opt_new < 0 ? +1 : opt_new + 1;
				break;
#endif
			case GO_EXTRACT:
				opt_extract = optarg;
				break;

			case GO_ESC:
				err += ScanEscapeChar (optarg) < 0;
				break;
			case GO_SOURCE:
				SetSource (optarg);
				break;
			case GO_ID_LIST:
				SetIdList (optarg);
				break;
			case GO_REFERENCE:
				SetReference (optarg);
				break;
			case GO_DEST:
				SetDest (optarg, false);
				break;
			case GO_DEST2:
				SetDest (optarg, true);
				break;
			case GO_PARENT:
				opt_parent = optarg;
				break;
			case GO_OVERWRITE:
				opt_overwrite = true;
				break;
			case GO_NUMBER:
				opt_number = true;
				break;
			case GO_REMOVE_SRC:
				opt_remove_src = true;
				break;
			case GO_REMOVE_DEST:
				opt_remove_dest = true;
				break;
			case GO_UPDATE:
				opt_update = true;
				break;
			case GO_PRESERVE:
				opt_preserve = true;
				break;
			case GO_IGNORE:
				opt_ignore++;
				break;
			case GO_IGNORE_SETUP:
				opt_ignore_setup = true;
				break;
			case GO_PURGE:
				opt_purge = true;
				break;

			case GO_YAZ0:
				SetCompressionFF (FF_INVALID, FF_YAZ0);
				break;
			case GO_YAZ1:
				SetCompressionFF (FF_INVALID, FF_YAZ1);
				break;
			case GO_XYZ:
				SetCompressionFF (FF_INVALID, FF_XYZ);
				break;
			case GO_BZ:
				SetCompressionFF (FF_INVALID, FF_BZ);
				break;
			case GO_BZIP2:
				SetCompressionFF (FF_INVALID, FF_BZIP2);
				break;
			case GO_CYBZ:
				SetCompressionFF (FF_INVALID, FF_YBZ);
				break;
			case GO_LZ:
				SetCompressionFF (FF_INVALID, FF_LZ);
				break;
			case GO_LZMA:
				SetCompressionFF (FF_INVALID, FF_LZMA);
				break;
			case GO_ZSTD:
				SetCompressionFF (FF_INVALID, FF_ZSTD);
				break;
			case GO_CYLZ:
				SetCompressionFF (FF_INVALID, FF_YLZ);
				break;

			case GO_U8:
				SetCompressionFF (FF_U8, FF_INVALID);
				break;
			case GO_SZS:
				SetCompressionFF (FF_U8, FF_YAZ0);
				break;
			case GO_WU8:
				SetCompressionFF (FF_WU8, FF_INVALID);
				break;
			case GO_XWU8:
				SetCompressionFF (FF_WU8, FF_XYZ);
				break;
			case GO_WBZ:
				SetCompressionFF (FF_WU8, FF_BZ);
				break;
			case GO_WLZ:
				SetCompressionFF (FF_WU8, FF_LZ);
				break;
			case GO_YBZ:
				SetCompressionFF (FF_U8, FF_YBZ);
				break;
			case GO_YLZ:
				SetCompressionFF (FF_U8, FF_YLZ);
				break;
			// case GO_LTA:		SetCompressionFF(FF_LTA,FF_INVALID); break;
			case GO_LFL:
				SetCompressionFF (FF_LFL, FF_INVALID);
				break;
			// case GO_ARC:		SetCompressionFF(FF_RARC,FF_INVALID); break;
			case GO_BRRES:
				SetCompressionFF (FF_BRRES, FF_INVALID);
				break;
			case GO_BREFF:
				SetCompressionFF (FF_BREFF, FF_INVALID);
				break;
			case GO_BREFT:
				SetCompressionFF (FF_BREFT, FF_INVALID);
				break;
			case GO_PACK:
				SetCompressionFF (FF_PACK, FF_INVALID);
				break;

			case GO_JSON:
				script_fform = FF_JSON;
				break;
			case GO_SH:
				script_fform = FF_SH;
				break;
			case GO_BASH:
				script_fform = FF_BASH;
				break;
			case GO_PHP:
				script_fform = FF_PHP;
				break;
			case GO_MAKEDOC:
				script_fform = FF_MAKEDOC;
				break;
			case GO_VAR:
				script_varname = optarg;
				break;
			case GO_ARRAY:
				script_array++;
				break;
			case GO_AVAR:
				script_array++;
				script_varname = optarg;
				break;
			case GO_CASE:
				err += ScanOptCase (optarg);
				break;
			case GO_FMODES:
				err += ScanOptFModes (optarg);
				break;
			case GO_INSTALL:
				opt_install++;
				break;

			case GO_PT_DIR:
				err += ScanOptPtDir (optarg);
				break;
			case GO_LINKS:
				opt_links = true;
				break;
			case GO_RM_AIPARAM:
				opt_rm_aiparam = true;
				break;
			case GO_ALIGN_U8:
				err += ScanOptAlignU8 (optarg);
				break;
			case GO_ALIGN_LTA:
				err += ScanOptAlignLTA (optarg);
				break;
			case GO_ALIGN_PACK:
				err += ScanOptAlignPACK (optarg);
				break;
			case GO_ALIGN_BRRES:
				err += ScanOptAlignBRRES (optarg);
				break;
			case GO_ALIGN_BREFF:
				err += ScanOptAlignBREFF (optarg);
				break;
			case GO_ALIGN_BREFT:
				err += ScanOptAlignBREFT (optarg);
				break;
			case GO_ALIGN:
				err += ScanOptAlign (optarg);
				break;
			case GO_TRANSFORM:
				err += ScanOptTransform (optarg);
				break;
			case GO_STRIP:
				opt_strip = true;
				break;

			case GO_NO_COMPRESS:
				opt_compr_mode = -1;
				break;
			case GO_COMPRESS:
				err += ScanOptCompr (optarg);
				break;
			case GO_FAST:
				opt_fast = true;
				err += ScanOptCompr ("fast");
				break;
			case GO_NORM:
				opt_norm = true;
				break;
			case GO_NO_COPY:
				opt_no_copy = true;
				break;
			case GO_MAX_FILE_SIZE:
				err += ScanOptMaxFileSize (optarg);
				break;
			case GO_TRACKS:
				err += ScanOptTracks (optarg);
				break;
			case GO_ARENAS:
				err += ScanOptArenas (optarg);
				break;
			case GO_LOAD_BMG:
				err += ScanOptLoadBMG (optarg);
				break;
			case GO_PATCH_BMG:
				err += ScanOptPatchMessage (optarg);
				break;
			case GO_MACRO_BMG:
				err += ScanOptMacroBMG (optarg);
				break;
			case GO_FILTER_BMG:
				filter_bmg = optarg;
				break;
			case GO_LE_MENU:
				opt_le_menu = true;
				break;
			case GO_9LAPS:
				opt_9laps = true;
				break;
			case GO_UI_SOURCE:
				opt_ui_source = optarg;
				break;
			case GO_CUP_ICONS:
				opt_cup_icons = optarg;
				break;
			case GO_TITLE_SCREEN:
				opt_title_screen = optarg;
				break;
			case GO_AUTOADD_PATH:
				DefineAutoAddPath (optarg);
				break;

			case GO_NO_PASSTHROUGH:
				opt_no_passthrough = true;
				break;
			case GO_WITH_WIT:
				opt_with_wit = optarg;
				break;
			case GO_WITH_NDSTOOL:
				opt_with_ndstool = optarg;
				break;
			case GO_WITH_CTRTOOL:
				opt_with_ctrtool = optarg;
				break;
			case GO_WITH_SHARPII:
				opt_with_sharpii = optarg;
				break;
			case GO_WITH_HACTOOL:
				opt_with_hactool = optarg;
				break;
			case GO_WITH_HACBREWPACK:
				opt_with_hacbrewpack = optarg;
				break;
			case GO_WITH_BMS:
				opt_with_bms = optarg;
				break;
			case GO_WITH_7Z:
				opt_with_7z = optarg;
				break;

			case GO_ENCODE_ALL:
				opt_encode_all = true;
				break;
			case GO_ENCODE_IMG:
				opt_encode_img = true;
				break;
			case GO_NO_ENCODE:
				opt_no_encode = true;
				break;
			case GO_NO_RECURSE:
				opt_no_recurse = true;
				break;
			case GO_AUTO_ADD:
				opt_auto_add = true;
				break;
			case GO_NO_ECHO:
				opt_no_echo = true;
				break;
			case GO_NO_CHECK:
				opt_no_check = true;
				break;
			case GO_BASEDIR:
				opt_basedir = optarg;
				break;
			case GO_RECURSE:
				err += ScanOptRecurse (optarg);
				break;
			case GO_EXT:
				opt_ext++;
				break;
			case GO_DECODE:
				opt_decode = true;
				break;
			case GO_WITH_UPDATE_PART:
				opt_with_update_part = true;
				break;
			case GO_EXPORT_MIIS:
				opt_export_miis = true;
				break;
			case GO_EXPORT_RAW:
				opt_export_raw = true;
				break;
			case GO_CMPR_DEFAULT:
				err += ScanOptCmprDefault (optarg);
				break;
			case GO_N_MIPMAPS:
				err += ScanOptNMipmaps (optarg);
				break;
			case GO_MAX_MIPMAPS:
				err += ScanOptMaxMipmaps (optarg);
				break;
			case GO_MIPMAP_SIZE:
				err += ScanOptMipmapSize (optarg);
				break;
			case GO_MIPMAPS:
				opt_mipmaps = +1;
				break;
			case GO_NO_MIPMAPS:
				opt_mipmaps = -1;
				break;
			case GO_FAST_MIPMAPS:
				fast_resize_enabled = true;
				break;
			case GO_CUT:
				opt_cut = true;
				break;
			case GO_RAW:
				opt_raw = true;
				break;

			case GO_ROUND:
				opt_round = true;
				break;
			case GO_LONG:
				long_count++;
				break;
				//	case GO_FULL:		full_count++; break;
			case GO_EXPORT:
				export_count++;
				break;
			case GO_SORT:
				err += ScanOptSort (optarg);
				break;
			case GO_NO_HEADER:
				print_header = false;
				break;
			case GO_BRIEF:
				brief_count++;
				break;
			case GO_NO_WILDCARDS:
				no_wildcards_count++;
				break;
			case GO_IN_ORDER:
				inorder_count++;
				break;
			case GO_PIPE:
				pipe_count++;
				break;
			case GO_DELTA:
				delta_count++;
				break;
			case GO_DIFF:
				diff_count++;
				break;
			case GO_NO_PARAM:
				print_param = false;
				break;
			case GO_EPSILON:
				err += ScanOptEpsilon (optarg);
				break;

			case GO_BMG_ENDIAN:
				err += ScanOptBmgEndian (optarg);
				break;
			case GO_BMG_ENCODING:
				err += ScanOptBmgEncoding (optarg);
				break;
			case GO_BMG_INF_SIZE:
				err += ScanOptBmgInfSize (optarg, false);
				break;
			case GO_BMG_MID:
				err += ScanOptBmgMid (optarg);
				break;
			case GO_FORCE_ATTRIB:
				err += ScanOptForceAttrib (optarg);
				break;
			case GO_DEF_ATTRIB:
				err += ScanOptDefAttrib (optarg);
				break;
			case GO_NO_ATTRIB:
				opt_bmg_no_attrib = true;
				break;
			case GO_X_ESCAPES:
				opt_bmg_x_escapes = true;
				break;
			case GO_OLD_ESCAPES:
				opt_bmg_old_escapes = true;
				break;
			case GO_SINGLE_LINE:
				opt_bmg_single_line++;
				break;
			case GO_NO_BMG_COLORS:
				opt_bmg_colors = 0;
				break;
			case GO_BMG_COLORS:
				opt_bmg_colors = 2;
				break;
			case GO_NO_BMG_INLINE:
				opt_bmg_inline_attrib = false;
				break;
			case GO_CACHE:
				opt_cache = optarg;
				opt_remove_dest = true;
				break;
			case GO_CNAME:
				opt_cname = optarg;
				break;
			case GO_LOG_CACHE:
				opt_log_cache = optarg;
				break;
			case GO_PARALLEL:
				parallel_count++;
				break;
			case GO_ID:
				opt_id = true;
				break;
			case GO_BASE64:
				opt_base64 = true;
				err += ScanOptCoding64 (optarg);
				break;
			case GO_DB64:
				opt_db64 = true;
				err += ScanOptCoding64 (optarg);
				break;
			case GO_CODING:
				err += ScanOptCoding64 (optarg);
				break;
			case GO_VERIFY:
				opt_verify = true;
				break;
			case GO_SECTIONS:
				print_sections++;
				break;
			case GO_ALL:
				set_all (0);
				break;

				// no default case defined
				//	=> compiler checks the existence of all enum values
		}
	}

	if (opt_compr_mode == -9)
	{
		// [[?]]
		if (opt_compr)
			opt_colorize = 1;
		list_compressions_exit ();
	}

#ifdef DEBUG
	DumpUsedOptions (&InfoUI_wszst, TRACE_FILE, 11);
#endif
	CloseTransformation ();
	NormalizeOptions (verbose > 3 && !is_env);
	SetupBMG (filter_bmg);
	SetupKCL ();
	SetupKMP ();
	SetupMDL ();

	// [[cache]] p2=true until final version of 'post-patch-mkw.sh'
	SetupSZSCache (opt_cache, true);

	if (!err)
	{
		enumError err = SetupPatchingListBMG ();
		if (err > ERR_WARNING)
			return err;
	}

	return !err ? ERR_OK : ProgInfo.max_error ? ProgInfo.max_error : ERR_SYNTAX;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                   check command                 ///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError CheckCommand (int argc, char **argv)
{
	const KeywordTab_t *cmd_ct = CheckCommandHelper (argc, argv, CommandTab);
	if (!cmd_ct)
		hint_exit (ERR_SYNTAX);

	TRACE ("COMMAND FOUND: #%lld = %s\n", (u64)cmd_ct->id, cmd_ct->name1);
	current_command = cmd_ct;

	if (!allow_all)
	{
		enumError err = VerifySpecificOptions (&InfoUI_wszst, cmd_ct);
		if (err)
			hint_exit (err);
	}
	WarnDepractedOptions (&InfoUI_wszst);

	if (cmd_ct->id != CMD_ARGTEST)
	{
		argc -= optind + 1;
		argv += optind + 1;

		if (cmd_ct->id == CMD_TEST)
			while (argc-- > 0)
				AddParam (*argv++);
		else
			while (argc-- > 0)
				AtFileHelper (*argv++, AddParam);
	}

	enumError err = ERR_OK;
	switch ((enumCommands)cmd_ct->id)
	{
		case CMD_VERSION:
			version_exit ();
		case CMD_HELP:
			PrintHelpColor (&InfoUI_wszst);
			break;
		case CMD_CONFIG:
			err = cmd_config ();
			break;
		case CMD_INSTALL:
			err = cmd_install ();
			break;
		case CMD_ARGTEST:
			err = cmd_argtest (argc, argv);
			break;
		case CMD_EXPAND:
			err = cmd_expand (argc, argv);
			break;
		case CMD_WILDCARDS:
			err = cmd_wildcards (argc, argv);
			break;
		case CMD_TEST:
			err = cmd_test ();
			break;
		case CMD_COLORS:
			err = Command_COLORS (brief_count ? -brief_count : long_count, 0, 0);
			break;
		case CMD_ERROR:
			err = cmd_error ();
			break;
		case CMD_FILETYPE:
			err = cmd_filetype ();
			break;
		case CMD_UI_CHECK:
			err = cmd_ui_check ();
			break;
		case CMD_FILEATTRIB:
			err = cmd_fileattrib ();
			break;
		case CMD_BRSUB:
			err = cmd_brsub ();
			break;

		case CMD_SYMBOLS:
			err = DumpSymbols (SetupParserVars ());
			break;
		case CMD_FUNCTIONS:
			SetupReferenceKCL (0);
			err = ListParserFunctions ();
			break;
		case CMD_CALCULATE:
			err = ParserCalc (SetupParserVars ());
			break;
		case CMD_MATRIX:
			err = cmd_matrix ();
			break;
		case CMD_FLOAT:
			err = cmd_float ();
			break;
		case CMD_VR_CALC:
			err = cmd_vr_calc ();
			break;
		case CMD_VR_RACE:
			err = cmd_vr_race ();
			break;
		case CMD_AUTOADD:
			err = cmd_autoadd ();
			break;
		case CMD_TRACKS:
			err = cmd_tracks ();
			break;
		case CMD_SCANCACHE:
			err = cmd_scancache ();
			break;
		case CMD_EXPORT:
			err = cmd_export ();
			break;
		case CMD_SIZEOF:
			err = cmd_sizeof ();
			break;
		case CMD_CODE:
			err = cmd_code ();
			break;
		case CMD_RECODE:
			err = cmd_recode ();
			break;
		case CMD_SUBFILE:
			err = cmd_subfile ();
			break;
		case CMD_TESTNORM:
			err = cmd_testnorm ();
			break;

		case CMD_LIST:
			err = cmd_list (0);
			break;
		case CMD_LIST_L:
			err = cmd_list (1);
			break;
		case CMD_LIST_LL:
			err = cmd_list (2);
			break;
		case CMD_LIST_LLL:
			err = cmd_list (3);
			break;
		case CMD_LIST_A:
			set_all (1);
			err = cmd_list (1);
			break;
		case CMD_LIST_LA:
			set_all (1);
			err = cmd_list (2);
			break;

		case CMD_NAME_REF:
			err = cmd_name_ref ();
			break;

		case CMD_ILIST:
			err = cmd_ilist (0);
			break;
		case CMD_ILIST_L:
			err = cmd_ilist (1);
			break;
		case CMD_ILIST_LL:
			err = cmd_ilist (2);
			break;
		case CMD_ILIST_A:
			set_all (0);
			err = cmd_ilist (0);
			break;
		case CMD_ILIST_LA:
			set_all (0);
			err = cmd_ilist (1);
			break;

		case CMD_DUMP:
			err = cmd_dump ();
			break;
		case CMD_MEMORY:
			err = cmd_memory ();
			break;
		case CMD_MEMORY_A:
			set_all (1);
			err = cmd_memory ();
			break;
		case CMD_SHA1:
			err = cmd_sha1 ();
			break;
		case CMD_ANALYZE:
			err = cmd_analyze ();
			break;
		case CMD_SPLIT:
			err = cmd_split ();
			break;
		case CMD_IS_TEXTURE:
			err = cmd_is_texture ();
			break;
		case CMD_FEATURES:
			err = cmd_features ();
			break;
		case CMD_DISTRIBUTION:
			err = cmd_distribution ();
			break;
		case CMD_DIFF:
			err = cmd_diff ();
			break;

		case CMD_CHECK:
			err = cmd_check ();
			break;
		case CMD_SLOTS:
			err = cmd_slots ();
			break;
		case CMD_STGI:
			err = cmd_stgi ();
			break;
		case CMD_IS_ARENA:
			err = cmd_is_arena ();
			break;
		case CMD_PATCH:
			err = cmd_patch ();
			break;
		case CMD_COPY:
			err = cmd_copy ();
			break;
		case CMD_DUPLICATE:
			err = cmd_duplicate ();
			break;
		case CMD_NORMALIZE:
			err = cmd_normalize ();
			break;
		case CMD_MINIMAP:
			err = cmd_minimap ();
			break;
		case CMD_COMPRESS:
			err = cmd_compress ();
			break;
		case CMD_DECOMPRESS:
			err = cmd_decompress ();
			break;
		case CMD_ENCODE:
			err = cmd_create (false);
			break;
		case CMD_CREATE:
			err = cmd_create (true);
			break;
		case CMD_UPDATE:
			err = cmd_update ();
			break;
		case CMD_EXTRACT:
		case CMD_XDECODE:
		case CMD_XEXPORT:
		case CMD_XALL:
		case CMD_XCOMMON:
			err = cmd_extract (cmd_ct->id);
			break;

		case CMD_WC24DECRYPT:
			err = cmd_wc24 (false);
			break;
		case CMD_WC24ENCRYPT:
			err = cmd_wc24 (true);
			break;
		case CMD_BMS:
			err = cmd_bms ();
			break;
		case CMD_SPRITES:
			err = cmd_sprites ();
			break;
		case CMD_BCH:
			err = cmd_bch ();
			break;

		case CMD_BINARY:
			err = cmd_convert (true);
			break;
		case CMD_TEXT:
			err = cmd_convert (false);
			break;
		case CMD_CAT:
			err = cmd_cat ();
			break;
		case CMD_BMG:
			err = cmd_bmg_cat (true);
			break;
		case CMD_KCL:
			err = cmd_FFORM (FF_KCL, "course.kcl");
			break;
		case CMD_KMP:
			err = cmd_FFORM (FF_KMP, "course.kmp");
			break;
		case CMD_LEX:
			err = cmd_FFORM (FF_LEX, "course.lex");
			break;
		case CMD_INFO:
			err = cmd_info ();
			break;
		case CMD_GHOST:
			err = cmd_ghost ();
			break;
		case CMD_YAZDUMP:
			err = cmd_yazdump ();
			break;

		case CMD_VEHICLE:
			err = cmd_vehicle ();
			break;

			// no default case defined
			//	=> compiler checks the existence of all enum values

		case CMD__NONE:
		case CMD__N:
			help_exit (false);
	}

	SaveSZSCache (false);
	return PrintErrorStat (err, verbose, cmd_ct->name1);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			main_wszst(), main()		///////////////
///////////////////////////////////////////////////////////////////////////////

int main_wszst (int argc, char **argv)
{
	tool_name = "wszst";
	print_title_func = print_title;
	SetupLib (argc, argv, WSZST_SHORT, VERSION, TITLE);
	SetupExtendedSZS ();

	//----- process arguments

	if (argc < 2)
	{
		printf ("\n%s\n%s\nVisit %s%s for more info.\n\n", text_logo, TITLE, URI_HOME, WSZST_SHORT);
		hint_exit (ERR_OK);
	}

	enumError err = CheckEnvOptions2 ("WSZST_OPT", CheckOptions);
	if (err)
		hint_exit (err);

	err = CheckOptions (argc, argv, false);
	if (err)
		hint_exit (err);

	err = CheckCommand (argc, argv);
	CloseAnalyzeFile ();
	DUMP_TRACE_ALLOC (TRACE_FILE);

	if (SIGINT_level)
		err = ERROR0 (ERR_INTERRUPT, "Program interrupted by user.");
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			tool wrapper			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef int (*main_func) (int argc, char **argv);

extern int main_wszst (int argc, char **argv);
extern int main_wbmgt (int argc, char **argv);
extern int main_wctct (int argc, char **argv);
extern int main_wimgt (int argc, char **argv);
extern int main_wkclt (int argc, char **argv);
extern int main_wkmpt (int argc, char **argv);
extern int main_wlect (int argc, char **argv);
extern int main_wmdlt (int argc, char **argv);
extern int main_wpatt (int argc, char **argv);
extern int main_wstrt (int argc, char **argv);
extern int main_wrapper (int argc, char **argv);
extern int main_getopt (int argc, char **argv);

// [[wrapper_t]]
typedef struct wrapper_t
{
	uint hide; // 0:off, 1:hide, 2:is wrapper
	main_func func;
	ccp name;
	ccp info;
} wrapper_t;

static const wrapper_t wrapper_tab[]
	= { { 0, main_wszst, WSZST_SHORT, WSZST_LONG }, { 0, main_wbmgt, WBMGT_SHORT, WBMGT_LONG },
		  { 0, main_wctct, WCTCT_SHORT, WCTCT_LONG }, { 0, main_wimgt, WIMGT_SHORT, WIMGT_LONG },
		  { 0, main_wkclt, WKCLT_SHORT, WKCLT_LONG }, { 0, main_wkmpt, WKMPT_SHORT, WKMPT_LONG },
		  { 0, main_wlect, WLECT_SHORT, WLECT_LONG }, { 0, main_wmdlt, WMDLT_SHORT, WMDLT_LONG },
		  { 0, main_wpatt, WPATT_SHORT, WPATT_LONG }, { 0, main_wstrt, WSTRT_SHORT, WSTRT_LONG },
		  { 2, main_getopt, "getopt", 0 }, { 2, main_wrapper, "wrapper", 0 }, { 0, 0, 0 } };

///////////////////////////////////////////////////////////////////////////////

static const wrapper_t *FindWrapper (int argc, char **argv)
{
	if (argc > 0 && argv[0])
	{
		ccp name = strrchr (argv[0], '/');
		name = name ? name + 1 : argv[0];
		if (*name == 'w' || *name == 'W')
			name++;

		const wrapper_t *w;
		for (w = wrapper_tab; w->func; w++)
			if (!strncasecmp (w->name + 1, name, 3))
				return w;
	}

	return wrapper_tab;
}

///////////////////////////////////////////////////////////////////////////////

int main_wrapper (int argc, char **argv)
{
	SetupLib (argc, argv, TOOLSET_SHORT, VERSION, TITLE);

	enum
	{
		C_CREATE = 0x001,
		C_OVERWRITE = 0x002,

		C_HARDLINKS = 0x010,
		C_SOFTLINKS = 0x020,
		C_CYGWIN = 0x040,
		C_SHELL = 0x080,
		C_BASH = 0x100,

		C_QUIET = 0x200,
		C_VERBOSE = 0x400,

		C_M_DEST = C_HARDLINKS | C_SOFTLINKS | C_CYGWIN | C_SHELL,
	};

	static const KeywordTab_t keytab[]
		= { { C_CREATE, "CREATE", 0, 3 }, { C_CREATE | C_OVERWRITE, "OVERWRITE", 0, 4 },

			  { C_HARDLINKS, "HARDLINKS", 0, 4 },
#ifdef __CYGWIN__
			  { C_HARDLINKS, "BESTLINKS", 0, 4 },
#endif
			  { C_SOFTLINKS, "SOFTLINKS", 0, 4 },
#ifndef __CYGWIN__
			  { C_SOFTLINKS, "BESTLINKS", 0, 4 },
#endif
			  { C_CYGWIN, "CYGWIN", 0, 3 }, { C_SHELL, "SHELL", 0, 2 },
			  { C_SHELL | C_BASH, "BASH", 0, 4 },

			  { C_QUIET, "QUIET", 0, 1 }, { C_VERBOSE, "VERBOSE", 0, 1 },

			  { 0, 0, 0, 0 } };

	uint i, param = 0;
	for (i = 1; i < argc; i++)
	{
		const KeywordTab_t *key = ScanKeyword (0, argv[i], keytab);
		if (!key || strlen (argv[i]) < key->opt)
			goto abort;
		param |= key->id;
	}

	const uint quiet = param & C_QUIET;
	const uint verbose = param & C_VERBOSE;
	const uint overwrite = param & C_OVERWRITE;
	const uint bash = param & C_BASH;
	const uint desttype = param & C_M_DEST;

	char mypath[PATH_MAX], dest[sizeof (mypath) + 20];
	GetProgramPath (mypath, sizeof (mypath), true, argv[0]);
	if (verbose)
		printf ("My Path: %s\n", mypath);

	if (param & C_CREATE && Count1Bits (&desttype, sizeof (desttype)) == 1 && *mypath)
	{
		memcpy (dest, mypath, sizeof (mypath));
		char *fname = strrchr (dest, '/');
		fname = fname ? fname + 1 : dest;
		if (!*fname)
			goto abort;

		u16 cygwin_link[1000];
		uint cygwin_len = 0;
		if (desttype == C_CYGWIN)
		{
			ccp src = fname;
			while (cygwin_len < sizeof (cygwin_link) / sizeof (*cygwin_link))
			{
				u32 code = ScanUTF8AnsiChar (&src);
				if (!code)
					break;
				write_le16 (cygwin_link + cygwin_len++, code);
			}
		}

		ccp srcname = mypath + (fname - dest);
		char srcname_buf[PATH_MAX];
		if (desttype == C_SHELL)
		{
			ccp src;
			for (src = srcname; *src; src++)
			{
				const int ch = *src;
				if (!isalnum (ch) && ch != '_' && (ch != '-' || src == srcname))
				{
					uint len;
					PrintEscapedString (srcname_buf + 1, sizeof (srcname_buf) - 2, srcname, -1,
						CHMD__ALL, '"', &len);
					if (bash)
						srcname = srcname_buf + 1;
					else
					{
						srcname_buf[0] = srcname_buf[len] = '"';
						srcname = srcname_buf;
					}
					break;
				}
			}
		}

		FILE *f = 0;
		const wrapper_t *w;

		for (w = wrapper_tab; w->func; w++)
		{
			if (w->hide)
				continue;

			strcpy (fname, w->name);
			if (!strcmp (mypath, dest))
				continue;

			struct stat st;
			if (!overwrite && !stat (dest, &st))
				continue;
			unlink (dest);

			switch (desttype)
			{
				case C_HARDLINKS:
					if (!quiet)
						printf ("CREATE HARDLINK %s\n", dest);
					link (mypath, dest);
					break;

				case C_SOFTLINKS:
					if (!quiet)
						printf ("CREATE SOFTLINK %s\n", dest);
					symlink (srcname, dest);
					break;

				case C_CYGWIN:
					if (!quiet)
						printf ("CREATE CYGWIN SOFTLINK %s\n", dest);
					f = fopen (dest, "w");
					if (f)
					{
						fwrite ("!<symlink>\xff\xfe", 1, 12, f);
						fwrite (cygwin_link, 2, cygwin_len, f);
						fwrite ("\0\0", 1, 2, f);
						fclose (f);
					}
					break;

				case C_SHELL:
					if (!quiet)
						printf ("CREATE %sSH SCRIPT %s\n", bash ? "BA" : "", dest);
					f = fopen (dest, "w");
					if (f)
					{
						if (bash)
							fprintf (f,
								"#!/usr/bin/env bash\n"
								"\"$(dirname \"$BASH_SOURCE\")/%s\" %s \"$@\"\n",
								srcname, w->name);
						else
							fprintf (f, "#!/bin/sh\n%s %s \"$@\"\n", srcname, w->name);
						fclose (f);
						chmod (dest, 0755);
					}
					break;
					//"$(dirname "$BASH_SOURCE")"
			}
		}
		return ERR_OK;
	}

abort:;
	fprintf (stdout,
		"\n%s\n%s\n\n   Usage: %s TOOLNAME ...\n\n"
		"This is a wrapper for the following tools:\n\n",
		text_logo, TOOLSET_TITLE, ProgInfo.progname);

	const wrapper_t *w, *active = FindWrapper (argc, argv);
	DASSERT (active);
	for (w = wrapper_tab; w->func; w++)
		if (w == active || !w->hide)
			fprintf (stdout, "  %c %-5s : %s\n", w == active ? '*' : ' ', w->name, w->info);

	fprintf (stdout,
		"\nWrapper commands:\n\n"
		"   WRAPPER HELP\n"
		"   WRAPPER CREATE    [QUIET] HARDLINKS|SOFTLINKS|BESTLINKS|CYGWIN|SHELL|BASH\n"
		"   WRAPPER OVERWRITE [QUIET] HARDLINKS|SOFTLINKS|BESTLINKS|CYGWIN|SHELL|BASH\n"
		"\n"
		"   H[ELP]      : Print this help and exit.\n"
		"   CRE[ATE]    : Create links or scripts, but don't overwrite.\n"
		"   OVER[WRITE] : Create links or scripts and remove existing files before.\n"
		"   HARD[LINKS] : Create hard links to the main program.\n"
		"   SOFT[LINKS] : Create soft links to the main program.\n"
		"   BEST[LINKS] : For Cygwin same as HARDLINKS, for all other same as SOFTLINKS.\n"
		"   CYG[WIN]    : Create softlinks for Cygwin (plain files with special content).\n"
		"   SH[ELL]     : Create simple `sh´ scripts assuming the main program is in PATH.\n"
		"   BASH        : Create `bash´ scripts with run time path detection.\n"
		"   Q[UIET]     : Option to suppress creation messages.\n"
		"\n"
		"   See https://szs.wiimm.de/doc/wrapper for details.\n"
		" \n");

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main (int argc, char **argv)
{
	ArgManager_t am = { 0 };
	SetupArgManager (&am, LOUP_AUTO, argc, argv, false);
	ExpandAtArgManager (&am, AMXM_SHORT, 10, false);
	argc = am.argc;
	argv = am.argv;

	// PRINT("ERR__N=%u\n",ERR__N);
	if (argc > 1)
	{
		const wrapper_t *w;
		for (w = wrapper_tab; w->func; w++)
			if (!strcasecmp (w->name, argv[1]))
			{
				argv[1] = argv[0];
				return w->func (argc - 1, argv + 1);
			}
	}

	const wrapper_t *w = FindWrapper (argc, argv);
	DASSERT (w);
	const int err = w->func (argc, argv);
	ClosePager ();
	return FixExitStatus (err);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    END				///////////////
///////////////////////////////////////////////////////////////////////////////
