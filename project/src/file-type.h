
//
///////////////////////////////////////////////////////////////////////////////
//////   This file is created by a script. Modifications will be lost!   //////
///////////////////////////////////////////////////////////////////////////////

#ifndef SZS_FILE_TYPE_H
#define SZS_FILE_TYPE_H 1

#include "types.h"
#include "dclib-basics.h"

//
///////////////////////////////////////////////////////////////////////////////
////////////////////////////   enum ff_attrib_t   /////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// [[ff_attrib_t]]
typedef enum ff_attrib_t
{
	FFT_VALID = 0x00001, // a valid file format

	FFT_COMPRESS = 0x00002, // is a compression format
	FFT_ARCHIVE = 0x00004, // is a supported archive
	FFT_GRAPHIC = 0x00008, // graphic image format
	FFT_TEXT = 0x00010, // (decoded) text file
	FFT_M_FTYPE = 0x0001e, // mask of file types

	FFT_BRSUB = 0x00020, // is a BRRES sub file
	FFT_BRSUB2 = 0x00040, // have a scondary file structure like a BRSUB
	FFT_EXTERNAL = 0x00080, // external file format
	FFT_TRACK = 0x00100, // can be a track file
	FFT_M_CLASS = 0x001e0, // mask of file classes

	FFT_CREATE = 0x00200, // creation supported
	FFT_EXTRACT = 0x00400, // extracting supported
	FFT_CUT = 0x00800, // 'cut-files' supported
	FFT_DECODE = 0x01000, // decoding supported
	FFT_ENCODE = 0x02000, // encoding supported
	FFT_PARSER = 0x04000, // parser support on encoding
	FFT_PATCH = 0x08000, // patching of binaries
	FFT_LINK = 0x10000, // hardlinks supported
	FFT_M_SUPPORT = 0x1fe00, // mask of supported operations

	FFT_NONE = 0x00000, // none set
	FFT_M_ALL = 0x1ffff // all flags of above

} ff_attrib_t;

//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////   enum file_format_t   ////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// [[file_format_t]]
typedef enum file_format_t
{
	FF_UNKNOWN = 0, // definitley 0

	//--- file modes

	FF_YAZ0, //  1
	FF_YAZ1, //  2
	FF_XYZ, //  3
	FF_BZ, //  4
	FF_YBZ, //  5
	FF_BZIP2, //  6
	FF_LZ, //  7
	FF_YLZ, //  8
	FF_LZMA, //  9
	FF_XZ, // 10

	FF_U8, // 11
	FF_WU8, // 12
	FF_RARC, // 13
	FF_BRRES, // 14
	FF_BREFF, // 15
	FF_BREFT, // 16
	FF_RKC, // 17
	FF_PACK, // 18
	FF_USE_LTA, // 19
	FF_LTA, // 20
	FF_LFL, // 21

	FF_CHR, // 22
	FF_CLR, // 23
	FF_MDL, // 24
	FF_PAT, // 25
	FF_SCN, // 26
	FF_SHP, // 27
	FF_SRT, // 28
	FF_TEX, // 29

	FF_TEX_CT, // 30
	FF_CTDEF, // 31

	FF_CT0_CODE, // 32
	FF_CT0_DATA, // 33
	FF_CT1_CODE, // 34
	FF_CT1_DATA, // 35

	FF_CUP1, // 36
	FF_CRS1, // 37
	FF_MOD1, // 38
	FF_MOD2, // 39
	FF_OVR1, // 40

	FF_LE_BIN, // 41
	FF_LEX, // 42
	FF_LEX_TXT, // 43

	FF_LPAR, // 44
	FF_LEDEF, // 45
	FF_LEDIS, // 46
	FF_LEREF, // 47
	FF_LESTR, // 48
	FF_SHA1REF, // 49
	FF_SHA1ID, // 50
	FF_PREFIX, // 51
	FF_MTCAT, // 52
	FF_CT_SHA1, // 53

	FF_MDL_TXT, // 54

	FF_PAT_TXT, // 55

	FF_TPL, // 56
	FF_TPLX, // 57
	FF_CUPICON, // 58
	FF_BTI, // 59
	FF_BREFT_IMG, // 60

	FF_BMG, // 61
	FF_BMG_TXT, // 62

	FF_KCL, // 63
	FF_KCL_TXT, // 64
	FF_WAV_OBJ, // 65
	FF_SKP_OBJ, // 66

	FF_KMP, // 67
	FF_KMP_TXT, // 68

	FF_ITEMSLT, // 69
	FF_ITEMSLT_TXT, // 70

	FF_KMG, // 71
	FF_KMG_TXT, // 72

	FF_KRM, // 73
	FF_KRM_TXT, // 74

	FF_KRT, // 75
	FF_KRT_TXT, // 76

	FF_OBJFLOW, // 77
	FF_OBJFLOW_TXT, // 78

	FF_GH_ITEM, // 79
	FF_GH_ITEM_TXT, // 80

	FF_GH_IOBJ, // 81
	FF_GH_IOBJ_TXT, // 82

	FF_GH_KART, // 83
	FF_GH_KART_TXT, // 84

	FF_GH_KOBJ, // 85
	FF_GH_KOBJ_TXT, // 86

	FF_DRIVER, // 87
	FF_VEHICLE, // 88

	FF_BRASD, // 89
	FF_RKG, // 90
	FF_RKCO, // 91

	FF_STATICR, // 92
	FF_DOL, // 93

	FF_GCT, // 94
	FF_GCT_TXT, // 95
	FF_GCH, // 96
	FF_WCH, // 97
	FF_WPF, // 98
	FF_XPF, // 99
	FF_DISTRIB, // 100

	FF_PNG, // 101
	FF_PORTDB, // 102

	FF_TXT, // 103
	FF_SCRIPT, // 104
	FF_JSON, // 105
	FF_SH, // 106
	FF_BASH, // 107
	FF_PHP, // 108
	FF_MAKEDOC, // 109

	FF_DIRECTORY, // 110

	// Nintendo DS / Wii U / 3DS formats (added by fork)
	FF_PLT0, // 111 - NDS/Wii BRRES palette
	FF_BRLYT, // 112 - Wii layout binary
	FF_BRLAN, // 113 - Wii layout animation
	FF_BFLYT, // 114 - Wii U/Switch layout binary
	FF_BCLYT, // 115 - 3DS layout binary
	FF_BFLYT_TXT, // 116 - Wii U/Switch layout text (.tflyt)
	FF_BCLYT_TXT, // 117 - 3DS layout text (.ctlyt)
	FF_BNTX, // 118 - Switch texture container (detection only)
	FF_GFA, // 119 - Good-Feel archive (GFAC)
	FF_BCH, // 120 - 3DS CTR H3D container
	FF_BCRES, // 121 - 3DS CGFX/BCRES container
	FF_AJPG, // 122 - Nintendo AJPG/ODH image
	FF_RST, // 123 - Monster Games archive (0TSR)
	FF_RST_TOC, // 124 - Monster Games TOC (0SERCOTE)
	FF_THP, // 125 - Nintendo GameCube/Wii THP video
	FF_MSBT, // 126 - Nintendo Message Studio Binary Text (MsgStdBn)
	FF_MSBP, // 127 - Nintendo Message Studio Binary Project (MsgPrjBn)
	FF_MSBF, // 128 - Nintendo Message Studio Binary Flow (MsgFlwBn)
	FF_MSBT_TXT, // 129 - MSBT text representation
	FF_MSBP_TXT, // 130 - MSBP text representation
	FF_MSBF_TXT, // 131 - MSBF text representation
	FF_SDAT, // 132 - Nintendo DS Sound Archive (SDAT)
	FF_BCSAR, // 133 - Nintendo 3DS Sound Archive (CSAR)
	FF_BFSAR, // 134 - Nintendo Wii U / Switch Sound Archive (FSAR)
	FF_BCWAR, // 135 - Nintendo 3DS Sound Wave Archive (CWAR)
	FF_BFWAR, // 136 - Nintendo Wii U / Switch Sound Wave Archive (FWAR)
	FF_BCGRP, // 137 - Nintendo 3DS Sound Group (CGRP)
	FF_BFGRP, // 138 - Nintendo Wii U / Switch Sound Group (FGRP)
	FF_GTX, // 139 - Wii U GX2 texture/shader container (Gfx2/GTX/GSH)
	FF_RSEQ, // 140 - Nintendo Wii Revolution Sequence (.rseq / .brseq)
	FF_CSEQ, // 141 - Nintendo 3DS CTR Sequence (.cseq / .bcseq)
	FF_FSEQ, // 142 - Nintendo Wii U / Switch Format Sequence (.fseq / .bfseq)
	FF_SSEQ, // 143 - Nintendo DS Nitro Sequence (.sseq)
	FF_SEQ_TXT, // 144 - Nintendo Sequence MML text (.rseq.txt / .mml)
	FF_MIDI, // 145 - Standard MIDI File (.mid)
	FF_FZIP, // 146 - Game & Wario FZIP compression (.fzip)
	FF_GVR, // 147 - Sega GameCube/Wii texture (GCIX/GVRT)
	FF_SMDH, // 148 - Nintendo 3DS icon/title metadata (SMDH)
	FF_SARC, // 149 - Nintendo SARC archive (SARC / .sarc)
	FF_BFMA, // 150 - Nintendo Wii U manual archive (.bfma)
	FF_ZLIB, // 151 - Zlib deflate compression (.zlib)
	FF_CHR_TXT, // 152 - Text version of CHR
	FF_SRT_TXT, // 153 - Text version of SRT
	FF_VIS, // 154 - NW4R node visibility animation (VIS0)
	FF_VIS_TXT, // 155 - Text version of VIS
	FF_CLR_TXT, // 156 - Text version of CLR
	FF_SHP_TXT, // 157 - Text version of SHP
	FF_ZSTD, // 158 - Zstandard compression (.zs / .zst / .zstd)
	FF_NSBTX, // 159 - Nintendo DS 3D texture archive (.nsbtx / BTX0)
	FF_NFTR, // 160 - Nintendo DS font resource (.nftr / RTNF)
	FF_BNFR, // 161 - Nintendo DS binary font resource (.bnfr / RNFB)
	FF_BNLL, // 162 - Nintendo DS binary layout (.bnll / LLNB)
	FF_BNCL, // 163 - Nintendo DS binary cell layout (.bncl / LCNB)
	FF_BNBL, // 164 - Nintendo DS binary block layout (.bnbl / LBNB)
	FF_VLX, // 165 - Namco/Pac-Man World DS compression (.vlx)
	FF_PUCRUNCH, // 166 - Griptonite Games DS compression (.pucrunch)
	FF_LZX, // 167 - Nintendo DS extended LZ11 (.lzx)
	FF_DIFF, // 168 - Nintendo differential filter (.diff)
	FF_SCN_TXT, // 169 - Text version of SCN
	FF_LZOVL, // 170 - Nintendo DS Overlay LZSS compression (.ovl)
	FF_ALAR, // 171 - Jump Ultimate Stars archive (.alar)
	FF_DARC, // 172 - Level-5 / Layton archive (.darc)
	FF_SADL, // 173 - Level-5 / Layton SADL sound archive (.sad)
	FF_NCER, // 174 - Nintendo DS Cell resource (.ncer / RECN)
	FF_NANR, // 175 - Nintendo DS Animation resource (.nanr / RNAN)
	FF_NUT, // 176 - Namco Universal Texture (.nut / NTP3 / NTWU)
	FF_NUD, // 177 - Namco Universal Data model (.nud / NDP3 / NDWU)
	FF_DTLS, // 178 - Smash 4 data/lookup archive (dt00 / ls00)
	FF_NUMSHB, // 179 - Smash Ultimate SSBH Mesh (.numshb)
	FF_UE4_PAK, // 180 - Unreal Engine 4 archive (Mario & Luigi: Brothership .pak)
	FF_SMASH_ARC, // 181 - Super Smash Bros. Ultimate data.arc archive
	FF_PRC, // 182 - Smash Parameter binary (.prc / parambinary)
	FF_CNUT, // 183 - Compiled Squirrel script / messages (Wii Party .cnut / SQIR)
	FF_CMP, // 184 - HAL Laboratory LZ11 compressed file (.cmp)
	FF_HSF, // 185 - Hudson Soft 3D Model (Mario Party 4-8 .hsf)
	FF_HSD, // 186 - HAL Laboratory SYS/DAT Data/Model (Super Smash Bros Melee .dat)
	FF_BNFM, // 187 - Nd Cube Wii U 3D Model (Mario Party 10 .bnfm)
	FF_XPCK, // 188 - Level-5 3DS/Switch Container Archive (Layton/Inazuma/Yokai .xc / XPCK)
	FF_XIMG, // 189 - Level-5 3DS/Switch Image/Texture (.xi / XIMG)
	FF_ZTAB, // 190 - Camelot GameCube/Wii Archive Table (.ztab / ZTAB)
	FF_GLG, // 191 - Next Level Games 3D Model (Super Mario Strikers .glg)
	FF_MDR, // 192 - Dance Dance Revolution Mario Mix Chunk Archive (.mdr)
	FF_MSH, // 193 - Monster Games collision mesh (.msh)
	FF_MOD, // 194 - Monster Games display list model (.mod)
	FF_PERS, // 195 - Pokemon Stadium N64 Model / Fragment (.pers / FRAGMENT)
	FF_PVOL, // 194 - Pikmin 1 & 2 Model Container Archive (.pvol)
	FF_STPK, // 195 - Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / STPK)
	FF_G1M, // 196 - Koei Tecmo 3D Model (Hyrule Warriors / FE Warriors .g1m)
	FF_G1T, // 197 - Koei Tecmo Texture Container (Hyrule Warriors / FE Warriors .g1t)
	FF_G4PKM, // 198 - Level-5 / Nintendo 3D Model (.g4pkm)
	FF_LMD, // 199 - Pokemon Masters 3D Model (.lmd)
	FF_XMSG, // 200 - Wii Party Message / Text Archive (mess.bin / XMSG)
	FF_NWR_LEVELINFO, // 201 - Newer SMBW Level Information (LevelInfo.bin / NWRp)
	FF_NWR_ANIMTILES, // 202 - Newer SMBW Animated Tiles (AnimTiles.bin / NWRa)
	FF_NSMBW_CHK, // 203 - NSMBW Tileset Collision Attributes (d_bgchk_*.bin)
	FF_KPBIN, // 204 - Koopatlas Binary World Map (.kpbin / KP_m)
	FF_KPMAP, // 205 - Koopatlas Map Project (.kpmap / JSON)
	FF_CHANS, // 206 - Nintendo Wii ChannelScript (.cs / RCHE)
	FF_RLG, // 207 - Next Level Games 3D Model (Mario Strikers Charged .rlg)
	FF_GAR, // 208 - Grezzo Zelda / Luigi's Mansion Archive (.zar / .gar)
	FF_CTXB, // 209 - Grezzo 3DS Texture Container (.ctxb)
	FF_TMPK, // 210 - Twilight Princess HD Archive (.pack / TMPK)
	FF_NXARC, // 211 - Nintendo Switch NX Archive (.nxarc / RAXN)
	FF_APAK, // 212 - Nintendo APAK Archive (.apak / APAK)
	FF_PKZ, // 213 - PlatinumGames Archive (.pkz / pkz)
	FF_VIBS, // 214 - Nintendo Switch Joy-Con Vibration Archive (.vibs)
	FF_PG_DAT, // 215 - PlatinumGames DAT Archive (.dat / DAT)
	FF_WTA, // 216 - PlatinumGames WT Archive (.wta / WTA )
	FF_GFPAK, // 217 - Game Freak Pokemon Archive (.gfpak / GFLXPACK)
	FF_BARS, // 218 - Nintendo Binary Audio Resource Archive (.bars / BARS)
	FF_NLG_DICT, // 219 - Next Level Games Dictionary Archive (.dict / LM2 / LM3 / Punch-Out!!)
	FF_TXTG, // 220 - Next Level Games Texture To Go (.txtg / 6PK0)
	FF_NLOC, // 221 - Next Level Games Localization Text (.nloc / .loc / NLOC)
	FF_XLNK, // 222 - Nintendo Effect Link Binary (.bslnk / .belnk / XLNK)
	FF_ROMFS, // 223 - Nintendo 3DS Read-Only File System (.romfs / IVFC)
	FF_XTX, // 224 - Nintendo Switch XTX Texture Container (.xtx / DFvN)
	FF_TVOL, // 225 - Koei Tecmo / Gust Texture Volume Archive (.tvol)
	FF_TXE, // 226 - Pikmin 1 Texture (.txe)
	FF_MKAGPDX_MDL, // 227 - Mario Kart Arcade GP DX Model (.bin / BIKE)
	FF_MTXT, // 228 - Nintendo Switch MTXT Texture Archive (.mtxt / MTXT)
	FF_SIR0, // 229 - Pokemon Mystery Dungeon Resource Container (.sir0 / SIR0)
	FF_TEX3DS, // 230 - Nintendo 3DS Proprietary Texture (.tex)
	FF_PTLG, // 231 - Next Level Games Texture Container (.glt / .rlt / PTLG)
	FF_BCSTM, // 232 - Nintendo 3DS Stream Audio (.bcstm / CSTM)
	FF_BFSTM, // 233 - Nintendo Wii U / Switch Stream Audio (.bfstm / FSTM)
	FF_BCWAV, // 234 - Nintendo 3DS Wave Audio (.bcwav / CWAV)
	FF_BFWAV, // 235 - Nintendo Wii U / Switch Wave Audio (.bfwav / FWAV)
	FF_BNSH, // 236 - Nintendo Switch Binary Shader (.bnsh / BNSH)
	FF_GFBMDL, // 237 - Game Freak FlatBuffer Model (.gfbmdl)
	FF_GFBANM, // 238 - Game Freak FlatBuffer Animation (.gfbanm)
	FF_BNSTX, // 239 - Nintendo Switch Texture Package (.bnstx / NSTX)
	FF_BFLIM, // 240 - Nintendo Wii U FLIM Texture (.bflim / FLIM)
	FF_BCLIM, // 241 - Nintendo 3DS CLIM Texture (.bclim / CLIM)
	FF_AAMP, // 242 - Nintendo Binary Parameter Archive (.aamp / AAMP)
	FF_BYML, // 243 - Nintendo Binary YAML (.byml / .byaml / BY)
	FF_MIO, // 244 - WarioWare D.I.Y. Game/Comic/Record (.mio)
	FF_IQIPACK, // 245 - NVIDIA Shield iQiyi PAK archive (.pak / PACK)
	FF_ZDAT, // 246 - Animal Crossing: Pocket Camp asset container (.zdat / ZDAT)

	//--- number of elements

	FF_N,
	FF_AUTO = FF_N,

	//--- some more values

	FF_INVALID = -1,
	FF__FIRST_BRSUB = FF_CHR,
	FF__LAST_BRSUB = FF_TEX,

} file_format_t;

//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////   struct file_type_t   ////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// [[file_type_t]]
typedef struct file_type_t
{
	file_format_t fform; // file format
	file_format_t fform_bin; // file format of binary partner (or 0=FF_UNKOWN)
	file_format_t fform_txt; // file format of text partner (or 0=FF_UNKOWN)

	ccp name; // name
	ccp ext; // standard file extension
	ccp ext_compr; // file extension if compressed
	ccp ext_magic; // magic based file extension

	ff_attrib_t attrib; // file atributes

	u8 magic_len; // length of magic, usually 0, 4 or 8 bytes
	u8 magic[8 + 1]; // usual magic, NULL terminated
	ccp subdir; // NULL or BRRES directory name

	ccp read_info; // info about reading support, never NULL
	ccp write_info; // info about writing support, never NULL
	ccp remark; // remark about file type, never NULL

} file_type_t;

extern const file_type_t FileTypeTab[FF_N + 1];
extern const struct KeywordTab_t cmdtab_FileType[];
extern const char filetype_info_unknown[];
extern const char filetype_info_not_supported[];

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////////   E N D   //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#endif // SZS_FILE_TYPE_H
