// SPDX-License-Identifier: GPL-2.0+
// Tenchu: Shadow Assassins "T4-*" tagged resource -- see lib-t4res.h for
// exactly what is and is not understood about it.

#include "lib-t4res.h"
#include "dclib-debug.h"
#include <string.h>
#include <ctype.h>
#include <errno.h>

//-----------------------------------------------------------------------------

#define T4_TAG_FIELD 12 // observed fixed padded width of the "T4-..." name

int IsT4Res (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < T4_TAG_FIELD + 4 || file_size < T4_TAG_FIELD + 4)
		return 0;
	if (memcmp (data, "T4-", 3) != 0)
		return 0;

	// name must be printable ASCII up to a NUL within the fixed field
	int saw_nul = 0;
	for (int i = 3; i < T4_TAG_FIELD; i++)
	{
		if (!data[i]) { saw_nul = 1; continue; }
		if (saw_nul)
		{
			if (data[i]) return 0; // non-zero byte after the NUL terminator
			continue;
		}
		if (!isalnum (data[i]) && data[i] != '_')
			return 0;
	}
	return saw_nul;
}

//-----------------------------------------------------------------------------

enumError DecodeT4Res_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !IsT4Res (data, size, file_size))
		return EINVAL;

	char name[T4_TAG_FIELD + 1];
	memcpy (name, data, T4_TAG_FIELD);
	name[T4_TAG_FIELD] = 0;

	const u8 *bcd = data + T4_TAG_FIELD;
	int date_valid = 1;
	for (int i = 0; i < 4; i++)
		if ((bcd[i] & 0xf) > 9 || (bcd[i] >> 4) > 9)
			date_valid = 0;

	fprintf (f, "# Tenchu: Shadow Assassins \"%s\" tagged resource\n", name);
	fprintf (f, "file_size = %zu\n", file_size);
	fprintf (f, "tag = %s\n", name);
	if (date_valid)
		fprintf (f, "date (packed BCD, best guess) = %02x%02x-%02x-%02x\n",
			bcd[0], bcd[1], bcd[2], bcd[3]);
	fprintf (f, "# record table layout is type-specific and not decoded here\n");

	return ERR_OK;
}
