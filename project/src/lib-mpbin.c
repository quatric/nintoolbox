// SPDX-License-Identifier: GPL-2.0+
#include "lib-mpbin.h"
#include "lib-std.h"
#include "file-type.h"
#include "lib-atb.h"
#include "lib-ptd.h"
#include "lib-archive-util.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

ccp MPBIN_SETUP_FILE = "mpbin-setup.txt";

#define MPBIN_MAX_FILES 4000
#define MPBIN_MAX_FILE_SIZE 0x10000000u

#define WINDOW_START 958
#define WINDOW_SIZE 1024
#define SHORTEST_MATCH_LENGTH 3

//-----------------------------------------------------------------------------
// Dynamic buffer helper
//-----------------------------------------------------------------------------

typedef struct dyn_buf_t
{
	u8 *data;
	uint size;
	uint alloc;
} dyn_buf_t;

static inline void dyn_init (dyn_buf_t *b)
{
	b->data = 0;
	b->size = 0;
	b->alloc = 0;
}

static inline void dyn_free (dyn_buf_t *b)
{
	FREE (b->data);
	b->data = 0;
	b->size = b->alloc = 0;
}

static inline bool dyn_reserve (dyn_buf_t *b, uint needed)
{
	if (needed > UINT_MAX - b->size)
		return false;
	const uint required = b->size + needed;
	if (required <= b->alloc)
		return true;
	uint nalloc = b->alloc ? b->alloc : 1024;
	while (nalloc < required)
	{
		if (nalloc > UINT_MAX / 2)
		{
			nalloc = required;
			break;
		}
		nalloc *= 2;
	}
	u8 *ndata = REALLOC (b->data, nalloc);
	if (!ndata)
		return false;
	b->data = ndata;
	b->alloc = nalloc;
	return true;
}

static inline bool dyn_write (dyn_buf_t *b, const void *src, uint len)
{
	if (!len)
		return true;
	if (!src || !dyn_reserve (b, len))
		return false;
	memcpy (b->data + b->size, src, len);
	b->size += len;
	return true;
}

static inline bool dyn_write_u8 (dyn_buf_t *b, u8 val)
{
	return dyn_write (b, &val, 1);
}

static inline bool dyn_write_be32 (dyn_buf_t *b, u32 val)
{
	u8 tmp[4];
	wr_be32 (tmp, val);
	return dyn_write (b, tmp, 4);
}

//-----------------------------------------------------------------------------
// Identification
//-----------------------------------------------------------------------------

bool IsMPBIN (const u8 *data, uint size)
{
	if (!data || size < 16)
		return false;

	const u32 num_files = rd_be32 (data);
	if (num_files < 1 || num_files > MPBIN_MAX_FILES)
		return false;

	const u64 tab_size = 4 + (u64)num_files * 4;
	if (tab_size > size)
		return false;

	// Every member must contain its own complete header and payload. Checking
	// only the first entry lets later slots underflow their payload lengths.
	for (u32 i = 0; i < num_files; i++)
	{
		const u32 off = rd_be32 (data + 4 + i * 4);
		const u32 end = i + 1 < num_files ? rd_be32 (data + 8 + i * 4) : size;
		if (off < tab_size || end > size || (u64)off + 8 > end)
			return false;
		const u32 decomp_size = rd_be32 (data + off);
		const u32 comp_type = rd_be32 (data + off + 4);
		if (comp_type > 7 || comp_type == 6 || decomp_size > MPBIN_MAX_FILE_SIZE)
			return false;
		if (comp_type == MPBIN_COMP_NONE && (u64)off + 8 + decomp_size > end)
			return false;
	}

	return true;
}

// The redundant size and compressed-size fields of inflate members coincide
// with MDR's chunk headers. Prefer MPBIN for this ambiguous shape unless the
// caller has an explicit .mdr filename.
bool IsMPBINInflate (const u8 *data, uint size)
{
	if (!IsMPBIN (data, size))
		return false;
	const uint count = rd_be32 (data);
	for (uint i = 0; i < count; i++)
	{
		const uint off = rd_be32 (data + 4 + i * 4);
		if (rd_be32 (data + off + 4) != MPBIN_COMP_INFLATE)
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Decompressors
//-----------------------------------------------------------------------------

enumError DecompressMPBIN_LZSS (u8 *dst, uint dst_len, const u8 *src, uint src_len)
{
	if (!dst || !src)
		return ERR_INVALID_DATA;

	u8 window[WINDOW_SIZE];
	memset (window, 0, sizeof (window));
	uint win_off = WINDOW_START;
	int code_word = 0;
	uint src_off = 0;
	uint dst_off = 0;

	while (dst_off < dst_len)
	{
		if ((code_word & 0x100) == 0)
		{
			if (src_off >= src_len)
				break;
			code_word = src[src_off++];
			code_word |= 0xff00;
		}

		if (code_word & 1)
		{
			if (src_off >= src_len)
				break;
			u8 val = src[src_off++];
			dst[dst_off++] = val;
			window[win_off] = val;
			win_off = (win_off + 1) % WINDOW_SIZE;
		}
		else
		{
			if (src_off + 1 >= src_len)
				break;
			u8 b1 = src[src_off++];
			u8 b2 = src[src_off++];
			int offset = ((b2 & 0xc0) << 2) | b1;
			int copy_len = (b2 & 0x3f) + SHORTEST_MATCH_LENGTH;

			for (int i = 0; i < copy_len && dst_off < dst_len; i++)
			{
				u8 val = window[(offset + i) % WINDOW_SIZE];
				window[win_off] = val;
				win_off = (win_off + 1) % WINDOW_SIZE;
				dst[dst_off++] = val;
			}
		}
		code_word >>= 1;
	}
	return dst_off == dst_len ? ERR_OK : ERR_WARNING;
}

enumError DecompressMPBIN_Slide (u8 *dst, uint dst_len, const u8 *src, uint src_len)
{
	if (!dst || !src || src_len < 4)
		return ERR_INVALID_DATA;

	uint src_off = 4; // skip 4-byte redundant decompressed size
	uint dst_off = 0;
	u32 code_word = 0;
	int bits_left = 0;

	while (dst_off < dst_len)
	{
		if (bits_left == 0)
		{
			if (src_off + 4 > src_len)
				break;
			code_word = rd_be32 (src + src_off);
			src_off += 4;
			bits_left = 32;
		}

		if (code_word & 0x80000000)
		{
			if (src_off >= src_len)
				break;
			dst[dst_off++] = src[src_off++];
		}
		else
		{
			if (src_off + 2 > src_len)
				break;
			u8 b1 = src[src_off++];
			u8 b2 = src[src_off++];
			int dist_back = (((b1 & 0x0f) << 8) | b2) + 1;
			int copy_len = ((b1 & 0xf0) >> 4) + 2;

			if (copy_len == 2)
			{
				if (src_off >= src_len)
					break;
				copy_len = src[src_off++] + 18;
			}

			for (int i = 0; i < copy_len && dst_off < dst_len; i++)
			{
				u8 val = (dist_back > (int)dst_off) ? 0 : dst[dst_off - dist_back];
				dst[dst_off++] = val;
			}
		}
		code_word <<= 1;
		bits_left--;
	}
	return dst_off == dst_len ? ERR_OK : ERR_WARNING;
}

enumError DecompressMPBIN_RLE (u8 *dst, uint dst_len, const u8 *src, uint src_len)
{
	if (!dst || !src)
		return ERR_INVALID_DATA;

	uint src_off = 0;
	uint dst_off = 0;

	while (dst_off < dst_len && src_off < src_len)
	{
		u8 code = src[src_off++];
		uint len = code & 0x7f;
		if (code & 0x80)
		{
			for (uint i = 0; i < len && dst_off < dst_len && src_off < src_len; i++)
				dst[dst_off++] = src[src_off++];
		}
		else
		{
			if (src_off >= src_len)
				break;
			u8 val = src[src_off++];
			for (uint i = 0; i < len && dst_off < dst_len; i++)
				dst[dst_off++] = val;
		}
	}
	return dst_off == dst_len ? ERR_OK : ERR_WARNING;
}

enumError DecompressMPBIN_Inflate (u8 *dst, uint dst_len, const u8 *src, uint src_len)
{
	if (!dst || !src || src_len < 8)
		return ERR_INVALID_DATA;

	const u32 comp_size = rd_be32 (src + 4);
	const u8 *payload = src + 8;
	const uint available = src_len >= 8 ? src_len - 8 : 0;
	const uint in_size = comp_size > 0 && comp_size <= available ? comp_size : available;

	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = (Bytef *)payload;
	strm.avail_in = in_size;
	strm.next_out = dst;
	strm.avail_out = dst_len;

	if (inflateInit2 (&strm, 15 + 32) != Z_OK)
		return ERR_INVALID_DATA;

	int ret = inflate (&strm, Z_FINISH);
	inflateEnd (&strm);
	return ret == Z_STREAM_END && strm.total_out == dst_len ? ERR_OK : ERR_WARNING;
}

//-----------------------------------------------------------------------------
// Compressors
//-----------------------------------------------------------------------------

#define LZSS_N 1024
#define LZSS_F 66
#define LZSS_THRESHOLD 2
#define LZSS_NIL LZSS_N

typedef struct lzss_tree_t
{
	int lchild[LZSS_N + 1], rchild[LZSS_N + 257], parent[LZSS_N + 1];
	u8 text_buf[LZSS_N + LZSS_F - 1];
	int match_position, match_length;
} lzss_tree_t;

static void lzss_init (lzss_tree_t *sp)
{
	memset (sp, 0, sizeof (*sp));
	for (int i = 0; i < LZSS_N - LZSS_F; i++)
		sp->text_buf[i] = '\0';
	for (int i = LZSS_N + 1; i <= LZSS_N + 256; i++)
		sp->rchild[i] = LZSS_NIL;
	for (int i = 0; i < LZSS_N; i++)
		sp->parent[i] = LZSS_NIL;
}

static void lzss_insert (lzss_tree_t *sp, int r)
{
	int p = LZSS_N + 1 + sp->text_buf[r];
	sp->rchild[r] = sp->lchild[r] = LZSS_NIL;
	sp->match_length = 0;
	int cmp = 1;
	for (;;)
	{
		if (cmp >= 0)
		{
			if (sp->rchild[p] != LZSS_NIL)
				p = sp->rchild[p];
			else
			{
				sp->rchild[p] = r;
				sp->parent[r] = p;
				return;
			}
		}
		else
		{
			if (sp->lchild[p] != LZSS_NIL)
				p = sp->lchild[p];
			else
			{
				sp->lchild[p] = r;
				sp->parent[r] = p;
				return;
			}
		}
		int i;
		for (i = 1; i < LZSS_F; i++)
		{
			cmp = sp->text_buf[r + i] - sp->text_buf[p + i];
			if (cmp != 0)
				break;
		}
		if (i > sp->match_length)
		{
			sp->match_position = p;
			sp->match_length = i;
			if (sp->match_length >= LZSS_F)
				break;
		}
	}
	sp->parent[r] = sp->parent[p];
	sp->lchild[r] = sp->lchild[p];
	sp->rchild[r] = sp->rchild[p];
	sp->parent[sp->lchild[p]] = r;
	sp->parent[sp->rchild[p]] = r;
	if (sp->rchild[sp->parent[p]] == p)
		sp->rchild[sp->parent[p]] = r;
	else
		sp->lchild[sp->parent[p]] = r;
	sp->parent[p] = LZSS_NIL;
}

static void lzss_delete (lzss_tree_t *sp, int p)
{
	int q;
	if (sp->parent[p] == LZSS_NIL)
		return;
	if (sp->rchild[p] == LZSS_NIL)
		q = sp->lchild[p];
	else if (sp->lchild[p] == LZSS_NIL)
		q = sp->rchild[p];
	else
	{
		q = sp->lchild[p];
		if (sp->rchild[q] != LZSS_NIL)
		{
			do
			{
				q = sp->rchild[q];
			} while (sp->rchild[q] != LZSS_NIL);
			sp->rchild[sp->parent[q]] = sp->lchild[q];
			sp->parent[sp->lchild[q]] = sp->parent[q];
			sp->lchild[q] = sp->lchild[p];
			sp->parent[sp->lchild[p]] = q;
		}
		sp->rchild[q] = sp->rchild[p];
		sp->parent[sp->rchild[p]] = q;
	}
	sp->parent[q] = sp->parent[p];
	if (sp->rchild[sp->parent[p]] == p)
		sp->rchild[sp->parent[p]] = q;
	else
		sp->lchild[sp->parent[p]] = q;
	sp->parent[p] = LZSS_NIL;
}

enumError CompressMPBIN_LZSS (u8 **dest, uint *dest_size, const u8 *src, uint src_len)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if ((!src && src_len) || src_len > MPBIN_MAX_FILE_SIZE)
		return ERR_INVALID_DATA;

	if (!src_len)
		return ERR_OK;

	lzss_tree_t *sp = CALLOC (1, sizeof (*sp));
	if (!sp)
		return ERR_CANT_CREATE;
	lzss_init (sp);

	dyn_buf_t out;
	dyn_init (&out);

	u8 code_buf[17];
	code_buf[0] = 0;
	u8 mask = 1;
	int code_buf_ptr = 1;

	int s = 0;
	int r = LZSS_N - LZSS_F;
	uint in_pos = 0;
	int len = 0;

	for (len = 0; len < LZSS_F && in_pos < src_len; len++)
		sp->text_buf[r + len] = src[in_pos++];
	for (int i = 1; i <= LZSS_F; i++)
		lzss_insert (sp, r - i);
	lzss_insert (sp, r);

	do
	{
		if (sp->match_length > len)
			sp->match_length = len;
		if (sp->match_length <= LZSS_THRESHOLD)
		{
			sp->match_length = 1;
			code_buf[0] |= mask;
			code_buf[code_buf_ptr++] = sp->text_buf[r];
		}
		else
		{
			code_buf[code_buf_ptr++] = (u8)sp->match_position;
			code_buf[code_buf_ptr++] = (u8)(((sp->match_position >> 2) & 0xc0) | (sp->match_length - (LZSS_THRESHOLD + 1)));
		}
		mask <<= 1;
		if (mask == 0)
		{
			if (!dyn_write (&out, code_buf, code_buf_ptr))
				goto fail;
			code_buf[0] = 0;
			code_buf_ptr = 1;
			mask = 1;
		}
		int last_match_length = sp->match_length;
		int i;
		for (i = 0; i < last_match_length && in_pos < src_len; i++)
		{
			lzss_delete (sp, s);
			u8 c = src[in_pos++];
			sp->text_buf[s] = c;
			if (s < LZSS_F - 1)
				sp->text_buf[s + LZSS_N] = c;
			s = (s + 1) % LZSS_N;
			r = (r + 1) % LZSS_N;
			lzss_insert (sp, r);
		}
		while (i++ < last_match_length)
		{
			lzss_delete (sp, s);
			s = (s + 1) % LZSS_N;
			r = (r + 1) % LZSS_N;
			if (--len)
				lzss_insert (sp, r);
		}
	} while (len > 0);

	if (code_buf_ptr > 1 && !dyn_write (&out, code_buf, code_buf_ptr))
		goto fail;

	FREE (sp);
	*dest = out.data;
	*dest_size = out.size;
	return ERR_OK;

fail:
	FREE (sp);
	dyn_free (&out);
	return ERR_OUT_OF_MEMORY;
}

static u32 slide_simple_enc (const u8 *src, uint size, uint pos, u32 *pMatchPos)
{
	int start = (int)pos - 0x1000;
	if (start < 0)
		start = 0;
	uint max_match = 0;
	u32 best_pos = 0;

	for (int i = start; i < (int)pos; i++)
	{
		uint match = 0;
		while (pos + match < size && src[i + match] == src[pos + match] && match < (0xff + 0x12))
			match++;
		if (match > max_match)
		{
			max_match = match;
			best_pos = i;
			if (max_match >= (0xff + 0x12))
				break;
		}
	}
	*pMatchPos = best_pos;
	return max_match < 3 ? 1 : max_match;
}

static u32 slide_nintendo_enc (const u8 *src, uint size, uint pos, u32 *pMatchPos, int *prevFlag, u32 *prevMatchPos, u32 *prevNumBytes)
{
	if (*prevFlag == 1)
	{
		*pMatchPos = *prevMatchPos;
		*prevFlag = 0;
		return *prevNumBytes;
	}
	*prevFlag = 0;
	u32 matchPos = 0;
	u32 numBytes = slide_simple_enc (src, size, pos, &matchPos);
	*pMatchPos = matchPos;

	if (numBytes >= 3 && pos + 1 < size)
	{
		u32 nextMatchPos = 0;
		u32 numBytes1 = slide_simple_enc (src, size, pos + 1, &nextMatchPos);
		if (numBytes1 >= numBytes + 2)
		{
			*prevMatchPos = nextMatchPos;
			*prevNumBytes = numBytes1;
			*prevFlag = 1;
			numBytes = 1;
		}
	}
	return numBytes;
}

enumError CompressMPBIN_Slide (u8 **dest, uint *dest_size, const u8 *src, uint src_len)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if ((!src && src_len) || src_len > MPBIN_MAX_FILE_SIZE)
		return ERR_INVALID_DATA;

	dyn_buf_t out;
	dyn_init (&out);

	// First 4 bytes: decompressed size (big endian)
	if (!dyn_write_be32 (&out, src_len))
		goto fail;

	uint src_pos = 0;
	u8 dst_block[96];
	uint dst_block_len = 0;
	u32 code_word = 0;
	uint bit_count = 0;
	int prev_flag = 0;
	u32 prev_match = 0;
	u32 prev_num = 0;

	while (src_pos < src_len)
	{
		u32 match_pos = 0;
		u32 num_bytes = slide_nintendo_enc (src, src_len, src_pos, &match_pos, &prev_flag, &prev_match, &prev_num);

		if (num_bytes < 3)
		{
			dst_block[dst_block_len++] = src[src_pos++];
			code_word |= (0x80000000 >> bit_count);
		}
		else
		{
			u32 dist = src_pos - match_pos - 1;
			if (num_bytes >= 0x12)
			{
				if (num_bytes > 0xff + 0x12)
					num_bytes = 0xff + 0x12;
				dst_block[dst_block_len++] = (u8)(dist >> 8);
				dst_block[dst_block_len++] = (u8)(dist & 0xff);
				dst_block[dst_block_len++] = (u8)(num_bytes - 0x12);
			}
			else
			{
				dst_block[dst_block_len++] = (u8)(((num_bytes - 2) << 4) | (dist >> 8));
				dst_block[dst_block_len++] = (u8)(dist & 0xff);
			}
			src_pos += num_bytes;
		}
		bit_count++;
		if (bit_count == 32)
		{
			if (!dyn_write_be32 (&out, code_word))
				goto fail;
			if (!dyn_write (&out, dst_block, dst_block_len))
				goto fail;
			code_word = 0;
			bit_count = 0;
			dst_block_len = 0;
		}
	}
	if (bit_count > 0)
	{
		if (!dyn_write_be32 (&out, code_word))
			goto fail;
		if (!dyn_write (&out, dst_block, dst_block_len))
			goto fail;
	}

	*dest = out.data;
	*dest_size = out.size;
	return ERR_OK;

fail:
	dyn_free (&out);
	return ERR_OUT_OF_MEMORY;
}

enumError CompressMPBIN_RLE (u8 **dest, uint *dest_size, const u8 *src, uint src_len)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if ((!src && src_len) || src_len > MPBIN_MAX_FILE_SIZE)
		return ERR_INVALID_DATA;

	dyn_buf_t out;
	dyn_init (&out);

	uint pos = 0;
	while (pos < src_len)
	{
		// Check for repeating run
		uint run = 1;
		while (pos + run < src_len && src[pos + run] == src[pos] && run < 127)
			run++;

		if (run >= 3 || pos + run == src_len)
		{
			if (!dyn_write_u8 (&out, (u8)run))
				goto fail;
			if (!dyn_write_u8 (&out, src[pos]))
				goto fail;
			pos += run;
		}
		else
		{
			// Literal run
			uint lit = 0;
			while (pos + lit < src_len && lit < 127)
			{
				if (pos + lit + 2 < src_len && src[pos + lit] == src[pos + lit + 1] && src[pos + lit] == src[pos + lit + 2])
					break;
				lit++;
			}
			if (!dyn_write_u8 (&out, (u8)(0x80 | lit)))
				goto fail;
			if (!dyn_write (&out, src + pos, lit))
				goto fail;
			pos += lit;
		}
	}
	*dest = out.data;
	*dest_size = out.size;
	return ERR_OK;

fail:
	dyn_free (&out);
	return ERR_OUT_OF_MEMORY;
}

enumError CompressMPBIN_Inflate (u8 **dest, uint *dest_size, const u8 *src, uint src_len)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if ((!src && src_len) || src_len > MPBIN_MAX_FILE_SIZE)
		return ERR_INVALID_DATA;

	uLongf bound = compressBound (src_len);
	u8 *comp = MALLOC (8 + bound);
	if (!comp)
		return ERR_CANT_CREATE;

	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = (Bytef *)src;
	strm.avail_in = src_len;
	strm.next_out = comp + 8;
	strm.avail_out = bound;

	if (deflateInit (&strm, Z_BEST_COMPRESSION) != Z_OK)
	{
		FREE (comp);
		return ERR_INVALID_DATA;
	}
	const int ret = deflate (&strm, Z_FINISH);
	u32 comp_len = (u32)strm.total_out;
	deflateEnd (&strm);
	if (ret != Z_STREAM_END)
	{
		FREE (comp);
		return ERR_CANT_CREATE;
	}

	wr_be32 (comp, src_len);
	wr_be32 (comp + 4, comp_len);

	*dest = comp;
	*dest_size = 8 + comp_len;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Scan / Extract MPBIN
//-----------------------------------------------------------------------------

enumError ScanMPBIN (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries)
		return ERR_INVALID_DATA;
	*entries = 0;
	*n_entries = 0;
	if (!IsMPBIN (data, size))
		return ERR_INVALID_DATA;

	const u32 num_files = rd_be32 (data);

	// Total entries: num_files + 1 (for setup file)
	nintendo_sarc_entry_t *out = CALLOC (num_files + 1, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	dyn_buf_t setup;
	dyn_init (&setup);
	enumError err = ERR_OUT_OF_MEMORY;
	uint out_cnt = 0;
	char line[256];
	snprintf (line, sizeof (line), "# MPBIN setup manifest\n# total_files = %u\n\n", num_files);
	if (!dyn_write (&setup, line, (uint)strlen (line)))
		goto fail;
	for (u32 i = 0; i < num_files; i++)
	{
		const u32 file_off = rd_be32 (data + 4 + i * 4);
		const u32 next_off = (i + 1 < num_files) ? rd_be32 (data + 4 + (i + 1) * 4) : size;

		const u32 decomp_size = rd_be32 (data + file_off);
		const u32 comp_type = rd_be32 (data + file_off + 4);
		const u8 *payload = data + file_off + 8;
		const uint payload_size = next_off - file_off - 8;

		u8 *uncomp = 0;
		if (decomp_size > 0)
		{
			uncomp = MALLOC (decomp_size);
			if (!uncomp)
				goto fail;

			err = ERR_OK;
			switch (comp_type)
			{
				case MPBIN_COMP_NONE:
					memcpy (uncomp, payload, decomp_size);
					break;
				case MPBIN_COMP_LZSS:
					err = DecompressMPBIN_LZSS (uncomp, decomp_size, payload, payload_size);
					break;
				case MPBIN_COMP_SLIDE:
				case MPBIN_COMP_FSLIDE_ALT:
				case MPBIN_COMP_FSLIDE:
					err = DecompressMPBIN_Slide (uncomp, decomp_size, payload, payload_size);
					break;
				case MPBIN_COMP_RLE:
					err = DecompressMPBIN_RLE (uncomp, decomp_size, payload, payload_size);
					break;
				case MPBIN_COMP_INFLATE:
					err = DecompressMPBIN_Inflate (uncomp, decomp_size, payload, payload_size);
					break;
				default:
					err = ERR_INVALID_DATA;
					break;
			}
			if (err)
			{
				FREE (uncomp);
				err = ERR_INVALID_DATA;
				goto fail;
			}
		}

		// Detect extension
		ccp ext = "dat";
		if (decomp_size >= 4)
		{
			if (!memcmp (uncomp, "HSFV", 4))
				ext = "hsf";
			else if (IsATB (uncomp, decomp_size))
				ext = "atb";
			else if (IsPTD (uncomp, decomp_size))
				ext = "ptd";
			else if (!memcmp (uncomp, "HBDF", 4) || !memcmp (uncomp, "HSDF", 4))
				ext = "hbdf";
			else
			{
				file_format_t ff = GetByMagicFF (uncomp, decomp_size, 0);
				if (ff > 0 && ff < FF_N)
				{
					ccp fext = GetExtFF (ff, 0);
					if (fext && *fext)
						ext = *fext == '.' ? fext + 1 : fext;
				}
			}
		}

		char fname[64];
		snprintf (fname, sizeof (fname), "file%03u.%s", i, ext);

		snprintf (line, sizeof (line), "file%03u\tcompress_type=%u\tname=%s\n", i, comp_type, fname);
		const bool ok = dyn_write (&setup, line, (uint)strlen (line))
			&& OwnedEntryAdd (out, out_cnt, fname, uncomp ? uncomp : (const u8 *)"", decomp_size);
		FREE (uncomp);
		err = ERR_OUT_OF_MEMORY;
		if (!ok)
			goto fail;
		out_cnt++;
	}

	// Add setup file as last entry
	if (!OwnedEntryAdd (out, out_cnt, MPBIN_SETUP_FILE, setup.data, setup.size))
		goto fail;
	out_cnt++;
	dyn_free (&setup);

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;

fail:
	dyn_free (&setup);
	ResetOwnedEntries (out, out_cnt);
	return err;
}

//-----------------------------------------------------------------------------
// Create MPBIN
//-----------------------------------------------------------------------------

typedef struct mpbin_subfile_info_t
{
	uint index;
	uint comp_type;
	char filename[PATH_MAX];
} mpbin_subfile_info_t;

bool looks_like_mpbin_dir (ccp dir)
{
	if (!dir || !*dir)
		return false;
	char setup_path[PATH_MAX];
	snprintf (setup_path, sizeof (setup_path), "%s/%s", dir, MPBIN_SETUP_FILE);
	struct stat st;
	if (!stat (setup_path, &st) && S_ISREG (st.st_mode))
		return true;
	const size_t len = strlen (dir);
	if (len > 6 && (!strcasecmp (dir + len - 6, ".bin.d") || !strcasecmp (dir + len - 6, ".mpb.d")))
		return true;
	return false;
}

enumError CreateMPBIN (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if (!entries || !n_entries)
		return ERR_INVALID_DATA;

	const nintendo_sarc_entry_t *files[MPBIN_MAX_FILES];
	uint comp_types[MPBIN_MAX_FILES];
	uint real_file_count = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = entries + i;
		if (!e->name || !OwnedNameOk (e->name))
			continue;
		if ((e->size && !e->data) || e->size > MPBIN_MAX_FILE_SIZE)
			return ERR_INVALID_DATA;
		if (!strcmp (leaf_name (e->name), MPBIN_SETUP_FILE))
			continue;
		if (real_file_count == MPBIN_MAX_FILES)
			return ERR_INVALID_DATA;
		files[real_file_count] = e;
		comp_types[real_file_count++] = MPBIN_COMP_LZSS;
	}
	if (!real_file_count)
		return ERR_NOTHING_TO_DO;

	// Resolve the manifest after collecting members, independently of its
	// position in the input list. Named settings refer to actual members.
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = entries + i;
		if (!e->name || !OwnedNameOk (e->name)
			|| strcmp (leaf_name (e->name), MPBIN_SETUP_FILE))
			continue;
		char *copy = MALLOC ((size_t)e->size + 1);
		if (!copy)
			return ERR_OUT_OF_MEMORY;
		if (e->size)
			memcpy (copy, e->data, e->size);
		copy[e->size] = 0;
		char *save = 0;
		for (char *line = strtok_r (copy, "\r\n", &save); line; line = strtok_r (0, "\r\n", &save))
		{
			uint idx = 0, ctype = 0;
			if (sscanf (line, "file%u\tcompress_type=%u", &idx, &ctype) == 2)
			{
				if (idx >= real_file_count || ctype > 7 || ctype == 6)
				{
					FREE (copy);
					return ERR_INVALID_DATA;
				}
				comp_types[idx] = ctype;
			}
			else
			{
				int name_off = 0;
				if (sscanf (line, "compress_type=%u: %n", &ctype, &name_off) != 1 || !name_off)
					continue;
				char *name = line + name_off;
				size_t len = strlen (name);
				while (len && isspace ((unsigned char)name[len-1]))
					name[--len] = 0;
				for (idx = 0; idx < real_file_count; idx++)
					if (!strcmp (files[idx]->name, name))
						break;
				if (idx == real_file_count || ctype > 7 || ctype == 6)
				{
					FREE (copy);
					return ERR_INVALID_DATA;
				}
				comp_types[idx] = ctype;
			}
		}
		FREE (copy);
	}

	u8 **comp_data = CALLOC (real_file_count, sizeof (*comp_data));
	uint *comp_sizes = CALLOC (real_file_count, sizeof (*comp_sizes));
	enumError err = ERR_OUT_OF_MEMORY;
	if (!comp_data || !comp_sizes)
		goto cleanup;

	const uint header_size = (4 + real_file_count * 4 + 31) & ~31u;
	u64 total_size = header_size;
	for (uint i = 0; i < real_file_count; i++)
	{
		const nintendo_sarc_entry_t *e = files[i];
		err = ERR_OK;
		// Empty members require no payload and may have a null data pointer.
		if (e->size)
		{
			switch (comp_types[i])
			{
				case MPBIN_COMP_NONE:
					comp_data[i] = MALLOC (e->size);
					if (!comp_data[i])
						err = ERR_OUT_OF_MEMORY;
					else
					{
						memcpy (comp_data[i], e->data, e->size);
						comp_sizes[i] = e->size;
					}
					break;
				case MPBIN_COMP_LZSS:
					err = CompressMPBIN_LZSS (comp_data + i, comp_sizes + i, e->data, e->size);
					break;
				case MPBIN_COMP_SLIDE:
				case MPBIN_COMP_FSLIDE_ALT:
				case MPBIN_COMP_FSLIDE:
					err = CompressMPBIN_Slide (comp_data + i, comp_sizes + i, e->data, e->size);
					break;
				case MPBIN_COMP_RLE:
					err = CompressMPBIN_RLE (comp_data + i, comp_sizes + i, e->data, e->size);
					break;
				case MPBIN_COMP_INFLATE:
					err = CompressMPBIN_Inflate (comp_data + i, comp_sizes + i, e->data, e->size);
					break;
			}
		}
		if (err)
			goto cleanup;
		total_size = (total_size + 8 + comp_sizes[i] + 3) & ~(u64)3;
		if (total_size > UINT_MAX)
		{
			err = ERR_INVALID_DATA;
			goto cleanup;
		}
	}

	u8 *out = CALLOC ((size_t)total_size, 1);
	if (!out)
	{
		err = ERR_OUT_OF_MEMORY;
		goto cleanup;
	}
	wr_be32 (out, real_file_count);
	uint offset = header_size;
	for (uint i = 0; i < real_file_count; i++)
	{
		wr_be32 (out + 4 + i * 4, offset);
		wr_be32 (out + offset, files[i]->size);
		wr_be32 (out + offset + 4, comp_types[i]);
		if (comp_sizes[i])
			memcpy (out + offset + 8, comp_data[i], comp_sizes[i]);
		offset = (offset + 8 + comp_sizes[i] + 3) & ~3u;
	}
	*dest = out;
	*dest_size = (uint)total_size;
	err = ERR_OK;

cleanup:
	if (comp_data)
		for (uint i = 0; i < real_file_count; i++)
			FREE (comp_data[i]);
	FREE (comp_data);
	FREE (comp_sizes);
	return err;
}

enumError create_mpbin_dir (ccp source, ccp dest)
{
	sarc_build_list_t list;
	memset (&list, 0, sizeof (list));
	enumError err = collect_sarc_dir (&list, source, "");
	if (err)
	{
		reset_sarc_build_list (&list);
		return err;
	}

	u8 *bin = 0;
	uint bin_size = 0;
	err = CreateMPBIN (&bin, &bin_size, list.entry, list.used);
	reset_sarc_build_list (&list);
	if (err || !bin)
	{
		FREE (bin);
		return err ? err : ERR_CANT_CREATE;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, 0);
	if (!err && F.f)
	{
		if (fwrite (bin, 1, bin_size, F.f) != bin_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing MPBIN failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (bin);
	return err;
}
