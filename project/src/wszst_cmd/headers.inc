
/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2011-2024 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

#include <time.h>
#include <utime.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <zlib.h>

#include "dclib-utf8.h"
#include "lib-analyze.h"
#include "lib-szs.h"
#include "lib-nintendo.h"
#include "lib-excite.h"
#include "lib-nintendo-archives.h"
#include "lib-gtx.h"
#include "lib-hsf.h"
#include "lib-quicklz.h"
#include "lib-zstd.h"
#include "lib-brres.h"
#include "lib-xbmg.h"
#include "lib-msbt.h"
#include "lib-kcl.h"
#include "lib-kmp.h"
#include "lib-ledis.h"
#include "lib-mdl.h"
#include "lib-pat.h"
#include <mxml.h>
#include "lib-chr.h"
#include "lib-srt.h"
#include "lib-ue4pak.h"
#include "lib-nut.h"
#include "lib-smash-arc.h"
#include "lib-prc.h"
#include "lib-cnut.h"
#include "lib-xmsg.h"
#include "lib-nsmbw.h"
#include "lib-koopatlas.h"
#include "lib-chans.h"
#include "lib-vis0.h"
#include "lib-clr.h"
#include "lib-shp.h"
#include "lib-scn.h"
#include "lib-breff.h"
#include "lib-image.h"
#include "lib-common.h"
#include "lib-rkg.h"
#include "lib-model-glb.h"
#include "lib-brres-inject.h"
#include "lib-nud.h"
#include "lib-nut.h"
#include "lib-numsh.h"
#include "lib-bntx.h"
#include "lib-vc.h"
#include "lib-sequence.h"
#include "lib-miirender.h"

static ccp opt_parent = 0;
#include "lib-bzip2.h"
#include "lib-pack.h"
#include "lib-rarc.h"
#include "lib-rkc.h"
#include "lib-staticr.h"
#include "db-dol.h"
#include "crypt.h"
#include "ui.h" // [[dclib]] wrapper
#include "ui-wszst.c"
#include "db-mkw.h"
#include "lib-object.h"
#include "lib-checksum.h"
#include "lib-bflyt.h"
#include "lib-wc24.h"
#include "lib-bms.h"
#include "lib-nitro.h"
#include "lib-bch.h"
#include "lib-bcres.h"
#include "lib-hsd.h"
#include "lib-halbank.h"
#include "lib-passthru.h"
#include "lib-brres-model.h"
#include "lib-nsbmd.h"
#include "lib-bfres.h"
#include "lib-gtx.h"
#include "lib-vc.h"
#include "lib-model-glb.h"

#if HAVE_WIIMM_EXT
#include "lib-vehicle.h"
#include "wcommand.h"
#endif

static inline bool is_ext (ccp src, ccp ext)
{
	ccp dot = strrchr (src, '.');
	return dot && !strcasecmp (dot, ext);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			definitions			///////////////
///////////////////////////////////////////////////////////////////////////////

#define TITLE                                                                                      \
	WSZST_SHORT ": " WSZST_LONG " v" VERSION " r" REVISION " " SYSTEM2 " - " AUTHOR " - " DATE

#define TOOLSET_TITLE                                                                              \
	TOOLSET_SHORT ": " TOOLSET_LONG " v" VERSION " r" REVISION " " SYSTEM2 " - " AUTHOR " - " DATE

//
///////////////////////////////////////////////////////////////////////////////

static void help_exit (bool xmode)
{
	SetupPager ();
	fputs (TITLE "\n", stdout);

	if (xmode)
	{
		int cmd;
		for (cmd = 0; cmd < CMD__N; cmd++)
			PrintHelpCmd (&InfoUI_wszst, stdout, 0, cmd, 0, 0, URI_HOME);
	}
	else
		PrintHelpCmd (&InfoUI_wszst, stdout, 0, 0, "HELP", 0, URI_HOME);

	ClosePager ();
	ExitFixed (ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void list_compressions_exit ()
{
	SetupColors ();
	int fw = GetTermWidth (80, 40) - 1;
	if (fw > 120)
		fw = 120;

	PrintColoredLines (stdout, colout, 0, fw, 0, 0,
		"\n{setup|%s}\n\n{caption|Compression modes:}\n"
		"\n"
		"There are several compression methods and most of them"
		" support different compression levels."
		" Each methods and level can be address by a numerical mode"
		" and some by a name.\n"
		"\n"
		"In general, 3 compression formats are supported: YAZ0/YAZ1, BZ (bzip2) and LZ (LZMA).\n"
		"\n"
		"The following list shows all modes."
		" A mode can by selected by option {opt|--compr=param} or {opt|-C param}.\n"
		"|[3,9,23]\n"
		"{heading| numbers\tnames\tdescription}\n"
		"{heading|%.*s\n"
		"\t{hl| -1\tUNCOMPRESSED}\t|"
		"Don't compress.\n"
		"\t{hl|  0\tNOCHUNKS}\t|"
		"{cmd|YAZ} only: Use only byte copies.\n"
		"\n"
		"\t{hl|  1\tFAST}\t|"
		"Fastest available standard compression.\n"
		"\t{hl|1-8\t}\t|"
		"Standard levels between {par|FAST} and {par|BEST}."
		" Do not use compression level >6 for {cmd|LZMA} if the file is intended"
		" for Mario Kart Wii, as too much memory is required for decoding.\n"
		"\t{hl|  9\tBEST}\t|"
		"Best compression level.\n"
		"\t{hl|\tDEFAULT}\t|"
		"Default compression: {par|6} for {cmd|LZMA} and {par|9} for all other methods.\n"
		"\n"
		"\t\t{hl|TRY2 - TRY5}\t|"
		"Because of many repeated data, the best {cmd|BZ} compression mode varies."
		" Therefor the levels {par|TRY2} to {par|TRY5} (or short {par|T2} to {par|T5})"
		" are defined to find the best compression mode with testing the first"
		" N levels of {par|9, 1, 8, 2, 5}. {par|TRY2} (levels 9 and 1)"
		" is used for normalizing."
		" For other than {cmd|BZ} compressions, {par|BEST} is used instead.\n"
		"\n"
		"\t{hl| 10\tULTRA}\t|"
		"A special time-consuming compression method for {cmd|YAZ} only."
		" It is dedicated to competitions with strict size limitations.\n"
		"\n"
		" {hl|100-150}\t\t|"
		"This {cmd|YAZ} compression uses a back tracking"
		" algorithm with recursion depth between {par|0 and 50} (last 2 digits)."
		" Depths 0 and 1 are similar to mode {par|9}."
		" Each new depth doubles the number of calculated paths."
		" Because of some optimizations, a depth of +5 results"
		" in 10 to 15 times and not in 32 (2^5) times like expected."
		" This algorithm needs 4 times as much memory as the uncompressed file."
		" So it is dedicated to small files only.\n"
#if HAVE_WIIMM_EXT && 0
		"{heading|%.*s\n"
		" {bad|500-530}\t\t|"
		"This {bad|experimental} {cmd|YAZ} compression uses a brute force"
		" algorithm with recursion depth from {par|0 to 30} (last 2 digits)."
		" It is a very time-consuming compression method"
		" and creates smaller files than {par|ULTRA}.\n"
		" {bad|600-630}\t\t|"
		"This {bad|experimental} {cmd|YAZ} compression"
		" is an optimized version of {par|100-130}.\n"
#endif
		"{heading|%.*s\n"
		"\n",
		TITLE, 3 * fw, ThinLine300_3
#if HAVE_WIIMM_EXT && 0
		,
		3 * fw, ThinLine300_3
#endif
		,
		3 * fw, ThinLine300_3);

	ExitFixed (ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void print_version_section (bool print_sect_header)
{
	cmd_version_section (print_sect_header, WSZST_SHORT, WSZST_LONG, long_count - 1);
}

///////////////////////////////////////////////////////////////////////////////

static void version_exit ()
{
	if (brief_count > 1)
		fputs (VERSION "\n", stdout);
	else if (brief_count)
		fputs (VERSION " r" REVISION " " SYSTEM2 "\n", stdout);
	else if (print_sections)
		print_version_section (true);
	else if (long_count)
		print_version_section (false);
	else
		fputs (TITLE "\n", stdout);

	ExitFixed (ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void print_title (FILE *f)
{
	static bool done = false;
	if (!done)
	{
		done = true;
		if (print_sections)
			print_version_section (true);
		else if (verbose >= 1 && f == stdout)
			fprintf (f, "\n%s\n\n", TITLE);
		else
			fprintf (f, "*****  %s  *****\n", TITLE);
	}
}

///////////////////////////////////////////////////////////////////////////////

static const KeywordTab_t *current_command = 0;

static void hint_exit (enumError stat)
{
	if (current_command)
		fprintf (stderr, "-> Type '%s help %s' (pipe it to a pager like 'less') for more help.\n\n",
			ProgInfo.progname, CommandInfo[current_command->id].name1);
	else
		fprintf (stderr,
			"-> Type '%s -h' or '%s help' (pipe it to a pager like 'less') for more help.\n\n",
			ProgInfo.progname, ProgInfo.progname);
	ExitFixed (stat);
}

///////////////////////////////////////////////////////////////////////////////

static void set_all (bool force_cut)
{
	if (all_count++ || force_cut)
		opt_cut = true;
	opt_decode = opt_encode_all = true;
	opt_mipmaps = 1;
	opt_recurse = INT_MAX;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			sizeof data			///////////////
///////////////////////////////////////////////////////////////////////////////

static const sizeof_info_t sizeof_info_szs_list[]
	= {
		  SIZEOF_INFO_TITLE ("SZS basics") SIZEOF_INFO_ENTRY (compatible_info_t) SIZEOF_INFO_ENTRY (SubFile_t) SIZEOF_INFO_ENTRY (
			  SubFileList_t) SIZEOF_INFO_ENTRY (SubDirList_t) SIZEOF_INFO_ENTRY (SubDir_t) SIZEOF_INFO_ENTRY (SubFileIterator_t) SIZEOF_INFO_ENTRY (DataContainer_t)
			  SIZEOF_INFO_ENTRY (search_filter_t) SIZEOF_INFO_ENTRY (SubstString_t) SIZEOF_INFO_ENTRY (repair_ff_t) SIZEOF_INFO_ENTRY (slot_info_t) SIZEOF_INFO_ENTRY (
				  FormatFieldItem_t) SIZEOF_INFO_ENTRY (FormatField_t)
				  SIZEOF_INFO_ENTRY (tiny_param_t) SIZEOF_INFO_ENTRY (ParamList_t) SIZEOF_INFO_ENTRY (
					  List_t) SIZEOF_INFO_ENTRY (MemItem_t) SIZEOF_INFO_ENTRY (SetupDef_t) SIZEOF_INFO_ENTRY (ListDef_t)
					  SIZEOF_INFO_ENTRY (SetupParam_t) SIZEOF_INFO_ENTRY (
						  config_t) SIZEOF_INFO_ENTRY (data_tab_t) SIZEOF_INFO_ENTRY (output_mode_t)

						  SIZEOF_INFO_TITLE (
							  "SZS basics: numeric") SIZEOF_INFO_ENTRY (tri_metrics_t)
							  SIZEOF_INFO_ENTRY (octahedron_t) SIZEOF_INFO_ENTRY (
								  cuboid_t) SIZEOF_INFO_ENTRY (IndexList_t) SIZEOF_INFO_ENTRY (FuncParam_t) SIZEOF_INFO_ENTRY (FuncTable_t)
								  SIZEOF_INFO_ENTRY (FuncInfo_t) SIZEOF_INFO_ENTRY (Var_t) SIZEOF_INFO_ENTRY (
									  VarMap_t) SIZEOF_INFO_ENTRY (ScanLoop_t) SIZEOF_INFO_ENTRY (ScanMacro_t)
									  SIZEOF_INFO_ENTRY (ScanFile_t) SIZEOF_INFO_ENTRY (
										  ScanInfo_t) SIZEOF_INFO_ENTRY (ScanParam_t) SIZEOF_INFO_ENTRY (Line_t)

										  SIZEOF_INFO_TITLE ("Archives and packing") SIZEOF_INFO_ENTRY (BZIP2_t) SIZEOF_INFO_ENTRY (
											  BZ2Manager_t) SIZEOF_INFO_ENTRY (BZ2Source_t) SIZEOF_INFO_ENTRY (CompressManager_t) SIZEOF_INFO_ENTRY (sha1_type_t)
											  SIZEOF_INFO_ENTRY (
												  sha1_db_t) SIZEOF_INFO_ENTRY (pack_header_t)
												  SIZEOF_INFO_ENTRY (
													  pack_metric_t) SIZEOF_INFO_ENTRY (check_cache_t)

													  SIZEOF_INFO_TITLE (
														  "BREFF & BREFT") SIZEOF_INFO_ENTRY (breff_root_head_t)
														  SIZEOF_INFO_ENTRY (breff_root_t) SIZEOF_INFO_ENTRY (breff_item_name_t) SIZEOF_INFO_ENTRY (breff_item_data_t) SIZEOF_INFO_ENTRY (
															  breff_item_list_t) SIZEOF_INFO_ENTRY (breft_image_t)

															  SIZEOF_INFO_TITLE (
																  "BRRES") SIZEOF_INFO_ENTRY (brres_info_t)
																  SIZEOF_INFO_ENTRY (brsub_list_t) SIZEOF_INFO_ENTRY (brsub_info_t) SIZEOF_INFO_ENTRY (
																	  brsub_iterator_t) SIZEOF_INFO_ENTRY (brres_t)
																	  SIZEOF_INFO_ENTRY (tex_info_t) SIZEOF_INFO_ENTRY (name_ref_elem_t) SIZEOF_INFO_ENTRY (
																		  name_ref_var_t) SIZEOF_INFO_ENTRY (name_ref_t)

																		  SIZEOF_INFO_TITLE (
																			  "Common files") SIZEOF_INFO_ENTRY (object_type_t) SIZEOF_INFO_ENTRY (object_mgr_t) SIZEOF_INFO_ENTRY (itemslot_bin_t) SIZEOF_INFO_ENTRY (itemslot_t) SIZEOF_INFO_ENTRY (minigame_kmg_head_t) SIZEOF_INFO_ENTRY (minigame_kmg_t) SIZEOF_INFO_ENTRY (minigame_t) SIZEOF_INFO_ENTRY (objflow_bin_t) SIZEOF_INFO_ENTRY (objflow_t) SIZEOF_INFO_ENTRY (geohit_bin_t)
																			  SIZEOF_INFO_ENTRY (
																				  geohit_t)

																				  SIZEOF_INFO_TITLE (
																					  "CT-CODE") SIZEOF_INFO_ENTRY (ctcode_sect_info_t)
																					  SIZEOF_INFO_ENTRY (
																						  ctcode_header_t) SIZEOF_INFO_ENTRY (ctcode_cup1_data_t)
																						  SIZEOF_INFO_ENTRY (ctcode_cup1_head_t) SIZEOF_INFO_ENTRY (
																							  ctcode_crs1_data_t)
																							  SIZEOF_INFO_ENTRY (ctcode_crs1_head_t) SIZEOF_INFO_ENTRY (
																								  ctcode_cupref_t) SIZEOF_INFO_ENTRY (ctcode_arena_t)
																								  SIZEOF_INFO_ENTRY (
																									  ctcode_t)

																									  SIZEOF_INFO_TITLE (
																										  "DOL") SIZEOF_INFO_ENTRY (DolSectionMap_t)
																										  SIZEOF_INFO_ENTRY (
																											  DolAddressMap_t)

																											  SIZEOF_INFO_TITLE (
																												  "File DB")
																												  SIZEOF_INFO_ENTRY (
																													  DbFile_t)
																													  SIZEOF_INFO_ENTRY (
																														  DbFileSZS_t)
																														  SIZEOF_INFO_ENTRY (
																															  DbFileSHA1_t)
																															  SIZEOF_INFO_ENTRY (
																																  DbFileFILE_t)
																																  SIZEOF_INFO_ENTRY (
																																	  DbFileGROUP_t)

																																	  SIZEOF_INFO_TITLE (
																																		  "Images")
																																		  SIZEOF_INFO_ENTRY (MipmapOptions_t) SIZEOF_INFO_ENTRY (ImageGeometry_t) SIZEOF_INFO_ENTRY (Image_t) SIZEOF_INFO_ENTRY (GenericImgParam_t) SIZEOF_INFO_ENTRY (cmpr_info_t) SIZEOF_INFO_ENTRY (tpl_header_t) SIZEOF_INFO_ENTRY (
																																			  tpl_header_ex_t) SIZEOF_INFO_ENTRY (tpl_imgtab_t) SIZEOF_INFO_ENTRY (tpl_pal_header_t) SIZEOF_INFO_ENTRY (tpl_img_header_t) SIZEOF_INFO_ENTRY (tpl_raw_t) SIZEOF_INFO_ENTRY (tpl_signature_t)
																																			  SIZEOF_INFO_ENTRY (
																																				  bti_header_t)

																																				  SIZEOF_INFO_TITLE ("KCL") SIZEOF_INFO_ENTRY (kcl_attrib_name_t) SIZEOF_INFO_ENTRY (kcl_class_t) SIZEOF_INFO_ENTRY (kcl_type_t) SIZEOF_INFO_ENTRY (kcl_analyze_t) SIZEOF_INFO_ENTRY (kcl_head_t) SIZEOF_INFO_ENTRY (kcl_triangle_t) SIZEOF_INFO_ENTRY (kcl_tridata_flt_t) SIZEOF_INFO_ENTRY (kcl_tridata_dbl_t) SIZEOF_INFO_ENTRY (kcl_tridata0_t) SIZEOF_INFO_ENTRY (kcl_tridata1_t) SIZEOF_INFO_ENTRY (
																																					  kcl_t) SIZEOF_INFO_ENTRY (kcl_tri_param_t) SIZEOF_INFO_ENTRY (kcl_cube_t)
#if SUPPORT_KCL_CUBE
																																					  SIZEOF_INFO_ENTRY (
																																						  kcl_cube_list_t)
#endif
																																						  SIZEOF_INFO_ENTRY (
																																							  kcl_tri_t)

																																							  SIZEOF_INFO_TITLE (
																																								  "KMP basics")
																																								  SIZEOF_INFO_ENTRY (kmp_file_gen_t)
																																									  SIZEOF_INFO_ENTRY (
																																										  kmp_file_mkw_t)
																																										  SIZEOF_INFO_ENTRY (
																																											  kmp_file_wim0_t) SIZEOF_INFO_ENTRY (kmp_head_info_t) SIZEOF_INFO_ENTRY (kmp_list_head_t) SIZEOF_INFO_ENTRY (kmp_ktpt_entry_t) SIZEOF_INFO_ENTRY (kmp_enpt_entry_t) SIZEOF_INFO_ENTRY (kmp_enph_entry_t) SIZEOF_INFO_ENTRY (kmp_ckpt_entry_t) SIZEOF_INFO_ENTRY (kmp_gobj_entry_t) SIZEOF_INFO_ENTRY (kmp_poti_group_t) SIZEOF_INFO_ENTRY (kmp_poti_point_t) SIZEOF_INFO_ENTRY (kmp_area_entry_t) SIZEOF_INFO_ENTRY (kmp_came_entry_t) SIZEOF_INFO_ENTRY (kmp_jgpt_entry_t) SIZEOF_INFO_ENTRY (kmp_stgi_entry_t) SIZEOF_INFO_ENTRY (kmp_wim0_t) SIZEOF_INFO_ENTRY (kmp_wim0_sect_t) SIZEOF_INFO_ENTRY (kmp_wim0_info_t) SIZEOF_INFO_ENTRY (kmp_section_t) SIZEOF_INFO_ENTRY (kmp_rtype_info_t) SIZEOF_INFO_ENTRY (kmp_gopt_t)
																																											  SIZEOF_INFO_ENTRY (
																																												  kmp_gopt2_t)
																																												  SIZEOF_INFO_ENTRY (
																																													  kmp_group_elem_t) SIZEOF_INFO_ENTRY (kmp_group_t) SIZEOF_INFO_ENTRY (kmp_group_list_t)
																																													  SIZEOF_INFO_ENTRY (
																																														  kmp_rtobj_t)
																																														  SIZEOF_INFO_ENTRY (
																																															  kmp_rtobj_list_t)
																																															  SIZEOF_INFO_ENTRY (
																																																  kmp_ph_t) SIZEOF_INFO_ENTRY (kmp_flag_t) SIZEOF_INFO_ENTRY (kmp_t) SIZEOF_INFO_ENTRY (Itembox_t) SIZEOF_INFO_ENTRY (DrawKCL_t) SIZEOF_INFO_ENTRY (pos_param_t)
																																																  SIZEOF_INFO_ENTRY (pos_file_t) SIZEOF_INFO_ENTRY (
																																																	  kmp_obj_info_t)

																																																	  SIZEOF_INFO_TITLE ("KMP analysis") SIZEOF_INFO_ENTRY (
																																																		  kmp_linfo_t) SIZEOF_INFO_ENTRY (kmp_finish_t) SIZEOF_INFO_ENTRY (kmp_usedpos_obj_t) SIZEOF_INFO_ENTRY (kmp_usedpos_t) SIZEOF_INFO_ENTRY (kmp_pflags_t)
																																																		  SIZEOF_INFO_ENTRY (
																																																			  gobj_cond_ref_t)
																																																			  SIZEOF_INFO_ENTRY (
																																																				  gobj_cond_mask_t)
																																																				  SIZEOF_INFO_ENTRY (
																																																					  cond_ref_info_t) SIZEOF_INFO_ENTRY (kmp_ana_defobj_t) SIZEOF_INFO_ENTRY (kmp_ana_ref_t) SIZEOF_INFO_ENTRY (gobj_condition_t)
																																																					  SIZEOF_INFO_ENTRY (gobj_condition_set_t) SIZEOF_INFO_ENTRY (kmp_gobj_info_t) SIZEOF_INFO_ENTRY (kmp_ana_gobj_t) SIZEOF_INFO_ENTRY (kmp_ana_pflag_res_t) SIZEOF_INFO_ENTRY (
																																																						  kmp_ana_pflag_t)

																																																						  SIZEOF_INFO_TITLE ("LE-CODE basics") SIZEOF_INFO_ENTRY (lecode_debug_info_t) SIZEOF_INFO_ENTRY (lecode_debug_ex_t) SIZEOF_INFO_ENTRY (le_lpar_t) SIZEOF_INFO_ENTRY (le_binary_head_v3_t) SIZEOF_INFO_ENTRY (
																																																							  le_binary_head_v4_t)
																																																							  SIZEOF_INFO_ENTRY (le_binary_head_v5_34_t) SIZEOF_INFO_ENTRY (
																																																								  le_binary_head_v5_38_t) SIZEOF_INFO_ENTRY (le_binary_head_v5_44_t) SIZEOF_INFO_ENTRY (le_binary_head_v5_t)
																																																								  SIZEOF_INFO_ENTRY (le_binary_head_t) SIZEOF_INFO_ENTRY (le_binary_param_t) SIZEOF_INFO_ENTRY (le_binpar_v1_35_t) SIZEOF_INFO_ENTRY (le_binpar_v1_37_t) SIZEOF_INFO_ENTRY (le_binpar_v1_f8_t) SIZEOF_INFO_ENTRY (le_binpar_v1_1b8_t) SIZEOF_INFO_ENTRY (le_binpar_v1_1bc_t) SIZEOF_INFO_ENTRY (le_binpar_v1_260_t) SIZEOF_INFO_ENTRY (
																																																									  le_binpar_v1_264_t)
																																																									  SIZEOF_INFO_ENTRY (
																																																										  le_binpar_v1_t) SIZEOF_INFO_ENTRY (le_cup_par_t) SIZEOF_INFO_ENTRY (le_course_par_t) SIZEOF_INFO_ENTRY (le_analyze_t) SIZEOF_INFO_ENTRY (le_region_t) SIZEOF_INFO_ENTRY (le_cup_track_t) SIZEOF_INFO_ENTRY (le_property_t) SIZEOF_INFO_ENTRY (le_music_t) SIZEOF_INFO_ENTRY (le_flags8_t) SIZEOF_INFO_ENTRY (le_flags_t)
																																																										  SIZEOF_INFO_ENTRY (
																																																											  LecodeFlags_t)

																																																											  SIZEOF_INFO_TITLE (
																																																												  "LE-CODE distribution")
																																																												  SIZEOF_INFO_ENTRY (
																																																													  le_group_t) SIZEOF_INFO_ENTRY (le_track_type_t) SIZEOF_INFO_ENTRY (le_track_status_t)
																																																													  SIZEOF_INFO_ENTRY (
																																																														  le_track_text_t)
																																																														  SIZEOF_INFO_ENTRY (
																																																															  le_options_t)
																																																															  SIZEOF_INFO_ENTRY (
																																																																  le_cup_ref_t)
																																																																  SIZEOF_INFO_ENTRY (le_cup_t) SIZEOF_INFO_ENTRY (le_strpar_t) SIZEOF_INFO_ENTRY (
																																																																	  le_track_id_t) SIZEOF_INFO_ENTRY (le_track_t)
																																																																	  SIZEOF_INFO_ENTRY (
																																																																		  le_track_arch_t)
																																																																		  SIZEOF_INFO_ENTRY (le_auto_setup_t) SIZEOF_INFO_ENTRY (le_type_settings_t) SIZEOF_INFO_ENTRY (le_settings_t) SIZEOF_INFO_ENTRY (le_group_info_t) SIZEOF_INFO_ENTRY (
																																																																			  le_distrib_t)
																																																																			  SIZEOF_INFO_ENTRY (
																																																																				  DistributionInfo_t)

																																																																				  SIZEOF_INFO_TITLE (
																																																																					  "LEX") SIZEOF_INFO_ENTRY (features_szs_t)
																																																																					  SIZEOF_INFO_ENTRY (check_features_szs_t) SIZEOF_INFO_ENTRY (
																																																																						  have_lex_info_t)
																																																																						  SIZEOF_INFO_ENTRY (
																																																																							  lex_header_t) SIZEOF_INFO_ENTRY (lex_element_t) SIZEOF_INFO_ENTRY (lex_dev1_t)
																																																																							  SIZEOF_INFO_ENTRY (
																																																																								  lex_set1_t) SIZEOF_INFO_ENTRY (lex_ctdn_t) SIZEOF_INFO_ENTRY (lex_hipt_rule_t) SIZEOF_INFO_ENTRY (lex_ritp_rule_t)
		  // [[new-lex-sect]]
		  SIZEOF_INFO_ENTRY (lex_test_t) SIZEOF_INFO_ENTRY (lex_item_t) SIZEOF_INFO_ENTRY (
			  lex_t) SIZEOF_INFO_ENTRY (lex_info_t)

			  SIZEOF_INFO_TITLE ("LE-CODE file types") SIZEOF_INFO_ENTRY (
				  lta_header_t) SIZEOF_INFO_ENTRY (lta_node_par_t) SIZEOF_INFO_ENTRY (lta_node_record_t)
				  SIZEOF_INFO_ENTRY (lta_node_t) SIZEOF_INFO_ENTRY (
					  lta_manager_t) SIZEOF_INFO_ENTRY (lfl_header_t) SIZEOF_INFO_ENTRY (lfl_node_t)

					  SIZEOF_INFO_TITLE ("MDL")
						  SIZEOF_INFO_ENTRY (mdl_head_t) SIZEOF_INFO_ENTRY (mdl_t) SIZEOF_INFO_ENTRY (mdl_sect0_t) SIZEOF_INFO_ENTRY (
							  mdl_sect1_t) SIZEOF_INFO_ENTRY (mdl_sect2_t)
							  SIZEOF_INFO_ENTRY (
								  mdl_sect8_t) SIZEOF_INFO_ENTRY (mdl_sect8_layer_t)
								  SIZEOF_INFO_ENTRY (Slot42MaterialInfo_t) SIZEOF_INFO_ENTRY (Slot42MaterialStat_t) SIZEOF_INFO_ENTRY (
									  mdl_sect10_t) SIZEOF_INFO_ENTRY (mdl_minimap_t) SIZEOF_INFO_ENTRY (StringIteratorMDL_t)

									  SIZEOF_INFO_TITLE (
										  "MKW") SIZEOF_INFO_ENTRY (TrackInfo_t)
										  SIZEOF_INFO_ENTRY (MusicInfo_t)

											  SIZEOF_INFO_TITLE (
												  "Object DB") SIZEOF_INFO_ENTRY (ObjSettingFormat_t)
												  SIZEOF_INFO_ENTRY (ObjectInfo_t) SIZEOF_INFO_ENTRY (ObjSpec_t) SIZEOF_INFO_ENTRY (
													  UsedObject_t) SIZEOF_INFO_ENTRY (UsedFile_t) SIZEOF_INFO_ENTRY (UsedFileSZS_t)
													  SIZEOF_INFO_ENTRY (UsedFileSHA1_t) SIZEOF_INFO_ENTRY (
														  UsedFileFILE_t) SIZEOF_INFO_ENTRY (UsedFileGROUP_t)

														  SIZEOF_INFO_TITLE (
															  "PAT") SIZEOF_INFO_ENTRY (pat_head_t)
															  SIZEOF_INFO_ENTRY (pat_s0_belem_t) SIZEOF_INFO_ENTRY (pat_s0_bhead_t) SIZEOF_INFO_ENTRY (
																  pat_s0_sref_t) SIZEOF_INFO_ENTRY (pat_s0_selem_t)
																  SIZEOF_INFO_ENTRY (pat_s0_shead_t) SIZEOF_INFO_ENTRY (
																	  pat_analyse_t) SIZEOF_INFO_ENTRY (pat_element_t)
																	  SIZEOF_INFO_ENTRY (pat_t)

																		  SIZEOF_INFO_TITLE ("RARC") SIZEOF_INFO_ENTRY (
																			  rarc_file_header_t)
																			  SIZEOF_INFO_ENTRY (
																				  rarc_header_t) SIZEOF_INFO_ENTRY (rarc_node_t)
																				  SIZEOF_INFO_ENTRY (
																					  rarc_entry_t)

																					  SIZEOF_INFO_TITLE (
																						  "RKC") SIZEOF_INFO_ENTRY (rkco_t)
																						  SIZEOF_INFO_ENTRY (
																							  rkct_t)

																							  SIZEOF_INFO_TITLE (
																								  "RKG") SIZEOF_INFO_ENTRY (rkg_head_t)
																								  SIZEOF_INFO_ENTRY (
																									  rkg_info_t)

																									  SIZEOF_INFO_TITLE (
																										  "StaticR")
																										  SIZEOF_INFO_ENTRY (str_server_list_t) SIZEOF_INFO_ENTRY (
																											  str_server_patch_t)
																											  SIZEOF_INFO_ENTRY (
																												  staticr_t) SIZEOF_INFO_ENTRY (VersusPointsInfo_t) SIZEOF_INFO_ENTRY (rel_header_t) SIZEOF_INFO_ENTRY (rel_sect_info_t) SIZEOF_INFO_ENTRY (rel_imp_t) SIZEOF_INFO_ENTRY (rel_data_t) SIZEOF_INFO_ENTRY (rel_info_t) SIZEOF_INFO_ENTRY (wpf_t) SIZEOF_INFO_ENTRY (wpf_head_t) SIZEOF_INFO_ENTRY (addr_port_t) SIZEOF_INFO_ENTRY (addr_port_version_t)

																												  SIZEOF_INFO_TITLE (
																													  "SZS") SIZEOF_INFO_ENTRY (yaz0_header_t) SIZEOF_INFO_ENTRY (u8_header_t) SIZEOF_INFO_ENTRY (u8_node_t) SIZEOF_INFO_ENTRY (string_pool_t) SIZEOF_INFO_ENTRY (szs_subfile_t) SIZEOF_INFO_ENTRY (szs_subfile_list_t) SIZEOF_INFO_ENTRY (szs_have_t)
																													  SIZEOF_INFO_ENTRY (szs_file_t) SIZEOF_INFO_ENTRY (
																														  szs_u8_info_t)
																														  SIZEOF_INFO_ENTRY (
																															  szs_norm_t)
																															  SIZEOF_INFO_ENTRY (
																																  scan_data_t)
																																  SIZEOF_INFO_ENTRY (
																																	  add_missing_t)
		  //	SIZEOF_INFO_ENTRY(check_szs_t)
		  SIZEOF_INFO_ENTRY (slot_ana_t) SIZEOF_INFO_ENTRY (iterator_param_t) SIZEOF_INFO_ENTRY (
			  szs_iterator_t) SIZEOF_INFO_ENTRY (brres_header_t) SIZEOF_INFO_ENTRY (brres_entry_t)
			  SIZEOF_INFO_ENTRY (brres_group_t) SIZEOF_INFO_ENTRY (brres_root_t) SIZEOF_INFO_ENTRY (
				  brsub_header_t) SIZEOF_INFO_ENTRY (brsub_cut_t) SIZEOF_INFO_ENTRY (szs_extract_t)
				  SIZEOF_INFO_ENTRY (raw_data_t) SIZEOF_INFO_ENTRY (wbz_header_t)
					  SIZEOF_INFO_ENTRY (wlz_header_t) SIZEOF_INFO_ENTRY (ybz_header_t)
						  SIZEOF_INFO_ENTRY (ylz_header_t) SIZEOF_INFO_ENTRY (analyze_param_t)
							  SIZEOF_INFO_ENTRY (analyze_szs_t) SIZEOF_INFO_ENTRY (file_type_t)
								  SIZEOF_INFO_ENTRY (mkw_prefix_t) SIZEOF_INFO_ENTRY (
									  mkw_category_t) SIZEOF_INFO_ENTRY (mkw_category_list_t)
									  SIZEOF_INFO_ENTRY (split_filename_t) SIZEOF_INFO_ENTRY (
										  print_split_par_t) SIZEOF_INFO_ENTRY (sha1_reference_t)

										  SIZEOF_INFO_TERM ()
	  };

//-----------------------------------------------------------------------------
