// Nintendo-format registry and bounded decoders added by the Nintendo fork.
#ifndef SZS_LIB_NINTENDO_H
#define SZS_LIB_NINTENDO_H 1

#include "types.h"

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
	NFMT_SADL

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
nfmt_info_t DetectNintendoFormat (const void *data, uint size, ccp filename);
ccp GetNintendoFormatName (nfmt_type_t type);

// All decode functions allocate *DEST with malloc(); release it with free().
// Return 0 on success, EINVAL for malformed input, EFBIG for unsafe sizes.
enumError DecodeCamelot (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeCamelot (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
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

enumError DecodeNintendoRL (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeNintendoHuff (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeNintendoHuff (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool four_bit);
enumError EncodeNintendoRL (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
// Encode with the standard Nintendo LZ10 or LZ11 framing.  LZ11 uses the
// short token form where possible, so the output is accepted by both the DS
// and 3DS SDK decoders without relying on a host-side compressor.
enumError EncodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool lz11);
enumError EncodeMVDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

enumError DecodeYay0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

// BLZ ("backward LZSS", DS ARM9/ARM7/overlay compression). No header magic
// to detect by -- only call this where the caller already knows the file
// might be BLZ (e.g. an ndstool-staged arm9.bin/arm7.bin/overlay), not from
// generic format-dispatch code. A file whose footer says "not coded"
// (BLZ_Encode() left it uncompressed) decodes to the input verbatim,
// matching the real reference tool's own behavior.
enumError DecodeBLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeBLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeYay0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
// LZH8 (0x40) compression, used by Wii Virtual Console titles.  The decoder
// also accepts the WarioWare Snapped variant with a 4-byte LE size prefix.
enumError DecodeLZH8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeLZH8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
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
// RNC (Rob Northen Compression), RNC1/RNC2 methods. Decodes the 18-byte
// framed stream; keyed (encrypted) streams are rejected with EINVAL.
enumError DecodeRNC (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeRNC (u8 **dest, uint *dest_size, const u8 *src, uint src_size, int method);

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

// Bounded view of a Nitro NCER cell bank. Object attributes are the original
// six-byte DS OAM records and remain owned by the NCER file buffer.
typedef struct nintendo_ncer_t
{
	const u8 *data, *cells, *objects;
	uint size, n_cells, cell_size, objects_size;
	uint mapping_mode;
} nintendo_ncer_t;

enumError ScanNCER (nintendo_ncer_t *ncer, const u8 *data, uint size);
enumError GetNCERCell (
	const nintendo_ncer_t *ncer, uint index, uint *n_objects, const u8 **oam_records);

// Bounded view of a Nitro NANR animation bank. Frame records and cell indices
// remain pointers into the original input data.
typedef struct nintendo_nanr_t
{
	const u8 *data, *animations, *frames, *frame_data;
	uint size, n_animations, n_frames, frames_size, frame_data_size;
} nintendo_nanr_t;

enumError ScanNANR (nintendo_nanr_t *nanr, const u8 *data, uint size);
enumError GetNANRAnimation (
	const nintendo_nanr_t *nanr, uint index, uint *n_frames, const u8 **frame_records);

// Decode the common BFLIM/BCLIM trailing-footer layout.  The uncompressed
// formats R8, RGB565, RGBA5551, RGBA4 and RGBA8 are accepted in both linear
// and 8x8 Morton-swizzled order.  Unsupported GPU-compressed formats return
// EINVAL rather than producing pixels with a bogus alpha channel.
enumError DecodeFLIM_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);
// Write a little-endian, RGBA8, 8x8-Morton-swizzled BFLIM or BCLIM.  The
// encoder deliberately uses the shared portable subset understood by the
// matching decoder and common CTR tooling.
enumError EncodeFLIM_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool bclim);

// NUTEXB (Switch texture wrapper used by Smash Ultimate and other titles;
// distinct from BNTX even though both are Switch/Tegra containers -- see
// DecodeNUTEXB_RGBA in lib-nintendo.c for the layout and format-code
// mapping). Decodes texture data by reusing lib-bntx.c's already-verified
// deswizzle and block decoders; a texture whose NUTEXB format code has no
// BNTX equivalent (currently only R32G32B32A32_FLOAT) is reported as
// unsupported rather than guessed at.
enumError DecodeNUTEXB_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);
enumError EncodeNUTEXB_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name);

// CTPK (CTR Texture Package, 3DS container)
typedef struct nintendo_ctpk_entry_t
{
	char name[PATH_MAX];
	uint width;
	uint height;
	uint format;
	uint mip_level;
	uint type;
	const u8 *data;
	uint data_size;
} nintendo_ctpk_entry_t;

typedef struct nintendo_ctpk_t
{
	const u8 *data;
	uint size;
	uint version;
	uint n_entries;
	uint texture_offset;
	uint texture_size;
} nintendo_ctpk_t;

enumError ScanCTPK (nintendo_ctpk_t *ctpk, const u8 *data, uint size);
enumError GetCTPKEntry (const nintendo_ctpk_t *ctpk, uint index, nintendo_ctpk_entry_t *entry);
enumError DecodeCTPKTexture_RGBA (
	u8 **dest, uint *width, uint *height, const nintendo_ctpk_entry_t *entry);
enumError DecodePicaTexture (u8 **dest, uint *width, uint *height, const u8 *src, uint w, uint h,
	uint format, uint src_size);
enumError EncodeCTPK (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name);

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

typedef struct nintendo_sarc_entry_t
{
	ccp name;
	const u8 *data;
	uint size;
} nintendo_sarc_entry_t;

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

enumError CreateAT7 (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
	uint n_entries, bool compress);

enumError CreateCTPK (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError ScanFSYS (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

enumError CreateFSYS (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool compress);

enumError ExtractRST (nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *car_data,
	uint car_size, const u8 *toc_data, uint toc_size);

enumError CreateRST (u8 **dest_car, uint *dest_car_size, u8 **dest_toc, uint *dest_toc_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, bool compress, bool big_endian);

enumError ExtractTHP (
	nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *thp_data, uint thp_size);

//-----------------------------------------------------------------------------
// GFA (Good-Feel archive, "GFAC" magic): the container used by Good-Feel's
// Wii titles -- Wario Land: Shake It! / The Shake Dimension, Kirby's Epic
// Yarn. A GFAC header plus a name/offset table, whose payload is one
// "GFCP"-compressed blob holding every member file back to back.

typedef struct gfa_entry_t
{
	ccp name; // points into the gfa_t name storage
	u32 offset; // offset within the decompressed blob
	u32 size; // 0 marks a directory entry
} gfa_entry_t;

typedef struct gfa_t
{
	u8 *blob; // decompressed payload (owned)
	uint blob_size;
	gfa_entry_t *entries; // owned
	uint n_entries;
	char *names; // owned name storage
	uint compression; // GFCP type: 1=BPE, 2/3=raw LZ10
} gfa_t;

void ResetGFA (gfa_t *gfa);
enumError ScanGFA (gfa_t *gfa, const u8 *data, uint size);

// Raw (headerless) LZ10 as used by GFCP: the stream starts directly with the
// block data, so the output size has to be supplied by the caller.
enumError DecodeLZ10Raw (u8 *dest, uint dest_size, const u8 *src, uint src_size);
enumError EncodeLZ10Raw (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

//-----------------------------------------------------------------------------
// PAC ("ARC\0" magic): Super Smash Bros. Brawl's flat archive format,
// bundling a fighter/stage/UI's models, textures, animations (including
// embedded PLT0 palette-animation chunks) as uncompressed, unnamed, typed
// entries. Layout per BrawlLib's own struct definitions (libertyernie/
// BrawlCrate, BrawlLib/SSBB/Types/ARC.cs ARCHeader/ARCFileHeader) -- unlike
// GFA/SARC there is no compression and no per-entry filename, so members are
// exposed by index + a numeric ARCFileType (BrawlLib's ARCFileType enum).

typedef struct pac_entry_t
{
	u16 type; // ARCFileType: 1=Misc 2=Model 3=Texture 4=Animation
			  // 5=Scene 6=Type6 7=GroupedArchive 8=Effect
	u16 index;
	u8 group_index;
	s16 redirect_index; // -1 (0xffff) if this entry owns its own data
	char name[16]; // optional creator extension in otherwise-reserved bytes
	const u8 *data; // points into the source buffer; NULL when redirected
	u32 size;
} pac_entry_t;

typedef struct pac_t
{
	const u8 *data;
	uint size;
	char name[48]; // archive's embedded name, e.g. "FitPeach"
	pac_entry_t *entries; // owned
	uint n_entries;
} pac_t;

void ResetPAC (pac_t *pac);
enumError ScanPAC (pac_t *pac, const u8 *data, uint size);

//-----------------------------------------------------------------------------
// Gorilla Games' ".pkg" archive (WiiWare: Bonsai Barber, and presumably this
// studio's other WiiWare titles). No container magic at all -- the entire
// file is one standard zlib stream (2-byte header, Adler32 trailer; despite
// aluigi's bonsai_barber.bms script naming its comtype "unzip_dynamic",
// real samples carry a genuine zlib wrapper, not raw deflate) wrapping a
// flat header+table+data layout. Header (all fields big-endian, 20 bytes,
// offsets relative to the DECOMPRESSED buffer):
//   00h 4   unknown (always 8 on real samples)
//   04h 4   data_off -- see BASE_OFF below
//   08h 4   zero (always 0 on real samples)
//   0Ch 4   crc (unverified -- not needed to extract)
//   10h 4   n_entries
// Entry table starts at 14h, one 0x28-byte record per entry:
//   00h 20h  name (ASCII, zero-padded)
//   20h 4    offset (relative to BASE_OFF, not absolute)
//   24h 4    size
// BASE_OFF = decompressed_size - data_off; an entry's real, absolute offset
// into the decompressed buffer is offset + BASE_OFF. Layout and field order
// derived from aluigi's bonsai_barber.bms (mirror.aluigi.org/bms/), verified
// byte-exact against a real retail Bonsai Barber bb_text.pkg: all 97 real
// entries' name/offset/size fields resolve to real, non-overlapping,
// in-bounds slices with plausible names and sub-format magics (.bui UI
// layouts, tex/-prefixed style records, etc.) -- this only unpacks the
// container to named raw files, it does not decode those sub-formats.

typedef struct gpkg_entry_t
{
	char name[0x21]; // 32 bytes + guaranteed NUL
	const u8 *data; // points into gpkg_t.data; NULL if out-of-range (skipped)
	u32 size;
} gpkg_entry_t;

typedef struct gpkg_t
{
	u8 *data; // decompressed buffer, owned (malloc'd)
	uint size;
	gpkg_entry_t *entries; // owned
	uint n_entries;
} gpkg_t;

void ResetGPKG (gpkg_t *pkg);

// Returns ERR_NOTHING_TO_DO for anything that doesn't decompress+validate as
// this format (deliberately strict -- ".pkg" is used by countless unrelated
// formats industry-wide, so detection here is entirely structural: it must
// zlib-decompress cleanly AND pass the header/table sanity+byte-accounting
// checks below, not just "any .pkg file").
enumError ScanGPKG (gpkg_t *pkg, const u8 *data, uint size);

//-----------------------------------------------------------------------------
// 2D Boy's "master.pak" archive (WiiWare: World of Goo). No filenames are
// stored at all -- only a per-entry 32-bit hash, presumably looked up by the
// engine's resource manager by hashing the requested path string -- so
// members are extracted under ordinal names, same convention this codebase
// already uses for Pokémon-series FSYS archives whose entries are all
// literally named "(null)".
//
// All fields big-endian, no container magic. Header (16 bytes):
//   00h 4   n_entries
//   04h 4   unknown (constant across real samples, looks like a version tag)
//   08h 4   zero
//   0Ch 4   hash/checksum of something (unverified, not needed to extract)
// Followed by n_entries 16-byte rows (table starts right after the header,
// i.e. at byte 16, and spans through byte (n_entries+1)*16 -- entry data
// begins immediately after that, no padding):
//   00h 4   absolute offset into the file
//   04h 4   size
//   08h 4   unknown (not a simple flag -- 442 distinct values were observed
//           across 1731 real entries; not needed to extract)
//   0Ch 4   hash/checksum of something (unverified)
//
// Reverse-engineered from a real retail master.pak (no public tool or BMS
// script exists for this format) and verified structurally, not guessed:
// all 1731 real entries' offset+size resolve inside the file with zero
// out-of-range hits, and entry 0 is proven to genuinely be a header (not a
// truncated first entry) because its own "offset" field value (1731) is
// exactly the real entry count, while entry 1's offset field independently
// equals the byte length of the header+table region (n_entries+1)*16 --
// i.e. file data starts exactly where the table ends, corroborated two
// different ways from two different fields.

typedef struct gpak_entry_t
{
	const u8 *data; // points into gpak_t.data; NULL if out-of-range (skipped)
	u32 size;
} gpak_entry_t;

typedef struct gpak_t
{
	const u8 *data; // NOT owned -- points into caller's buffer
	uint size;
	gpak_entry_t *entries; // owned
	uint n_entries;
} gpak_t;

void ResetGPAK (gpak_t *pak);

// Deliberately strict like ScanGPKG() above: every single entry (not just a
// sample) must resolve in-bounds for the file to be accepted, since there is
// no magic to check and ".pak" is another industry-wide-overloaded
// extension.
enumError ScanGPAK (gpak_t *pak, const u8 *data, uint size);

//-----------------------------------------------------------------------------
// Koei Tecmo's "LINKDATA*.BNS" archive (Wii: Samurai Warriors 3). No magic,
// no filenames -- entries are extracted under ordinal names, same convention
// as GPAK above.
//
// All fields big-endian. Header (16 bytes):
//   00h 4   unknown (constant 0x19dad/105901 across all 5 real LINKDATA*.BNS
//           samples in one retail disc, incl. files with wildly different
//           sizes/entry counts -- looks like a fixed engine build constant,
//           not per-file data; not needed to extract)
//   04h 4   n_entries
//   08h 4   block_size (0x800 = 2048 in every sample seen)
//   0Ch 4   zero
// Followed by n_entries 8-byte rows (table starts right after the header,
// i.e. at byte 16):
//   00h 4   offset, in block_size units (real byte offset = value*block_size)
//   04h 4   size, in bytes
//
// Reverse-engineered from 5 real retail LINKDATA*.BNS files (no public tool
// or BMS script exists for this format) and verified structurally: every
// entry's offset*block_size+size resolves strictly in-bounds across all 5
// samples (entry counts 1, 18, 41, 45, and 5276), with zero out-of-range
// hits, and the last entry's end lands within one block of each file's true
// end (block-aligned tiling of the whole file, not a coincidental subset
// match).
typedef struct bns_entry_t
{
	const u8 *data; // points into bns_t.data; NULL if out-of-range (skipped)
	u32 size;
} bns_entry_t;

typedef struct bns_t
{
	const u8 *data; // NOT owned -- points into caller's buffer
	uint size;
	bns_entry_t *entries; // owned
	uint n_entries;
} bns_t;

void ResetBNS (bns_t *bns);

// Deliberately strict like ScanGPAK() above: every single entry (not just a
// sample) must resolve in-bounds for the file to be accepted.
enumError ScanBNS (bns_t *bns, const u8 *data, uint size);

//-----------------------------------------------------------------------------
// Retro Studios' ".pak" archive (Wii: Donkey Kong Country Returns and
// Metroid Prime Trilogy's Wii remaster). No public tool exists for the
// *original* Wii-era format specifically (later Tropical Freeze/Wii U .pak
// is a different, RFRM-based layout -- Aruki's PakTool targets that one,
// not this), but a dedicated open-source editor for exactly this game
// exists and fully documents the container: github.com/jellees/crPakTool
// (Form1.cs loadRSHDData()/ExportDecompress()). Layout transcribed from
// that tool's real, working code, not guessed, and independently verified
// byte-exact against a real retail MiscData.pak (112/112 entries resolve
// in-bounds).
//
// All fields big-endian. Header:
//   00h 4   version
//   08h 8   hash (unverified, not needed to extract)
//   48h 4   strg_length  -- length of the STRG (string table) section
//   50h 4   rshd_length  -- length of the RSHD (entry table) section
//   58h 4   data_length
//   80h 4   strg_count   -- STRG section starts here
// RSHD section starts at (0x80+strg_length): a u32 rshd_count (entry count)
// followed by rshd_count 24-byte rows:
//   00h 4   compressed  -- nonzero: entry payload is CMPD-wrapped (below);
//           zero: payload is the raw file data as-is
//   04h 4   magic       -- 4-char file-type tag, used as the extension
//   08h 4   file_id_hi  -- with file_id_lo, forms the 64-bit ordinal name
//   0Ch 4   file_id_lo
//   10h 4   data_length -- byte length of the payload as stored (before
//           CMPD decompression, if any)
//   14h 4   pointer     -- payload offset, relative to the end of the
//           header+STRG+RSHD region, i.e. absolute offset =
//           pointer + 0x80 + strg_length + rshd_length
//
// A compressed (CMPD-wrapped) payload is itself:
//   00h 4   "CMPD" magic
//   04h 4   block_count
// followed by block_count 8-byte block headers:
//   00h 1   unknown (a flag byte -- 0x00 on stored/raw blocks, 0xc0 on every
//           compressed block seen so far, but not verified as a bitmask)
//   01h 1   stored_size, top 8 bits  -- NOT part of the "unknown" byte: real
//           blocks over 64KB stored (common for texture-sized payloads) need
//           this, and re-deriving it from a real sample is what caught it --
//           reading only the big-endian u16 at 02h truncates the true stored
//           size, making the trailing zlib stream look prematurely
//           terminated (verified: 7 of 112 entries in a real MiscData.pak
//           have a compressed block over 0xffff bytes stored and decompress
//           correctly only with this 24-bit size)
//   02h 2   stored_size, low 16 bits
//   04h 4   uncompressed_size -- if stored_size==uncompressed_size the block
//           is stored raw (copy stored_size bytes verbatim); otherwise the
//           stored_size bytes are a standard zlib stream (with the normal
//           2-byte zlib header, not raw deflate) that decompresses to
//           exactly uncompressed_size bytes.
// Block payloads are concatenated in order immediately after the last block
// header (no separate offset table -- each block's bytes follow directly).
typedef struct rpak_entry_t
{
	const u8 *data; // points into rpak_t.data; NULL if out-of-range (skipped)
	u32 size; // stored size (as on disk, before CMPD decompression)
	u32 magic; // 4-char type tag, big-endian
	u32 id_hi;
	u32 id_lo;
	bool compressed; // CMPD-wrapped
} rpak_entry_t;

typedef struct rpak_t
{
	const u8 *data; // NOT owned -- points into caller's buffer
	uint size;
	rpak_entry_t *entries; // owned
	uint n_entries;
} rpak_t;

void ResetRPAK (rpak_t *pak);

// Deliberately strict like ScanGPAK()/ScanBNS() above: every single entry
// must resolve in-bounds for the file to be accepted.
enumError ScanRPAK (rpak_t *pak, const u8 *data, uint size);

// Decompress one CMPD-wrapped entry payload. DKCR blocks contain one zlib
// stream; Metroid Prime 2/3 blocks contain signed-size raw/zlib/LZO1X
// segments. Returns an ALLOC()'d buffer (caller FREE()s it) and sets
// *res_size, or returns NULL on any structural error (caller should then fall
// back to treating the entry as uncompressed raw data instead).
u8 *DecompressRPAKEntry (const u8 *data, uint size, uint *res_size);

// Clean-room decoder for the byte-aligned LZO1X stream used by Metroid
// Prime's CMPD segments. It is deliberately not linked to liblzo. On
// success, *dest is ALLOC()'d and contains the complete stream output.
enumError DecodeLZO1XGrow (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

// Plain zlib-stream decompress (standard 2-byte zlib header, not raw
// deflate), size unknown up front -- grows the output buffer as needed.
// Shared by GPKG/RPAK internally and exposed here for LSPK's zlib-tagged
// entries (magic byte 0x78) too.
enumError DecodeZlibGrow (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
int IsZlib (cvp data, uint size);

//-----------------------------------------------------------------------------
// Mistwalker's ".pk"/".pkh" archive pair (Wii: The Last Story). Layout
// transcribed from RGBA_CRT's open-source LSPK-Extracter (found on GitHub
// before any from-scratch RE, per project convention:
// github.com/RGBA-CRT/LSPK-Extracter, LSPK-lib.sbp's PKH_READER class),
// not guessed. A third sibling file, ".pfs", carries directory/filename
// metadata only -- not needed to recover the raw archived files, so it is
// deliberately not parsed here (ordinal names instead, same convention as
// GPAK/BNS/RPAK above).
//
// ".pkh" (the entry table) -- all fields big-endian:
//   00h 4   n_entries
// followed by n_entries fixed-size rows starting at byte 4. The row size
// is *not* fixed across games -- it's derived as (pkh_filesize-4)/n_entries
// and must come out to exactly 16 or 24 (any other value means this isn't
// really an LSPK archive):
//   16-byte row:                       24-byte row:
//     00h 4   hash                       00h 4   hash
//     04h 4   offset (into .pk)          04h 4   unused
//     08h 4   dec_size                   08h 4   offset_hi
//     0Ch 4   com_size                   0Ch 4   offset_lo (abs = hi<<32|lo)
//                                        10h 4   dec_size
//                                        14h 4   com_size
// In both layouts, com_size==0 means "not actually compressed": treat the
// payload as dec_size raw bytes instead (this is the source tool's own
// convention, not an inference).
//
// ".pk" (the payload blob) -- entry payloads are read directly from the
// resolved offset in this file, length com_size (or dec_size per the above
// rule). Compression is per-archive, not per-entry, auto-detected from the
// first payload byte: 0x10/0x11 is Nintendo-standard LZ10/LZ11 (this
// codebase's own DecodeLZ10LZ11 handles both), 0x78 is a plain zlib stream;
// anything else means the entry is stored uncompressed as-is.
typedef struct lspk_entry_t
{
	const u8 *data; // points into lspk_t.pk_data; NULL if out-of-range (skipped)
	u32 size; // com_size (payload length as stored in the .pk file)
	u32 dec_size; // expected decompressed size (0 if entry is not compressed)
	u32 hash;
} lspk_entry_t;

typedef struct lspk_t
{
	const u8 *pk_data; // NOT owned -- points into caller's .pk buffer
	uint pk_size;
	lspk_entry_t *entries; // owned
	uint n_entries;
} lspk_t;

void ResetLSPK (lspk_t *pak);

// Parses PKH_DATA (the ".pkh" file contents) and resolves each entry against
// PK_DATA/PK_SIZE (the sibling ".pk" file). Deliberately strict like
// ScanGPAK()/ScanBNS()/ScanRPAK() above: every single entry must resolve
// in-bounds for the file to be accepted.
enumError ScanLSPK (
	lspk_t *pak, const u8 *pkh_data, uint pkh_size, const u8 *pk_data, uint pk_size);

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

//-----------------------------------------------------------------------------
// WARC ("WARC" magic): Game & Wario (Wii U)'s flat archive format. Unlike
// SARC/GFA it is a *different* Monster Games/Nintendo container -- big
// endian, uncompressed (no QuickLZ despite the superficial similarity to
// Excite's TOC/RES), with real per-entry filenames but only a single flat
// folder-path prefix (no real directory tree -- the format itself only
// tracks one path string per archive, applied to every entry; see ScanWARC's
// definition for the layout, ported from aluigi's public game_wario.bms).

typedef struct warc_entry_t
{
	char *name; // "<path>/<name>", owned name storage
	const u8 *data; // points into the source buffer
	u32 size;
} warc_entry_t;

typedef struct warc_t
{
	const u8 *data;
	uint size;
	warc_entry_t *entries; // owned
	uint n_entries;
	char *names; // owned name storage backing entries[].name
} warc_t;

void ResetWARC (warc_t *warc);
enumError ScanWARC (warc_t *warc, const u8 *data, uint size);

// Build a WARC matching ScanWARC's layout. All entries share a single flat
// folder prefix, taken from the directory portion of entries[0].name (empty
// if it has none); any entry whose name doesn't start with that same prefix
// keeps its full name as-is (no folder stripped for it).
enumError CreateWARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

//-----------------------------------------------------------------------------
// NCCARC: an undocumented flat blob-archive format used by WarioWare: Touched!
// (DS) for its "cg_*" graphics files. No magic, no public reference exists
// (a single unanswered forum thread is the entire prior art) -- this is a
// from-scratch, byte-verified container split only, not a pixel decoder for
// whatever's inside each chunk. See ScanNCCARC() in lib-nintendo.c for the
// verification detail.

typedef struct nccarc_entry_t
{
	const u8 *data; // points into the source buffer
	u32 size;
	bool flag; // bit 31 of the raw table entry -- meaning unknown,
			   // preserved rather than discarded (see comment at
			   // ScanNCCARC's definition)
} nccarc_entry_t;

typedef struct nccarc_t
{
	const u8 *data;
	uint size;
	nccarc_entry_t *entries; // owned
	uint n_entries;
} nccarc_t;

void ResetNCCARC (nccarc_t *nc);
enumError ScanNCCARC (nccarc_t *nc, const u8 *data, uint size);
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
//     0x0C u32     file_size (total archive size)
//     0x10 u32     table_offset (start of the entry table; usually == 0x1C)
//     0x14 u32     table_size (entry table + name area, combined)
//     0x18 u32     data_offset (where file content starts; 32-byte aligned)
//   Entry (12 bytes each), table_size/12 of them, starting at table_offset:
//     +0x00 u32  bits 0-23 = name offset (from the name area, which starts
//                right after the last entry, i.e. table_offset + n*12);
//                bit 24 (0x01000000) = is-directory flag
//     +0x04 u32  files: absolute data offset from archive start.
//                dirs:  parent entry index
//     +0x08 u32  files: data length in bytes.
//                dirs:  end index (exclusive) -- this dir's descendants are
//                       every entry with index in (this_index, end_index)
//   Entry 0 is always the root directory; its end-index equals the total
//   entry count. Entry 1 is often a "." alias of the root and should be
//   skipped when reconstructing paths, same as GBATEK documents.
//   Names are UTF-16LE, NUL-terminated, stored in the name area.
// All of the above matched a real sample byte-for-byte: magic/BOM/header
// size/version(0x01000000)/table+data offsets and alignment, root entry's
// directory flag + end-index, and the "." alias entry's name offset.

typedef struct darc_entry_t
{
	bool is_dir;
	u32 parent_or_offset; // dirs: parent index.   files: data offset.
	u32 end_or_size; // dirs: end index.       files: data size.
	char *name; // owned, UTF-8; "" for the root entry
} darc_entry_t;

typedef struct darc_t
{
	const u8 *data; // not owned, borrowed from the caller's buffer
	uint size;
	darc_entry_t *entries; // owned
	uint n_entries;
} darc_t;

void ResetDARC (darc_t *darc);
enumError ScanDARC (darc_t *darc, const u8 *data, uint size);

// BYML / BYAML (Binary YAML parameter format, 3DS / Wii U / Switch)
enumError DecodeBYML_YAML (FILE *out, const u8 *data, size_t size);
enumError EncodeBYML_Text (
	u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le, u16 version);

// NARC (Nitro Archive, DS / 3DS container)
typedef struct narc_entry_t
{
	char *name; // owned, UTF-8
	u32 offset; // relative to FIMG data
	u32 size;
} narc_entry_t;

typedef struct narc_t
{
	const u8 *raw;
	size_t raw_size;
	bool is_le;
	narc_entry_t *entries;
	uint n_entries;
	const u8 *fimg_data;
	uint fimg_size;
} narc_t;

void ResetNARC (narc_t *narc);
enumError ScanNARC (narc_t *narc, const u8 *data, size_t size);

// JARC / jCMP (Ganbarion archive & compression container, Wii / Wii U / 3DS)
typedef struct jarc_entry_t
{
	char name[256];
	const u8 *data; // points into decompressed or uncompressed buffer
	u32 size;
	u32 offset;
	char ext[16];   // e.g. "jmdl", "jtex", "jmot", "jmsg", "jclt", "jefc", "bin"
} jarc_entry_t;

typedef struct jarc_t
{
	const u8 *raw;
	size_t raw_size;
	u8 *decomp_buffer; // owned, if jCMP decompressed
	uint decomp_size;
	bool is_big_endian;
	jarc_entry_t *entries; // owned array
	uint n_entries;
} jarc_t;

void ResetJARC (jarc_t *jarc);
enumError DecodeJCMP (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError ScanJARC (jarc_t *jarc, const u8 *data, size_t size);

//-----------------------------------------------------------------------------
// Sound Archive Family: BCSAR (3DS, "CSAR"), BFSAR (Wii U / Switch, "FSAR"),
//                       BCWAR ("CWAR"), BFWAR ("FWAR"), BCGRP ("CGRP"), BFGRP ("FGRP")

typedef struct sar_file_entry_t
{
	u32 file_id;
	char *name; // symbol name from STRG / PATRICIA tree, or NULL
	const u8 *data; // points into source buffer
	u32 size;
	u32 offset; // relative to FILE block payload (+8)
	u16 type_id; // internal type id
	char ext[8]; // e.g. ".bcseq", ".bfseq", ".bcbnk", ".bfbnk", ".bcwar", ".bfwar", ".bcwsd",
				 // ".bfwsd", ".bcgrp", ".bfgrp"
} sar_file_entry_t;

typedef struct sound_archive_t
{
	const u8 *raw;
	size_t raw_size;
	bool is_big_endian;
	bool is_cafe_or_switch; // 'F' vs 'C'
	char magic[5]; // "CSAR", "FSAR", "CWAR", "FWAR", "CGRP", "FGRP"
	u32 version;
	sar_file_entry_t *entries;
	uint n_entries;
} sound_archive_t;

void ResetSoundArchive (sound_archive_t *sar);
enumError ScanSoundArchive (sound_archive_t *sar, const u8 *data, size_t size);
enumError CreateSoundArchive (u8 **dest, uint *dest_size, const sound_archive_t *sar);

//-----------------------------------------------------------------------------
///////////////	  QuickBMS-derived flat archive ports		///////////////
//-----------------------------------------------------------------------------
// The scanners below all produce a flat, malloc-owned entry list (both the
// name and the payload of every entry are owned copies, because several of
// these formats compress their members and cannot point into the source
// buffer).  Free with ResetOwnedEntries().

void ResetOwnedEntries (nintendo_sarc_entry_t *entries, uint n_entries);

//-----------------------------------------------------------------------------
// Star Fox Zero DAT ("DAT\0", Wii U, big endian).  PlatinumGames' flat
// container: a 7-word header (magic, file count, and five section offsets)
// followed by five parallel per-file sections.  Layout ported from aluigi's
// public star_fox_zero_dat.bms; see ScanSFZDAT() for the record widths that
// the script's sequential string reads leave implicit.

enumError ScanSFZDAT (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// Inverse of ScanSFZDAT.  Entry names are "<ext>/<name>" exactly as the
// scanner emits them; an entry without a '/' gets its extension taken from
// its filename suffix.  The trailing hash-map section is emitted with a
// valid, empty structure -- see the comment at the definition.
enumError CreateSFZDAT (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

//-----------------------------------------------------------------------------
// BG4 ("BG4\0", 3DS, little endian): Mario & Luigi: Paper Jam / Paper Mario
// MIX.  Flat, named; a member whose table offset has bit 31 set is BLZ
// ("backward LZSS") compressed and is decompressed in place by the scanner.
// Layout ported from aluigi's public mario_luigi_paper.bms.

enumError ScanBG4 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

enumError CreateBG4 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

//-----------------------------------------------------------------------------
// Hyrule Warriors Legends (3DS): a split ".idx"/".bin" pair.  The .idx is a
// bare array of {u32 size, u32 offset} into the .bin with no header, no
// magic and no names; size==0 entries are holes and are skipped.  Layout
// ported from aluigi's public hyrule_warriors_legends.bms.

enumError ScanHWLegends (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *idx,
	uint idx_size, const u8 *bin, uint bin_size);

enumError CreateHWLegends (u8 **dest_idx, uint *dest_idx_size, u8 **dest_bin, uint *dest_bin_size,
	const nintendo_sarc_entry_t *entries, uint n_entries);

//-----------------------------------------------------------------------------
// Xenoblade Chronicles 3D ("cram", 3DS, little endian): flat, named,
// uncompressed.  Layout ported from aluigi's public xenoblade_arc.bms.

enumError ScanCramARC (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

enumError CreateCramARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

//-----------------------------------------------------------------------------
// SZE (Encrypted SZS / SARC / Zstd container used by F-Zero 99 and Switch titles).
// Wraps encrypted payload behind a 32-byte header with AES-128-CTR/CBC.

enumError DecodeSZE (
	u8 **dest, uint *dest_size, const u8 *data, uint size, const u8 key[16]);

enumError EncodeSZE (
	u8 **dest, uint *dest_size, const u8 *data, uint size, const u8 key[16], const u8 iv[16], uint mode);

//-----------------------------------------------------------------------------
// RFL_Res.dat / RFLRes.dat (Revolution Face Library / Wii Mii Resource archive).
// Hierarchical container of 18 sub-archives (shapes, textures, facelines, etc.)

enumError ScanRFLRes (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

enumError CreateRFLRes (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

//-----------------------------------------------------------------------------
// Mii Maker (Wii U, "SA01") and amiibo Settings (3DS, "CA01").  Both are a
// zlib payload -- Mii Maker behind a bare big-endian u32 uncompressed size,
// amiibo behind a "ZCMP" header with the payload at 0x80 -- wrapping a flat
// archive of three parallel arrays (offsets, sizes, and for SA01 only a
// 0x80-byte fixed-width name per file).  Layout ported from aluigi's public
// mii_maker.bms and amiibo.bms.  ScanSA01() takes the *inner*, already
// decompressed image; DecodeSA01Container() handles both outer wrappers and
// a bare inner image.

enumError ScanSA01 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError DecodeSA01Container (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

enumError CreateSA01 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries,
	bool compress, bool big_endian);

enumError CreateCA01 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries,
	bool compress, bool big_endian);

//-----------------------------------------------------------------------------
// Metroid: Samus Returns (3DS): a headerless {u32 info_size, u32 data_size,
// u32 files} + per-file {u32 crc, u32 offset, u32 end_offset} table with no
// magic and no names.  Layout ported from aluigi's public metroid_sr_3ds.bms.
// Because there is nothing to key detection off, ScanMetroidSR() enforces a
// deliberately strict set of self-consistency constraints; see its
// definition.

enumError ScanMetroidSR (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

//-----------------------------------------------------------------------------
// Smash 4 (Wii U / 3DS) DTLS resource / lookup archive (ls00 / dt00 / resource)
//-----------------------------------------------------------------------------

enumError ScanDTLS (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *ls_data, uint ls_size,
	const u8 *dt_data, uint dt_size);

enumError CreateDTLS (
	u8 **out_ls, uint *out_ls_size, u8 **out_dt, uint *out_dt_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, bool compress, bool big_endian);

#endif
