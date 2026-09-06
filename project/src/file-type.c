
//
///////////////////////////////////////////////////////////////////////////////
//////   This file is created by a script. Modifications will be lost!   //////
///////////////////////////////////////////////////////////////////////////////

#include "file-type.h"

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////   FileTypeTab[]   //////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const char filetype_info_unknown[] = "?";
const char filetype_info_not_supported[] = "not supported";

const file_type_t FileTypeTab[FF_N + 1] = {

	// FF_UNKNOWN = 0
	{ FF_UNKNOWN, 0, 0, "?", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, filetype_info_not_supported, filetype_info_not_supported, "Unknown file" },

	// FF_YAZ0 = 1
	{ FF_YAZ0, 0, 0, "YAZ0", ".szs", ".szs", ".szs", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 4,
		{ 0x59, 0x61, 0x7a, 0x30 }, // "Yaz0"
		0, "0", "0,1", "YAZ compression" },

	// FF_YAZ1 = 2
	{ FF_YAZ1, 0, 0, "YAZ1", ".szs", ".szs", ".szs", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 4,
		{ 0x59, 0x61, 0x7a, 0x31 }, // "Yaz1"
		0, "1", "0,1", "YAZ compression" },

	// FF_XYZ = 3
	{ FF_XYZ, 0, 0, "XYZ", ".xyz", ".szs", ".xyz", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 0,
		{ 0 }, // no magic
		0, MinusString, MinusString, "disguise YAZ compression" },

	// FF_BZ = 4
	{ FF_BZ, 0, 0, "BZ", ".bz", ".szs", ".bz", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 4,
		{ 0x57, 0x42, 0x5a, 0x61 }, // "WBZa"
		0, MinusString, MinusString, "YAZ0 like, but with bzip2 compression" },

	// FF_YBZ = 5
	{ FF_YBZ, 0, 0, "YBZ", ".ybz", ".szs", ".ybz", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 4,
		{ 0x59, 0x42, 0x5a, 0x30 }, // "YBZ0"
		0, MinusString, MinusString, "YAZ0 like header, but with bzip2 compression" },

	// FF_BZIP2 = 6
	{ FF_BZIP2, 0, 0, "BZIP2", ".bz2", ".szs", ".bz2", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 0,
		{ 0 }, // no magic
		0, MinusString, MinusString, "bzip2 compressed file" },

	// FF_LZ = 7
	{ FF_LZ, 0, 0, "LZ", ".lz", ".szs", ".lz", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 4,
		{ 0x57, 0x4c, 0x5a, 0x61 }, // "WLZa"
		0, MinusString, MinusString, "YAZ0 like, but with LZMA compression" },

	// FF_YLZ = 8
	{ FF_YLZ, 0, 0, "YLZ", ".ylz", ".szs", ".ylz", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 4,
		{ 0x59, 0x4c, 0x5a, 0x30 }, // "YLZ0"
		0, MinusString, MinusString, "YAZ0 like header, but with LZMA compression" },

	// FF_LZMA = 9
	{ FF_LZMA, 0, 0, "LZMA", ".lzma", ".szs", ".lzma", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 0,
		{ 0 }, // no magic
		0, MinusString, MinusString, "LZMA compressed file" },

	// FF_XZ = 10
	{ FF_XZ, 0, 0, "XZ", ".xz", ".szs", ".xz", FFT_VALID | FFT_COMPRESS, 0, { 0 }, // no magic
		0, MinusString, MinusString, "xz compressed file" },

	// FF_U8 = 11
	{ FF_U8, 0, 0, "U8", ".arc", ".szs", ".arc",
		FFT_VALID | FFT_ARCHIVE | FFT_TRACK | FFT_CREATE | FFT_EXTRACT | FFT_CUT | FFT_LINK, 4,
		{ 0x55, 0xaa, 0x38, 0x2d }, // "Uª8-"
		0, MinusString, MinusString, "Nintendos archive format" },

	// FF_WU8 = 12
	{ FF_WU8, 0, 0, "WU8", ".wu8", ".wu8", ".wu8",
		FFT_VALID | FFT_ARCHIVE | FFT_TRACK | FFT_CREATE | FFT_EXTRACT | FFT_CUT | FFT_LINK, 4,
		{ 0x57, 0x55, 0x38, 0x61 }, // "WU8a"
		0, MinusString, MinusString, "Wiimms differential U8" },

	// FF_RARC = 13
	{ FF_RARC, 0, 0, "RARC", ".rarc", ".arc", ".rarc",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT | FFT_CUT, 4,
		{ 0x52, 0x41, 0x52, 0x43 }, // "RARC"
		0, MinusString, MinusString, "Archive format for objects" },

	// FF_BRRES = 14
	{ FF_BRRES, 0, 0, "BRRES", ".brres", ".szs", ".bres",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT | FFT_CUT, 4,
		{ 0x62, 0x72, 0x65, 0x73 }, // "bres"
		0, MinusString, MinusString, "Archive format for objects" },

	// FF_BREFF = 15
	{ FF_BREFF, 0, 0, "BREFF", ".breff", ".szs", ".reff",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT, 4, { 0x52, 0x45, 0x46, 0x46 }, // "REFF"
		0, "9,11", "9,11", "Main file of an effect pair" },

	// FF_BREFT = 16
	{ FF_BREFT, 0, 0, "BREFT", ".breft", ".szs", ".reft",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT | FFT_CUT, 4,
		{ 0x52, 0x45, 0x46, 0x54 }, // "REFT"
		0, "9,11", "9,11", "Image file of an effect pair" },

	// FF_RKC = 17
	{ FF_RKC, 0, 0, "RKC", ".rkc", ".szs", ".rkc",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT | FFT_CUT, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_PACK = 18
	{ FF_PACK, 0, 0, "PACK", ".pack", ".szs", ".pack",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT | FFT_CUT, 4,
		{ 0x50, 0x41, 0x43, 0x4b }, // "PACK"
		0, MinusString, MinusString, "Simple archive format" },

	// FF_USE_LTA = 19
	{ FF_USE_LTA, 0, 0, "USE-LTA", ".szs", ".szs", ".szs", FFT_VALID, 8,
		{ 0x23, 0x55, 0x53, 0x45, 0x2d, 0x4c, 0x54, 0x41 }, // "#USE-LTA"
		0, MinusString, MinusString, "LE-CODE redirect to LTA" },

	// FF_LTA = 20
	{ FF_LTA, 0, 0, "LTA", ".lta", "", ".lta",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT | FFT_CUT, 8,
		{ 0x4c, 0x54, 0x52, 0x2d, 0x41, 0x52, 0x43, 0x48 }, // "LTR-ARCH"
		0, MinusString, MinusString, "LE-CODE Track Archive" },

	// FF_LFL = 21
	{ FF_LFL, 0, 0, "LFL", ".lfl", ".szs", ".lfl",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT | FFT_CUT, 4,
		{ 0x4c, 0x2d, 0x46, 0x4c }, // "L-FL"
		0, MinusString, MinusString, "LE-CODE File List" },

	// FF_CHR = 22
	{ FF_CHR, FF_CHR, FF_CHR_TXT, "CHR", ".chr", ".szs", ".chr0",
		FFT_VALID | FFT_BRSUB | FFT_BRSUB2 | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x43, 0x48, 0x52, 0x30 }, // "CHR0"
		"AnmChr(NW4R)", "5,(*)", "4,5", "Model movement animations" },

	// FF_CLR = 23
	{ FF_CLR, FF_CLR, FF_CLR_TXT, "CLR", ".clr", ".szs", ".clr0",
		FFT_VALID | FFT_BRSUB | FFT_BRSUB2 | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x43, 0x4c, 0x52, 0x30 }, // "CLR0"
		"AnmClr(NW4R)", "4,(*)", "3,4", "Colour changing animations" },

	// FF_MDL = 24
	{ FF_MDL, FF_MDL, FF_MDL_TXT, "MDL", ".mdl", ".szs", ".mdl0",
		FFT_VALID | FFT_BRSUB | FFT_BRSUB2 | FFT_CUT | FFT_DECODE | FFT_PATCH, 4,
		{ 0x4d, 0x44, 0x4c, 0x30 }, // "MDL0"
		"3DModels(NW4R)", "11,(*)", filetype_info_not_supported, "Model files" },

	// FF_PAT = 25
	{ FF_PAT, FF_PAT, FF_PAT_TXT, "PAT", ".pat", ".szs", ".pat0",
		FFT_VALID | FFT_BRSUB | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x50, 0x41, 0x54, 0x30 }, // "PAT0"
		"AnmTexPat(NW4R)", "4,(*)", "4", "Texture swapping animations" },

	// FF_SCN = 26
	{ FF_SCN, FF_SCN, FF_SCN_TXT, "SCN", ".scn", ".szs", ".scn0",
	    FFT_VALID | FFT_BRSUB | FFT_BRSUB2 | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x53, 0x43, 0x4e, 0x30 }, // "SCN0"
		"AnmScn(NW4R)", "5,(*)", filetype_info_not_supported, "Polygon morphing animations" },

	// FF_SHP = 27
	{ FF_SHP, FF_SHP, FF_SHP_TXT, "SHP", ".shp", ".szs", ".shp0",
		FFT_VALID | FFT_BRSUB | FFT_BRSUB2 | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x53, 0x48, 0x50, 0x30 }, // "SHP0"
		"AnmShp(NW4R)", "4,(*)", "3,4", "Vertex morph animations" },

	// FF_SRT = 28
	{ FF_SRT, FF_SRT, FF_SRT_TXT, "SRT", ".srt", ".szs", ".srt0",
		FFT_VALID | FFT_BRSUB | FFT_BRSUB2 | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x53, 0x52, 0x54, 0x30 }, // "SRT0"
		"AnmTexSrt(NW4R)", "5,(*)", "4,5", "Texture movement animations" },

	// FF_TEX = 29
	{ FF_TEX, 0, 0, "TEX", ".tex", ".szs", ".tex0",
		FFT_VALID | FFT_GRAPHIC | FFT_BRSUB | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x54, 0x45, 0x58, 0x30 }, // "TEX0"
		"Textures(NW4R)", "3,(*)", "3", "Texture file" },

	// FF_TEX_CT = 30
	{ FF_TEX_CT, 0, 0, "TEX+CT", ".tex", ".szs", ".tex0",
		FFT_VALID | FFT_GRAPHIC | FFT_BRSUB | FFT_CUT | FFT_DECODE | FFT_ENCODE | FFT_PATCH, 0,
		{ 0 }, // no magic
		0, "3,(*)", "3", "Texture file with CTGP-CODE" },

	// FF_CTDEF = 31
	{ FF_CTDEF, 0, 0, "CT-DEF", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x43, 0x54, 0x2d, 0x43, 0x4f, 0x44, 0x45 }, // "#CT-CODE"
		0, MinusString, MinusString, "CT-CODE definition file" },

	// FF_CT0_CODE = 32
	{ FF_CT0_CODE, 0, 0, "C0CODE", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-CODE for main.dol section T2" },

	// FF_CT0_DATA = 33
	{ FF_CT0_DATA, 0, 0, "C0DATA", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-DATA for main.dol section D8" },

	// FF_CT1_CODE = 34
	{ FF_CT1_CODE, 0, 0, "C1CODE", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-CODE for strap TEX0 file" },

	// FF_CT1_DATA = 35
	{ FF_CT1_DATA, 0, 0, "C1DATA", ".ctcode", ".szs", ".ctcode",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_PATCH, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-DATA for strap TEX0 file" },

	// FF_CUP1 = 36
	{ FF_CUP1, 0, 0, "CUP1", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-DATA section: Cups" },

	// FF_CRS1 = 37
	{ FF_CRS1, 0, 0, "CRS1", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-DATA section: Tracks" },

	// FF_MOD1 = 38
	{ FF_MOD1, 0, 0, "MOD1", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-DATA section: main.dol patches" },

	// FF_MOD2 = 39
	{ FF_MOD2, 0, 0, "MOD2", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-DATA section: StaticR.rel patches" },

	// FF_OVR1 = 40
	{ FF_OVR1, 0, 0, "OVR1", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "CT-DATA section: Speedometer" },

	// FF_LE_BIN = 41
	{ FF_LE_BIN, 0, 0, "LE-BIN", ".bin", ".szs", ".bin",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_PATCH, 0, { 0 }, // no magic
		0, MinusString, MinusString, "LE-CODE binary" },

	// FF_LEX = 42
	{ FF_LEX, FF_LEX, FF_LEX_TXT, "LEX", ".lex", ".szs", ".lex",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4, { 0x4c, 0x45, 0x2d, 0x58 }, // "LE-X"
		0, MinusString, MinusString, "LECODE extension file" },

	// FF_LEX_TXT = 43
	{ FF_LEX_TXT, FF_LEX, FF_LEX_TXT, "LEX-TXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x4c, 0x45, 0x58 }, // "#LEX"
		0, MinusString, MinusString, "Text version of LEX" },

	// FF_LPAR = 44
	{ FF_LPAR, 0, 0, "LPAR", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x4c, 0x45, 0x2d, 0x4c, 0x50, 0x41, 0x52 }, // "#LE-LPAR"
		0, MinusString, MinusString, "Text file with LE-CODE parameters" },

	// FF_LEDEF = 45
	{ FF_LEDEF, 0, 0, "LE-DEF", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x4c, 0x45, 0x2d, 0x44, 0x45, 0x46, 0x31 }, // "#LE-DEF1"
		0, MinusString, MinusString, "LE-CODE definition file" },

	// FF_LEDIS = 46
	{ FF_LEDIS, 0, 0, "LE-DIS", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x23, 0x4c, 0x45, 0x2d, 0x44, 0x49, 0x53, 0x54 }, // "#LE-DIST"
		0, MinusString, MinusString, "Text file with CT+LE-CODE distribution settings" },

	// FF_LEREF = 47
	{ FF_LEREF, 0, 0, "LE-REF", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x23, 0x4c, 0x45, 0x2d, 0x52, 0x45, 0x46, 0x31 }, // "#LE-REF1"
		0, MinusString, MinusString, "Text file as track reference" },

	// FF_LESTR = 48
	{ FF_LESTR, 0, 0, "LE-STR", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x23, 0x4c, 0x45, 0x2d, 0x53, 0x54, 0x52, 0x31 }, // "#LE-STR1"
		0, MinusString, MinusString, "Text file strings for tracks" },

	// FF_SHA1REF = 49
	{ FF_SHA1REF, 0, 0, "SHA1REF", ".txt", ".szs", ".txt", FFT_VALID | FFT_TEXT | FFT_ENCODE, 8,
		{ 0x23, 0x53, 0x48, 0x41, 0x31, 0x52, 0x45, 0x46 }, // "#SHA1REF"
		0, MinusString, MinusString, "Text file with SHA1 of tracks" },

	// FF_SHA1ID = 50
	{ FF_SHA1ID, 0, 0, "SHA1ID", ".txt", ".szs", ".txt", FFT_VALID | FFT_TEXT | FFT_ENCODE, 8,
		{ 0x23, 0x53, 0x48, 0x41, 0x31, 0x49, 0x44, 0x31 }, // "#SHA1ID1"
		0, MinusString, MinusString, "Text file with SHA1 and file_id of tracks" },

	// FF_PREFIX = 51
	{ FF_PREFIX, 0, 0, "PREFIX", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x23, 0x50, 0x52, 0x45, 0x46, 0x49, 0x58, 0x31 }, // "#PREFIX1"
		0, MinusString, MinusString, "Text file with console/game prefixes and info" },

	// FF_MTCAT = 52
	{ FF_MTCAT, 0, 0, "MTCAT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x23, 0x4d, 0x54, 0x43, 0x41, 0x54, 0x30, 0x33 }, // "#MTCAT03"
		0, MinusString, MinusString, "Text file with MKW Track Categories" },

	// FF_CT_SHA1 = 53
	{ FF_CT_SHA1, 0, 0, "CT-SHA1", ".txt", ".szs", ".txt", FFT_VALID | FFT_TEXT, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_MDL_TXT = 54
	{ FF_MDL_TXT, FF_MDL, FF_MDL_TXT, "MDLTXT", ".txt", ".szs", ".txt", FFT_VALID | FFT_TEXT, 4,
		{ 0x23, 0x4d, 0x44, 0x4c }, // "#MDL"
		0, filetype_info_not_supported, MinusString, "Text version of MDL" },

	// FF_PAT_TXT = 55
	{ FF_PAT_TXT, FF_PAT, FF_PAT_TXT, "PATTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x50, 0x41, 0x54 }, // "#PAT"
		0, MinusString, MinusString, "Text version of PAT" },

	// FF_TPL = 56
	{ FF_TPL, 0, 0, "TPL", ".tpl", ".szs", ".tpl",
		FFT_VALID | FFT_GRAPHIC | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x00, 0x20, 0xaf, 0x30 }, // "\000 ¯0"
		0, MinusString, MinusString, "Image container" },

	// FF_TPLX = 57
	{ FF_TPLX, 0, 0, "TPLx", ".tpl", ".szs", ".tpl",
		FFT_VALID | FFT_GRAPHIC | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x00, 0x20, 0xaf, 0x30 }, // "\000 ¯0"
		0, MinusString, MinusString, "Image container with extended file header" },

	// FF_CUPICON = 58
	{ FF_CUPICON, 0, 0, "TPLx", ".tpl", ".szs", ".tpl", FFT_VALID | FFT_GRAPHIC | FFT_ENCODE, 0,
		{ 0 }, // no magic
		0, MinusString, MinusString, "Alias for TPLx.CMPR" },

	// FF_BTI = 59
	{ FF_BTI, 0, 0, "BTI", ".bti", ".szs", ".bti",
		FFT_VALID | FFT_GRAPHIC | FFT_CUT | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Image container" },

	// FF_BREFT_IMG = 60
	{ FF_BREFT_IMG, 0, 0, "BT-IMG", ".bt-img", ".szs", ".bt-img",
		FFT_VALID | FFT_GRAPHIC | FFT_CUT | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Raw image of BREFT file" },

	// FF_BMG = 61
	{ FF_BMG, FF_BMG, FF_BMG_TXT, "BMG", ".bmg", ".szs", ".bmg",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Message file" },

	// FF_BMG_TXT = 62
	{ FF_BMG_TXT, FF_BMG, FF_BMG_TXT, "BMGTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Text version of BMG" },

	// FF_KCL = 63
	{ FF_KCL, FF_KCL, FF_KCL_TXT, "KCL", ".kcl", ".szs", ".kcl",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE | FFT_PATCH, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Collision file" },

	// FF_KCL_TXT = 64
	{ FF_KCL_TXT, FF_KCL, FF_KCL_TXT, "KCLTXT", ".obj", ".szs", ".obj",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x4b, 0x43, 0x4c }, // "#KCL"
		0, MinusString, MinusString, "Wavefront OBJ by WSZST" },

	// FF_WAV_OBJ = 65
	{ FF_WAV_OBJ, FF_KCL, FF_WAV_OBJ, "WAVOBJ", ".obj", ".szs", ".obj",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Wavefront OBJ" },

	// FF_SKP_OBJ = 66
	{ FF_SKP_OBJ, FF_KCL, FF_SKP_OBJ, "SKPOBJ", ".obj", ".szs", ".obj",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Wavefront OBJ by Sketchup" },

	// FF_KMP = 67
	{ FF_KMP, FF_KMP, FF_KMP_TXT, "KMP", ".kmp", ".szs", ".kmp",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4, { 0x52, 0x4b, 0x4d, 0x44 }, // "RKMD"
		0, MinusString, MinusString, "Track information file" },

	// FF_KMP_TXT = 68
	{ FF_KMP_TXT, FF_KMP, FF_KMP_TXT, "KMPTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x4b, 0x4d, 0x50 }, // "#KMP"
		0, MinusString, MinusString, "Text version of KMP" },

	// FF_ITEMSLT = 69
	{ FF_ITEMSLT, FF_ITEMSLT, FF_ITEMSLT_TXT, "ITEMSLT", ".bin", ".szs", ".slt", FFT_VALID, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_ITEMSLT_TXT = 70
	{ FF_ITEMSLT_TXT, FF_ITEMSLT, FF_ITEMSLT_TXT, "ITEMSLTTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_KMG = 71
	{ FF_KMG, FF_KMG, FF_KMG_TXT, "KMG", ".kmg", ".szs", ".kmg",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 4, { 0x52, 0x4b, 0x4d, 0x47 }, // "RKMG"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_KMG_TXT = 72
	{ FF_KMG_TXT, FF_KMG, FF_KMG_TXT, "KMGTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x4b, 0x4d, 0x47 }, // "#KMG"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_KRM = 73
	{ FF_KRM, FF_KRM, FF_KRM_TXT, "KRM", ".krm", ".szs", ".krm", FFT_VALID, 4,
		{ 0x52, 0x4b, 0x52, 0x4d }, // "RKRM"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_KRM_TXT = 74
	{ FF_KRM_TXT, FF_KRM, FF_KRM_TXT, "KRMTXT", ".txt", ".szs", ".txt", FFT_VALID | FFT_TEXT, 4,
		{ 0x23, 0x4b, 0x52, 0x4d }, // "#KRM"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_KRT = 75
	{ FF_KRT, FF_KRT, FF_KRT_TXT, "KRT", ".krt", ".szs", ".krt", FFT_VALID, 4,
		{ 0x52, 0x4b, 0x47, 0x54 }, // "RKGT"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_KRT_TXT = 76
	{ FF_KRT_TXT, FF_KRT, FF_KRT_TXT, "KRTTXT", ".txt", ".szs", ".txt", FFT_VALID | FFT_TEXT, 4,
		{ 0x23, 0x4b, 0x52, 0x54 }, // "#KRT"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_OBJFLOW = 77
	{ FF_OBJFLOW, FF_OBJFLOW, FF_OBJFLOW_TXT, "OBFLOW", ".bin", ".szs", ".bin",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_OBJFLOW_TXT = 78
	{ FF_OBJFLOW_TXT, FF_OBJFLOW, FF_OBJFLOW_TXT, "OF-TXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x4f, 0x42, 0x4a, 0x46, 0x4c, 0x4f, 0x57 }, // "#OBJFLOW"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_GH_ITEM = 79
	{ FF_GH_ITEM, FF_GH_ITEM, FF_GH_ITEM_TXT, "GHITEM", ".bin", ".szs", ".bin",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_GH_ITEM_TXT = 80
	{ FF_GH_ITEM_TXT, FF_GH_ITEM, FF_GH_ITEM_TXT, "GI-TXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x47, 0x48, 0x2d, 0x49, 0x54, 0x45, 0x4d }, // "#GH-ITEM"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_GH_IOBJ = 81
	{ FF_GH_IOBJ, FF_GH_IOBJ, FF_GH_IOBJ_TXT, "GHIOBJ", ".bin", ".szs", ".bin",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_GH_IOBJ_TXT = 82
	{ FF_GH_IOBJ_TXT, FF_GH_IOBJ, FF_GH_IOBJ_TXT, "GIOTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x47, 0x48, 0x2d, 0x49, 0x4f, 0x42, 0x4a }, // "#GH-IOBJ"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_GH_KART = 83
	{ FF_GH_KART, FF_GH_KART, FF_GH_KART_TXT, "GHKART", ".bin", ".szs", ".bin",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_GH_KART_TXT = 84
	{ FF_GH_KART_TXT, FF_GH_KART, FF_GH_KART_TXT, "GK-TXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x47, 0x48, 0x2d, 0x4b, 0x41, 0x52, 0x54 }, // "#GH-KART"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_GH_KOBJ = 85
	{ FF_GH_KOBJ, FF_GH_KOBJ, FF_GH_KOBJ_TXT, "GHKOBJ", ".bin", ".szs", ".bin",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_GH_KOBJ_TXT = 86
	{ FF_GH_KOBJ_TXT, FF_GH_KOBJ, FF_GH_KOBJ_TXT, "GKOTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x47, 0x48, 0x2d, 0x4b, 0x4f, 0x42, 0x4a }, // "#GH-KOBJ"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_DRIVER = 87
	{ FF_DRIVER, 0, 0, "DRV", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_VEHICLE = 88
	{ FF_VEHICLE, 0, 0, "VEH", ".bin", ".szs", ".bin", FFT_VALID, 0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_BRASD = 89
	{ FF_BRASD, 0, 0, "BRASD", ".brasd", ".szs", ".brasd", FFT_VALID, 4,
		{ 0x52, 0x41, 0x53, 0x44 }, // "RASD"
		0, MinusString, MinusString, filetype_info_unknown },

	// FF_RKG = 90
	{ FF_RKG, 0, 0, "RKG", ".rkg", ".szs", ".rkg", FFT_VALID, 4,
		{ 0x52, 0x4b, 0x47, 0x44 }, // "RKGD"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_RKCO = 91
	{ FF_RKCO, 0, 0, "RKCO", ".rkco", ".szs", ".rkco", FFT_VALID, 4,
		{ 0x52, 0x4b, 0x43, 0x4f }, // "RKCO"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_STATICR = 92
	{ FF_STATICR, 0, 0, "STATICR", ".rel", ".szs", ".rel", FFT_VALID | FFT_PATCH, 0,
		{ 0 }, // no magic
		0, MinusString, MinusString, "A 'StaticR.rel' file" },

	// FF_DOL = 93
	{ FF_DOL, 0, 0, "DOL", ".dol", ".szs", ".dol", FFT_VALID | FFT_EXTRACT | FFT_PATCH, 0,
		{ 0 }, // no magic
		0, MinusString, MinusString, "An executable DOL file" },

	// FF_GCT = 94
	{ FF_GCT, 0, 0, "GCT", ".gct", ".szs", ".gct", FFT_VALID, 4,
		{ 0x00, 0xd0, 0xc0, 0xde }, // "\000ÐÀÞ"
		0, MinusString, MinusString, "Gecko Cheat Code, binary" },

	// FF_GCT_TXT = 95
	{ FF_GCT_TXT, 0, 0, "GCT-TXT", ".gct", ".szs", ".gct", FFT_VALID, 4,
		{ 0x23, 0x47, 0x43, 0x54 }, // "#GCT"
		0, MinusString, MinusString, "Gecko Cheat Code, text" },

	// FF_GCH = 96
	{ FF_GCH, 0, 0, "GCH", ".gch", ".szs", ".gch", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Gecko Cheat Handler + coded" },

	// FF_WCH = 97
	{ FF_WCH, 0, 0, "WCH", ".wch", ".szs", ".wch", FFT_VALID, 0, { 0 }, // no magic
		0, MinusString, MinusString, "Wiimms Cheat Handler + codes" },

	// FF_WPF = 98
	{ FF_WPF, 0, 0, "WPF", ".wpf", ".szs", ".wpf", FFT_VALID, 4,
		{ 0x57, 0x50, 0x46, 0x01 }, // "WPF\001"
		0, MinusString, MinusString, "Wiimms Patch File" },

	// FF_XPF = 99
	{ FF_XPF, 0, 0, "XPF", ".xpf", ".szs", ".xpf", FFT_VALID, 4,
		{ 0x58, 0x50, 0x46, 0x01 }, // "XPF\001"
		0, MinusString, MinusString, "Extended Patch File" },

	// FF_DISTRIB = 100
	{ FF_DISTRIB, 0, 0, "DISTRIB", ".txt", ".szs", ".txt", FFT_VALID | FFT_TEXT, 8,
		{ 0x23, 0x44, 0x49, 0x53, 0x54, 0x52, 0x49, 0x42 }, // "#DISTRIB"
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_PNG = 101
	{ FF_PNG, 0, 0, "PNG", ".png", ".png", ".png",
		FFT_VALID | FFT_GRAPHIC | FFT_EXTERNAL | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a }, // "PNG\r\n\032\n"
		0, MinusString, MinusString, "A public image format" },

	// FF_PORTDB = 102
	{ FF_PORTDB, 0, 0, "PORTDB", ".bin", ".szs", ".bin", FFT_VALID | FFT_EXTERNAL, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_TXT = 103
	{ FF_TXT, 0, 0, "TXT", ".txt", ".szs", ".txt", FFT_VALID | FFT_TEXT | FFT_EXTERNAL, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_SCRIPT = 104
	{ FF_SCRIPT, 0, 0, "SCRIPT", ".script", ".szs", ".script", FFT_VALID | FFT_TEXT | FFT_EXTERNAL,
		0, { 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_JSON = 105
	{ FF_JSON, 0, 0, "JSON", ".json", ".szs", ".json", FFT_VALID | FFT_TEXT | FFT_EXTERNAL, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_SH = 106
	{ FF_SH, 0, 0, "SH", ".sh", ".szs", ".sh", FFT_VALID | FFT_TEXT | FFT_EXTERNAL, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_BASH = 107
	{ FF_BASH, 0, 0, "BASH", ".sh", ".szs", ".sh", FFT_VALID | FFT_TEXT | FFT_EXTERNAL, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_PHP = 108
	{ FF_PHP, 0, 0, "PHP", ".php", ".szs", ".php", FFT_VALID | FFT_TEXT | FFT_EXTERNAL, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_MAKEDOC = 109
	{ FF_MAKEDOC, 0, 0, "MAKEDOC", ".md", ".szs", ".md", FFT_VALID | FFT_TEXT | FFT_EXTERNAL, 0,
		{ 0 }, // no magic
		0, filetype_info_unknown, filetype_info_unknown, EmptyString },

	// FF_DIRECTORY = 110
	{ FF_DIRECTORY, 0, 0, "DIR", ".d", ".d", ".d", FFT_EXTERNAL, 0, { 0 }, // no magic
		0, "transparent", "transparent", "Directory of filesystem" },

	// FF_PLT0 = 111 (NDS/Wii palette file)
	{ FF_PLT0, 0, 0, "PLT0", ".plt0", ".szs", ".plt0", FFT_VALID | FFT_GRAPHIC, 4,
		{ 0x50, 0x4c, 0x54, 0x30 }, // "PLT0"
		0, MinusString, MinusString, "NDS/Wii palette file" },

	// FF_BRLYT = 112 (Wii layout binary)
	{ FF_BRLYT, 0, 0, "BRLYT", ".brlyt", ".szs", ".brlyt", FFT_VALID | FFT_DECODE, 4,
		{ 0x52, 0x4c, 0x59, 0x54 }, // "RLYT"
		0, MinusString, MinusString, "Wii layout binary (BRLYT)" },

	// FF_BRLAN = 113 (Wii layout animation)
	{ FF_BRLAN, 0, 0, "BRLAN", ".brlan", ".szs", ".brlan", FFT_VALID | FFT_DECODE, 4,
		{ 0x52, 0x4c, 0x41, 0x4e }, // "RLAN"
		0, MinusString, MinusString, "Wii layout animation (BRLAN)" },

	// FF_BFLYT = 114 (Wii U/Switch layout binary)
	{ FF_BFLYT, 0, FF_BFLYT_TXT, "BFLYT", ".bflyt", ".szs", ".bflyt", FFT_VALID | FFT_DECODE, 4,
		{ 0x46, 0x4c, 0x59, 0x54 }, // "FLYT"
		0, MinusString, MinusString, "Wii U/Switch layout binary (BFLYT)" },

	// FF_BCLYT = 115 (3DS layout binary)
	{ FF_BCLYT, 0, FF_BCLYT_TXT, "BCLYT", ".bclyt", ".szs", ".bclyt", FFT_VALID | FFT_DECODE, 4,
		{ 0x43, 0x4c, 0x59, 0x54 }, // "CLYT"
		0, MinusString, MinusString, "3DS layout binary (BCLYT)" },

	// FF_BFLYT_TXT = 116 (Wii U/Switch layout text)
	{ FF_BFLYT_TXT, FF_BFLYT, FF_BFLYT_TXT, "BFLYTTXT", ".tflyt", ".tflyt", ".tflyt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x46, 0x4c, 0x59 }, // "#FLY"
		0, MinusString, MinusString, "Text version of BFLYT" },

	// FF_BCLYT_TXT = 117 (3DS layout text)
	{ FF_BCLYT_TXT, FF_BCLYT, FF_BCLYT_TXT, "BCLYTTXT", ".ctlyt", ".ctlyt", ".ctlyt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x43, 0x4c, 0x59 }, // "#CLY"
		0, MinusString, MinusString, "Text version of BCLYT" },

	// FF_BNTX = 118 (Switch texture container -- detection only, not decoded)
	{ FF_BNTX, 0, 0, "BNTX", ".bntx", ".szs", ".bntx", FFT_VALID, 4,
		{ 0x42, 0x4e, 0x54, 0x58 }, // "BNTX"
		0, MinusString, MinusString, "Switch texture container (detected, not decoded)" },

	// FF_GFA = 119 (Good-Feel archive)
	{ FF_GFA, 0, 0, "GFA", ".gfa", ".gfa", ".gfa", FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT, 4,
		{ 0x47, 0x46, 0x41, 0x43 }, // "GFAC"
		0, MinusString, MinusString, "Good-Feel archive (GFAC)" },

	// FF_BCH = 120 (3DS CTR H3D container)
	{ FF_BCH, 0, 0, "BCH", ".bch", ".bch", ".bch", FFT_VALID, 4,
		{ 0x42, 0x43, 0x48, 0x00 }, // "BCH\0"
		0, MinusString, MinusString, "3DS CTR H3D container (BCH)" },

	// FF_BCRES = 121 (3DS CGFX/BCRES container)
	{ FF_BCRES, 0, 0, "BCRES", ".bcres", ".bcres", ".bcres", FFT_VALID, 4,
		{ 0x43, 0x47, 0x46, 0x58 }, // "CGFX"
		0, MinusString, MinusString, "3DS CGFX/BCRES container" },

	// FF_AJPG = 122 (Nintendo AJPG/ODH image)
	{ FF_AJPG, 0, 0, "AJPG", ".ajpg", ".ajpg", ".ajpg",
		FFT_VALID | FFT_GRAPHIC | FFT_DECODE | FFT_ENCODE, 4, { 0x41, 0x4A, 0x50, 0x47 }, // "AJPG"
		0, MinusString, MinusString, "Nintendo AJPG/ODH image" },

	// FF_RST = 123 (Monster Games archive 0TSR)
	{ FF_RST, 0, 0, "RST", ".car", ".car", ".car",
		FFT_VALID | FFT_ARCHIVE | FFT_CREATE | FFT_EXTRACT, 4, { 0x30, 0x54, 0x53, 0x52 }, // "0TSR"
		0, MinusString, MinusString, "Monster Games archive (0TSR)" },

	// FF_RST_TOC = 124 (Monster Games TOC)
	{ FF_RST_TOC, 0, 0, "RST-TOC", ".toc", ".toc", ".toc", FFT_VALID | FFT_ARCHIVE, 8,
		{ 0x30, 0x53, 0x45, 0x52, 0x43, 0x4f, 0x54, 0x45 }, // "0SERCOTE"
		0, MinusString, MinusString, "Monster Games TOC (0SERCOTE)" },

	// FF_THP = 125 (Nintendo GameCube/Wii THP video)
	{ FF_THP, 0, 0, "THP", ".thp", ".thp", ".thp",
		FFT_VALID | FFT_GRAPHIC | FFT_DECODE | FFT_EXTRACT, 4,
		{ 0x54, 0x48, 0x50, 0x00 }, // "THP\0"
		0, MinusString, MinusString, "Nintendo GameCube/Wii THP video" },

	// FF_MSBT = 126 (Message Studio Binary Text)
	{ FF_MSBT, 0, FF_MSBT_TXT, "MSBT", ".msbt", ".msbt", ".msbt",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x4d, 0x73, 0x67, 0x53, 0x74, 0x64, 0x42, 0x6e }, // "MsgStdBn"
		0, MinusString, MinusString, "Nintendo Message Studio Binary Text (MSBT)" },

	// FF_MSBP = 127 (Message Studio Binary Project)
	{ FF_MSBP, 0, FF_MSBP_TXT, "MSBP", ".msbp", ".msbp", ".msbp",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x4d, 0x73, 0x67, 0x50, 0x72, 0x6a, 0x42, 0x6e }, // "MsgPrjBn"
		0, MinusString, MinusString, "Nintendo Message Studio Binary Project (MSBP)" },

	// FF_MSBF = 128 (Message Studio Binary Flow)
	{ FF_MSBF, 0, FF_MSBF_TXT, "MSBF", ".msbf", ".msbf", ".msbf",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 8,
		{ 0x4d, 0x73, 0x67, 0x46, 0x6c, 0x77, 0x42, 0x6e }, // "MsgFlwBn"
		0, MinusString, MinusString, "Nintendo Message Studio Binary Flowchart (MSBF)" },

	// FF_MSBT_TXT = 129 (MSBT text representation)
	{ FF_MSBT_TXT, FF_MSBT, FF_MSBT_TXT, "MSBTTXT", ".tmsbt", ".tmsbt", ".tmsbt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x20, 0x4d, 0x53, 0x42, 0x54, 0x3a, 0x20 }, // "# MSBT: "
		0, MinusString, MinusString, "MSBT text representation" },

	// FF_MSBP_TXT = 130 (MSBP text representation)
	{ FF_MSBP_TXT, FF_MSBP, FF_MSBP_TXT, "MSBPTXT", ".tmsbp", ".tmsbp", ".tmsbp",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x20, 0x4d, 0x53, 0x42, 0x50, 0x3a, 0x20 }, // "# MSBP: "
		0, MinusString, MinusString, "MSBP text representation" },

	// FF_MSBF_TXT = 131 (MSBF text representation)
	{ FF_MSBF_TXT, FF_MSBF, FF_MSBF_TXT, "MSBFTXT", ".tmsbf", ".tmsbf", ".tmsbf",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 8,
		{ 0x23, 0x20, 0x4d, 0x53, 0x42, 0x46, 0x3a, 0x20 }, // "# MSBF: "
		0, MinusString, MinusString, "MSBF text representation" },

	// FF_SDAT = 132 (Nintendo DS Sound Archive)
	{ FF_SDAT, 0, 0, "SDAT", ".sdat", ".sdat", ".sdat", FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4,
		{ 0x53, 0x44, 0x41, 0x54 }, // "SDAT"
		0, MinusString, MinusString, "Nintendo DS Sound Archive (SDAT)" },

	// FF_BCSAR = 133 (Nintendo 3DS Sound Archive)
	{ FF_BCSAR, 0, 0, "BCSAR", ".bcsar", ".bcsar", ".bcsar",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4, { 0x43, 0x53, 0x41, 0x52 }, // "CSAR"
		0, MinusString, MinusString, "Nintendo 3DS Sound Archive (BCSAR)" },

	// FF_BFSAR = 134 (Nintendo Wii U / Switch Sound Archive)
	{ FF_BFSAR, 0, 0, "BFSAR", ".bfsar", ".bfsar", ".bfsar",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4, { 0x46, 0x53, 0x41, 0x52 }, // "FSAR"
		0, MinusString, MinusString, "Nintendo Wii U / Switch Sound Archive (BFSAR)" },

	// FF_BCWAR = 135 (Nintendo 3DS Sound Wave Archive)
	{ FF_BCWAR, 0, 0, "BCWAR", ".bcwar", ".bcwar", ".bcwar",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4, { 0x43, 0x57, 0x41, 0x52 }, // "CWAR"
		0, MinusString, MinusString, "Nintendo 3DS Sound Wave Archive (BCWAR)" },

	// FF_BFWAR = 136 (Nintendo Wii U / Switch Sound Wave Archive)
	{ FF_BFWAR, 0, 0, "BFWAR", ".bfwar", ".bfwar", ".bfwar",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4, { 0x46, 0x57, 0x41, 0x52 }, // "FWAR"
		0, MinusString, MinusString, "Nintendo Wii U / Switch Sound Wave Archive (BFWAR)" },

	// FF_BCGRP = 137 (Nintendo 3DS Sound Group)
	{ FF_BCGRP, 0, 0, "BCGRP", ".bcgrp", ".bcgrp", ".bcgrp",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4, { 0x43, 0x47, 0x52, 0x50 }, // "CGRP"
		0, MinusString, MinusString, "Nintendo 3DS Sound Group (BCGRP)" },

	// FF_BFGRP = 138 (Nintendo Wii U / Switch Sound Group)
	{ FF_BFGRP, 0, 0, "BFGRP", ".bfgrp", ".bfgrp", ".bfgrp",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4, { 0x46, 0x47, 0x52, 0x50 }, // "FGRP"
		0, MinusString, MinusString, "Nintendo Wii U / Switch Sound Group (BFGRP)" },

	// FF_GTX = 139 (Wii U GX2 texture/shader container -- detection + decode)
	{ FF_GTX, 0, 0, "GTX", ".gtx", ".gtx", ".gtx", FFT_VALID, 4,
		{ 0x47, 0x66, 0x78, 0x32 }, // "Gfx2"
		0, MinusString, MinusString, "Wii U GX2 texture/shader container (Gfx2/GTX/GSH)" },

	// FF_RSEQ = 140 (Nintendo Wii Revolution Sequence)
	{ FF_RSEQ, FF_RSEQ, FF_SEQ_TXT, "RSEQ", ".rseq", ".rseq", ".rseq",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 4, { 0x52, 0x53, 0x45, 0x51 }, // "RSEQ"
		0, MinusString, MinusString, "Nintendo Wii Revolution Sequence (.rseq / .brseq)" },

	// FF_CSEQ = 141 (Nintendo 3DS CTR Sequence)
	{ FF_CSEQ, FF_CSEQ, FF_SEQ_TXT, "CSEQ", ".cseq", ".cseq", ".cseq",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 4, { 0x43, 0x53, 0x45, 0x51 }, // "CSEQ"
		0, MinusString, MinusString, "Nintendo 3DS CTR Sequence (.cseq / .bcseq)" },

	// FF_FSEQ = 142 (Nintendo Wii U / Switch Format Sequence)
	{ FF_FSEQ, FF_FSEQ, FF_SEQ_TXT, "FSEQ", ".fseq", ".fseq", ".fseq",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 4, { 0x46, 0x53, 0x45, 0x51 }, // "FSEQ"
		0, MinusString, MinusString, "Nintendo Wii U / Switch Format Sequence (.fseq / .bfseq)" },

	// FF_SSEQ = 143 (Nintendo DS Nitro Sequence)
	{ FF_SSEQ, FF_SSEQ, FF_SEQ_TXT, "SSEQ", ".sseq", ".sseq", ".sseq",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 4, { 0x53, 0x53, 0x45, 0x51 }, // "SSEQ"
		0, MinusString, MinusString, "Nintendo DS Nitro Sequence (.sseq)" },

	// FF_SEQ_TXT = 144 (Nintendo Sequence MML text representation)
	{ FF_SEQ_TXT, FF_RSEQ, FF_SEQ_TXT, "MML", ".txt", ".txt", ".txt", FFT_VALID | FFT_ENCODE, 0,
		{ 0 }, 0, MinusString, MinusString, "Nintendo Sequence MML text representation" },

	// FF_MIDI = 145 (Standard MIDI File)
	{ FF_MIDI, FF_MIDI, FF_SEQ_TXT, "MIDI", ".mid", ".mid", ".mid",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 4, { 0x4D, 0x54, 0x68, 0x64 }, // "MThd"
		0, MinusString, MinusString, "Standard MIDI File (.mid)" },

	// FF_FZIP = 146 (Game & Wario FZIP compression)
	{ FF_FZIP, 0, 0, "FZIP", ".fzip", ".fzip", ".fzip", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 4,
		{ 0x46, 0x5a, 0x49, 0x50 }, // "FZIP"
		0, MinusString, MinusString, "Game & Wario FZIP compression" },

	// FF_GVR = 147 (Sega GameCube/Wii texture)
	{ FF_GVR, 0, 0, "GVR", ".gvr", ".gvr", ".gvr", FFT_VALID | FFT_GRAPHIC | FFT_DECODE, 4,
		{ 0x47, 0x43, 0x49, 0x58 }, // "GCIX"
		0, MinusString, MinusString, "Sega GameCube/Wii texture (GCIX/GVRT)" },

	// FF_SMDH = 148 (3DS icon/title metadata)
	{ FF_SMDH, 0, 0, "SMDH", ".smdh", ".smdh", ".smdh", FFT_VALID | FFT_GRAPHIC | FFT_DECODE, 4,
		{ 0x53, 0x4d, 0x44, 0x48 }, // "SMDH"
		0, MinusString, MinusString, "3DS application icon/title metadata (SMDH)" },

	// FF_SARC = 149 (Nintendo SARC archive)
	{ FF_SARC, 0, 0, "SARC", ".sarc", ".szs", ".sarc",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4,
		{ 0x53, 0x41, 0x52, 0x43 }, // "SARC"
		0, MinusString, MinusString, "Nintendo SARC archive (.sarc)" },

	// FF_BFMA = 150 (Nintendo Wii U manual archive)
	{ FF_BFMA, 0, 0, "BFMA", ".bfma", ".bfma", ".bfma",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4,
		{ 0x53, 0x41, 0x52, 0x43 }, // "SARC"
		0, MinusString, MinusString, "Nintendo Wii U manual archive (.bfma)" },

	// FF_ZLIB = 151 (Zlib deflate compression)
	{ FF_ZLIB, 0, 0, "ZLIB", ".zlib", ".zlib", ".zlib", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 0,
		{ 0 },
		0, MinusString, MinusString, "Zlib deflate compression (.zlib)" },

	// FF_CHR_TXT = 152 (text version of CHR)
	{ FF_CHR_TXT, FF_CHR, FF_CHR_TXT, "CHRTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x43, 0x48, 0x52 }, // "#CHR"
		0, MinusString, MinusString, "Text version of CHR" },

	// FF_SRT_TXT = 153 (text version of SRT)
	{ FF_SRT_TXT, FF_SRT, FF_SRT_TXT, "SRTTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x53, 0x52, 0x54 }, // "#SRT"
		0, MinusString, MinusString, "Text version of SRT" },

	// FF_VIS = 154 (NW4R node visibility animation)
	{ FF_VIS, FF_VIS, FF_VIS_TXT, "VIS", ".vis", ".szs", ".vis0",
		FFT_VALID | FFT_BRSUB | FFT_BRSUB2 | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x56, 0x49, 0x53, 0x30 }, // "VIS0"
		"AnmVis(NW4R)", "4,(*)", "3,4", "Node visibility animations" },

	// FF_VIS_TXT = 155 (text version of VIS)
	{ FF_VIS_TXT, FF_VIS, FF_VIS_TXT, "VISTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x56, 0x49, 0x53 }, // "#VIS"
		0, MinusString, MinusString, "Text version of VIS" },

	// FF_CLR_TXT = 156 (text version of CLR)
	{ FF_CLR_TXT, FF_CLR, FF_CLR_TXT, "CLRTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x43, 0x4c, 0x52 }, // "#CLR"
		0, MinusString, MinusString, "Text version of CLR" },

	// FF_SHP_TXT = 157 (text version of SHP)
	{ FF_SHP_TXT, FF_SHP, FF_SHP_TXT, "SHPTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x53, 0x48, 0x50 }, // "#SHP"
		0, MinusString, MinusString, "Text version of SHP" },

	// FF_ZSTD = 158 (Zstandard compression)
	{ FF_ZSTD, 0, 0, "ZSTD", ".zs", ".zs", ".zst", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 4,
		{ 0x28, 0xb5, 0x2f, 0xfd }, // Zstandard magic 0xFD2FB528
		0, MinusString, MinusString, "Zstandard compression (.zs / .zst / .zstd)" },

	// FF_NSBTX = 159 (Nitro 3D texture archive)
	{ FF_NSBTX, FF_NSBTX, 0, "NSBTX", ".nsbtx", ".szs", ".btx0",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x42, 0x54, 0x58, 0x30 }, // "BTX0"
		"NitroTexArc", "4,(*)", "1", "Nintendo DS 3D texture archive" },

	// FF_NFTR = 160 (Nitro font resource)
	{ FF_NFTR, FF_NFTR, 0, "NFTR", ".nftr", ".szs", ".fntr",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x52, 0x54, 0x4e, 0x46 }, // "RTNF"
		"NitroFont", "4,(*)", "1", "Nintendo DS font resource" },

	// FF_BNFR = 161 (Binary Nitro font resource)
	{ FF_BNFR, FF_BNFR, 0, "BNFR", ".bnfr", ".szs", ".rnfb",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x52, 0x4e, 0x46, 0x42 }, // "RNFB"
		"BinaryNitroFont", "4,(*)", "1", "Binary Nitro font resource" },

	// FF_BNLL = 162 (Nitro binary layout)
	{ FF_BNLL, FF_BNLL, 0, "BNLL", ".bnll", ".szs", ".llnb",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x4c, 0x4c, 0x4e, 0x42 }, // "LLNB"
		"NitroLayout", "4,(*)", "1", "Nintendo DS binary layout" },

	// FF_BNCL = 163 (Nitro binary cell layout)
	{ FF_BNCL, FF_BNCL, 0, "BNCL", ".bncl", ".szs", ".lcnb",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x4c, 0x43, 0x4e, 0x42 }, // "LCNB"
		"NitroCellLayout", "4,(*)", "1", "Nintendo DS binary cell layout" },

	// FF_BNBL = 164 (Nitro binary block layout)
	{ FF_BNBL, FF_BNBL, 0, "BNBL", ".bnbl", ".szs", ".lbnb",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x4c, 0x42, 0x4e, 0x42 }, // "LBNB"
		"NitroBlockLayout", "4,(*)", "1", "Nintendo DS binary block layout" },

	// FF_VLX = 165 (Pac-Man World DS compression)
	{ FF_VLX, 0, 0, "VLX", ".vlx", ".szs", ".vlx", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 0,
		{ 0 }, 0, MinusString, MinusString, "Namco VLX compression" },

	// FF_PUCRUNCH = 166 (Griptonite Games PuCrunch compression)
	{ FF_PUCRUNCH, 0, 0, "PUCRUNCH", ".pc", ".szs", ".pc", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 1,
		{ 0x60 }, 0, MinusString, MinusString, "Griptonite PuCrunch compression" },

	// FF_LZX = 167 (Nintendo DS extended LZ11)
	{ FF_LZX, 0, 0, "LZX", ".lzx", ".szs", ".lzx", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 1,
		{ 0x19 }, 0, MinusString, MinusString, "Nintendo DS extended LZ11 (0x19)" },

	// FF_DIFF = 168 (Nintendo differential filter)
	{ FF_DIFF, 0, 0, "DIFF", ".diff", ".szs", ".diff", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 1,
		{ 0x80 }, 0, MinusString, MinusString, "Nintendo differential filter (0x80/0x81)" },

	// FF_SCN_TXT = 169 (text version of SCN)
	{ FF_SCN_TXT, FF_SCN, FF_SCN_TXT, "SCNTXT", ".txt", ".szs", ".txt",
		FFT_VALID | FFT_TEXT | FFT_DECODE | FFT_ENCODE | FFT_PARSER, 4,
		{ 0x23, 0x53, 0x43, 0x4e }, // "#SCN"
		0, MinusString, MinusString, "Text version of SCN" },

	// FF_LZOVL = 170 (Nintendo DS Overlay LZSS compression)
	{ FF_LZOVL, 0, 0, "LZOVL", ".ovl", ".szs", ".ovl", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 0,
		{ 0 }, 0, MinusString, MinusString, "Nintendo DS Overlay LZSS compression" },

	// FF_ALAR = 171 (Jump Ultimate Stars archive)
	{ FF_ALAR, FF_ALAR, 0, "ALAR", ".alar", ".szs", ".alar",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE, 4,
		{ 0x41, 0x4c, 0x41, 0x52 }, // "ALAR"
		0, "4,(*)", "1", "Jump Ultimate Stars archive" },

	// FF_DARC = 172 (Level-5 / Layton archive)
	{ FF_DARC, FF_DARC, 0, "DARC", ".darc", ".szs", ".darc",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE, 4,
		{ 0x44, 0x41, 0x52, 0x43 }, // "DARC"
		0, "4,(*)", "1", "Level-5 / Layton archive" },

	// FF_SADL = 173 (Level-5 / Layton SADL sound archive)
	{ FF_SADL, FF_SADL, 0, "SADL", ".sad", ".szs", ".sadl",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x53, 0x41, 0x44, 0x4c }, // "SADL"
		0, MinusString, MinusString, "Level-5 / Layton SADL sound archive" },

	// FF_NCER = 174 (Nitro cell resource)
	{ FF_NCER, FF_NCER, 0, "NCER", ".ncer", ".szs", ".recn",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x52, 0x45, 0x43, 0x4e }, // "RECN"
		0, MinusString, MinusString, "Nintendo DS Cell resource" },

	// FF_NANR = 175 (Nitro animation resource)
	{ FF_NANR, FF_NANR, 0, "NANR", ".nanr", ".szs", ".rnan",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x52, 0x4e, 0x41, 0x4e }, // "RNAN"
		0, MinusString, MinusString, "Nintendo DS Animation resource" },

	// FF_NUT = 176 (Namco Universal Texture)
	{ FF_NUT, FF_NUT, 0, "NUT", ".nut", ".szs", ".nut",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE, 4,
		{ 0x4e, 0x54, 0x50, 0x33 }, // "NTP3"
		0, MinusString, MinusString, "Namco Universal Texture (NTP3/NTWU)" },

	// FF_NUD = 177 (Namco Universal Data model)
	{ FF_NUD, FF_NUD, 0, "NUD", ".nud", ".szs", ".nud",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x4e, 0x44, 0x50, 0x33 }, // "NDP3"
		0, MinusString, MinusString, "Namco Universal Data 3D model (NDP3/NDWU)" },

	// FF_DTLS = 178 (Smash 4 data/lookup archive)
	{ FF_DTLS, FF_DTLS, 0, "DTLS", ".ls", ".szs", ".ls",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_CREATE, 4,
		{ 0x4c, 0x53, 0x00, 0x00 }, // "LS\0\0"
		0, MinusString, MinusString, "Smash 4 DTLS archive (dt00/ls00)" },

	// FF_NUMSHB = 179 (Smash Ultimate SSBH Mesh)
	{ FF_NUMSHB, FF_NUMSHB, 0, "NUMSHB", ".numshb", ".szs", ".numshb",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x53, 0x53, 0x42, 0x48 }, // "SSBH"
		0, MinusString, MinusString, "Smash Ultimate SSBH Mesh (NUMSHB)" },

	// FF_UE4_PAK = 180 (Unreal Engine 4 archive)
	{ FF_UE4_PAK, 0, 0, "PAK", ".pak", ".pak", ".pak",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 0,
		{ 0 },
		0, MinusString, MinusString, "Unreal Engine 4 archive (Mario & Luigi: Brothership .pak)" },

	// FF_SMASH_ARC = 182 (Smash Ultimate data.arc)
	{ FF_SMASH_ARC, 0, 0, "ARC", ".arc", ".arc", ".arc",
		FFT_VALID | FFT_ARCHIVE | FFT_EXTRACT | FFT_CREATE, 4,
		{ 0x00, 0xef, 0xcd, 0xab }, // 0xABCDEF00
		0, MinusString, MinusString, "Super Smash Bros. Ultimate data.arc archive" },

	// FF_PRC = 183 (Smash Parameter binary)
	{ FF_PRC, 0, 0, "PRC", ".prc", ".prc", ".prc",
		FFT_VALID | FFT_DECODE | FFT_EXTRACT, 4,
		{ 0x70, 0x61, 0x72, 0x61 }, // "para"
		0, MinusString, MinusString, "Smash Parameter binary (.prc / parambinary)" },

	// FF_CNUT = 184 (Compiled Squirrel script / messages)
	{ FF_CNUT, 0, 0, "CNUT", ".cnut", ".cnut", ".cnut",
		FFT_VALID | FFT_ARCHIVE | FFT_DECODE | FFT_ENCODE | FFT_EXTRACT | FFT_CREATE, 6,
		{ 0xfa, 0xfa, 0x53, 0x51, 0x49, 0x52 }, // 0xFAFA + "SQIR"
		0, MinusString, MinusString, "Compiled Squirrel script / messages (Wii Party .cnut / SQIR)" },

	// FF_CMP = 185 (HAL Laboratory LZ11 compressed file)
	{ FF_CMP, 0, 0, "CMP", ".cmp", ".szs", ".cmp", FFT_VALID | FFT_COMPRESS | FFT_TRACK, 1,
		{ 0x11 }, 0, MinusString, MinusString, "HAL Laboratory LZ11 compressed file (.cmp)" },

	// FF_HSF = 185 (Hudson Soft 3D Model)
	{ FF_HSF, FF_HSF, 0, "HSF", ".hsf", ".szs", ".hsf",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x48, 0x53, 0x46, 0x56 }, // "HSFV"
		0, MinusString, MinusString, "Hudson Soft 3D Model (Mario Party 4-8 .hsf)" },

	// FF_HSD = 186 (HAL Laboratory SYS/DAT Data/Model)
	{ FF_HSD, FF_HSD, 0, "HSD", ".dat", ".szs", ".dat",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "HAL Laboratory SYS/DAT Model/Data (Super Smash Bros Melee)" },

	// FF_BNFM = 187 (Nd Cube Wii U 3D Model)
	{ FF_BNFM, FF_BNFM, 0, "BNFM", ".bnfm", ".szs", ".bnfm",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x42, 0x4e, 0x46, 0x4d }, // "BNFM"
		0, MinusString, MinusString, "Nd Cube Wii U 3D Model (Mario Party 10 .bnfm)" },

	// FF_XPCK = 188 (Level-5 3DS/Switch Container Archive)
	{ FF_XPCK, FF_XPCK, 0, "XPCK", ".xc", ".szs", ".xpck",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 0x58, 0x50, 0x43, 0x4b }, // "XPCK"
		0, MinusString, MinusString, "Level-5 3DS/Switch Container Archive (.xc / XPCK)" },

	// FF_XIMG = 189 (Level-5 3DS/Switch Image/Texture)
	{ FF_XIMG, FF_XIMG, 0, "XIMG", ".xi", ".szs", ".xi",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x58, 0x49, 0x4d, 0x32 }, // "XIM2"
		0, MinusString, MinusString, "Level-5 3DS/Switch Image/Texture (.xi / XIMG)" },

	// FF_HGO = 190 (Camelot GameCube/Wii 3D Model)
	{ FF_HGO, FF_HGO, 0, "HGO", ".hgo", ".szs", ".hgo",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x30, 0x4f, 0x47, 0x48 }, // "0OGH"
		0, MinusString, MinusString, "Camelot GameCube/Wii 3D Model (Mario Golf & Tennis GC)" },

	// FF_ZTAB = 191 (Camelot GameCube/Wii Archive Table)
	{ FF_ZTAB, FF_ZTAB, 0, "ZTAB", ".ztab", ".szs", ".ztab",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE, 4,
		{ 0x5a, 0x54, 0x41, 0x42 }, // "ZTAB"
		0, MinusString, MinusString, "Camelot GameCube/Wii Archive Table (.ztab / ZTAB)" },

	// FF_GLG = 192 (Next Level Games 3D Model)
	{ FF_GLG, FF_GLG, 0, "GLG", ".glg", ".szs", ".glg",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Next Level Games 3D Model (Super Mario Strikers .glg)" },

	// FF_MDR = 193 (Dance Dance Revolution Mario Mix Chunk Archive)
	{ FF_MDR, FF_MDR, 0, "MDR", ".mdr", ".szs", ".mdr",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 0,
		{ 0 },
		0, MinusString, MinusString, "Dance Dance Revolution Mario Mix Chunk Archive (.mdr)" },

	// FF_MSH = 194 (Monster Games Collision Mesh)
	{ FF_MSH, FF_MSH, 0, "MSH", ".msh", ".szs", ".msh",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x50, 0x4d, 0x73, 0x68 }, // "PMsh"
		0, MinusString, MinusString, "Monster Games Collision Mesh (Excite Truck/Bots .msh)" },

	// FF_MOD = 195 (Monster Games Display List Model)
	{ FF_MOD, FF_MOD, 0, "MOD", ".mod", ".szs", ".mod",
		FFT_VALID | FFT_CUT | FFT_DECODE | FFT_ENCODE, 4,
		{ 0x4e, 0x44, 0x4c, 0x33 }, // "NDL3"
		0, MinusString, MinusString, "Monster Games Display List Model (Excite Truck/Bots .mod)" },

	// FF_PERS = 196 (Pokemon Stadium N64 Model / Fragment)
	{ FF_PERS, FF_PERS, 0, "PERS", ".pers", ".szs", ".pers",
		FFT_VALID | FFT_CUT | FFT_DECODE, 8,
		{ 0x50, 0x45, 0x52, 0x53, 0x2d, 0x53, 0x5a, 0x50 }, // "PERS-SZP"
		0, MinusString, MinusString, "Pokemon Stadium N64 Model / Fragment (.pers / FRAGMENT)" },

	// FF_PVOL = 195 (Pikmin 1 & 2 Model Container Archive)
	{ FF_PVOL, FF_PVOL, 0, "PVOL", ".pvol", ".szs", ".pvol",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 0,
		{ 0 },
		0, MinusString, MinusString, "Pikmin 1 & 2 Model Container Archive (.pvol)" },

	// FF_STPK = 196 (Jump Super Stars / Jump Ultimate Stars DS Archive)
	{ FF_STPK, FF_STPK, 0, "STPK", ".srd", ".szs", ".stpk",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 0x53, 0x54, 0x50, 0x4b }, // "STPK"
		0, MinusString, MinusString, "Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / STPK)" },

	// FF_G1M = 197 (Koei Tecmo 3D Model)
	{ FF_G1M, FF_G1M, 0, "G1M", ".g1m", ".szs", ".g1m",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x47, 0x31, 0x4d, 0x5f }, // "G1M_"
		0, MinusString, MinusString, "Koei Tecmo 3D Model (Hyrule Warriors / FE Warriors .g1m)" },

	// FF_G1T = 198 (Koei Tecmo Texture Container)
	{ FF_G1T, FF_G1T, 0, "G1T", ".g1t", ".szs", ".g1t",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 0x47, 0x31, 0x54, 0x5f }, // "G1T_"
		0, MinusString, MinusString, "Koei Tecmo Texture Container (Hyrule Warriors / FE Warriors .g1t)" },

	// FF_G4PKM = 199 (Level-5 / Nintendo 3D Model)
	{ FF_G4PKM, FF_G4PKM, 0, "G4PKM", ".g4pkm", ".szs", ".g4pkm",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Level-5 / Nintendo 3D Model (.g4pkm)" },

	// FF_LMD = 200 (Pokemon Masters 3D Model)
	{ FF_LMD, FF_LMD, 0, "LMD", ".lmd", ".szs", ".lmd",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Pokemon Masters 3D Model (.lmd)" },

	// FF_XMSG = 201 (Wii Party Message / Text Archive)
	{ FF_XMSG, FF_XMSG, 0, "XMSG", ".bin", ".bin", ".bin",
		FFT_VALID | FFT_ARCHIVE | FFT_DECODE | FFT_ENCODE | FFT_EXTRACT | FFT_CREATE, 8,
		{ 0x58, 0x4D, 0x53, 0x47, 0x20, 0x10, 0x05, 0x03 }, // "XMSG \x10\x05\x03"
		0, MinusString, MinusString, "Wii Party Message / Text Archive (mess.bin / XMSG)" },

	// FF_NWR_LEVELINFO = 202 (Newer SMBW Level Information)
	{ FF_NWR_LEVELINFO, FF_NWR_LEVELINFO, 0, "NWR-LEVELINFO", ".bin", ".bin", ".bin",
		FFT_VALID | FFT_ARCHIVE | FFT_DECODE | FFT_ENCODE | FFT_EXTRACT | FFT_CREATE, 4,
		{ 'N', 'W', 'R', 'p' },
		0, MinusString, MinusString, "Newer SMBW Level Information (LevelInfo.bin / NWRp)" },

	// FF_NWR_ANIMTILES = 203 (Newer SMBW Animated Tiles)
	{ FF_NWR_ANIMTILES, FF_NWR_ANIMTILES, 0, "NWR-ANIMTILES", ".bin", ".bin", ".bin",
		FFT_VALID | FFT_ARCHIVE | FFT_DECODE | FFT_ENCODE | FFT_EXTRACT | FFT_CREATE, 4,
		{ 'N', 'W', 'R', 'a' },
		0, MinusString, MinusString, "Newer SMBW Animated Tiles (AnimTiles.bin / NWRa)" },

	// FF_NSMBW_CHK = 204 (NSMBW Tileset Collision Attributes)
	{ FF_NSMBW_CHK, FF_NSMBW_CHK, 0, "NSMBW-CHK", ".bin", ".bin", ".bin",
		FFT_VALID | FFT_DECODE | FFT_EXTRACT, 0,
		{ 0 },
		0, MinusString, MinusString, "NSMBW Tileset Collision Attributes (d_bgchk_*.bin)" },

	// FF_KPBIN = 205 (Koopatlas Binary World Map)
	{ FF_KPBIN, FF_KPBIN, 0, "KPBIN", ".kpbin", ".kpbin", ".kpbin",
		FFT_VALID | FFT_ARCHIVE | FFT_DECODE | FFT_ENCODE | FFT_EXTRACT | FFT_CREATE, 4,
		{ 'K', 'P', '_', 'm' },
		0, MinusString, MinusString, "Koopatlas Binary World Map (.kpbin / KP_m)" },

	// FF_KPMAP = 206 (Koopatlas Map Project)
	{ FF_KPMAP, FF_KPMAP, 0, "KPMAP", ".kpmap", ".kpmap", ".kpmap",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Koopatlas Map Project (.kpmap / JSON)" },

	// FF_CHANS = 207 (Nintendo Wii ChannelScript)
	{ FF_CHANS, FF_CHANS, 0, "CHANS", ".cs", ".cs", ".cs",
		FFT_VALID | FFT_ARCHIVE | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'R', 'C', 'H', 'E' },
		0, MinusString, MinusString, "Nintendo Wii ChannelScript (.cs / RCHE)" },

	// FF_RLG = 208 (Next Level Games 3D Model)
	{ FF_RLG, FF_RLG, 0, "RLG", ".rlg", ".szs", ".rlg",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Next Level Games 3D Model (Mario Strikers Charged .rlg)" },

	// FF_GAR = 209 (Grezzo Archive)
	{ FF_GAR, FF_GAR, 0, "GAR", ".zar", ".szs", ".zar",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'Z', 'A', 'R', 0x01 },
		0, MinusString, MinusString, "Grezzo Zelda / Luigi's Mansion Archive (.zar / .gar)" },

	// FF_CTXB = 210 (Grezzo Texture Container)
	{ FF_CTXB, FF_CTXB, 0, "CTXB", ".ctxb", ".szs", ".ctxb",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'c', 't', 'x', 'b' },
		0, MinusString, MinusString, "Grezzo 3DS Texture Container (.ctxb)" },

	// FF_TMPK = 211 (Twilight Princess HD Archive)
	{ FF_TMPK, FF_TMPK, 0, "TMPK", ".pack", ".szs", ".pack",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'T', 'M', 'P', 'K' },
		0, MinusString, MinusString, "Twilight Princess HD Archive (.pack / TMPK)" },

	// FF_NXARC = 212 (Nintendo Switch NX Archive)
	{ FF_NXARC, FF_NXARC, 0, "NXARC", ".nxarc", ".szs", ".nxarc",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'R', 'A', 'X', 'N' },
		0, MinusString, MinusString, "Nintendo Switch NX Archive (.nxarc / RAXN)" },

	// FF_APAK = 213 (Nintendo APAK Archive)
	{ FF_APAK, FF_APAK, 0, "APAK", ".apak", ".szs", ".apak",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'A', 'P', 'A', 'K' },
		0, MinusString, MinusString, "Nintendo APAK Archive (.apak / APAK)" },

	// FF_PKZ = 214 (PlatinumGames Archive)
	{ FF_PKZ, FF_PKZ, 0, "PKZ", ".pkz", ".szs", ".pkz",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'p', 'k', 'z', 0 },
		0, MinusString, MinusString, "PlatinumGames Archive (.pkz / pkz)" },

	// FF_VIBS = 215 (Nintendo Switch Joy-Con Vibration Archive)
	{ FF_VIBS, FF_VIBS, 0, "VIBS", ".vibs", ".szs", ".vibs",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 0,
		{ 0 },
		0, MinusString, MinusString, "Nintendo Switch Joy-Con Vibration Archive (.vibs)" },

	// FF_PG_DAT = 216 (PlatinumGames DAT Archive)
	{ FF_PG_DAT, FF_PG_DAT, 0, "PG-DAT", ".dat", ".szs", ".dat",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'D', 'A', 'T', 0 },
		0, MinusString, MinusString, "PlatinumGames DAT Archive (.dat / DAT)" },

	// FF_WTA = 217 (PlatinumGames WT Archive)
	{ FF_WTA, FF_WTA, 0, "WTA", ".wta", ".szs", ".wta",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'W', 'T', 'A', ' ' },
		0, MinusString, MinusString, "PlatinumGames WT Archive (.wta / WTA )" },

	// FF_GFPAK = 218 (Game Freak Pokemon Archive)
	{ FF_GFPAK, FF_GFPAK, 0, "GFPAK", ".gfpak", ".szs", ".gfpak",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 8,
		{ 'G', 'F', 'L', 'X', 'P', 'A', 'C', 'K' },
		0, MinusString, MinusString, "Game Freak Pokemon Archive (.gfpak / GFLXPACK)" },

	// FF_BARS = 219 (Nintendo Binary Audio Resource Archive)
	{ FF_BARS, FF_BARS, 0, "BARS", ".bars", ".szs", ".bars",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'B', 'A', 'R', 'S' },
		0, MinusString, MinusString, "Nintendo Binary Audio Resource Archive (.bars / BARS)" },

	// FF_NLG_DICT = 220 (Next Level Games Dictionary Archive)
	{ FF_NLG_DICT, FF_NLG_DICT, 0, "NLG-DICT", ".dict", ".szs", ".dict",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 0,
		{ 0 },
		0, MinusString, MinusString, "Next Level Games Dictionary Archive (.dict)" },

	// FF_TXTG = 221 (Next Level Games Texture To Go)
	{ FF_TXTG, FF_TXTG, 0, "TXTG", ".txtg", ".szs", ".txtg",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ '6', 'P', 'K', '0' },
		0, MinusString, MinusString, "Next Level Games Texture To Go (.txtg / 6PK0)" },

	// FF_NLOC = 222 (Next Level Games Localization Text)
	{ FF_NLOC, FF_NLOC, 0, "NLOC", ".nloc", ".txt", ".nloc",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 4,
		{ 'N', 'L', 'O', 'C' },
		0, MinusString, MinusString, "Next Level Games Localization Text (.nloc / NLOC)" },

	// FF_XLNK = 223 (Nintendo Effect Link Binary)
	{ FF_XLNK, FF_XLNK, 0, "XLNK", ".bslnk", ".xml", ".bslnk",
		FFT_VALID | FFT_DECODE | FFT_ENCODE, 4,
		{ 'X', 'L', 'N', 'K' },
		0, MinusString, MinusString, "Nintendo Effect Link Binary (.bslnk / XLNK)" },

	// FF_ROMFS = 224 (Nintendo 3DS Read-Only File System)
	{ FF_ROMFS, FF_ROMFS, 0, "ROMFS", ".romfs", ".szs", ".romfs",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'I', 'V', 'F', 'C' },
		0, MinusString, MinusString, "Nintendo 3DS Read-Only File System (.romfs / IVFC)" },

	// FF_XTX = 225 (Nintendo Switch XTX Texture Container)
	{ FF_XTX, FF_XTX, 0, "XTX", ".xtx", ".szs", ".xtx",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'D', 'F', 'v', 'N' },
		0, MinusString, MinusString, "Nintendo Switch XTX Texture Container (.xtx / DFvN)" },

	// FF_TVOL = 226 (Koei Tecmo / Gust Texture Volume Archive)
	{ FF_TVOL, FF_TVOL, 0, "TVOL", ".tvol", ".szs", ".tvol",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 0,
		{ 0 },
		0, MinusString, MinusString, "Koei Tecmo Texture Volume Archive (.tvol)" },

	// FF_TXE = 227 (Pikmin 1 Texture)
	{ FF_TXE, FF_TXE, 0, "TXE", ".txe", ".png", ".txe",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Pikmin 1 Texture (.txe)" },

	// FF_MKAGPDX_MDL = 228 (Mario Kart Arcade GP DX Model)
	{ FF_MKAGPDX_MDL, FF_MKAGPDX_MDL, 0, "MKAGPDX-MDL", ".bin", ".szs", ".bin",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'B', 'I', 'K', 'E' },
		0, MinusString, MinusString, "Mario Kart Arcade GP DX Model (.bin / BIKE)" },

	// FF_MTXT = 229 (Nintendo Switch MTXT Texture Archive)
	{ FF_MTXT, FF_MTXT, 0, "MTXT", ".mtxt", ".szs", ".mtxt",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'M', 'T', 'X', 'T' },
		0, MinusString, MinusString, "Nintendo Switch MTXT Texture Archive (.mtxt / MTXT)" },

	// FF_SIR0 = 230 (Pokemon Mystery Dungeon Resource Container)
	{ FF_SIR0, FF_SIR0, 0, "SIR0", ".sir0", ".szs", ".sir0",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'S', 'I', 'R', '0' },
		0, MinusString, MinusString, "Pokemon Mystery Dungeon Resource Container (.sir0 / SIR0)" },

	// FF_TEX3DS = 231 (Nintendo 3DS Proprietary Texture)
	{ FF_TEX3DS, FF_TEX3DS, 0, "TEX3DS", ".tex", ".png", ".tex",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Nintendo 3DS Proprietary Texture (.tex)" },

	// FF_PTLG = 232 (Next Level Games Texture Container)
	{ FF_PTLG, FF_PTLG, 0, "PTLG", ".rlt", ".szs", ".rlt",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 4,
		{ 'P', 'T', 'L', 'G' },
		0, MinusString, MinusString, "Next Level Games Texture Container (.glt / .rlt / PTLG)" },

	// FF_BCSTM = 233 (Nintendo 3DS Stream Audio)
	{ FF_BCSTM, FF_BCSTM, 0, "BCSTM", ".bcstm", ".wav", ".bcstm",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'C', 'S', 'T', 'M' },
		0, MinusString, MinusString, "Nintendo 3DS Stream Audio (.bcstm / CSTM)" },

	// FF_BFSTM = 234 (Nintendo Wii U / Switch Stream Audio)
	{ FF_BFSTM, FF_BFSTM, 0, "BFSTM", ".bfstm", ".wav", ".bfstm",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'F', 'S', 'T', 'M' },
		0, MinusString, MinusString, "Nintendo Wii U / Switch Stream Audio (.bfstm / FSTM)" },

	// FF_BCWAV = 235 (Nintendo 3DS Wave Audio)
	{ FF_BCWAV, FF_BCWAV, 0, "BCWAV", ".bcwav", ".wav", ".bcwav",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'C', 'W', 'A', 'V' },
		0, MinusString, MinusString, "Nintendo 3DS Wave Audio (.bcwav / CWAV)" },

	// FF_BFWAV = 236 (Nintendo Wii U / Switch Wave Audio)
	{ FF_BFWAV, FF_BFWAV, 0, "BFWAV", ".bfwav", ".wav", ".bfwav",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'F', 'W', 'A', 'V' },
		0, MinusString, MinusString, "Nintendo Wii U / Switch Wave Audio (.bfwav / FWAV)" },

	// FF_BNSH = 237 (Nintendo Switch Binary Shader)
	{ FF_BNSH, FF_BNSH, 0, "BNSH", ".bnsh", ".szs", ".bnsh",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'B', 'N', 'S', 'H' },
		0, MinusString, MinusString, "Nintendo Switch Binary Shader (.bnsh / BNSH)" },

	// FF_GFBMDL = 238 (Game Freak FlatBuffer Model)
	{ FF_GFBMDL, FF_GFBMDL, 0, "GFBMDL", ".gfbmdl", ".szs", ".gfbmdl",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Game Freak FlatBuffer Model (.gfbmdl)" },

	// FF_GFBANM = 239 (Game Freak FlatBuffer Animation)
	{ FF_GFBANM, FF_GFBANM, 0, "GFBANM", ".gfbanm", ".szs", ".gfbanm",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Game Freak FlatBuffer Animation (.gfbanm)" },

	// FF_BNSTX = 240 (Nintendo Switch Texture Package)
	{ FF_BNSTX, FF_BNSTX, 0, "BNSTX", ".bnstx", ".szs", ".bnstx",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'N', 'S', 'T', 'X' },
		0, MinusString, MinusString, "Nintendo Switch Texture Package (.bnstx / NSTX)" },

	// FF_BFLIM = 241 (Nintendo Wii U FLIM Texture)
	{ FF_BFLIM, FF_BFLIM, 0, "BFLIM", ".bflim", ".szs", ".bflim",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Nintendo Wii U FLIM Texture (.bflim / FLIM)" },

	// FF_BCLIM = 242 (Nintendo 3DS CLIM Texture)
	{ FF_BCLIM, FF_BCLIM, 0, "BCLIM", ".bclim", ".szs", ".bclim",
		FFT_VALID | FFT_CUT | FFT_DECODE, 0,
		{ 0 },
		0, MinusString, MinusString, "Nintendo 3DS CLIM Texture (.bclim / CLIM)" },

	// FF_AAMP = 243 (Nintendo Binary Parameter Archive)
	{ FF_AAMP, FF_AAMP, 0, "AAMP", ".aamp", ".szs", ".aamp",
		FFT_VALID | FFT_CUT | FFT_DECODE, 4,
		{ 'A', 'A', 'M', 'P' },
		0, MinusString, MinusString, "Nintendo Binary Parameter Archive (.aamp / AAMP)" },

	// FF_BYML = 244 (Nintendo Binary YAML)
	{ FF_BYML, FF_BYML, 0, "BYML", ".byml", ".szs", ".byml",
		FFT_VALID | FFT_CUT | FFT_DECODE, 2,
		{ 'B', 'Y' },
		0, MinusString, MinusString, "Nintendo Binary YAML (.byml / .byaml / BY)" },

	// FF_MIO = 245 (WarioWare D.I.Y. Game/Comic/Record)
	{ FF_MIO, FF_MIO, 0, "MIO", ".mio", ".szs", ".mio",
		FFT_VALID | FFT_ARCHIVE | FFT_CUT | FFT_DECODE | FFT_EXTRACT, 0,
		{ 0 },
		0, MinusString, MinusString, "WarioWare D.I.Y. Game/Comic/Record (.mio)" },

	// FF_N
	{ 0 }
};

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////   FileTypeTab[]   //////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const KeywordTab_t cmdtab_FileType[] = { // INFO: cmd->opt := ff_attrib_t

	{ FF_YAZ0, "YAZ", "YAZ0", 0x103 }, { FF_YAZ1, "YAZ1", 0, 0x103 }, { FF_XYZ, "XYZ", 0, 0x103 },
	{ FF_BZ, "BZ", 0, 0x103 }, { FF_YBZ, "YBZ", 0, 0x103 }, { FF_BZIP2, "BZ2", "BZIP2", 0x103 },
	{ FF_LZ, "LZ", 0, 0x103 }, { FF_YLZ, "YLZ", 0, 0x103 }, { FF_LZMA, "LZMA", 0, 0x103 },
	{ FF_XZ, "XZ", 0, 0x3 }, { FF_U8, "U8", 0, 0x10f05 }, { FF_WU8, "WU8", 0, 0x10f05 },
	{ FF_RARC, "ARC", "RARC", 0xe05 }, { FF_BRRES, "BRES", "BRRES", 0xe05 },
	{ FF_BREFF, "BREFF", "REFF", 0x605 }, { FF_BREFT, "BREFT", "REFT", 0xe05 },
	{ FF_RKC, "RKC", 0, 0xe05 }, { FF_PACK, "PACK", 0, 0xe05 },
	{ FF_USE_LTA, "USE-LTA", "USELTA", 0x1 }, { FF_LTA, "", "LTA", 0xe05 },
	{ FF_LFL, "LFL", 0, 0xe05 }, { FF_CHR, "CHR", "CHR0", 0x861 }, { FF_CLR, "CLR", "CLR0", 0x861 },
	{ FF_MDL, "MDL", "MDL0", 0x9861 }, { FF_PAT, "PAT", "PAT0", 0x3821 },
	{ FF_SCN, "SCN", "SCN0", 0x861 }, { FF_SHP, "SHP", "SHP0", 0x861 },
	{ FF_SRT, "SRT", "SRT0", 0x861 }, { FF_TEX, "TEX", "TEX0", 0x3829 },
	{ FF_TEX_CT, "TEX", "TEX+CT", 0xb829 }, { FF_TEX_CT, "TEX-CT", "TEX0", 0xb829 },
	{ FF_TEX_CT, "TEXCT", 0, 0xb829 }, { FF_CTDEF, "CT-DEF", "CTDEF", 0x7011 },
	{ FF_CT0_CODE, "C0CODE", "CT0-CODE", 0x1 }, { FF_CT0_CODE, "CT0CODE", 0, 0x1 },
	{ FF_CT0_DATA, "C0DATA", "CT0-DATA", 0x1 }, { FF_CT0_DATA, "CT0DATA", 0, 0x1 },
	{ FF_CT1_CODE, "C1CODE", "CT1-CODE", 0x1 }, { FF_CT1_CODE, "CT1CODE", 0, 0x1 },
	{ FF_CT1_DATA, "C1DATA", "CT1-DATA", 0x9801 }, { FF_CT1_DATA, "CT1DATA", "CTCODE", 0x9801 },
	{ FF_CUP1, "CUP1", 0, 0x1 }, { FF_CRS1, "CRS1", 0, 0x1 }, { FF_MOD1, "MOD1", 0, 0x1 },
	{ FF_MOD2, "MOD2", 0, 0x1 }, { FF_OVR1, "OVR1", 0, 0x1 },
	{ FF_LE_BIN, "LE-BIN", "LEBIN", 0x9801 }, { FF_LEX, "LEX", 0, 0x3801 },
	{ FF_LEX_TXT, "LEX-TXT", "LEXTXT", 0x7011 }, { FF_LPAR, "LPAR", 0, 0x7011 },
	{ FF_LEDEF, "LE-DEF", "LEDEF", 0x7011 }, { FF_LEDIS, "LE-DIS", "LEDIS", 0x3011 },
	{ FF_LEREF, "LE-REF", "LEREF", 0x3011 }, { FF_LESTR, "LE-STR", "LESTR", 0x3011 },
	{ FF_SHA1REF, "SHA1REF", 0, 0x2011 }, { FF_SHA1ID, "SHA1ID", 0, 0x2011 },
	{ FF_PREFIX, "PREFIX", 0, 0x3011 }, { FF_MTCAT, "MTCAT", 0, 0x3011 },
	{ FF_CT_SHA1, "CT-SHA1", "CTSHA1", 0x11 }, { FF_MDL_TXT, "MDL-TXT", "MDLTXT", 0x11 },
	{ FF_PAT_TXT, "PAT-TXT", "PATTXT", 0x7011 },
	{ FF_CHR_TXT, "CHR-TXT", "CHRTXT", 0x7011 },
	{ FF_SRT_TXT, "SRT-TXT", "SRTTXT", 0x7011 },
	{ FF_VIS, "VIS", "VIS0", 0x861 }, { FF_VIS_TXT, "VIS-TXT", "VISTXT", 0x7011 },
	{ FF_CLR_TXT, "CLR-TXT", "CLRTXT", 0x7011 },
	{ FF_SHP_TXT, "SHP-TXT", "SHPTXT", 0x7011 },
	{ FF_SCN_TXT, "SCN-TXT", "SCNTXT", 0x7011 },{ FF_TPL, "TPL", 0, 0x3809 },
	{ FF_TPLX, "TPL", "TPLX", 0x3809 }, { FF_CUPICON, "CUPICON", "TPL", 0x2009 },
	{ FF_CUPICON, "TPLX", 0, 0x2009 }, { FF_BTI, "BTI", "BTIENV", 0x3809 },
	{ FF_BTI, "BTIMAT", 0, 0x3809 }, { FF_BREFT_IMG, "BREFT-IMG", "BREFTIMG", 0x3809 },
	{ FF_BREFT_IMG, "BT-IMG", "BTIMG", 0x3809 }, { FF_BMG, "BMG", "MESGBMG1", 0x3801 },
	{ FF_BMG_TXT, "BMG-TXT", "BMGTXT", 0x3011 }, { FF_KCL, "KCL", 0, 0xb801 },
	{ FF_KCL_TXT, "KCL-TXT", "KCLTXT", 0x7011 }, { FF_WAV_OBJ, "WAV-OBJ", "WAVOBJ", 0x7011 },
	{ FF_SKP_OBJ, "SKP-OBJ", "SKPOBJ", 0x7011 }, { FF_KMP, "KMP", 0, 0x3801 },
	{ FF_KMP_TXT, "KMP-TXT", "KMPTXT", 0x7011 }, { FF_ITEMSLT, "ITEMSLT", "SLT", 0x1 },
	{ FF_ITEMSLT_TXT, "ITEMSLT-TXT", "ITEMSLTTXT", 0x11 }, { FF_KMG, "KMG", 0, 0x3001 },
	{ FF_KMG_TXT, "KMG-TXT", "KMGTXT", 0x7011 }, { FF_KRM, "KRM", 0, 0x1 },
	{ FF_KRM_TXT, "KRM-TXT", "KRMTXT", 0x11 }, { FF_KRT, "KRT", 0, 0x1 },
	{ FF_KRT_TXT, "KRT-TXT", "KRTTXT", 0x11 }, { FF_OBJFLOW, "OBFLOW", "OBJFLOW", 0x3001 },
	{ FF_OBJFLOW_TXT, "OBJFLOW-TXT", "OBJFLOWTXT", 0x7011 },
	{ FF_OBJFLOW_TXT, "OF-TXT", "OFTXT", 0x7011 }, { FF_GH_ITEM, "GH-ITEM", "GHITEM", 0x3001 },
	{ FF_GH_ITEM_TXT, "GH-ITEM-TXT", "GHITEMTXT", 0x7011 },
	{ FF_GH_ITEM_TXT, "GI-TXT", "GITXT", 0x7011 }, { FF_GH_IOBJ, "GH-IOBJ", "GHIOBJ", 0x3001 },
	{ FF_GH_IOBJ_TXT, "GH-IOBJ-TXT", "GHIOBJTXT", 0x7011 }, { FF_GH_IOBJ_TXT, "GIOTXT", 0, 0x7011 },
	{ FF_GH_KART, "GH-KART", "GHKART", 0x3001 },
	{ FF_GH_KART_TXT, "GH-KART-TXT", "GHKARTTXT", 0x7011 },
	{ FF_GH_KART_TXT, "GK-TXT", "GKTXT", 0x7011 }, { FF_GH_KOBJ, "GH-KOBJ", "GHKOBJ", 0x3001 },
	{ FF_GH_KOBJ_TXT, "GH-KOBJ-TXT", "GHKOBJTXT", 0x7011 }, { FF_GH_KOBJ_TXT, "GKOTXT", 0, 0x7011 },
	{ FF_DRIVER, "DRIVER", "DRV", 0x1 }, { FF_VEHICLE, "VEH", "VEHICLE", 0x1 },
	{ FF_BRASD, "BRASD", 0, 0x1 }, { FF_RKG, "RKG", 0, 0x1 }, { FF_RKCO, "RKCO", 0, 0x1 },
	{ FF_STATICR, "REL", "STATICR", 0x8001 }, { FF_DOL, "DOL", 0, 0x8401 },
	{ FF_GCT, "GCT", 0, 0x1 }, { FF_GCT_TXT, "GCT", "GCT-TXT", 0x1 },
	{ FF_GCT_TXT, "GCTTXT", 0, 0x1 }, { FF_GCH, "GCH", 0, 0x1 }, { FF_WCH, "WCH", 0, 0x1 },
	{ FF_WPF, "WPF", 0, 0x1 }, { FF_XPF, "XPF", 0, 0x1 }, { FF_DISTRIB, "DISTRIB", 0, 0x11 },
	{ FF_PNG, "PNG", 0, 0x3089 }, { FF_PORTDB, "PORTDB", 0, 0x81 },
	{ FF_SCRIPT, "SCRIPT", 0, 0x91 }, { FF_JSON, "JSON", 0, 0x91 }, { FF_SH, "SH", 0, 0x91 },
	{ FF_BASH, "BASH", "SH", 0x91 }, { FF_PHP, "PHP", 0, 0x91 },
	{ FF_MAKEDOC, "MAKEDOC", "MD", 0x91 }, { FF_DIRECTORY, "DIRECTORY", 0, 0x80 },
	{ FF_PLT0, "PLT0", 0, 0x3809 }, { FF_BRLYT, "BRLYT", 0, 0x3001 },
	{ FF_BRLAN, "BRLAN", 0, 0x3001 }, { FF_BFLYT, "BFLYT", 0, 0x3001 },
	{ FF_BCLYT, "BCLYT", 0, 0x3001 }, { FF_BNTX, "BNTX", 0, 0x3001 }, { FF_GFA, "GFA", 0, 0x3001 },
	{ FF_BCH, "BCH", 0, 0x3001 }, { FF_BCRES, "BCRES", "CGFX", 0x3001 },
	{ FF_AJPG, "AJPG", "AJPG", 0x0100 }, { FF_RST, "RST", "0TSR", 0xe05 },
	{ FF_RST_TOC, "RST-TOC", "0SERCOTE", 0xc05 }, { FF_THP, "THP", "THP", 0x3801 },
	{ FF_MSBT, "MSBT", "MSGSTDBN", 0x3001 }, { FF_MSBP, "MSBP", "MSGPRJBN", 0x3001 },
	{ FF_MSBF, "MSBF", "MSGFLWBN", 0x3001 }, { FF_MSBT_TXT, "MSBT-TXT", "MSBTTXT", 0x7011 },
	{ FF_MSBP_TXT, "MSBP-TXT", "MSBPTXT", 0x7011 }, { FF_MSBF_TXT, "MSBF-TXT", "MSBFTXT", 0x7011 },
	{ FF_SDAT, "SDAT", "SDAT", 0x3801 }, { FF_BCSAR, "BCSAR", "CSAR", 0x3801 },
	{ FF_BFSAR, "BFSAR", "FSAR", 0x3801 }, { FF_BCWAR, "BCWAR", "CWAR", 0x3801 },
	{ FF_BFWAR, "BFWAR", "FWAR", 0x3801 }, { FF_BCGRP, "BCGRP", "CGRP", 0x3801 },
	{ FF_BFGRP, "BFGRP", "FGRP", 0x3801 }, { FF_GTX, "GTX", "GFX2", 0x3001 },
	{ FF_FZIP, "FZIP", "FZIP", 0x103 }, { FF_GVR, "GVR", "GCIX", 0x3809 },
	{ FF_SMDH, "SMDH", 0, 0x3009 }, { FF_SARC, "SARC", "SARC", 0xe05 },
	{ FF_BFMA, "BFMA", "BFMA", 0xe05 }, { FF_ZLIB, "ZLIB", "ZLIB", 0x103 },
	{ FF_ZLIB, "DEFLATE", 0, 0x103 },
	{ FF_ZSTD, "ZSTD", "ZSTD", 0x103 },
	{ FF_ZSTD, "ZST", "ZST", 0x103 },
	{ FF_ZSTD, "ZS", "ZS", 0x103 },
	{ FF_NSBTX, "NSBTX", "BTX0", 0x3829 },
	{ FF_NFTR, "NFTR", "FNTR", 0x3809 },
	{ FF_BNFR, "BNFR", "RNFB", 0x3809 },
	{ FF_BNLL, "BNLL", "LLNB", 0x3001 },
	{ FF_BNCL, "BNCL", "LCNB", 0x3001 },
	{ FF_BNBL, "BNBL", "LBNB", 0x3001 },
	{ FF_VLX, "VLX", 0, 0x103 },
	{ FF_PUCRUNCH, "PUCRUNCH", "PCRUNCH", 0x103 },
	{ FF_LZX, "LZX", 0, 0x103 },
	{ FF_DIFF, "DIFF", 0, 0x103 },
	{ FF_LZOVL, "LZOVL", "OVL", 0x103 },
	{ FF_ALAR, "ALAR", 0, 0xe05 },
	{ FF_DARC, "DARC", 0, 0xe05 },
	{ FF_SADL, "SADL", 0, 0x861 },
	{ FF_NCER, "NCER", "RECN", 0x3001 },
	{ FF_NANR, "NANR", "RNAN", 0x3001 },
	{ FF_NUT, "NUT", "NTP3", 0x3809 },
	{ FF_NUD, "NUD", "NDP3", 0x3001 },
	{ FF_DTLS, "DTLS", "LS", 0xe05 },
	{ FF_NUMSHB, "NUMSHB", "SSBH", 0x3001 },
	{ FF_UE4_PAK, "PAK", "UE4PAK", 0xe05 },
	{ FF_SMASH_ARC, "ARC", "SMASHARC", 0xe05 },
	{ FF_PRC, "PRC", "PARAM", 0x3001 },
	{ FF_CNUT, "CNUT", "SQIR", 0xe05 },
	{ FF_CMP, "CMP", 0, 0x103 },
	{ FF_HSF, "HSF", "HSFV", 0x3001 },
	{ FF_HSD, "HSD", "HALDAT", 0x3001 },
	{ FF_BNFM, "BNFM", 0, 0x3001 },
	{ FF_XPCK, "XPCK", "XC", 0xe05 },
	{ FF_XIMG, "XIMG", "XI", 0x3809 },
	{ FF_HGO, "HGO", "0OGH", 0x3001 },
	{ FF_ZTAB, "ZTAB", 0, 0xe05 },
	{ FF_GLG, "GLG", "RLG", 0x3001 },
	{ FF_MDR, "MDR", 0, 0xe05 },
	{ FF_MSH, "MSH", "PMSH", 0x3001 },
	{ FF_MOD, "MOD", "NDL3", 0x3001 },
	{ FF_PERS, "PERS", "FRAGMENT", 0x3001 },
	{ FF_PVOL, "PVOL", 0, 0xe05 },
	{ FF_STPK, "STPK", "SRD", 0xe05 },
	{ FF_G1M, "G1M", 0, 0x3001 },
	{ FF_G1T, "G1T", 0, 0x3809 },
	{ FF_G4PKM, "G4PKM", 0, 0x3001 },
	{ FF_LMD, "LMD", 0, 0x3001 },
	{ FF_XMSG, "XMSG", "MESS", 0xe05 },
	{ FF_NWR_LEVELINFO, "NWR-LEVELINFO", "LEVELINFO", 0xe05 },
	{ FF_NWR_ANIMTILES, "NWR-ANIMTILES", "ANIMTILES", 0xe05 },
	{ FF_NSMBW_CHK, "NSMBW-CHK", "BGCHK", 0x3001 },
	{ FF_KPBIN, "KPBIN", "KP_M", 0xe05 },
	{ FF_KPMAP, "KPMAP", 0, 0x3001 },
	{ FF_CHANS, "CHANS", "RCHE", 0xe05 },
	{ FF_RLG, "RLG", 0, 0x3001 },
	{ FF_GAR, "GAR", "ZAR", 0xe05 },
	{ FF_CTXB, "CTXB", 0, 0x3809 },
	{ FF_TMPK, "TMPK", 0, 0xe05 },
	{ FF_NXARC, "NXARC", "RAXN", 0xe05 },
	{ FF_APAK, "APAK", 0, 0xe05 },
	{ FF_PKZ, "PKZ", 0, 0xe05 },
	{ FF_VIBS, "VIBS", 0, 0xe05 },
	{ FF_PG_DAT, "PG-DAT", "PGDAT", 0xe05 },
	{ FF_WTA, "WTA", 0, 0xe05 },
	{ FF_GFPAK, "GFPAK", "GFLXPACK", 0xe05 },
	{ FF_BARS, "BARS", 0, 0xe05 },
	{ FF_NLG_DICT, "NLG-DICT", "DICT", 0xe05 },
	{ FF_TXTG, "TXTG", "6PK0", 0xe05 },
	{ FF_NLOC, "NLOC", 0, 0x3001 },
	{ FF_XLNK, "XLNK", "BSLNK", 0x3001 },
	{ FF_ROMFS, "ROMFS", "IVFC", 0xe05 },
	{ FF_XTX, "XTX", "DFVN", 0xe05 },
	{ FF_TVOL, "TVOL", 0, 0xe05 },
	{ FF_TXE, "TXE", 0, 0x3809 },
	{ FF_MKAGPDX_MDL, "MKAGPDX-MDL", "BIKE", 0x3001 },
	{ FF_MTXT, "MTXT", 0, 0xe05 },
	{ FF_SIR0, "SIR0", 0, 0xe05 },
	{ FF_TEX3DS, "TEX3DS", "TEX", 0x3809 },
	{ FF_PTLG, "PTLG", "RLT", 0xe05 },
	// Super Mario Strikers (GameCube) names the same container .glt, which
	// ExtractPTLGArchive() already accepts; without this the name did not
	// resolve and nothing could tell that a .glt destination was not PTLG.
	{ FF_PTLG, "GLT", 0, 0xe05 },
	{ FF_BCSTM, "BCSTM", "CSTM", 0x3801 },
	{ FF_BFSTM, "BFSTM", "FSTM", 0x3801 },
	{ FF_BCWAV, "BCWAV", "CWAV", 0x3801 },
	{ FF_BFWAV, "BFWAV", "FWAV", 0x3801 },
	{ FF_BNSH, "BNSH", 0, 0x3001 },
	{ FF_MIO, "MIO", 0, 0xe05 },

	{ 0, 0, 0, 0 }
};

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////////   E N D   //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
