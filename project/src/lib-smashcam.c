// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 stage camera/animation files: path.bin stage
// paths, CMR0 camera motion and light.bin lighting animation.
//
// Reference: KillzXGaming/Smash-Forge,
// "Smash Forge/Filetypes/Animation/PATH.cs",
// "Smash Forge/Filetypes/Animation/CMR0.cs" and
// "Smash Forge/Filetypes/Animation/LIGH.cs"
// (MIT licensed; clean-room C ports of the binary layouts).

#include "lib-smashcam.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <string.h>

static float smashcam_f32be (const u8 *p)
{
	u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static float smashcam_f32le (const u8 *p)
{
	u32 v = rd_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

// ---------------------------------------------------------------------------
// path.bin: 12 magic bytes (not validated by the reference reader),
// s32 frame count, then frames of 7 floats (qx qy qz qw x y z).
// Endianness follows the host of the reference tool; accept a file only
// under its real filename with an exact-size match in either endianness.

static bool smashcam_path_frames_ok (const u8 *data, size_t size, bool be, u32 *count)
{
	if (size < 16)
		return false;
	const u32 n = be ? rd_be32 (data + 12) : rd_le32 (data + 12);
	if (!n || n > 1000000)
		return false;
	if (16 + (size_t)n * 28 != size)
		return false;
	*count = n;
	return true;
}

bool IsSmashPath (const u8 *data, size_t size, ccp name)
{
	if (!data || !name)
		return false;
	ccp leaf = leaf_name (name);
	if (strcasecmp (leaf, "path.bin"))
		return false;
	u32 n;
	return smashcam_path_frames_ok (data, size, true, &n)
		|| smashcam_path_frames_ok (data, size, false, &n);
}

enumError DecodeSmashPath_Text (FILE *out, const u8 *data, size_t size)
{
	u32 n;
	bool be = true;
	if (!out || !data
		|| (!smashcam_path_frames_ok (data, size, true, &n)
			&& (be = false, !smashcam_path_frames_ok (data, size, false, &n))))
		return ERR_INVALID_DATA;

	fprintf (out, "#PATH\n# Super Smash Bros. 4 stage path/camera spline\n\n"
		"endian = %s\nframes = %u\n\n[frames]\n# idx | quat xyzw | pos xyz\n",
		be ? "big" : "little", n);
	for (u32 i = 0; i < n; i++)
	{
		const u8 *f = data + 16 + (size_t)i * 28;
		fprintf (out, "%u | %.6g %.6g %.6g %.6g | %.6g %.6g %.6g\n", i,
			be ? smashcam_f32be (f) : smashcam_f32le (f),
			be ? smashcam_f32be (f + 4) : smashcam_f32le (f + 4),
			be ? smashcam_f32be (f + 8) : smashcam_f32le (f + 8),
			be ? smashcam_f32be (f + 12) : smashcam_f32le (f + 12),
			be ? smashcam_f32be (f + 16) : smashcam_f32le (f + 16),
			be ? smashcam_f32be (f + 20) : smashcam_f32le (f + 20),
			be ? smashcam_f32be (f + 24) : smashcam_f32le (f + 24));
	}
	return ERR_OK;
}

// ---------------------------------------------------------------------------
// CMR0: big-endian, 8 header bytes skipped by the reference reader,
// s32 frame count, then 3x4-float row-major matrices (fourth row
// implied 0 0 0 1). No magic: accept only by structural exact-size
// match on camera-flavoured filenames.

bool IsSmashCMR0 (const u8 *data, size_t size)
{
	if (!data || size < 12)
		return false;
	const u32 n = rd_be32 (data + 8);
	return n && n < 100000 && 12 + (size_t)n * 48 == size;
}

enumError DecodeSmashCMR0_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsSmashCMR0 (data, size))
		return ERR_INVALID_DATA;
	const u32 n = rd_be32 (data + 8);
	fprintf (out, "#CMR0\n# Super Smash Bros. 4 camera motion\n\nframes = %u\n\n[frames]\n", n);
	for (u32 i = 0; i < n; i++)
	{
		const u8 *m = data + 12 + (size_t)i * 48;
		fprintf (out, "frame %u\n", i);
		for (int r = 0; r < 3; r++)
			fprintf (out, "  %.6g %.6g %.6g %.6g\n",
				smashcam_f32be (m + r * 16), smashcam_f32be (m + r * 16 + 4),
				smashcam_f32be (m + r * 16 + 8), smashcam_f32be (m + r * 16 + 12));
	}
	return ERR_OK;
}

// ---------------------------------------------------------------------------
// light.bin: "LIGH", s32 version (4/5), s32 frameCount,
// s32 frameDuration (v5 only), 6 absolute offsets (light data first,
// then 5 RGB tracks, 0 = disabled). Light frames: 17 sets of
// 4 lights { s32 enabled; float angle[3], hue, sat, val } + 4-byte fog,
// then effect { u8 unknown, rgb[3], float pos[3] }.

static bool smashcam_ligh_ok (const u8 *data, size_t size,
	u32 *frames, u32 *ver, u32 offs[6])
{
	if (!data || size < 12 || memcmp (data, "LIGH", 4))
		return false;
	const u32 v = rd_be32 (data + 4);
	if (v != 4 && v != 5)
		return false;
	const u32 nf = rd_be32 (data + 8);
	if (!nf || nf > 100000)
		return false;
	const size_t hbase = v == 5 ? 16 : 12;
	if (hbase + 24 > size)
		return false;
	for (int i = 0; i < 6; i++)
		offs[i] = rd_be32 (data + hbase + (size_t)i * 4);

	// light data: frames * (17 * (4 * 28 + 4) + 16) bytes
	const size_t light_sz = (size_t)nf * (17 * (4 * 28 + 4) + 16);
	if ((size_t)offs[0] + light_sz > size)
		return false;
	for (int i = 1; i < 6; i++)
	{
		if (!offs[i])
			continue;
		const size_t rgb_sz = (size_t)nf * 3;
		const size_t pad = (4 - rgb_sz % 4) % 4;
		if ((size_t)offs[i] + rgb_sz + pad > size)
			return false;
	}
	*frames = nf;
	*ver = v;
	return true;
}

bool IsSmashLIGH (const u8 *data, size_t size)
{
	u32 nf, v, offs[6];
	return smashcam_ligh_ok (data, size, &nf, &v, offs);
}

enumError DecodeSmashLIGH_Text (FILE *out, const u8 *data, size_t size)
{
	u32 nf, ver, offs[6];
	if (!out || !smashcam_ligh_ok (data, size, &nf, &ver, offs))
		return ERR_INVALID_DATA;

	static const char *const rgb_names[5] =
	{
		"fighter_fresnel_sky", "fighter_fresnel_ground",
		"fighter_ambient_sky", "fighter_ambient_ground", "reflection"
	};
	fprintf (out, "#LIGH\n# Super Smash Bros. 4 stage lighting animation\n\n"
		"version = %u\nframes = %u\nframe_duration = %u\n\n",
		ver, nf, ver == 5 ? rd_be32 (data + 12) : 1);
	for (int i = 0; i < 5; i++)
		fprintf (out, "rgb_%s = %s\n", rgb_names[i], offs[i + 1] ? "present" : "disabled");
	fprintf (out, "\n[frames]\n# per frame: 17 light sets x 4 lights (enabled + angles + hsv) + fog + effect\n");
	for (u32 f = 0; f < nf; f++)
	{
		fprintf (out, "frame %u\n", f);
		size_t p = (size_t)offs[0] + (size_t)f * (17 * (4 * 28 + 4) + 16);
		for (int s = 0; s < 17; s++)
		{
			fprintf (out, "  set%d:\n", s);
			for (int l = 0; l < 4; l++)
			{
				const u8 *lp = data + p;
				p += 28;
				fprintf (out, "    light%d enabled=%d angle=%.4g %.4g %.4g hsv=%.4g %.4g %.4g\n",
					l, (s32)rd_be32 (lp),
					smashcam_f32be (lp + 4), smashcam_f32be (lp + 8),
					smashcam_f32be (lp + 12), smashcam_f32be (lp + 16),
					smashcam_f32be (lp + 20), smashcam_f32be (lp + 24));
			}
			fprintf (out, "    fog unk=%u rgb=%u %u %u\n",
				data[p], data[p + 1], data[p + 2], data[p + 3]);
			p += 4;
		}
		fprintf (out, "  effect unk=%u rgb=%u %u %u pos=%.4g %.4g %.4g\n",
			data[p], data[p + 1], data[p + 2], data[p + 3],
			smashcam_f32be (data + p + 4), smashcam_f32be (data + p + 8),
			smashcam_f32be (data + p + 12));
		for (int i = 0; i < 5; i++)
		{
			if (!offs[i + 1])
				continue;
			const u8 *rgb = data + offs[i + 1] + (size_t)f * 3;
			fprintf (out, "  rgb_%s = %u %u %u\n", rgb_names[i], rgb[0], rgb[1], rgb[2]);
		}
	}
	return ERR_OK;
}
