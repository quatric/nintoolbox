// Nintendo-format registry and bounded decoders added by the Nintendo fork.
#ifndef SZS_LIB_NINTENDO_H
#define SZS_LIB_NINTENDO_H 1

#include "types.h"

typedef enum nfmt_type_t
{
    NFMT_UNKNOWN,
    NFMT_DSB, NFMT_TPL, NFMT_STPL, NFMT_SARC,
    NFMT_LZ10, NFMT_LZ11, NFMT_HUFF4, NFMT_HUFF8, NFMT_RL, NFMT_ASH0, NFMT_YAY0, NFMT_LZH8,
    NFMT_BFLIM, NFMT_BCLIM, NFMT_BNR, NFMT_NCGR, NFMT_NCLR, NFMT_NCER, NFMT_NANR,
    NFMT_BRFNT, NFMT_BRFNA, NFMT_BCFNT, NFMT_BRLAN, NFMT_BRLYT,
    NFMT_BFLAN, NFMT_BFLYT, NFMT_BCLAN, NFMT_BCLYT,
    NFMT_PLT0,
    NFMT_MSBT, NFMT_BCRES, NFMT_BFRES, NFMT_BNTX, NFMT_GFA, NFMT_BCH, NFMT_QLZ,
    NFMT_PAC,
    NFMT_RNC, NFMT_ROMC, NFMT_PSDK, NFMT_AT7, NFMT_CTPK,
    NFMT_BYML, NFMT_NARC,
    NFMT_NSCR,
    NFMT_FZIP
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
nfmt_info_t DetectNintendoFormat ( const void *data, uint size, ccp filename );
ccp GetNintendoFormatName ( nfmt_type_t type );

// All decode functions allocate *DEST with malloc(); release it with free().
// Return 0 on success, EINVAL for malformed input, EFBIG for unsafe sizes.
enumError DecodeCamelot ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeCamelot ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeLZ10LZ11 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeNintendoRL ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeNintendoHuff ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeNintendoHuff
(
    u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool four_bit
);
enumError EncodeNintendoRL ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError DecodeASH0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeASH0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// Encode with the standard Nintendo LZ10 or LZ11 framing.  LZ11 uses the
// short token form where possible, so the output is accepted by both the DS
// and 3DS SDK decoders without relying on a host-side compressor.
enumError EncodeLZ10LZ11
(
    u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool lz11
);
enumError DecodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );

// BLZ ("backward LZSS", DS ARM9/ARM7/overlay compression). No header magic
// to detect by -- only call this where the caller already knows the file
// might be BLZ (e.g. an ndstool-staged arm9.bin/arm7.bin/overlay), not from
// generic format-dispatch code. A file whose footer says "not coded"
// (BLZ_Encode() left it uncompressed) decodes to the input verbatim,
// matching the real reference tool's own behavior.
enumError DecodeBLZ ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeBLZ ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeYay0 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// LZH8 (0x40) compression, used by Wii Virtual Console titles.  The decoder
// also accepts the WarioWare Snapped variant with a 4-byte LE size prefix.
enumError DecodeLZH8 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeLZH8 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// AT7 (AT7P/AT7X/AT7E) compression, used by Pokémon Mystery Dungeon WiiWare titles.
enumError DecodeAT7 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeAT7 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// FZIP (Game & Wario, Wii U) zlib-based compression
enumError DecodeFZIP ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeFZIP ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// RWAV (Wii)/FWAV (Wii U/Switch)/CWAV (3DS) NintendoWare wave audio -> PCM WAV.
// PCM8/PCM16/DSP-ADPCM/IMA-ADPCM, mono or multi-channel (non-interleaved
// source streams are interleaved into the WAV output).
enumError DecodeBXWAV ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
// RNC (Rob Northen Compression), RNC1/RNC2 methods. Decodes the 18-byte
// framed stream; keyed (encrypted) streams are rejected with EINVAL.
enumError DecodeRNC ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );
enumError EncodeRNC ( u8 **dest, uint *dest_size, const u8 *src, uint src_size, int method );

// Decode Animal Crossing: Wild World TXTR/DSB A3I5 textures to tightly packed
// RGBA8 pixels. WIDTH and HEIGHT are populated on success.
enumError DecodeDSB_RGBA
(
    u8 **dest, uint *width, uint *height,
    const u8 *src, uint src_size
);

// Encode the AC:WW 128x128 TXTR layout: a 32-colour RGB555 palette followed
// by A3I5 texels.
enumError EncodeDSB_RGBA ( u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height );

// Decode the 96x32 RGB5A3 icon embedded at offset 0x20 in Wii BNR1/BNR2
// banner files. The caller owns *DEST on success.
enumError DecodeBNR_RGBA ( u8 **dest, const u8 *src, uint src_size );
// Create a complete BNR1 banner with a 96x32 RGB5A3 icon and zero-filled
// textual metadata fields.  BNR1 is accepted by Wii/GameCube banner readers.
enumError EncodeBNR_RGBA ( u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height );
// Encode an RGBA sheet to a Wii bitmap font (BRFNT / RFNT) with FINF/TGLP/CWDH/CMAP.
enumError EncodeBRFNT_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height,
    uint cell_w, uint cell_h
);

enumError EncodeBRFNA_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height,
    uint cell_w, uint cell_h
);
// Encode an RGBA sheet to a 3DS/Wii U bitmap font (BCFNT / BFFNT).
enumError EncodeBCFNT_RGBA
(
    u8 **dest, uint *dest_size,
    const u8 *rgba, uint width, uint height,
    uint cell_w, uint cell_h, bool is_wiiu
);
// Decode NCGR tile data as a 16-tile-wide indexed grayscale sheet. A paired
// NCLR palette can be applied by the higher-level DS asset project layer.
enumError DecodeNCGR_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
);

enumError EncodeNCGR_RGBA
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool is_8bpp
);

// Decode a Nitro NCLR (RLCN/TTLP) palette into a readable RGBA8 swatch
// image. Each palette entry is an opaque BGR555 colour; entries are laid out
// in 16 columns with 8x8 pixel swatches.
enumError DecodeNCLR_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
);

enumError EncodeNCLR_RGBA
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height
);

// Bounded view of a Nitro NCER cell bank. Object attributes are the original
// six-byte DS OAM records and remain owned by the NCER file buffer.
typedef struct nintendo_ncer_t
{
    const u8 *data, *cells, *objects;
    uint size, n_cells, cell_size, objects_size;
    uint mapping_mode;
}
nintendo_ncer_t;

enumError ScanNCER ( nintendo_ncer_t *ncer, const u8 *data, uint size );
enumError GetNCERCell
(
    const nintendo_ncer_t *ncer, uint index, uint *n_objects,
    const u8 **oam_records
);

// Bounded view of a Nitro NANR animation bank. Frame records and cell indices
// remain pointers into the original input data.
typedef struct nintendo_nanr_t
{
    const u8 *data, *animations, *frames, *frame_data;
    uint size, n_animations, n_frames, frames_size, frame_data_size;
}
nintendo_nanr_t;

enumError ScanNANR ( nintendo_nanr_t *nanr, const u8 *data, uint size );
enumError GetNANRAnimation
(
    const nintendo_nanr_t *nanr, uint index, uint *n_frames,
    const u8 **frame_records
);

// Decode the common BFLIM/BCLIM trailing-footer layout.  The uncompressed
// formats R8, RGB565, RGBA5551, RGBA4 and RGBA8 are accepted in both linear
// and 8x8 Morton-swizzled order.  Unsupported GPU-compressed formats return
// EINVAL rather than producing pixels with a bogus alpha channel.
enumError DecodeFLIM_RGBA
(
    u8 **dest, uint *width, uint *height, const u8 *src, uint src_size
);
// Write a little-endian, RGBA8, 8x8-Morton-swizzled BFLIM or BCLIM.  The
// encoder deliberately uses the shared portable subset understood by the
// matching decoder and common CTR tooling.
enumError EncodeFLIM_RGBA
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height,
    bool bclim
);

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
}
nintendo_ctpk_entry_t;

typedef struct nintendo_ctpk_t
{
    const u8 *data;
    uint size;
    uint version;
    uint n_entries;
    uint texture_offset;
    uint texture_size;
}
nintendo_ctpk_t;

enumError ScanCTPK ( nintendo_ctpk_t *ctpk, const u8 *data, uint size );
enumError GetCTPKEntry
(
    const nintendo_ctpk_t *ctpk, uint index, nintendo_ctpk_entry_t *entry
);
enumError DecodeCTPKTexture_RGBA
(
    u8 **dest, uint *width, uint *height, const nintendo_ctpk_entry_t *entry
);
enumError DecodePicaTexture
(
    u8 **dest, uint *width, uint *height,
    const u8 *src, uint w, uint h, uint format, uint src_size
);
enumError EncodeCTPK
(
    u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name
);

typedef struct nintendo_sarc_t
{
    const u8 *data;
    uint size, data_offset, sfnt_offset, entries_offset, n_entries;
    bool big_endian;
}
nintendo_sarc_t;

// Parse either SARC byte order without allocating.  Entry pointers returned
// by GetSARCEntry remain owned by the original input buffer.
enumError ScanSARC ( nintendo_sarc_t *sarc, const u8 *data, uint size );
enumError GetSARCEntry
(
    const nintendo_sarc_t *sarc, uint index, ccp *name,
    const u8 **data, uint *size
);

typedef struct nintendo_sarc_entry_t
{
    ccp name;
    const u8 *data;
    uint size;
}
nintendo_sarc_entry_t;

// Build a canonical SARC with a 0x100-byte aligned data section.  ENTRIES are
// sorted by the standard 0x65 SFAT hash; the returned buffer is malloc-owned.
enumError CreateSARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
    uint n_entries, bool big_endian
);

enumError CreateNARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
    uint n_entries, bool is_le
);

enumError CreateDARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError CreatePAC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError CreateRARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError CreateGFA
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError CreateNCCARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError CreateAT7
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool compress
);

enumError CreateCTPK
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

enumError ExtractRST
(
    nintendo_sarc_entry_t **out_entries, uint *out_n_entries,
    const u8 *car_data, uint car_size,
    const u8 *toc_data, uint toc_size
);

enumError CreateRST
(
    u8 **dest_car, uint *dest_car_size,
    u8 **dest_toc, uint *dest_toc_size,
    const nintendo_sarc_entry_t *entries, uint n_entries,
    bool compress, bool big_endian
);

enumError ExtractTHP
(
    nintendo_sarc_entry_t **out_entries, uint *out_n_entries,
    const u8 *thp_data, uint thp_size
);

//-----------------------------------------------------------------------------
// GFA (Good-Feel archive, "GFAC" magic): the container used by Good-Feel's
// Wii titles -- Wario Land: Shake It! / The Shake Dimension, Kirby's Epic
// Yarn. A GFAC header plus a name/offset table, whose payload is one
// "GFCP"-compressed blob holding every member file back to back.

typedef struct gfa_entry_t
{
    ccp  name;    // points into the gfa_t name storage
    u32  offset;  // offset within the decompressed blob
    u32  size;    // 0 marks a directory entry
}
gfa_entry_t;

typedef struct gfa_t
{
    u8          *blob;      // decompressed payload (owned)
    uint        blob_size;
    gfa_entry_t *entries;   // owned
    uint        n_entries;
    char        *names;     // owned name storage
    uint        compression; // GFCP type: 1=BPE, 2/3=raw LZ10
}
gfa_t;

void      ResetGFA ( gfa_t *gfa );
enumError ScanGFA  ( gfa_t *gfa, const u8 *data, uint size );

// Raw (headerless) LZ10 as used by GFCP: the stream starts directly with the
// block data, so the output size has to be supplied by the caller.
enumError DecodeLZ10Raw ( u8 *dest, uint dest_size, const u8 *src, uint src_size );
enumError EncodeLZ10Raw ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );

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
    u16      type;    // ARCFileType: 1=Misc 2=Model 3=Texture 4=Animation
                       // 5=Scene 6=Type6 7=GroupedArchive 8=Effect
    u16      index;
    u8       group_index;
    s16      redirect_index; // -1 (0xffff) if this entry owns its own data
    char     name[16]; // optional creator extension in otherwise-reserved bytes
    const u8 *data;   // points into the source buffer; NULL when redirected
    u32      size;
}
pac_entry_t;

typedef struct pac_t
{
    const u8    *data;
    uint        size;
    char        name[48]; // archive's embedded name, e.g. "FitPeach"
    pac_entry_t *entries;  // owned
    uint        n_entries;
}
pac_t;

void      ResetPAC ( pac_t *pac );
enumError ScanPAC  ( pac_t *pac, const u8 *data, uint size );

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
    char     name[0x21]; // 32 bytes + guaranteed NUL
    const u8 *data;      // points into gpkg_t.data; NULL if out-of-range (skipped)
    u32      size;
}
gpkg_entry_t;

typedef struct gpkg_t
{
    u8           *data;       // decompressed buffer, owned (malloc'd)
    uint         size;
    gpkg_entry_t *entries;    // owned
    uint         n_entries;
}
gpkg_t;

void      ResetGPKG ( gpkg_t *pkg );

// Returns ERR_NOTHING_TO_DO for anything that doesn't decompress+validate as
// this format (deliberately strict -- ".pkg" is used by countless unrelated
// formats industry-wide, so detection here is entirely structural: it must
// zlib-decompress cleanly AND pass the header/table sanity+byte-accounting
// checks below, not just "any .pkg file").
enumError ScanGPKG ( gpkg_t *pkg, const u8 *data, uint size );

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
    const u8 *data;   // points into gpak_t.data; NULL if out-of-range (skipped)
    u32      size;
}
gpak_entry_t;

typedef struct gpak_t
{
    const u8      *data;      // NOT owned -- points into caller's buffer
    uint          size;
    gpak_entry_t  *entries;   // owned
    uint          n_entries;
}
gpak_t;

void      ResetGPAK ( gpak_t *pak );

// Deliberately strict like ScanGPKG() above: every single entry (not just a
// sample) must resolve in-bounds for the file to be accepted, since there is
// no magic to check and ".pak" is another industry-wide-overloaded
// extension.
enumError ScanGPAK ( gpak_t *pak, const u8 *data, uint size );

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
    const u8 *data;   // points into bns_t.data; NULL if out-of-range (skipped)
    u32      size;
}
bns_entry_t;

typedef struct bns_t
{
    const u8     *data;      // NOT owned -- points into caller's buffer
    uint         size;
    bns_entry_t  *entries;   // owned
    uint         n_entries;
}
bns_t;

void      ResetBNS ( bns_t *bns );

// Deliberately strict like ScanGPAK() above: every single entry (not just a
// sample) must resolve in-bounds for the file to be accepted.
enumError ScanBNS ( bns_t *bns, const u8 *data, uint size );

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
    char     *name;   // "<path>/<name>", owned name storage
    const u8 *data;   // points into the source buffer
    u32      size;
}
warc_entry_t;

typedef struct warc_t
{
    const u8     *data;
    uint         size;
    warc_entry_t *entries; // owned
    uint         n_entries;
    char         *names;   // owned name storage backing entries[].name
}
warc_t;

void      ResetWARC ( warc_t *warc );
enumError ScanWARC  ( warc_t *warc, const u8 *data, uint size );

// Build a WARC matching ScanWARC's layout. All entries share a single flat
// folder prefix, taken from the directory portion of entries[0].name (empty
// if it has none); any entry whose name doesn't start with that same prefix
// keeps its full name as-is (no folder stripped for it).
enumError CreateWARC
(
    u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries
);

//-----------------------------------------------------------------------------
// NCCARC: an undocumented flat blob-archive format used by WarioWare: Touched!
// (DS) for its "cg_*" graphics files. No magic, no public reference exists
// (a single unanswered forum thread is the entire prior art) -- this is a
// from-scratch, byte-verified container split only, not a pixel decoder for
// whatever's inside each chunk. See ScanNCCARC() in lib-nintendo.c for the
// verification detail.

typedef struct nccarc_entry_t
{
    const u8 *data;   // points into the source buffer
    u32      size;
    bool     flag;    // bit 31 of the raw table entry -- meaning unknown,
                       // preserved rather than discarded (see comment at
                       // ScanNCCARC's definition)
}
nccarc_entry_t;

typedef struct nccarc_t
{
    const u8       *data;
    uint           size;
    nccarc_entry_t *entries; // owned
    uint           n_entries;
}
nccarc_t;

void      ResetNCCARC ( nccarc_t *nc );
enumError ScanNCCARC  ( nccarc_t *nc, const u8 *data, uint size );
// Byte Pair Encoding, the other GFCP comtype.
enumError DecodeBPE ( u8 *dest, uint dest_size, const u8 *src, uint src_size );

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

enumError DecryptArikaInfo ( u8 *buf, uint size );

// Inverse of DecryptArikaInfo, derived algebraically from GBATEK's decrypt
// formula (nibble-swap is its own inverse, so only the add/subtract and the
// two xor's need un-doing): buf[i] = ror4( (buf[i] + key[i&0xF]) xor FFh ).
// A no-op when buf[0..0xF] (the key/title) starts with a 00h byte, matching
// the "unencrypted" convention DecryptArikaInfo reads on the other side.
enumError EncryptArikaInfo ( u8 *buf, uint size );

// Arika ALZ1 compression: near-identical to classic LZSS (4096-byte ring
// buffer, initial write pointer FEEh, matches encoded as [dict_lsb,
// dict_msb<<4|(len-3)] with len 3..18), except the flag byte is inverted
// (0=compressed) and consumed LSB-first. Operates on the bitstream *after*
// the 4-byte "ALZ1" magic (or the 8-byte "ZALZ" header used by a couple of
// older titles, which shares the same bitstream per aluigi's arika.bms) --
// callers skip that header themselves, same convention as DecodeLZ10Raw.
enumError DecodeALZ1 ( u8 *dest, uint dest_size, const u8 *src, uint src_size );
enumError EncodeALZ1 ( u8 **dest, uint *dest_size, const u8 *src, uint src_size );

// Extracts both INFO.DAT and GAME.DAT into a flat entry list. Entries whose
// raw (uncompressed) GAME.DAT slice itself starts with an "RF2" tag are
// recursively split into their own sub-entries -- a nested grouping table
// Endless Ocean: Blue World uses for related assets (e.g. a character's rig
// plus its sub-meshes); see ScanArika()'s definition for the RF2 layout.
// Unrecognised/out-of-range entries are skipped rather than aborting the
// whole archive.
enumError ExtractArika
(
    nintendo_sarc_entry_t **out_entries, uint *out_n_entries,
    const u8 *info_data, uint info_size,
    const u8 *game_data, uint game_size
);

// Builds a fresh INFO.DAT/GAME.DAT pair from a flat entry list (no RF2
// re-nesting -- entries are written back exactly as given). TITLE becomes
// the 16-byte INFO.DAT key; pass one starting with a non-NUL byte to get an
// encrypted archive, or NULL/an empty string for an unencrypted one. When
// COMPRESS is set, each member is ALZ1-encoded and only kept compressed if
// that's actually smaller than storing it raw.
enumError CreateArika
(
    u8 **dest_info, uint *dest_info_size,
    u8 **dest_game, uint *dest_game_size,
    const nintendo_sarc_entry_t *entries, uint n_entries,
    ccp title, bool compress
);

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
    bool  is_dir;
    u32   parent_or_offset; // dirs: parent index.   files: data offset.
    u32   end_or_size;      // dirs: end index.       files: data size.
    char  *name;            // owned, UTF-8; "" for the root entry
}
darc_entry_t;

typedef struct darc_t
{
    const u8     *data;   // not owned, borrowed from the caller's buffer
    uint         size;
    darc_entry_t *entries; // owned
    uint         n_entries;
}
darc_t;

void      ResetDARC ( darc_t *darc );
enumError ScanDARC  ( darc_t *darc, const u8 *data, uint size );

// BYML / BYAML (Binary YAML parameter format, 3DS / Wii U / Switch)
enumError DecodeBYML_YAML ( FILE *out, const u8 *data, size_t size );
enumError EncodeBYML_Text ( u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le, u16 version );

// NARC (Nitro Archive, DS / 3DS container)
typedef struct narc_entry_t
{
    char *name; // owned, UTF-8
    u32  offset; // relative to FIMG data
    u32  size;
} narc_entry_t;

typedef struct narc_t
{
    const u8     *raw;
    size_t       raw_size;
    bool         is_le;
    narc_entry_t *entries;
    uint         n_entries;
    const u8     *fimg_data;
    uint         fimg_size;
} narc_t;

void      ResetNARC ( narc_t *narc );
enumError ScanNARC  ( narc_t *narc, const u8 *data, size_t size );

//-----------------------------------------------------------------------------
// Sound Archive Family: BCSAR (3DS, "CSAR"), BFSAR (Wii U / Switch, "FSAR"),
//                       BCWAR ("CWAR"), BFWAR ("FWAR"), BCGRP ("CGRP"), BFGRP ("FGRP")

typedef struct sar_file_entry_t
{
    u32         file_id;
    char        *name;        // symbol name from STRG / PATRICIA tree, or NULL
    const u8    *data;        // points into source buffer
    u32         size;
    u32         offset;       // relative to FILE block payload (+8)
    u16         type_id;      // internal type id
    char        ext[8];       // e.g. ".bcseq", ".bfseq", ".bcbnk", ".bfbnk", ".bcwar", ".bfwar", ".bcwsd", ".bfwsd", ".bcgrp", ".bfgrp"
} sar_file_entry_t;

typedef struct sound_archive_t
{
    const u8            *raw;
    size_t              raw_size;
    bool                is_big_endian;
    bool                is_cafe_or_switch; // 'F' vs 'C'
    char                magic[5];          // "CSAR", "FSAR", "CWAR", "FWAR", "CGRP", "FGRP"
    u32                 version;
    sar_file_entry_t    *entries;
    uint                n_entries;
} sound_archive_t;

void      ResetSoundArchive ( sound_archive_t *sar );
enumError ScanSoundArchive  ( sound_archive_t *sar, const u8 *data, size_t size );
enumError CreateSoundArchive ( u8 **dest, uint *dest_size, const sound_archive_t *sar );

#endif
