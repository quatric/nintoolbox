#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define VGMTRANS_FMT_SF2  1
#define VGMTRANS_FMT_DLS  2
#define VGMTRANS_FMT_BOTH (VGMTRANS_FMT_SF2 | VGMTRANS_FMT_DLS)

// Converts a game audio bank (e.g. Wii BRSAR) to MIDI + SF2/DLS files written
// into outDir, using the vgmtrans format scanners and SF2/DLS/MIDI exporters.
// Only 1 copy of the soundfont (SF2/DLS) is exported for the whole BRSAR archive.
// Returns 0 on success, non-zero on failure. No subprocess is spawned;
// this call runs entirely in-process.
int VgmtransConvertFile(const char *inFile, const char *outDir);
int VgmtransConvertFileExt(const char *inFile, const char *outDir, int formatFlags);

#ifdef __cplusplus
}
#endif
