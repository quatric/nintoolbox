// SPDX-License-Identifier: GPL-2.0+
// Havok "classic packfile" binary tagfile (magic 0x57E0E057), as shipped by
// Tenchu: Shadow Assassins (Wii, Acquire, 2009) for skeletons/animations/
// ragdolls under DATA/files/Common/Motion/*.HKX. Not a proprietary format --
// Havok is licensed middleware -- but the classic (pre-2011 "tagfile")
// packfile layout is undocumented by Havok itself and every open-source
// reimplementation found (HavokLib, hkxparse, hkxcmd, etc.) targets Havok
// 5.0+ or the 2010+ tagfile variant. This title embeds "Havok-4.6.1-r1",
// which predates all of those.
//
// Structure confirmed directly against retail samples (e.g.
// Common/Motion/Skeleton/skeleton_main.HKX, Common/Motion/Face/*.HKX):
// a fixed hkPackfileHeader --
//   u32 magic0 (0x57E0E057), u32 magic1 (0x10C0C010), u32 userTag,
//   u32 fileVersion, u8 layoutRules[4] (ptrSize, littleEndian,
//   reusePaddingOptimization, emptyBaseClass -- observed {4,0,1,1}, i.e.
//   32-bit pointers, big-endian, matching the Wii target), u32 numSections,
//   u32 contentsSectionIndex, u32 contentsSectionOffset,
//   u32 contentsClassNameSectionIndex, u32 contentsClassNameSectionOffset,
//   char contentsVersion[16] (NUL-padded, e.g. "Havok-4.6.1-r1"), then
//   padding to a 48-byte section-header array --
// followed by `numSections` 48-byte hkPackfileSectionHeader entries:
//   char tagname[20] (NUL-padded, e.g. "__classnames__", "__types__",
//   "__data__"), u32 absoluteDataStart, u32 localFixupsOffset,
//   u32 globalFixupsOffset, u32 virtualFixupsOffset, u32 exportsOffset,
//   u32 importsOffset, u32 endOffset (the last five all relative to
//   absoluteDataStart; a section with no fixups/exports/imports has all
//   five equal). Verified byte-for-byte on skeleton_main.HKX: the
//   "__classnames__" section's absoluteDataStart/endOffset exactly bound
//   the next section's header, and every section boundary in the file
//   checks out this way.
//
// This is a self-describing packfile -- the "__types__" section holds a
// full hkClass/hkClassMember reflection table for every type used, so a
// generic reader does not need Havok's SDK or a per-version layout file to
// know what's *in* a file. What this module decodes: the header, the full
// section table (name/offset/size for however many sections exist -- not
// hardcoded to 3), and, from the "__classnames__" section, every embedded
// Havok class name (plain ASCII strings prefixed "hk"/"hcl", extracted by
// scanning for NUL-terminated runs rather than by reverse-engineering the
// exact per-entry record format, which was not pinned down). Reported class
// names alone are enough to identify a file's content (e.g. skeleton vs.
// ragdoll vs. animation) without decoding the "__data__" section itself,
// which is not attempted here.

#ifndef SZS_LIB_HKX_H
#define SZS_LIB_HKX_H 1

#include "types.h"
#include <stdio.h>

int IsHKX (const u8 *data, size_t size, size_t file_size);
enumError DecodeHKX_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_HKX_H
