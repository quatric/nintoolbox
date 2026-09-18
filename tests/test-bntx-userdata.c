#include "lib-bntx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Mirrors tests/test-bntx-formats.c: stub out the dclib allocator and error
// shims so ScanBNTX/DumpStructureBNTX link standalone.

void trace_free (ccp f, ccp p, uint l, void *v) { (void)f; (void)p; (void)l; free (v); }
void *trace_malloc (ccp f, ccp p, uint l, size_t n) { (void)f; (void)p; (void)l; return malloc (n); }
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s) { (void)f; (void)p; (void)l; return calloc (n, s); }
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n) { (void)f; (void)p; (void)l; return realloc (v, n); }
void dclib_free (void *v) { free (v); }
void *dclib_malloc (size_t n) { return malloc (n); }
void *dclib_calloc (size_t n, size_t s) { return calloc (n, s); }
void *dclib_realloc (void *v, size_t n) { return realloc (v, n); }
enumError PrintError (enumError err, ccp format, ...) { (void)format; return err; }

int main (int argc, char **argv)
{
	// Verifies the LegacySwitchLibraries UserData port: UserDataType.String
	// payloads are `count` u64 offsets to length-prefixed UTF-8 strings and
	// UserDataType.WString payloads are offsets to UTF-16LE strings
	// (transcoded to UTF-8), per BntxFileLoader.LoadStrings(). The fixture
	// is built at test time by tests/mk_bntx_extra.py (ud.bntx): entry 0 is
	// STRING ["hello","world!"], entry 1 is WSTRING ["Hi\xc3\xa9"].
	if (argc != 2)
	{
		fprintf (stderr, "usage: %s file.bntx\n", argv[0]);
		return 2;
	}
	FILE *f = fopen (argv[1], "rb");
	assert (f);
	fseek (f, 0, SEEK_END);
	const long len = ftell (f);
	fseek (f, 0, SEEK_SET);
	assert (len > 0x40 && len < 0x1000000);
	u8 *buf = malloc ((size_t)len);
	assert (buf && fread (buf, 1, (size_t)len, f) == (size_t)len);
	fclose (f);

	bntx_t bntx;
	assert (ScanBNTX (&bntx, buf, (uint)len) == ERR_OK);
	assert (bntx.n_textures == 1);
	assert (bntx.textures[0].n_user_data == 2);

	const bntx_user_data_t *su = &bntx.textures[0].user_data[0];
	assert (su->type == BNTX_UD_STRING && su->count == 2 && su->val.str);
	assert (!strcmp (su->name, "test_str"));
	assert (su->val.str[0] && !strcmp (su->val.str[0], "hello"));
	assert (su->val.str[1] && !strcmp (su->val.str[1], "world!"));

	const bntx_user_data_t *wu = &bntx.textures[0].user_data[1];
	assert (wu->type == BNTX_UD_WSTRING && wu->count == 1 && wu->val.wstr);
	assert (!strcmp (wu->name, "test_wstr"));
	assert (wu->val.wstr[0] && !strcmp (wu->val.wstr[0], "Hi\xc3\xa9"));

	// Structural dump must survive string UserData (exercises the new
	// first-value printing), and ResetBNTX must free the owned copies.
	DumpStructureBNTX (stdout, &bntx, 0);
	ResetBNTX (&bntx);
	assert (bntx.textures == 0 && bntx.n_textures == 0);
	free (buf);

	printf ("BNTX UserData STRING/WSTRING test passed!\n");
	return 0;
}
