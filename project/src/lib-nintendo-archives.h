// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_NINTENDO_ARCHIVES_H
#define LIB_NINTENDO_ARCHIVES_H 1

#include "types.h"
#include "lib-nintendo.h"

// Extract Level-5 3DS/Switch Container Archive (.xc / .xpck / XPCK / XPC2)
enumError ExtractXPCKArchive (ccp arg, ccp basedir, uint depth);

// Extract Camelot GameCube/Wii Archive Table (.ztab / ZTAB)
enumError ExtractZTABArchive (ccp arg, ccp basedir, uint depth);

// Extract Dance Dance Revolution Mario Mix Chunk Archive (.mdr)
enumError ExtractMDRArchive (ccp arg, ccp basedir, uint depth);

// Extract Pikmin 1 & 2 Model/Archive Container (.pvol)
enumError ExtractPVOLArchive (ccp arg, ccp basedir, uint depth);

// Extract Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / .stpk / STPK)
enumError ExtractSTPKArchive (ccp arg, ccp basedir, uint depth);

// Extract GameCube Resource Archive (.res / res\n)
enumError ExtractF9ResArchive (ccp arg, ccp basedir, uint depth);

// Extract NES Remix indieszero Archive (.zlarc)
enumError ExtractZLARCArchive (ccp arg, ccp basedir, uint depth);

// Extract Grezzo Zelda / Luigi's Mansion 3DS Archive (.zar / .gar / ZAR\x01 / GAR\x02..GAR\x05)
enumError ExtractGARArchive (ccp arg, ccp basedir, uint depth);

// Extract Mario Kart Arcade GP DX Layout Archive (.pac / pack)
enumError ExtractMKGPDXPacArchive (ccp arg, ccp basedir, uint depth);

// Extract Twilight Princess HD / Zelda TMPK Archive (.pack / TMPK)
enumError ExtractTMPKArchive (ccp arg, ccp basedir, uint depth);

// Extract Nintendo Switch NX Archive (.nxarc / RAXN)
enumError ExtractNXARCArchive (ccp arg, ccp basedir, uint depth);

// Extract Nintendo APAK Archive (.apak / APAK)
enumError ExtractAPAKArchive (ccp arg, ccp basedir, uint depth);

// Extract PlatinumGames Archive (.pkz / pkz)
enumError ExtractPKZArchive (ccp arg, ccp basedir, uint depth);

// Extract Nintendo Switch Joy-Con Vibration Archive (.vibs)
enumError ExtractVIBSArchive (ccp arg, ccp basedir, uint depth);

// Extract PlatinumGames DAT Archive (.dat / .pkz / DAT)
enumError ExtractPGDATArchive (ccp arg, ccp basedir, uint depth);

// Extract PlatinumGames WT Archive (.wta / WTA )
enumError ExtractWTAArchive (ccp arg, ccp basedir, uint depth);

// Repack / Create functions
enumError CreateXPCKArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateZTABArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateMDRArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreatePVOLArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateSTPKArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateF9ResArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateZLARCArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_NINTENDO_ARCHIVES_H
