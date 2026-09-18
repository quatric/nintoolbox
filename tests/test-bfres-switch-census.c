#include "lib-bfres.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

// Mirrors tests/test-bntx-formats.c: stub out allocators, errors and the two
// external link dependencies of lib-bfres.o (FZIP wrapper + model freezer)
// so the Switch archive census links standalone. Neither stub is reached by
// ParseBFRESArchive/ParseBFRESAnims.

void trace_free (ccp f, ccp p, uint l, void *v) { (void)f; (void)p; (void)l; free (v); }
void *trace_malloc (ccp f, ccp p, uint l, size_t n) { (void)f; (void)p; (void)l; return malloc (n); }
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s) { (void)f; (void)p; (void)l; return calloc (n, s); }
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n) { (void)f; (void)p; (void)l; return realloc (v, n); }
void dclib_free (void *v) { free (v); }
void *dclib_malloc (size_t n) { return malloc (n); }
void *dclib_calloc (size_t n, size_t s) { return calloc (n, s); }
void *dclib_realloc (void *v, size_t n) { return realloc (v, n); }
enumError PrintError (enumError err, ccp format, ...) { (void)format; return err; }
enumError DecodeFZIP (u8 **a, uint *b, const u8 *c, uint d)
{
	(void)a; (void)b; (void)c; (void)d;
	return EINVAL;
}
void FreeModel (model_t *model) { (void)model; }

static uint8_t *load_file (const char *path, size_t *size_out)
{
	FILE *f = fopen (path, "rb");
	assert (f);
	fseek (f, 0, SEEK_END);
	const long len = ftell (f);
	fseek (f, 0, SEEK_SET);
	assert (len > 0x100 && len < 0x10000000);
	uint8_t *buf = malloc ((size_t)len);
	assert (buf && fread (buf, 1, (size_t)len, f) == (size_t)len);
	fclose (f);
	*size_out = (size_t)len;
	return buf;
}

static const bfres_slot_census_t *find_slot (const bfres_archive_t *arch, const char *magic)
{
	for (uint8_t i = 0; i < arch->n_slots; i++)
		if (!memcmp (arch->slots[i].magic, magic, 4))
			return &arch->slots[i];
	return 0;
}

int main (int argc, char **argv)
{
	// Switch ResFile census (LegacySwitchLibraries ResFile.cs lists) against
	// the Tomodachi Life retail fixture: 1 FMDL model and 6 FSKA skeletal
	// animations, no material/visibility/shape/scene anims and no attached
	// external files.
	if (argc != 2)
	{
		fprintf (stderr, "usage: %s file.bfres\n", argv[0]);
		return 2;
	}
	size_t size = 0;
	uint8_t *data = load_file (argv[1], &size);

	bfres_archive_t arch;
	assert (ParseBFRESArchive (data, size, &arch) == 1);
	assert (arch.n_objects == 7);
	const bfres_slot_census_t *fmdl = find_slot (&arch, "FMDL");
	const bfres_slot_census_t *fska = find_slot (&arch, "FSKA");
	assert (fmdl && fmdl->count_dict == 1);
	assert (fska && fska->count_dict == 6);
	assert (!find_slot (&arch, "FMAA"));
	assert (!find_slot (&arch, "FBVS"));
	assert (!find_slot (&arch, "FSHA"));
	assert (!find_slot (&arch, "FSCN"));
	assert (!find_slot (&arch, "EXTF"));

	bfres_anim_entry_t *anims = 0;
	const int n = ParseBFRESAnims (data, size, &anims);
	assert (n == 6 && anims);
	for (int i = 0; i < n; i++)
		assert (!memcmp (anims[i].cls, "FSKA", 4) && anims[i].frames > 0);
	free (anims);
	free (data);

	printf ("BFRES Switch census test passed!\n");
	return 0;
}
