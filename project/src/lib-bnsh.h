#ifndef LIB_BNSH_H
#define LIB_BNSH_H

#include "lib-nintendo.h"
#include <stdio.h>

// Nintendo Switch Binary Shader (.bnsh). References:
// KillzXGaming/Switch-Toolbox File_Format_Library/FileFormats/Shader/BNSH.cs
// Header/ShaderVariation/ShaderProgram/ShaderData, plus KillzXGaming/BinaryShaderLibrary
// (BNSH/BnshFile.cs, ShaderVariation/ShaderProgram.cs/ShaderInfoData.cs/
// ShaderCodeDataBinary.cs/ShaderCodeDataCompressed.cs/ShaderCodeDataSource.cs, GFX/Enums.cs)
// for the dual-program variations, zlib-compressed stages, MemoryData and the older
// reflection unknowns that Switch-Toolbox leaves implicit.

bool IsBNSH (const u8 *data, size_t size);

// Lists the shader variations found in a BNSH container and, for each present stage
// (vertex/tess-control/tess-eval/geometry/fragment/compute), the raw blob offset(s) and
// size(s) inside the file. Handles both Switch-Toolbox's summed single-program layout and
// BinaryShaderLibrary's independent source/binary programs per variation, plus
// zlib-compressed stages (compression == 1) and each program's MemoryData. The GPU machine
// code / GLSL-NX source itself is version- and target-dependent and not disassembled here
// -- this only exposes enough to let the caller pull the raw bytes back out, the same scope
// as the AAMP header-only decode.
enumError DecodeBNSH_Text (FILE *out, const u8 *data, size_t size);

// One extractable shader blob inside a BNSH file, as found by ScanBNSH_Blobs().
typedef struct bnsh_blob_t
{
	uint variation; // variation table index
	char program_kind[16]; // "program", "source" or "binary"
	char stage[32]; // e.g. "vertex", "fragment"
	uint blob_index; // source chunk index, or 0/1 for the binary pair
	u64 offset; // absolute file offset of the blob
	u32 size; // blob size in bytes
	u32 decompressed_size; // for zlib-compressed stages, else 0
	bool is_source; // source-text chunk (extract as .glsl)
	bool is_compressed; // zlib-compressed chunk (still compressed on extract)
} bnsh_blob_t;

typedef struct bnsh_blobs_t
{
	uint n_blobs;
	bnsh_blob_t *blobs; // owned, CALLOC'd/REALLOC'd; NULL/0 length if n_blobs == 0
} bnsh_blobs_t;

// Enumerates every in-bounds shader blob (source chunks, binary pairs, compressed blobs)
// so the caller (the EXTRACT command) can write them out as sibling files without
// re-implementing the variation/program walk. Out-of-bounds entries are skipped.
enumError ScanBNSH_Blobs (bnsh_blobs_t *out, const u8 *data, size_t size);
void ResetBNSH_Blobs (bnsh_blobs_t *out);

#endif // LIB_BNSH_H
