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

// Extract Game Freak Pokemon Archive (.gfpak / GFLXPACK)
enumError ExtractGFPAKArchive (ccp arg, ccp basedir, uint depth);

// Extract Nintendo Binary Audio Resource Archive (.bars / BARS)
enumError ExtractBARSArchive (ccp arg, ccp basedir, uint depth);

// Extract Next Level Games Dictionary Archive (.dict / LM2 / LM3 / Punch-Out!!)
enumError ExtractNLGDictArchive (ccp arg, ccp basedir, uint depth);

// Extract Next Level Games Texture To Go (.txtg / 6PK0)
enumError ExtractTXTGArchive (ccp arg, ccp basedir, uint depth);

// Extract Nintendo 3DS RomFS Archive (.romfs / IVFC)
enumError ExtractROMFSArchive (ccp arg, ccp basedir, uint depth);

// Extract Nintendo Switch XTX Texture Container (.xtx / DFvN)
enumError ExtractXTXArchive (ccp arg, ccp basedir, uint depth);

// Extract Koei Tecmo / Gust Texture Volume Archive (.tvol)
enumError ExtractTVOLArchive (ccp arg, ccp basedir, uint depth);

// Extract Nintendo Switch MTXT Texture Archive (.mtxt / MTXT)
enumError ExtractMTXTArchive (ccp arg, ccp basedir, uint depth);

// Extract Pokemon Mystery Dungeon Resource Container (.sir0 / SIR0)
enumError ExtractSIR0Archive (ccp arg, ccp basedir, uint depth);

// Extract Next Level Games PTLG texture container (.glt / .rlt), as used by
// Super Mario Strikers (GameCube) and Mario Strikers Charged (Wii). Each
// texture is written as a standalone TPL, since PTLG stores plain GX pixel
// data in the same formats TPL wraps.
enumError ExtractPTLGArchive (ccp arg, ccp basedir, uint depth);
enumError CreatePTLGArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool is_gc);

// Extract Bandai Namco NUS3AUDIO Audio Archive (.nus3audio / NUS3)
enumError ExtractNUS3AudioArchive (ccp arg, ccp basedir, uint depth);

// Repack / Create functions
enumError CreateXPCKArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateZTABArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateMDRArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreatePVOLArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateSTPKArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateF9ResArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateZLARCArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateAPAKArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateNXARCArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreatePKZArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateTMPKArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateVIBSArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateMTXTArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateMKGPDXPacArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateSIR0Archive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);
enumError CreateGARArchive (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

#endif // LIB_NINTENDO_ARCHIVES_H
