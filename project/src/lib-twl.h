// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nintendo DSi ("TWL") title packaging and crypto.
//
// Three things live here, all using the DSi's AES scheme as implemented by
// WinterMute/twltool (WulfyStylez, CaitSith2) and documented by GBATEK and
// DSiBrew:
//
// (1) Installable ".tad" files, the DSi's WAD (TwlSDK "maketad", TDT/TwlNmenu
//     install them). Same container as a Wii WAD, big-endian:
//       0x00 u32 header_size (0x20)
//       0x04 char[2] type ("Is"; "ib" and "Bk" are the Wii variants)
//       0x06 u16 version
//       0x08 u32 cert_size, crl_size, tik_size, tmd_size, data_size, footer_size
//     followed by cert chain, CRL, ticket, TMD, content data and footer, each
//     starting on a 0x40 boundary. Content i is stored as AES-128-CBC with
//     IV = {content index BE u16, 14 zero bytes}, padded to 16, and the
//     ticket carries the title key wrapped by the DSi common key with
//     IV = {title id, 8 zero bytes}. The TMD content records hold the plain
//     size and SHA-1, which is what identifies the right common key
//     (retail, debug, or the Wii debug key) after the fact.
//
// (2) "DSiWare export" .bin files that the DSi writes to an SD card
//     (GBATEK "Tad Files"). Decrypted layout, each part an "ES block" =
//     data followed by a 0x20-byte metablock (16-byte MAC + 16-byte info):
//       0x0000 0x4000  banner (icon/title)      fixed key
//       0x4020 0xb4    header, ID "4ANT"        fixed key
//       0x40f4 0x440   footer: SHA-1s + certs   fixed key
//       0x4554 ...     title.tmd, app (SRL), 7 unused, public.sav, banner.sav
//     The tmd and the app use a console-specific key derived from the
//     ConsoleID, which the TW certificate in the footer spells out as 16 hex
//     digits, so the file is self-sufficient.
//
// (3) "modcrypt": the retail/debug AES-CTR layer over up to two regions of a
//     DSi SRL, marked by header byte 0x1c bit 1.
//
// Extract-only. Keys and the ES block construction follow twltool exactly.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_TWL_H
#define SZS_LIB_TWL_H 1

#include "lib-nintendo.h"

// --- primitives (exported for tests and for tools built on them) -----------

// key = (C + (X ^ Y)) <<< 42, the DSi/3DS key scrambler, all values little-endian.
void TwlKeyXY (u8 key[16], const u8 keyx[16], const u8 keyy[16]);

// ES block: 'buf' holds 'size' data bytes, 'meta' the 32-byte metablock that
// follows them. Decrypts in place. 0 = ok, -1 bad magic (wrong key), -2 size
// mismatch, -3 MAC mismatch (buf is still decrypted).
int TwlEsDecrypt (const u8 key[16], u8 *buf, const u8 meta[32], u32 size);
// Encrypts in place and fills 'meta'. 'nonce' may be NULL for an all-zero one.
void TwlEsEncrypt (const u8 key[16], u8 *buf, u8 meta[32], u32 size, const u8 nonce[12]);

// Console-specific ("variable") ES key from the 8 ConsoleID bytes as they are
// printed in the TW certificate (first byte = first hex digit pair).
void TwlTadVarKey (u8 key[16], const u8 console_id[8]);

// --- SRL modcrypt ----------------------------------------------------------

// True if the SRL header says modcrypt regions are present and encrypted.
bool TwlSrlIsModcrypted (const u8 *srl, size_t size);
// Toggles the modcrypt layer in place (same operation both ways, like
// twltool). Returns false if the header offsets do not fit the buffer.
bool TwlSrlModcrypt (u8 *srl, size_t size);
// Marks a decrypted SRL as no longer modcrypted: clears header 0x1c bit 1 and,
// if the header CRC16 at 0x15e was valid, recomputes it. (twltool leaves the
// flag alone, which makes decrypting the result a second time re-encrypt it.)
bool TwlSrlClearModcryptFlag (u8 *srl, size_t size);

// --- containers -------------------------------------------------------------

bool IsDsiTad (const u8 *data, size_t size);
enumError ScanDsiTad (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

bool IsDsiExportBin (const u8 *data, size_t size);
enumError ScanDsiExportBin (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif // SZS_LIB_TWL_H
