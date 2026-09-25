// SPDX-License-Identifier: GPL-2.0+
// Havok classic packfile (.HKX) -- see lib-hkx.h for exactly what is and is
// not understood about it.

#include "lib-hkx.h"
#include "dclib-debug.h"
#include "dclib-basics.h"
#include <string.h>
#include <ctype.h>
#include <errno.h>

//-----------------------------------------------------------------------------

#define HKX_MAGIC0   0x57e0e057
#define HKX_MAGIC1   0x10c0c010
#define HKX_HDR_SIZE 0x40 // through contentsVersion[16] + 16 bytes padding
#define HKX_SEC_SIZE 48
#define HKX_MAX_SECTIONS 64

int IsHKX (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < HKX_HDR_SIZE || file_size < HKX_HDR_SIZE)
		return 0;
	return be32 (data) == HKX_MAGIC0 && be32 (data + 4) == HKX_MAGIC1;
}

//-----------------------------------------------------------------------------

typedef struct hkx_section_t
{
	char name[21];
	u32  data_start;
	u32  end_offset; // relative to data_start
} hkx_section_t;

enumError DecodeHKX_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !IsHKX (data, size, file_size))
		return EINVAL;

	const u32 user_tag       = be32 (data + 0x08);
	const u32 file_version   = be32 (data + 0x0c);
	const u8  ptr_size       = data[0x10];
	const u8  little_endian  = data[0x11];
	const u32 num_sections   = be32 (data + 0x14);
	const u32 cont_sec_idx   = be32 (data + 0x18);
	const u32 cont_sec_off   = be32 (data + 0x1c);
	const u32 cont_cls_idx   = be32 (data + 0x20);
	const u32 cont_cls_off   = be32 (data + 0x24);

	char version[17];
	memcpy (version, data + 0x28, 16);
	version[16] = 0;
	for (int i = 0; i < 16; i++)
		if (!isprint ((unsigned char) version[i]))
		{
			version[i] = 0;
			break;
		}

	fprintf (f, "# Havok classic packfile (.HKX)\n");
	fprintf (f, "file_size = %zu\n", file_size);
	fprintf (f, "contents_version = %s\n", version);
	fprintf (f, "user_tag = %u\n", user_tag);
	fprintf (f, "file_version = %u\n", file_version);
	fprintf (f, "ptr_size = %u\n", ptr_size);
	fprintf (f, "little_endian = %u\n", little_endian);
	fprintf (f, "num_sections = %u\n", num_sections);
	fprintf (f, "contents_section = %u @ offset 0x%x\n", cont_sec_idx, cont_sec_off);
	fprintf (f, "contents_classname_section = %u @ offset 0x%x\n", cont_cls_idx, cont_cls_off);

	if (little_endian)
	{
		fprintf (f, "# little-endian packfiles are not supported by this decoder\n");
		return ERR_OK;
	}

	if (num_sections > HKX_MAX_SECTIONS)
	{
		fprintf (f, "# num_sections implausible, not walking section table\n");
		return ERR_OK;
	}

	hkx_section_t sec[HKX_MAX_SECTIONS];
	memset (sec, 0, sizeof sec);
	u32 nsec = 0;

	fprintf (f, "\n# Section table\n");
	size_t pos = HKX_HDR_SIZE;
	for (u32 i = 0; i < num_sections; i++)
	{
		if (pos + HKX_SEC_SIZE > size)
		{
			fprintf (f, "# section table truncated at index %u\n", i);
			break;
		}
		const u8 *s = data + pos;
		char name[21];
		memcpy (name, s, 20);
		name[20] = 0;
		for (int c = 0; c < 20; c++)
			if (!name[c])
				break;
			else if (!isprint ((unsigned char) name[c]))
			{
				name[c] = 0;
				break;
			}

		const u32 data_start = be32 (s + 20);
		const u32 end_off    = be32 (s + 44);

		fprintf (f, "  [%u] %-16s data_start=0x%06x size=0x%06x (ends 0x%06x)\n",
			i, name, data_start, end_off, data_start + end_off);

		if (nsec < HKX_MAX_SECTIONS)
		{
			memcpy (sec[nsec].name, name, sizeof name);
			sec[nsec].data_start = data_start;
			sec[nsec].end_offset = end_off;
			nsec++;
		}
		pos += HKX_SEC_SIZE;
	}

	// Heuristic class-name extraction from "__classnames__": the section is
	// self-describing reflection data (see lib-hkx.h) but the exact
	// per-entry record layout wasn't pinned down, so this scans for
	// NUL-terminated printable-ASCII runs starting with "hk" or "hcl"
	// instead of trusting a guessed record stride.
	for (u32 i = 0; i < nsec; i++)
	{
		if (strcmp (sec[i].name, "__classnames__") != 0)
			continue;

		const size_t start = sec[i].data_start;
		const size_t end   = start + sec[i].end_offset;
		if (start >= size || end > size || end <= start)
			break;

		fprintf (f, "\n# Embedded Havok class names (from __classnames__)\n");
		int n_classes = 0;
		size_t p = start;
		while (p < end)
		{
			if ((data[p] == 'h' && p + 1 < end && (data[p + 1] == 'k' || data[p + 1] == 'c'))
				&& isupper ((unsigned char) data[p + 2]))
			{
				size_t q = p;
				while (q < end && data[q])
					q++;
				size_t len = q - p;
				if (len >= 3 && len < 128)
				{
					fprintf (f, "  %.*s\n", (int) len, data + p);
					n_classes++;
					p = q;
					continue;
				}
			}
			p++;
		}
		fprintf (f, "# %d class name(s) found\n", n_classes);
		break;
	}

	return ERR_OK;
}
