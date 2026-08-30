// Minimal native QuickBMS script interpreter -- shared implementation used
// by both `wszst BMS` and the wbmsx tool.
//
// Supports enough of the QuickBMS scripting language to run typical linear
// "open, walk a table, extract entries" style extraction scripts:
// IDSTRING, GET/GETDSTRING, GOTO, SAVEPOS, MATH, FOR/NEXT, IF/ELSE/ENDIF,
// LOG, CLOG, COMTYPE, ENDIAN, PRINT. This is not a full QuickBMS clone --
// string-manipulation opcodes, CALLFUNCTION, and most of the more exotic
// opcodes are not implemented. No quickbms binary is invoked; the script
// is parsed and executed entirely in this process.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <zlib.h>
#include "lib-std.h"
#include "lib-szs.h"
#include "lib-nintendo.h"
#include "lib-quicklz.h"
#include "lib-bms.h"

#define MAX_VARS 1024
#define MAX_LINES 8192
#define MAX_TOK 16

typedef struct var_t
{
	char name[64];
	int64_t val;
	char sval[512];
	bool is_str;
} var_t;

typedef struct
{
	var_t vars[MAX_VARS];
	int n_vars;

	uint8_t *file;
	size_t file_size;
	size_t pos;

	bool big_endian;
	char comtype[32];

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

// A "value" in QuickBMS is either a variable name, a bare number, or a
// quoted string. This resolves it to an integer (0 if it's a var that
// doesn't exist yet, which QuickBMS also treats as 0).
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

// Evaluates a simple "A" or "A op B" expression starting at ln->tok[idx],
// where op is one of + - * /. Returns the number of tokens consumed.
static int64_t eval_expr_at (bms_ctx_t *ctx, line_t *ln, int idx, int *consumed)
{
	int64_t a = val_of (ctx, ln->tok[idx]);
	if (idx + 2 < ln->n)
	{
		const char *o = ln->tok[idx + 1];
		if (!strcmp (o, "+") || !strcmp (o, "-") || !strcmp (o, "*") || !strcmp (o, "/"))
		{
			int64_t b = val_of (ctx, ln->tok[idx + 2]);
			if (consumed)
				*consumed = 3;
			if (!strcmp (o, "+"))
				return a + b;
			if (!strcmp (o, "-"))
				return a - b;
			if (!strcmp (o, "*"))
				return a * b;
			return b ? a / b : 0;
		}
	}
	if (consumed)
		*consumed = 1;
	return a;
}

static void set_var (bms_ctx_t *ctx, const char *name, int64_t value)
{
	var_t *v = find_var (ctx, name, true);
	v->val = value;
	v->is_str = false;
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

static void save_span (bms_ctx_t *ctx, const char *name, size_t off, size_t size)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", ctx->outdir, name);
	mkdirs (path);
	if (off > ctx->file_size)
		off = ctx->file_size;
	if (off + size > ctx->file_size)
		size = ctx->file_size - off;
	FILE *f = fopen (path, "wb");
	if (!f)
	{
		fprintf (stderr, "wbmsx: can't write %s\n", path);
		return;
	}
	fwrite (ctx->file + off, 1, size, f);
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

static void clog_span (
	bms_ctx_t *ctx, const char *name, size_t off, size_t comp_size, size_t uncomp_size)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", ctx->outdir, name);
	mkdirs (path);
	if (off > ctx->file_size)
		off = ctx->file_size;
	if (off + comp_size > ctx->file_size)
		comp_size = ctx->file_size - off;
	const u8 *src = ctx->file + off;

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
	(void)uncomp_size;

	if (err || !dest)
	{
		fprintf (stderr, "wbmsx: decompression failed for %s (comtype=%s)\n", name, ctx->comtype);
		if (dest)
			FREE (dest);
		return;
	}

	FILE *f = fopen (path, "wb");
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

static bool eval_cond (bms_ctx_t *ctx, line_t *ln)
{
	// IF VAR op VAL  (supports ==, !=, <, >, <=, >=)
	if (ln->n < 3)
		return val_of (ctx, ln->tok[1]) != 0;
	int64_t a = val_of (ctx, ln->tok[1]);
	const char *op = ln->tok[2];
	int64_t b = val_of (ctx, ln->tok[3]);
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
			char *needle = strip_quotes (ln->tok[1]);
			size_t nl = strlen (needle);
			if (ctx->pos + nl > ctx->file_size || memcmp (ctx->file + ctx->pos, needle, nl))
			{
				fprintf (stderr, "wbmsx: IDSTRING mismatch at 0x%zx (expected \"%s\")\n", ctx->pos,
					needle);
				return;
			}
			ctx->pos += nl;
		}
		else if (!strcasecmp (op, "ENDIAN"))
		{
			ctx->big_endian = !strcasecmp (ln->tok[1], "big");
		}
		else if (!strcasecmp (op, "COMTYPE"))
		{
			strncpy (ctx->comtype, ln->tok[1], sizeof (ctx->comtype) - 1);
		}
		else if (!strcasecmp (op, "GET") || !strcasecmp (op, "GETDSTRING"))
		{
			if (!strcasecmp (op, "GETDSTRING"))
			{
				int64_t n = val_of (ctx, ln->tok[2]);
				if (n < 0)
					n = 0;
				if (ctx->pos + (size_t)n > ctx->file_size)
					n = ctx->file_size - ctx->pos;
				var_t *v = find_var (ctx, ln->tok[1], true);
				size_t cl = n < (int64_t)sizeof (v->sval) - 1 ? (size_t)n : sizeof (v->sval) - 1;
				memcpy (v->sval, ctx->file + ctx->pos, cl);
				v->sval[cl] = 0;
				v->is_str = true;
				ctx->pos += n;
			}
			else
			{
				int sz = type_size (ln->tok[2]);
				if (ctx->pos + sz > ctx->file_size)
				{
					set_var (ctx, ln->tok[1], 0);
					continue;
				}
				uint64_t v = ctx->big_endian ? read_be (ctx->file + ctx->pos, sz)
											 : read_le (ctx->file + ctx->pos, sz);
				set_var (ctx, ln->tok[1], (int64_t)v);
				ctx->pos += sz;
			}
		}
		else if (!strcasecmp (op, "GOTO"))
		{
			int64_t p = val_of (ctx, ln->tok[1]);
			bool rel = ln->n > 2 && !strcasecmp (ln->tok[2], "SEEK_CUR");
			ctx->pos = rel ? ctx->pos + p : (size_t)(p < 0 ? 0 : p);
		}
		else if (!strcasecmp (op, "SAVEPOS"))
		{
			set_var (ctx, ln->tok[1], (int64_t)ctx->pos);
		}
		else if (!strcasecmp (op, "MATH"))
		{
			int64_t a = val_of (ctx, ln->tok[1]);
			const char *o = ln->tok[2];
			int64_t b = val_of (ctx, ln->tok[3]);
			int64_t r = a;
			if (!strcmp (o, "+"))
				r = a + b;
			else if (!strcmp (o, "-"))
				r = a - b;
			else if (!strcmp (o, "*"))
				r = a * b;
			else if (!strcmp (o, "/"))
				r = b ? a / b : 0;
			else if (!strcmp (o, "&"))
				r = a & b;
			else if (!strcmp (o, "|"))
				r = a | b;
			else if (!strcmp (o, "^"))
				r = a ^ b;
			set_var (ctx, ln->tok[1], r);
		}
		else if (!strcasecmp (op, "SET"))
		{
			const char *rhs = ln->tok[ln->n > 2 ? 2 : 1];
			if (rhs[0] == '"')
			{
				var_t *v = find_var (ctx, ln->tok[1], true);
				strncpy (v->sval, strip_quotes ((char *)rhs), sizeof (v->sval) - 1);
				v->is_str = true;
			}
			else
				set_var (ctx, ln->tok[1], val_of (ctx, rhs));
		}
		else if (!strcasecmp (op, "LOG"))
		{
			var_t *nv = find_var (ctx, ln->tok[1], false);
			const char *name = (nv && nv->is_str) ? nv->sval : strip_quotes (ln->tok[1]);
			size_t off = (size_t)val_of (ctx, ln->tok[2]);
			size_t size = (size_t)val_of (ctx, ln->tok[3]);
			save_span (ctx, name, off, size);
		}
		else if (!strcasecmp (op, "CLOG"))
		{
			var_t *nv = find_var (ctx, ln->tok[1], false);
			const char *name = (nv && nv->is_str) ? nv->sval : strip_quotes (ln->tok[1]);
			size_t off = (size_t)val_of (ctx, ln->tok[2]);
			size_t zsize = (size_t)val_of (ctx, ln->tok[3]);
			size_t usize = ln->n > 4 ? (size_t)val_of (ctx, ln->tok[4]) : 0;
			clog_span (ctx, name, off, zsize, usize);
		}
		else if (!strcasecmp (op, "PRINT"))
		{
			printf ("wbmsx: %s\n", ln->n > 1 ? strip_quotes (ln->tok[1]) : "");
		}
		else if (!strcasecmp (op, "FOR"))
		{
			// FOR VAR = START TO END  (START/END may be "A op B" expressions)
			set_var (ctx, ln->tok[1], eval_expr_at (ctx, ln, 3, NULL));
			// loop condition checked at matching NEXT
		}
		else if (!strcasecmp (op, "NEXT"))
		{
			int for_ip = find_matching (ip, "NEXT", "FOR", -1);
			if (for_ip >= 0)
			{
				line_t *forln = &lines[for_ip];
				var_t *v = find_var (ctx, forln->tok[1], true);
				v->val++;
				// tok[4] is "TO"; the end expression starts at tok[5].
				int64_t end = forln->n > 5 ? eval_expr_at (ctx, forln, 5, NULL) : v->val;
				if (v->val <= end)
					ip = for_ip; // loop back (for-loop re-executes body next iteration)
			}
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
		// Unknown opcodes are silently skipped (best-effort execution).
	}
}

enumError RunBmsScript (ccp script_path, ccp infile, ccp outdir)
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
	ctx.file = fdata;
	ctx.file_size = (size_t)fsize;
	strcpy (ctx.comtype, "copy");
	ctx.outdir = (char *)outdir;
	ctx.infile_name = (char *)infile;
	mkdir (ctx.outdir, 0755);

	run (&ctx);

	FREE (fdata);
	return ERR_OK;
}
