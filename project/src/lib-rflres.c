#include "lib-std.h"
#include "lib-rflres.h"
#include <string.h>
#include <errno.h>

//-----------------------------------------------------------------------------
///////////////	  RFL_Res.dat (Revolution Face Library)		///////////////
//-----------------------------------------------------------------------------

static const char * const rfl_arc_names[18] = {
	"beard",
	"eye",
	"eyebrow",
	"faceline",
	"face_tex",
	"fore_head",
	"glass",
	"glass_tex",
	"hair",
	"mask",
	"mole",
	"mouth",
	"mustache",
	"nose",
	"nline",
	"nline_tex",
	"cap",
	"cap_tex"
};

enumError ScanRFLRes (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 8)
		return EINVAL;

	*entries = 0;
	*n_entries = 0;

	const u16 total_arc = rd_be16 (data);
	if (total_arc == 0 || total_arc > 64)
		return EINVAL;

	if (4 + total_arc * 4 > size)
		return EINVAL;

	for (uint i = 0; i < total_arc; i++)
	{
		const u32 off = rd_be32 (data + 4 + i * 4);
		if (off + 4 > size)
			return EINVAL;
	}

	uint total_files = 0;
	for (uint i = 0; i < total_arc; i++)
	{
		const u32 arc_off = rd_be32 (data + 4 + i * 4);
		const u16 num = rd_be16 (data + arc_off);
		if (arc_off + 4 + (num + 1) * 4 > size)
			return EINVAL;
		total_files += num;
	}

	if (total_files == 0 || total_files > 0x10000)
		return EINVAL;

	nintendo_sarc_entry_t *res = CALLOC (total_files, sizeof (*res));
	if (!res)
		return ERR_CANT_CREATE;

	uint out_idx = 0;
	for (uint i = 0; i < total_arc; i++)
	{
		const u32 arc_off = rd_be32 (data + 4 + i * 4);
		const u16 num = rd_be16 (data + arc_off);
		const u32 data_base = arc_off + 4 + (num + 1) * 4;

		ccp cat_name = i < 18 ? rfl_arc_names[i] : "subarc";
		char cat_buf[32];
		if (i >= 18)
		{
			snprintf (cat_buf, sizeof (cat_buf), "arc_%02u", i);
			cat_name = cat_buf;
		}

		for (uint j = 0; j < num; j++)
		{
			const u32 f_off = rd_be32 (data + arc_off + 4 + j * 4);
			const u32 next_off = rd_be32 (data + arc_off + 4 + (j + 1) * 4);
			if (next_off < f_off || data_base + next_off > size)
			{
				ResetOwnedEntries (res, out_idx);
				return EINVAL;
			}

			const uint file_sz = next_off - f_off;
			u8 *file_buf = MALLOC (file_sz ? file_sz : 1);
			if (!file_buf)
			{
				ResetOwnedEntries (res, out_idx);
				return ERR_CANT_CREATE;
			}
			if (file_sz)
				memcpy (file_buf, data + data_base + f_off, file_sz);

			char name_buf[128];
			snprintf (name_buf, sizeof (name_buf), "%s/%03u.bin", cat_name, j);

			res[out_idx].name = STRDUP (name_buf);
			res[out_idx].data = file_buf;
			res[out_idx].size = file_sz;
			out_idx++;
		}
	}

	*entries = res;
	*n_entries = out_idx;
	return ERR_OK;
}

enumError CreateRFLRes (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return EINVAL;

	*dest = 0;
	*dest_size = 0;

	uint max_cat = 18;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp slash = entries[i].name ? strchr (entries[i].name, '/') : 0;
		if (slash)
		{
			char dir[64];
			const size_t dlen = (size_t)(slash - entries[i].name);
			if (dlen < sizeof (dir))
			{
				memcpy (dir, entries[i].name, dlen);
				dir[dlen] = 0;
				if (!strncmp (dir, "arc_", 4))
				{
					uint idx = (uint)atoi (dir + 4);
					if (idx + 1 > max_cat && idx < 64)
						max_cat = idx + 1;
				}
			}
		}
	}

	typedef struct {
		uint count;
		uint alloc;
		const nintendo_sarc_entry_t **files;
	} rfl_cat_t;

	rfl_cat_t *cats = CALLOC (max_cat, sizeof (*cats));
	if (!cats)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp slash = entries[i].name ? strchr (entries[i].name, '/') : 0;
		int cat_idx = -1;
		if (slash)
		{
			char dir[64];
			const size_t dlen = (size_t)(slash - entries[i].name);
			if (dlen < sizeof (dir))
			{
				memcpy (dir, entries[i].name, dlen);
				dir[dlen] = 0;
				for (int c = 0; c < 18; c++)
				{
					if (!strcmp (dir, rfl_arc_names[c]))
					{
						cat_idx = c;
						break;
					}
				}
				if (cat_idx < 0 && !strncmp (dir, "arc_", 4))
					cat_idx = atoi (dir + 4);
			}
		}
		if (cat_idx < 0 || (uint)cat_idx >= max_cat)
			cat_idx = 0;

		rfl_cat_t *c = cats + cat_idx;
		if (c->count >= c->alloc)
		{
			c->alloc = c->alloc ? c->alloc * 2 : 16;
			c->files = REALLOC (c->files, c->alloc * sizeof (*c->files));
		}
		c->files[c->count++] = entries + i;
	}

	const uint hdr_size = 4 + max_cat * 4;
	u32 *arc_offsets = CALLOC (max_cat, sizeof (u32));

	uint cur_offset = hdr_size;
	for (uint i = 0; i < max_cat; i++)
	{
		arc_offsets[i] = cur_offset;
		const uint num = cats[i].count;
		cur_offset += 4 + (num + 1) * 4;
		for (uint j = 0; j < num; j++)
			cur_offset += cats[i].files[j]->size;
		cur_offset = (cur_offset + 3) & ~3u;
	}

	u8 *buf = CALLOC (1, cur_offset);
	if (!buf)
	{
		for (uint i = 0; i < max_cat; i++)
			FREE (cats[i].files);
		FREE (cats);
		FREE (arc_offsets);
		return ERR_CANT_CREATE;
	}

	wr_be16 (buf, (u16)max_cat);
	wr_be16 (buf + 2, 0x0001);
	for (uint i = 0; i < max_cat; i++)
		wr_be32 (buf + 4 + i * 4, arc_offsets[i]);

	for (uint i = 0; i < max_cat; i++)
	{
		const u32 a_off = arc_offsets[i];
		const uint num = cats[i].count;
		uint max_sz = 0;
		for (uint j = 0; j < num; j++)
		{
			if (cats[i].files[j]->size > max_sz)
				max_sz = cats[i].files[j]->size;
		}

		wr_be16 (buf + a_off, (u16)num);
		wr_be16 (buf + a_off + 2, (u16)max_sz);

		u32 rel_off = 0;
		const u32 data_start = a_off + 4 + (num + 1) * 4;
		for (uint j = 0; j < num; j++)
		{
			wr_be32 (buf + a_off + 4 + j * 4, rel_off);
			if (cats[i].files[j]->size)
				memcpy (buf + data_start + rel_off, cats[i].files[j]->data, cats[i].files[j]->size);
			rel_off += cats[i].files[j]->size;
		}
		wr_be32 (buf + a_off + 4 + num * 4, rel_off);
	}

	for (uint i = 0; i < max_cat; i++)
		FREE (cats[i].files);
	FREE (cats);
	FREE (arc_offsets);

	*dest = buf;
	*dest_size = cur_offset;
	return ERR_OK;
}

