/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 ***************************************************************************/

#include <math.h>
#include <string.h>
#include "lib-brres-anim.h"

///////////////////////////////////////////////////////////////////////////////
///////////////			byte order helper		///////////////
///////////////////////////////////////////////////////////////////////////////
// BRRES animation data is always big endian (Wii).

static inline u16 banim_rd16 (const u8 *p) { return (u16)p[0] << 8 | p[1]; }

static inline u32 banim_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline void banim_w16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}

static inline void banim_w32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}

static inline float banim_rdf (const u8 *p)
{
	const u32 raw = banim_rd32 (p);
	float f;
	memcpy (&f, &raw, 4);
	return f;
}

static inline void banim_wf (u8 *p, float f)
{
	u32 raw;
	memcpy (&raw, &f, 4);
	banim_w32 (p, raw);
}

// round to nearest, halves away from zero (matches BrawlLib's +/-0.5 idiom)
static inline int banim_round (float v)
{
	return (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

static inline int banim_clamp (int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			format names			///////////////
///////////////////////////////////////////////////////////////////////////////

ccp GetFormatNameBANIM (banim_format_t fmt)
{
	switch (fmt)
	{
		case BANIM_NONE: return "NONE";
		case BANIM_I4: return "I4";
		case BANIM_I6: return "I6";
		case BANIM_I12: return "I12";
		case BANIM_L1: return "L1";
		case BANIM_L2: return "L2";
		case BANIM_L4: return "L4";
	}
	return "?";
}

///////////////////////////////////////////////////////////////////////////////

banim_format_t ScanFormatNameBANIM (ccp name)
{
	if (!name)
		return BANIM_NONE;
	if (!strcmp (name, "I4"))
		return BANIM_I4;
	if (!strcmp (name, "I6"))
		return BANIM_I6;
	if (!strcmp (name, "I12"))
		return BANIM_I12;
	if (!strcmp (name, "L1"))
		return BANIM_L1;
	if (!strcmp (name, "L2"))
		return BANIM_L2;
	if (!strcmp (name, "L4"))
		return BANIM_L4;
	return BANIM_NONE;
}

///////////////////////////////////////////////////////////////////////////////

bool IsIndexedBANIM (banim_format_t fmt)
{
	return fmt == BANIM_I4 || fmt == BANIM_I6 || fmt == BANIM_I12;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			track management		///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeTrackBANIM (banim_track_t *tr)
{
	DASSERT (tr);
	memset (tr, 0, sizeof (*tr));
	tr->format = BANIM_I12;
}

///////////////////////////////////////////////////////////////////////////////

void ResetTrackBANIM (banim_track_t *tr)
{
	if (!tr)
		return;
	FREE (tr->key);
	InitializeTrackBANIM (tr);
}

///////////////////////////////////////////////////////////////////////////////

banim_key_t *AppendKeyBANIM (banim_track_t *tr, float frame, float value, float tangent)
{
	DASSERT (tr);
	if (tr->n_key == tr->n_key_alloced)
	{
		tr->n_key_alloced = tr->n_key_alloced ? tr->n_key_alloced * 2 : 16;
		tr->key = REALLOC (tr->key, tr->n_key_alloced * sizeof (*tr->key));
	}

	banim_key_t *k = tr->key + tr->n_key++;
	k->frame = frame;
	k->value = value;
	k->tangent = tangent;
	return k;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			DecodeTrackBANIM		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError DecodeTrackBANIM (banim_track_t *tr, const u8 *data, uint avail,
	banim_format_t format, uint frame_limit)
{
	return DecodeTrackBANIM_Ext (tr, data, avail, format, frame_limit, false);
}

enumError DecodeTrackBANIM_Ext (banim_track_t *tr, const u8 *data, uint avail,
	banim_format_t format, uint frame_limit, bool short_header)
{
	DASSERT (tr);
	DASSERT (data);

	InitializeTrackBANIM (tr);
	tr->format = format;

	switch (format)
	{
		//--- indexed, quantized: 16 byte header, 4 byte entries
		case BANIM_I4:
		{
			if (avail < 16)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: truncated I4 track header\n");
			const uint n = banim_rd16 (data);
			tr->unknown = banim_rd16 (data + 2);
			tr->frame_scale = banim_rdf (data + 4);
			tr->step = banim_rdf (data + 8);
			tr->base = banim_rdf (data + 12);

			if ((u64)16 + (u64)n * 4 > avail)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: I4 track exceeds file size\n");

			for (uint i = 0; i < n; i++)
			{
				const u32 v = banim_rd32 (data + 16 + i * 4);
				const uint index = v >> 24;
				const uint step = v >> 12 & 0xfff;
				// sign extend the low 12 bits
				const int tan = (int)(v << 20) >> 20;
				AppendKeyBANIM (tr, (float)index, tr->base + step * tr->step,
					tan / 32.0f);
			}
			break;
		}

		//--- indexed, quantized: 16 byte header (or 8 byte for v3), 6 byte entries
		case BANIM_I6:
		{
			const uint hdr_sz = short_header ? 8 : 16;
			if (avail < hdr_sz)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: truncated I6 track header\n");
			const uint n = banim_rd16 (data);
			tr->unknown = banim_rd16 (data + 2);
			tr->frame_scale = banim_rdf (data + 4);
			if (short_header)
			{
				tr->step = 1.0f / 256.0f;
				tr->base = 0.0f;
			}
			else
			{
				tr->step = banim_rdf (data + 8);
				tr->base = banim_rdf (data + 12);
			}

			if ((u64)hdr_sz + (u64)n * 6 > avail)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: I6 track exceeds file size\n");

			for (uint i = 0; i < n; i++)
			{
				const u8 *e = data + hdr_sz + i * 6;
				const uint index = banim_rd16 (e) >> 5;
				const uint step = banim_rd16 (e + 2);
				const int tan = (s16)banim_rd16 (e + 4);
				AppendKeyBANIM (tr, (float)index, tr->base + step * tr->step,
					tan / 256.0f);
			}
			break;
		}

		//--- indexed, raw floats: 8 byte header, 12 byte entries
		case BANIM_I12:
		{
			if (avail < 8)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: truncated I12 track header\n");
			const uint n = banim_rd16 (data);
			tr->unknown = banim_rd16 (data + 2);
			tr->frame_scale = banim_rdf (data + 4);

			if ((u64)8 + (u64)n * 12 > avail)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: I12 track exceeds file size\n");

			for (uint i = 0; i < n; i++)
			{
				const u8 *e = data + 8 + i * 12;
				AppendKeyBANIM (tr, banim_rdf (e), banim_rdf (e + 4), banim_rdf (e + 8));
			}
			break;
		}

		//--- linear, quantized: 8 byte header, one u8/u16 per frame
		case BANIM_L1:
		case BANIM_L2:
		{
			if (avail < 8)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: truncated %s track header\n",
					GetFormatNameBANIM (format));
			tr->step = banim_rdf (data);
			tr->base = banim_rdf (data + 4);

			const uint width = format == BANIM_L1 ? 1 : 2;
			if ((u64)8 + (u64)frame_limit * width > avail)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: %s track exceeds file size\n",
					GetFormatNameBANIM (format));

			for (uint i = 0; i < frame_limit; i++)
			{
				const uint raw = width == 1 ? data[8 + i] : banim_rd16 (data + 8 + i * 2);
				// linear formats carry no tangent, it is implied by the
				// neighbouring frames; store 0.0 and let consumers derive it
				AppendKeyBANIM (tr, (float)i, tr->base + raw * tr->step, 0.0f);
			}
			break;
		}

		//--- linear, raw floats: no header, one float per frame
		case BANIM_L4:
		{
			if ((u64)frame_limit * 4 > avail)
				return ERROR0 (ERR_INVALID_DATA, "BRRES anim: L4 track exceeds file size\n");
			for (uint i = 0; i < frame_limit; i++)
				AppendKeyBANIM (tr, (float)i, banim_rdf (data + i * 4), 0.0f);
			break;
		}

		default:
			return ERROR0 (ERR_INVALID_DATA, "BRRES anim: invalid track format %u\n",
				(uint)format);
	}

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			EncodeTrackBANIM		///////////////
///////////////////////////////////////////////////////////////////////////////

uint GetEncodedSizeBANIM (const banim_track_t *tr, uint frame_limit)
{
	DASSERT (tr);
	switch (tr->format)
	{
		case BANIM_I4: return 16 + tr->n_key * 4;
		case BANIM_I6: return 16 + tr->n_key * 6;
		case BANIM_I12: return 8 + tr->n_key * 12;
		case BANIM_L1: return 8 + frame_limit;
		case BANIM_L2: return 8 + frame_limit * 2;
		case BANIM_L4: return frame_limit * 4;
		default: return 0;
	}
}

///////////////////////////////////////////////////////////////////////////////

// Invert 'value = base + raw * step'. When base/step were preserved from the
// source this reproduces the original integer exactly.
static uint banim_quantize (float value, float base, float step)
{
	if (step == 0.0f || !isfinite (step))
		return 0;
	const float raw = (value - base) / step;
	if (!isfinite (raw))
		return 0;
	return (uint)banim_clamp (banim_round (raw), 0, 0xffff);
}

///////////////////////////////////////////////////////////////////////////////

uint EncodeTrackBANIM (const banim_track_t *tr, u8 *dest, uint frame_limit)
{
	DASSERT (tr);
	DASSERT (dest);

	switch (tr->format)
	{
		case BANIM_I4:
		{
			banim_w16 (dest, (u16)tr->n_key);
			banim_w16 (dest + 2, tr->unknown);
			banim_wf (dest + 4, tr->frame_scale);
			banim_wf (dest + 8, tr->step);
			banim_wf (dest + 12, tr->base);

			for (uint i = 0; i < tr->n_key; i++)
			{
				const banim_key_t *k = tr->key + i;
				const uint index = (uint)banim_clamp (banim_round (k->frame), 0, 0xff);
				const uint step = banim_quantize (k->value, tr->base, tr->step) & 0xfff;
				const int tan = banim_clamp (banim_round (k->tangent * 32.0f), -2048, 2047);
				banim_w32 (dest + 16 + i * 4,
					index << 24 | step << 12 | ((u32)tan & 0xfff));
			}
			return 16 + tr->n_key * 4;
		}

		case BANIM_I6:
		{
			banim_w16 (dest, (u16)tr->n_key);
			banim_w16 (dest + 2, tr->unknown);
			banim_wf (dest + 4, tr->frame_scale);
			banim_wf (dest + 8, tr->step);
			banim_wf (dest + 12, tr->base);

			for (uint i = 0; i < tr->n_key; i++)
			{
				const banim_key_t *k = tr->key + i;
				u8 *e = dest + 16 + i * 6;
				const uint index = (uint)banim_clamp (banim_round (k->frame), 0, 0x7ff);
				banim_w16 (e, (u16)(index << 5));
				banim_w16 (e + 2, (u16)banim_quantize (k->value, tr->base, tr->step));
				banim_w16 (e + 4,
					(u16)(s16)banim_clamp (banim_round (k->tangent * 256.0f), -32768, 32767));
			}
			return 16 + tr->n_key * 6;
		}

		case BANIM_I12:
		{
			banim_w16 (dest, (u16)tr->n_key);
			banim_w16 (dest + 2, tr->unknown);
			banim_wf (dest + 4, tr->frame_scale);

			for (uint i = 0; i < tr->n_key; i++)
			{
				const banim_key_t *k = tr->key + i;
				u8 *e = dest + 8 + i * 12;
				banim_wf (e, k->frame);
				banim_wf (e + 4, k->value);
				banim_wf (e + 8, k->tangent);
			}
			return 8 + tr->n_key * 12;
		}

		case BANIM_L1:
		case BANIM_L2:
		{
			banim_wf (dest, tr->step);
			banim_wf (dest + 4, tr->base);

			const uint width = tr->format == BANIM_L1 ? 1 : 2;
			for (uint i = 0; i < frame_limit; i++)
			{
				// linear tracks store exactly one value per frame; a short
				// track repeats its last value rather than writing garbage
				const banim_key_t *k = tr->n_key
					? tr->key + (i < tr->n_key ? i : tr->n_key - 1)
					: 0;
				const uint raw = k ? banim_quantize (k->value, tr->base, tr->step) : 0;
				if (width == 1)
					dest[8 + i] = (u8)(raw > 0xff ? 0xff : raw);
				else
					banim_w16 (dest + 8 + i * 2, (u16)raw);
			}
			return 8 + frame_limit * width;
		}

		case BANIM_L4:
		{
			for (uint i = 0; i < frame_limit; i++)
			{
				const banim_key_t *k = tr->n_key
					? tr->key + (i < tr->n_key ? i : tr->n_key - 1)
					: 0;
				banim_wf (dest + i * 4, k ? k->value : 0.0f);
			}
			return frame_limit * 4;
		}

		default:
			return 0;
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		   BRRES trailing string pool		///////////////
///////////////////////////////////////////////////////////////////////////////

uint CalcPoolBANIM (ccp *names, uint n_names, uint pool_start, uint *char_off)
{
	DASSERT (names);

	uint pos = pool_start;
	for (uint i = 0; i < n_names; i++)
	{
		if (!names[i] || !*names[i])
		{
			if (char_off)
				char_off[i] = 0;
			continue;
		}
		if (char_off)
			char_off[i] = pos + 4;
		pos += 4 + strlen (names[i]) + 1;
		pos = pos + 3 & ~3u;
	}
	return pos - pool_start;
}

///////////////////////////////////////////////////////////////////////////////

void WritePoolBANIM (u8 *file_base, ccp *names, uint n_names, uint pool_start)
{
	DASSERT (file_base);
	DASSERT (names);

	uint pos = pool_start;
	for (uint i = 0; i < n_names; i++)
	{
		if (!names[i] || !*names[i])
			continue;
		const uint len = strlen (names[i]);
		banim_w32 (file_base + pos, len);
		memcpy (file_base + pos + 4, names[i], len + 1);
		pos += 4 + len + 1;
		pos = pos + 3 & ~3u;
	}
}

///////////////////////////////////////////////////////////////////////////////

// Retail lays the trailing string pool out in ordinal (strcmp) name order
// rather than in the logical slot order of the sub-file. These two helpers
// wrap CalcPoolBANIM()/WritePoolBANIM() with that permutation so a re-encode
// reproduces retail byte for byte; CalcPoolSortedBANIM() still returns the
// offsets scattered back onto the caller's *logical* slots.

static uint *OrderPoolBANIM (ccp *names, uint n_names)
{
	uint *ord = CALLOC (n_names, sizeof (uint));
	for (uint i = 0; i < n_names; i++)
		ord[i] = i;

	for (uint i = 1; i < n_names; i++)
	{
		const uint cur = ord[i];
		int j = i - 1;
		while (j >= 0 && strcmp (names[ord[j]], names[cur]) > 0)
		{
			ord[j + 1] = ord[j];
			j--;
		}
		ord[j + 1] = cur;
	}
	return ord;
}

///////////////////////////////////////////////////////////////////////////////

uint CalcPoolSortedBANIM (ccp *names, uint n_names, uint pool_start, uint *char_off)
{
	DASSERT (names);

	uint *ord = OrderPoolBANIM (names, n_names);
	ccp *sorted = CALLOC (n_names, sizeof (ccp));
	uint *sorted_off = CALLOC (n_names, sizeof (uint));
	for (uint i = 0; i < n_names; i++)
		sorted[i] = names[ord[i]];

	const uint pool_size = CalcPoolBANIM (sorted, n_names, pool_start, sorted_off);
	if (char_off)
		for (uint i = 0; i < n_names; i++)
			char_off[ord[i]] = sorted_off[i];

	FREE (sorted);
	FREE (sorted_off);
	FREE (ord);
	return pool_size;
}

///////////////////////////////////////////////////////////////////////////////

void WritePoolSortedBANIM (u8 *file_base, ccp *names, uint n_names, uint pool_start)
{
	DASSERT (file_base);
	DASSERT (names);

	uint *ord = OrderPoolBANIM (names, n_names);
	ccp *sorted = CALLOC (n_names, sizeof (ccp));
	for (uint i = 0; i < n_names; i++)
		sorted[i] = names[ord[i]];

	WritePoolBANIM (file_base, sorted, n_names, pool_start);

	FREE (sorted);
	FREE (ord);
}

