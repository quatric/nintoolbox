// FileTypeTab[] is indexed directly by the file_format_t value (see
// GetNameFF() and friends in lib-file.c), so every row must sit at the index
// its own fform names. Removing or inserting a format shifts everything after
// it, and a table that drifts out of step with the enum mislabels every later
// format rather than failing outright -- so check the whole table.
#include <stdio.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "file-type.h"
#ifdef __cplusplus
}
#endif
extern void trace_free (const char*, const char*, unsigned int, void*);
extern void *trace_calloc (const char*, const char*, unsigned int, size_t, size_t);
extern void *trace_malloc (const char*, const char*, unsigned int, size_t);

int main (void)
{
	int fail = 0, checked = 0;
	for (int i = 0; i < FF_N; i++)
	{
		const file_type_t *ft = FileTypeTab + i;
		checked++;
		if ((int)ft->fform != i)
		{
			printf ("FAIL row %d holds fform %d (%s)\n", i, (int)ft->fform,
				ft->name ? ft->name : "?");
			fail = 1;
		}
		else if (!ft->name || !*ft->name)
		{
			printf ("FAIL row %d has no name\n", i);
			fail = 1;
		}
	}
	printf ("checked %d rows, FF_N=%d\n", checked, FF_N);
	printf (fail ? "RESULT: FAIL\n" : "RESULT: ok\n");
	return fail;
}
