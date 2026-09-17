#ifndef LIB_NUSKTB_H
#define LIB_NUSKTB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH skeleton (.nusktb), Super Smash Bros. Ultimate. Reference:
// ultimate-research/ssbh_lib ssbh_lib/src/formats/skel.rs (Skel::V10,
// SkelBoneEntry, SkelEntryFlags, BillboardType), same container convention
// already verified in lib-numsh.c against 151 retail skeletons.

bool IsNUSKTB (const u8 *data, size_t size);

// Lists every bone's index, parent index, billboard type, and world-space
// position (the translation column of its world_transforms matrix -- the
// same matrix/column convention ParseNUMSHBSkinned() already reads).
enumError DecodeNUSKTB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NUSKTB_H
