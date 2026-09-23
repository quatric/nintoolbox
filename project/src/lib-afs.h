#ifndef LIB_AFS_H
#define LIB_AFS_H

#include "lib-std.h"

// Sega/CRI-style "AFS" flat archive, reused by Spike Co.'s game engine for
// Dragon Ball Z: Budokai Tenkaichi 3 (wzs3us0/1/2.afs on the retail disc).
// See lib-afs.c.
//
// Layout, confirmed against a retail sample by direct hexdump:
//   0x00  magic "AFS\0"
//   0x04  u32 LE file_count
//   0x08  file_count * { u32 LE offset, u32 LE size }
//   ..    one more 8-byte pair right after the last entry:
//         { u32 LE metadata_offset, u32 LE metadata_size } -- a footer
//         descriptor pointing at the name/date table below
//   metadata_offset .. file_count * 48-byte records:
//         char name[32]; u16 LE year,month,day,hour,minute,second; u32 unk;
//         (the trailing u32 did not cleanly match the entry's size in the
//         sample checked -- treat it as unverified/reserved and always get
//         the real size from the offset/size table, never from here)
typedef struct afs_entry_t
{
	u32 offset;
	u32 size;
	char name[33];			// from the metadata table, or empty if absent
	u16 year, month, day;
	u16 hour, minute, second;
} afs_entry_t;

typedef struct afs_t
{
	const u8 *raw;
	size_t raw_size;
	uint n_entries;
	afs_entry_t *entries;
	u32 meta_offset;		// 0 if no metadata table was found/usable
	u32 meta_size;
} afs_t;

enumError ScanAFS (afs_t *afs, const u8 *data, size_t size);
void ResetAFS (afs_t *afs);
enumError ExtractAFSArchive (ccp arg, ccp basedir, uint depth);

#endif
