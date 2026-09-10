
/***************************************************************************
 *   Vendored QuickBMS decompression codecs -- dispatch shim.               *
 *   See qbms-comp.h and codecs/README.                                    *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdarg.h>

#include "qbms-comp.h"

//
// ---------------------------------------------------------------------------
// Small helper expected by codecs/lzss.c when it is handed a parameter
// string ("EI EJ P rless init_chr").  QuickBMS provides this in utils.c; the
// codec only calls it on the non-default path, so a compact reimplementation
// is enough.  Parses up to N leading integers (decimal or 0x-hex) separated
// by any run of non-alphanumeric characters, into the int32 pointers passed
// as a NULL-terminated varargs list.  Returns the count actually stored.
// ---------------------------------------------------------------------------
//
int get_parameter_numbers_i32 ( unsigned char *s, ... )
{
	va_list ap;
	int count = 0;

	if ( !s || !*s )
		return 0;

	va_start ( ap, s );
	for (;;)
	{
		int32_t *par = va_arg ( ap, int32_t * );
		if ( !par )
			break;

		while ( *s && !( ( *s >= '0' && *s <= '9' )
			|| ( *s >= 'a' && *s <= 'z' ) || ( *s >= 'A' && *s <= 'Z' ) ) )
			s++;
		if ( !*s )
			break;

		*par = (int32_t) strtol ( (const char *) s, NULL, 0 );
		count++;

		while ( *s && ( ( *s >= '0' && *s <= '9' )
			|| ( *s >= 'a' && *s <= 'z' ) || ( *s >= 'A' && *s <= 'Z' ) ) )
			s++;
		if ( !*s )
			break;
	}
	va_end ( ap );
	return count;
}

//
// ---------------------------------------------------------------------------
// Prototypes for the vendored codec entry points (codecs/*.c).  Signatures
// mirror how QuickBMS's perform.c invokes each one.
// ---------------------------------------------------------------------------
//
extern int unlzss   ( unsigned char *src, int srclen, unsigned char *dst, int dstlen, unsigned char *parameters );
extern int unlzari  ( unsigned char *in, int insz, unsigned char *out, int outsz );
extern int unlzh    ( unsigned char *in, int insz, unsigned char *out, int outsz );
extern int unlzx    ( unsigned char *in, int insz, unsigned char *out, int outsz );
extern int undmc    ( unsigned char *in, int insz, unsigned char *out, int outsz );
extern int unq3huff ( unsigned char *in, int insz, unsigned char *out, int outsz );
extern int unshrink ( unsigned char *in, int insz, unsigned char *out, int outsz );
extern int yuke_bpe ( unsigned char *in, int insz, unsigned char *out, int outsz, int fill_outsz );
extern int splay_trees ( unsigned char *in, int insz, unsigned char *out, int outsz, int do_compress );
extern int fastlz_decompress ( const void *input, int length, void *output, int maxout );
extern int shrinker_decompress ( void *in, void *out, int size );
extern int smaz_decompress ( char *in, int inlen, char *out, int outlen );
extern int lz4x ( unsigned char *in, int len, unsigned char *out, int dec_enc );
extern int lzfx_decompress ( const void *ibuf, unsigned int ilen, void *obuf, unsigned int *olen );

// lzmat_dec.c exposes two overloads guarded by macros; the 4-arg form is the
// one QuickBMS links (MP_U32 == unsigned int, MP_U8 == unsigned char).
extern int lzmat_decode ( unsigned char *pbOut, unsigned int *pcbOut, unsigned char *pbIn, unsigned int cbIn );

extern int unsixpack ( unsigned char *in, int insz, unsigned char *out, int outsz );
extern unsigned int unlzw  ( unsigned char *outbuff, unsigned int maxsize, unsigned char *in, unsigned int insize );
extern unsigned int unlzwx ( unsigned char *outbuff, unsigned int maxsize, unsigned char *in, unsigned int insize );

// codecs/qbms_lzh8.c -- upstream compression/lzh8_dec.c, function renamed to
// avoid colliding with the FILE*-based analyze_LZH8() in src/lzh8_dec.c.
extern int qbms_analyze_LZH8 ( unsigned char *infile, unsigned char *outbuf, int uncompressed_length );

// scexpand.c: QuickBMS calls this for COMTYPE SCPACK / SCPACK0.  The last
// argument selects the pair table: 1 => first embedded SCPACK table,
// 0 => table stored in the first 256 bytes of the stream.
extern int strexpand ( unsigned char *dest, unsigned char *source, int sourcelen,
	int maxlen, unsigned char *input_pairtable, int input_pairtable_default );

// refpack.c: safe EA RefPack decoder.  skip_header == 0 => parse the 10 FB
// stream header; success is a 0 return with the byte count in *written.
extern int refpack_decompress_safe ( const unsigned char *indata, size_t insize,
	size_t *bytes_read_out, unsigned char *outdata, size_t outsize,
	size_t *bytes_written_out, unsigned int *compressed_size_out,
	unsigned int *decompressed_size_out, int skip_header );

//
// ---------------------------------------------------------------------------
// COMTYPE table.  Each name maps to a codec kind; the dispatch below turns a
// kind into the right call.  Add rows here (and a case in run_codec) to
// widen coverage.
// ---------------------------------------------------------------------------
//
enum {
	K_LZSS, K_LZSS0, K_LZARI, K_LZH, K_LZX, K_DMC, K_Q3HUFF, K_SHRINK,
	K_YUKE_BPE, K_SPLAY, K_FASTLZ, K_SHRINKER, K_SMAZ, K_LZ4X, K_LZFX,
	K_LZMAT, K_SIXPACK, K_LZW, K_LZWX, K_LZH8, K_SCPACK, K_SCPACK0,
	K_REFPACK
};

typedef struct {
	const char *name;
	int         kind;
} comtype_row_t;

static const comtype_row_t comtype_table[] = {
	{ "lzss",           K_LZSS     },
	{ "lzss0",          K_LZSS0    },
	{ "lzari",          K_LZARI    },
	{ "lzh",            K_LZH      },
	{ "lzhuf",          K_LZH      },
	{ "lzx",            K_LZX      },
	{ "dmc",            K_DMC      },
	{ "q3huff",         K_Q3HUFF   },
	{ "shrink",         K_SHRINK   },
	{ "pkware_shrink",  K_SHRINK   },
	{ "yuke_bpe",       K_YUKE_BPE },
	{ "comprlib_splay", K_SPLAY    },
	{ "splay",          K_SPLAY    },
	{ "fastlz",         K_FASTLZ   },
	{ "shrinker",       K_SHRINKER },
	{ "smaz",           K_SMAZ     },
	{ "lz4x",           K_LZ4X     },
	{ "lzfx",           K_LZFX     },
	{ "lzmat",          K_LZMAT    },
	{ "sixpack",        K_SIXPACK  },
	{ "unlzw",          K_LZW      },
	{ "lzw",            K_LZW      },
	{ "unlzwx",         K_LZWX     },
	{ "lzwx",           K_LZWX     },
	{ "lzh8",           K_LZH8     },
	{ "nlzh8",          K_LZH8     },
	{ "scpack",         K_SCPACK   },
	{ "scpack0",        K_SCPACK0  },
	{ "refpack",        K_REFPACK  },
};

#define COMTYPE_TABLE_LEN ( (int)(sizeof(comtype_table)/sizeof(comtype_table[0])) )

static int lookup_kind ( const char *comtype )
{
	if ( !comtype )
		return -1;
	for ( int i = 0; i < COMTYPE_TABLE_LEN; i++ )
		if ( !strcasecmp ( comtype, comtype_table[i].name ) )
			return comtype_table[i].kind;
	return -1;
}

//
// Run one codec into a caller-provided output buffer.  Returns the number of
// bytes produced, or a value < 0 on failure.  A return value == dstsz is
// treated by the caller as "buffer may have been too small".
//
static long run_codec ( int kind, const unsigned char *src, unsigned int insz,
	unsigned char *dst, unsigned int dstsz )
{
	// Codecs take non-const pointers but do not write through the input.
	unsigned char *in = (unsigned char *) src;

	switch ( kind )
	{
		case K_LZSS:
			return unlzss ( in, (int) insz, dst, (int) dstsz, NULL );

		case K_LZSS0:
			return unlzss ( in, (int) insz, dst, (int) dstsz,
				(unsigned char *) "12 4 2 2 0" );

		case K_LZARI:
			return unlzari ( in, (int) insz, dst, (int) dstsz );

		case K_LZH:
			return unlzh ( in, (int) insz, dst, (int) dstsz );

		case K_LZX:
			return unlzx ( in, (int) insz, dst, (int) dstsz );

		case K_DMC:
			return undmc ( in, (int) insz, dst, (int) dstsz );

		case K_Q3HUFF:
			return unq3huff ( in, (int) insz, dst, (int) dstsz );

		case K_SHRINK:
			return unshrink ( in, (int) insz, dst, (int) dstsz );

		case K_YUKE_BPE:
			return yuke_bpe ( in, (int) insz, dst, (int) dstsz, 1 );

		case K_SPLAY:
			return splay_trees ( in, (int) insz, dst, (int) dstsz, 0 );

		case K_FASTLZ:
		{
			int n = fastlz_decompress ( in, (int) insz, dst, (int) dstsz );
			return n > 0 ? n : -1;
		}

		case K_SHRINKER:
			return shrinker_decompress ( in, dst, (int) dstsz );

		case K_SMAZ:
			return smaz_decompress ( (char *) in, (int) insz,
				(char *) dst, (int) dstsz );

		case K_LZ4X:
		{
			// lz4x() does not bound-check the destination; only run it when
			// the buffer is comfortably larger than any plausible output.
			long n = lz4x ( in, (int) insz, dst, 0 );
			return n > 0 ? n : -1;
		}

		case K_LZFX:
		{
			unsigned int olen = dstsz;
			int rc = lzfx_decompress ( in, insz, dst, &olen );
			return rc == 0 ? (long) olen : -1;
		}

		case K_LZMAT:
		{
			unsigned int olen = dstsz;
			int rc = lzmat_decode ( dst, &olen, in, insz );
			return rc == 0 ? (long) olen : -1;
		}

		case K_SIXPACK:
			return unsixpack ( in, (int) insz, dst, (int) dstsz );

		case K_LZW:
		{
			unsigned int n = unlzw ( dst, dstsz, in, insz );
			return n > 0 ? (long) n : -1;
		}

		case K_LZWX:
		{
			// unlzwx() consumes a two-byte trailer; guard the subtraction.
			unsigned int n;
			if ( insz < 2 )
				return -1;
			n = unlzwx ( dst, dstsz, in, insz );
			return n > 0 ? (long) n : -1;
		}

		case K_LZH8:
			return qbms_analyze_LZH8 ( in, dst, (int) dstsz );

		case K_SCPACK:
			return strexpand ( dst, in, (int) insz, (int) dstsz, NULL, 1 );

		case K_SCPACK0:
			return strexpand ( dst, in, (int) insz, (int) dstsz, NULL, 0 );

		case K_REFPACK:
		{
			size_t written = 0;
			int rc = refpack_decompress_safe ( in, insz, NULL, dst, dstsz,
				&written, NULL, NULL, 0 );
			return rc == 0 ? (long) written : -1;
		}
	}
	return -1;
}

//
// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------
//

#define QBMS_MAX_OUTPUT  ( 256u * 1024u * 1024u )

int QbmsHandlesCompType ( const char *comtype )
{
	return lookup_kind ( comtype ) >= 0;
}

int QbmsDecompress ( const char *comtype, const unsigned char *src,
	unsigned int comp_size, unsigned int hint_size,
	unsigned char **out, unsigned int *out_size )
{
	int kind = lookup_kind ( comtype );
	if ( kind < 0 || !src || !out || !out_size )
		return 0;

	// Initial guess for the decompressed size.  The hint (a CLOG operand)
	// is used when present; otherwise start from a multiple of the input
	// and grow on demand.  lz4x needs extra head-room because it never
	// checks the output bound.
	unsigned int cap;
	if ( hint_size )
		cap = hint_size + 4096;
	else if ( comp_size < 4096 )
		cap = 65536;
	else
		cap = comp_size * 16u + 65536u;
	if ( kind == K_LZ4X && cap < comp_size * 64u )
		cap = comp_size * 64u + 65536u;

	for ( int attempt = 0; attempt < 24; attempt++ )
	{
		if ( cap > QBMS_MAX_OUTPUT )
			cap = QBMS_MAX_OUTPUT;

		unsigned char *dst = malloc ( cap ? cap : 1 );
		if ( !dst )
			return 0;

		long n = run_codec ( kind, src, comp_size, dst, cap );

		if ( n > 0 && (unsigned long) n <= cap )
		{
			// A result that exactly fills the buffer may have been
			// truncated; grow once more unless we're already at the cap.
			if ( (unsigned int) n == cap && cap < QBMS_MAX_OUTPUT )
			{
				free ( dst );
				cap *= 2u;
				continue;
			}
			*out = dst;
			*out_size = (unsigned int) n;
			return 1;
		}

		free ( dst );
		if ( cap >= QBMS_MAX_OUTPUT )
			return 0;
		cap *= 2u;
	}
	return 0;
}

const char * QbmsCompTypeList ( void )
{
	static char buf[512];
	if ( !buf[0] )
	{
		size_t pos = 0;
		for ( int i = 0; i < COMTYPE_TABLE_LEN; i++ )
		{
			int w = snprintf ( buf + pos, sizeof (buf) - pos,
				"%s%s", i ? " " : "", comtype_table[i].name );
			if ( w <= 0 || (size_t) w >= sizeof (buf) - pos )
				break;
			pos += w;
		}
	}
	return buf;
}
