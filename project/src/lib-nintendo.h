// Nintendo-format registry and bounded decoders added by the Nintendo fork.
#ifndef SZS_LIB_NINTENDO_H
#define SZS_LIB_NINTENDO_H 1

#include "types.h"

#define NFMT_MAX_OUTPUT (512u << 20)

static inline u32 rd_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline u32 rd_le32 (const u8 *p)
{
	return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}
static inline u16 rd_be16 (const u8 *p)
{
	return (u16)p[0] << 8 | p[1];
}
static inline u16 rd_le16 (const u8 *p)
{
	return (u16)p[1] << 8 | p[0];
}
static inline void wr_be16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = v;
}
static inline void wr_le16 (u8 *p, u16 v)
{
	p[0] = v;
	p[1] = v >> 8;
}
static inline void wr_be32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}
static inline void wr_le32 (u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

typedef enum nfmt_type_t
{
	NFMT_UNKNOWN,
	NFMT_DSB,
	NFMT_TPL,
	NFMT_STPL,
	NFMT_SARC,
	NFMT_LZ10,
	NFMT_LZ11,
	NFMT_HUFF4,
	NFMT_HUFF8,
	NFMT_RL,
	NFMT_ASH0,
	NFMT_YAY0,
	NFMT_LZH8,
	NFMT_BFLIM,
	NFMT_BCLIM,
	NFMT_NUTEXB,
	NFMT_BNR,
	NFMT_NCGR,
	NFMT_NCLR,
	NFMT_NCER,
	NFMT_NANR,
	NFMT_BRFNT,
	NFMT_BRFNA,
	NFMT_BCFNT,
	NFMT_BRLAN,
	NFMT_BRLYT,
	NFMT_BFLAN,
	NFMT_BFLYT,
	NFMT_BCLAN,
	NFMT_BCLYT,
	NFMT_PLT0,
	NFMT_MSBT,
	NFMT_BCRES,
	NFMT_BFRES,
	NFMT_BNTX,
	NFMT_GFA,
	NFMT_BCH,
	NFMT_QLZ,
	NFMT_PAC,
	NFMT_RNC,
	NFMT_ROMC,
	NFMT_PSDK,
	NFMT_AT7,
	NFMT_CTPK,
	NFMT_BYML,
	NFMT_NARC,
	NFMT_NSCR,
	NFMT_FZIP,
	NFMT_JARC,
	NFMT_JCMP,
	NFMT_BFMA,
	NFMT_ZLIB,
	NFMT_MVDK,
	NFMT_VLX,
	NFMT_PUCRUNCH,
	NFMT_LZX,
	NFMT_DIFF8,
	NFMT_DIFF16,
	NFMT_NSBTX,
	NFMT_NFTR,
	NFMT_BNFR,
	NFMT_BNLL,
	NFMT_BNCL,
	NFMT_BNBL,
	NFMT_LZOVL,
	NFMT_ALAR,
	NFMT_DARC,
	NFMT_SADL,
	NFMT_HSF,
	NFMT_HSD,
	NFMT_BNFM,
	NFMT_XPCK,
	NFMT_XIMG,
	NFMT_HGO,
	NFMT_ZTAB,
	NFMT_GLG,
	NFMT_MDR,
	NFMT_PERS,
	NFMT_PVOL,
	NFMT_STPK,
	NFMT_G1M,
	NFMT_G1T,
	NFMT_G4PKM,
	NFMT_LMD,
	NFMT_MSH,
	NFMT_MOD
} nfmt_type_t;

typedef struct nfmt_info_t
{
	nfmt_type_t type;
	bool big_endian;
	bool compressed;
	u32 declared_size;
	// Byte offset of the real payload (magic + body) within the buffer that
	// was handed to DetectNintendoFormat(). Zero for every format except the
	// WarioWare: D.I.Y. Showcase / "WarioWare Snapped!" (DSiWare) Nitro
	// graphics wrapper -- see the NITRO_SIZE_PREFIX comment in
	// DetectNintendoFormat() for what this 4-byte field actually is.
	u32 payload_offset;
} nfmt_info_t;

// Detect formats by their stable magic/header fields. Never reads past SIZE.
__attribute__((weak)) nfmt_info_t DetectNintendoFormat (const void *data, uint size, ccp filename);
ccp GetNintendoFormatName (nfmt_type_t type);

enumError AllocOutput (u8 **dest, uint *dest_size, u32 size);

#include "lib-camelot.h"
#include "lib-lz10.h"
int CxIsCompressedMvDK (const unsigned char *buffer, unsigned int size);
enumError DecodeMVDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

enumError DecodeLZOvl (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeLZOvl (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
int CxIsCompressedLZOvl (const unsigned char *src, unsigned int size);

enumError DecodeALAR (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeDARC (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeSADL_WAV (u8 **dest_wav, uint *dest_size, const u8 *src, uint src_size);
enumError DecodePSDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodePSDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeSSZL (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeSSZL (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

enumError DecodeVLX (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeVLX (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
int CxIsCompressedVlx (const unsigned char *src, unsigned int size);

enumError DecodePuCrunch (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodePuCrunch (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
int CxIsCompressedPuCrunch (const unsigned char *buffer, unsigned int size);

enumError DecodeLZX (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeLZX (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
int CxIsCompressedLZX (const unsigned char *buffer, unsigned int size);

enumError DecodeDiff8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeDiff8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeDiff16 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeDiff16 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#include "lib-nintendo-rl.h"
#include "lib-huff.h"
#include "lib-ash0.h"
enumError EncodeMVDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#include "lib-yay0.h"
#include "lib-blz.h"
#include "lib-lzh8.h"
// AT7 (AT7P/AT7X/AT7E) compression, used by Pokémon Mystery Dungeon WiiWare titles.
enumError DecodeAT7 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeAT7 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
// FZIP (Game & Wario, Wii U) zlib-based compression
enumError DecodeFZIP (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeFZIP (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
// RWAV (Wii)/FWAV (Wii U/Switch)/CWAV (3DS) NintendoWare wave audio -> PCM WAV.
// PCM8/PCM16/DSP-ADPCM/IMA-ADPCM, mono or multi-channel (non-interleaved
// source streams are interleaved into the WAV output).
enumError DecodeBXWAV (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
#include "lib-rnc.h"

// Decode Animal Crossing: Wild World TXTR/DSB A3I5 textures to tightly packed
// RGBA8 pixels. WIDTH and HEIGHT are populated on success.
enumError DecodeDSB_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

// Encode the AC:WW 128x128 TXTR layout: a 32-colour RGB555 palette followed
// by A3I5 texels.
enumError EncodeDSB_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height);

// Decode the 96x32 RGB5A3 icon embedded at offset 0x20 in Wii BNR1/BNR2
// banner files. The caller owns *DEST on success.
enumError DecodeBNR_RGBA (u8 **dest, const u8 *src, uint src_size);
// Create a complete BNR1 banner with a 96x32 RGB5A3 icon and zero-filled
// textual metadata fields.  BNR1 is accepted by Wii/GameCube banner readers.
enumError EncodeBNR_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height);
// Encode an RGBA sheet to a Wii bitmap font (BRFNT / RFNT) with FINF/TGLP/CWDH/CMAP.
enumError EncodeBRFNT_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint cell_w, uint cell_h);

enumError EncodeBRFNA_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint cell_w, uint cell_h);
// Encode an RGBA sheet to a 3DS/Wii U bitmap font (BCFNT / BFFNT).
enumError EncodeBCFNT_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height,
	uint cell_w, uint cell_h, bool is_wiiu);
// Decode NCGR tile data as a 16-tile-wide indexed grayscale sheet. A paired
// NCLR palette can be applied by the higher-level DS asset project layer.
enumError DecodeNCGR_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

enumError EncodeNCGR_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool is_8bpp);

// Decode a Nitro NCLR (RLCN/TTLP) palette into a readable RGBA8 swatch
// image. Each palette entry is an opaque BGR555 colour; entries are laid out
// in 16 columns with 8x8 pixel swatches.
enumError DecodeNCLR_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);

enumError EncodeNCLR_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height);

typedef struct nintendo_sarc_entry_t
{
	ccp name;
	const u8 *data;
	uint size;
} nintendo_sarc_entry_t;

#include "lib-ncer.h"

#include "lib-flim.h"
#include "lib-nutexb.h"
#include "lib-ctpk.h"

typedef struct nintendo_sarc_t
{
	const u8 *data;
	uint size, data_offset, sfnt_offset, entries_offset, n_entries;
	bool big_endian;
} nintendo_sarc_t;

// Parse either SARC byte order without allocating.  Entry pointers returned
// by GetSARCEntry remain owned by the original input buffer.
enumError ScanSARC (nintendo_sarc_t *sarc, const u8 *data, uint size);
enumError GetSARCEntry (
	const nintendo_sarc_t *sarc, uint index, ccp *name, const u8 **data, uint *size);

// Build a canonical SARC with a 0x100-byte aligned data section.  ENTRIES are
// sorted by the standard 0x65 SFAT hash; the returned buffer is malloc-owned.
enumError CreateSARC (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
	uint n_entries, bool big_endian);

enumError CreateNARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool is_le);

enumError CreateDARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError CreatePAC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError CreateRARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError CreateGFA (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError CreateNCCARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#include "lib-at7.h"

#include "lib-fsys.h"

enumError ExtractRST (nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *car_data,
	uint car_size, const u8 *toc_data, uint toc_size);

enumError CreateRST (u8 **dest_car, uint *dest_car_size, u8 **dest_toc, uint *dest_toc_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, bool compress, bool big_endian);

enumError ExtractTHP (
	nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *thp_data, uint thp_size);

#include "lib-gfa.h"
#include "lib-pac.h"

#include "lib-lzo.h"
#include "lib-gpkg.h"
#include "lib-gpak.h"
#include "lib-bns.h"
#include "lib-rpak.h"
#include "lib-lspk.h"

enumError DecodeZlibGrow (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
int IsZlib (cvp data, uint size);

//-----------------------------------------------------------------------------
// Fallback scan for self-describing NW4R layout resources (RLYT/RLAN)
// embedded inside an otherwise-opaque blob with no container of its own.
//
// Found while investigating We Ski's `DATA/files/SKI.DAT` (368MB, a fully
// custom in-house Namco engine format -- "Map::CMapSki" per the executable's
// own C++ symbols -- with no public documentation, no QuickBMS script, and
// no discoverable offset/size table for its bulk content after extensive
// static analysis; cracking the real course/terrain data would need dynamic
// analysis, e.g. a Dolphin memory-read breakpoint on the loader, which is
// out of reach here). That bulk content stays unrecovered. But scattered
// through the same file are genuine standalone NW4R menu-layout resources
// (RLYT/RLAN -- the exact same self-describing header this codebase already
// parses for standalone .brlyt/.brlan files: 4-byte magic, 0xFEFF BOM,
// version, and -- critically -- the resource's own total byte length) with
// no wrapping container at all, just placed back-to-back with other,
// unidentified data. Since each one carries its own length, no table is
// needed to recover them: scan for the magic, validate the header, and the
// length field says exactly how much to copy out.
//
// Deliberately conservative: requires the BOM, a byte-length that resolves
// in-bounds, and a header_size of exactly 0x10 (the only value ever seen in
// this codebase's own encoder/decoder) before accepting a candidate, and the
// caller additionally requires at least one hit before treating the file as
// a real match -- a handful of false-positive coincidental 4-byte matches
// in 368MB of other content is expected and just quietly skipped.
typedef struct nw4r_embedded_entry_t
{
	const u8 *data;
	u32 size;
	bool is_brlan; // false: RLYT (.brlyt); true: RLAN (.brlan)
} nw4r_embedded_entry_t;

typedef struct nw4r_embedded_t
{
	nw4r_embedded_entry_t *entries; // owned
	uint n_entries;
} nw4r_embedded_t;

void ResetEmbeddedNW4R (nw4r_embedded_t *found);
void ScanEmbeddedNW4R (nw4r_embedded_t *found, const u8 *data, uint size);

#include "lib-warc.h"

#include "lib-nccarc.h"
// Byte Pair Encoding, the other GFCP comtype.
enumError DecodeBPE (u8 *dest, uint dest_size, const u8 *src, uint src_size);

//-----------------------------------------------------------------------------
// Arika archives: a "rom:\INFO.DAT" / "rom:\GAME.DAT" file pair used by
// Arika's DS/DSi titles -- Dr. Mario Online Rx, Dr. Mario Express, the
// original (DS) Endless Ocean, and (per its own non-encrypted variant, see
// below) Endless Ocean: Blue World.  INFO.DAT holds an obfuscated directory
// table; GAME.DAT holds the member payloads, each stored either raw or
// ALZ1-compressed.  Layout and algorithms per GBATEK's "DS Encrypted Arika
// Archives with ALZ1 compression" page.
//
// INFO.DAT header (all fields little-endian):
//   000h 10h   Title (e.g. "*Dr.Mario-DSi!!!"), doubles as the decryption
//              key; a title starting with 00h marks the file unencrypted --
//              this is also how Endless Ocean: Blue World's *ARK variant
//              (whose first 4 bytes are the magic 00h 'A' 'R' 'K') ends up
//              handled for free by the same decrypt-or-not check.
//   010h 14h   Zerofilled
//   024h 4     Sector size (0 defaults to 800h)
//   028h 4     Unknown, reportedly a version field
//   02Ch 4     Number of directory entries
//   030h N*30h Directory entries
// Directory entry (30h bytes):
//   000h 20h   Filename (ASCII, zero-padded; unused slots are all-zero)
//   020h 4     Size on disk in GAME.DAT, in bytes (== decompressed size when
//              the entry is stored raw)
//   024h 4     Offset in GAME.DAT, in sector units
//   028h 4     Size in sector units (informational, not needed to extract)
//   02Ch 4     Decompressed size in bytes
//
// Decryption (applied to bytes [10h, filesize) only, when byte 0 != 0):
//   buf[i] = ((buf[i] ror 4) xor FFh) - buf[i AND 0Fh]
// where the subtracted byte is always one of the untouched 16 title/key
// bytes at the very start of the file (the loop never touches those).

enumError DecryptArikaInfo (u8 *buf, uint size);

// Inverse of DecryptArikaInfo, derived algebraically from GBATEK's decrypt
// formula (nibble-swap is its own inverse, so only the add/subtract and the
// two xor's need un-doing): buf[i] = ror4( (buf[i] + key[i&0xF]) xor FFh ).
// A no-op when buf[0..0xF] (the key/title) starts with a 00h byte, matching
// the "unencrypted" convention DecryptArikaInfo reads on the other side.
enumError EncryptArikaInfo (u8 *buf, uint size);

// Arika ALZ1 compression: near-identical to classic LZSS (4096-byte ring
// buffer, initial write pointer FEEh, matches encoded as [dict_lsb,
// dict_msb<<4|(len-3)] with len 3..18), except the flag byte is inverted
// (0=compressed) and consumed LSB-first. Operates on the bitstream *after*
// the 4-byte "ALZ1" magic (or the 8-byte "ZALZ" header used by a couple of
// older titles, which shares the same bitstream per aluigi's arika.bms) --
// callers skip that header themselves, same convention as DecodeLZ10Raw.
enumError DecodeALZ1 (u8 *dest, uint dest_size, const u8 *src, uint src_size);
enumError EncodeALZ1 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

// Extracts both INFO.DAT and GAME.DAT into a flat entry list. Entries whose
// raw (uncompressed) GAME.DAT slice itself starts with an "RF2" tag are
// recursively split into their own sub-entries -- a nested grouping table
// Endless Ocean: Blue World uses for related assets (e.g. a character's rig
// plus its sub-meshes); see ScanArika()'s definition for the RF2 layout.
// Unrecognised/out-of-range entries are skipped rather than aborting the
// whole archive.
enumError ExtractArika (nintendo_sarc_entry_t **out_entries, uint *out_n_entries,
	const u8 *info_data, uint info_size, const u8 *game_data, uint game_size);

// Builds a fresh INFO.DAT/GAME.DAT pair from a flat entry list (no RF2
// re-nesting -- entries are written back exactly as given). TITLE becomes
// the 16-byte INFO.DAT key; pass one starting with a non-NUL byte to get an
// encrypted archive, or NULL/an empty string for an unencrypted one. When
// COMPRESS is set, each member is ALZ1-encoded and only kept compressed if
// that's actually smaller than storing it raw.
enumError CreateArika (u8 **dest_info, uint *dest_info_size, u8 **dest_game, uint *dest_game_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, ccp title, bool compress);

//-----------------------------------------------------------------------------
// DARC ("darc" magic): the 3DS/NW4C "differential archive" container --
// structurally the CTR-era counterpart of the Wii's U8/SARC format (same
// idea: a flat table of file/folder nodes plus a name blob), but with its
// own header shape and a parent-index/end-index tree instead of SARC's
// hash-based node table. Real titles use it to bundle a whole family of
// related BFLYT/BFLAN layouts into one romfs file (e.g. Tomodachi Life's
// romfs/layout/*.bin -- confirmed on a real retail disc, CTR-P-EC6E).
//
// Layout verified byte-for-byte against a real sample plus two independent
// docs (GBATEK's 3DS-files-archive-darc page, 3dbrew's DARC page) and one
// working reference implementation (Tyulis/3DSkit's unpack/DARC.py) -- all
// three agree on the essential offsets:
//   Header (0x1C bytes), all fields little-endian:
//     0x00 char[4] magic "darc"
//     0x04 u16     BOM (bytes FF FE -> 0xFEFF read LE)
//     0x06 u16     header_size (0x001C)
//     0x08 u32     version
#include "lib-darc.h"

#include "lib-byml.h"

#include "lib-narc.h"

#include "lib-jarc.h"
#include "lib-sound-archive.h"

//-----------------------------------------------------------------------------
///////////////	  QuickBMS-derived flat archive ports		///////////////
//-----------------------------------------------------------------------------
// The scanners below all produce a flat, malloc-owned entry list (both the
// name and the payload of every entry are owned copies, because several of
// these formats compress their members and cannot point into the source
// buffer).  Free with ResetOwnedEntries().

void ResetOwnedEntries (nintendo_sarc_entry_t *entries, uint n_entries);
bool OwnedEntryAdd (
	nintendo_sarc_entry_t *entries, uint idx, ccp name, const u8 *data, uint size);
bool OwnedNameOk (ccp name);

#include "lib-sfzdat.h"
#include "lib-bg4.h"
#include "lib-hwl.h"
#include "lib-cram.h"
#include "lib-sze.h"
#include "lib-rflres.h"
#include "lib-sa01.h"
#include "lib-msr.h"

#include "lib-dtls.h"

#endif
