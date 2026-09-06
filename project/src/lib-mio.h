#ifndef LIB_MIO_H
#define LIB_MIO_H

#include "lib-nintendo.h"

typedef enum mio_type_t
{
	MIO_TYPE_UNKNOWN = 0,
	MIO_TYPE_GAME,
	MIO_TYPE_COMIC,
	MIO_TYPE_RECORD
} mio_type_t;

typedef struct mio_meta_t
{
	char name[64];
	char brand[64];
	char creator[64];
	char desc[128];
} mio_meta_t;

// Detection & type checks
bool IsMIO (const u8 *data, size_t size);
mio_type_t GetMIOType (const u8 *data, size_t size);
void ReadMIOMetadata (const u8 *data, size_t size, mio_meta_t *meta);

// Image decoders: allocates width*height*4 RGBA8 buffer using MALLOC()
// Caller owns returned pointer.
u8 *DecodeMIOComicPanel (const u8 *data, size_t size, uint panel_idx, uint *out_w, uint *out_h);
u8 *DecodeMIOGameBG (const u8 *data, size_t size, uint *out_w, uint *out_h);
u8 *DecodeMIOGameSprite (const u8 *data, size_t size, uint obj_idx, uint frame_idx, uint *out_w, uint *out_h);

// Audio decoder: generates standard Type-1 MIDI file from record tracks.
// Returns allocated buffer via DCLib MALLOC().
u8 *DecodeMIORecordMIDI (const u8 *data, size_t size, uint *out_size);

// Extraction helper for wszst xx: extracts images, midi, and metadata.txt into destination folder.
enumError ExtractMIOArchive (ccp arg, ccp basedir, uint depth);

#endif
