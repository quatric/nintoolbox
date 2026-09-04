// Native QuickBMS script interpreter -- shared implementation used by both
// `wszst BMS` and the wbmsx tool.
//
// This covers the opcodes real-world "open archive, walk a table, extract
// entries" scripts actually use: IDSTRING, GET/GETDSTRING/GETCT, PUT/
// PUTDSTRING/PUTCT, GOTO, SAVEPOS, MATH/XMATH (full operator-precedence
// expressions with parens), SET, STRING/STRLEN, GETVARCHR/PUTVARCHR,
// REVERSESHORT/LONG/LONGLONG, GETBITS/PUTBITS, PADDING, FINDLOC, APPEND,
// OPEN (multiple file handles, including MEMORY_FILE), FOR/NEXT,
// WHILE/ENDWHILE, IF/ELSE/ENDIF (with AND/OR), LOG, CLOG, COMTYPE, ENDIAN,
// PRINT. Reference: the real QuickBMS opcode set in aluigi/quickbms's
// cmd.c (CMD_*_func), used only to confirm names/argument order -- this is
// a from-scratch interpreter, not a port of that GPL source.
//
// Not implemented: CallDLL/CALLFUNCTION (would need a full expression VM
// plus dynamic library loading), the ~100-algorithm crypto suite behind
// ENCRYPTION (registered as a no-op passthrough), CRC/hashing opcodes,
// array opcodes (GetArray/PutArray/SortArray/SearchArray), and most of
// the ~200 ComType-specific compression plugins beyond what this fork's
// own native decoders already cover. This is not a full QuickBMS clone.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>
#include "lib-std.h"
#include "lib-szs.h"
#include "lib-nintendo.h"
#include "lib-quicklz.h"
#include "lib-bms.h"

#define MAX_VARS 1024
#define MAX_LINES 8192
#define MAX_TOK 16
#define MAX_FILES 16

typedef struct var_t
{
	char name[64];
	int64_t val;
	char sval[512];
	bool is_str;
} var_t;

// One open file/memory handle. Handle 0 is always the script's main input
// file, pre-opened before the script runs (matching how these Nintendo
// extraction scripts are invoked -- there's no separate "select input
// file" step).
typedef struct
{
	bool used;
	uint8_t *data;
	size_t size, cap; // cap only meaningful for owned (memory-file) buffers
	size_t pos;
	bool owned; // true if 'data' was malloc'd here (memory file) and must be freed
} bfile_t;

typedef struct
{
	var_t vars[MAX_VARS];
	int n_vars;

	bfile_t files[MAX_FILES];

	bool big_endian;
	char comtype[32];
	bool append;

	char *outdir;
	char *infile_name;
} bms_ctx_t;

typedef struct
{
	char *tok[MAX_TOK];
	int n;
} line_t;

static line_t lines[MAX_LINES];
static int n_lines;

static var_t *find_var (bms_ctx_t *ctx, const char *name, bool create)
{
	for (int i = 0; i < ctx->n_vars; i++)
		if (!strcmp (ctx->vars[i].name, name))
			return &ctx->vars[i];
	if (!create || ctx->n_vars >= MAX_VARS)
		return NULL;
	var_t *v = &ctx->vars[ctx->n_vars++];
	memset (v, 0, sizeof (*v));
	strncpy (v->name, name, sizeof (v->name) - 1);
	return v;
}

static bfile_t *get_file (bms_ctx_t *ctx, int handle)
{
	if (handle < 0 || handle >= MAX_FILES || !ctx->files[handle].used)
		return &ctx->files[0]; // best-effort: fall back to main input
	return &ctx->files[handle];
}

// ------------------------------------------------------------------------
// Expression evaluation: real operator-precedence parser (not just "A op
// B"), since MATH/XMATH/SET/IF/FOR bounds in real scripts commonly chain
// multiple operators and use parentheses, e.g. "(NUM_FILES - 1) * 0x10".

static bms_ctx_t *g_ctx; // parser reads tokens only; needs ctx for var lookups
static char **g_ptoks;
static int g_pn, g_pi;

static int64_t val_of (bms_ctx_t *ctx, const char *tok)
{
	if (!tok)
		return 0;
	if (tok[0] == '"')
		return 0; // string literal in numeric context: not meaningful, 0
	char *end;
	long long n = strtoll (tok, &end, 0);
	if (end != tok && *end == 0)
		return n;
	var_t *v = find_var (ctx, tok, false);
	return v ? v->val : 0;
}

static const char *ptok (void)
{
	return g_pi < g_pn ? g_ptoks[g_pi] : NULL;
}
static void padv (void)
{
	g_pi++;
}

static int64_t parse_expr (void); // fwd
static int expand_op_tokens (char **in, int n, char **out, int max_out); // fwd

static int64_t parse_primary (void)
{
	const char *t = ptok ();
	if (!t)
		return 0;
	if (!strcmp (t, "("))
	{
		padv ();
		int64_t v = parse_expr ();
		if (ptok () && !strcmp (ptok (), ")"))
			padv ();
		return v;
	}
	if (!strcmp (t, "-"))
	{
		padv ();
		return -parse_primary ();
	}
	if (!strcmp (t, "!"))
	{
		padv ();
		return !parse_primary ();
	}
	padv ();
	return val_of (g_ctx, t);
}

// QuickBMS scripts are tokenized on whitespace before we ever see them
// here, so "A+B" is one token unless the script itself has spaces; handle
// both by re-splitting any single token that contains operator characters
// isn't attempted -- instead this parser expects operators as separate
// tokens (true for well-formed BMS scripts using "VAR op VAR" spacing,
// which is what quickbms.txt documents and what real scripts use).
static int op_prec (const char *o)
{
	if (!strcmp (o, "*") || !strcmp (o, "/") || !strcmp (o, "%"))
		return 3;
	if (!strcmp (o, "+") || !strcmp (o, "-"))
		return 2;
	if (!strcmp (o, "<<") || !strcmp (o, ">>") || !strcmp (o, "&") || !strcmp (o, "|")
		|| !strcmp (o, "^"))
		return 1;
	return -1;
}

static int64_t apply_op (const char *o, int64_t a, int64_t b)
{
	if (!strcmp (o, "+"))
		return a + b;
	if (!strcmp (o, "-"))
		return a - b;
	if (!strcmp (o, "*"))
		return a * b;
	if (!strcmp (o, "/"))
		return b ? a / b : 0;
	if (!strcmp (o, "%"))
		return b ? a % b : 0;
	if (!strcmp (o, "&"))
		return a & b;
	if (!strcmp (o, "|"))
		return a | b;
	if (!strcmp (o, "^"))
		return a ^ b;
	if (!strcmp (o, "<<"))
		return a << b;
	if (!strcmp (o, ">>"))
		return a >> b;
	return b;
}

static int64_t parse_bin (int min_prec)
{
	int64_t lhs = parse_primary ();
	for (;;)
	{
		const char *o = ptok ();
		if (!o || !strcmp (o, ")"))
			break;
		int prec = op_prec (o);
		if (prec < min_prec)
			break;
		padv ();
		int64_t rhs = parse_bin (prec + 1);
		lhs = apply_op (o, lhs, rhs);
	}
	return lhs;
}

static int64_t parse_expr (void)
{
	return parse_bin (0);
}

// Evaluates tokens [idx, ln->n) of 'ln' as one expression. Returns the
// value; *consumed (if given) is set to how many tokens were used, so
// callers with trailing operands (e.g. an optional handle number after a
// GOTO offset expression) can still find them -- though in practice BMS
// expressions in these scripts run to end-of-line, so most callers just
// pass consumed=NULL and take the whole remainder.
static int64_t eval_tokens (bms_ctx_t *ctx, char **toks, int n)
{
	if (n <= 0)
		return 0;
	char *expanded[MAX_TOK * 2];
	int en = expand_op_tokens (toks, n, expanded, (int)(sizeof (expanded) / sizeof (*expanded)));
	g_ctx = ctx;
	g_ptoks = expanded;
	g_pn = en;
	g_pi = 0;
	return parse_expr ();
}

static void set_var (bms_ctx_t *ctx, const char *name, int64_t value)
{
	var_t *v = find_var (ctx, name, true);
	v->val = value;
	v->is_str = false;
}

static void set_str_var (bms_ctx_t *ctx, const char *name, const char *s)
{
	var_t *v = find_var (ctx, name, true);
	strncpy (v->sval, s, sizeof (v->sval) - 1);
	v->sval[sizeof (v->sval) - 1] = 0;
	v->is_str = true;
}

static const char *str_of (bms_ctx_t *ctx, const char *tok)
{
	if (!tok)
		return "";
	if (tok[0] == '"')
	{
		size_t l = strlen (tok);
		static char buf[512];
		size_t cl = l >= 2 ? l - 2 : 0;
		if (cl >= sizeof (buf))
			cl = sizeof (buf) - 1;
		memcpy (buf, tok + 1, cl);
		buf[cl] = 0;
		return buf;
	}
	var_t *v = find_var (ctx, tok, false);
	return v && v->is_str ? v->sval : "";
}

static uint64_t read_le (const uint8_t *p, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; i++)
		v |= (uint64_t)p[i] << (8 * i);
	return v;
}

static uint64_t read_be (const uint8_t *p, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; i++)
		v = (v << 8) | p[i];
	return v;
}

static void write_le (uint8_t *p, int n, uint64_t v)
{
	for (int i = 0; i < n; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

static void write_be (uint8_t *p, int n, uint64_t v)
{
	for (int i = 0; i < n; i++)
		p[n - 1 - i] = (uint8_t)(v >> (8 * i));
}

static int type_size (const char *type)
{
	if (!strcasecmp (type, "byte") || !strcasecmp (type, "char") || !strcasecmp (type, "uint8"))
		return 1;
	if (!strcasecmp (type, "short") || !strcasecmp (type, "uint16"))
		return 2;
	if (!strcasecmp (type, "threebyte"))
		return 3;
	if (!strcasecmp (type, "long") || !strcasecmp (type, "uint32"))
		return 4;
	if (!strcasecmp (type, "longlong") || !strcasecmp (type, "uint64"))
		return 8;
	return 4;
}

// Ensures a memory-file handle has room for at least 'need' more bytes
// starting at its current position, growing (and zero-filling the gap) as
// needed. Read-only (non-owned) handles are left alone -- writing past
// their end is a no-op, matching "best effort" behavior elsewhere in this
// interpreter rather than crashing on a script targeting a real file.
static void ensure_cap (bfile_t *f, size_t need)
{
	if (!f->owned)
		return;
	size_t want = f->pos + need;
	if (want <= f->cap)
	{
		if (want > f->size)
			f->size = want;
		return;
	}
	size_t newcap = f->cap ? f->cap * 2 : 4096;
	while (newcap < want)
		newcap *= 2;
	f->data = REALLOC (f->data, newcap);
	memset (f->data + f->cap, 0, newcap - f->cap);
	f->cap = newcap;
	if (want > f->size)
		f->size = want;
}

// Creates every directory component of 'path' except the final one (the
// final component is assumed to be the file being written, not a dir).
static void mkdirs (const char *path)
{
	char buf[PATH_MAX];
	strncpy (buf, path, sizeof (buf) - 1);
	buf[sizeof (buf) - 1] = 0;
	for (char *p = buf + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = 0;
			mkdir (buf, 0755);
			*p = '/';
		}
	}
}

static void save_span (bms_ctx_t *ctx, const char *name, const uint8_t *file, size_t file_size,
	size_t off, size_t size)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", ctx->outdir, name);
	mkdirs (path);
	if (off > file_size)
		off = file_size;
	if (off + size > file_size)
		size = file_size - off;
	FILE *f = fopen (path, ctx->append ? "ab" : "wb");
	if (!f)
	{
		fprintf (stderr, "wbmsx: can't write %s\n", path);
		return;
	}
	fwrite (file + off, 1, size, f);
	fclose (f);
	printf ("wbmsx: extracted %s (%zu bytes @ 0x%zx)\n", path, size, off);
}

// QuickBMS's "zlib" COMTYPE: a zlib-wrapped (2-byte header + Adler32
// trailer) deflate stream, via the system zlib already linked for libpng.
// "deflate" (raw, no header) is the same call with windowBits negated --
// aliased here too since it's the same few lines. 'hint_size' is the
// CLOG-supplied uncompressed-size operand when the script provides one
// (0 if not); when it's missing or turns out too small, grow and retry
// rather than trusting it blindly, since a wrong hint would otherwise
// truncate the output instead of failing loudly.
static enumError decode_zlib_comtype (
	u8 **dest, uint *dest_size, const u8 *src, uint comp_size, uint hint_size, bool raw_deflate)
{
	uint cap = hint_size ? hint_size : comp_size * 4 + 256;
	u8 *buf = MALLOC (cap);

	for (;;)
	{
		z_stream zs;
		memset (&zs, 0, sizeof (zs));
		zs.next_in = (Bytef *)src;
		zs.avail_in = comp_size;
		zs.next_out = buf;
		zs.avail_out = cap;

		if (inflateInit2 (&zs, raw_deflate ? -15 : 15) != Z_OK)
		{
			FREE (buf);
			return ERR_ERROR;
		}
		int rc = inflate (&zs, Z_FINISH);
		uint produced = cap - zs.avail_out;
		inflateEnd (&zs);

		if (rc == Z_STREAM_END)
		{
			*dest = buf;
			*dest_size = produced;
			return ERR_OK;
		}
		if (rc == Z_BUF_ERROR && cap < comp_size * 1024u)
		{
			// Real output didn't fit -- the hint (if any) was wrong; grow
			// and decompress again from scratch (inflate can't resume into
			// a bigger buffer mid-stream without more bookkeeping than this
			// is worth for an extraction tool).
			cap *= 4;
			buf = REALLOC (buf, cap);
			continue;
		}
		FREE (buf);
		return ERR_ERROR;
	}
}

static void clog_span (bms_ctx_t *ctx, const char *name, const uint8_t *file, size_t file_size,
	size_t off, size_t comp_size, size_t uncomp_size)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", ctx->outdir, name);
	mkdirs (path);
	if (off > file_size)
		off = file_size;
	if (off + comp_size > file_size)
		comp_size = file_size - off;
	const u8 *src = file + off;

	u8 *dest = 0;
	uint dest_size = 0;
	enumError err = ERR_ERROR;
	if (!strcasecmp (ctx->comtype, "copy"))
	{
		dest_size = (uint)comp_size;
		dest = MALLOC (dest_size ? dest_size : 1);
		memcpy (dest, src, dest_size);
		err = ERR_OK;
	}
	else if (!strcasecmp (ctx->comtype, "lz10") || !strcasecmp (ctx->comtype, "lze"))
		err = DecodeLZ10LZ11 (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "lz11"))
		err = DecodeLZ10LZ11 (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "yay0"))
		err = DecodeYay0 (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "zlib"))
		err = decode_zlib_comtype (
			&dest, &dest_size, src, (uint)comp_size, (uint)uncomp_size, false);
	else if (!strcasecmp (ctx->comtype, "deflate"))
		err = decode_zlib_comtype (
			&dest, &dest_size, src, (uint)comp_size, (uint)uncomp_size, true);
	else if (!strcasecmp (ctx->comtype, "comp_unzip_dynamic")
		|| !strcasecmp (ctx->comtype, "unzip_dynamic") || !strcasecmp (ctx->comtype, "comp_zlib")
		|| !strcasecmp (ctx->comtype, "fzip"))
	{
		err = decode_zlib_comtype (
			&dest, &dest_size, src, (uint)comp_size, (uint)uncomp_size, false);
		if (err)
			err = decode_zlib_comtype (
				&dest, &dest_size, src, (uint)comp_size, (uint)uncomp_size, true);
		if (err && comp_size >= 8 && !memcmp (src, "FZIP", 4))
			err = DecodeFZIP (&dest, &dest_size, src, (uint)comp_size);
	}
	// The COMTYPEs below aren't stock QuickBMS names (quickbms itself has no
	// Nintendo-specific plugin for most of these) -- they're this fork's own
	// aliases for the native decoders already used elsewhere (BLZ/ASH0/RL/
	// Huffman/RNC/LZH8/QuickLZ/Camelot), so a BMS script for a Nintendo game
	// that names its own compression this way runs natively instead of
	// falling through to raw-copy.
	else if (!strcasecmp (ctx->comtype, "ash0"))
		err = DecodeASH0 (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "rl") || !strcasecmp (ctx->comtype, "rle"))
		err = DecodeNintendoRL (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "huff4") || !strcasecmp (ctx->comtype, "huff8")
		|| !strcasecmp (ctx->comtype, "huffman"))
		err = DecodeNintendoHuff (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "rnc") || !strcasecmp (ctx->comtype, "rnc1")
		|| !strcasecmp (ctx->comtype, "rnc2"))
		err = DecodeRNC (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "lzh8"))
		err = DecodeLZH8 (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "quicklz") || !strcasecmp (ctx->comtype, "qlz"))
		err = DecodeQuickLZ (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "blz"))
		err = DecodeBLZ (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "camelot") || !strcasecmp (ctx->comtype, "stpl"))
		err = DecodeCamelot (&dest, &dest_size, src, (uint)comp_size);
	else if (!strcasecmp (ctx->comtype, "at7") || !strcasecmp (ctx->comtype, "at7p")
		|| !strcasecmp (ctx->comtype, "pmd"))
		err = DecodeAT7 (&dest, &dest_size, src, (uint)comp_size);
	else
	{
		szs_file_t szs;
		InitializeSZS (&szs);
		szs.fname = name;
		szs.cdata = (u8 *)src;
		szs.csize = comp_size;
		szs.file_size = comp_size;
		szs.fform_arch = szs.fform_current = GetByMagicFF (src, comp_size, comp_size);
		if (TryDecompressSZS (&szs, true) && szs.data)
		{
			dest_size = szs.size;
			dest = MALLOC (dest_size ? dest_size : 1);
			memcpy (dest, szs.data, dest_size);
			err = ERR_OK;
		}
		else
		{
			fprintf (stderr, "wbmsx: unsupported COMTYPE '%s', copying raw\n", ctx->comtype);
			dest_size = (uint)comp_size;
			dest = MALLOC (dest_size ? dest_size : 1);
			memcpy (dest, src, dest_size);
			err = ERR_OK;
		}
		szs.cdata = 0;
		ResetSZS (&szs);
	}

	if (err || !dest)
	{
		fprintf (stderr, "wbmsx: decompression failed for %s (comtype=%s)\n", name, ctx->comtype);
		if (dest)
			FREE (dest);
		return;
	}

	FILE *f = fopen (path, ctx->append ? "ab" : "wb");
	if (!f)
	{
		fprintf (stderr, "wbmsx: can't write %s\n", path);
		FREE (dest);
		return;
	}
	fwrite (dest, 1, dest_size, f);
	fclose (f);
	printf ("wbmsx: extracted+decompressed %s (%u bytes @ 0x%zx, comtype=%s)\n", path, dest_size,
		off, ctx->comtype);
	FREE (dest);
}

static char *strip_quotes (char *s)
{
	size_t l = strlen (s);
	if (l >= 2 && s[0] == '"' && s[l - 1] == '"')
	{
		s[l - 1] = 0;
		return s + 1;
	}
	return s;
}

static void tokenize (char *line, line_t *out)
{
	out->n = 0;
	char *p = line;
	while (*p)
	{
		while (*p == ' ' || *p == '\t' || *p == ',')
			p++;
		if (!*p)
			break;
		if (*p == '#')
			break; // comment
		char *start;
		if (*p == '"')
		{
			start = p++;
			while (*p && *p != '"')
				p++;
			if (*p == '"')
				p++;
		}
		else
		{
			start = p;
			while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != '#')
				p++;
		}
		char save = *p;
		*p = 0;
		if (out->n < MAX_TOK)
			out->tok[out->n++] = start;
		if (save == '#')
			break;
		if (save)
			p++;
	}
}

// Expression tokens only (MATH/XMATH/IF/FOR bounds): real scripts sometimes
// write expressions densely ("(2+3)*4-1", no spaces), so any *already
// whitespace-split* token containing embedded operator/paren glyphs is
// broken further into single tokens here (two-char operators == != <= >=
// << >> kept together). This is deliberately scoped to expression
// evaluation only, not the main tokenizer -- a bare token elsewhere (e.g.
// a GOTO offset of "-1") must stay one token so plain val_of() lookups on
// it keep working; only the expression parser needs per-operator tokens.
// A token that's already a clean number (e.g. "-1", "0x10") is left whole.
// Sub-tokens are copied into this pool rather than split in place: an
// operator like the "(" in "(2+3)" is only one byte, with no spare
// separator byte after it to overwrite with a null terminator (unlike the
// main tokenizer, which always splits on whitespace/comma it can safely
// destroy) -- copying is the only way to hand back independently
// null-terminated strings for the parser to strcmp against.
#define OP_POOL_SLOTS (MAX_TOK * 2)
#define OP_POOL_SLOT_LEN 32
static char op_pool[OP_POOL_SLOTS][OP_POOL_SLOT_LEN];

static int expand_op_tokens (char **in, int n, char **out, int max_out)
{
	int oc = 0;
	for (int i = 0; i < n && oc < max_out; i++)
	{
		char *t = in[i];
		if (t[0] == '"')
		{
			out[oc++] = t;
			continue;
		}
		char *end;
		strtoll (t, &end, 0);
		if (end != t && *end == 0)
		{
			out[oc++] = t; // whole token is a clean number -- keep intact
			continue;
		}
		bool has_op = false;
		for (char *q = t; *q; q++)
			if (strchr ("+-*/%&|^()<>=!~", *q))
			{
				has_op = true;
				break;
			}
		if (!has_op)
		{
			out[oc++] = t;
			continue;
		}
		static const char *two_char[] = { "==", "!=", "<=", ">=", "<<", ">>", NULL };
		const char *p = t;
		while (*p && oc < max_out)
		{
			const char *start = p;
			int len;
			if (strchr ("+-*/%&|^()<>=!~", *p))
			{
				len = 1;
				for (int k = 0; two_char[k]; k++)
					if (p[0] == two_char[k][0] && p[1] == two_char[k][1])
					{
						len = 2;
						break;
					}
			}
			else
			{
				len = 0;
				while (start[len] && !strchr ("+-*/%&|^()<>=!~", start[len]))
					len++;
			}
			char *slot = op_pool[oc % OP_POOL_SLOTS];
			int cl = len < OP_POOL_SLOT_LEN - 1 ? len : OP_POOL_SLOT_LEN - 1;
			memcpy (slot, start, cl);
			slot[cl] = 0;
			out[oc++] = slot;
			p += len;
		}
	}
	return oc;
}

static int find_matching (int from, const char *open_kw, const char *close_kw, int dir)
{
	int depth = 0;
	for (int i = from; i >= 0 && i < n_lines; i += dir)
	{
		if (lines[i].n == 0)
			continue;
		if (!strcasecmp (lines[i].tok[0], open_kw))
			depth += dir > 0 ? 1 : -1;
		else if (!strcasecmp (lines[i].tok[0], close_kw))
		{
			depth -= dir > 0 ? 1 : -1;
			if (depth == 0)
				return i;
		}
	}
	return -1;
}

// Evaluates one "A op B" atom; op is one of == != < > <= >= (defaults to
// "A != 0" if no operator token is present).
static bool eval_atom (bms_ctx_t *ctx, char **tok, int n)
{
	if (n < 3)
		return val_of (ctx, n > 0 ? tok[0] : NULL) != 0;
	int64_t a = val_of (ctx, tok[0]);
	const char *op = tok[1];
	int64_t b = val_of (ctx, tok[2]);
	if (!strcmp (op, "=="))
		return a == b;
	if (!strcmp (op, "!="))
		return a != b;
	if (!strcmp (op, "<"))
		return a < b;
	if (!strcmp (op, ">"))
		return a > b;
	if (!strcmp (op, "<="))
		return a <= b;
	if (!strcmp (op, ">="))
		return a >= b;
	return a != 0;
}

// IF VAR op VAL [AND|OR VAR op VAL ...] -- real QuickBMS scripts commonly
// chain conditions this way. Evaluated strictly left-to-right (no
// precedence between AND/OR, matching how quickbms itself folds them).
static bool eval_cond (bms_ctx_t *ctx, line_t *ln)
{
	// Expand dense operands ("count==3") into separate tokens first, same
	// as MATH/XMATH -- AND/OR are real keyword tokens (already whitespace
	// separated in any well-formed script) so they pass through untouched.
	char *expanded[MAX_TOK * 2];
	int n = expand_op_tokens (
		&ln->tok[1], ln->n - 1, expanded, (int)(sizeof (expanded) / sizeof (*expanded)));

	int i = 0;
	bool result = eval_atom (ctx, &expanded[i], n - i);
	// advance past the 3-token atom we just consumed (or 1, if it was bare)
	i += (n - i >= 3
			 && (!strcmp (expanded[i + 1], "==") || !strcmp (expanded[i + 1], "!=")
				 || !strcmp (expanded[i + 1], "<") || !strcmp (expanded[i + 1], ">")
				 || !strcmp (expanded[i + 1], "<=") || !strcmp (expanded[i + 1], ">=")))
		? 3
		: 1;
	while (i < n)
	{
		bool is_and = !strcasecmp (expanded[i], "AND");
		bool is_or = !strcasecmp (expanded[i], "OR");
		if (!is_and && !is_or)
			break;
		i++;
		bool atom = eval_atom (ctx, &expanded[i], n - i);
		result = is_and ? (result && atom) : (result || atom);
		i += (n - i >= 3
				 && (!strcmp (expanded[i + 1], "==") || !strcmp (expanded[i + 1], "!=")
					 || !strcmp (expanded[i + 1], "<") || !strcmp (expanded[i + 1], ">")
					 || !strcmp (expanded[i + 1], "<=") || !strcmp (expanded[i + 1], ">=")))
			? 3
			: 1;
	}
	return result;
}

// Resolves the file handle a command should operate on. Most commands
// list the handle as their last operand *only* when the script names one
// explicitly; a bare trailing integer/var after the command's normal
// operands is taken as the handle, otherwise handle 0 (main input).
static int handle_arg (bms_ctx_t *ctx, line_t *ln, int expected_min_n)
{
	if (ln->n > expected_min_n)
		return (int)val_of (ctx, ln->tok[ln->n - 1]);
	return 0;
}

static void do_open (bms_ctx_t *ctx, line_t *ln)
{
	// OPEN type filename handle   (type is e.g. FDSE/FDDE/GENERIC/TCC/
	// MEMORY_FILE -- only MEMORY_FILE and "reopen the main input" matter
	// here; other real on-disk archive members that live outside the
	// single input file this interpreter was invoked with are a known gap)
	if (ln->n < 4)
		return;
	const char *type = ln->tok[1];
	const char *fname = strip_quotes (ln->tok[2]);
	int handle = (int)val_of (ctx, ln->tok[3]);
	if (handle < 0 || handle >= MAX_FILES)
		return;
	bfile_t *f = &ctx->files[handle];
	if (f->used && f->owned)
		FREE (f->data);

	if (!strcasecmp (type, "MEMORY_FILE") || (fname[0] && !strcmp (fname, "?")))
	{
		f->data = 0;
		f->size = f->cap = f->pos = 0;
		f->owned = true;
		f->used = true;
		return;
	}
	if (!fname[0])
	{
		// Empty filename: alias of the main input file (handle 0's buffer),
		// which is how real scripts reopen "the archive currently being
		// processed" under a second handle.
		bfile_t *main0 = &ctx->files[0];
		f->data = main0->data;
		f->size = main0->size;
		f->pos = 0;
		f->owned = false;
		f->used = true;
		return;
	}
	// Named real file: try relative to the main input's directory (scripts
	// that open sibling files, e.g. a separate header/data pair).
	char path[PATH_MAX];
	const char *slash = ctx->infile_name ? strrchr (ctx->infile_name, '/') : NULL;
	if (slash)
		snprintf (path, sizeof (path), "%.*s/%s", (int)(slash - ctx->infile_name), ctx->infile_name,
			fname);
	else
		snprintf (path, sizeof (path), "%s", fname);
	FILE *fp = fopen (path, "rb");
	if (!fp)
	{
		fprintf (stderr, "wbmsx: OPEN could not find sibling file '%s', using main input\n", fname);
		bfile_t *main0 = &ctx->files[0];
		f->data = main0->data;
		f->size = main0->size;
		f->pos = 0;
		f->owned = false;
		f->used = true;
		return;
	}
	fseek (fp, 0, SEEK_END);
	long sz = ftell (fp);
	fseek (fp, 0, SEEK_SET);
	f->data = MALLOC (sz > 0 ? sz : 1);
	f->size = sz > 0 ? fread (f->data, 1, sz, fp) : 0;
	fclose (fp);
	f->cap = f->size;
	f->pos = 0;
	f->owned = true;
	f->used = true;
}

static void run (bms_ctx_t *ctx)
{
	for (int ip = 0; ip < n_lines; ip++)
	{
		line_t *ln = &lines[ip];
		if (ln->n == 0)
			continue;
		const char *op = ln->tok[0];

		if (!strcasecmp (op, "IDSTRING"))
		{
			int handle = handle_arg (ctx, ln, 2);
			bfile_t *f = get_file (ctx, handle);
			char *needle = strip_quotes (ln->tok[1]);
			size_t nl = strlen (needle);
			if (f->pos + nl > f->size || memcmp (f->data + f->pos, needle, nl))
			{
				fprintf (stderr, "wbmsx: IDSTRING mismatch at 0x%zx (expected \"%s\")\n", f->pos,
					needle);
				return;
			}
			f->pos += nl;
		}
		else if (!strcasecmp (op, "ENDIAN"))
			ctx->big_endian = !strcasecmp (ln->tok[1], "big");
		else if (!strcasecmp (op, "COMTYPE"))
			strncpy (ctx->comtype, ln->tok[1], sizeof (ctx->comtype) - 1);
		else if (!strcasecmp (op, "APPEND"))
			ctx->append = ln->n < 2 || strcasecmp (ln->tok[1], "off") != 0;
		else if (!strcasecmp (op, "OPEN"))
			do_open (ctx, ln);
		else if (!strcasecmp (op, "GET") || !strcasecmp (op, "GETDSTRING")
			|| !strcasecmp (op, "GETCT"))
		{
			int handle = handle_arg (ctx, ln, !strcasecmp (op, "GETCT") ? 4 : 3);
			bfile_t *f = get_file (ctx, handle);
			if (!strcasecmp (op, "GETDSTRING"))
			{
				int64_t n = val_of (ctx, ln->tok[2]);
				if (n < 0)
					n = 0;
				if (f->pos + (size_t)n > f->size)
					n = f->size - f->pos;
				set_str_var (ctx, ln->tok[1], "");
				var_t *v = find_var (ctx, ln->tok[1], true);
				size_t cl = n < (int64_t)sizeof (v->sval) - 1 ? (size_t)n : sizeof (v->sval) - 1;
				memcpy (v->sval, f->data + f->pos, cl);
				v->sval[cl] = 0;
				v->is_str = true;
				f->pos += n;
			}
			else if (!strcasecmp (op, "GETCT"))
			{
				// GETCT NAME TYPE DELIMITER [handle] -- reads bytes up to
				// (not including) the delimiter byte and advances past it.
				char delim = ln->tok[3][0] == '"' ? ln->tok[3][1] : (char)val_of (ctx, ln->tok[3]);
				size_t start = f->pos, p = f->pos;
				while (p < f->size && f->data[p] != (uint8_t)delim)
					p++;
				size_t cl = p - start;
				var_t *v = find_var (ctx, ln->tok[1], true);
				if (cl >= sizeof (v->sval))
					cl = sizeof (v->sval) - 1;
				memcpy (v->sval, f->data + start, cl);
				v->sval[cl] = 0;
				v->is_str = true;
				f->pos = p < f->size ? p + 1 : p;
			}
			else if (!strcasecmp (ln->tok[2], "asize") || !strcasecmp (ln->tok[2], "size")
				|| !strcasecmp (ln->tok[2], "fsize"))
			{
				// QuickBMS pseudo-type: not a byte width to read from the
				// stream -- it's the current file's total size, and the
				// read position is left untouched.
				set_var (ctx, ln->tok[1], (int64_t)f->size);
			}
			else
			{
				int sz = type_size (ln->tok[2]);
				if (f->pos + sz > f->size)
				{
					set_var (ctx, ln->tok[1], 0);
					continue;
				}
				uint64_t v = ctx->big_endian ? read_be (f->data + f->pos, sz)
											 : read_le (f->data + f->pos, sz);
				set_var (ctx, ln->tok[1], (int64_t)v);
				f->pos += sz;
			}
		}
		else if (!strcasecmp (op, "PUT") || !strcasecmp (op, "PUTDSTRING")
			|| !strcasecmp (op, "PUTCT"))
		{
			// PUT VAL TYPE [handle] (base 3 tokens) vs PUTDSTRING/PUTCT
			// VAL [handle] (base 2 tokens) -- different arity, so they
			// need different thresholds for "is there a trailing handle".
			int handle = handle_arg (ctx, ln, strcasecmp (op, "PUT") ? 2 : 3);
			bfile_t *f = get_file (ctx, handle);
			if (!strcasecmp (op, "PUTDSTRING") || !strcasecmp (op, "PUTCT"))
			{
				const char *s = str_of (ctx, ln->tok[1]);
				size_t l = strlen (s);
				ensure_cap (f, l);
				if (f->pos + l <= f->cap || !f->owned)
				{
					size_t cl
						= f->pos + l <= f->size ? l : (f->size > f->pos ? f->size - f->pos : 0);
					if (f->owned)
						memcpy (f->data + f->pos, s, l);
					f->pos += !strcasecmp (op, "PUTCT") ? cl + 1 : l;
				}
			}
			else
			{
				int sz = type_size (ln->tok[2]);
				uint64_t v = (uint64_t)val_of (ctx, ln->tok[1]);
				ensure_cap (f, sz);
				if (f->owned)
				{
					if (ctx->big_endian)
						write_be (f->data + f->pos, sz, v);
					else
						write_le (f->data + f->pos, sz, v);
				}
				f->pos += sz;
			}
		}
		else if (!strcasecmp (op, "GOTO"))
		{
			// GOTO OFFSET [SEEK_CUR|SEEK_END] [HANDLE] -- the seek-origin
			// keyword is optional, so a bare trailing token (tok[2] when
			// it isn't SEEK_CUR/SEEK_END) is the handle, not the origin.
			int64_t p = val_of (ctx, ln->tok[1]);
			bool rel = false, end = false;
			int handle = 0;
			if (ln->n > 2 && !strcasecmp (ln->tok[2], "SEEK_CUR"))
			{
				rel = true;
				if (ln->n > 3)
					handle = (int)val_of (ctx, ln->tok[3]);
			}
			else if (ln->n > 2 && !strcasecmp (ln->tok[2], "SEEK_END"))
			{
				end = true;
				if (ln->n > 3)
					handle = (int)val_of (ctx, ln->tok[3]);
			}
			else if (ln->n > 2)
				handle = (int)val_of (ctx, ln->tok[2]);
			bfile_t *f = get_file (ctx, handle);
			f->pos = rel ? f->pos + p
				: end	 ? (size_t)(p < 0 ? (int64_t)f->size + p : (int64_t)f->size - p)
						 : (size_t)(p < 0 ? 0 : p);
		}
		else if (!strcasecmp (op, "SAVEPOS"))
		{
			int handle = handle_arg (ctx, ln, 2);
			bfile_t *f = get_file (ctx, handle);
			set_var (ctx, ln->tok[1], (int64_t)f->pos);
		}
		else if (!strcasecmp (op, "PADDING"))
		{
			int handle = handle_arg (ctx, ln, 2);
			bfile_t *f = get_file (ctx, handle);
			int64_t align = val_of (ctx, ln->tok[1]);
			if (align > 0)
			{
				size_t rem = f->pos % align;
				if (rem)
					f->pos += align - rem;
			}
		}
		else if (!strcasecmp (op, "FINDLOC"))
		{
			// FINDLOC VAR TYPE "needle" [start_offset] [handle] -- a lone
			// 5th token is genuinely ambiguous (could be start_offset with
			// no handle, or handle with no start_offset); this treats it
			// as start_offset, matching the reading below, since that's
			// the more commonly-supplied optional argument in real scripts.
			int handle = handle_arg (ctx, ln, ln->n >= 5 ? 5 : 4);
			bfile_t *f = get_file (ctx, handle);
			const char *needle = str_of (ctx, ln->tok[3]);
			size_t nl = strlen (needle);
			size_t start
				= ln->n >= 5 && ln->tok[4][0] != '"' ? (size_t)val_of (ctx, ln->tok[4]) : f->pos;
			int64_t found = -1;
			if (nl && start < f->size)
				for (size_t p = start; p + nl <= f->size; p++)
					if (!memcmp (f->data + p, needle, nl))
					{
						found = (int64_t)p;
						break;
					}
			set_var (ctx, ln->tok[1], found);
		}
		else if (!strcasecmp (op, "MATH") || !strcasecmp (op, "XMATH"))
		{
			// MATH VAR op VAL  |  XMATH VAR "expr tokens..."
			if (!strcasecmp (op, "XMATH") && ln->n >= 3 && ln->tok[2][0] == '"')
			{
				// XMATH scripts often quote the whole expression as one
				// token; re-tokenize just that string with our tokenizer so
				// the operator-precedence parser can walk it normally.
				char buf[512];
				strncpy (buf, str_of (ctx, ln->tok[2]), sizeof (buf) - 1);
				buf[sizeof (buf) - 1] = 0;
				line_t sub;
				tokenize (buf, &sub);
				set_var (ctx, ln->tok[1], eval_tokens (ctx, sub.tok, sub.n));
			}
			else if (ln->n == 4 && strlen (ln->tok[2]) == 2 && ln->tok[2][1] == '='
				&& strchr ("+-*/%&|^", ln->tok[2][0]))
			{
				// Compound assignment ("MATH VAR -= VAL" etc): the RHS
				// expression parser only ever sees the tokens after VAR, so
				// it has no way to fold in VAR's own current value -- do
				// that explicitly instead of letting it silently evaluate
				// "-= VAL" as if "-=" were a variable (reads as 0).
				char binop[2] = { ln->tok[2][0], 0 };
				int64_t lhs = val_of (ctx, ln->tok[1]);
				int64_t rhs = eval_tokens (ctx, &ln->tok[3], ln->n - 3);
				set_var (ctx, ln->tok[1], apply_op (binop, lhs, rhs));
			}
			else
				set_var (ctx, ln->tok[1], eval_tokens (ctx, &ln->tok[2], ln->n - 2));
		}
		else if (!strcasecmp (op, "SET"))
		{
			const char *rhs = ln->tok[ln->n > 2 ? 2 : 1];
			if (rhs[0] == '"')
				set_str_var (ctx, ln->tok[1], strip_quotes ((char *)rhs));
			else
			{
				var_t *rv = find_var (ctx, rhs, false);
				if (rv && rv->is_str && ln->n <= 3)
					set_str_var (ctx, ln->tok[1], rv->sval);
				else
					set_var (ctx, ln->tok[1],
						eval_tokens (
							ctx, &ln->tok[ln->n > 2 ? 2 : 1], ln->n - (ln->n > 2 ? 2 : 1)));
			}
		}
		else if (!strcasecmp (op, "STRING"))
		{
			// STRING VAR = VAR2 [+ VAR3 ...]  -- concatenation. Real
			// QuickBMS also supports numeric string ops (compare, etc);
			// only concatenation (by far the common case in extraction
			// scripts building output paths) is implemented.
			int eq = (ln->n > 2 && !strcmp (ln->tok[2], "=")) ? 3 : 2;
			char buf[512] = { 0 };
			for (int i = eq; i < ln->n; i++)
			{
				if (!strcmp (ln->tok[i], "+"))
					continue;
				strncat (buf, str_of (ctx, ln->tok[i]), sizeof (buf) - strlen (buf) - 1);
			}
			set_str_var (ctx, ln->tok[1], buf);
		}
		else if (!strcasecmp (op, "STRLEN"))
		{
			set_var (ctx, ln->tok[1], (int64_t)strlen (str_of (ctx, ln->tok[2])));
		}
		else if (!strcasecmp (op, "GETVARCHR"))
		{
			// GETVARCHR VAR SRC_STRING INDEX [TYPE]
			const char *s = str_of (ctx, ln->tok[2]);
			int64_t idx = val_of (ctx, ln->tok[3]);
			size_t l = strlen (s);
			set_var (ctx, ln->tok[1], idx >= 0 && (size_t)idx < l ? (uint8_t)s[idx] : 0);
		}
		else if (!strcasecmp (op, "PUTVARCHR"))
		{
			// PUTVARCHR SRC_STRING INDEX VAR [TYPE]
			var_t *v = find_var (ctx, ln->tok[1], true);
			if (!v->is_str)
				v->sval[0] = 0, v->is_str = true;
			int64_t idx = val_of (ctx, ln->tok[2]);
			int64_t val = val_of (ctx, ln->tok[3]);
			size_t l = strlen (v->sval);
			if (idx >= 0 && (size_t)idx < sizeof (v->sval) - 1)
			{
				if ((size_t)idx >= l)
				{
					memset (v->sval + l, ' ', (size_t)idx - l);
					v->sval[idx + 1] = 0;
				}
				v->sval[idx] = (char)val;
			}
		}
		else if (!strcasecmp (op, "REVERSESHORT") || !strcasecmp (op, "REVERSELONG")
			|| !strcasecmp (op, "REVERSELONGLONG"))
		{
			int n = !strcasecmp (op, "REVERSESHORT") ? 2 : !strcasecmp (op, "REVERSELONG") ? 4 : 8;
			uint64_t v = (uint64_t)val_of (ctx, ln->tok[1]);
			uint64_t r = 0;
			for (int i = 0; i < n; i++)
				r |= ((v >> (8 * i)) & 0xff) << (8 * (n - 1 - i));
			set_var (ctx, ln->tok[1], (int64_t)r);
		}
		else if (!strcasecmp (op, "GETBITS"))
		{
			// GETBITS VAR NUMBITS [handle] -- reads NUMBITS bits MSB-first
			// from the byte stream at the current position, advancing by
			// whole bytes as bits are consumed (QuickBMS keeps a persistent
			// bit cursor per file; this simplified version resets to a byte
			// boundary after each GETBITS call, which is exact for the
			// common "read one bitfield, move on" scripts and only wrong
			// for back-to-back GETBITS calls meant to share a byte).
			int handle = handle_arg (ctx, ln, 3);
			bfile_t *f = get_file (ctx, handle);
			int64_t nbits = val_of (ctx, ln->tok[2]);
			uint64_t v = 0;
			int64_t got = 0;
			while (got < nbits && f->pos < f->size)
			{
				uint8_t byte = f->data[f->pos++];
				int take = nbits - got < 8 ? (int)(nbits - got) : 8;
				v = (v << take) | (byte >> (8 - take));
				got += take;
			}
			set_var (ctx, ln->tok[1], (int64_t)v);
		}
		else if (!strcasecmp (op, "PUTBITS"))
		{
			// PUTBITS VAR NUMBITS [handle] -- inverse of GETBITS, same
			// byte-boundary simplification.
			int handle = handle_arg (ctx, ln, 3);
			bfile_t *f = get_file (ctx, handle);
			int64_t nbits = val_of (ctx, ln->tok[2]);
			uint64_t v = (uint64_t)val_of (ctx, ln->tok[1]);
			int64_t written = 0;
			while (written < nbits)
			{
				int take = nbits - written < 8 ? (int)(nbits - written) : 8;
				ensure_cap (f, 1);
				if (f->owned)
					f->data[f->pos] = (uint8_t)((v >> (nbits - written - take)) << (8 - take));
				f->pos++;
				written += take;
			}
		}
		else if (!strcasecmp (op, "ENCRYPTION"))
		{
			// No-op: the crypto suite behind this (AES/RC4/XOR/Blowfish/...,
			// ~100 algorithms in real QuickBMS) isn't implemented. Scripts
			// that rely on it will extract still-encrypted data rather than
			// failing outright.
		}
		else if (!strcasecmp (op, "LOG"))
		{
			int handle = handle_arg (ctx, ln, 4);
			bfile_t *f = get_file (ctx, handle);
			var_t *nv = find_var (ctx, ln->tok[1], false);
			const char *name = (nv && nv->is_str) ? nv->sval : strip_quotes (ln->tok[1]);
			size_t off = (size_t)val_of (ctx, ln->tok[2]);
			size_t size = (size_t)val_of (ctx, ln->tok[3]);
			save_span (ctx, name, f->data, f->size, off, size);
		}
		else if (!strcasecmp (op, "CLOG"))
		{
			// Same ambiguity as FINDLOC's optional argument: a lone 5th
			// token is read as usize, not handle.
			int handle = handle_arg (ctx, ln, 5);
			bfile_t *f = get_file (ctx, handle);
			var_t *nv = find_var (ctx, ln->tok[1], false);
			const char *name = (nv && nv->is_str) ? nv->sval : strip_quotes (ln->tok[1]);
			size_t off = (size_t)val_of (ctx, ln->tok[2]);
			size_t zsize = (size_t)val_of (ctx, ln->tok[3]);
			size_t usize = ln->n > 4 ? (size_t)val_of (ctx, ln->tok[4]) : 0;
			clog_span (ctx, name, f->data, f->size, off, zsize, usize);
		}
		else if (!strcasecmp (op, "PRINT"))
		{
			if (ln->n <= 1)
				printf ("wbmsx: \n");
			else if (ln->tok[1][0] == '"')
				printf ("wbmsx: %s\n", strip_quotes (ln->tok[1]));
			else
			{
				var_t *v = find_var (ctx, ln->tok[1], false);
				if (v && v->is_str)
					printf ("wbmsx: %s\n", v->sval);
				else
					printf ("wbmsx: %" PRId64 "\n", val_of (ctx, ln->tok[1]));
			}
		}
		else if (!strcasecmp (op, "FOR"))
			// FOR VAR = START TO END  (START/END may be full expressions)
			set_var (ctx, ln->tok[1], eval_tokens (ctx, &ln->tok[3], 1));
		else if (!strcasecmp (op, "NEXT"))
		{
			int for_ip = find_matching (ip, "NEXT", "FOR", -1);
			if (for_ip >= 0)
			{
				line_t *forln = &lines[for_ip];
				var_t *v = find_var (ctx, forln->tok[1], true);
				v->val++;
				// tok[4] is "TO"; the end expression runs from tok[5] to
				// end-of-line.
				int64_t end
					= forln->n > 5 ? eval_tokens (ctx, &forln->tok[5], forln->n - 5) : v->val;
				if (v->val <= end)
					ip = for_ip; // loop back (for-loop re-executes body next iteration)
			}
		}
		else if (!strcasecmp (op, "WHILE"))
		{
			if (!eval_cond (ctx, ln))
			{
				int endw_ip = find_matching (ip, "WHILE", "ENDWHILE", 1);
				ip = endw_ip >= 0 ? endw_ip : ip;
			}
		}
		else if (!strcasecmp (op, "ENDWHILE"))
		{
			int while_ip = find_matching (ip, "ENDWHILE", "WHILE", -1);
			if (while_ip >= 0)
				ip = while_ip - 1; // re-enter the WHILE test next iteration
		}
		else if (!strcasecmp (op, "IF"))
		{
			if (!eval_cond (ctx, ln))
			{
				int endif_ip = find_matching (ip, "IF", "ENDIF", 1);
				ip = endif_ip >= 0 ? endif_ip : ip;
			}
		}
		else if (!strcasecmp (op, "ELSE") || !strcasecmp (op, "ENDIF"))
		{
			if (!strcasecmp (op, "ELSE"))
			{
				int endif_ip = find_matching (ip, "IF", "ENDIF", 1);
				ip = endif_ip >= 0 ? endif_ip : ip;
			}
			// ENDIF: no-op fallthrough
		}
		// Unknown opcodes (CallDLL, array ops, hashing, etc -- see the file
		// header for what's out of scope) are silently skipped: best-effort
		// execution, matching this interpreter's existing philosophy of
		// running what it can rather than aborting the whole script.
	}
}

static enumError RunNativeBmsScript (ccp script_path, ccp infile, ccp outdir)
{
	FILE *sf = fopen (script_path, "rb");
	if (!sf)
		return ERROR0 (ERR_CANT_OPEN, "Can't open BMS script: %s\n", script_path);
	static char script_buf[1 << 20];
	size_t script_len = fread (script_buf, 1, sizeof (script_buf) - 1, sf);
	script_buf[script_len] = 0;
	fclose (sf);

	n_lines = 0;
	char *save;
	char *line = strtok_r (script_buf, "\n", &save);
	while (line && n_lines < MAX_LINES)
	{
		const size_t l = strlen (line);
		if (l && line[l - 1] == '\r')
			line[l - 1] = 0;
		tokenize (line, &lines[n_lines]);
		if (lines[n_lines].n > 0 && lines[n_lines].tok[0][0] != '#')
			n_lines++;
		line = strtok_r (NULL, "\n", &save);
	}

	FILE *inf = fopen (infile, "rb");
	if (!inf)
		return ERROR0 (ERR_CANT_OPEN, "Can't open BMS input: %s\n", infile);
	fseek (inf, 0, SEEK_END);
	long fsize = ftell (inf);
	fseek (inf, 0, SEEK_SET);
	if (fsize < 0)
	{
		fclose (inf);
		return ERR_READ_FAILED;
	}
	uint8_t *fdata = (uint8_t *)MALLOC (fsize ? fsize : 1);
	if (fsize && fread (fdata, 1, fsize, inf) != (size_t)fsize)
	{
		fclose (inf);
		FREE (fdata);
		return ERROR0 (ERR_READ_FAILED, "Short read on BMS input: %s\n", infile);
	}
	fclose (inf);

	bms_ctx_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	strcpy (ctx.comtype, "copy");
	ctx.outdir = (char *)outdir;
	ctx.infile_name = (char *)infile;
	mkdir (ctx.outdir, 0755);

	ctx.files[0].data = fdata;
	ctx.files[0].size = (size_t)fsize;
	ctx.files[0].cap = (size_t)fsize;
	ctx.files[0].owned = false; // freed explicitly below, not via generic cleanup
	ctx.files[0].used = true;

	run (&ctx);

	for (int i = 0; i < MAX_FILES; i++)
		if (ctx.files[i].used && ctx.files[i].owned)
			FREE (ctx.files[i].data);
	FREE (fdata);
	return ERR_OK;
}

// Run the bundled QuickBMS engine when it is available.  Keeping this as a
// separate process is intentional: QuickBMS exposes a plugin/DLL ABI and a
// very large codec and crypto registry which cannot safely share this
// program's process state.  WBMSX_QUICKBMS is primarily useful to packagers;
// the second path makes an in-tree build work without installation.
enumError RunBmsScript (ccp script_path, ccp infile, ccp outdir)
{
	const char *engine = getenv ("WBMSX_QUICKBMS");
	if (!engine || !*engine)
	{
		static char bundled[PATH_MAX];
		if (ProgInfo.progdir && *ProgInfo.progdir)
			snprintf (bundled, sizeof (bundled), "%s/third_party/quickbms/quickbms",
				ProgInfo.progdir);
		else
			strcpy (bundled, "third_party/quickbms/quickbms");
		if (!access (bundled, X_OK))
			engine = bundled;
		else
			engine = "quickbms";
	}

	pid_t pid = fork ();
	if (pid < 0)
		return ERROR0 (ERR_ERROR, "Can't start QuickBMS\n");
	if (!pid)
	{
		execlp (engine, engine, script_path, infile, outdir, (char *)NULL);
		// A source checkout remains useful before its bundled dependency has
		// been built.  Do not make that look like full QuickBMS compatibility.
		if (!strcmp (engine, "quickbms"))
			exit (RunNativeBmsScript (script_path, infile, outdir));
		perror ("wbmsx: exec QuickBMS");
		exit (127);
	}

	int status;
	if (waitpid (pid, &status, 0) < 0)
		return ERROR0 (ERR_ERROR, "Can't wait for QuickBMS\n");
	if (WIFEXITED (status))
		return WEXITSTATUS (status) ? ERR_ERROR : ERR_OK;
	return ERR_ERROR;
}
