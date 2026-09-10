// Factor 5 VID1 DivX movie container -- split out for GameCube .vid support.
//
// Layout (big-endian chunk sizes; no retail sample on hand, structure per
// the MultimediaWiki "Factor 5 VID1" page and the field offsets reverse-
// engineered in slfx77/neversoft-multitool's Vid1VideoFile.cs, which itself
// was ported from Factor 5's GameCube M4Decoder):
//
//   chunk  = 4-byte ASCII tag + BE u32 size (including the 8-byte header)
//   root   = "VID1" chunk at offset 0
//   "HEAD" chunk immediately after the root; its children start at
//            HEAD+0x0C, one of them a "VIDH" chunk (body >= 0x20):
//            base=VIDH+8: u16BE width @+0x04, u16BE height @+0x06,
//            u32BE frame count @+0x08, u32BE fps numerator @+0x10,
//            u16BE fps denominator @+0x14
//   "FRAM" chunks from the end of HEAD to EOF (zero padding tolerated);
//            each FRAM's children start at FRAM+0x20 and are "VIDD"
//            (video: cut-down MPEG-4 Part 2 bitstream, DivX 5.02 based)
//            and "AUDD" (audio: custom Vorbis packets) payloads.
//
// Only the container is demuxed here: VIDD payloads extract as raw
// frame_*.vidd bitstreams (no MPEG-4 decoder in this tree) and AUDD
// payloads as audio_*.audd packet streams, plus an info.txt summary.
// This mirrors the ExtractTHP() shape (lib-thp.c).

#include "lib-std.h"
#include "lib-nintendo.h"

#define VID1_MAX_DIMS 4096

typedef struct vid1_chunk_t
{
	uint off;
	uint size;
	uint end;
} vid1_chunk_t;

// Read one [tag(4)+BE32 size] chunk header at OFF bounded by LIMIT.
// Returns false on truncation or an impossible size.
static bool vid1_read_chunk (
	const u8 *d, uint off, uint limit, vid1_chunk_t *ch)
{
	if (!d || !ch || off > limit || (uint64_t)off + 8 > (uint64_t)limit)
		return false;
	const uint size = rd_be32 (d + off + 4);
	if (size < 8 || (uint64_t)off + size > (uint64_t)limit)
		return false;
	ch->off = off;
	ch->size = size;
	ch->end = off + size;
	return true;
}

static bool vid1_tag_is (const u8 *d, uint off, ccp tag)
{
	return !memcmp (d + off, tag, 4);
}

// True when [OFF,LIMIT) is all zero bytes (inter-chunk padding).
static bool vid1_is_padding (const u8 *d, uint off, uint limit)
{
	for (uint i = off; i < limit; i++)
		if (d[i])
			return false;
	return true;
}

bool IsVID1 (const u8 *data, uint size)
{
	if (!data || size < 8 || memcmp (data, "VID1", 4))
		return false;
	vid1_chunk_t root;
	return vid1_read_chunk (data, 0, size, &root);
}

enumError ScanVID1 (vid1_info_t *info, const u8 *d, uint size)
{
	if (!info || !d || size < 8 || memcmp (d, "VID1", 4))
		return EINVAL;
	memset (info, 0, sizeof (*info));

	vid1_chunk_t root;
	if (!vid1_read_chunk (d, 0, size, &root))
		return EINVAL;

	// HEAD follows the root chunk.
	vid1_chunk_t head;
	if (!vid1_read_chunk (d, root.end, size, &head)
		|| !vid1_tag_is (d, head.off, "HEAD"))
		return EINVAL;

	// HEAD children start at HEAD+0x0C; find VIDH.
	uint coff = head.off + 0x0c;
	bool found_vidh = false;
	while (coff + 8 <= head.end)
	{
		if (vid1_is_padding (d, coff, head.end))
			break;
		vid1_chunk_t ch;
		if (!vid1_read_chunk (d, coff, head.end, &ch))
			return EINVAL;
		if (vid1_tag_is (d, ch.off, "VIDH"))
		{
			if (ch.size < 0x20)
				return EINVAL;
			const u8 *b = d + ch.off + 8;
			const uint w = (uint)rd_be16 (b + 0x04);
			const uint h = (uint)rd_be16 (b + 0x06);
			const uint n = rd_be32 (b + 0x08);
			const uint num = rd_be32 (b + 0x10);
			const uint den = (uint)rd_be16 (b + 0x14);
			if (!w || !h || w > VID1_MAX_DIMS || h > VID1_MAX_DIMS
				|| !n || n > VID1_MAX_FRAMES || !num || !den)
				return EINVAL;
			info->width = w;
			info->height = h;
			info->frame_count = n;
			info->fps_num = num;
			info->fps_den = den;
			found_vidh = true;
			break;
		}
		coff = ch.end;
	}
	if (!found_vidh)
		return EINVAL;

	// FRAM chunks from the end of HEAD to EOF.
	uint off = head.end;
	uint n_vidd = 0, n_audd = 0;
	while (off + 8 <= size)
	{
		if (vid1_is_padding (d, off, size))
			break;
		vid1_chunk_t fram;
		if (!vid1_read_chunk (d, off, size, &fram))
			return EINVAL;
		if (!vid1_tag_is (d, fram.off, "FRAM"))
			break;
		if (fram.end < fram.off + 0x20)
			return EINVAL;
		uint child = fram.off + 0x20;
		while (child + 8 <= fram.end)
		{
			vid1_chunk_t sub;
			if (!vid1_read_chunk (d, child, fram.end, &sub))
				return EINVAL;
			if (vid1_tag_is (d, sub.off, "VIDD"))
			{
				if (n_vidd < VID1_MAX_FRAMES)
				{
					info->vidd_off[n_vidd] = sub.off + 8;
					info->vidd_size[n_vidd] = sub.end - (sub.off + 8);
					n_vidd++;
				}
			}
			else if (vid1_tag_is (d, sub.off, "AUDD"))
			{
				if (n_audd < VID1_MAX_FRAMES)
				{
					info->audd_off[n_audd] = sub.off + 8;
					info->audd_size[n_audd] = sub.end - (sub.off + 8);
					n_audd++;
				}
			}
			child = sub.end;
		}
		if (child < fram.end && !vid1_is_padding (d, child, fram.end))
			return EINVAL;
		off = fram.end;
	}

	if (!n_vidd)
		return EINVAL;
	info->n_vidd = n_vidd;
	info->n_audd = n_audd;
	return ERR_OK;
}

enumError ExtractVID1 (
	nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *vid_data, uint vid_size)
{
	if (!out_entries || !out_n_entries || !vid_data)
		return EINVAL;

	// ScanVID1() records offsets into fixed-size tables; allocate the
	// info on the heap so a hostile-but-well-formed header cannot blow
	// past the stack (tables dominate sizeof(vid1_info_t)).
	vid1_info_t *vi = CALLOC (1, sizeof (*vi));
	if (!vi)
		return ERR_CANT_CREATE;
	enumError serr = ScanVID1 (vi, vid_data, vid_size);
	if (serr)
	{
		FREE (vi);
		return serr;
	}

	const uint n_total = vi->n_vidd + vi->n_audd + 1; // + info.txt
	nintendo_sarc_entry_t *entries = CALLOC (n_total, sizeof (*entries));
	if (!entries)
	{
		FREE (vi);
		return ERR_CANT_CREATE;
	}

	uint count = 0;
	for (uint i = 0; i < vi->n_vidd; i++)
	{
		const uint off = vi->vidd_off[i], sz = vi->vidd_size[i];
		if (!sz || (uint64_t)off + sz > (uint64_t)vid_size)
			continue;
		char name[64];
		snprintf (name, sizeof (name), "frame_%05u.vidd", i);
		entries[count].name = STRDUP (name);
		entries[count].size = sz;
		entries[count].data = MALLOC (sz);
		if (!entries[count].name || !entries[count].data)
			break;
		memcpy ((void *)entries[count].data, vid_data + off, sz);
		count++;
	}
	for (uint i = 0; i < vi->n_audd; i++)
	{
		const uint off = vi->audd_off[i], sz = vi->audd_size[i];
		if (!sz || (uint64_t)off + sz > (uint64_t)vid_size)
			continue;
		char name[64];
		snprintf (name, sizeof (name), "audio_%05u.audd", i);
		entries[count].name = STRDUP (name);
		entries[count].size = sz;
		entries[count].data = MALLOC (sz);
		if (!entries[count].name || !entries[count].data)
			break;
		memcpy ((void *)entries[count].data, vid_data + off, sz);
		count++;
	}

	char info_txt[256];
	const int info_len = snprintf (info_txt, sizeof (info_txt),
		"VID1 Factor 5 DivX movie (GameCube)\n"
		"dimensions: %ux%u\nframes (header): %u\nvideo payloads: %u\naudio payloads: %u\n"
		"frame rate: %u/%u fps\n",
		vi->width, vi->height, vi->frame_count, vi->n_vidd, vi->n_audd,
		vi->fps_num, vi->fps_den);
	FREE (vi);
	if (info_len > 0 && count < n_total)
	{
		entries[count].name = STRDUP ("info.txt");
		entries[count].size = (uint)info_len;
		entries[count].data = MALLOC ((uint)info_len);
		if (entries[count].name && entries[count].data)
		{
			memcpy ((void *)entries[count].data, info_txt, (uint)info_len);
			count++;
		}
	}

	if (!count)
	{
		for (uint i = 0; i < n_total; i++)
		{
			FREE ((void *)entries[i].name);
			FREE (entries[i].data);
		}
		FREE (entries);
		return EINVAL;
	}

	*out_entries = entries;
	*out_n_entries = count;
	return ERR_OK;
}
