#include "lib-std.h"
#include "lib-ncer.h"
#include <string.h>
#include <errno.h>

enumError ScanNCER (nintendo_ncer_t *ncer, const u8 *data, uint size)
{
	if (!ncer || !data || size < 0x30 || memcmp (data, "RECN", 4)
		|| memcmp (data + 0x10, "KBEC", 4))
		return EINVAL;
	const u8 *kbec = data + 0x10;
	const uint chunk_size = rd_le32 (kbec + 4);
	const uint n_cells = rd_le16 (kbec + 8);
	const uint entry_kind = rd_le16 (kbec + 10);
	const uint cell_size = entry_kind == 0 ? 8 : entry_kind == 1 ? 16 : 0;
	const uint cell_off = 8 + rd_le32 (kbec + 12);
	if (!chunk_size || chunk_size > size - 0x10 || !n_cells || !cell_size || cell_off > chunk_size
		|| n_cells > (chunk_size - cell_off) / cell_size)
		return EINVAL;
	const uint objects_off = cell_off + n_cells * cell_size;
	if (objects_off > chunk_size)
		return EINVAL;
	memset (ncer, 0, sizeof (*ncer));
	ncer->data = data;
	ncer->size = size;
	ncer->n_cells = n_cells;
	ncer->cell_size = cell_size;
	ncer->cells = kbec + cell_off;
	ncer->objects = kbec + objects_off;
	ncer->objects_size = chunk_size - objects_off;
	ncer->mapping_mode = chunk_size >= 20 ? rd_le32 (kbec + 16) : 0;
	for (uint i = 0; i < n_cells; i++)
	{
		const u8 *cell = ncer->cells + i * cell_size;
		const uint n_obj = rd_le16 (cell);
		const uint obj_off = rd_le32 (cell + 4);
		if (obj_off > ncer->objects_size || n_obj > (ncer->objects_size - obj_off) / 6)
			return EINVAL;
	}
	return ERR_OK;
}

enumError GetNCERCell (
	const nintendo_ncer_t *ncer, uint index, uint *n_objects, const u8 **oam_records)
{
	if (!ncer || !n_objects || !oam_records || index >= ncer->n_cells)
		return EINVAL;
	const u8 *cell = ncer->cells + index * ncer->cell_size;
	const uint count = rd_le16 (cell);
	const uint off = rd_le32 (cell + 4);
	if (off > ncer->objects_size || count > (ncer->objects_size - off) / 6)
		return EINVAL;
	*n_objects = count;
	*oam_records = ncer->objects + off;
	return ERR_OK;
}

enumError ScanNANR (nintendo_nanr_t *nanr, const u8 *data, uint size)
{
	if (!nanr || !data || size < 0x38 || memcmp (data, "RNAN", 4)
		|| memcmp (data + 0x10, "KNBA", 4))
		return EINVAL;
	const u8 *knba = data + 0x10;
	const uint chunk_size = rd_le32 (knba + 4);
	const uint n_anims = rd_le16 (knba + 8), n_frames = rd_le16 (knba + 10);
	const uint anim_off = 8 + rd_le32 (knba + 12);
	const uint frame_off = 8 + rd_le32 (knba + 16);
	const uint data_off = 8 + rd_le32 (knba + 20);
	if (!chunk_size || chunk_size > size - 0x10 || !n_anims || !n_frames || anim_off > chunk_size
		|| n_anims > (chunk_size - anim_off) / 16 || frame_off > chunk_size
		|| n_frames > (chunk_size - frame_off) / 8 || data_off > chunk_size)
		return EINVAL;
	memset (nanr, 0, sizeof (*nanr));
	nanr->data = data;
	nanr->size = size;
	nanr->n_animations = n_anims;
	nanr->n_frames = n_frames;
	nanr->animations = knba + anim_off;
	nanr->frames = knba + frame_off;
	nanr->frames_size = n_frames * 8;
	nanr->frame_data = knba + data_off;
	nanr->frame_data_size = chunk_size - data_off;
	for (uint i = 0; i < n_anims; i++)
	{
		const u8 *anim = nanr->animations + 16 * i;
		const uint count = rd_le32 (anim);
		const uint off = rd_le32 (anim + 12);
		if (!count || off > nanr->frames_size || count > (nanr->frames_size - off) / 8)
			return EINVAL;
	}
	if (nanr->frame_data_size < 2)
		return EINVAL;
	for (uint i = 0; i < n_frames; i++)
	{
		const u8 *frame = nanr->frames + 8 * i;
		if (rd_le32 (frame) > nanr->frame_data_size - 2)
			return EINVAL;
	}
	return ERR_OK;
}

enumError GetNANRAnimation (
	const nintendo_nanr_t *nanr, uint index, uint *n_frames, const u8 **frame_records)
{
	if (!nanr || !n_frames || !frame_records || index >= nanr->n_animations)
		return EINVAL;
	const u8 *anim = nanr->animations + 16 * index;
	const uint count = rd_le32 (anim), off = rd_le32 (anim + 12);
	if (!count || off > nanr->frames_size || count > (nanr->frames_size - off) / 8)
		return EINVAL;
	*n_frames = count;
	*frame_records = nanr->frames + off;
	return ERR_OK;
}

