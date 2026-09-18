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
	if (b->size + needed <= b->alloc)
		return true;
	uint nalloc = b->alloc ? b->alloc * 2 : 1024;
	while (nalloc < b->size + needed)
		nalloc *= 2;
	u8 *ndata = REALLOC (b->data, nalloc);
	if (!ndata)
		return false;
	b->data = ndata;
	b->alloc = nalloc;
	return true;
}

static inline bool dyn_write (dyn_buf_t *b, const void *src, uint len)
{
	if (!dyn_reserve (b, len))
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
	if (num_files < 1 || num_files > 4000)
		return false;

	const u64 tab_size = 4 + (u64)num_files * 4;
	if (tab_size > size)
		return false;

	const u32 first_off = rd_be32 (data + 4);
	if (first_off < tab_size || first_off >= size)
		return false;

	u32 prev_off = first_off;
	for (u32 i = 1; i < num_files; i++)
	{
		const u32 cur_off = rd_be32 (data + 4 + i * 4);
		if (cur_off < prev_off || cur_off >= size)
			return false;
		prev_off = cur_off;
	}

	// Verify first entry's header
	if (first_off + 8 > size)
		return false;
	const u32 decomp_size = rd_be32 (data + first_off);
	const u32 comp_type = rd_be32 (data + first_off + 4);

	if (comp_type > 7 || comp_type == 6)
		return false;
	if (decomp_size > 0x10000000) // max 256MB
		return false;

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
	return (ret == Z_OK || ret == Z_STREAM_END) ? ERR_OK : ERR_WARNING;
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
	if (!dest || !dest_size || !src)
		return ERR_INVALID_DATA;

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
			dyn_write (&out, code_buf, code_buf_ptr);
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

	if (code_buf_ptr > 1)
		dyn_write (&out, code_buf, code_buf_ptr);

	FREE (sp);
	*dest = out.data;
	*dest_size = out.size;
	return ERR_OK;
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
	if (!dest || !dest_size || !src)
		return ERR_INVALID_DATA;

	dyn_buf_t out;
	dyn_init (&out);

	// First 4 bytes: decompressed size (big endian)
	dyn_write_be32 (&out, src_len);

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
			dyn_write_be32 (&out, code_word);
			dyn_write (&out, dst_block, dst_block_len);
			code_word = 0;
			bit_count = 0;
			dst_block_len = 0;
		}
	}
	if (bit_count > 0)
	{
		dyn_write_be32 (&out, code_word);
		dyn_write (&out, dst_block, dst_block_len);
	}

	*dest = out.data;
	*dest_size = out.size;
	return ERR_OK;
}

enumError CompressMPBIN_RLE (u8 **dest, uint *dest_size, const u8 *src, uint src_len)
{
	if (!dest || !dest_size || !src)
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
			dyn_write_u8 (&out, (u8)run);
			dyn_write_u8 (&out, src[pos]);
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
			dyn_write_u8 (&out, (u8)(0x80 | lit));
			dyn_write (&out, src + pos, lit);
			pos += lit;
		}
	}
	*dest = out.data;
	*dest_size = out.size;
	return ERR_OK;
}

enumError CompressMPBIN_Inflate (u8 **dest, uint *dest_size, const u8 *src, uint src_len)
{
	if (!dest || !dest_size || !src)
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
	deflate (&strm, Z_FINISH);
	u32 comp_len = (u32)strm.total_out;
	deflateEnd (&strm);

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
	if (!entries || !n_entries || !data || !IsMPBIN (data, size))
		return ERR_INVALID_DATA;

	const u32 num_files = rd_be32 (data);
	*entries = 0;
	*n_entries = 0;

	// Total entries: num_files + 1 (for setup file)
	nintendo_sarc_entry_t *out = CALLOC (num_files + 1, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	dyn_buf_t setup;
	dyn_init (&setup);
	char line[256];
	snprintf (line, sizeof (line), "# MPBIN setup manifest\n# total_files = %u\n\n", num_files);
	dyn_write (&setup, line, (uint)strlen (line));

	uint out_cnt = 0;
	for (u32 i = 0; i < num_files; i++)
	{
		const u32 file_off = rd_be32 (data + 4 + i * 4);
		const u32 next_off = (i + 1 < num_files) ? rd_be32 (data + 4 + (i + 1) * 4) : size;
		if (file_off + 8 > size || next_off > size || file_off >= next_off)
			continue;

		const u32 decomp_size = rd_be32 (data + file_off);
		const u32 comp_type = rd_be32 (data + file_off + 4);
		const u8 *payload = data + file_off + 8;
		const uint payload_size = next_off - file_off - 8;

		u8 *uncomp = 0;
		if (decomp_size > 0)
		{
			uncomp = MALLOC (decomp_size);
			if (!uncomp)
				continue;

			enumError err = ERR_OK;
			switch (comp_type)
			{
				case MPBIN_COMP_NONE:
					memcpy (uncomp, payload, decomp_size <= payload_size ? decomp_size : payload_size);
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
					memcpy (uncomp, payload, decomp_size <= payload_size ? decomp_size : payload_size);
					break;
			}
			(void)err;
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
		dyn_write (&setup, line, (uint)strlen (line));

		OwnedEntryAdd (out, out_cnt++, fname, uncomp ? uncomp : (const u8 *)"", decomp_size);
		FREE (uncomp);
	}

	// Add setup file as last entry
	OwnedEntryAdd (out, out_cnt++, MPBIN_SETUP_FILE, setup.data, setup.size);
	dyn_free (&setup);

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
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
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	// Parse mpbin-setup.txt if present among entries
	uint comp_types[4096];
	memset (comp_types, 0, sizeof (comp_types));
	for (int i = 0; i < 4096; i++)
		comp_types[i] = MPBIN_COMP_LZSS; // default LZSS

	uint real_file_count = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (entries[i].name && !strcmp (leaf_name (entries[i].name), MPBIN_SETUP_FILE))
		{
			// Parse lines
			char *copy = MALLOC (entries[i].size + 1);
			if (copy)
			{
				memcpy (copy, entries[i].data, entries[i].size);
				copy[entries[i].size] = 0;
				char *line = strtok (copy, "\r\n");
				while (line)
				{
					uint idx = 0, ctype = 0;
					if (sscanf (line, "file%u\tcompress_type=%u", &idx, &ctype) == 2)
					{
						if (idx < 4096)
							comp_types[idx] = ctype;
					}
					else if (sscanf (line, "compress_type=%u:", &ctype) == 1)
					{
						// Alternative format: compress_type=X: filename
						if (real_file_count < 4096)
							comp_types[real_file_count] = ctype;
					}
					line = strtok (NULL, "\r\n");
				}
				FREE (copy);
			}
		}
		else if (entries[i].name && OwnedNameOk (entries[i].name))
		{
			real_file_count++;
		}
	}

	if (!real_file_count)
		return ERR_NOTHING_TO_DO;

	// Compress subfiles
	u8 **comp_data = CALLOC (real_file_count, sizeof (u8 *));
	uint *comp_sizes = CALLOC (real_file_count, sizeof (uint));
	uint *uncomp_sizes = CALLOC (real_file_count, sizeof (uint));
	uint *entry_comp_types = CALLOC (real_file_count, sizeof (uint));

	uint file_idx = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (!entries[i].name || !strcmp (leaf_name (entries[i].name), MPBIN_SETUP_FILE) || !OwnedNameOk (entries[i].name))
			continue;

		uint ctype = comp_types[file_idx];
		entry_comp_types[file_idx] = ctype;
		uncomp_sizes[file_idx] = entries[i].size;

		u8 *cd = 0;
		uint cs = 0;
		switch (ctype)
		{
			case MPBIN_COMP_NONE:
				cd = MALLOC (entries[i].size);
				if (cd) { memcpy (cd, entries[i].data, entries[i].size); cs = entries[i].size; }
				break;
			case MPBIN_COMP_LZSS:
				CompressMPBIN_LZSS (&cd, &cs, entries[i].data, entries[i].size);
				break;
			case MPBIN_COMP_SLIDE:
			case MPBIN_COMP_FSLIDE_ALT:
			case MPBIN_COMP_FSLIDE:
				CompressMPBIN_Slide (&cd, &cs, entries[i].data, entries[i].size);
				break;
			case MPBIN_COMP_RLE:
				CompressMPBIN_RLE (&cd, &cs, entries[i].data, entries[i].size);
				break;
			case MPBIN_COMP_INFLATE:
				CompressMPBIN_Inflate (&cd, &cs, entries[i].data, entries[i].size);
				break;
			default:
				CompressMPBIN_LZSS (&cd, &cs, entries[i].data, entries[i].size);
				break;
		}
		comp_data[file_idx] = cd;
		comp_sizes[file_idx] = cs;
		file_idx++;
	}

	// Calculate header and offsets
	dyn_buf_t out;
	dyn_init (&out);

	dyn_write_be32 (&out, real_file_count);

	// Offsets table placeholder
	uint tab_off = out.size;
	for (uint i = 0; i < real_file_count; i++)
		dyn_write_be32 (&out, 0);

	// Align to 32 bytes before first file payload
	while (out.size % 32 != 0)
		dyn_write_u8 (&out, 0);

	// Write files
	for (uint i = 0; i < real_file_count; i++)
	{
		uint cur_off = out.size;
		wr_be32 (out.data + tab_off + i * 4, cur_off);

		dyn_write_be32 (&out, uncomp_sizes[i]);
		dyn_write_be32 (&out, entry_comp_types[i]);
		if (comp_data[i] && comp_sizes[i])
			dyn_write (&out, comp_data[i], comp_sizes[i]);

		// Align files to 4 bytes
		while (out.size % 4 != 0)
			dyn_write_u8 (&out, 0);
	}

	// Cleanup
	for (uint i = 0; i < real_file_count; i++)
		FREE (comp_data[i]);
	FREE (comp_data);
	FREE (comp_sizes);
	FREE (uncomp_sizes);
	FREE (entry_comp_types);

	*dest = out.data;
	*dest_size = out.size;
	return ERR_OK;
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
