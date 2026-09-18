// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_BGLPBD_H
#define LIB_BGLPBD_H 1

#include "lib-nintendo.h"
#include "lib-aamp.h"
#include "lib-model-glb.h"

#define BGLPBD_INV_PROBE_IDX   0xfff5
#define BGLPBD_EMPTY_PROBE_IDX 0xfff6
#define BGLPBD_INIT_PROBE_IDX  0xfff7

typedef struct
{
	float min_pos[3];
	float max_pos[3];
	float step_pos[3];
	u32 stride[3];

	u16 *probe_indices;
	size_t num_indices;

	float *sh_buffer;
	size_t num_sh_data; // number of 27-float probes (sh_buffer length is num_sh_data * 27)
} bglpbd_box_t;

typedef struct
{
	float color[3];
	float dir_light_indirect;
	float point_light_indirect;
	float spot_light_indirect;
	float emission_scale;
} bglpbd_settings_t;

typedef struct
{
	u32 version;
	bool is_switch; // true = V2 little endian, false = V1 big endian
	float root_min[3];
	float root_max[3];
	float root_step[3];
	bglpbd_settings_t settings;

	bglpbd_box_t *boxes;
	size_t num_boxes;
} bglpbd_file_t;

void InitializeBGLPBD (bglpbd_file_t *file);
void ResetBGLPBD (bglpbd_file_t *file);

bool IsBGLPBD (const u8 *data, size_t size);

// Scan an AAMP-based BGLPBD file
enumError ScanBGLPBD (bglpbd_file_t *file, const u8 *data, size_t size);

// Convert BGLPBD to AAMP file structure
enumError BGLPBD_ToAAMP (const bglpbd_file_t *file, aamp_file_t *aamp);

// Encode BGLPBD directly to binary AAMP buffer
enumError EncodeBGLPBD (const bglpbd_file_t *file, u8 **dest, size_t *dest_size);

// Create BGLPBD from a model bounding box (or model_t)
enumError CreateBGLPBD_FromBounds (bglpbd_file_t *file, const float min_pos[3], const float max_pos[3],
	const float step_pos[3], const bglpbd_settings_t *settings, bool is_switch);

// Create BGLPBD from model_t
enumError CreateBGLPBD_FromModel (bglpbd_file_t *file, const model_t *model,
	const bglpbd_settings_t *settings, bool is_switch);

// Import Unity light probe txt format into BGLPBD
enumError CreateBGLPBD_FromUnityText (bglpbd_file_t *file, const char *txt_content, size_t txt_len,
	const bglpbd_settings_t *settings, bool is_switch);

// Dump 8 direction PNG slices per box (into out_prefix with naming <prefix>box<B>_<dir>.png)
enumError DumpBGLPBD_Images (const bglpbd_file_t *file, const char *out_prefix, bool overwrite);

// Spherical Harmonics utility functions
void SH_UpdateCoeff (float *sh_data_27, const float color[3], const float dir[3]);
void SH_UpdateCoeffBias (float *sh_data_27, const float color[3], const float dir[3]);
void SH_SetConstantColor (float *sh_data_27, const float color[3]);
void SH_ConvertSH2RGB (const float sh_data_27[27], float rgb_out[7][4]);
void SH_GetRGBColor (const float normal[3], const float sh_data_rgba[7][4], float rgb_out[3]);

#endif // LIB_BGLPBD_H
