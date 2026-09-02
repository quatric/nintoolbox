#include "lib-std.h"
#include "lib-lzh8.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

// ============================================================
//  LZH8  (0x40)  --  buffer-based port of hcs's public-domain
//  compressor / decompressor.  Unlike the standalone wlzh8
//  tool the port returns enumError instead of calling exit().
// ============================================================

#define LZH8_LENBITS 9
#define LZH8_DISPBITS 5
#define LZH8_LENCNT (1u << LZH8_LENBITS)
#define LZH8_DISPCNT (1u << LZH8_DISPBITS)

enumError DecodeLZH8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 8)
		return EINVAL;

	uint input_offset = 0;
	u8 pool = 0;
	int bits_left = 0;

	// Read header; accept the WarioWare Snapped 4-byte LE size prefix.
	if (input_offset + 4 > src_size)
		return EINVAL;
	u32 header = rd_le32 (src + input_offset);
	input_offset += 4;
	if ((header & 0xFF) != 0x40)
	{
		if (input_offset + 4 > src_size)
			return EINVAL;
		const u32 next_header = rd_le32 (src + input_offset);
		if ((next_header & 0xFF) != 0x40)
			return EINVAL;
		header = next_header;
		input_offset += 4;
	}
	u64 uncompressed_length = header >> 8;
	if (!uncompressed_length)
	{
		if (input_offset + 4 > src_size)
			return EINVAL;
		uncompressed_length = rd_le32 (src + input_offset);
		input_offset += 4;
	}
	enumError err = AllocOutput (dest, dest_size, uncompressed_length);
	if (err)
		return err;

	u16 length_decode_table[LZH8_LENCNT * 2];
	u8 displen_decode_table[LZH8_DISPCNT * 2];

// MSB-first bit reader over SRC; returns false on end-of-input.
#define LZH8_READ_BITS(n, out)                                                                     \
	do                                                                                             \
	{                                                                                              \
		uint _n = (n), _got = 0;                                                                   \
		u32 _v = 0;                                                                                \
		while (_got < _n)                                                                          \
		{                                                                                          \
			if (!bits_left)                                                                        \
			{                                                                                      \
				if (input_offset >= src_size)                                                      \
					goto invalid;                                                                  \
				pool = src[input_offset++];                                                        \
				bits_left = 8;                                                                     \
			}                                                                                      \
			const uint _take = (uint)bits_left < _n - _got ? bits_left : _n - _got;                \
			_v = _v << _take | (pool >> (bits_left - _take)) & ((1u << _take) - 1);                \
			bits_left -= _take;                                                                    \
			_got += _take;                                                                         \
		}                                                                                          \
		*(out) = _v;                                                                               \
	} while (0)

	// Backreference length decode table (9-bit entries).
	if (input_offset + 2 > src_size)
		goto invalid;
	const u32 length_table_bytes = (rd_le16 (src + input_offset) + 1) * 4;
	input_offset += 2;
	const u32 length_start = input_offset - 2;
	{
		uint i = 1;
		bits_left = 0;
		while (input_offset - length_start < length_table_bytes && i < LZH8_LENCNT * 2)
		{
			u32 v;
			LZH8_READ_BITS (LZH8_LENBITS, &v);
			length_decode_table[i++] = v;
		}
		input_offset = length_start + length_table_bytes;
		if (input_offset > src_size)
			goto invalid;
		bits_left = 0;
	}

	// Displacement length decode table (5-bit entries).
	if (input_offset + 1 > src_size)
		goto invalid;
	const u32 displen_table_bytes = (src[input_offset] + 1) * 4;
	input_offset++;
	const u32 displen_start = input_offset - 1;
	{
		uint i = 1;
		bits_left = 0;
		while (input_offset - displen_start < displen_table_bytes && i < LZH8_DISPCNT * 2)
		{
			u32 v;
			LZH8_READ_BITS (LZH8_DISPBITS, &v);
			displen_decode_table[i++] = v;
		}
		input_offset = displen_start + displen_table_bytes;
		if (input_offset > src_size)
			goto invalid;
		bits_left = 0;
	}

	u8 *out = *dest;
	u64 bytes_decoded = 0;
	while (bytes_decoded < uncompressed_length)
	{
		u32 length_table_offset = 1;
		for (;;)
		{
			u32 next_child;
			LZH8_READ_BITS (1, &next_child);
			const u32 node_payload = length_decode_table[length_table_offset] & 0x7F;
			const u32 next_offset
				= (length_table_offset / 2 * 2) + (node_payload + 1) * 2 + next_child;
			if (next_offset >= LZH8_LENCNT * 2)
				goto invalid;
			if (length_decode_table[length_table_offset] & (0x100u >> next_child))
			{
				u16 length = length_decode_table[next_offset];
				if (length < 0x100)
				{
					if (bytes_decoded >= uncompressed_length)
						goto invalid;
					out[bytes_decoded++] = length;
				}
				else
				{
					length = (length & 0xFF) + 3;
					u32 displen_table_offset = 1;
					for (;;)
					{
						u32 dchild;
						LZH8_READ_BITS (1, &dchild);
						const u32 dpayload = displen_decode_table[displen_table_offset] & 0x7;
						const u32 doffset
							= (displen_table_offset / 2 * 2) + (dpayload + 1) * 2 + dchild;
						if (doffset >= LZH8_DISPCNT * 2)
							goto invalid;
						if (displen_decode_table[displen_table_offset] & (0x10u >> dchild))
						{
							u16 displen = displen_decode_table[doffset];
							u32 displacement = 0;
							if (displen)
							{
								displacement = 1;
								for (u32 i = displen - 1; i; i--)
								{
									u32 bit;
									LZH8_READ_BITS (1, &bit);
									displacement = displacement * 2 | bit;
								}
							}
							if (displacement + 1 > bytes_decoded)
								goto invalid;
							const u64 start = bytes_decoded;
							for (; bytes_decoded < uncompressed_length
								&& bytes_decoded < start + length;
								bytes_decoded++)
								out[bytes_decoded] = out[bytes_decoded - displacement - 1];
							break;
						}
						displen_table_offset = doffset;
					}
				}
				break;
			}
			length_table_offset = next_offset;
		}
	}

	*dest_size = uncompressed_length;
	return ERR_OK;

invalid:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

// ============================================================
//  LZH8 encoder
// ============================================================

struct lzh8_symbol
{
	uint8_t is_reference;
	uint8_t length_or_literal;
	uint16_t offset;
};

struct lzh8_huff_node
{
	int lchild, rchild;
	uint16_t leaf;
	uint16_t subtree_size;
};

struct lzh8_table_ctrl
{
	int node_idx;
	bool placed : 1;
};

struct lzh8_huff_symbol
{
	uint16_t key_len;
	uint32_t key_bits;
};

static uint LZH8_displen_length (uint16_t displacement)
{
	uint bits = 0;
	while (displacement)
	{
		displacement >>= 1;
		bits++;
	}
	return bits;
}

// Growable output buffer written at absolute offsets, mirroring the
// seek()-style put_*_seek() helpers of the original tool.
typedef struct lzh8_wr_t
{
	u8 *data;
	uint size, cap;
} lzh8_wr_t;

static enumError lzh8_wr_ensure (lzh8_wr_t *w, uint need)
{
	if (need <= w->cap)
	{
		if (w->size < need)
			w->size = need;
		return ERR_OK;
	}
	uint cap = w->cap ? w->cap * 2 : 0x4000;
	if (cap < need)
		cap = (need + 0xfff) & ~0xfffu;
	u8 *nd = REALLOC (w->data, cap);
	if (!nd)
		return ERR_CANT_CREATE;
	w->data = nd;
	w->cap = cap;
	if (w->size < need)
		w->size = need;
	return ERR_OK;
}

static enumError lzh8_wr_byte (lzh8_wr_t *w, uint off, u8 v)
{
	enumError err = lzh8_wr_ensure (w, off + 1);
	if (err)
		return err;
	w->data[off] = v;
	return ERR_OK;
}

static enumError lzh8_wr_16_le (lzh8_wr_t *w, uint off, u16 v)
{
	enumError err = lzh8_wr_ensure (w, off + 2);
	if (err)
		return err;
	w->data[off] = v;
	w->data[off + 1] = v >> 8;
	return ERR_OK;
}

static enumError lzh8_wr_32_le (lzh8_wr_t *w, uint off, u32 v)
{
	enumError err = lzh8_wr_ensure (w, off + 4);
	if (err)
		return err;
	w->data[off] = v;
	w->data[off + 1] = v >> 8;
	w->data[off + 2] = v >> 16;
	w->data[off + 3] = v >> 24;
	return ERR_OK;
}

static enumError lzh8_wr_32_be (lzh8_wr_t *w, uint off, u32 v)
{
	enumError err = lzh8_wr_ensure (w, off + 4);
	if (err)
		return err;
	w->data[off] = v >> 24;
	w->data[off + 1] = v >> 16;
	w->data[off + 2] = v >> 8;
	w->data[off + 3] = v;
	return ERR_OK;
}

static enumError lzh8_flush_bits (lzh8_wr_t *w, uint *offset_p, u32 *pool_p, int *written_p)
{
	if (*written_p)
	{
		enumError err = lzh8_wr_32_be (w, *offset_p, *pool_p);
		if (err)
			return err;
		*written_p = 0;
		*pool_p = 0;
		*offset_p += 4;
	}
	return ERR_OK;
}

static enumError lzh8_write_bits (
	lzh8_wr_t *w, uint *offset_p, u32 *pool_p, int *written_p, u32 bits_to_write, int bit_count)
{
	int produced = 0;
	while (produced < bit_count)
	{
		if (32 == *written_p)
		{
			enumError err = lzh8_flush_bits (w, offset_p, pool_p, written_p);
			if (err)
				return err;
		}
		int this_round;
		if (*written_p + (bit_count - produced) <= 32)
			this_round = bit_count - produced;
		else
			this_round = 32 - *written_p;
		const u32 selected
			= (bits_to_write >> (bit_count - this_round - produced)) & ((1u << this_round) - 1);
		*pool_p |= selected << (32 - this_round - *written_p);
		*written_p += this_round;
		produced += this_round;
	}
	return ERR_OK;
}

static uint lzh8_hash (const u8 *p, int len, int hash_size)
{
	int key = 0;
	for (int i = 0; i < len; i++)
		key = ((key << 5) ^ p[i]) % hash_size;
	return key;
}

// LZSS with hashing; STRICT mode reproduces Nintendo's exact output.
static enumError LZH8_LZSS_compress (const u8 *input_data, uint input_length,
	struct lzh8_symbol **lzss_stream_p, uint *lzss_length_p)
{
	const int min_length = 3;
	const int max_length = (1 << 8) - 1 + 3;
	const uint max_window_size = (1u << 15);

	struct lzss_hash_node
	{
		long offset;
		struct lzss_hash_node *next_node, *prev_node;
	};
	const int hash_size = 1024;

	struct lzss_hash_node *hash_queue = CALLOC (max_window_size, sizeof (struct lzss_hash_node));
	struct lzss_hash_node *hash_table = CALLOC (hash_size, sizeof (struct lzss_hash_node));
	if (!hash_queue || !hash_table)
	{
		FREE (hash_queue);
		FREE (hash_table);
		return ERR_CANT_CREATE;
	}
	for (int i = 0; i < hash_size; i++)
	{
		hash_table[i].next_node = NULL;
		hash_table[i].prev_node = NULL;
		hash_queue[i].offset = -2;
	}
	for (uint i = 0; i < max_window_size; i++)
	{
		hash_queue[i].next_node = NULL;
		hash_queue[i].prev_node = NULL;
		hash_queue[i].offset = -1;
	}

	struct lzh8_symbol *lzss_stream = NULL;
	uint lzss_length = 0, capacity = 0;

	uint bytes_done = 0;
	uint window_size = 0;
	uint hash_queue_head = 0, hash_queue_tail = 0;

	for (; bytes_done < input_length;)
	{
		int longest_match = 0;
		long longest_match_offset = 0;

		const uint next_input_offset
			= bytes_done + max_length < input_length ? bytes_done + max_length : input_length;

		if (bytes_done + min_length <= input_length)
		{
			const uint input_key = lzh8_hash (&input_data[bytes_done], min_length, hash_size);
			for (struct lzss_hash_node *cur = hash_table[input_key].next_node; cur;
				cur = cur->next_node)
			{
				if (cur->offset == bytes_done - 1)
					continue; // POLICY
				uint match_length = 0;
				for (uint i = 0; bytes_done + i < next_input_offset
					&& input_data[bytes_done + i] == input_data[cur->offset + i];
					i++)
					match_length = i + 1;
				if (match_length > (uint)longest_match)
				{
					longest_match = match_length;
					longest_match_offset = cur->offset;
				}
			}
		}

		if (lzss_length >= capacity)
		{
			capacity = capacity ? capacity * 2 : 0x800;
			struct lzh8_symbol *ns = REALLOC (lzss_stream, capacity * sizeof (*lzss_stream));
			if (!ns)
			{
				FREE (hash_queue);
				FREE (hash_table);
				FREE (lzss_stream);
				return ERR_CANT_CREATE;
			}
			lzss_stream = ns;
		}

		uint bytes_in_this_symbol;
		if (longest_match < min_length)
		{
			lzss_stream[lzss_length].is_reference = 0;
			lzss_stream[lzss_length].length_or_literal = input_data[bytes_done];
			lzss_length++;
			bytes_in_this_symbol = 1;
		}
		else
		{
			lzss_stream[lzss_length].is_reference = 1;
			lzss_stream[lzss_length].length_or_literal = longest_match - 3;
			lzss_stream[lzss_length].offset = bytes_done - longest_match_offset - 1;
			lzss_length++;
			bytes_in_this_symbol = longest_match;
		}

		for (uint i = 0; i < bytes_in_this_symbol; i++, bytes_done++)
		{
			if (window_size == max_window_size)
			{
				struct lzss_hash_node *old_node = &hash_queue[hash_queue_head];
				old_node->prev_node->next_node = NULL;
				hash_queue_head = (hash_queue_head + 1) % max_window_size;
				old_node->offset = -1;
				window_size--;
			}
			if (input_length - bytes_done >= min_length)
			{
				struct lzss_hash_node *new_node = &hash_queue[hash_queue_tail];
				const uint hash_key = lzh8_hash (&input_data[bytes_done], min_length, hash_size);
				new_node->next_node = hash_table[hash_key].next_node;
				new_node->prev_node = &hash_table[hash_key];
				hash_table[hash_key].next_node = new_node;
				if (new_node->next_node)
					new_node->next_node->prev_node = new_node;
				new_node->offset = bytes_done;
				hash_queue_tail = (hash_queue_tail + 1) % max_window_size;
				window_size++;
			}
		}
	}

	*lzss_stream_p = lzss_stream;
	*lzss_length_p = lzss_length;
	FREE (hash_queue);
	FREE (hash_table);
	return ERR_OK;
}

static int LZH8_Huff_build_tree (
	int *node_remains, long *freq, struct lzh8_huff_node *node_array, int symbol_count)
{
	int nodes_left = 0;
	int next_new_node_idx = symbol_count;
	for (int i = 0; i < symbol_count; i++)
	{
		if (0 != freq[i])
		{
			node_remains[i] = 1;
			nodes_left++;
		}
		else
			node_remains[i] = 0;
		node_array[i].lchild = -1;
		node_array[i].rchild = -1;
		node_array[i].leaf = i;
		node_array[i].subtree_size = 0;
	}
	for (int i = symbol_count; i < symbol_count * 2 - 1; i++)
		node_remains[i] = 0;

	int root_idx = 0;
	if (0 == nodes_left)
		return -1;

	if (1 == nodes_left)
	{
		int i;
		for (i = 0; i < symbol_count; i++)
			if (node_remains[i])
				break;
		node_array[next_new_node_idx].lchild = i;
		node_array[next_new_node_idx].rchild = i;
		node_array[next_new_node_idx].subtree_size = 1;
		root_idx = next_new_node_idx;
	}

	for (; nodes_left > 1; nodes_left--)
	{
		int smallest_idx = -1, next_smallest_idx = -1;
		{
			long smallest = -1, next_smallest = -1;
			for (int i = 0; i < next_new_node_idx; i++)
			{
				if (node_remains[i])
				{
					if (freq[i] < smallest || -1 == smallest)
					{
						next_smallest = smallest;
						next_smallest_idx = smallest_idx;
						smallest = freq[i];
						smallest_idx = i;
					}
					else if (freq[i] < next_smallest || -1 == next_smallest)
					{
						next_smallest = freq[i];
						next_smallest_idx = i;
					}
				}
			}
		}
		struct lzh8_huff_node sum_node;
		sum_node.lchild = smallest_idx;
		sum_node.rchild = next_smallest_idx;
		sum_node.leaf = 0;
		sum_node.subtree_size = node_array[smallest_idx].subtree_size
			+ node_array[next_smallest_idx].subtree_size + 1;
		const long total_freq = freq[smallest_idx] + freq[next_smallest_idx];
		const int sum_node_idx = next_new_node_idx;
		freq[sum_node_idx] = total_freq;
		node_remains[sum_node_idx] = 1;
		node_remains[smallest_idx] = 0;
		node_remains[next_smallest_idx] = 0;
		node_array[sum_node_idx] = sum_node;
		root_idx = sum_node_idx;
		next_new_node_idx++;
	}
	return root_idx;
}

static void LZH8_Huff_compute_prefix (const struct lzh8_huff_node *node_array, int root_idx,
	struct lzh8_huff_symbol *sym_array, u32 key_bits, int key_len)
{
	const struct lzh8_huff_node *root = &node_array[root_idx];
	if (-1 == root_idx)
		return;
	if (-1 != root->lchild)
	{
		key_len++;
		LZH8_Huff_compute_prefix (node_array, root->lchild, sym_array, key_bits << 1, key_len);
		LZH8_Huff_compute_prefix (
			node_array, root->rchild, sym_array, (key_bits << 1) | 1, key_len);
	}
	else
	{
		sym_array[root->leaf].key_len = key_len;
		sym_array[root->leaf].key_bits = key_bits;
	}
}

static bool LZH8_Huff_could_satisfy (const struct lzh8_table_ctrl *ctrl, int table_idx,
	uint16_t proposed_size, int proposed_idx, const int offset_bits)
{
	(void)proposed_idx;
	const int max_offset = 1 << offset_bits;
	for (unsigned int i = 0; i < table_idx; i++)
	{
		if (!ctrl[i].placed)
		{
			const uint16_t dest_offset = table_idx / 2 + proposed_size;
			if (max_offset >= dest_offset - i / 2)
				proposed_size++;
			else
				return false;
		}
	}
	return true;
}

static void LZH8_Huff_flatten_single (const struct lzh8_huff_node *node_array,
	struct lzh8_table_ctrl *ctrl, u16 *tree_table, const int offset_bits, unsigned int parent_idx,
	unsigned int *table_idx_p, unsigned int *outstanding_p)
{
	u8 leaf_flags = 0;
	const struct lzh8_huff_node *parent_node = &node_array[ctrl[parent_idx].node_idx];
	if (node_array[parent_node->lchild].lchild != -1)
	{
		tree_table[*table_idx_p] = 0;
		ctrl[*table_idx_p].placed = false;
		ctrl[*table_idx_p].node_idx = parent_node->lchild;
		(*outstanding_p)++;
	}
	else
	{
		tree_table[*table_idx_p] = node_array[parent_node->lchild].leaf;
		ctrl[*table_idx_p].placed = true;
		leaf_flags |= 2;
	}
	(*table_idx_p)++;
	if (node_array[parent_node->rchild].lchild != -1)
	{
		tree_table[*table_idx_p] = 0;
		ctrl[*table_idx_p].placed = false;
		ctrl[*table_idx_p].node_idx = parent_node->rchild;
		(*outstanding_p)++;
	}
	else
	{
		tree_table[*table_idx_p] = node_array[parent_node->rchild].leaf;
		ctrl[*table_idx_p].placed = true;
		leaf_flags |= 1;
	}
	(*table_idx_p)++;
	const u16 offset = (((*table_idx_p) - 2) - parent_idx / 2 * 2) / 2 - 1;
	tree_table[parent_idx] = (leaf_flags << offset_bits) | offset;
	ctrl[parent_idx].placed = true;
	(*outstanding_p)--;
}

static uint LZH8_Huff_flatten_tree (
	const struct lzh8_huff_node *node_array, u16 *tree_table, int root_idx, const int offset_bits)
{
	if (-1 == root_idx)
		return 0;
	struct lzh8_table_ctrl *ctrl = MALLOC ((root_idx + 2) * sizeof (*ctrl));
	if (!ctrl)
		return UINT_MAX;

	unsigned int outstanding_nodes = 1;
	ctrl[0].placed = true;
	ctrl[1].node_idx = root_idx;
	ctrl[1].placed = false;
	unsigned int table_idx = 2;

	while (0 < outstanding_nodes)
	{
		uint16_t fitting_idx = table_idx;
		for (int i = table_idx - 1; i >= 0; i--)
		{
			if (!ctrl[i].placed)
			{
				const struct lzh8_huff_node *candidate = &node_array[ctrl[i].node_idx];
				if (candidate->subtree_size + outstanding_nodes <= (1u << offset_bits)
					&& LZH8_Huff_could_satisfy (
						ctrl, table_idx, candidate->subtree_size, i, offset_bits))
				{
					fitting_idx = i;
					break;
				}
			}
		}

		if (fitting_idx != table_idx)
		{
			unsigned int i = table_idx;
			LZH8_Huff_flatten_single (node_array, ctrl, tree_table, offset_bits, fitting_idx,
				&table_idx, &outstanding_nodes);
			for (; i < table_idx; i++)
			{
				if (!ctrl[i].placed)
					LZH8_Huff_flatten_single (node_array, ctrl, tree_table, offset_bits, i,
						&table_idx, &outstanding_nodes);
			}
		}
		else
		{
			for (unsigned int i = 0; i < table_idx; i += 2)
			{
				unsigned int node_to_break = table_idx;
				if (!ctrl[i + 0].placed)
				{
					if (!ctrl[i + 1].placed
						&& node_array[ctrl[i + 1].node_idx].subtree_size
							> node_array[ctrl[i + 0].node_idx].subtree_size)
						node_to_break = i + 1;
					else
						node_to_break = i + 0;
				}
				else if (!ctrl[i + 1].placed)
					node_to_break = i + 1;
				if (node_to_break != table_idx)
				{
					LZH8_Huff_flatten_single (node_array, ctrl, tree_table, offset_bits,
						node_to_break, &table_idx, &outstanding_nodes);
					break;
				}
			}
		}
	}
	FREE (ctrl);
	return table_idx;
}

static enumError LZH8_Huff_produce_encodings (const struct lzh8_symbol *lzss_stream,
	uint lzss_length, struct lzh8_huff_symbol *back_litlen, struct lzh8_huff_symbol *back_displen,
	uint *output_offset_p, lzh8_wr_t *w)
{
	long length_freq[LZH8_LENCNT * 2 - 1] = { 0 };
	long displen_freq[LZH8_DISPCNT * 2 - 1] = { 0 };
	for (uint i = 0; i < lzss_length; i++)
	{
		length_freq[(lzss_stream[i].is_reference << 8) | lzss_stream[i].length_or_literal]++;
		if (lzss_stream[i].is_reference)
			displen_freq[LZH8_displen_length (lzss_stream[i].offset)]++;
	}

#define LZH8_WRITE_BITS(bits, count)                                                               \
	do                                                                                             \
	{                                                                                              \
		enumError e = lzh8_write_bits (                                                            \
			w, output_offset_p, &lzh8_bit_pool, &lzh8_bits_written, bits, count);                  \
		if (e)                                                                                     \
			return e;                                                                              \
	} while (0)

	// Length/literal tree
	{
		int node_remains[LZH8_LENCNT * 2 - 1];
		struct lzh8_huff_node node_array[LZH8_LENCNT * 2 - 1];
		const int root_idx
			= LZH8_Huff_build_tree (node_remains, length_freq, node_array, LZH8_LENCNT);
		LZH8_Huff_compute_prefix (node_array, root_idx, back_litlen, 0, 0);

		u16 tree_table[LZH8_LENCNT * 2] = { 0 };
		const uint table_size
			= LZH8_Huff_flatten_tree (node_array, tree_table, root_idx, LZH8_LENBITS - 2);
		if (table_size == UINT_MAX)
			return ERR_CANT_CREATE;

		const uint start = *output_offset_p;
		u32 lzh8_bit_pool = 0;
		int lzh8_bits_written = 16; // leave space for the size field
		for (uint i = 1; i < table_size; i++)
			LZH8_WRITE_BITS (tree_table[i], LZH8_LENBITS);
		{
			enumError e = lzh8_flush_bits (w, output_offset_p, &lzh8_bit_pool, &lzh8_bits_written);
			if (e)
				return e;
		}
		const uint table_bytes = (*output_offset_p - start) / 4 - 1;
		{
			enumError e = lzh8_wr_16_le (w, start, table_bytes);
			if (e)
				return e;
		}
	}

	// Displacement length tree
	{
		int node_remains[LZH8_DISPCNT * 2 - 1];
		struct lzh8_huff_node node_array[LZH8_DISPCNT * 2 - 1];
		const int root_idx
			= LZH8_Huff_build_tree (node_remains, displen_freq, node_array, LZH8_DISPCNT);
		LZH8_Huff_compute_prefix (node_array, root_idx, back_displen, 0, 0);

		u16 tree_table[LZH8_DISPCNT * 2] = { 0 };
		const uint table_size
			= LZH8_Huff_flatten_tree (node_array, tree_table, root_idx, LZH8_DISPBITS - 2);
		if (table_size == UINT_MAX)
			return ERR_CANT_CREATE;

		const uint start = *output_offset_p;
		u32 lzh8_bit_pool = 0;
		int lzh8_bits_written = 8; // leave space for the size field
		for (uint i = 1; i < table_size; i++)
			LZH8_WRITE_BITS (tree_table[i], LZH8_DISPBITS);
		{
			enumError e = lzh8_flush_bits (w, output_offset_p, &lzh8_bit_pool, &lzh8_bits_written);
			if (e)
				return e;
		}
		const uint table_bytes = (*output_offset_p - start) / 4 - 1;
		{
			enumError e = lzh8_wr_byte (w, start, table_bytes);
			if (e)
				return e;
		}
	}

#undef LZH8_WRITE_BITS
	return ERR_OK;
}

enumError EncodeLZH8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size)
		return EINVAL;

	lzh8_wr_t w = { 0 };
	uint output_offset = 0;

	// Step 0: header
	enumError err;
	if (src_size < 0x1000000)
	{
		err = lzh8_wr_32_le (&w, 0, (((u32)src_size) << 8) | 0x40);
		if (err)
			return err;
		output_offset = 4;
	}
	else
	{
		err = lzh8_wr_32_le (&w, 0, 0x40);
		if (!err)
			err = lzh8_wr_32_le (&w, 4, src_size);
		if (err)
			return err;
		output_offset = 8;
	}

	// Step 1: LZSS
	struct lzh8_symbol *lzss_stream = NULL;
	uint lzss_length = 0;
	err = LZH8_LZSS_compress (src, src_size, &lzss_stream, &lzss_length);
	if (err)
	{
		FREE (lzss_stream);
		FREE (w.data);
		return err;
	}

	// Step 2: build Huffman codes and write the flattened trees
	struct lzh8_huff_symbol back_litlen[LZH8_LENCNT];
	struct lzh8_huff_symbol back_displen[LZH8_DISPCNT];
	err = LZH8_Huff_produce_encodings (
		lzss_stream, lzss_length, back_litlen, back_displen, &output_offset, &w);
	if (err)
	{
		FREE (lzss_stream);
		FREE (w.data);
		return err;
	}

	// Step 3: encoded symbol stream
	{
		u32 bit_pool = 0;
		int bits_written = 0;
		for (uint i = 0; i < lzss_length; i++)
		{
			const struct lzh8_huff_symbol litlen = back_litlen[(lzss_stream[i].is_reference << 8)
				| lzss_stream[i].length_or_literal];
			err = lzh8_write_bits (
				&w, &output_offset, &bit_pool, &bits_written, litlen.key_bits, litlen.key_len);
			if (!err && lzss_stream[i].is_reference)
			{
				const uint displen_length = LZH8_displen_length (lzss_stream[i].offset);
				const struct lzh8_huff_symbol displen_sym = back_displen[displen_length];
				err = lzh8_write_bits (&w, &output_offset, &bit_pool, &bits_written,
					displen_sym.key_bits, displen_sym.key_len);
				if (!err && lzss_stream[i].offset > 1)
					err = lzh8_write_bits (&w, &output_offset, &bit_pool, &bits_written,
						lzss_stream[i].offset, displen_length - 1);
			}
			if (err)
			{
				FREE (lzss_stream);
				FREE (w.data);
				return err;
			}
		}
		err = lzh8_flush_bits (&w, &output_offset, &bit_pool, &bits_written);
		if (err)
		{
			FREE (lzss_stream);
			FREE (w.data);
			return err;
		}
	}

	FREE (lzss_stream);
	*dest = w.data;
	*dest_size = w.size;
	return ERR_OK;
}

