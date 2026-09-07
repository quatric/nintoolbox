// Nintendo THP movie format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
// THP Video File ("THP\0") Frame & Audio Extraction
// Used across GameCube and Wii games (e.g. Super Smash Bros. Brawl, Mario Kart Wii, etc.)
//-----------------------------------------------------------------------------

enumError ExtractTHP (
	nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *thp_data, uint thp_size)
{
	if (!out_entries || !out_n_entries || !thp_data || thp_size < 0x30)
		return EINVAL;

	if (memcmp (thp_data, "THP\0", 4))
		return EINVAL;

	u32 frame_count = rd_be32 (thp_data + 0x14);
	u32 comp_data_off = rd_be32 (thp_data + 0x20);
	u32 movie_data_off = rd_be32 (thp_data + 0x28);

	if (!frame_count || frame_count > 500000 || movie_data_off >= thp_size)
		return EINVAL;

	// Component table
	u32 num_comps = 0;
	if (comp_data_off + 4 <= thp_size)
		num_comps = rd_be32 (thp_data + comp_data_off);
	if (!num_comps || num_comps > 16)
		num_comps = 1;

	// Allocate entries (up to frame_count * num_comps)
	nintendo_sarc_entry_t *entries
		= CALLOC (frame_count * num_comps, sizeof (nintendo_sarc_entry_t));
	uint count = 0;

	u32 cur_off = movie_data_off;
	for (u32 f = 0; f < frame_count && cur_off + 8 + num_comps * 4 <= thp_size; f++)
	{
		u32 next_frame_size = rd_be32 (thp_data + cur_off);
		u32 prev_frame_size = rd_be32 (thp_data + cur_off + 4);
		(void)prev_frame_size;

		u32 comp_sizes[16] = { 0 };
		for (u32 c = 0; c < num_comps; c++)
			comp_sizes[c] = rd_be32 (thp_data + cur_off + 8 + c * 4);

		u32 comp_payload_off = cur_off + 8 + num_comps * 4;
		for (u32 c = 0; c < num_comps; c++)
		{
			u32 csz = comp_sizes[c];
			// Perform the bounds check in 64-bit: a corrupt/truncated THP can
			// carry a component size near 0xFFFFFFFF, and the 32-bit sum
			// comp_payload_off + csz would wrap around and pass the check,
			// making MALLOC/memcpy read far past thp_size (crash).
			if (csz > 0 && (uint64_t)comp_payload_off + csz <= (uint64_t)thp_size)
			{
				char name[64];
				if (c == 0)
					snprintf (name, sizeof (name), "frame_%05u.jpg", f);
				else
					snprintf (name, sizeof (name), "audio_%05u_%u.bin", f, c);

				entries[count].name = STRDUP (name);
				entries[count].size = csz;
				entries[count].data = MALLOC (csz);
				memcpy ((void *)entries[count].data, thp_data + comp_payload_off, csz);
				count++;
				comp_payload_off += csz;
			}
		}

		if (next_frame_size == 0)
			break;
		cur_off += next_frame_size;
	}

	*out_entries = entries;
	*out_n_entries = count;
	return ERR_OK;
}
