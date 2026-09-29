// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_CTRRES_H
#define LIB_CTRRES_H 1

#include "lib-std.h"

// Nintendo 3DS system-application resources as shipped with the Camera
// application (all little-endian). Layouts were read from the fixtures:
//
// .pack: a flat table of 0x40-byte records {char name[0x38]; u32 offset;
//   u32 size} (names are the member file names, e.g. "BubB.bcptl.LZ", each
//   member LZ11-compressed on its own), terminated by an all-zero record and
//   padded to the first member; members follow back to back.
//
// .gbin ("GBIN"): u32 record count, then per record "GUID", seven u32 words
//   (word 0 = number of parts; the others are flags/ids whose meaning is not
//   known), char name[16] (e.g. "T_003") and `parts` 20-byte records
//   {char name[16]; u32 value} (e.g. "D_003_0"). Dumped as YAML.
//
// .bcbnk / .bcwsd (NW4C "CBNK" / "CWSD"): the common CTR file header (magic,
//   BOM 0xfeff, header size, version, file size, block count, block refs)
//   and one INFO block. Its body starts with references {u16 type, u16 0,
//   u32 offset from the body start}: a wave id table {u32 count, count *
//   {u32 wave archive id (0x05000000 | index), u32 wave index}} and, for a
//   bank, a table of instrument references (type 0x5900 = instrument with
//   regions, 0x5903 = empty slot with offset -1), for a wave sound file a
//   table of wave sound references (type 0x4900). The nested instrument /
//   region / wave sound structures are listed by type and offset only.
//
// The dumps are YAML; ParseCtr* return a malloc-owned string or 0.
bool IsCtrPack (const u8 *data, size_t size);
bool IsCtrGbin (const u8 *data, size_t size);
bool IsCtrBankOrWsd (const u8 *data, size_t size);

// Extracts the members into the directory; returns ERR_NOTHING_TO_DO when the
// file is not a camera PACK.
enumError ExtractCtrPack (ccp arg, ccp basedir, uint depth);
// Writes ARG.yaml (or the destination option) for .gbin / .bcbnk / .bcwsd.
enumError ExtractCtrResource (ccp arg, ccp basedir, uint depth);

char *DumpCtrGbin (const u8 *data, size_t size, ccp source);
char *DumpCtrBankOrWsd (const u8 *data, size_t size, ccp source);

#endif
