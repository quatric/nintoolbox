#ifndef SZS_LIB_SDAT_H
#define SZS_LIB_SDAT_H 1

#include "lib-std.h"

// Nintendo DS Nitro SDAT sound archive. See lib-sdat.c. The directory form
// accepts .sseq, .sbnk and .swar files; .txt files are assembled as SSEQ.
enumError PackSDATDir (u8 **out_data, size_t *out_size, ccp input_dir);
enumError UnpackSDAT (const u8 *data, size_t size, ccp out_dir);

#endif
