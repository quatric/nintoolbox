#include "lib-std.h"
#include "lib-huff.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

enumError DecodeNintendoHuff (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 9 || (src[0] != 0x24 && src[0] != 0x28))
		return EINVAL;
	const bool four_bit = src[0] == 0x24;
	u32 out_size = (u32)src[1] | (u32)src[2] << 8 | (u32)src[3] << 16;
	uint tree_off = 4;
	if (!out_size)
	{
		if (src_size < 13)
			return EINVAL;
		out_size = rd_le32 (src + 4);
		tree_off = 8;
	}
	const uint tree_size = 2u * (src[tree_off] + 1);
	const uint tree_base = tree_off + 1;
	if (!out_size || tree_size > src_size - tree_base || src_size - (tree_base + tree_size) < 4)
		return EINVAL;
	enumError err = AllocOutput (dest, dest_size, out_size);
	if (err)
		return err;
	const u8 *tree = src + tree_base;
	const u8 *bits = tree + tree_size;
	uint bits_pos = 0, bits_left = 0, out_pos = 0;
	u32 word = 0;
	int half = -1;
	while (out_pos < out_size)
	{
		uint node = 0;
		u8 symbol = 0;
		for (;;)
		{
			if (node >= tree_size)
			{
				FREE (*dest);
				*dest = 0;
				return EINVAL;
			}
			if (!bits_left)
			{
				if (bits_pos > src_size - (bits - tree) - 4)
				{
					FREE (*dest);
					*dest = 0;
					return EINVAL;
				}
				word = rd_le32 (bits + bits_pos);
				bits_pos += 4;
				bits_left = 32;
			}
			const bool bit = (word >> (bits_left - 1)) & 1;
			bits_left--;
			const u8 entry = tree[node];
			const uint child = (node & ~1u) + 2 + 2 * (entry & 0x3f) + (bit ? 1 : 0);
			if (child >= tree_size)
			{
				FREE (*dest);
				*dest = 0;
				return EINVAL;
			}
			if (entry & (bit ? 0x40 : 0x80))
			{
				symbol = tree[child];
				break;
			}
			node = child;
		}
		if (!four_bit)
			(*dest)[out_pos++] = symbol;
		else if (half < 0)
			half = symbol << 4;
		else
		{
			(*dest)[out_pos++] = half | (symbol & 15);
			half = -1;
		}
	}
	if (half >= 0)
	{
		FREE (*dest);
		*dest = 0;
		return EINVAL;
	}
	return ERR_OK;
}

typedef struct hnode_t
{
	uint freq;
	int left;
	int right;
	int symbol; // -1 for internal node
	uint code;
	uint len;
} hnode_t;

typedef struct bfs_q_t
{
	int node_idx;
	uint tree_pos;
} bfs_q_t;

enumError EncodeNintendoHuff (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool four_bit)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00FFFFFF)
		return EINVAL;

	const uint num_syms = four_bit ? 16 : 256;
	uint freq[256] = { 0 };
	if (four_bit)
	{
		for (uint i = 0; i < src_size; i++)
		{
			freq[(src[i] >> 4) & 0xF]++;
			freq[src[i] & 0xF]++;
		}
	}
	else
	{
		for (uint i = 0; i < src_size; i++)
			freq[src[i]]++;
	}

	hnode_t nodes[512];
	int n_nodes = 0;
	int active[256];
	int n_active = 0;

	for (uint i = 0; i < num_syms; i++)
	{
		if (freq[i] > 0)
		{
			nodes[n_nodes].freq = freq[i];
			nodes[n_nodes].left = -1;
			nodes[n_nodes].right = -1;
			nodes[n_nodes].symbol = (int)i;
			nodes[n_nodes].code = 0;
			nodes[n_nodes].len = 0;
			active[n_active++] = n_nodes++;
		}
	}

	if (n_active == 0)
		return EINVAL;

	if (n_active == 1)
	{
		uint dummy_sym = (nodes[active[0]].symbol + 1) % num_syms;
		nodes[n_nodes].freq = 0;
		nodes[n_nodes].left = -1;
		nodes[n_nodes].right = -1;
		nodes[n_nodes].symbol = (int)dummy_sym;
		nodes[n_nodes].code = 0;
		nodes[n_nodes].len = 0;
		active[n_active++] = n_nodes++;
	}

	while (n_active > 1)
	{
		int min1 = 0;
		for (int i = 1; i < n_active; i++)
			if (nodes[active[i]].freq < nodes[active[min1]].freq)
				min1 = i;
		int idx1 = active[min1];
		active[min1] = active[--n_active];

		int min2 = 0;
		for (int i = 1; i < n_active; i++)
			if (nodes[active[i]].freq < nodes[active[min2]].freq)
				min2 = i;
		int idx2 = active[min2];
		active[min2] = active[--n_active];

		int parent = n_nodes++;
		nodes[parent].freq = nodes[idx1].freq + nodes[idx2].freq;
		nodes[parent].left = idx1;
		nodes[parent].right = idx2;
		nodes[parent].symbol = -1;
		nodes[parent].code = 0;
		nodes[parent].len = 0;
		active[n_active++] = parent;
	}

	int root = active[0];

	int stack[512];
	int top = 0;
	stack[top++] = root;
	while (top > 0)
	{
		int curr = stack[--top];
		if (nodes[curr].left >= 0)
		{
			int l = nodes[curr].left;
			nodes[l].code = (nodes[curr].code << 1) | 0;
			nodes[l].len = nodes[curr].len + 1;
			stack[top++] = l;
		}
		if (nodes[curr].right >= 0)
		{
			int r = nodes[curr].right;
			nodes[r].code = (nodes[curr].code << 1) | 1;
			nodes[r].len = nodes[curr].len + 1;
			stack[top++] = r;
		}
	}

	uint sym_code[256] = { 0 };
	uint sym_len[256] = { 0 };
	for (int i = 0; i < n_nodes; i++)
	{
		if (nodes[i].symbol >= 0)
		{
			sym_code[nodes[i].symbol] = nodes[i].code;
			sym_len[nodes[i].symbol] = nodes[i].len;
		}
	}

	u8 tree[1024] = { 0 };
	bfs_q_t q[512];
	int q_head = 0, q_tail = 0;
	q[q_tail++] = (bfs_q_t) { root, 0 };
	uint next_pair = 2;

	while (q_head < q_tail)
	{
		bfs_q_t item = q[q_head++];
		int n_idx = item.node_idx;
		int l = nodes[n_idx].left;
		int r = nodes[n_idx].right;

		uint pair_pos = next_pair;
		next_pair += 2;
		if (next_pair > sizeof (tree))
			return EFBIG;

		uint offset = (pair_pos - ((item.tree_pos & ~1u) + 2)) / 2;
		if (offset > 0x3F)
			return EFBIG;

		u8 entry = (u8)(offset & 0x3F);

		if (nodes[l].symbol >= 0)
		{
			entry |= 0x80;
			tree[pair_pos + 0] = (u8)nodes[l].symbol;
		}
		else
		{
			tree[pair_pos + 0] = 0;
			q[q_tail++] = (bfs_q_t) { l, pair_pos + 0 };
		}

		if (nodes[r].symbol >= 0)
		{
			entry |= 0x40;
			tree[pair_pos + 1] = (u8)nodes[r].symbol;
		}
		else
		{
			tree[pair_pos + 1] = 0;
			q[q_tail++] = (bfs_q_t) { r, pair_pos + 1 };
		}

		tree[item.tree_pos] = entry;
	}

	uint tree_size = next_pair;
	u8 tree_size_byte = (u8)((tree_size / 2) - 1);

	uint max_bits_bytes = src_size * 2 + 1024;
	u8 *bits_buf = MALLOC (max_bits_bytes);
	if (!bits_buf)
		return ERR_CANT_CREATE;

	uint bits_pos = 0;
	u32 cur_word = 0;
	uint bits_left = 32;

	const uint total_syms = four_bit ? 2 * src_size : src_size;
	for (uint s_idx = 0; s_idx < total_syms; s_idx++)
	{
		u8 sym;
		if (four_bit)
		{
			uint byte_i = s_idx / 2;
			sym = (s_idx % 2 == 0) ? ((src[byte_i] >> 4) & 0xF) : (src[byte_i] & 0xF);
		}
		else
		{
			sym = src[s_idx];
		}

		uint code = sym_code[sym];
		uint len = sym_len[sym];

		for (uint b = 0; b < len; b++)
		{
			bool bit = (code >> (len - 1 - b)) & 1;
			if (bit)
				cur_word |= (1u << (bits_left - 1));
			bits_left--;
			if (bits_left == 0)
			{
				if (bits_pos + 4 > max_bits_bytes)
				{
					max_bits_bytes *= 2;
					u8 *grown = REALLOC (bits_buf, max_bits_bytes);
					if (!grown)
					{
						FREE (bits_buf);
						return ERR_CANT_CREATE;
					}
					bits_buf = grown;
				}
				wr_le32 (bits_buf + bits_pos, cur_word);
				bits_pos += 4;
				cur_word = 0;
				bits_left = 32;
			}
		}
	}

	if (bits_left < 32)
	{
		if (bits_pos + 4 > max_bits_bytes)
		{
			max_bits_bytes += 1024;
			u8 *grown = REALLOC (bits_buf, max_bits_bytes);
			if (!grown)
			{
				FREE (bits_buf);
				return ERR_CANT_CREATE;
			}
			bits_buf = grown;
		}
		wr_le32 (bits_buf + bits_pos, cur_word);
		bits_pos += 4;
	}

	const uint tree_off = 4;
	const uint total_out = tree_off + 1 + tree_size + bits_pos;
	u8 *out = CALLOC (1, total_out);
	if (!out)
	{
		FREE (bits_buf);
		return ERR_CANT_CREATE;
	}

	out[0] = four_bit ? 0x24 : 0x28;
	out[1] = src_size & 0xFF;
	out[2] = (src_size >> 8) & 0xFF;
	out[3] = (src_size >> 16) & 0xFF;
	out[tree_off] = tree_size_byte;
	memcpy (out + tree_off + 1, tree, tree_size);
	memcpy (out + tree_off + 1 + tree_size, bits_buf, bits_pos);
	FREE (bits_buf);

	*dest = out;
	*dest_size = total_out;
	return ERR_OK;
}

