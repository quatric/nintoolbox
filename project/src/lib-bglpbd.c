// SPDX-License-Identifier: GPL-2.0+
#include "lib-bglpbd.h"
#include "lib-image.h"
#include "lib-std.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

void InitializeBGLPBD (bglpbd_file_t *file)
{
	if (!file)
		return;
	memset (file, 0, sizeof (*file));
	file->version = 1;
	file->is_switch = true;
	file->settings.color[0] = 1.0f;
	file->settings.color[1] = 1.0f;
	file->settings.color[2] = 1.0f;
	file->settings.dir_light_indirect = 0.525f;
	file->settings.point_light_indirect = 1.0f;
	file->settings.spot_light_indirect = 1.0f;
	file->settings.emission_scale = 8.0f;
}

void ResetBGLPBD (bglpbd_file_t *file)
{
	if (!file)
		return;
	if (file->boxes)
	{
		for (size_t i = 0; i < file->num_boxes; i++)
		{
			FREE (file->boxes[i].probe_indices);
			FREE (file->boxes[i].sh_buffer);
		}
		FREE (file->boxes);
		file->boxes = NULL;
	}
	file->num_boxes = 0;
}

///////////////////////////////////////////////////////////////////////////////
// Spherical Harmonics Utilities
///////////////////////////////////////////////////////////////////////////////

void SH_SetConstantColor (float *sh_data_27, const float color[3])
{
	if (!sh_data_27)
		return;
	memset (sh_data_27, 0, 27 * sizeof (float));
	sh_data_27[0] = color[0];  // Red L00
	sh_data_27[9] = color[1];  // Green L00
	sh_data_27[18] = color[2]; // Blue L00
}

static void pack_coeff_to_buffer (const float coeff[9][3], float *sh_data_27)
{
	// 9 coefficients for red, then 9 for green, then 9 for blue
	for (int ch = 0; ch < 3; ch++)
	{
		for (int i = 0; i < 9; i++)
			sh_data_27[ch * 9 + i] = coeff[i][ch];
	}
}

void SH_UpdateCoeff (float *sh_data_27, const float color[3], const float dir[3])
{
	if (!sh_data_27)
		return;

	float coeff[9][3];
	memset (coeff, 0, sizeof (coeff));

	for (int c = 0; c < 3; c++)
	{
		coeff[0][c] += color[c];
		coeff[1][c] += color[c] * dir[1];
		coeff[2][c] += color[c] * dir[2];
		coeff[3][c] += color[c] * dir[0];

		coeff[4][c] += color[c] * (dir[0] * dir[1]);
		coeff[5][c] += color[c] * (dir[1] * dir[2]);
		coeff[6][c] += color[c] * (dir[2] * dir[0]);

		coeff[7][c] += color[c] * (dir[2] * dir[2]);
		coeff[6][c] += color[c] * (dir[0] * dir[0] - dir[1] * dir[1]);
	}

	pack_coeff_to_buffer (coeff, sh_data_27);
}

void SH_UpdateCoeffBias (float *sh_data_27, const float color[3], const float dir[3])
{
	if (!sh_data_27)
		return;

	float coeff[9][3];
	memset (coeff, 0, sizeof (coeff));

	for (int c = 0; c < 3; c++)
	{
		// Band 0
		coeff[0][c] += 0.282095f * color[c];

		// Band 1
		coeff[1][c] += color[c] * (0.488603f * dir[1]);
		coeff[2][c] += color[c] * (0.488603f * dir[2]);
		coeff[3][c] += color[c] * (0.488603f * dir[0]);

		// Band 2
		coeff[4][c] += color[c] * (1.092548f * dir[0] * dir[1]);
		coeff[5][c] += color[c] * (-1.092548f * dir[1] * dir[2]);
		coeff[6][c] += color[c] * (0.315392f * (3.0f * dir[2] * dir[2] - 1.0f));
		coeff[7][c] += color[c] * (-1.092548f * dir[0] * dir[2]);
		coeff[8][c] += color[c] * (0.546274f * (dir[0] * dir[0] - dir[1] * dir[1]));
	}

	pack_coeff_to_buffer (coeff, sh_data_27);
}

static void convert_channel (float weight, const float data[9], float out_vec[2][4])
{
	const float const_1 = 0.3253434f;
	const float const_2 = 0.2817569f;
	const float const_3 = 0.07875311f;
	const float const_4 = 0.2728088f;
	const float const_5 = 0.2362593f;

	float v1 = weight * data[3] * const_1;
	float v2 = weight * data[1] * const_1;
	float v3 = weight * data[2] * const_1;
	float v4 = weight * data[0] * const_2 - data[6] * const_3;

	float v21 = weight * data[4] * const_4;
	float v22 = weight * data[5] * const_4;
	float v23 = weight * data[6] * const_5;
	float v24 = weight * data[7] * const_4;

	out_vec[0][0] = v1;
	out_vec[0][1] = v2;
	out_vec[0][2] = v3;
	out_vec[0][3] = v4;

	out_vec[1][0] = v21;
	out_vec[1][1] = v22;
	out_vec[1][2] = v23;
	out_vec[1][3] = v24;
}

void SH_ConvertSH2RGB (const float sh_data_27[27], float rgb_out[7][4])
{
	const float weights[3] = { 1.0f, 1.0f, 1.0f };
	int data_idx = 0;

	for (int ch = 0; ch < 3; ch++)
	{
		float ch_data[9];
		for (int i = 0; i < 9; i++)
			ch_data[i] = sh_data_27[data_idx++];

		float out_vec[2][4];
		convert_channel (weights[ch], ch_data, out_vec);

		for (int k = 0; k < 4; k++)
		{
			rgb_out[ch][k] = out_vec[0][k];
			rgb_out[3 + ch][k] = out_vec[1][k];
		}
	}

	const float const_5 = 0.1364044f;
	rgb_out[6][0] = weights[0] * sh_data_27[8] * const_5;
	rgb_out[6][1] = weights[1] * sh_data_27[17] * const_5; // shData[19] in C# bug: 9+8=17
	rgb_out[6][2] = weights[2] * sh_data_27[26] * const_5;
	rgb_out[6][3] = 1.0f;
}

static inline float dot4 (const float a[4], const float b[4])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

void SH_GetRGBColor (const float normal[3], const float sh_data_rgba[7][4], float rgb_out[3])
{
	const float normal4[4] = { normal[0], normal[1], normal[2], 1.0f };

	float x0[3] = {
		dot4 (sh_data_rgba[0], normal4),
		dot4 (sh_data_rgba[1], normal4),
		dot4 (sh_data_rgba[2], normal4)
	};

	float v_b[4] = {
		normal4[0] * normal4[1],
		normal4[1] * normal4[2],
		normal4[2] * normal4[0],
		normal4[2] * normal4[2]
	};

	float x1[3] = {
		dot4 (sh_data_rgba[3], v_b),
		dot4 (sh_data_rgba[4], v_b),
		dot4 (sh_data_rgba[5], v_b)
	};

	float v_c = normal4[0] * normal4[0] - normal4[1] * normal4[1];
	float x2[3] = {
		sh_data_rgba[6][0] * v_c,
		sh_data_rgba[6][1] * v_c,
		sh_data_rgba[6][2] * v_c
	};

	for (int c = 0; c < 3; c++)
	{
		float val = x0[c] + x1[c] + x2[c];
		rgb_out[c] = (val < 0.0f) ? 0.0f : val;
	}
}

///////////////////////////////////////////////////////////////////////////////
// Detection & Scanning
///////////////////////////////////////////////////////////////////////////////

bool IsBGLPBD (const u8 *data, size_t size)
{
	if (!IsAAMP (data, size))
		return false;

	// In AAMP v1 or v2, inspect PIO type
	u32 ver_le = rd_le32 (data + 4);
	bool le = (ver_le == 1 || ver_le == 2);
	u32 version = le ? rd_le32 (data + 4) : rd_be32 (data + 4);

	if (version == 1)
	{
		if (size < 24)
			return false;
		u32 name_len = le ? rd_le32 (data + 20) : rd_be32 (data + 20);
		if (name_len >= 5 && 24 + 5 <= size && !memcmp (data + 24, "glpbd", 5))
			return true;
	}
	else if (version == 2)
	{
		if (size < 0x35)
			return false;
		if (!memcmp (data + 0x30, "glpbd", 5))
			return true;
	}
	return false;
}

static const aamp_param_entry_t *find_entry (const aamp_param_object_t *obj, const char *name)
{
	u32 h = AAMP_NameToHash (name);
	for (u32 i = 0; i < obj->entry_count; i++)
	{
		if (obj->entries[i].hash == h)
			return &obj->entries[i];
	}
	return NULL;
}

static const aamp_param_object_t *find_object (const aamp_param_list_t *list, const char *name)
{
	u32 h = AAMP_NameToHash (name);
	for (u32 i = 0; i < list->object_count; i++)
	{
		if (list->objects[i].hash == h)
			return &list->objects[i];
	}
	return NULL;
}

static void calc_stride (const float min_pos[3], const float max_pos[3], const float step[3], u32 stride[3])
{
	for (int i = 0; i < 3; i++)
	{
		float sz = max_pos[i] - min_pos[i];
		float s = step[i] > 1e-4f ? step[i] : 1000.0f;
		stride[i] = (u32)ceilf (sz / s);
		if (stride[i] == 0)
			stride[i] = 1;
	}
}

enumError ScanBGLPBD (bglpbd_file_t *file, const u8 *data, size_t size)
{
	if (!file || !data)
		return ERR_INVALID_DATA;

	InitializeBGLPBD (file);

	aamp_file_t aamp;
	enumError err = ScanAAMP (&aamp, data, size);
	if (err)
		return err;

	file->version = aamp.pio_version;
	file->is_switch = aamp.is_le;

	// Check root_grid
	const aamp_param_object_t *root_grid = find_object (&aamp.root, "root_grid");
	if (root_grid)
	{
		const aamp_param_entry_t *e_min = find_entry (root_grid, "aabb_min_pos");
		const aamp_param_entry_t *e_max = find_entry (root_grid, "aabb_max_pos");
		const aamp_param_entry_t *e_step = find_entry (root_grid, "voxel_step_pos");
		if (e_min) memcpy (file->root_min, e_min->vec, 3 * sizeof (float));
		if (e_max) memcpy (file->root_max, e_max->vec, 3 * sizeof (float));
		if (e_step) memcpy (file->root_step, e_step->vec, 3 * sizeof (float));
	}

	// Check root param_obj
	const aamp_param_object_t *root_param = find_object (&aamp.root, "param_obj");
	if (root_param)
	{
		const aamp_param_entry_t *e;
		if ((e = find_entry (root_param, "dir_light_indirect"))) file->settings.dir_light_indirect = e->f;
		if ((e = find_entry (root_param, "point_light_indirect"))) file->settings.point_light_indirect = e->f;
		if ((e = find_entry (root_param, "spot_light_indirect"))) file->settings.spot_light_indirect = e->f;
		if ((e = find_entry (root_param, "emission_scale"))) file->settings.emission_scale = e->f;
	}

	// Scan boxes (child lists of root)
	file->num_boxes = aamp.root.list_count;
	if (file->num_boxes > 0)
	{
		file->boxes = CALLOC (file->num_boxes, sizeof (bglpbd_box_t));
		for (size_t b = 0; b < file->num_boxes; b++)
		{
			const aamp_param_list_t *blist = &aamp.root.lists[b];
			bglpbd_box_t *box = &file->boxes[b];

			const aamp_param_object_t *grid = find_object (blist, "grid");
			if (grid)
			{
				const aamp_param_entry_t *e_min = find_entry (grid, "aabb_min_pos");
				const aamp_param_entry_t *e_max = find_entry (grid, "aabb_max_pos");
				const aamp_param_entry_t *e_step = find_entry (grid, "voxel_step_pos");
				if (e_min) memcpy (box->min_pos, e_min->vec, 3 * sizeof (float));
				if (e_max) memcpy (box->max_pos, e_max->vec, 3 * sizeof (float));
				if (e_step) memcpy (box->step_pos, e_step->vec, 3 * sizeof (float));
				calc_stride (box->min_pos, box->max_pos, box->step_pos, box->stride);
			}

			const aamp_param_object_t *idx_obj = find_object (blist, "sh_index_buffer");
			if (idx_obj)
			{
				const aamp_param_entry_t *e_idx = find_entry (idx_obj, "index_buffer");
				if (e_idx && (e_idx->type == AAMP_TYPE_BUFFER_UINT || e_idx->type == AAMP_TYPE_BUFFER_INT) && e_idx->buf.data)
				{
					const u32 *packed = (const u32 *)e_idx->buf.data;
					size_t num_u32 = e_idx->buf.count;
					box->num_indices = num_u32 * 2;
					box->probe_indices = MALLOC (box->num_indices * sizeof (u16));
					for (size_t k = 0; k < num_u32; k++)
					{
						// In AglLightProbeTool:
						// buffer[i] = (ushort)(packedData[i] >> 16);
						// buffer[i + 1] = (ushort)(packedData[i] & 0xFFFF);
						// but note SetProbeIndicesUint32Buffer had:
						// lowUShort = unpacked[2*i]; highUShort = unpacked[2*i+1]<<16;
						// so low = 2*k, high = 2*k+1.
						u32 val = packed[k];
						box->probe_indices[k * 2 + 0] = (u16)(val & 0xFFFF);
						box->probe_indices[k * 2 + 1] = (u16)(val >> 16);
					}
				}
			}

			const aamp_param_object_t *data_obj = find_object (blist, "sh_data_buffer");
			if (data_obj)
			{
				const aamp_param_entry_t *e_data = find_entry (data_obj, "data_buffer");
				if (e_data && e_data->type == AAMP_TYPE_BUFFER_FLOAT && e_data->buf.data)
				{
					box->num_sh_data = e_data->buf.count / 27;
					box->sh_buffer = MALLOC (e_data->buf.count * sizeof (float));
					memcpy (box->sh_buffer, e_data->buf.data, e_data->buf.count * sizeof (float));
				}
			}
		}
	}

	ResetAAMP (&aamp);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// Encoding & AAMP Generation
///////////////////////////////////////////////////////////////////////////////

static void add_entry_vec3 (aamp_param_object_t *obj, const char *name, const float v[3])
{
	aamp_param_entry_t e;
	memset (&e, 0, sizeof (e));
	e.hash = AAMP_NameToHash (name);
	e.type = AAMP_TYPE_VEC3;
	e.vec[0] = v[0];
	e.vec[1] = v[1];
	e.vec[2] = v[2];
	e.vec[3] = 0.0f;

	if (obj->entry_count >= obj->entry_alloc)
	{
		obj->entry_alloc = obj->entry_alloc ? obj->entry_alloc * 2 : 8;
		obj->entries = REALLOC (obj->entries, obj->entry_alloc * sizeof (aamp_param_entry_t));
	}
	obj->entries[obj->entry_count++] = e;
}

static void add_entry_float (aamp_param_object_t *obj, const char *name, float val)
{
	aamp_param_entry_t e;
	memset (&e, 0, sizeof (e));
	e.hash = AAMP_NameToHash (name);
	e.type = AAMP_TYPE_FLOAT;
	e.f = val;

	if (obj->entry_count >= obj->entry_alloc)
	{
		obj->entry_alloc = obj->entry_alloc ? obj->entry_alloc * 2 : 8;
		obj->entries = REALLOC (obj->entries, obj->entry_alloc * sizeof (aamp_param_entry_t));
	}
	obj->entries[obj->entry_count++] = e;
}

static void add_entry_int (aamp_param_object_t *obj, const char *name, s32 val)
{
	aamp_param_entry_t e;
	memset (&e, 0, sizeof (e));
	e.hash = AAMP_NameToHash (name);
	e.type = AAMP_TYPE_INT;
	e.i = val;

	if (obj->entry_count >= obj->entry_alloc)
	{
		obj->entry_alloc = obj->entry_alloc ? obj->entry_alloc * 2 : 8;
		obj->entries = REALLOC (obj->entries, obj->entry_alloc * sizeof (aamp_param_entry_t));
	}
	obj->entries[obj->entry_count++] = e;
}

static void add_entry_uint (aamp_param_object_t *obj, const char *name, u32 val)
{
	aamp_param_entry_t e;
	memset (&e, 0, sizeof (e));
	e.hash = AAMP_NameToHash (name);
	e.type = AAMP_TYPE_UINT;
	e.u = val;

	if (obj->entry_count >= obj->entry_alloc)
	{
		obj->entry_alloc = obj->entry_alloc ? obj->entry_alloc * 2 : 8;
		obj->entries = REALLOC (obj->entries, obj->entry_alloc * sizeof (aamp_param_entry_t));
	}
	obj->entries[obj->entry_count++] = e;
}

static aamp_param_object_t *list_add_obj (aamp_param_list_t *list, const char *name)
{
	if (list->object_count >= list->object_alloc)
	{
		list->object_alloc = list->object_alloc ? list->object_alloc * 2 : 8;
		list->objects = REALLOC (list->objects, list->object_alloc * sizeof (aamp_param_object_t));
	}
	aamp_param_object_t *obj = &list->objects[list->object_count++];
	memset (obj, 0, sizeof (*obj));
	obj->hash = AAMP_NameToHash (name);
	return obj;
}

static aamp_param_list_t *list_add_sublist (aamp_param_list_t *list, const char *name)
{
	if (list->list_count >= list->list_alloc)
	{
		list->list_alloc = list->list_alloc ? list->list_alloc * 2 : 8;
		list->lists = REALLOC (list->lists, list->list_alloc * sizeof (aamp_param_list_t));
	}
	aamp_param_list_t *sub = &list->lists[list->list_count++];
	memset (sub, 0, sizeof (*sub));
	sub->hash = AAMP_NameToHash (name);
	return sub;
}

enumError BGLPBD_ToAAMP (const bglpbd_file_t *file, aamp_file_t *aamp)
{
	if (!file || !aamp)
		return ERR_INVALID_DATA;

	InitializeAAMP (aamp);
	aamp->version = file->is_switch ? 2 : 1;
	aamp->is_le = file->is_switch;
	aamp->pio_version = 0;
	snprintf (aamp->pio_type, sizeof (aamp->pio_type), "glpbd");
	aamp->root.hash = AAMP_NameToHash ("param_root");

	// Add root_grid
	aamp_param_object_t *root_grid = list_add_obj (&aamp->root, "root_grid");
	add_entry_vec3 (root_grid, "aabb_min_pos", file->root_min);
	add_entry_vec3 (root_grid, "aabb_max_pos", file->root_max);
	add_entry_vec3 (root_grid, "voxel_step_pos", file->root_step);

	// Add root param_obj
	aamp_param_object_t *root_param = list_add_obj (&aamp->root, "param_obj");
	add_entry_uint (root_param, "version", 1);
	add_entry_float (root_param, "dir_light_indirect", file->settings.dir_light_indirect);
	add_entry_float (root_param, "point_light_indirect", file->settings.point_light_indirect);
	add_entry_float (root_param, "spot_light_indirect", file->settings.spot_light_indirect);
	add_entry_float (root_param, "emission_scale", file->settings.emission_scale);
	add_entry_uint (root_param, "used_box_num", (u32)file->num_boxes);

	// Add boxes
	for (size_t b = 0; b < file->num_boxes; b++)
	{
		char bname[32];
		snprintf (bname, sizeof (bname), "b_%u", (uint)b);
		aamp_param_list_t *blist = list_add_sublist (&aamp->root, bname);
		const bglpbd_box_t *box = &file->boxes[b];

		// Box param_obj
		aamp_param_object_t *b_obj = list_add_obj (blist, "param_obj");
		add_entry_int (b_obj, "index", (s32)b);
		add_entry_int (b_obj, "type", 0);

		// Box grid
		aamp_param_object_t *b_grid = list_add_obj (blist, "grid");
		add_entry_vec3 (b_grid, "aabb_min_pos", box->min_pos);
		add_entry_vec3 (b_grid, "aabb_max_pos", box->max_pos);
		add_entry_vec3 (b_grid, "voxel_step_pos", box->step_pos);

		// Box sh_index_buffer
		aamp_param_object_t *b_idx = list_add_obj (blist, "sh_index_buffer");
		add_entry_int (b_idx, "type", 1);
		size_t num_packed = (box->num_indices + 1) / 2;
		add_entry_int (b_idx, "used_index_num", (s32)num_packed);
		add_entry_int (b_idx, "max_index_num", (s32)num_packed);

		u32 *packed_indices = CALLOC (num_packed, sizeof (u32));
		for (size_t k = 0; k < num_packed; k++)
		{
			u32 low = (k * 2 < box->num_indices) ? box->probe_indices[k * 2] : 0;
			u32 high = (k * 2 + 1 < box->num_indices) ? box->probe_indices[k * 2 + 1] : 0;
			packed_indices[k] = low | (high << 16);
		}
		aamp_param_entry_t e_idx_buf;
		memset (&e_idx_buf, 0, sizeof (e_idx_buf));
		e_idx_buf.hash = AAMP_NameToHash ("index_buffer");
		e_idx_buf.type = AAMP_TYPE_BUFFER_UINT;
		e_idx_buf.buf.count = (u32)num_packed;
		e_idx_buf.buf.data = packed_indices;
		if (b_idx->entry_count >= b_idx->entry_alloc)
		{
			b_idx->entry_alloc = b_idx->entry_alloc ? b_idx->entry_alloc * 2 : 8;
			b_idx->entries = REALLOC (b_idx->entries, b_idx->entry_alloc * sizeof (aamp_param_entry_t));
		}
		b_idx->entries[b_idx->entry_count++] = e_idx_buf;

		// Box sh_data_buffer
		aamp_param_object_t *b_sh = list_add_obj (blist, "sh_data_buffer");
		add_entry_int (b_sh, "type", 0);
		add_entry_int (b_sh, "max_sh_data_num", (s32)box->num_sh_data);
		add_entry_int (b_sh, "used_data_num", (s32)box->num_sh_data);
		add_entry_int (b_sh, "per_probe_float_num", 27);

		size_t float_count = box->num_sh_data * 27;
		float *sh_floats = CALLOC (float_count ? float_count : 1, sizeof (float));
		if (box->sh_buffer && float_count > 0)
			memcpy (sh_floats, box->sh_buffer, float_count * sizeof (float));

		aamp_param_entry_t e_sh_buf;
		memset (&e_sh_buf, 0, sizeof (e_sh_buf));
		e_sh_buf.hash = AAMP_NameToHash ("data_buffer");
		e_sh_buf.type = AAMP_TYPE_BUFFER_FLOAT;
		e_sh_buf.buf.count = (u32)float_count;
		e_sh_buf.buf.data = sh_floats;
		if (b_sh->entry_count >= b_sh->entry_alloc)
		{
			b_sh->entry_alloc = b_sh->entry_alloc ? b_sh->entry_alloc * 2 : 8;
			b_sh->entries = REALLOC (b_sh->entries, b_sh->entry_alloc * sizeof (aamp_param_entry_t));
		}
		b_sh->entries[b_sh->entry_count++] = e_sh_buf;
	}

	return ERR_OK;
}

enumError EncodeBGLPBD (const bglpbd_file_t *file, u8 **dest, size_t *dest_size)
{
	aamp_file_t aamp;
	enumError err = BGLPBD_ToAAMP (file, &aamp);
	if (err)
		return err;

	err = WriteAAMP (&aamp, dest, dest_size, aamp.version, aamp.is_le);
	ResetAAMP (&aamp);
	return err;
}

///////////////////////////////////////////////////////////////////////////////
// Generator from Bounds & Model
///////////////////////////////////////////////////////////////////////////////

enumError CreateBGLPBD_FromBounds (bglpbd_file_t *file, const float min_pos[3], const float max_pos[3],
	const float step_pos[3], const bglpbd_settings_t *settings, bool is_switch)
{
	if (!file)
		return ERR_INVALID_DATA;

	InitializeBGLPBD (file);
	file->is_switch = is_switch;
	if (settings)
		file->settings = *settings;

	memcpy (file->root_min, min_pos, 3 * sizeof (float));
	memcpy (file->root_max, max_pos, 3 * sizeof (float));
	memcpy (file->root_step, step_pos, 3 * sizeof (float));

	file->num_boxes = 1;
	file->boxes = CALLOC (1, sizeof (bglpbd_box_t));
	bglpbd_box_t *box = &file->boxes[0];
	memcpy (box->min_pos, min_pos, 3 * sizeof (float));
	memcpy (box->max_pos, max_pos, 3 * sizeof (float));
	memcpy (box->step_pos, step_pos, 3 * sizeof (float));
	calc_stride (box->min_pos, box->max_pos, box->step_pos, box->stride);

	size_t num_probes = (size_t)box->stride[0] * box->stride[1] * box->stride[2] * 8;
	box->num_indices = num_probes;
	box->probe_indices = CALLOC (num_probes, sizeof (u16));

	// Default AglLightProbeTool generator creates 8 constant colors cycling 0..7
	u16 sh_idx = 0;
	for (size_t i = 0; i < num_probes; i++)
	{
		box->probe_indices[i] = sh_idx++;
		if (sh_idx >= 8)
			sh_idx = 0;
	}

	box->num_sh_data = 8;
	box->sh_buffer = CALLOC (8 * 27, sizeof (float));

	const float default_colors[8][3] = {
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f },
		{ 1.0f, 1.0f, 0.0f },
		{ 0.0f, 1.0f, 1.0f },
		{ 0.5f, 1.0f, 0.5f },
		{ 0.0f, 0.5f, 1.0f },
		{ 1.0f, 0.5f, 0.5f }
	};

	// If a custom non-default color was passed in settings, scale them
	for (int i = 0; i < 8; i++)
	{
		float c[3] = {
			default_colors[i][0] * file->settings.color[0],
			default_colors[i][1] * file->settings.color[1],
			default_colors[i][2] * file->settings.color[2]
		};
		SH_SetConstantColor (box->sh_buffer + i * 27, c);
	}

	return ERR_OK;
}

enumError CreateBGLPBD_FromModel (bglpbd_file_t *file, const model_t *model,
	const bglpbd_settings_t *settings, bool is_switch)
{
	if (!file || !model || model->num_meshes == 0)
		return ERR_INVALID_DATA;

	float min_p[3] = { 1e30f, 1e30f, 1e30f };
	float max_p[3] = { -1e30f, -1e30f, -1e30f };

	bool found_vtx = false;
	for (size_t m = 0; m < model->num_meshes; m++)
	{
		const mesh_t *mesh = &model->meshes[m];
		for (size_t v = 0; v < mesh->num_positions; v++)
		{
			const vec3_t *p = &mesh->positions[v];
			if (p->x < min_p[0]) min_p[0] = p->x;
			if (p->y < min_p[1]) min_p[1] = p->y;
			if (p->z < min_p[2]) min_p[2] = p->z;
			if (p->x > max_p[0]) max_p[0] = p->x;
			if (p->y > max_p[1]) max_p[1] = p->y;
			if (p->z > max_p[2]) max_p[2] = p->z;
			found_vtx = true;
		}
	}

	if (!found_vtx)
	{
		min_p[0] = min_p[1] = min_p[2] = -1000.0f;
		max_p[0] = max_p[1] = max_p[2] = 1000.0f;
	}

	const float step[3] = { 1000.0f, 1000.0f, 1000.0f };
	return CreateBGLPBD_FromBounds (file, min_p, max_p, step, settings, is_switch);
}

///////////////////////////////////////////////////////////////////////////////
// Unity Probe TXT Parser & Generator
///////////////////////////////////////////////////////////////////////////////

static float *lerp_sh (const float a[27], const float b[27], float t, float out[27])
{
	for (int i = 0; i < 27; i++)
		out[i] = a[i] + (b[i] - a[i]) * t;
	return out;
}

static const float *get_unity_point (const float *buffer, size_t total_sh, u32 sx, u32 sy, u32 sz, int x, int y, int z)
{
	if (x < 0) x = 0;
	if (x >= (int)sx) x = (int)sx - 1;
	if (y < 0) y = 0;
	if (y >= (int)sy) y = (int)sy - 1;
	if (z < 0) z = 0;
	if (z >= (int)sz) z = (int)sz - 1;

	int inv_x = (int)(sx - x);
	if (inv_x < 0) inv_x = 0;
	if (inv_x >= (int)sx) inv_x = (int)sx - 1;

	size_t idx = (size_t)(sx * sz * y + sx * z + inv_x);
	if (idx >= total_sh)
		idx = total_sh ? total_sh - 1 : 0;
	return buffer + idx * 27;
}

enumError CreateBGLPBD_FromUnityText (bglpbd_file_t *file, const char *txt_content, size_t txt_len,
	const bglpbd_settings_t *settings, bool is_switch)
{
	if (!file || !txt_content || txt_len == 0)
		return ERR_INVALID_DATA;

	InitializeBGLPBD (file);
	file->is_switch = is_switch;
	if (settings)
		file->settings = *settings;

	// Parse first line header: e.g. Count; (minX, minY, minZ); (maxX, maxY, maxZ); Step
	float u_min[3] = { 0, 0, 0 };
	float u_max[3] = { 0, 0, 0 };
	float u_step[3] = { 100.0f, 100.0f, 100.0f };

	const char *p = txt_content;
	const char *end = txt_content + txt_len;

	// Read first line
	const char *eol = memchr (p, '\n', (size_t)(end - p));
	size_t line_len = eol ? (size_t)(eol - p) : (size_t)(end - p);
	char head_line[512];
	if (line_len >= sizeof (head_line))
		line_len = sizeof (head_line) - 1;
	memcpy (head_line, p, line_len);
	head_line[line_len] = '\0';
	p = eol ? eol + 1 : end;

	// Parse tokens split by ';'
	char *saveptr = NULL;
	(void)strtok_r (head_line, ";", &saveptr);
	char *part1 = strtok_r (NULL, ";", &saveptr);
	char *part2 = strtok_r (NULL, ";", &saveptr);
	char *part3 = strtok_r (NULL, ";", &saveptr);

	if (part1 && part2)
	{
		sscanf (part1, " ( %f , %f , %f )", &u_min[0], &u_min[1], &u_min[2]);
		sscanf (part2, " ( %f , %f , %f )", &u_max[0], &u_max[1], &u_max[2]);
	}
	if (part3)
	{
		float s = 100.0f;
		if (sscanf (part3, " %f", &s) == 1 && s > 1e-3f)
			u_step[0] = u_step[1] = u_step[2] = s;
	}

	// Read float buffer
	size_t flt_cap = 1024;
	size_t flt_count = 0;
	float *u_buffer = MALLOC (flt_cap * sizeof (float));

	while (p < end)
	{
		while (p < end && (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t'))
			p++;
		if (p >= end)
			break;
		char fbuf[64];
		size_t fi = 0;
		while (p < end && *p != '\r' && *p != '\n' && *p != ' ' && *p != '\t' && fi < sizeof (fbuf) - 1)
		{
			char c = *p++;
			if (c == ',') c = '.';
			fbuf[fi++] = c;
		}
		fbuf[fi] = '\0';
		float v = 0.0f;
		if (sscanf (fbuf, "%f", &v) == 1)
		{
			if (flt_count >= flt_cap)
			{
				flt_cap *= 2;
				u_buffer = REALLOC (u_buffer, flt_cap * sizeof (float));
			}
			u_buffer[flt_count++] = v;
		}
	}

	// Padding as done in UnityProbeGenerator.cs:
	// box.Min = box.Min - box.Step * 0.5f;
	// box.Max = box.Max + box.Step * 0.5f;
	for (int i = 0; i < 3; i++)
	{
		u_min[i] -= u_step[i] * 0.5f;
		u_max[i] += u_step[i] * 0.5f;
	}

	memcpy (file->root_min, u_min, 3 * sizeof (float));
	memcpy (file->root_max, u_max, 3 * sizeof (float));
	memcpy (file->root_step, u_step, 3 * sizeof (float));

	file->num_boxes = 1;
	file->boxes = CALLOC (1, sizeof (bglpbd_box_t));
	bglpbd_box_t *box = &file->boxes[0];
	memcpy (box->min_pos, u_min, 3 * sizeof (float));
	memcpy (box->max_pos, u_max, 3 * sizeof (float));
	memcpy (box->step_pos, u_step, 3 * sizeof (float));
	calc_stride (box->min_pos, box->max_pos, box->step_pos, box->stride);

	size_t num_indices = (size_t)box->stride[0] * box->stride[1] * box->stride[2] * 8;
	box->num_indices = num_indices;
	box->probe_indices = CALLOC (num_indices, sizeof (u16));

	size_t total_sh = flt_count / 27;

	// Dynamic deduplicating color buffer
	size_t col_cap = 256;
	size_t col_count = 0;
	float *col_buf = MALLOC (col_cap * 27 * sizeof (float));

	float lerp_ratio = 0.5f;

	for (u32 y = 0; y < box->stride[1]; y++)
	{
		for (u32 z = 0; z < box->stride[2]; z++)
		{
			for (u32 x = 0; x < box->stride[0]; x++)
			{
				for (int i = 0; i < 8; i++)
				{
					size_t p_idx = (size_t)(box->stride[0] * box->stride[2] * y + box->stride[0] * z + x) * 8 + i;
					const float *orig_sh = get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x, y, z);

					float sh_data[27];
					memcpy (sh_data, orig_sh, 27 * sizeof (float));

					float tmp_sh[27];
					switch (i)
					{
						case 0: // Top front right: x+1, y+1, z+1
							lerp_sh (sh_data, get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x+1, y+1, z+1), lerp_ratio, tmp_sh);
							memcpy (sh_data, tmp_sh, 27 * sizeof (float));
							break;
						case 1: // Top front left: x-1, y+1, z+1
							lerp_sh (sh_data, get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x-1, y+1, z+1), lerp_ratio, tmp_sh);
							memcpy (sh_data, tmp_sh, 27 * sizeof (float));
							break;
						case 2: // Top back right: x+1, y+1, z-1
							lerp_sh (sh_data, get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x+1, y+1, z-1), lerp_ratio, tmp_sh);
							memcpy (sh_data, tmp_sh, 27 * sizeof (float));
							break;
						case 3: // Top back left: x-1, y+1, z-1
							lerp_sh (sh_data, get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x-1, y+1, z-1), lerp_ratio, tmp_sh);
							memcpy (sh_data, tmp_sh, 27 * sizeof (float));
							break;
						case 4: // Bottom front right: x+1, y-1, z+1
							lerp_sh (sh_data, get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x+1, y-1, z+1), lerp_ratio, tmp_sh);
							memcpy (sh_data, tmp_sh, 27 * sizeof (float));
							break;
						case 5: // Bottom front left: x-1, y-1, z+1
							lerp_sh (sh_data, get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x-1, y-1, z+1), lerp_ratio, tmp_sh);
							memcpy (sh_data, tmp_sh, 27 * sizeof (float));
							break;
						case 6: // Bottom back right: x+1, y-1, z-1
							lerp_sh (sh_data, get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x+1, y-1, z-1), lerp_ratio, tmp_sh);
							memcpy (sh_data, tmp_sh, 27 * sizeof (float));
							break;
						case 7: // Bottom back left: x-1, y-1, z-1
							lerp_sh (sh_data, get_unity_point (u_buffer, total_sh, box->stride[0], box->stride[1], box->stride[2], x-1, y-1, z-1), lerp_ratio, tmp_sh);
							memcpy (sh_data, tmp_sh, 27 * sizeof (float));
							break;
					}

					bool all_zero = true;
					for (int k = 0; k < 27; k++)
					{
						if (fabsf (sh_data[k]) > 1e-6f)
						{
							all_zero = false;
							break;
						}
					}

					if (all_zero)
					{
						box->probe_indices[p_idx] = BGLPBD_EMPTY_PROBE_IDX;
						continue;
					}

					// Find in col_buf
					int found_entry = -1;
					for (size_t c = 0; c < col_count; c++)
					{
						if (!memcmp (col_buf + c * 27, sh_data, 27 * sizeof (float)))
						{
							found_entry = (int)c;
							break;
						}
					}

					if (found_entry < 0)
					{
						if (col_count >= col_cap)
						{
							col_cap *= 2;
							col_buf = REALLOC (col_buf, col_cap * 27 * sizeof (float));
						}
						found_entry = (int)col_count++;
						memcpy (col_buf + found_entry * 27, sh_data, 27 * sizeof (float));
					}

					box->probe_indices[p_idx] = (u16)found_entry;
				}
			}
		}
	}

	FREE (u_buffer);

	box->num_sh_data = col_count;
	box->sh_buffer = col_buf;

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
// Dump PNG Images
///////////////////////////////////////////////////////////////////////////////

enumError DumpBGLPBD_Images (const bglpbd_file_t *file, const char *out_prefix, bool overwrite)
{
	if (!file || !out_prefix)
		return ERR_INVALID_DATA;

	for (size_t b = 0; b < file->num_boxes; b++)
	{
		const bglpbd_box_t *box = &file->boxes[b];
		if (!box->probe_indices || !box->sh_buffer)
			continue;

		uint width = box->stride[0];
		uint height = box->stride[2] * box->stride[1];
		if (width == 0 || height == 0)
			continue;

		size_t img_pixels = (size_t)width * height;

		// 8 direction images
		for (int dir_idx = 0; dir_idx < 8; dir_idx++)
		{
			u8 *rgba = CALLOC (img_pixels * 4, 1);

			for (u32 y = 0; y < box->stride[1]; y++)
			{
				for (u32 z = 0; z < box->stride[2]; z++)
				{
					for (u32 x = 0; x < box->stride[0]; x++)
					{
						size_t probe_linear_idx = (size_t)(box->stride[0] * box->stride[2] * y + box->stride[0] * z + x) * 8 + dir_idx;
						size_t pixel_idx = ((size_t)y * box->stride[2] + z) * box->stride[0] + x;
						u8 *px = rgba + pixel_idx * 4;

						u16 p_idx = (probe_linear_idx < box->num_indices) ? box->probe_indices[probe_linear_idx] : BGLPBD_EMPTY_PROBE_IDX;
						if (p_idx == BGLPBD_INIT_PROBE_IDX || p_idx == BGLPBD_INV_PROBE_IDX || p_idx == BGLPBD_EMPTY_PROBE_IDX)
						{
							px[0] = 0;
							px[1] = 0;
							px[2] = 0;
							px[3] = 255;
						}
						else if (p_idx < box->num_sh_data)
						{
							const float *sh = box->sh_buffer + p_idx * 27;
							float sh2rgb[7][4];
							SH_ConvertSH2RGB (sh, sh2rgb);
							const float normal[3] = { 0.0f, -1.0f, 0.0f };
							float rgb[3];
							SH_GetRGBColor (normal, sh2rgb, rgb);

							px[0] = (u8)fminf (255.0f, fmaxf (0.0f, rgb[0] * 255.0f));
							px[1] = (u8)fminf (255.0f, fmaxf (0.0f, rgb[1] * 255.0f));
							px[2] = (u8)fminf (255.0f, fmaxf (0.0f, rgb[2] * 255.0f));
							px[3] = 255;
						}
						else
						{
							px[3] = 255;
						}
					}
				}
			}

			char out_png[PATH_MAX];
			snprintf (out_png, sizeof (out_png), "%s_box%u_%d.png", out_prefix, (uint)b, dir_idx);
			SaveDecodedRGBAToPNG (rgba, width, height, &be_func, out_png, 0, overwrite);
		}
	}

	return ERR_OK;
}
