#ifndef LIB_XBIN_H
#define LIB_XBIN_H

#include "lib-nintendo.h"

// HAL Laboratory "XData" (magic "XBIN"): the header HAL's HEL engine puts on almost
// every non-asset file (Kirby's Return to Dream Land, Star Allies, Forgotten Land, ...).
// Two payloads have a generic, documented layout and are decoded to YAML text here:
//   "YAML" -- binary YAML (versions 2..6; v5+ offsets are self-relative)
//   "FDGH"/"HGDF" -- scene preload graphs (versions 2..3)
// Other payloads (CND parameters, Basil scripts, ...) are game-specific tables.

typedef enum xbin_kind_t
{
	XBIN_KIND_NONE = 0,
	XBIN_KIND_YAML,
	XBIN_KIND_FDG,
	XBIN_KIND_OTHER,
} xbin_kind_t;

// Returns the payload kind, or XBIN_KIND_NONE when 'data' is not an XData file.
xbin_kind_t GetXBINKind (const u8 *data, uint size);

// Writes the decoded YAML / FDG payload as YAML text to 'path'.
enumError SaveTextXBIN (const u8 *data, uint size, ccp path);

#endif // LIB_XBIN_H
