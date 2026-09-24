// SPDX-License-Identifier: GPL-2.0+
// "Artlist Collection - The Dog Island" (Wii, USA/En,Fr,Es disc) proprietary
// asset formats. No public documentation of any of these exists (checked
// XeNTaX, GBAtemp, Models-Resource, romhacking.net -- all empty on this
// title). Everything below was reverse-engineered from scratch against the
// retail disc, by extracting files/ and cross-checking multiple real
// samples of each extension byte-for-byte (never a single-sample guess).
//
// Eight formats are covered, at varying depth -- some are small enough to
// be fully understood, others (bytecode scripts) are only header/magic
// detected on purpose, per the project convention of not asserting opcode
// semantics that were not actually reverse-engineered. Measured decode
// success rate across every real sample of each extension extracted from
// the retail disc: .wds 1838/1869 (98%), .wdb 1464/1465 (99.9%), .ymg/
// .ymm 236/237 (99.6%), .pms 380/382 (99.5%), .cprm 126/131 (96%), .mpq
// 2/2 (100%), .mtt/.ypc/.pac 1344/2595 (52% -- most of the remainder are
// evidently a different, unidentified container reusing these same
// extensions; see note (5) below), .efi 65/67 (97%, no fixed magic so a
// handful collide with other pre-existing magic-less probes); .sci/.qci
// have exactly one sample each on this disc and were not found to share
// a single consistent header shape with .efi or each other, so they are
// only extension-recognized, not structurally probed (see note (7)).
//
// (1) ".wds" -- "WARDP" dialogue string table. Confirmed structure:
//       char magic[4];   // "WARD" (fixed)
//       u8   variant;    // observed 'P' (0x50), '0' (0x30), '@' (0x40),
//                         // ' ' (0x20), '@' (0x40) across samples -- value
//                         // not understood (sub-version? content-type
//                         // tag?), so it is reported raw, not asserted.
//       u8   pad[3];     // always 0 in every sample seen
//       u32  count;      // LE. Number of string entries.
//     Immediately followed by an interleaved LE u32 array of length
//     (2*count - 1): offset[0], idx[0], offset[1], idx[1], ..., idx[count-2],
//     offset[count-1]. Confirmed across every sample: the idx[] values are
//     simply 0, 1, 2, ... (count-2) -- a redundant running counter, not
//     used by this decoder except to sanity-check the table. The offset[]
//     values are strictly non-decreasing byte offsets, confirmed (by
//     locating real string data at the computed positions in every sample)
//     to be relative to a base position: the end of this array, rounded up
//     to the next 16-byte boundary.
//     At base+offset[i] sits entry i's data: a short raw byte tag (often
//     Shift-JIS bytes -- looks like a leftover Japanese speaker name/icon
//     id from the original release), then a single 0x2F ('/') delimiter
//     byte, then an ASCII display string (speaker name for the first entry
//     of a block, dialogue text with '#'-tag inline control codes such as
//     "#c#"/"#n#"/"#red#...#black#" for later entries), NUL-terminated,
//     with NUL padding out to the next entry's offset. This tag+'/'+text
//     shape is confirmed by inspection of every sample, but the exact byte
//     width/encoding of the leading raw tag is NOT confirmed (it varies:
//     4, 5, 6, 8 bytes observed before the '/' across different entries in
//     the same file) -- so the decoder reports the whole
//     [offset[i], offset[i+1]) span as one raw/escaped text field rather
//     than trying to split tag from text.
//     NOT understood: the 'variant' byte's meaning; the exact encoding of
//     the leading per-entry tag bytes; whether entry 0 is always a
//     "speaker name" (true in every retail sample seen, but not asserted
//     as a hard rule since nothing in the header marks it as such).
//
// (2) ".wdb" -- generic offset/string table, same family as ".wds" but a
//     flatter, non-interleaved layout. Confirmed structure:
//       u32 count;        // LE.
//       u32 header_size;  // LE. Observed exactly 16 in every sample.
//     followed by 'count' 24-byte LE records, of which only the first
//     dword is confirmed stable across every sample pulled:
//       u32 start;        // byte offset of this entry's string, relative
//                          // to the start of the file. record[0].start is
//                          // confirmed to exactly equal the end of the
//                          // whole record table (16 + 24*count) in every
//                          // sample, and start[i] is confirmed
//                          // non-decreasing across every sample.
//       u32 rest[5];       // Small-integer values in the same rough
//                           // shape as "start"/an end offset/a 0xffff-
//                           // prefixed flags word on a first look at a
//                           // handful of samples -- but a wider sample
//                           // pull found files where the exact same
//                           // relative field positions do NOT hold (the
//                           // "duplicate of start" and "duplicate of end"
//                           // fields are not always where they first
//                           // appeared to be). NOT understood; reported
//                           // raw rather than assumed.
//     followed by the raw string blob; each entry's span is decoded as
//     [start[i], start[i+1]) (or to end-of-file for the last entry), same
//     technique as ".wds" above, since no "end" field could be pinned
//     down reliably. Plain ASCII in every sample seen, no '#'-tag inline
//     control codes observed here unlike ".wds".
//
// (3) ".ymg" / ".ymm" -- "YOBJ" model. Two on-disc shapes observed for the
//     same "YOBJ" payload:
//       - Wrapped in a "DUMY" container (see below) -- seen for every
//         ".ymg" sample -- with the YOBJ size field stored big-endian,
//         matching the POF0 container's own BE size convention.
//       - Bare, magic "YOBJ" at byte 0 with no DUMY wrapper -- seen for
//         the sole ".ymm" sample on this disc -- with the YOBJ size field
//         stored little-endian instead.
//     Only the outer shell (magic + size, in whichever endianness makes
//     the size sane against the real file size) is confirmed; the dense
//     table of BE/LE u32s that follows (bone/mesh offsets, by the look of
//     the repeated small-integer runs and a "Cylinder01"-style ASCII name
//     spotted mid-file in the bare sample) was NOT reverse-engineered --
//     this module only detects and reports the outer header.
//
// (4) ".pms" -- "EVNT" event script. Confirmed header:
//       char magic[4];    // "EVNT"
//       u8   pad[8];      // always 0 in every sample seen
//       u32  event_id;    // LE. Confirmed to exactly equal the numeric
//                          // suffix of the file's own name in every
//                          // sample (e.g. "evt0081.pms" -> 0x51 == 81).
//     Everything after the header is compiled event-script bytecode (long
//     runs of 0xff sentinel/unused slots interleaved with small BE
//     integers and what look like packed opcodes); NOT reverse-engineered
//     and NOT decoded here, same policy as ".efi"/".sci"/".qci" below.
//
// (5) ".mtt" (also seen reused for ".ypc" and ".pac") -- "DUMY"-wrapped
//     "POF0" pointer-fixup container. Confirmed "DUMY" outer shell:
//       char magic[4];    // "DUMY"
//       u32  header_size; // BE (unlike most other fields in this file,
//                          // which are LE -- see the ".cprm" note below
//                          // for another BE outlier). Observed exactly
//                          // 16 in every sample -- this counts the 16
//                          // reserved bytes that follow it, so the inner
//                          // chunk starts at byte 24, not byte 16.
//       u8   pad[16];     // always 0 in every sample seen.
//     followed immediately by the inner chunk (at byte 24):
//       char inner_magic[4]; // e.g. "POF0" (".mtt"/".ypc"/".pac") or
//                              // "YOBJ" (see (3) above, when DUMY-wrapped).
//       u32  inner_size;      // BE. Confirmed sane against real file size
//                              // in every sample (inner_size + 16 either
//                              // exactly equals or is close to the real
//                              // file size, allowing for outer padding).
//     "POF0" is a chunk name also used, unrelated, by several other
//     Nintendo formats (a general pointer/offset fixup table); the packed
//     table that follows the size field here was NOT reverse-engineered
//     (it is not a plain flat u32 array -- attempts to read it as one did
//     not produce sane, monotonic values across samples), so only the
//     outer DUMY+POF0 shell is detected/decoded.
//
// (6) ".cprm" -- fixed-size float record table. Fully confirmed, BIG
//     ENDIAN throughout (unlike every other format in this file, which is
//     little-endian):
//       u32 count;        // BE. Number of 64-byte records that follow.
//       u32 header_size;  // BE. Observed exactly 16 in every sample.
//       u8  pad[8];       // always 0 in every sample seen.
//     followed by 'count' 64-byte records of raw BE data (16 x u32/f32
//     slots per record); one sample's second record's fourth-ish word
//     decodes as the BE float 3.14159274 (pi), confirming byte order and
//     f32 slot alignment. The task description's "144-byte" figure is
//     just the on-disc size for the specific count=2 case (16 + 2*64);
//     the true fixed unit is the 64-byte record, with count=0..N seen
//     (16, 80, 144, 208, ... on this disc, every one exactly
//     16 + 64*count). Individual field semantics inside the 64-byte
//     record (which floats mean what) are NOT understood; the decoder
//     dumps each record as 16 raw BE f32 values.
//
// (7) ".efi" / ".sci" / ".qci" -- compiled script bytecode. No fixed
//     magic bytes on any of the three; only a loose structural pattern
//     was confirmed across every sample:
//       u32 field_a;      // LE. Varies per file (a size/entry count?).
//       u32 field_b;      // LE. Varies per file (another size/count?).
//       u32 header_size;  // LE. Observed exactly 16 in .efi/.qci; a
//                          // different small header size (0x18) in the
//                          // one .sci sample.
//       u32 zero;         // LE. Always 0 in every sample seen right
//                          // after header_size.
//     This is genuinely a bytecode format (opcode-looking runs of small
//     integers, embedded ASCII identifiers such as "EventState" spotted
//     mid-file in the .sci sample); actual opcode semantics were NOT
//     reverse-engineered. This module only implements a conservative
//     structural probe (loose enough that it would also plausibly match
//     unrelated 16-byte-header binaries -- it is gated on the file
//     extension by the caller, same as ".mpq" below) and reports the
//     handful of confirmed header words raw.
//
// (8) ".mpq" -- explicitly NOT Blizzard's MPQ archive format, verified
//     empirically: Blizzard MPQ always starts with the 4-byte magic
//     "MPQ\x1A" (0x4D 0x50 0x51 0x1A) followed by a 32-byte header whose
//     second field is a fixed header size (usually 32). Every ".mpq"
//     sample on this disc instead starts with "MPQ\0" (0x4D 0x50 0x51
//     0x00) -- a different fourth magic byte -- and the bytes that follow
//     do not match the Blizzard MPQ header layout at all (no plausible
//     32/44/68-byte Blizzard header size at the expected field position;
//     instead small values sit where Blizzard's archive size/format-
//     version would be, and the file is many times larger than any such
//     small "archive size" would allow). Confirmed custom header layout,
//     BIG ENDIAN (like ".cprm" and the DUMY container's header_size --
//     several of this disc's formats mix LE and BE fields):
//       char magic[4];    // "MPQ\0"
//       u32  field_a;     // BE. Observed 2048 (0x800) in both samples.
//       u32  field_b;     // BE. Observed 16 (0x10) in both samples.
//       u32  field_c;     // BE. Observed 1 and 7 in the two samples on
//                          // this disc (a count?).
//       u32  field_d;     // BE. Observed 2048 in both samples (== field_a).
//     followed by a denser table (offset-pair-looking u32s) that was NOT
//     reverse-engineered -- only two samples exist on this disc (a small
//     7MB ".mpq" and an 83MB one), too few to confidently pin down a
//     per-entry record shape, so this module only detects the outer
//     magic/header and confirms the "not Blizzard MPQ" claim; it does not
//     attempt to list or extract the archive's contents.
#ifndef LIB_DOGISLAND_H
#define LIB_DOGISLAND_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// (1) ".wds" WARDP dialogue string table.
int IsDogIslandWds (const u8 *data, size_t size, size_t file_size);
enumError DecodeDogIslandWds_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".wdb" offset/string table.
int IsDogIslandWdb (const u8 *data, size_t size, size_t file_size);
enumError DecodeDogIslandWdb_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".ymg"/".ymm" YOBJ model, bare or DUMY-wrapped.
int IsDogIslandYobj (const u8 *data, size_t size, size_t file_size);
enumError DecodeDogIslandYobj_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (4) ".pms" EVNT event script (header decode only -- body is
// unreverse-engineered bytecode).
int IsDogIslandPms (const u8 *data, size_t size, size_t file_size);
enumError DecodeDogIslandPms_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (5) ".mtt"/".ypc"/".pac" DUMY+POF0 pointer-fixup container (outer shell
// only -- the packed pointer table itself is unreverse-engineered).
int IsDogIslandMtt (const u8 *data, size_t size, size_t file_size);
enumError DecodeDogIslandMtt_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (6) ".cprm" fixed 64-byte-record BE float table.
int IsDogIslandCprm (const u8 *data, size_t size, size_t file_size);
enumError DecodeDogIslandCprm_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (7) ".efi"/".sci"/".qci" script bytecode -- structural probe only, no
// opcode decoding. Loose enough to also match unrelated small-header
// binaries, so callers should gate this on the file extension.
int IsDogIslandScript (const u8 *data, size_t size, size_t file_size);
enumError DecodeDogIslandScript_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (8) ".mpq" custom (non-Blizzard) container -- outer header only.
int IsDogIslandMpq (const u8 *data, size_t size, size_t file_size);
enumError DecodeDogIslandMpq_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // LIB_DOGISLAND_H
