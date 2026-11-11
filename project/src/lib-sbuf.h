// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_SBUF_H
#define SZS_LIB_SBUF_H 1

// A small growable text/byte buffer for converters that build their whole output in memory
// (YAML/JSON dumps, encoded images). Header-only; every function is static.

#include "lib-std.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct sbuf_t
{
	char *d;
	size_t n, cap;
	bool fail;
} sbuf_t;

static inline bool sb_reserve (sbuf_t *b, size_t more)
{
	if (b->fail)
		return false;
	if (b->n + more + 1 <= b->cap)
		return true;
	size_t cap = b->cap ? b->cap : 1 << 14;
	while (cap < b->n + more + 1)
		cap *= 2;
	char *nd = REALLOC (b->d, cap);
	if (!nd)
	{
		b->fail = true;
		return false;
	}
	b->d = nd;
	b->cap = cap;
	return true;
}

static inline void sb_write (sbuf_t *b, const void *data, size_t len)
{
	if (!len || !sb_reserve (b, len))
		return;
	memcpy (b->d + b->n, data, len);
	b->n += len;
	b->d[b->n] = 0;
}

static inline void sb_puts (sbuf_t *b, const char *s)
{
	sb_write (b, s, strlen (s));
}

static void sb_printf (sbuf_t *b, const char *fmt, ...) __attribute__ ((format (printf, 2, 3), unused));
static void sb_printf (sbuf_t *b, const char *fmt, ...)
{
	if (b->fail)
		return;
	va_list ap;
	va_start (ap, fmt);
	const int len = vsnprintf (0, 0, fmt, ap);
	va_end (ap);
	if (len < 0 || !sb_reserve (b, (size_t)len))
	{
		b->fail = true;
		return;
	}
	va_start (ap, fmt);
	vsnprintf (b->d + b->n, (size_t)len + 1, fmt, ap);
	va_end (ap);
	b->n += (size_t)len;
}

// Appends S as a double-quoted JSON/YAML string (control characters escaped, UTF-8 kept).
static inline void sb_quote (sbuf_t *b, const char *s, size_t len)
{
	sb_write (b, "\"", 1);
	for (size_t i = 0; i < len; i++)
	{
		const u8 c = (u8)s[i];
		if (c == '"' || c == '\\')
		{
			const char e[2] = { '\\', (char)c };
			sb_write (b, e, 2);
		}
		else if (c == '\n')
			sb_write (b, "\\n", 2);
		else if (c == '\t')
			sb_write (b, "\\t", 2);
		else if (c == '\r')
			sb_write (b, "\\r", 2);
		else if (c < 0x20)
			sb_printf (b, "\\u%04x", c);
		else
			sb_write (b, &s[i], 1);
	}
	sb_write (b, "\"", 1);
}

static inline void sb_quotez (sbuf_t *b, const char *s)
{
	sb_quote (b, s ? s : "", s ? strlen (s) : 0);
}

// Hands the buffer to the caller (*out, *len) or frees it on failure; returns ERR_OK or ERR_OUT_OF_MEMORY.
static inline enumError sb_finish (sbuf_t *b, char **out, size_t *len)
{
	if (b->fail || !b->d)
	{
		FREE (b->d);
		*out = 0;
		if (len)
			*len = 0;
		return ERR_OUT_OF_MEMORY;
	}
	*out = b->d;
	if (len)
		*len = b->n;
	return ERR_OK;
}

#endif // SZS_LIB_SBUF_H
