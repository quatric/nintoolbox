// SPDX-License-Identifier: GPL-2.0+
// "Red Steel 2" (Wii, USA En/Fr/Es disc) proprietary formats. No public
// documentation of the Ubisoft "ABE" bigfile family exists anywhere
// (checked XeNTaX, GBAtemp, Models-Resource, romhacking.net -- all empty
// on this title); reverse-engineered from scratch against the retail disc.
// The ".rel" module format is a separately-covered, well-documented public
// format (the standard Nintendo/Metrowerks CodeWarrior "REL" relocatable
// module, used across many GC/Wii titles); it is wired in here because
// nintoolbox did not previously have a generic decoder for it (only the
// unrelated, special-cased "StaticR.rel" from Mario Kart Wii).
//
// (1) ".bbf" / ".BF" -- Ubisoft "ABE" bigfile. This is a SECOND, distinct
//     Ubisoft bigfile family from the already-supported "Magma" engine
//     bigfile (lib-magma.h/.c): different magic, different header shape,
//     and (unlike Magma's ".fat"/".big" pair) a single self-contained file
//     that is both index and data. Confirmed empirically to be the SAME
//     format across all three on-disc samples despite two different
//     extensions ("RS2.bbf", "RS2.$hd$.bik.BF", "RS2.wii.sns.BF") -- all
//     three share the identical 4-byte magic, LE header field widths, and
//     a header field (byte offset 24) observed as the exact same constant
//     (5336 / 0x14d8) across all three, strongly suggesting a fixed inner
//     header/table layout shared by the whole family regardless of what
//     the game's file list happens to call the container.
//
//     Confirmed 64-byte outer header, all fields LE u32:
//       char magic[4];      // "ABE\0" (fixed)
//       u32  version;       // Observed 4 in all three samples.
//       u32  entry_count;   // Observed 18395 / 337 / 4854 across the
//                             // three samples -- a plausible per-file
//                             // record count for a bigfile this size.
//       u32  field_c;       // Observed 1 (RS2.bbf, bik.BF) and 2
//                             // (wii.sns.BF). Meaning not understood
//                             // (archive "kind" tag? sub-format version?).
//       u32  field_d;       // Observed 1 in all three samples.
//       u32  zero_a;        // Observed 0 in all three samples.
//       u32  field_e;       // Observed 5336 (0x14d8) in ALL THREE
//                             // samples -- looks like a fixed constant
//                             // of the format itself (e.g. a directory-
//                             // record struct size for the whole ABE
//                             // family), not per-file game data.
//       u32  zero_b;        // Observed 0 in all three samples.
//       u32  table_offset;  // Byte offset of a following directory-tree
//                             // table. Confirmed: dereferencing this
//                             // offset in every sample lands exactly on a
//                             // recognizable node run (a literal "Root"
//                             // ASCII string tag, 0xff sentinel words,
//                             // and short name fragments such as "01",
//                             // "be", "bf", "c0" interleaved with what
//                             // look like child-index/sibling-index u32
//                             // fields) -- confirms the field is a real
//                             // offset, but the exact node record shape
//                             // was NOT pinned down (see below).
//       u32  field_g, field_h; // Two more small counts/offsets, values
//                             // vary per sample and were not further
//                             // interpreted individually.
//       u32  field_i, field_j; // Same as field_g/field_h; often but not
//                             // always equal to entry_count-ish values
//                             // seen elsewhere in the same header.
//       u32  sentinel[3];    // Observed 0xffffffff (all three fields) in
//                             // every sample -- looks like a fixed
//                             // end-of-fixed-header marker.
//
//     NOT understood/NOT decoded: the directory-tree table itself. Every
//     sample's table at table_offset is a run of fixed-size-looking slots
//     that mix short NUL-terminated ASCII name fragments (e.g. "Root",
//     "01", "be", "bf", "c0" -- consistent with a per-character or
//     per-path-segment trie/hash-bucket directory, plausible given the
//     large entry_count on RS2.bbf, 18395 entries) with runs of
//     0xffffffff sentinel words and small LE u32 fields that look like
//     child/sibling/parent indices, but the exact record width and field
//     order were not pinned down from only three samples (all sharing
//     the same overall disc, so no independent cross-title check was
//     possible the way Magma's was). No candidate record layout was found
//     that both (a) tiles the table into a whole number of same-size
//     records ending exactly at entry_count records, and (b) produces
//     offset/size pairs into the surrounding file that are contiguous and
//     non-overlapping the way Magma's ".fat" records were confirmed to be
//     -- so this module deliberately does not attempt payload extraction,
//     matching the project convention (see lib-magma.h's ".bf" variant,
//     lib-dogisland.h's ".mpq") of only structurally detecting a table
//     that could not be confidently reverse-engineered from too few
//     samples, rather than guessing at a record shape.
//
// (2) ".rel" -- standard Nintendo/CodeWarrior "REL" relocatable module
//     (used by many GC/Wii titles for dynamically-loaded code overlays;
//     this is a documented public format, unlike (1) above). Confirmed
//     byte-for-byte against all 6 real ".rel" samples on this disc
//     (all version 3, all internally self-consistent: section table
//     entries stay within the file, name_offset/name_size land on
//     printable text, imp_offset/imp_size and rel_offset/fix_size are
//     mutually consistent). Confirmed BIG ENDIAN (PowerPC), unlike most
//     of this project's LE-header formats:
//       u32 id;                 // module id (0 in the standalone "main"
//                                 // module case is common on other
//                                 // titles; nonzero per-module ids
//                                 // 2..7 observed across this disc's
//                                 // samples).
//       u32 next, prev;         // runtime linked-list pointers; 0 in an
//                                 // unloaded on-disc module (confirmed 0
//                                 // in every sample).
//       u32 num_sections;
//       u32 section_info_offset; // offset of an array of num_sections
//                                 // 8-byte {offset_and_exec_flag, length}
//                                 // entries (low bit of offset = "this
//                                 // section is executable", standard
//                                 // REL convention; confirmed: masking it
//                                 // off yields offsets that stay inside
//                                 // the file and tile without overlap
//                                 // against each entry's length).
//       u32 name_offset, name_size; // per the public REL spec this is a
//                                 // module name string, but on EVERY
//                                 // sample on this disc it points at
//                                 // ordinary binary section content
//                                 // (PowerPC code bytes, confirmed by
//                                 // dereferencing it) rather than
//                                 // printable text -- these particular
//                                 // modules evidently ship with no name
//                                 // set. Reported raw, not decoded as a
//                                 // string.
//       u32 version;             // observed 3 in every ".rel" sample.
//       u32 bss_size;
//       u32 rel_offset;          // relocation-data table offset.
//       u32 imp_offset, imp_size; // import table (per-module relocation
//                                 // source list) offset/size.
//       u8  prolog_section, epilog_section,
//           unresolved_section, bss_section;
//       u32 prolog_offset, epilog_offset, unresolved_offset;
//       u32 align, bss_align;    // version>=2 fields; present (version 3)
//                                 // in every sample.
//       u32 fix_size;            // version>=3 field; present in every
//                                 // sample, observed equal to rel_offset
//                                 // in every sample seen here.
//     The relocation/import table bodies themselves (the actual per-
//     instruction fixup opcodes) are NOT decoded -- only the section
//     table and the header fields above; this matches the project's
//     existing depth convention for structurally-complex tables (the
//     section list is enough to validate and to locate each section's
//     raw bytes, same spirit as e.g. lib-magma's ".fat" record dump).
//
// (3) ".sel" / ".rso" -- also present on this disc (as
//     "LynWiiRetail.sel" and "Ai2CppWiiFinal.rso"), and superficially
//     from the same "relocatable module" family as ".rel" by name and by
//     genre (Lynx script engine bytecode module, and a C++/CLR-ish
//     "Ai2Cpp" runtime module respectively -- both third-party Wii
//     middleware, not first-party Nintendo). Checked directly against
//     the ".rel" layout above and confirmed NOT to match: interpreting
//     either sample's header with the REL field layout produces
//     obviously-nonsensical values (e.g. LynWiiRetail.sel: num_sections
//     would read as 88 with a name_size of 1; Ai2CppWiiFinal.rso:
//     section_info_offset would read as ~2.8MB into a 5.9MB file with
//     num_sections 88 and imp_size ~780KB, inconsistent with each
//     other and with the rest of the header). Only one real sample of
//     each exists on this disc, too few to reverse-engineer a distinct
//     record shape with confidence, so this module does NOT attempt to
//     detect or decode ".sel"/".rso" -- they are left unhandled,
//     consistent with the project convention of not asserting a layout
//     from a single sample.
#ifndef LIB_REDSTEEL2_H
#define LIB_REDSTEEL2_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// (1) ".bbf"/".BF" Ubisoft "ABE" bigfile -- header only, directory table
// not decoded (see big comment above for exactly why).
int IsRedSteel2Abe (const u8 *data, size_t size, size_t file_size);
enumError DecodeRedSteel2Abe_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".rel" Nintendo/CodeWarrior REL relocatable module -- full header +
// section table decode; relocation/import table bodies not decoded.
int IsRedSteel2Rel (const u8 *data, size_t size, size_t file_size);
enumError DecodeRedSteel2Rel_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // LIB_REDSTEEL2_H
