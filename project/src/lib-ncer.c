#include "lib-std.h"
#include "lib-ncer.h"
#include <mxml.h>
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


typedef struct ncer_xml_cell_t
{
	uint count, object_off;
} ncer_xml_cell_t;

// The XML reader intentionally accepts the narrow, stable manifest emitted by
// extract_nitro_sprite_manifest().  It is not a general XML parser: rejecting
// extensions outside that vocabulary prevents a malformed project from being
// converted into an unsafe binary layout.
enumError create_ncer_xml ( ccp source, ccp dest )
{
    u8 *xml = 0;
    size_t xml_size = 0;
    enumError err = LoadFileAlloc(source,0,0,&xml,&xml_size,16<<20,0,0,false);
    if (err) return err;

    mxml_node_t *tree = mxmlLoadString(NULL, (ccp)xml, MXML_OPAQUE_CALLBACK);
    if (!tree) { FREE(xml); return ERR_INVALID_DATA; }

    mxml_node_t *ncer_node = mxmlFindElement(tree, tree, "ncer", NULL, NULL, MXML_DESCEND);
    if (!ncer_node) { mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA; }

    const char *cells_attr = mxmlElementGetAttr(ncer_node, "cells");
    if (!cells_attr) { mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA; }

    uint n_cells = strtoul(cells_attr, NULL, 10);
    if (!n_cells || n_cells > 65535) { mxmlDelete(tree); FREE(xml); return ERR_INVALID_DATA; }

    ncer_xml_cell_t *cells = CALLOC(n_cells,sizeof(*cells));
    u16 *objects = 0;
    uint n_objects = 0, obj_alloc = 0;

    mxml_node_t *cell_node = mxmlFindElement(ncer_node, ncer_node, "cell", NULL, NULL, MXML_DESCEND_FIRST);
    for (uint i = 0; i < n_cells; i++)
    {
        if (!cell_node) { err = ERR_INVALID_DATA; break; }
        const char *idx_attr = mxmlElementGetAttr(cell_node, "index");
        const char *cnt_attr = mxmlElementGetAttr(cell_node, "objects");
        if (!idx_attr || !cnt_attr) { err = ERR_INVALID_DATA; break; }

        uint index = strtoul(idx_attr, NULL, 10);
        uint count = strtoul(cnt_attr, NULL, 10);
        if (index != i || count > 65535-n_objects) { err = ERR_INVALID_DATA; break; }

        cells[i].count = count;
        cells[i].object_off = n_objects;

        mxml_node_t *obj_node = mxmlFindElement(cell_node, cell_node, "obj", NULL, NULL, MXML_DESCEND_FIRST);
        for (uint j = 0; j < count; j++)
        {
            if (!obj_node) { err = ERR_INVALID_DATA; break; }
            const char *a0_attr = mxmlElementGetAttr(obj_node, "attr0");
            const char *a1_attr = mxmlElementGetAttr(obj_node, "attr1");
            const char *a2_attr = mxmlElementGetAttr(obj_node, "attr2");
            if (!a0_attr || !a1_attr || !a2_attr) { err = ERR_INVALID_DATA; break; }

            uint a0 = strtoul(a0_attr, NULL, 16);
            uint a1 = strtoul(a1_attr, NULL, 16);
            uint a2 = strtoul(a2_attr, NULL, 16);
            if (a0 > 0xffff || a1 > 0xffff || a2 > 0xffff) { err = ERR_INVALID_DATA; break; }

            if (n_objects == obj_alloc)
            {
                const uint new_alloc = obj_alloc ? obj_alloc*2 : 64;
                u16 *new_objects = REALLOC(objects,3*new_alloc*sizeof(*objects));
                if (!new_objects) { err = ERR_CANT_CREATE; break; }
                objects = new_objects; obj_alloc = new_alloc;
            }
            objects[3*n_objects] = a0; objects[3*n_objects+1] = a1;
            objects[3*n_objects+2] = a2; n_objects++;
            obj_node = mxmlFindElement(obj_node, cell_node, "obj", NULL, NULL, MXML_NO_DESCEND);
        }
        if (err) break;
        cell_node = mxmlFindElement(cell_node, ncer_node, "cell", NULL, NULL, MXML_NO_DESCEND);
    }
    mxmlDelete(tree);
    const u64 chunk64 = ( 0x20ull + 8ull*n_cells + 6ull*n_objects + 3 ) & ~3ull;
    if (!err && (chunk64 > UINT_MAX || chunk64+0x10 > UINT_MAX)) err = ERR_FILE_TOO_BIG;
    u8 *out = !err ? CALLOC(1,0x10+(uint)chunk64) : 0;
    if (!err && !out) err = ERR_CANT_CREATE;
    if (!err)
    {
        memcpy(out,"RECN",4); write_le16(out+4,0xfeff); write_le16(out+6,0x100);
        write_le32(out+8,0x10+(uint)chunk64); write_le16(out+12,0x10); write_le16(out+14,1);
        u8 *kbec = out+0x10;
        memcpy(kbec,"KBEC",4); write_le32(kbec+4,chunk64); write_le16(kbec+8,n_cells);
        write_le32(kbec+12,0x18); // cell table is at KBEC+0x20
        u8 *obj = kbec+0x20+8*n_cells;
        for (uint i = 0; i < n_cells; i++)
        {
            u8 *cell = kbec+0x20+8*i;
            write_le16(cell,cells[i].count); write_le32(cell+4,6*cells[i].object_off);
        }
        for (uint i = 0; i < n_objects; i++)
        {
            write_le16(obj+6*i,objects[3*i]); write_le16(obj+6*i+2,objects[3*i+1]);
            write_le16(obj+6*i+4,objects[3*i+2]);
        }
        if (verbose >= 0 || testmode)
            fprintf(stdlog,"%sCREATE NCER XML:%s -> %s\n",testmode ? "WOULD " : "",source,dest);
        if (!testmode)
        {
            File_t F;
            CreateFILE(&F,true,dest,testmode,false,true,false,false);
            if (F.f && fwrite(out,1,0x10+(uint)chunk64,F.f) != 0x10+(uint)chunk64)
                err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing NCER failed: %s\n",dest);
            ResetFile(&F,opt_preserve);
        }
    }
    FREE(out); FREE(objects); FREE(cells); FREE(xml);
    return err;
}

