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

#endif
