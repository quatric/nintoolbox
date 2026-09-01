#ifndef SZS_LIB_SMDH_H
#define SZS_LIB_SMDH_H 1

#include "types.h"

// SMDH ("System Menu Data Header"): the 3DS application icon + title
// metadata block. Present standalone (icon.bin/*.smdh, as produced by
// smdhtool/bannertool) and embedded in every CIA/3DSX/CCI title.
//
// Layout (fixed size, 0x36c0 bytes total; all multi-byte fields little
// endian), per 3dbrew's SMDH page and cross-checked against Gericom's
// EveryFileExplorer (3DS/SMDH.cs), which this was verified byte-offset-for-
// byte-offset against:
//   0x0000  header:  magic "SMDH", u16 version, u16 reserved            (8)
//   0x0008  16x application_title_t (one per 3DS system language),
//           each 0x200 bytes: short title (0x80, UTF-16LE), long title
//           (0x100, UTF-16LE), publisher (0x80, UTF-16LE)          (0x2000)
//   0x2008  application_settings_t: 0x10 game-ratings bytes, u32 region
//           lock, u32 matchmaker id, u64 matchmaker bit-id, u32 flags,
//           u16 EULA version, u16 reserved, f32 banner animation default
//           frame, u32 StreetPass id                                (0x30)
//   0x2038  8 reserved bytes                                          (8)
//   0x2040  small icon: 24x24 RGB565, 8x8-tile Morton-swizzled      (0x480)
//   0x24c0  large icon: 48x48 RGB565, same swizzle                (0x1200)
//   0x36c0  end of file

#define SMDH_SIZE 0x36c0
#define SMDH_N_TITLES 16
#define SMDH_SMALL_ICON_SIZE 0x480
#define SMDH_LARGE_ICON_SIZE 0x1200

// Index into smdh_t.title[] -- the 3DS system language order (matches
// CFG_Language / 3dbrew's SMDH title table order).
typedef enum smdh_lang_t
{
	SMDH_LANG_JAPANESE,
	SMDH_LANG_ENGLISH,
	SMDH_LANG_FRENCH,
	SMDH_LANG_GERMAN,
	SMDH_LANG_ITALIAN,
	SMDH_LANG_SPANISH,
	SMDH_LANG_CHINESE_SIMPLIFIED,
	SMDH_LANG_KOREAN,
	SMDH_LANG_DUTCH,
	SMDH_LANG_PORTUGUESE,
	SMDH_LANG_RUSSIAN,
	SMDH_LANG_CHINESE_TRADITIONAL,
	SMDH_LANG__N // = SMDH_N_TITLES, 4 reserved language slots follow it
} smdh_lang_t;

typedef struct smdh_title_t
{
	ccp short_desc; // malloc'd UTF-8, never NULL (may be empty)
	ccp long_desc; // malloc'd UTF-8, never NULL (may be empty)
	ccp publisher; // malloc'd UTF-8, never NULL (may be empty)
} smdh_title_t;

typedef struct smdh_t
{
	u16 version;
	smdh_title_t title[SMDH_N_TITLES]; // owned strings

	u8 game_ratings[0x10];
	u32 region_lock;
	u32 matchmaker_id;
	u64 matchmaker_bit_id;
	u32 flags;
	u16 eula_version;
	float banner_frame;
	u32 streetpass_id;

	const u8 *small_icon; // points into source buffer, RGB565, 0x480 bytes
	const u8 *large_icon; // points into source buffer, RGB565, 0x1200 bytes
} smdh_t;

// Parses DATA (must be exactly SMDH_SIZE bytes, magic "SMDH") into SMDH.
// smdh->small_icon/large_icon point into DATA -- DATA must outlive SMDH.
enumError ScanSMDH (smdh_t *smdh, const u8 *data, uint size);

// Frees the owned title strings; safe to call on a zeroed or reset smdh_t.
void ResetSMDH (smdh_t *smdh);

// Decodes the small (24x24) or large (48x48) icon to tightly packed RGBA8.
enumError DecodeSMDHIcon_RGBA (u8 **dest, uint *width, uint *height, const smdh_t *smdh, bool large);

// Renders a human-readable text summary (all 16 language slots that carry
// a non-empty title, plus the settings fields) into a malloc'd, NUL-
// terminated buffer. Caller FREEs the result.
char *TextSMDH (const smdh_t *smdh);

#endif
