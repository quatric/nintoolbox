// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_MPMESS_H
#define SZS_LIB_MPMESS_H 1

#include "types.h"
#include "lib-archive-util.h"
#include <stdio.h>

// Mario Party 4-7 / GCN Message files (board.dat, mini.dat, mini_e.dat, board_e.dat).
// Reference: MPLibrary/GCWii/Message/MessFile.cs & MessFileData.cs.

bool IsMPMESS (const u8 *data, size_t size);

// Extracts the message file container into text / JSON / subfile strings.
enumError ScanMPMESS (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

// Dumps decoded message strings to out file stream.
enumError DecodeMPMESS_Text (FILE *out, const u8 *data, size_t size);

// Full message-archive model preserving versions and IDs.
// Reference: MPLibrary/GCWii/Message/MessFileData.cs. Versions 4, 5 and 6
// share the file-level shape (u32 count + offset table, then messages) but
// differ in offset bases and entry headers:
//   v4: offsets absolute (base 0); a 0xFFFFFFFF MaxValue word follows the
//       file table and a section-size word trails every message; entries
//       are 0x0B + encoded string.
//   v5: file offsets relative to 4 (stored = actual - 4); messages carry
//       the MaxValue word and 0x0B entries like v4.
//   v6: file offsets relative to 4 like v5, but messages drop the MaxValue
//       word and entries are u32 ID + encoded string.
// Unparseable messages are kept verbatim (raw/raw_size) exactly like the
// reference ReadMessageData, so repacks stay byte-exact.
typedef struct mpmess_entry_t
{
	u32 id; // v6 message ID (0 for v4/v5)
	char *text; // decoded string with [Dialog:]/[COLOR:]/[ICON:]/[INSERT:]/[Select]/[Align_N] tags
} mpmess_entry_t;

typedef struct mpmess_file_t
{
	char name[64];
	mpmess_entry_t *entries;
	uint num_entries;
	u8 *raw; // verbatim message bytes when entries could not be parsed
	uint raw_size;
} mpmess_file_t;

typedef struct mpmess_archive_t
{
	uint version; // 4, 5 or 6
	mpmess_file_t *files;
	uint num_files;
} mpmess_archive_t;

enumError ScanMPMESSArchive (mpmess_archive_t *arc, const u8 *data, size_t size);
void ResetMPMESSArchive (mpmess_archive_t *arc);
// Inverse of ScanMPMESSArchive (encode tags back to bytes).
enumError CreateMPMESS (u8 **dest, uint *dest_size, const mpmess_archive_t *arc);

#endif // SZS_LIB_MPMESS_H
