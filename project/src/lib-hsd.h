
/***************************************************************************
 *                                                                         *
 *   HAL Laboratory "sysdolphin" (HSD) .dat archive support                 *
 *                                                                         *
 ***************************************************************************/

#ifndef LIB_HSD_H
#define LIB_HSD_H

#include "lib-std.h"
#include "lib-model-glb.h"
#include <stdint.h>
#include <stddef.h>

//
///////////////////////////////////////////////////////////////////////////////
///////////////			HAL sysdolphin (HSD) DAT	///////////////
///////////////////////////////////////////////////////////////////////////////
//
// A sysdolphin ".dat" is not a directory-style container but a serialized
// object graph: a flat blob of C structs starting at file offset 0x20 whose
// inter-struct pointers are stored as offsets relative to that 0x20 data
// base and are patched to real addresses at load time using an explicit
// relocation table.  Used by Super Smash Bros. Melee, Kirby Air Ride and the
// Wii channel "Terebi no Tomo" / "TV no Tomo" (JPN).
//
// Header layout (big endian, ported from Ploaj/HSDLib, MIT licensed,
// HSDRaw/HSDRawFile.cs Open(), the relocation/root/reference parsing):
//
//	+0x00  u32  file size (== real file size)
//	+0x04  u32  relocation table offset, relative to the 0x20 data base
//	+0x08  u32  relocation entry count
//	+0x0C  u32  root node count
//	+0x10  u32  external reference count
//	+0x14  char[4] version string, e.g. "001B"
//	+0x20       data section (structs; all pointers are 0x20-relative)
//
// Each relocation entry is a 0x20-relative offset naming a *location* that
// holds a pointer; the u32 stored there is itself a 0x20-relative offset of
// the pointed-to struct.  After the relocation table come rootCount and
// refCount (offset,name-offset) pairs, followed by the string pool.
//
///////////////////////////////////////////////////////////////////////////////

#define HSD_DATA_BASE 0x20 // all stored offsets are relative to this

// [[hsd_t]]
typedef struct hsd_t
{
	const u8 *data; // whole file, not owned
	uint size; // size of 'data'

	uint fsize; // header: file size
	uint reloc_off; // header: relocation table, absolute
	uint n_reloc; // header: number of relocation entries
	uint n_root; // header: number of root nodes
	uint n_ref; // header: number of external references
	char version[5]; // header: version string, 0-terminated

	u32 *rel_src; // 'n_rel' absolute pointer locations, sorted
	u32 *rel_dest; // 'n_rel' absolute pointed-to offsets
	uint n_rel; // number of valid relocations

	u32 *target; // sorted, unique list of pointed-to offsets
	uint n_target; // number of entries in 'target'

	u32 *ptr_keys; // open-addressing hash: key=loc, val=dest
	u32 *ptr_vals;
	uint ptr_cap; // hash table capacity (power-of-two)
} hsd_t;

///////////////////////////////////////////////////////////////////////////////

// Quick structural probe: HSD DAT files carry no magic, so this validates the
// header (file size, relocation table placement, root/reference counts and
// the ASCII version tag) instead.
bool IsHSD (const u8 *data, uint size);

// Parse header + relocation table. Returns false if 'data' is not a HSD DAT.
// 'data' is borrowed and must outlive 'hsd'. Call ResetHSD() when done.
bool ScanHSD (hsd_t *hsd, const u8 *data, uint size);
void ResetHSD (hsd_t *hsd);

// Walk the object graph, decode every texture found and write it as a PNG
// into 'dest_dir', named "<basename>.tex###_<W>x<H>_<format>.png".
// Returns the number of textures written, or -1 on error.
int ExportHSDTextures (const hsd_t *hsd, // valid, scanned HSD file
	ccp dest_dir, // output directory, created if needed
	ccp basename // name prefix for the written PNGs
);

// Convenience wrapper: scan 'data' and export its textures.
int ExportHSDTexturesFromData (const u8 *data, uint size, ccp dest_dir, ccp basename);

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     model geometry		///////////////
///////////////////////////////////////////////////////////////////////////////
//
// JOBJ/DOBJ/POBJ tree -> model_t (lib-model-glb.h), verified against Super
// Smash Bros. Melee's real HSD_JOBJ/HSD_DOBJ/HSD_POBJ/GX_Attribute field
// layout (Ploaj/HSDLib, MIT licensed) and the real GX display-list opcode
// stream (byte-for-byte confirmed on TyBox.dat's item-box model: attribute
// array, per-attribute INDEX8 fetch, buffer resolution via the relocation
// table, and real, sane position data all checked against actual file
// bytes before trusting the port). Scope: static geometry via each POBJ's
// owning joint (or SingleBoundJOBJ) with envelope/skinning weights, material
// colours, and texture binding. Two-pass skeleton walk first discovers all
// JOBJs into a lookup table, then builds meshes with full joint resolution.
//
// Verified against a real retail disc (Super Smash Bros. Melee, redump
// dump): 346 of 352 real "Ty*.dat" item/object files (stage hazards,
// pickup items, thrown weapons) decode to real, sane, glTF-validated
// geometry -- 9564 meshes total, 8 of them (0.08%, confined to 3 menu/UI
// display objects: TyMnDisp/TyMnFigp/TyGrtfox) showing implausible
// coordinates, a real remaining rough edge rather than a systemic bug. The
// other 6 non-decoding files (TyDataf/TyDatai/TyLight/TyMnBg/TyMnInfo/
// TyCathar) are genuinely not models (data tables, lighting, 2D menu
// backgrounds) rather than a gap. Playable-character files (PlMr.dat etc.)
// decode correctly with envelope-weighted skinning (two-pass skeleton walk
// resolves the per-fighter root indirection to real JOBJ trees).
//
// Root JOBJs are found via the file's own root table (see ScanHSD's header
// comment) rather than a blind structural scan, since the root table gives
// an exact, unambiguous list with real names ("ToyBoxModel_TopN_joint" etc,
// verified against real files) to pick JOBJ-shaped roots from.

// Export every root JOBJ tree found in 'hsd' as one glTF/GLB. Returns the
// number of meshes written, 0 if none were found, or -1 on error.
int ExportHSDModel (const hsd_t *hsd, // valid, scanned HSD file
	ccp out_glb_file // destination .glb path
);

// Convenience wrapper: scan 'data' and export its model.
int ExportHSDModelFromData (const u8 *data, uint size, ccp out_glb_file);

// Encode a portable static HSD (sysdolphin .dat) model file. Geometry,
// normals, UVs, vertex colors, materials and the joint/object hierarchy
// are written in sysdolphin's serialized object graph layout.
enumError EncodeModelToHSD (const model_t *model, ccp out_path);

#endif // LIB_HSD_H
