#ifndef LIB_BYML_H
#define LIB_BYML_H

#include "lib-nintendo.h"
#include <stdio.h>

enumError DecodeBYML_YAML (FILE *out, const u8 *data, size_t size);
enumError DecodeBYML_XML (FILE *out, const u8 *data, size_t size);
enumError DecodeBYML_JSON (FILE *out, const u8 *data, size_t size);

enumError EncodeBYML_Text (
	u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le, u16 version);
enumError EncodeBYML_XML (
	u8 **dest, uint *dest_size, const char *xml, uint xml_len, bool is_le, u16 version);

enumError encode_byml_file (ccp source, ccp dest);

// BotW-style S-prefixed BYML containers (.sbyml, .smubin, ...) are Yaz0
// compressed. True if 'dest' names one of them, so the encoder must compress.
bool byml_dest_is_compressed (ccp dest);

// Load a BYML file, transparently decompressing Yaz0-wrapped containers
// (BotW .sbyml and friends, cf. NintenTools.Byaml's Yaz0 handling).
// Validates the BY signature and version. Caller must FREE(*data).
enumError LoadBYMLData (ccp path, u8 **data, size_t *size);

// Case-insensitive substring search over map keys and scalar values,
// in the spirit of NintenTools.Byaml's editor search. Prints one
// "path = value" line per match to 'out' ('out' may be NULL to only count).
// *found receives the hit count. Empty or NULL pattern is an error.
enumError SearchBYML (FILE *out, const u8 *data, size_t size, ccp pattern, uint *found);

#endif
