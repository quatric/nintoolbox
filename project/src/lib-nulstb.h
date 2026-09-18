#ifndef LIB_NULSTB_H
#define LIB_NULSTB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH file-name list (.nulstb), Super Smash Bros. Ultimate.
// Reference: ultimate-research/ssbh_lib ssbh_lib/src/formats/nlst.rs
// (Nlst::V10) -- just a flat array of file names to load.

bool IsNULSTB (const u8 *data, size_t size);
enumError DecodeNULSTB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NULSTB_H
