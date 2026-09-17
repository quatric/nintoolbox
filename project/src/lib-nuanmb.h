#ifndef LIB_NUANMB_H
#define LIB_NUANMB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH skeletal/material animation (.nuanmb), Super Smash Bros.
// Ultimate. Reference: ultimate-research/ssbh_lib ssbh_lib/src/formats/anim.rs
// (Anim::V12/V20/V21).
//
// Lists the animation's name, frame count, and its full track hierarchy
// (version 1.2: a flat track list with named properties; version 2.0/2.1:
// group -> node -> track) -- each track's type, compression scheme, frame
// count, and its own compressed keyframe data's offset+size within the
// file's data buffer. The per-frame keyframe data itself is a separate,
// non-trivial bit-packed compression scheme (documented only in the
// companion ssbh_data crate, not ssbh_lib) and isn't decoded here, same
// scope already established for BNSH's/NUSHDB's embedded GPU binaries.

bool IsNUANMB (const u8 *data, size_t size);
enumError DecodeNUANMB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NUANMB_H
