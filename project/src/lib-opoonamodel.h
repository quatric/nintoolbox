// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Opoona (Wii, ArtePiazza) ".mdl" model resource -> model_t.
//
// A character/object model is an "OPKG" package (see wszst_cmd/extract/opoona.inc)
// whose members are the model chunk stream (".mdl"), a TPL bundle (".tex") and a
// material side table (".tmp"). The model chunk stream is a list of big-endian
// {char tag[4], u32 size, data} chunks:
//   DHDM  header                              BODM  nodes, 0xc0 bytes each
//   TMDM  materials, 0x110 bytes each         SMDM  u32 offsets of the meshes
//   MBDM, CCDM  not interpreted               TSDM  node name strings
// Layouts were taken from the game's own loader (main.dol, FUN_8000b8c0) and
// verified against the retail character models.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_OPOONAMODEL_H
#define SZS_LIB_OPOONAMODEL_H 1

#include "lib-std.h"
#include "lib-model-glb.h"

bool IsOpoonaModel (const u8 *data, size_t size);

// 'tex_names' (ntex entries) are the texture identifiers material texture
// indices refer to (".tex" package order). Returns a model_t for FreeModel() or NULL.
model_t *ParseOpoonaModel (const u8 *data, size_t size, char tex_names[][64], uint ntex);

// Appends the ".mot" clip 'data' as animation 'name' (see lib-opoonamodel.c for the
// layout). False if the node count differs from the model or the file is malformed.
bool OpoonaAddMotion (model_t *model, const u8 *data, size_t size, const char *name);

#endif
