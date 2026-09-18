#ifndef SZS_LIB_EFFN_H
#define SZS_LIB_EFFN_H 1

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco Effect File (.eff / .effn, magic "EFFN").
// Used in Super Smash Bros 4 (Wii U / 3DS) and Super Smash Bros Ultimate (Switch).
// Reference: KillzXGaming/EffectLibrary FileData/EFFN/NamcoEffectFile.cs.

bool IsEFFN (const u8 *data, size_t size);

// Decodes the EFFN header, effect entries, external model flags/names,
// bone variants, and the embedded VFXB (PTCL) archive into human-readable text.
enumError DecodeEFFN_Text (FILE *out, const u8 *data, size_t size);

// Extracts an EFFN archive into dest_dir:
// - NamcoFile.json: JSON metadata mapping effect names, emitter sets, external models, and variants.
// - particle.ptcl: The embedded NintendoWare VFXB particle effect binary.
// - Base.ptcl: Exact duplicate copy of the particle file for reference.
enumError ExtractEFFNArchive (ccp source_file, ccp dest_dir);

// Reconstructs an EFFN archive from source_dir into dest_file:
// Requires NamcoFile.json (or namco.json) and particle.ptcl (or Base.ptcl).
enumError CreateEFFNArchive (ccp source_dir, ccp dest_file);

#endif // SZS_LIB_EFFN_H
