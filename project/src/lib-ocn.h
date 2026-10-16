
/***************************************************************************
 *                                                                         *
 *   Paon "OCN." skinned model container                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef LIB_OCN_H
#define LIB_OCN_H

#include "lib-std.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////			Paon OCN model			///////////////
///////////////////////////////////////////////////////////////////////////////
//
// Character/prop model of Donkey Kong: Barrel Blast (Wii), found as chara/
// *_1200/_2500/_4000.dat (LZ10 compressed). Reverse engineered; the layout:
//
//	+0x00	"OCN." (4), u32 0x18, u32 version/flags, u32 chunk count, pad to 0x20
//
// Chunks follow from 0x20. A chunk is a 4 byte tag stored byte-reversed
// ("SXET" is TEXS), a u32 size that excludes those 8 bytes, and 0x18 more
// header bytes (so the payload of every chunk starts at +0x20). Fields are big
// endian. Containers hold their children after the 0x20 byte header and end
// with an ENDE chunk:
//
//	TEXS  -> TEXT*    u16 index at +0x0C; at +0x20 u16 GX format, u16 w, u16 h;
//			  pixels at +0x40
//	MDLS  -> MDEL  -> MATE*  u16 texture index at +0x14
//			  BONE*  (NAME child at +0x40, name at +0x60)
//			  SHAP*  -> ELEM* (TXCD child: u16 u,v pairs, 1/1024)
//			            MESH* (u16 material at +0x16) -> PRIM ->
//			              VIDL  GX display list of (pos,nrm,uv) u16 indices
//			              VIDX  redundant strip index list
//			              WEIG  skinned vertex pool
//
// BONE: u16 index, u16 parent (0xffff none) at +0x08/+0x0A, quaternion xyzw at
// +0x10, scale at +0x20, translation at +0x30 (all f32).
//
// WEIG: u16 vertex count at +0x0C of its header; records of 4 + 16*k bytes:
//	u8 bone[4]
//	k * { s16 x,y,z (1/512 units, local to the bone), u16 weight (0x400 = 1) }
//	k * { s16 nx,ny,nz (1/16384), pad }
// followed by an unused 4*k byte per vertex array, padded to 0x20.

bool IsOCN (const u8 *data, uint size);

// Writes <out_glb_file> plus "<tex_base>.texNNN.png" beside it. Returns the
// mesh count, 0 for an OCN without geometry, -1 on error.
int ExportOCNModel (const u8 *data, uint size, ccp out_glb_file, ccp tex_base);

#endif // LIB_OCN_H
