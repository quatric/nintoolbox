#ifndef SZS_LIB_COD_PAK_H
#define SZS_LIB_COD_PAK_H 1

#include "lib-std.h"

// Call of Duty: Black Ops / MW3 (Wii) sound archive (PAK0). See lib-cod-pak.c.
bool looks_like_cod_pak_dir (ccp source);
enumError create_cod_pak_dir (ccp source, ccp dest);

#endif
