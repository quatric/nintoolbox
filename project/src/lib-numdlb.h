#ifndef LIB_NUMDLB_H
#define LIB_NUMDLB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH model descriptor (.numdlb / .nusrcmdlb), Super Smash Bros.
// Ultimate. Reference: ultimate-research/ssbh_lib ssbh_lib/src/formats/modl.rs
// (Modl::V17, ModlEntry). Ties the mesh (.numshb), skeleton (.nusktb),
// materials (.numatb) and, optionally, animation (.nuanmb) file names
// together and assigns a material label to each mesh object.

bool IsNUMDLB (const u8 *data, size_t size);

enumError DecodeNUMDLB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NUMDLB_H
