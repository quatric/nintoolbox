#ifndef SZS_LIB_WII_BANNER_H
#define SZS_LIB_WII_BANNER_H 1

#include "types.h"

//-----------------------------------------------------------------------------
// The Wii "banner family": the wrappers a Wii title's channel banner is built
// out of. A disc's /opening.bnr, a channel's 00000000.app content and a
// WiiWare title all use the same shape:
//
//   IMET header (0x600 bytes, with the title in 10 languages)
//     U8 archive
//       meta/  banner.bin  icon.bin  sound.bin
//                 each: IMD5 header (0x20) + optional "LZ77" wrapper
//                   U8 archive of BRLYT / BRLAN / TPL / BRSTM resources
//
// so unwrapping IMET, IMD5 and the LZ77 wrapper is all that stands between
// this toolset's existing U8/BRLYT/BRLAN/TPL/BRSTM support and a real banner.
//-----------------------------------------------------------------------------

#define IMET_SIZE 0x600 // full header, including the 0x40 leading zeros
#define IMET_MAGIC_OFFSET 0x40
#define IMET_N_TITLES 10
#define IMET_TITLE_SIZE 0x54 // 42 UTF-16BE code units per language
#define IMET_MD5_OFFSET 0x5f0

// IMET title slots, in the order the header stores them (the Wii menu's
// language order, which is *not* the SMDH or NDS banner order).
typedef enum imet_lang_t
{
	IMET_LANG_JAPANESE,
	IMET_LANG_ENGLISH,
	IMET_LANG_GERMAN,
	IMET_LANG_FRENCH,
	IMET_LANG_SPANISH,
	IMET_LANG_ITALIAN,
	IMET_LANG_DUTCH,
	IMET_LANG_CHINESE_SIMPLIFIED,
	IMET_LANG_CHINESE_TRADITIONAL,
	IMET_LANG_KOREAN,
	IMET_LANG__N
} imet_lang_t;

typedef struct imet_t
{
	uint header_offset; // 0x40 normally; 0 for a header without the padding
	uint header_size; // the stored size, normally IMET_SIZE
	uint n_files; // the stored count, always 3 on real titles

	// Uncompressed sizes of the three U8 members, in this order.
	uint icon_size;
	uint banner_size;
	uint sound_size;

	ccp title[IMET_N_TITLES]; // malloc'd UTF-8, never NULL (may be empty)

	bool md5_ok; // the stored MD5 matches the header
	u8 md5[16]; // as stored

	uint u8_offset; // where the embedded U8 archive starts
} imet_t;

// True if DATA carries an IMET header (magic at 0x40, or at 0 for the
// rare unpadded variant) and the embedded U8 archive is present.
bool IsIMET (const u8 *data, uint size);

enumError ScanIMET (imet_t *imet, const u8 *data, uint size);
void ResetIMET (imet_t *imet);

// Human-readable summary (every non-empty title, the member sizes, and
// whether the MD5 checks out). Caller FREEs the result.
char *TextIMET (const imet_t *imet);

//-----------------------------------------------------------------------------
// IMD5: the 0x20-byte header Nintendo puts in front of banner.bin, icon.bin
// and sound.bin (and other Wii menu resources):
//   0x00 "IMD5", 0x04 u32 size of the payload, 0x08 8 reserved bytes,
//   0x10 MD5 of the payload, 0x20 payload.
//-----------------------------------------------------------------------------

#define IMD5_SIZE 0x20

typedef struct imd5_t
{
	uint payload_offset; // always IMD5_SIZE
	uint payload_size; // as stored
	bool md5_ok;
	u8 md5[16];
} imd5_t;

bool IsIMD5 (const u8 *data, uint size);
enumError ScanIMD5 (imd5_t *imd5, const u8 *data, uint size);

// Strips an IMD5 header and, if what follows is a Wii "LZ77" wrapper
// ("LZ77" + the standard LZ10/LZ11 type+size word), decompresses it, so the
// result is the plain payload -- normally a U8 archive.  Returns
// ERR_NOTHING_TO_DO if DATA is neither IMD5- nor LZ77-wrapped.  On success
// *DEST is a fresh buffer the caller FREEs, and *WAS_COMPRESSED (optional)
// says whether the LZ77 layer was present.
enumError UnwrapWiiBannerFile (
	u8 **dest, uint *dest_size, bool *was_compressed, const u8 *data, uint size);

//-----------------------------------------------------------------------------
// WIBN: the banner of a Wii *save game* rather than a channel.  It lives at
// the start of a save's decrypted banner.bin / data.bin and, unlike the
// channel banner above, holds finished bitmaps instead of a layout archive:
//
//   0x0000 "WIBN", 0x0004 u32 flags, 0x0008 u16 animation speed,
//   0x000a 22 reserved bytes,
//   0x0020 title    -- 32 UTF-16BE code units
//   0x0060 subtitle -- 32 UTF-16BE code units
//   0x00a0 banner   -- 192x64 RGB5A3, GameCube 4x4 tile order
//   0x60a0 icons    -- 1..8 frames of 48x48 RGB5A3, same tile order
//
// The icon count is not stored: it follows from the file size, and trailing
// all-zero frames are padding rather than real animation steps.
//-----------------------------------------------------------------------------

#define WIBN_MAGIC_NUM 0x5749424e // "WIBN"
#define WIBN_BANNER_OFFSET 0xa0
#define WIBN_BANNER_WIDTH 192
#define WIBN_BANNER_HEIGHT 64
#define WIBN_ICON_WIDTH 48
#define WIBN_ICON_HEIGHT 48
#define WIBN_BANNER_DATA_SIZE (WIBN_BANNER_WIDTH * WIBN_BANNER_HEIGHT * 2)
#define WIBN_ICON_DATA_SIZE (WIBN_ICON_WIDTH * WIBN_ICON_HEIGHT * 2)
#define WIBN_ICONS_OFFSET (WIBN_BANNER_OFFSET + WIBN_BANNER_DATA_SIZE)
#define WIBN_MAX_ICONS 8

#define WIBN_FLAG_NOCOPY 0x01 // save is marked as not copyable

// Passed to DecodeWIBNImage_RGBA() instead of an icon index.
#define WIBN_IMAGE_BANNER (~(uint) 0)

typedef struct wibn_t
{
	uint flags;
	uint anim_speed;
	ccp title; // malloc'd UTF-8, never NULL (may be empty)
	ccp subtitle; // ditto
	uint n_icons; // 1..WIBN_MAX_ICONS, trailing zero frames excluded
	const u8 *data; // borrowed source pointer
	uint size;
} wibn_t;

bool IsWIBN (const u8 *data, uint size);
enumError ScanWIBN (wibn_t *wibn, const u8 *data, uint size);
void ResetWIBN (wibn_t *wibn);
char *TextWIBN (const wibn_t *wibn);

// Decode the banner (FRAME == WIBN_IMAGE_BANNER) or icon frame FRAME to
// RGBA. The caller owns *DEST on success.
enumError DecodeWIBNImage_RGBA (
	u8 **dest, uint *width, uint *height, const wibn_t *wibn, uint frame);

#endif
