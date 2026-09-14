// SPDX-License-Identifier: GPL-2.0+
// Small shared helpers for the NW4R/NW4F sound-archive builders (BRSAR and
// SDAT), split out of the old combined lib-brsar.c. Header-only (static
// inline) so no extra translation unit is needed.
#ifndef LIB_SOUND_ARCHIVE_UTIL_H
#define LIB_SOUND_ARCHIVE_UTIL_H 1

#include "lib-std.h"
#include <assert.h>
#include <string.h>

// -----------------------------------------------------------------------------
// A tiny growable byte buffer, local to this file.

typedef struct membuf_t
{
	u8 *data;
	size_t size;
	size_t capacity;
} membuf_t;

static inline void mb_init (membuf_t *mb)
{
	mb->data = 0;
	mb->size = 0;
	mb->capacity = 0;
}

static inline void mb_reserve (membuf_t *mb, size_t need)
{
	if (mb->size + need <= mb->capacity)
		return;
	size_t new_cap = mb->capacity ? mb->capacity * 2 : 0x1000;
	while (new_cap < mb->size + need)
		new_cap *= 2;
	mb->data = REALLOC (mb->data, new_cap);
	mb->capacity = new_cap;
}

static inline size_t mb_append (membuf_t *mb, const void *src, size_t len)
{
	mb_reserve (mb, len);
	size_t offs = mb->size;
	if (len)
		memcpy (mb->data + offs, src, len);
	mb->size += len;
	return offs;
}

static inline size_t mb_append_u32 (membuf_t *mb, u32 val)
{
	u8 be[4] = { (u8)(val >> 24), (u8)(val >> 16), (u8)(val >> 8), (u8)val };
	return mb_append (mb, be, 4);
}

static inline size_t mb_append_u16 (membuf_t *mb, u16 val)
{
	u8 be[2] = { (u8)(val >> 8), (u8)val };
	return mb_append (mb, be, 2);
}

static inline void mb_put_u32 (membuf_t *mb, size_t offs, u32 val)
{
	assert (offs + 4 <= mb->size);
	mb->data[offs + 0] = (u8)(val >> 24);
	mb->data[offs + 1] = (u8)(val >> 16);
	mb->data[offs + 2] = (u8)(val >> 8);
	mb->data[offs + 3] = (u8)val;
}

static inline void mb_put_u16 (membuf_t *mb, size_t offs, u16 val)
{
	assert (offs + 2 <= mb->size);
	mb->data[offs + 0] = (u8)(val >> 8);
	mb->data[offs + 1] = (u8)val;
}

static inline size_t mb_append_u16e (membuf_t *mb, u16 val, bool le)
{
	u8 b[2];
	if (le)
	{
		b[0] = (u8)val;
		b[1] = (u8)(val >> 8);
	}
	else
	{
		b[0] = (u8)(val >> 8);
		b[1] = (u8)val;
	}
	return mb_append (mb, b, 2);
}

static inline size_t mb_append_u32e (membuf_t *mb, u32 val, bool le)
{
	u8 b[4];
	if (le)
	{
		b[0] = (u8)val;
		b[1] = (u8)(val >> 8);
		b[2] = (u8)(val >> 16);
		b[3] = (u8)(val >> 24);
	}
	else
	{
		b[0] = (u8)(val >> 24);
		b[1] = (u8)(val >> 16);
		b[2] = (u8)(val >> 8);
		b[3] = (u8)val;
	}
	return mb_append (mb, b, 4);
}

static inline void mb_put_u32e (membuf_t *mb, size_t offs, u32 val, bool le)
{
	assert (offs + 4 <= mb->size);
	if (le)
	{
		mb->data[offs] = (u8)val;
		mb->data[offs + 1] = (u8)(val >> 8);
		mb->data[offs + 2] = (u8)(val >> 16);
		mb->data[offs + 3] = (u8)(val >> 24);
	}
	else
	{
		mb->data[offs] = (u8)(val >> 24);
		mb->data[offs + 1] = (u8)(val >> 16);
		mb->data[offs + 2] = (u8)(val >> 8);
		mb->data[offs + 3] = (u8)val;
	}
}

static inline void mb_align (membuf_t *mb, size_t align)
{
	size_t pad = (align - (mb->size % align)) % align;
	if (pad)
	{
		u8 zero[32] = { 0 };
		while (pad)
		{
			size_t n = pad < sizeof (zero) ? pad : sizeof (zero);
			mb_append (mb, zero, n);
			pad -= n;
		}
	}
}

static inline void mb_free (membuf_t *mb)
{
	if (mb->data)
		FREE (mb->data);
	mb->data = 0;
	mb->size = mb->capacity = 0;
}

static inline u32 rd_u32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline u16 rd_u16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}
static inline u32 rd_u32e (const u8 *p, bool le)
{
	return le ? ((u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]) : rd_u32 (p);
}
static inline u16 rd_u16e (const u8 *p, bool le)
{
	return le ? (u16)((u16)p[1] << 8 | p[0]) : rd_u16 (p);
}

static inline void mb_put_u16e (membuf_t *mb, size_t offs, u16 val, bool le)
{
	assert (offs + 2 <= mb->size);
	if (le)
	{
		mb->data[offs] = val;
		mb->data[offs + 1] = val >> 8;
	}
	else
	{
		mb->data[offs] = val >> 8;
		mb->data[offs + 1] = val;
	}
}

// -----------------------------------------------------------------------------

static inline bool has_suffix (ccp name, ccp suffix)
{
	size_t nl = strlen (name), sl = strlen (suffix);
	return nl >= sl && !strcasecmp (name + nl - sl, suffix);
}

// Strips a trailing extension, and additionally strips a second, "compiled
// container" extension behind it (e.g. "foo.rseq.brseq" -> "foo") so the
// bare asset name survives round-tripping through either sound archive's
// own extension convention (RSAR's brseq/brbnk/brwar/brwsd, CSAR/FSAR's
// rseq/rbnk/rwar/rwsd, or SDAT's sseq/sbnk/swar/fseq/fbnk/fwar/cseq/cbnk/
// cwar). Caller FREEs the result.
static inline char *strip_ext_dup (ccp name)
{
	ccp dot = strrchr (name, '.');
	size_t len = dot ? (size_t)(dot - name) : strlen (name);
	char *out = MALLOC (len + 1);
	memcpy (out, name, len);
	out[len] = 0;

	ccp dot2 = strrchr (out, '.');
	if (dot2
		&& (!strcasecmp (dot2, ".rseq") || !strcasecmp (dot2, ".rbnk")
			|| !strcasecmp (dot2, ".rwar") || !strcasecmp (dot2, ".rwsd")
			|| !strcasecmp (dot2, ".brseq") || !strcasecmp (dot2, ".brbnk")
			|| !strcasecmp (dot2, ".brwar") || !strcasecmp (dot2, ".brwsd")
			|| !strcasecmp (dot2, ".sseq") || !strcasecmp (dot2, ".sbnk")
			|| !strcasecmp (dot2, ".swar") || !strcasecmp (dot2, ".fseq")
			|| !strcasecmp (dot2, ".fbnk") || !strcasecmp (dot2, ".fwar")
			|| !strcasecmp (dot2, ".cseq") || !strcasecmp (dot2, ".cbnk")
			|| !strcasecmp (dot2, ".cwar")))
	{
		size_t len2 = (size_t)(dot2 - out);
		out[len2] = 0;
	}
	return out;
}

#endif // LIB_SOUND_ARCHIVE_UTIL_H
