// SPDX-License-Identifier: GPL-2.0+
// T&E Soft "Super Swing Golf" / "We Love Golf" series container format.
//
// The game's .szip files (and, surprisingly, its top-level .iff files, e.g.
// DATA/files/pangya.iff) are NOT a custom proprietary container at all: they
// are ordinary PKZIP archives (local file header magic "PK\x03\x04"), each
// bundling a handful of the engine's other typed files (.sbin, .gsr, ...)
// under plain ASCII names. This was confirmed byte-exact against zlib/Python
// on multiple samples: general-purpose flag bit 3 (streamed sizes) is never
// set, so every local file header already carries the real CRC-32,
// compressed size and uncompressed size, no central directory walk needed.
// Compression method observed so far is exclusively 8 (deflate); method 0
// (store) is handled too since it is trivial and legal PKZIP.
//
// This single container, once unpacked, is what "unlocks" the rest of the
// engine's format zoo: e.g. a hell_rain_03.szip sample extracts to a
// hell_cloud_03.sbin plus six hell_cloud_03-NNN.gsr siblings.
//
// Central-directory / ZIP64 / encrypted / streamed-size (bit 3) entries are
// out of scope here -- every sample found on this title's disc uses plain
// sequential local-file-header records, so this reads those directly and
// stops at the first non-PK\x03\x04 signature (central directory or end).
#ifndef LIB_TESZIP_H
#define LIB_TESZIP_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// Local file header layout, read field-by-field with rd_le16()/rd_le32()
// (little-endian, unlike the rest of the Wii disc which is big-endian --
// these are literal off-the-shelf PKZIP records written by a PC-side
// packing tool) rather than a packed struct, to avoid any alignment
// surprises:
//
//   offset 0x00  u32  magic          0x04034b50 ("PK\x03\x04")
//   offset 0x04  u16  version_needed
//   offset 0x06  u16  flags          bit 3 (streamed sizes) never seen set
//   offset 0x08  u16  method         0 = store, 8 = deflate (only ones seen)
//   offset 0x0a  u16  mod_time
//   offset 0x0c  u16  mod_date
//   offset 0x0e  u32  crc32
//   offset 0x12  u32  comp_size
//   offset 0x16  u32  uncomp_size
//   offset 0x1a  u16  name_len
//   offset 0x1c  u16  extra_len
//   offset 0x1e  ...  name[name_len], extra[extra_len], payload[comp_size]
#define TE_ZIP_LOCAL_HEADER_SIZE 0x1e

#define TE_ZIP_LOCAL_MAGIC 0x04034b50 // "PK\x03\x04"
#define TE_ZIP_CENTRAL_MAGIC 0x02014b50 // "PK\x01\x02"
#define TE_ZIP_END_MAGIC 0x06054b50 // "PK\x05\x06"

//-----------------------------------------------------------------------------
// One decoded (and, for deflate entries, inflated) entry.
typedef struct te_zip_entry_t
{
	char name[256]; // NUL-terminated, truncated if longer
	u16 method;
	u32 crc32;
	u32 comp_size;
	u32 uncomp_size;
	u8 *data; // MALLOC'd uncompressed payload, uncomp_size bytes
} te_zip_entry_t;

typedef struct te_zip_list_t
{
	te_zip_entry_t *entry;
	uint n;
	uint n_alloc;
} te_zip_list_t;

//-----------------------------------------------------------------------------
// Magic-only probe: true if 'data' begins with a PKZIP local file header
// whose fixed fields are internally consistent (method is 0 or 8, name_len
// is plausible and the header+name fits within 'size').
int IsTEZip (const u8 *data, size_t size);

// Walks every sequential local file header starting at offset 0, stopping
// at the first non-local-file-header signature (central directory / end of
// central directory) or end of buffer. Decodes (inflates) each entry's
// payload. Returns ERR_OK and a populated, caller-owned list (free with
// FreeTEZipList) even if zero entries were found; returns an error only on
// a structurally malformed header (never on "ran out of entries").
enumError DecodeTEZip (te_zip_list_t *list, const u8 *data, size_t size);

// Frees every entry's data buffer and the entry array itself.
void FreeTEZipList (te_zip_list_t *list);

// Text dump: one line per entry (index, method, comp/uncomp size, crc32,
// name). Does not require the entries to have been inflated.
enumError DecodeTEZip_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_TESZIP_H
