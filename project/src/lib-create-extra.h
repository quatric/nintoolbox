#ifndef SZS_LIB_CREATE_EXTRA_H
#define SZS_LIB_CREATE_EXTRA_H 1

#include "lib-std.h"

// Table-driven CREATE for flat archives repacked from an extracted directory.
// Returns false (and touches nothing) if `ext` is not one of the handled
// suffixes; otherwise sets *err and returns true. See lib-create-extra.c.
bool create_extra_dir (ccp ext, ccp source, ccp dest, enumError *err);
ccp create_extra_name (ccp ext);
enumError create_aal_yaml_file (ccp src, ccp dest);

#endif
