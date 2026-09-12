// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_WTA_H
#define LIB_WTA_H 1

#include "lib-nintendo.h"

// PlatinumGames WT Archive (.wta / WTA )
enumError ExtractWTAArchive (ccp arg, ccp basedir, uint depth);

// PlatinumGames WTA/WTP texture bundles, Wii U big-endian form
// (`\0BTW` index + sibling .wtp payloads, Star Fox Zero). Members are
// wrapped as .gtx containers for the normal GTX->PNG cascade; layout
// per RandomTBush's Bayo2-SF0-SFG_WTA-WTP.bms (re-implemented).
enumError ScanWTA (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *wta, uint wta_size,
	const u8 *wtp, uint wtp_size);

#endif // LIB_WTA_H
