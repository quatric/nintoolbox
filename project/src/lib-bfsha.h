#ifndef LIB_BFSHA_H
#define LIB_BFSHA_H

#include "lib-nintendo.h"
#include <stdio.h>

// Nintendo Switch Binary Shader Archive (.bfsha). Reference: KillzXGaming/Switch-Toolbox
// File_Format_Library/FileFormats/Shader/BFSHA.cs, whose actual byte layout lives in the
// separate KillzXGaming/BfshaLibrary repo it wraps (ShaderLibrary/Switch/BfshaLoader.cs and
// ShaderLibrary/Structs.cs) -- BFSHA.cs itself only hands the stream to that library.

bool IsBFSHA (const u8 *data, size_t size);
bool IsBFSHA_WiiU (const u8 *data, size_t size);

// Lists the archive-level string table (name/path), every named shader model, and -- when the
// model's header version is one of the tractable fixed layouts (major 4/5/7/8/9) -- its shader
// program table (variation offset, used-attribute flags, per-stage sampler/uniform-block index
// counts) plus the raw static+dynamic option key row per program from the archive's key table.
// Also decodes each model's option/attribute/sampler/image/uniform-block ResDict name tables
// (BfshaLoader.cs's LoadDictionary() calls), including the per-block inner uniform-name dict.
enumError DecodeBFSHA_Text (FILE *out, const u8 *data, size_t size);

// One shader model's embedded BNSH blob location, as found by ScanBFSHA_ModelRefs().
typedef struct bfsha_model_ref_t
{
	char name[256];    // dictionary key, or "<unnamed>" if the key pointer was invalid
	u64  bnsh_offset;  // absolute offset of the embedded BNSH file within the .bfsha buffer
	u32  bnsh_size;    // that BNSH file's own recorded size (its BinaryHeader.FileSize)
} bfsha_model_ref_t;

typedef struct bfsha_models_t
{
	uint n_models;
	bfsha_model_ref_t *models; // owned, CALLOC'd; NULL/0 length if n_models == 0
} bfsha_models_t;

// Walks the shader-model dictionary purely to recover each model's name and its embedded BNSH
// blob's offset+size (mh.shader_file_off / the u32 at +0x1c of that offset, same as the "bnsh:"
// line DecodeBFSHA_Text already prints) -- so the caller (the EXTRACT command) can pull the raw
// bytes back out without re-implementing the model-record walk. A model with no BNSH reference,
// or one whose bnsh_offset/bnsh_size fail the 64-bit bounds check, gets bnsh_size == 0.
enumError ScanBFSHA_ModelRefs (bfsha_models_t *out, const u8 *data, size_t size);
void ResetBFSHA_ModelRefs (bfsha_models_t *out);

#endif // LIB_BFSHA_H
