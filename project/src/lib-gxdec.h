// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Small GX texture decoders for engines that store a CMPR colour plane
// followed by a separate 4-bit alpha plane (Virtua Tennis 2009 PTEX, Petz
// "atxl" TEXL textures).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_GXDEC_H
#define SZS_LIB_GXDEC_H 1

#include "lib-std.h"

// Decode a GX CMPR plane (w*h/2 bytes; w,h multiples of 8) and optionally an
// I4 alpha plane (w*h/2 bytes, 8x8 tiles, NULL = opaque) into a malloc'ed,
// tightly packed w*h RGBA8 buffer (free with FREE, or hand over to
// SaveDecodedRGBAToPNG). Returns NULL on bad parameters / out of memory.
u8 *GxDecodeCmprAlpha4 (const u8 *cmpr, const u8 *alpha4, uint w, uint h);

#endif
