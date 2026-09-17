#ifndef LIB_NUHLPB_H
#define LIB_NUHLPB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH helper-bone constraints (.nuhlpb), Super Smash Bros.
// Ultimate. Reference: ultimate-research/ssbh_lib ssbh_lib/src/formats/hlpb.rs
// (Hlpb::V11, AimConstraint, OrientConstraint, ConstraintType).

bool IsNUHLPB (const u8 *data, size_t size);

// Lists every aim and orient constraint (names, referenced bones, and the
// constraint's own tunable values), plus the ordered list tying them
// together (constraint_indices/constraint_types: which constraint of which
// kind applies at each step).
enumError DecodeNUHLPB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NUHLPB_H
