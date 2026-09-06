#ifndef LIB_IQIPACK_H
#define LIB_IQIPACK_H

#include "lib-nintendo.h"

// NVIDIA Shield iQiyi PAK container format
// Magic: "PACK" (in little-endian word: 0x4B434150)
#define IQIPACK_MAGIC 0x4B434150

typedef struct iqipack_header_t
{
	u32 magic;       // 'PACK' = 0x4B434150
	u32 version;     // usually 1
	u8  unk1[0x0C];
	u32 header_size; // size of encrypted header block
	u32 size2;       // duplicate size
	u8  unk2[0x0C];
} __attribute__((packed)) iqipack_header_t;

// Detection & type checks
bool IsIQIPack (const u8 *data, size_t size);
bool IsIQIPackFile (ccp filename);

// Extraction helper for wszst xx:
// Decrypts header and all assets using XXTEA and writes to target directory.
enumError ExtractIQIPack (ccp arg, ccp basedir, uint depth);

#endif // LIB_IQIPACK_H
