#ifndef X_BRAWL_H
#define X_BRAWL_H 1

#include "lib-std.h"
#include "lib-sf.h"

typedef struct BrawlOptions_t
{
	ccp source_image;
	ccp mod_folder;
	ccp gct_file;
	ccp dest_path;
	ccp custom_id;
	ccp custom_name;
	ccp banner_file;
	u32 gct_offset;
	bool remove_sse;
	bool no_gct_patch;
	bool no_alt_pad;
	bool interactive;
	bool test_mode;
	bool overwrite;
	bool keep_temp;
} BrawlOptions_t;

extern BrawlOptions_t brawl_options;

enumError BrawlCommand (BrawlOptions_t *opt);

#endif // X_BRAWL_H
