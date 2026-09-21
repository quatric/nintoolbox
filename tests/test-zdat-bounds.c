// ZDAT builder size and input validation.
#include "lib-zdat.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup

void trace_free (ccp f, ccp p, uint l, void *v)
{
	(void)f;
	(void)p;
	(void)l;
	free (v);
}
void *trace_malloc (ccp f, ccp p, uint l, size_t n)
{
	(void)f;
	(void)p;
	(void)l;
	return malloc (n);
}
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s)
{
	(void)f;
	(void)p;
	(void)l;
	return calloc (n, s);
}
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n)
{
	(void)f;
	(void)p;
	(void)l;
	return realloc (v, n);
}
void dclib_free (void *v)
{
	free (v);
}
void *dclib_malloc (size_t n)
{
	return malloc (n);
}
void *dclib_calloc (size_t n, size_t s)
{
	return calloc (n, s);
}
void *dclib_realloc (void *v, size_t n)
{
	return realloc (v, n);
}
char *dclib_strdup (ccp s)
{
	return s ? strdup (s) : 0;
}

int main (int argc, char **argv)
{
	u8 *out = 0;
	uint size = 0;
	const u8 byte = 0x42;
	nintendo_sarc_entry_t entry = { 0 };
	entry.name = "payload";
	entry.data = &byte;
	entry.size = 0xffffffffu;
	if (argc == 1 || !strcmp (argv[1], "overflow"))
	{
		if (CreateZDATArchive (&out, &size, &entry, 1, 0) == ERR_OK || out)
			return 1;
	}
	entry.data = 0;
	entry.size = 1;
	if (argc == 1 || !strcmp (argv[1], "null"))
	{
		if (CreateZDATArchive (&out, &size, &entry, 1, 0) == ERR_OK || out)
			return 2;
	}
	entry.size = 0;
	if (argc == 1 || !strcmp (argv[1], "empty"))
	{
		if (CreateZDATArchive (&out, &size, &entry, 1, 0) != ERR_OK || !out
			|| size != 48 + strlen (entry.name))
			return 3;
		free (out);
	}
	puts ("ZDAT builder bounds passed");
	return 0;
}
