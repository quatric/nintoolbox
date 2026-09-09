#ifndef SZS_LIB_NDS_BANNER_H
#define SZS_LIB_NDS_BANNER_H 1

#include "types.h"

// The Nintendo DS ROM banner ("banner.bin", the block the ROM header's
// icon/title offset points at, and what ndstool -b/-t extracts): a 32x32
// 4bpp icon plus one UTF-16LE title per supported language. DSi-enhanced
// titles extend it with an 8-frame animated icon.
//
// Layout per GBATEK ("DS Cartridge Icon/Title"), cross-checked against
// ndstool's banner_t (ndstool/source/banner.h) and DS-Homebrew/twlmenu's
// reader. All multi-byte fields are little endian:
//   0x0000  u16 version: 1 = 6 titles, 2 = +Chinese, 3 = +Korean,
//           0x103 = version 3 plus the DSi animated icon
//   0x0002  u16 CRC16 over 0x0020..0x083f  (always present)
//   0x0004  u16 CRC16 over 0x0020..0x093f  (version >= 2)
//   0x0006  u16 CRC16 over 0x0020..0x0a3f  (version >= 3)
//   0x0008  u16 CRC16 over 0x1240..0x23bf  (version 0x103)
//   0x000a  0x16 reserved bytes
//   0x0020  0x200 icon bitmap: 4bpp, 8x8 tiles, 4 tiles per row
//   0x0220  0x20  icon palette: 16 x BGR555, entry 0 is transparent
//   0x0240  0x100 per language, in NDS_BANNER_LANG_* order
//   0x0a40  0x800 reserved
//   0x1240  0x1000 8 animated-icon bitmaps (0x200 each)
//   0x2240  0x100  8 animated-icon palettes (0x20 each)
//   0x2340  0x80   64 animation tokens (u16 each)
//   0x23c0  end of a DSi animated banner
//
// The CRC16 is the standard reflected CRC-16/MODBUS (poly 0xa001, init
// 0xffff) Nintendo uses throughout the DS header; because the banner has no
// magic of its own, that checksum is what makes content-based detection
// (IsNDSBanner()) trustworthy rather than a guess about a u16 version field.

#define NDS_BANNER_SIZE_V1 0x840
#define NDS_BANNER_SIZE_V2 0x940
#define NDS_BANNER_SIZE_V3 0xa40
#define NDS_BANNER_SIZE_DSI 0x23c0

#define NDS_BANNER_ICON_DIM 32
#define NDS_BANNER_BITMAP_SIZE 0x200
#define NDS_BANNER_PALETTE_SIZE 0x20
#define NDS_BANNER_TITLE_SIZE 0x100
#define NDS_BANNER_MAX_TITLES 8
#define NDS_BANNER_DSI_FRAMES 8
#define NDS_BANNER_DSI_SEQ_LEN 64

// Passed as DecodeNDSBannerIcon_RGBA()'s FRAME to ask for the static icon
// rather than an animation step.
#define NDS_BANNER_ICON_STATIC (~(uint)0)

typedef enum nds_banner_lang_t
{
	NDS_BANNER_LANG_JAPANESE,
	NDS_BANNER_LANG_ENGLISH,
	NDS_BANNER_LANG_FRENCH,
	NDS_BANNER_LANG_GERMAN,
	NDS_BANNER_LANG_ITALIAN,
	NDS_BANNER_LANG_SPANISH,
	NDS_BANNER_LANG_CHINESE, // version >= 2
	NDS_BANNER_LANG_KOREAN, // version >= 3
	NDS_BANNER_LANG__N
} nds_banner_lang_t;

// One decoded animation token. A DSi animated icon plays these in order
// until a token with `duration == 0`, then repeats from the start.
typedef struct nds_banner_frame_t
{
	u8 duration; // in 1/60 s units; 0 terminates the sequence
	u8 bitmap; // index into bitmap[] (0..7)
	u8 palette; // index into palette[] (0..7)
	bool flip_h;
	bool flip_v;
} nds_banner_frame_t;

typedef struct nds_banner_t
{
	u16 version;
	uint size; // the size this version defines
	uint n_titles; // 6, 7 or 8, by version
	bool animated; // version 0x103 and the animated CRC16 checks out

	// malloc'd UTF-8, never NULL for i < n_titles (may be empty). The DS
	// stores a title as up to 3 newline-separated lines: game name, optional
	// second line, publisher -- kept verbatim, newlines included.
	ccp title[NDS_BANNER_MAX_TITLES];

	// All of these point into the buffer passed to ScanNDSBanner(), which
	// must outlive the nds_banner_t.

	// The classic single icon at 0x20/0x220: what a DS -- and every non-DSi
	// reader, including the DSi's own "DS mode" -- shows, for every version.
	// Always set for a valid banner, animated or not.
	const u8 *static_bitmap;
	const u8 *static_palette;

	// The 8 DSi animated-icon bitmap/palette slots at 0x1240/0x2240. Only
	// valid when `animated` is true; a frame names one of each independently
	// (see nds_banner_frame_t), and neither is a variation on static_bitmap/
	// static_palette above -- they're a wholly separate image pair, often
	// drawn or colored differently for the DSi's animated HOME Menu tile.
	const u8 *bitmap[NDS_BANNER_DSI_FRAMES];
	const u8 *palette[NDS_BANNER_DSI_FRAMES];
	uint n_bitmaps; // 0, or 8 when animated
	nds_banner_frame_t frame[NDS_BANNER_DSI_SEQ_LEN];
	uint n_frames; // 0 unless animated
} nds_banner_t;

// True if DATA looks like an NDS banner: a known version whose own CRC16
// matches. Cheap enough to call from file-type detection.
bool IsNDSBanner (const u8 *data, uint size);

// Parses DATA into BANNER. Fails unless IsNDSBanner() would accept it.
enumError ScanNDSBanner (nds_banner_t *banner, const u8 *data, uint size);

// Frees the owned title strings; safe on a zeroed nds_banner_t.
void ResetNDSBanner (nds_banner_t *banner);

// Decodes one 32x32 icon to tightly packed RGBA8, palette entry 0 fully
// transparent. FRAME is NDS_BANNER_ICON_STATIC for the classic static icon
// at 0x20/0x220 (what a DS -- and every non-DSi reader -- shows, and the
// only icon a non-animated banner has), or an index into banner->frame[]
// for one step of a DSi animated icon, which is a separate bitmap+palette
// pair pulled from banner->bitmap[]/palette[] and generally does NOT match
// the static icon. Caller FREEs *dest.
enumError DecodeNDSBannerIcon_RGBA (
	u8 **dest, uint *width, uint *height, const nds_banner_t *banner, uint frame);

// Renders a human-readable summary (version, every non-empty title, and the
// animation sequence if present) into a malloc'd, NUL-terminated buffer.
// Caller FREEs the result.
char *TextNDSBanner (const nds_banner_t *banner);

#endif
