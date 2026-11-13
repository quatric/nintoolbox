#ifndef LIB_PHX_H
#define LIB_PHX_H

#include "lib-nintendo.h"
#include "lib-model-glb.h"

// Kirby Air Riders' "phx" engine asset packs (Switch 2):
//   Asset/assets.cat  -- "PARC" catalog: 64-bit asset IDs, per-asset block lists
//                        ("Main", "HEAD", "BODY", "GBuf", "$DEP", ...) and a location
//                        table {pack, codec (0 raw / 1 zstd), offset, sizes, SHA-256}
//   Asset/assets_*.dat -- "PDAT" packs holding the blocks
// Records are flatbuffers whose identifier at +4 names the type (PTEX, PMSH,
// PANM, PSND, PMTR, PMAP, PEFF, ...). Names are hashed; files are named by ID.

bool IsPARC (const u8 *data, uint size);
enumError ExtractPARCArchive (ccp arg, ccp basedir, uint depth);

// PTEX texture: the HEAD flatbuffer (".ptex") plus its BODY (".ptex.body") -> RGBA.
bool IsPTEX (const u8 *data, uint size);
enumError DecodePTEX_RGBA (u8 **dest, uint *width, uint *height, const u8 *head, uint head_size,
	const u8 *body, uint body_size, ccp *format_name);

// PMSH mesh: the "Main" flatbuffer (".pmsh") plus its "GBuf" block (".pmsh.gbuf") -> model
// (one mesh per submesh: positions, normals, UV0, colour 0, skin). NULL when not decodable.
bool IsPMSH (const u8 *data, uint size);
// Skinned meshes (attribute 4) get a flat skeleton from the mesh's joint palette (name hashes +
// inverse binds); PMSHApplySkeleton replaces it with the PSKS hierarchy when that skeleton has
// every palette joint. Returns the matched joint count, 0 when the skeleton does not fit.
model_t *DecodePMSH (const u8 *main_d, uint main_n, const u8 *gbuf, uint gbuf_n);
bool IsPSKS (const u8 *data, uint size);
uint PMSHApplySkeleton (model_t *model, const u8 *psks, uint psks_size);

// Sygnus ".soundstream" (SOL-AVES sound engine, Kirby Air Riders): "SYGN" ... "STRM" header,
// payload at 0x2c. Encrypted files (AES-192-ECB, key from the game) are decrypted in place.
#define SYGNUS_PAYLOAD 0x2c
typedef enum sygnus_kind_t
{
	SYGNUS_NONE, // not a Sygnus stream
	SYGNUS_ADPCM, // {channels, rate, samples, samples per block} + float-seeded 4-bit ADPCM
	SYGNUS_OPUS, // Nintendo Switch Opus (0x80000001 chunks)
	SYGNUS_RIFF, // embedded RIFF/WAVE
	SYGNUS_OTHER,
} sygnus_kind_t;

sygnus_kind_t PrepareSygnusStream (u8 *data, uint size, bool *decrypted);
// The single embedded source of a phx "PSND" record (Sygnus "ASRC" bank), or SYGNUS_NONE.
sygnus_kind_t FindSygnusSource (const u8 *data, uint size, uint *payload_off);
// DATA points at the ADPCM payload {channels, rate, samples, samples per block, blocks...}.
enumError DecodeSygnusADPCM (s16 **pcm, uint *channels, uint *rate, uint *n_samples, const u8 *data, uint size);

#endif // LIB_PHX_H
