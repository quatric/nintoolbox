#ifndef LIB_BNVIB_H
#define LIB_BNVIB_H

#include "lib-nintendo.h"

// Nintendo Switch Joy-Con HD Rumble Vibration Binary (.bnvib / .nvib)
// Used in NintendoSDK (Siglo) across all Nintendo Switch titles for HD rumble.
//
// Header (little-endian):
//   u32 meta_data_size   (minimum 4; 12 if loop info; 16 if loop interval)
//   u16 format_id        (always 3)
//   u16 sampling_rate    (always 200 Hz, i.e. 5ms per frame)
//   [if meta_data_size >= 12]
//     u32 loop_start     (sample index)
//     u32 loop_end       (sample index)
//   [if meta_data_size >= 16]
//     u32 loop_interval  (interval duration)
//   u32 data_size        (bytes of sample data)
//   u8  data[]           (4 bytes per sample: amp_low, freq_low, amp_high, freq_high)

typedef struct bnvib_sample_t
{
	float amp_low;
	float freq_low;
	float amp_high;
	float freq_high;
} bnvib_sample_t;

typedef struct bnvib_t
{
	u32 meta_data_size;
	u16 format_id;
	u16 sampling_rate;
	bool is_loop;
	u32 loop_start;
	u32 loop_end;
	u32 loop_interval;
	u32 sample_count;
	bnvib_sample_t *samples;
} bnvib_t;

void InitializeBNVIB (bnvib_t *vib);
void ResetBNVIB (bnvib_t *vib);

bool IsBNVIB (const u8 *data, uint size);
enumError ScanBNVIB (bnvib_t *vib, const u8 *data, uint size);
enumError SaveTextBNVIB (const bnvib_t *vib, ccp dest_path);

#endif // LIB_BNVIB_H
