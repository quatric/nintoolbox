// lib-bflyt.c - BFLYT/BCLYT/BFLAN/BCLAN layout & animation tools
//
// C port of the reference implementation 'benzin' (bflyt.py + txtree.py)
// by thakis / tyulis. The model is a lossless ordered tree (BFTreeDump/
// BFTreeLoad use the same text format as benzin's txtree.py), and the
// binary readers/writers mirror benzin's read*/pack* methods.
//
// Known intentional deviations from benzin (bug fixes):
//   - byte order mark: '>' writes 0xFEFF (benzin writes 0xFFFE for both)
//   - file magic is preserved (FLYT/CLYT/FLAN/CLAN), not hardcoded FLYT
//   - mat1 flag bits are symmetric read/write (alpha-compare bit 9,
//     blend-mode 10-11, blend-alpha 12-13, indirect 14, projection 15-16,
//     shadow 17); benzin's pack used shifted, inconsistent bits
//   - indirect-adjustment is read correctly (benzin had a bit-typo)
//   - colors are written RED,GREEN,BLUE[,ALPHA] (benzin swapped G/B)
//   - alpha-compare value is written as float32
//   - prt1 entries are 40 bytes with correct data offsets
//   - usd1 offsets are written entry-relative and match the reader
//   - 'dump' raw data is preserved as Python b'..' bytes literals

#include "lib-bflyt.h"
#include "lib-std.h"
#include "lib-szs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdint.h>

//
///////////////////////////////////////////////////////////////////////////////
///////////////			small helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static char *bf_strdup (ccp s)
{
	if (!s)
		return 0;
	uint n = strlen (s);
	char *r = (char *)MALLOC (n + 1);
	if (r)
		memcpy (r, s, n + 1);
	return r;
}

static char *bf_strndup (ccp s, uint n)
{
	char *r = (char *)MALLOC (n + 1);
	if (r)
	{
		memcpy (r, s, n);
		r[n] = 0;
	}
	return r;
}

#define BFE(stmt)                                                                                  \
	do                                                                                             \
	{                                                                                              \
		enumError _e = (stmt);                                                                     \
		if (_e)                                                                                    \
			return _e;                                                                             \
	} while (0)

//
///////////////////////////////////////////////////////////////////////////////
///////////////			growable byte buffer		///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct bf_buf_t
{
	u8 *d;
	uint n, cap;
} bf_buf_t;

static enumError bf_buf_reserve (bf_buf_t *b, uint add)
{
	if (add > UINT_MAX - b->n)
		return ERR_OUT_OF_MEMORY;
	uint need = b->n + add;
	if (need <= b->cap)
		return ERR_OK;
	uint newcap = b->cap ? b->cap : 256;
	while (newcap < need)
	{
		if (newcap > UINT_MAX / 2)
		{
			newcap = need;
			break;
		}
		newcap *= 2;
	}
	u8 *nd = (u8 *)REALLOC (b->d, newcap);
	if (!nd)
		return ERR_OUT_OF_MEMORY;
	b->d = nd;
	b->cap = newcap;
	return ERR_OK;
}

static enumError bf_buf_raw (bf_buf_t *b, const void *p, uint n)
{
	BFE (bf_buf_reserve (b, n));
	if (n)
		memcpy (b->d + b->n, p, n);
	b->n += n;
	return ERR_OK;
}

static enumError bf_buf_u8 (bf_buf_t *b, u8 v)
{
	return bf_buf_raw (b, &v, 1);
}

static enumError bf_buf_u16 (bf_buf_t *b, bool be, u16 v)
{
	u8 p[2];
	if (be)
	{
		p[0] = v >> 8;
		p[1] = v;
	}
	else
	{
		p[0] = v;
		p[1] = v >> 8;
	}
	return bf_buf_raw (b, p, 2);
}

static enumError bf_buf_u32 (bf_buf_t *b, bool be, u32 v)
{
	u8 p[4];
	if (be)
	{
		p[0] = v >> 24;
		p[1] = v >> 16;
		p[2] = v >> 8;
		p[3] = v;
	}
	else
	{
		p[0] = v;
		p[1] = v >> 8;
		p[2] = v >> 16;
		p[3] = v >> 24;
	}
	return bf_buf_raw (b, p, 4);
}

static enumError bf_buf_f32 (bf_buf_t *b, bool be, float v)
{
	union
	{
		float f;
		u32 i;
	} u;
	u.f = v;
	return bf_buf_u32 (b, be, u.i);
}

static enumError bf_buf_pad (bf_buf_t *b, uint n)
{
	BFE (bf_buf_reserve (b, n));
	if (n)
		memset (b->d + b->n, 0, n);
	b->n += n;
	return ERR_OK;
}

// write a NUL-terminated (padded) string, like benzin TypeWriter.string()
static enumError bf_buf_str (bf_buf_t *b, ccp s, uint align)
{
	uint len = strlen (s);
	if (align == 0)
	{
		// unbounded field: string + a single NUL terminator, no fixed width.
		BFE (bf_buf_raw (b, s, len));
		return bf_buf_pad (b, 1);
	}
	// Fixed-width struct field (e.g. grp1's name/sub-entry slots): must
	// never exceed 'align' bytes, even if that drops the NUL terminator --
	// a real, observed 3DS BCLYT case is a 16-byte grp1 name field holding
	// a name that's exactly 16 characters with no NUL at all ("G_BtnLU_
	// Interior" in a real Tomodachi Life layout). rb_strn() (the matching
	// reader) never returns more than 'align' characters for these fields,
	// so truncating here can only ever reproduce what a real fixed-width
	// field already legitimately holds -- growing past 'align' (the old
	// behaviour) corrupted every subsequent fixed-offset field in the
	// struct instead.
	if (len > align)
		len = align;
	BFE (bf_buf_raw (b, s, len));
	return len < align ? bf_buf_pad (b, align - len) : ERR_OK;
}

// string + NUL, padded to a 4-byte boundary
static enumError bf_buf_str4 (bf_buf_t *b, ccp s)
{
	uint len = strlen (s);
	BFE (bf_buf_raw (b, s, len));
	BFE (bf_buf_u8 (b, 0));
	uint rem = (b->n) & 3;
	if (rem)
		return bf_buf_pad (b, 4 - rem);
	return ERR_OK;
}

// section header: magic (ascii) + u32 size (payload + 8)
static enumError bf_buf_sechdr (bf_buf_t *b, bool be, const char magic[4], uint payload_len)
{
	BFE (bf_buf_raw (b, magic, 4));
	return bf_buf_u32 (b, be, payload_len + 8);
}

static void bf_buf_free (bf_buf_t *b)
{
	FREE (b->d);
	memset (b, 0, sizeof (*b));
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ordered tree			///////////////
///////////////////////////////////////////////////////////////////////////////

static void bf_kv_free (bf_node_t *node, struct bf_kv_t *kv);

void BFValClear (bf_val_t *val)
{
	switch (val->type)
	{
		case BF_T_STR:
			FREE (val->u.s);
			break;
		case BF_T_BYTES:
			FREE (val->u.by.d);
			break;
		case BF_T_NODE:
			BFNodeFree (val->u.node);
			FREE (val->u.node);
			break;
		case BF_T_LIST:
			BFListFree (val->u.list);
			FREE (val->u.list);
			break;
		default:
			break;
	}
	memset (val, 0, sizeof (*val));
}

void BFListInit (bf_list_t *list)
{
	memset (list, 0, sizeof (*list));
}

void BFListFree (bf_list_t *list)
{
	uint i;
	for (i = 0; i < list->n; i++)
		BFValClear (&list->items[i]);
	FREE (list->items);
	memset (list, 0, sizeof (*list));
}

// clear the items array only; does NOT free the values. Use for lists that
// store borrowed pointers (e.g. the reader's material cache).
static void bf_list_clear (bf_list_t *list)
{
	FREE (list->items);
	memset (list, 0, sizeof (*list));
}

void BFNodeInit (bf_node_t *node)
{
	memset (node, 0, sizeof (*node));
}

void BFNodeFree (bf_node_t *node)
{
	uint i;
	for (i = 0; i < node->n; i++)
	{
		FREE (node->kv[i].key);
		BFValClear (&node->kv[i].val);
	}
	FREE (node->kv);
	memset (node, 0, sizeof (*node));
}

static void bf_kv_free (bf_node_t *node, struct bf_kv_t *kv)
{
	FREE (kv->key);
	BFValClear (&kv->val);
	// remove entry at index 'kv'
	uint idx = (uint)(kv - node->kv);
	memmove (node->kv + idx, node->kv + idx + 1, sizeof (*node->kv) * (node->n - idx - 1));
	node->n--;
}

static struct bf_kv_t *bf_node_find (bf_node_t *node, ccp key)
{
	uint i;
	for (i = 0; i < node->n; i++)
		if (!strcmp (node->kv[i].key, key))
			return &node->kv[i];
	return 0;
}

static enumError bf_node_alloc (bf_node_t *node, ccp key, bf_val_t **out)
{
	if (!key)
		return ERR_INVALID_DATA;
	if (node->n == node->cap)
	{
		uint newcap = node->cap ? node->cap * 2 : 8;
		struct bf_kv_t *nkv = (struct bf_kv_t *)REALLOC (node->kv, newcap * sizeof (*node->kv));
		if (!nkv)
			return ERR_OUT_OF_MEMORY;
		node->kv = nkv;
		node->cap = newcap;
	}
	char *nkey = bf_strdup (key);
	if (!nkey)
		return ERR_OUT_OF_MEMORY;
	memset (&node->kv[node->n], 0, sizeof (node->kv[node->n]));
	node->kv[node->n].key = nkey;
	*out = &node->kv[node->n].val;
	node->n++;
	return ERR_OK;
}

static enumError bf_list_alloc (bf_list_t *list, bf_val_t **out)
{
	if (list->n == list->cap)
	{
		uint newcap = list->cap ? list->cap * 2 : 8;
		bf_val_t *ni = (bf_val_t *)REALLOC (list->items, newcap * sizeof (*list->items));
		if (!ni)
			return ERR_OUT_OF_MEMORY;
		list->items = ni;
		list->cap = newcap;
	}
	memset (&list->items[list->n], 0, sizeof (list->items[list->n]));
	*out = &list->items[list->n];
	list->n++;
	return ERR_OK;
}

bf_val_t *BFNodeGet (bf_node_t *node, ccp key)
{
	if (!node || !key)
		return 0;
	struct bf_kv_t *kv = bf_node_find (node, key);
	return kv ? &kv->val : 0;
}

enumError BFNodeSetStr (bf_node_t *node, ccp key, ccp s)
{
	bf_val_t *v;
	BFE (bf_node_alloc (node, key, &v));
	v->type = BF_T_STR;
	v->u.s = bf_strdup (s);
	if (!v->u.s)
	{
		bf_kv_free (node, &node->kv[node->n - 1]);
		return ERR_OUT_OF_MEMORY;
	}
	return ERR_OK;
}

enumError BFNodeSetBytes (bf_node_t *node, ccp key, const void *data, uint n)
{
	bf_val_t *v;
	BFE (bf_node_alloc (node, key, &v));
	v->type = BF_T_BYTES;
	v->u.by.d = (u8 *)MALLOC (n ? n : 1);
	if (!v->u.by.d)
	{
		bf_kv_free (node, &node->kv[node->n - 1]);
		return ERR_OUT_OF_MEMORY;
	}
	if (n)
		memcpy (v->u.by.d, data, n);
	v->u.by.n = n;
	return ERR_OK;
}

enumError BFNodeSetInt (bf_node_t *node, ccp key, int i)
{
	bf_val_t *v;
	BFE (bf_node_alloc (node, key, &v));
	v->type = BF_T_INT;
	v->u.i = i;
	return ERR_OK;
}

enumError BFNodeSetFloat (bf_node_t *node, ccp key, double f)
{
	bf_val_t *v;
	BFE (bf_node_alloc (node, key, &v));
	v->type = BF_T_FLOAT;
	v->u.f = f;
	return ERR_OK;
}

enumError BFNodeSetBool (bf_node_t *node, ccp key, bool b)
{
	bf_val_t *v;
	BFE (bf_node_alloc (node, key, &v));
	v->type = BF_T_BOOL;
	v->u.b = b;
	return ERR_OK;
}

enumError BFNodeSetNone (bf_node_t *node, ccp key)
{
	bf_val_t *v;
	BFE (bf_node_alloc (node, key, &v));
	v->type = BF_T_NONE;
	return ERR_OK;
}

bf_node_t *BFNodeSetNode (bf_node_t *node, ccp key)
{
	bf_val_t *v;
	if (bf_node_alloc (node, key, &v))
		return 0;
	v->type = BF_T_NODE;
	v->u.node = (bf_node_t *)MALLOC (sizeof (bf_node_t));
	if (!v->u.node)
	{
		bf_kv_free (node, &node->kv[node->n - 1]);
		return 0;
	}
	BFNodeInit (v->u.node);
	return v->u.node;
}

bf_list_t *BFNodeSetList (bf_node_t *node, ccp key)
{
	bf_val_t *v;
	if (bf_node_alloc (node, key, &v))
		return 0;
	v->type = BF_T_LIST;
	v->u.list = (bf_list_t *)MALLOC (sizeof (bf_list_t));
	if (!v->u.list)
	{
		bf_kv_free (node, &node->kv[node->n - 1]);
		return 0;
	}
	BFListInit (v->u.list);
	return v->u.list;
}

enumError BFListAddStr (bf_list_t *list, ccp s)
{
	bf_val_t *v;
	BFE (bf_list_alloc (list, &v));
	v->type = BF_T_STR;
	v->u.s = bf_strdup (s);
	if (!v->u.s)
	{
		list->n--;
		return ERR_OUT_OF_MEMORY;
	}
	return ERR_OK;
}

enumError BFListAddBytes (bf_list_t *list, const void *data, uint n)
{
	bf_val_t *v;
	BFE (bf_list_alloc (list, &v));
	v->type = BF_T_BYTES;
	v->u.by.d = (u8 *)MALLOC (n ? n : 1);
	if (!v->u.by.d)
	{
		list->n--;
		return ERR_OUT_OF_MEMORY;
	}
	if (n)
		memcpy (v->u.by.d, data, n);
	v->u.by.n = n;
	return ERR_OK;
}

enumError BFListAddInt (bf_list_t *list, int i)
{
	bf_val_t *v;
	BFE (bf_list_alloc (list, &v));
	v->type = BF_T_INT;
	v->u.i = i;
	return ERR_OK;
}

enumError BFListAddFloat (bf_list_t *list, double f)
{
	bf_val_t *v;
	BFE (bf_list_alloc (list, &v));
	v->type = BF_T_FLOAT;
	v->u.f = f;
	return ERR_OK;
}

enumError BFListAddBool (bf_list_t *list, bool b)
{
	bf_val_t *v;
	BFE (bf_list_alloc (list, &v));
	v->type = BF_T_BOOL;
	v->u.b = b;
	return ERR_OK;
}

bf_node_t *BFListAddNode (bf_list_t *list)
{
	bf_val_t *v;
	if (bf_list_alloc (list, &v))
		return 0;
	v->type = BF_T_NODE;
	v->u.node = (bf_node_t *)MALLOC (sizeof (bf_node_t));
	if (!v->u.node)
	{
		list->n--;
		return 0;
	}
	BFNodeInit (v->u.node);
	return v->u.node;
}

bf_list_t *BFListAddList (bf_list_t *list)
{
	bf_val_t *v;
	if (bf_list_alloc (list, &v))
		return 0;
	v->type = BF_T_LIST;
	v->u.list = (bf_list_t *)MALLOC (sizeof (bf_list_t));
	if (!v->u.list)
	{
		list->n--;
		return 0;
	}
	BFListInit (v->u.list);
	return v->u.list;
}

// type safe getters ---------------------------------------------------------

static ccp bf_get_str (const bf_node_t *node, ccp key)
{
	bf_val_t *v = BFNodeGet ((bf_node_t *)node, key);
	return (v && v->type == BF_T_STR) ? v->u.s : 0;
}

static int bf_get_int (const bf_node_t *node, ccp key, int def)
{
	bf_val_t *v = BFNodeGet ((bf_node_t *)node, key);
	if (v && v->type == BF_T_INT)
		return v->u.i;
	if (v && v->type == BF_T_FLOAT)
		return (int)v->u.f;
	return def;
}

static double bf_get_float (const bf_node_t *node, ccp key, double def)
{
	bf_val_t *v = BFNodeGet ((bf_node_t *)node, key);
	if (v && v->type == BF_T_FLOAT)
		return v->u.f;
	if (v && v->type == BF_T_INT)
		return (double)v->u.i;
	return def;
}

static bool bf_get_bool (const bf_node_t *node, ccp key, bool def)
{
	bf_val_t *v = BFNodeGet ((bf_node_t *)node, key);
	return (v && v->type == BF_T_BOOL) ? v->u.b : def;
}

static bf_node_t *bf_get_node (const bf_node_t *node, ccp key)
{
	bf_val_t *v = BFNodeGet ((bf_node_t *)node, key);
	return (v && v->type == BF_T_NODE) ? v->u.node : 0;
}

static bf_list_t *bf_get_list (const bf_node_t *node, ccp key)
{
	bf_val_t *v = BFNodeGet ((bf_node_t *)node, key);
	return (v && v->type == BF_T_LIST) ? v->u.list : 0;
}

static int bf_strlist_index (const bf_list_t *l, ccp s)
{
	uint i;
	for (i = 0; i < l->n; i++)
		if (l->items[i].type == BF_T_STR && !strcmp (l->items[i].u.s, s))
			return (int)i;
	return -1;
}

static int bf_str_index (const char *const *tab, uint n, ccp s)
{
	uint i;
	for (i = 0; i < n; i++)
		if (tab[i] && s && !strcmp (tab[i], s))
			return (int)i;
	return -1;
}

// count list items (or node keys) with a given type/key prefix
static uint bf_magiccount (const bf_node_t *node, ccp magic)
{
	uint i, n = 0;
	uint mlen = strlen (magic);
	for (i = 0; i < node->n; i++)
	{
		ccp key = node->kv[i].key;
		uint klen = strlen (key);
		// key must be '<magic>-<something>'
		if (klen > mlen && !strncmp (key, magic, mlen) && key[mlen] == '-')
			n++;
	}
	return n;
}

// count keys with a prefix like 'coords-'
static uint bf_keyprefix_count (const bf_node_t *node, ccp prefix)
{
	uint i, n = 0;
	uint plen = strlen (prefix);
	for (i = 0; i < node->n; i++)
		if (!strncmp (node->kv[i].key, prefix, plen))
			n++;
	return n;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			utf-8 / utf-16			///////////////
///////////////////////////////////////////////////////////////////////////////

static uint utf8_append (char *out, u32 cp)
{
	if (cp < 0x80)
	{
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800)
	{
		out[0] = 0xC0 | (cp >> 6);
		out[1] = 0x80 | (cp & 0x3F);
		return 2;
	}
	if (cp < 0x10000)
	{
		out[0] = 0xE0 | (cp >> 12);
		out[1] = 0x80 | ((cp >> 6) & 0x3F);
		out[2] = 0x80 | (cp & 0x3F);
		return 3;
	}
	out[0] = 0xF0 | (cp >> 18);
	out[1] = 0x80 | ((cp >> 12) & 0x3F);
	out[2] = 0x80 | ((cp >> 6) & 0x3F);
	out[3] = 0x80 | (cp & 0x3F);
	return 4;
}

// decode UTF-16 to a malloc'd UTF-8 string
static char *utf16_decode (const u8 *p, uint n, bool be)
{
	if (n & 1)
		n &= ~1;
	// worst case: 1 UTF-16 unit -> 3 bytes UTF-8
	char *out = (char *)MALLOC (n / 2 * 3 + 1);
	if (!out)
		return 0;
	uint o = 0;
	for (uint i = 0; i < n; i += 2)
	{
		u16 u = be ? (u16)((p[i] << 8) | p[i + 1]) : (u16)(p[i] | (p[i + 1] << 8));
		u32 cp;
		if (u >= 0xD800 && u <= 0xDBFF && i + 3 < n)
		{
			u16 lo = be ? (u16)((p[i + 2] << 8) | p[i + 3]) : (u16)(p[i + 2] | (p[i + 3] << 8));
			if (lo >= 0xDC00 && lo <= 0xDFFF)
			{
				cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				i += 2;
			}
			else
				cp = 0xFFFD;
		}
		else if (u >= 0xD800 && u <= 0xDFFF)
			cp = 0xFFFD;
		else
			cp = u;
		o += utf8_append (out + o, cp);
	}
	out[o] = 0;
	return out;
}

// encode UTF-8 string to malloc'd UTF-16 bytes
static u8 *utf8_to_utf16 (ccp s, bool be, uint *out_n)
{
	uint len = strlen (s);
	// max 2 units per codepoint
	u8 *out = (u8 *)MALLOC (len * 2 + 2);
	if (!out)
		return 0;
	uint o = 0;
	const u8 *p = (const u8 *)s;
	uint i = 0;
	while (i < len)
	{
		u32 cp;
		u8 c = p[i];
		if (c < 0x80)
		{
			cp = c;
			i += 1;
		}
		else if ((c >> 5) == 6)
		{
			cp = ((c & 0x1F) << 6) | (p[i + 1] & 0x3F);
			i += 2;
		}
		else if ((c >> 4) == 14)
		{
			cp = ((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
			i += 3;
		}
		else
		{
			cp = ((c & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6)
				| (p[i + 3] & 0x3F);
			i += 4;
		}
		if (cp >= 0x10000)
		{
			u32 v = cp - 0x10000;
			u16 hi = (u16)(0xD800 + (v >> 10));
			u16 lo = (u16)(0xDC00 + (v & 0x3FF));
			if (be)
			{
				out[o] = hi >> 8;
				out[o + 1] = hi;
				out[o + 2] = lo >> 8;
				out[o + 3] = lo;
			}
			else
			{
				out[o] = hi;
				out[o + 1] = hi >> 8;
				out[o + 2] = lo;
				out[o + 3] = lo >> 8;
			}
			o += 4;
		}
		else
		{
			u16 u = (u16)cp;
			if (be)
			{
				out[o] = u >> 8;
				out[o + 1] = u;
			}
			else
			{
				out[o] = u;
				out[o + 1] = u >> 8;
			}
			o += 2;
		}
	}
	*out_n = o;
	return out;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			hex encode/decode			///////////////
///////////////////////////////////////////////////////////////////////////////

static char *bf_hex_encode (const u8 *d, uint n)
{
	char *out = (char *)MALLOC (n * 2 + 1);
	if (!out)
		return 0;
	for (uint i = 0; i < n; i++)
		sprintf (out + i * 2, "%02x", d[i]);
	return out;
}

static enumError bf_hex_decode (ccp hex, bf_buf_t *out)
{
	uint len = strlen (hex);
	if (len & 1)
		return ERR_INVALID_DATA;
	for (uint i = 0; i < len; i += 2)
	{
		int hi = isxdigit ((u8)hex[i])
			? (isdigit ((u8)hex[i]) ? hex[i] - '0' : (tolower ((u8)hex[i]) - 'a' + 10))
			: -1;
		int lo = isxdigit ((u8)hex[i + 1])
			? (isdigit ((u8)hex[i + 1]) ? hex[i + 1] - '0' : (tolower ((u8)hex[i + 1]) - 'a' + 10))
			: -1;
		if (hi < 0 || lo < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_u8 (out, (u8)((hi << 4) | lo)));
	}
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			repr / scalar values			///////////////
///////////////////////////////////////////////////////////////////////////////

// format a double like Python repr() (shortest round-trip)
static void fmt_double (double v, char *buf, uint bufsize)
{
	if (isnan (v))
	{
		snprintf (buf, bufsize, "nan");
		return;
	}
	if (isinf (v))
	{
		snprintf (buf, bufsize, v < 0 ? "-inf" : "inf");
		return;
	}
	snprintf (buf, bufsize, "%.9g", v);
	// verify round-trip; fall back to full precision
	double back = strtod (buf, 0);
	if (memcmp (&back, &v, sizeof (double)))
		snprintf (buf, bufsize, "%.17g", v);
	// force a float-looking literal (Python repr of 608.0 is '608.0')
	if (!strpbrk (buf, ".eE"))
	{
		uint l = strlen (buf);
		if (l + 2 < bufsize)
		{
			buf[l] = '.';
			buf[l + 1] = '0';
			buf[l + 2] = 0;
		}
	}
}

// Python-style repr() of a string: single/double quoted, escaped
static char *bf_repr_str (ccp s)
{
	bool has_q = !!strchr (s, '\'');
	bool has_dq = !!strchr (s, '"');
	char q = (has_q && !has_dq) ? '"' : '\'';
	uint len = strlen (s);
	char *out = (char *)MALLOC (len * 4 + 3);
	if (!out)
		return 0;
	uint o = 0;
	out[o++] = q;
	for (uint i = 0; i < len; i++)
	{
		u8 c = (u8)s[i];
		if (c == '\\' || c == (u8)q)
		{
			out[o++] = '\\';
			out[o++] = (char)c;
		}
		else if (c == '\n')
		{
			out[o++] = '\\';
			out[o++] = 'n';
		}
		else if (c == '\r')
		{
			out[o++] = '\\';
			out[o++] = 'r';
		}
		else if (c == '\t')
		{
			out[o++] = '\\';
			out[o++] = 't';
		}
		else if (c < 0x20 || c == 0x7f)
		{
			out[o++] = '\\';
			out[o++] = 'x';
			static const char hex[] = "0123456789abcdef";
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		}
		else
			out[o++] = (char)c;
	}
	out[o++] = q;
	out[o] = 0;
	return out;
}

// Python-style repr() of a bytes value: b'...'
static char *bf_repr_bytes (const u8 *d, uint n)
{
	char *out = (char *)MALLOC (n * 4 + 4);
	if (!out)
		return 0;
	uint o = 0;
	memcpy (out + o, "b'", 2);
	o += 2;
	for (uint i = 0; i < n; i++)
	{
		u8 c = d[i];
		if (c == '\\' || c == '\'')
		{
			out[o++] = '\\';
			out[o++] = (char)c;
		}
		else if (c >= 0x20 && c < 0x7f)
			out[o++] = (char)c;
		else
		{
			out[o++] = '\\';
			out[o++] = 'x';
			static const char hex[] = "0123456789abcdef";
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		}
	}
	memcpy (out + o, "'", 2);
	o += 1;
	out[o] = 0;
	return out;
}

// unquote a Python string literal token [tok, tok+len); returns malloc'd string
static char *bf_unquote (ccp tok, uint len)
{
	if (len < 2)
		return 0;
	char q = tok[0];
	if (q != '\'' && q != '"')
		return 0;
	if (tok[len - 1] != q)
		return 0;
	char *out = (char *)MALLOC (len + 1);
	if (!out)
		return 0;
	uint o = 0;
	for (uint i = 1; i < len - 1; i++)
	{
		char c = tok[i];
		if (c == '\\' && i + 1 < len - 1)
		{
			char e = tok[++i];
			switch (e)
			{
				case 'n':
					out[o++] = '\n';
					break;
				case 'r':
					out[o++] = '\r';
					break;
				case 't':
					out[o++] = '\t';
					break;
				case 'b':
					out[o++] = '\b';
					break;
				case 'f':
					out[o++] = '\f';
					break;
				case 'a':
					out[o++] = '\a';
					break;
				case 'v':
					out[o++] = '\v';
					break;
				case 'x':
				{
					int hi = isxdigit ((u8)tok[i + 1])
						? (isdigit ((u8)tok[i + 1]) ? tok[i + 1] - '0'
													: tolower ((u8)tok[i + 1]) - 'a' + 10)
						: -1;
					int lo = (i + 2 < len - 1)
						? (isxdigit ((u8)tok[i + 2])
								  ? (isdigit ((u8)tok[i + 2]) ? tok[i + 2] - '0'
															  : tolower ((u8)tok[i + 2]) - 'a' + 10)
								  : -1)
						: -1;
					if (hi >= 0 && lo >= 0)
					{
						out[o++] = (char)((hi << 4) | lo);
						i += 2;
					}
					else
						out[o++] = e;
					break;
				}
				case 'u':
				case 'U':
				{
					uint nd = (e == 'u') ? 4 : 8;
					if (i + nd < len - 1)
					{
						u32 cp = 0;
						bool ok = true;
						for (uint k = 0; k < nd; k++)
						{
							char h = tok[i + 1 + k];
							int v = isxdigit ((u8)h)
								? (isdigit ((u8)h) ? h - '0' : tolower ((u8)h) - 'a' + 10)
								: -1;
							if (v < 0)
							{
								ok = false;
								break;
							}
							cp = cp * 16 + (u32)v;
						}
						if (ok)
						{
							o += utf8_append (out + o, cp);
							i += nd;
							break;
						}
					}
					out[o++] = e;
					break;
				}
				default:
					out[o++] = e;
					break;
			}
		}
		else
			out[o++] = c;
	}
	out[o] = 0;
	return out;
}

// parse a Python bytes literal b'...'
static enumError bf_unbytes (ccp tok, uint len, bf_buf_t *out)
{
	if (len < 4 || tok[0] != 'b')
		return ERR_INVALID_DATA;
	char q = tok[1];
	if ((q != '\'' && q != '"') || tok[len - 1] != q)
		return ERR_INVALID_DATA;
	for (uint i = 2; i < len - 1; i++)
	{
		char c = tok[i];
		if (c == '\\' && i + 1 < len - 1)
		{
			char e = tok[++i];
			if (e == 'x' && i + 2 < len - 1)
			{
				int hi = isxdigit ((u8)tok[i + 1])
					? (isdigit ((u8)tok[i + 1]) ? tok[i + 1] - '0'
												: tolower ((u8)tok[i + 1]) - 'a' + 10)
					: -1;
				int lo = isxdigit ((u8)tok[i + 2])
					? (isdigit ((u8)tok[i + 2]) ? tok[i + 2] - '0'
												: tolower ((u8)tok[i + 2]) - 'a' + 10)
					: -1;
				if (hi >= 0 && lo >= 0)
				{
					BFE (bf_buf_u8 (out, (u8)((hi << 4) | lo)));
					i += 2;
					continue;
				}
			}
			if (e == 'n')
				BFE (bf_buf_u8 (out, '\n'));
			else if (e == 'r')
				BFE (bf_buf_u8 (out, '\r'));
			else if (e == 't')
				BFE (bf_buf_u8 (out, '\t'));
			else if (e == '\\')
				BFE (bf_buf_u8 (out, '\\'));
			else if (e == '\'')
				BFE (bf_buf_u8 (out, '\''));
			else if (e == '"')
				BFE (bf_buf_u8 (out, '"'));
			else
				BFE (bf_buf_u8 (out, (u8)e));
		}
		else
			BFE (bf_buf_u8 (out, (u8)c));
	}
	return ERR_OK;
}

// evaluate a scalar text token (Python eval equivalent)
static enumError bf_eval_scalar (ccp tok, uint len, bf_val_t *out)
{
	// trim
	while (len && isspace ((u8)*tok))
	{
		tok++;
		len--;
	}
	while (len && isspace ((u8)tok[len - 1]))
		len--;
	if (!len)
		return ERR_SYNTAX;

	// bytes literal
	if (len >= 2 && tok[0] == 'b' && (tok[1] == '\'' || tok[1] == '"'))
	{
		bf_buf_t tmp;
		memset (&tmp, 0, sizeof (tmp));
		enumError err = bf_unbytes (tok, len, &tmp);
		if (err)
			return err;
		out->type = BF_T_BYTES;
		out->u.by.d = tmp.d;
		out->u.by.n = tmp.n;
		return ERR_OK;
	}

	// quoted string
	if (tok[0] == '\'' || tok[0] == '"')
	{
		char *s = bf_unquote (tok, len);
		if (!s)
			return ERR_SYNTAX;
		out->type = BF_T_STR;
		out->u.s = s;
		return ERR_OK;
	}

	// True / False / None
	if (len == 4 && !strncasecmp (tok, "true", 4))
	{
		out->type = BF_T_BOOL;
		out->u.b = true;
		return ERR_OK;
	}
	if (len == 5 && !strncasecmp (tok, "false", 5))
	{
		out->type = BF_T_BOOL;
		out->u.b = false;
		return ERR_OK;
	}
	if (len == 4 && !strncasecmp (tok, "none", 4))
	{
		out->type = BF_T_NONE;
		return ERR_OK;
	}

	// integer?
	{
		char *tmp = (char *)MALLOC (len + 1);
		if (!tmp)
			return ERR_OUT_OF_MEMORY;
		memcpy (tmp, tok, len);
		tmp[len] = 0;
		char *end = 0;
		long long ll = strtoll (tmp, &end, 10);
		if (end && *end == 0 && end != tmp)
		{
			FREE (tmp);
			out->type = BF_T_INT;
			out->u.i = (int)ll;
			return ERR_OK;
		}
		// float?
		double d = strtod (tmp, &end);
		bool valid = (end && *end == 0 && end != tmp);
		FREE (tmp);
		if (valid)
		{
			out->type = BF_T_FLOAT;
			out->u.f = d;
			return ERR_OK;
		}
	}
	return ERR_SYNTAX;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			txtree dump / load			///////////////
///////////////////////////////////////////////////////////////////////////////

static void dump_indent (bf_buf_t *out, int depth)
{
	for (int i = 0; i < depth; i++)
		bf_buf_raw (out, "\t|", 2);
}

static enumError dump_scalar (bf_buf_t *out, const bf_val_t *v)
{
	char tmp[64];
	char *s;
	switch (v->type)
	{
		case BF_T_NONE:
			return bf_buf_raw (out, "None", 4);
		case BF_T_BOOL:
			return bf_buf_raw (out, v->u.b ? "True" : "False", v->u.b ? 4 : 5);
		case BF_T_INT:
			snprintf (tmp, sizeof (tmp), "%d", v->u.i);
			return bf_buf_raw (out, tmp, strlen (tmp));
		case BF_T_FLOAT:
			fmt_double (v->u.f, tmp, sizeof (tmp));
			return bf_buf_raw (out, tmp, strlen (tmp));
		case BF_T_STR:
			s = bf_repr_str (v->u.s);
			if (!s)
				return ERR_OUT_OF_MEMORY;
			enumError e = bf_buf_raw (out, s, strlen (s));
			FREE (s);
			return e;
		case BF_T_BYTES:
			s = bf_repr_bytes (v->u.by.d, v->u.by.n);
			if (!s)
				return ERR_OUT_OF_MEMORY;
			enumError e2 = bf_buf_raw (out, s, strlen (s));
			FREE (s);
			return e2;
		default:
			break;
	}
	return ERR_INVALID_DATA;
}

static enumError dump_list_rec (bf_buf_t *out, const bf_list_t *list, int depth);

static enumError dump_node_rec (bf_buf_t *out, const bf_node_t *node, int depth)
{
	for (uint i = 0; i < node->n; i++)
	{
		ccp key = node->kv[i].key;
		if (key[0] == '_' && key[1] == '_')
			continue;
		const bf_val_t *v = &node->kv[i].val;
		char *rkey = bf_repr_str (key);
		if (!rkey)
			return ERR_OUT_OF_MEMORY;
		switch (v->type)
		{
			case BF_T_NODE:
				dump_indent (out, depth);
				BFE (bf_buf_raw (out, rkey, strlen (rkey)));
				BFE (bf_buf_raw (out, ": \n", 3));
				BFE (dump_node_rec (out, v->u.node, depth + 1));
				break;
			case BF_T_LIST:
				dump_indent (out, depth);
				BFE (bf_buf_raw (out, rkey, strlen (rkey)));
				BFE (bf_buf_raw (out, ": list\n", 7));
				BFE (dump_list_rec (out, v->u.list, depth + 1));
				break;
			default:
				dump_indent (out, depth);
				BFE (bf_buf_raw (out, rkey, strlen (rkey)));
				BFE (bf_buf_raw (out, ": ", 2));
				BFE (dump_scalar (out, v));
				BFE (bf_buf_u8 (out, '\n'));
				break;
		}
		FREE (rkey);
	}
	return ERR_OK;
}

static enumError dump_list_rec (bf_buf_t *out, const bf_list_t *list, int depth)
{
	char idx[16];
	for (uint i = 0; i < list->n; i++)
	{
		const bf_val_t *v = &list->items[i];
		snprintf (idx, sizeof (idx), "%u", i);
		switch (v->type)
		{
			case BF_T_NODE:
				dump_indent (out, depth);
				BFE (bf_buf_raw (out, idx, strlen (idx)));
				BFE (bf_buf_raw (out, ": \n", 3));
				BFE (dump_node_rec (out, v->u.node, depth + 1));
				break;
			case BF_T_LIST:
				dump_indent (out, depth);
				BFE (bf_buf_raw (out, idx, strlen (idx)));
				BFE (bf_buf_raw (out, ": list\n", 7));
				BFE (dump_list_rec (out, v->u.list, depth + 1));
				break;
			default:
				dump_indent (out, depth);
				BFE (bf_buf_raw (out, idx, strlen (idx)));
				BFE (bf_buf_raw (out, ": ", 2));
				BFE (dump_scalar (out, v));
				BFE (bf_buf_u8 (out, '\n'));
				break;
		}
	}
	return ERR_OK;
}

char *BFTreeDump (const bf_node_t *root)
{
	bf_buf_t out;
	memset (&out, 0, sizeof (out));
	if (dump_node_rec (&out, root, 0))
	{
		bf_buf_free (&out);
		return 0;
	}
	enumError rerr = bf_buf_reserve (&out, 1);
	if (rerr)
	{
		bf_buf_free (&out);
		return 0;
	}
	out.d[out.n] = 0;
	return (char *)out.d;
}

// load children of a container; 'child' = nested, 'strip' = the number of
// '\t|' prefixes those nested lines must start with (one per depth level)
static enumError load_container (ccp p, bool child, bool as_list, bf_list_t *list_out,
	bf_node_t *node_out, uint strip, ccp *endp)
{
	ccp cur = p;
	for (;;)
	{
		// each child line must start with at least 'strip' '\t|' prefixes
		// (one per nesting depth); fewer means the line is a sibling of the
		// current container and belongs to the caller - stop processing
		if (child)
		{
			ccp start = cur;
			uint n = 0;
			while (cur[0] == '\t' && cur[1] == '|')
			{
				cur += 2;
				n++;
			}
			if (n < strip)
			{
				cur = start;
				break;
			}
			cur = start + (strip << 1);
		}
		else if (!*cur)
			break;

		// skip comment lines ('#...'), e.g. the '#FLYT' text magic line
		if (*cur == '#')
		{
			ccp nl = strchr (cur, '\n');
			cur = nl ? nl + 1 : cur + strlen (cur);
			continue;
		}

		// find end of line
		ccp nl = strchr (cur, '\n');
		ccp eol = nl ? nl : cur + strlen (cur);
		ccp sep = (ccp)memchr (cur, ':', (size_t)(eol - cur));
		while (sep)
		{
			if ((sep + 1 < eol) && sep[1] == ' ')
				break;
			sep = (ccp)memchr (sep + 1, ':', (size_t)(eol - sep - 1));
		}
		if (!sep)
		{
			// line without ': ' - stop like benzin's IndexError break
			break;
		}
		ccp keytok = cur;
		uint keylen = (uint)(sep - cur);
		ccp valtok = sep + 2;
		uint vallen = (uint)(eol - valtok);
		if (vallen && eol[-1] == '\r')
			vallen--;

		// strip value
		uint v2 = vallen;
		while (v2 && isspace ((u8)valtok[v2 - 1]))
			v2--;
		uint v3 = 0;
		while (v3 < v2 && isspace ((u8)valtok[v3]))
			v3++;

		bool is_container = (v2 == 0) || (v2 - v3 == 4 && !strncmp (valtok + v3, "list", 4))
			|| (v2 - v3 == 5 && !strncmp (valtok + v3, "tuple", 5));
		bool is_list = (v2 - v3 == 4 && !strncmp (valtok + v3, "list", 4));

		ccp next = nl ? nl + 1 : eol;

		if (is_container)
		{
			bf_node_t *cn = 0;
			bf_list_t *cl = 0;
			if (as_list)
			{
				if (is_list)
					cl = BFListAddList (list_out);
				else
					cn = BFListAddNode (list_out);
				if (!cl && !cn)
					return ERR_OUT_OF_MEMORY;
			}
			else
			{
				char *key = bf_unquote (keytok, keylen);
				if (!key)
					key = bf_strndup (keytok, keylen);
				if (!key)
					return ERR_OUT_OF_MEMORY;
				if (is_list)
					cl = BFNodeSetList (node_out, key);
				else
					cn = BFNodeSetNode (node_out, key);
				FREE (key);
				if (!cl && !cn)
					return ERR_OUT_OF_MEMORY;
			}
			ccp child_end;
			enumError err = load_container (next, true, is_list, cl, cn, strip + 1, &child_end);
			if (err)
				return err;
			cur = child_end;
		}
		else
		{
			bf_val_t val;
			memset (&val, 0, sizeof (val));
			enumError err = bf_eval_scalar (valtok + v3, v2 - v3, &val);
			if (err)
				return err;
			if (as_list)
			{
				// append to list
				bf_val_t *slot;
				if (bf_list_alloc (list_out, &slot))
				{
					BFValClear (&val);
					return ERR_OUT_OF_MEMORY;
				}
				*slot = val;
			}
			else
			{
				char *key = bf_unquote (keytok, keylen);
				if (!key)
				{
					// numeric key (unquoted) - store as decimal string
					char *tmp = bf_strndup (keytok, keylen);
					if (!tmp)
					{
						BFValClear (&val);
						return ERR_OUT_OF_MEMORY;
					}
					key = tmp;
				}
				bf_val_t *slot;
				if (bf_node_alloc (node_out, key, &slot))
				{
					FREE (key);
					BFValClear (&val);
					return ERR_OUT_OF_MEMORY;
				}
				FREE (key);
				*slot = val;
			}
			cur = next;
		}
	}
	if (endp)
		*endp = cur;
	return ERR_OK;
}

enumError BFTreeLoad (const char *text, uint len, bf_node_t *root)
{
	char *buf = (char *)MALLOC (len + 1);
	if (!buf)
		return ERR_OUT_OF_MEMORY;
	if (len)
		memcpy (buf, text, len);
	buf[len] = 0;
	enumError err = load_container (buf, false, false, 0, root, 0, 0);
	FREE (buf);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			reference string tables		///////////////
///////////////////////////////////////////////////////////////////////////////

static const char *const WRAPS[8] = { "Near-Clamp", "Near-Repeat", "Near-Mirror", "GX2-Mirror-Once",
	"Clamp", "Repeat", "Mirror", "GX2-Mirror-Once-Border" };

static const char *const MAPPING_METHODS[5]
	= { "UV-Mapping", "", "", "Orthogonal-Projection", "PaneBasedProjection" };

// 3DS mat1 TexCoordGen source (distinct from the Wii MAPPING_METHODS above --
// verified against Gericom/EveryFileExplorer's mat1.cs reference decoder)
static const char *const TEXCOORD_GEN_SOURCE[6] = { "Tex0", "Tex1", "Tex2", "Orthogonal-Projection",
	"PaneBasedProjection", "Perspective-Projection" };

static const char *const COLOR_BLENDS[12] = { "Overwrite", "Multiply", "Add", "Exclude", "4",
	"Subtract", "Dodge", "Burn", "Overlay", "Indirect", "Blend-Indirect", "Each-Indirect" };

static const char *const BLENDS[2] = { "Max", "Min" };

static const char *const ALPHA_COMPARE_CONDITIONS[8] = { "Never", "Less", "Less-or-Equal", "Equal",
	"Not-Equal", "Greater-or-Equal", "Greater", "Always" };

static const char *const BLEND_CALC[10] = { "0", "1", "FBColor", "1-FBColor", "PixelAlpha",
	"1-PixelAlpha", "FBAlpha", "1-FBAlpha", "PixelColor", "1-PixelColor" };

static const char *const BLEND_CALC_OPS[6]
	= { "0", "Add", "Subtract", "Reverse-Subtract", "Min", "Max" };

static const char *const LOGICAL_CALC_OPS[17] = { "None", "NoOp", "Clear", "Set", "Copy", "InvCopy",
	"Inv", "And", "Nand", "Or", "Nor", "Xor", "Equiv", "RevAnd", "InvAnd", "RevOr", "InvOr" };

static const char *const PROJECTION_MAPPING_TYPES[7]
	= { "Standard", "Entire-Layout", "2", "3", "Pane-RandS-Projection", "5", "6" };

static const char *const TEXT_ALIGNS[4] = { "NA", "Left", "Center", "Right" };
static const char *const ORIG_X[3] = { "Center", "Left", "Right" };
static const char *const ORIG_Y[3] = { "Center", "Up", "Down" };

//
///////////////////////////////////////////////////////////////////////////////
///////////////			reader context			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct bf_rctx_t
{
	bool be; // big endian
	bool is_rlyt; // Wii RLYT / RLAN
	bool is_wiiu; // Wii U FLYT / FLAN (see readpane())
	u32 version; // header version
	const u8 *data; // whole file
	uint size;
	bf_node_t *tree; // root
	bf_node_t *actnode; // current node
	char prevname[64]; // current level pane name
	char prevname_stack[32][64];
	int grsnum; // current level group counter
	int grsnum_stack[32];
	bf_node_t *actnode_stack[32];
	int sp; // stack pointer
	bf_list_t texturenames;
	bf_list_t fontnames;
	bf_list_t materials; // list of node
} bf_rctx_t;

static void rctx_push (bf_rctx_t *ctx, bf_node_t *node)
{
	if (ctx->sp < 32)
	{
		strcpy (ctx->prevname_stack[ctx->sp], ctx->prevname);
		ctx->grsnum_stack[ctx->sp] = ctx->grsnum;
		ctx->actnode_stack[ctx->sp] = ctx->actnode;
		ctx->sp++;
	}
	ctx->actnode = node;
	ctx->prevname[0] = 0;
	ctx->grsnum = 0;
}

static void rctx_pop (bf_rctx_t *ctx)
{
	if (ctx->sp > 0)
	{
		ctx->sp--;
		strcpy (ctx->prevname, ctx->prevname_stack[ctx->sp]);
		ctx->grsnum = ctx->grsnum_stack[ctx->sp];
		ctx->actnode = ctx->actnode_stack[ctx->sp];
	}
}

static inline u16 rd16 (const u8 *p, bool be)
{
	return be ? (u16)((p[0] << 8) | p[1]) : (u16)(p[0] | (p[1] << 8));
}

static inline u32 rd32 (const u8 *p, bool be)
{
	if (be)
		return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline int rds16 (const u8 *p, bool be)
{
	u16 v = rd16 (p, be);
	return (int16_t)v;
}

static inline int rds32 (const u8 *p, bool be)
{
	return (int32_t)rd32 (p, be);
}

static inline float rdf32 (const u8 *p, bool be)
{
	union
	{
		u32 i;
		float f;
	} u;
	u.i = rd32 (p, be);
	return u.f;
}

static bool rb_ok (const bf_rctx_t *ctx, uint ptr, uint n)
{
	return ptr <= ctx->size && n <= ctx->size - ptr;
}

// read NUL-terminated ascii string at 'ptr', bounded by 'limit' bytes
static enumError rb_strn (bf_rctx_t *ctx, const u8 *d, uint size, uint ptr, uint limit, char **out)
{
	uint end = ptr;
	uint max = ptr + limit;
	if (max > size)
		max = size;
	while (end < max && d[end])
		end++;
	char *s = bf_strndup ((ccp)d + ptr, end - ptr);
	if (!s)
		return ERR_OUT_OF_MEMORY;
	*out = s;
	return ERR_OK;
}

static enumError rb_str (bf_rctx_t *ctx, const u8 *d, uint size, uint ptr, char **out)
{
	return rb_strn (ctx, d, size, ptr, size - ptr, out);
}

static enumError rb_color (bf_rctx_t *ctx, const u8 *d, uint size, uint ptr, bf_node_t *node)
{
	if (!rb_ok (ctx, ptr, 3))
		return ERR_INVALID_DATA;
	BFE (BFNodeSetInt (node, "RED", d[ptr]));
	BFE (BFNodeSetInt (node, "GREEN", d[ptr + 1]));
	BFE (BFNodeSetInt (node, "BLUE", d[ptr + 2]));
	if (rb_ok (ctx, ptr, 4))
		BFE (BFNodeSetInt (node, "ALPHA", d[ptr + 3]));
	return ERR_OK;
}

// read the common pane header (76 bytes) into 'node'
static enumError readpane (bf_rctx_t *ctx, const u8 *d, uint size, uint *ptr, bf_node_t *node)
{
	uint p = *ptr;
	if (!rb_ok (ctx, p, ctx->is_wiiu ? 76 : 68))
		return ERR_INVALID_DATA;
	u8 flags = d[p];
	BFE (BFNodeSetBool (node, "visible", (flags & 0x01) != 0));
	BFE (BFNodeSetBool (node, "transmit-alpha-to-children", (flags & 0x02) != 0));
	BFE (BFNodeSetBool (node, "position-adjustment", (flags & 0x04) != 0));
	u8 origin = d[p + 1];
	u8 mainorigin = origin % 16;
	u8 parentorigin = origin / 16;
	bf_node_t *orignode = BFNodeSetNode (node, "origin");
	if (!orignode)
		return ERR_OUT_OF_MEMORY;
	BFE (BFNodeSetStr (orignode, "x", ORIG_X[mainorigin % 4]));
	BFE (BFNodeSetStr (orignode, "y", ORIG_Y[mainorigin / 4]));
	orignode = BFNodeSetNode (node, "parent-origin");
	if (!orignode)
		return ERR_OUT_OF_MEMORY;
	BFE (BFNodeSetStr (orignode, "x", ORIG_X[parentorigin % 4]));
	BFE (BFNodeSetStr (orignode, "y", ORIG_Y[parentorigin / 4]));
	BFE (BFNodeSetInt (node, "alpha", d[p + 2]));
	BFE (BFNodeSetInt (node, "part-scale", d[p + 3]));

	// Verified against a real 3DS pan1/txt1 pair (Gericom/EveryFileExplorer's
	// pan1.cs): the name field is 24 bytes, not the Wii RLYT's 32 -- with the
	// old width, every field after the name (translation/rotation/scale/size,
	// and every txt1/pic1/wnd1/bnd1 field beyond that) was read 8 bytes out
	// of place. Confirmed by re-decoding a real txt1's MaterialId with the
	// corrected offset: it now resolves to the pane's own material by name,
	// where the old offset produced an out-of-range index.
	char *name;
	BFE (rb_strn (ctx, d, size, p + 4, 24, &name));
	BFE (BFNodeSetStr (node, "name", name));
	strncpy (ctx->prevname, name, sizeof (ctx->prevname) - 1);
	ctx->prevname[sizeof (ctx->prevname) - 1] = 0;
	FREE (name);
	p += 28;

	// Wii U's FLYT/FLAN pane struct has an extra 8-byte "user data" field
	// here that the 3DS CLYT struct (and this project's earlier reference,
	// Gericom/EveryFileExplorer's pan1.cs, which only documents 3DS) does
	// not have -- confirmed against kinnay/Nintendo-File-Formats' bflyt.md
	// (Pane struct: 0x24 "User data", 8 bytes, between name and X position)
	// and cross-checked via byte-accounting on real retail files: without
	// skipping it, every field below (translation/rotation/scale/width/
	// height, and everything in pic1/txt1/wnd1 built on top of this) reads
	// 8 bytes early. Seen consistently across two real Wii U games and
	// FLYT versions 4.0.0.0 and 5.2.0.0; no 3DS sample was available to
	// confirm CLYT lacks it, so this is gated on the FLYT/FLAN magic
	// rather than applied unconditionally.
	if (ctx->is_wiiu)
		p += 8;

	BFE (BFNodeSetFloat (node, "X-translation", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "Y-translation", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "Z-translation", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "X-rotation", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "Y-rotation", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "Z-rotation", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "X-scale", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "Y-scale", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "width", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "height", rdf32 (d + p, ctx->be)));
	p += 4;

	*ptr = p;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			section readers			///////////////
///////////////////////////////////////////////////////////////////////////////

// Verified against a real 3DS lyt1 plus Gericom/EveryFileExplorer's CLYT.cs:
// the real struct is just ScreenOrigin(u32 enum) + LayoutSize(2 floats) = 12
// bytes -- not the Wii RLYT's 20-byte struct (drawn-from-middle bool + 4
// floats) plus a trailing name string. With the old layout every float here
// silently decoded to garbage (no bounds check catches a wrong-but-in-range
// offset) instead of erroring, so this was never caught by the "does it
// fail" corpus sweep that found grp1/mat1/pan1 -- found by noticing the
// decoded max-parts-width/height values were absurd (~1e-9/1e-43) on a real
// file, not by a parse failure.
static enumError r_lyt1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	bf_node_t *node = BFNodeSetNode (ctx->actnode, "lyt1");
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;

	// Same platform split as readpane()'s "user data" field: Wii U's FLYT
	// lyt1 is a different, longer struct than the 3DS CLYT one this code
	// was verified against (per kinnay/Nintendo-File-Formats' bflyt.md):
	// a bool + 3 padding bytes, width/height/max-parts-height/max-parts-
	// width floats, then a trailing null-terminated name -- not the 3DS
	// struct's plain origin-enum + 2 floats. Confirmed on real files: the
	// 3DS-shaped read hit `origin >= 2` (an always-invalid enum value) on
	// every real Wii U lyt1 tried, hard-failing the whole file.
	if (ctx->is_wiiu)
	{
		if (!rb_ok (ctx, p, 20))
			return ERR_INVALID_DATA;
		BFE (BFNodeSetBool (node, "drawn-from-middle", d[p] != 0));
		p += 4; // +3 padding
		BFE (BFNodeSetFloat (node, "screen-width", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (node, "screen-height", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (node, "max-parts-height", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (node, "max-parts-width", rdf32 (d + p, ctx->be)));
		p += 4;
		if (p < size)
		{
			char *name;
			BFE (rb_strn (ctx, d, size, p, size - p, &name));
			BFE (BFNodeSetStr (node, "name", name));
			FREE (name);
		}
		return ERR_OK;
	}

	if (!rb_ok (ctx, p, 12))
		return ERR_INVALID_DATA;
	u32 origin = rd32 (d + p, ctx->be);
	p += 4;
	if (origin >= 2)
		return ERR_INVALID_DATA;
	BFE (BFNodeSetStr (node, "screen-origin", origin ? "Normal" : "Classic"));
	BFE (BFNodeSetFloat (node, "screen-width", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "screen-height", rdf32 (d + p, ctx->be)));
	p += 4;
	return ERR_OK;
}

static enumError r_name_list (bf_rctx_t *ctx, const u8 *d, uint size, ccp sec_key, ccp count_key,
	ccp names_key, bf_list_t *cache)
{
	bf_node_t *node = BFNodeSetNode (ctx->actnode, sec_key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	if (!rb_ok (ctx, p, 2))
		return ERR_INVALID_DATA;
	uint num = rd16 (d + p, ctx->be);
	p += 4;
	if (!rb_ok (ctx, p, num * 4))
		return ERR_INVALID_DATA;
	uint startentries = p;
	bf_list_t *names = BFNodeSetList (node, names_key);
	if (!names)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < num; i++)
	{
		u32 off = rd32 (d + p, ctx->be);
		p += 4;
		if (!rb_ok (ctx, startentries + off, 0))
			return ERR_INVALID_DATA;
		char *s;
		BFE (rb_str (ctx, d, size, startentries + off, &s));
		BFE (BFListAddStr (names, s));
		BFE (BFListAddStr (cache, s));
		FREE (s);
	}
	BFE (BFNodeSetInt (node, count_key, (int)num));
	// move count key in front of the names list
	struct bf_kv_t *ck = bf_node_find (node, count_key);
	struct bf_kv_t *nk = bf_node_find (node, names_key);
	if (ck && nk && ck > nk)
	{
		struct bf_kv_t tmp = *ck;
		memmove (nk + 1, nk, (size_t)(ck - nk) * sizeof (*ck));
		*nk = tmp;
	}
	return ERR_OK;
}

static enumError r_txl1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	return r_name_list (ctx, d, size, "txl1", "texture-number", "file-names", &ctx->texturenames);
}

static enumError r_fnl1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	return r_name_list (ctx, d, size, "fnl1", "fonts-number", "file-names", &ctx->fontnames);
}

static enumError r_mat1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	bf_node_t *node = BFNodeSetNode (ctx->actnode, "mat1");
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	if (!rb_ok (ctx, p, 2))
		return ERR_INVALID_DATA;
	uint num = rd16 (d + p, ctx->be);
	p += 4;
	if (!rb_ok (ctx, p, num * 4))
		return ERR_INVALID_DATA;
	u32 *offsets = (u32 *)MALLOC (num ? num * 4 : 1);
	if (!offsets)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < num; i++)
	{
		offsets[i] = rd32 (d + p, ctx->be);
		p += 4;
	}
	bf_list_t *materials = BFNodeSetList (node, "materials");
	if (!materials)
	{
		FREE (offsets);
		return ERR_OUT_OF_MEMORY;
	}
	uint ptr = 0;

	// Wii U's mat1 material struct is different from the 3DS one this code
	// was originally verified against (Gericom/EveryFileExplorer's
	// mat1.cs, 3DS-only): 28-byte name (not 20), separate 4-byte
	// foreground/background colors (not one buffer-color + six const-
	// colors), and a flags bitfield with different bit widths -- notably
	// a new "mapping settings" count with no 3DS equivalent, a 2-bit (not
	// 3-bit) combiner-stage count using a 4-byte struct (not the 3DS
	// TevStage's 12-byte one), and color/alpha blend mode as 2-bit COUNTS
	// (not single presence bits). Cross-checked against Tyulis/3DSkit's
	// BFLYT.md (a documentation source; its constant tables -- WRAPS,
	// MAPPING_METHODS, BLENDS, COLOR_BLENDS, BLEND_CALC, BLEND_CALC_OPS,
	// LOGICAL_CALC_OPS, PROJECTION_MAPPING_TYPES -- all already exist
	// verbatim above in this file, apparently prepared for this at some
	// point and never wired up) and verified via byte-accounting against
	// two independent real materials from a real Splatoon USA file (108
	// and 72 bytes): both consume their declared size exactly, zero
	// slack. This is the version < 8.0.0 layout (fg/bg colors before
	// flags); the >= 8.0.0 layout swaps that order per 3DSkit's own doc,
	// marked there as an unverified guess -- no real >= 8.0.0 (Switch-era)
	// BFLYT sample was available to check, so that branch isn't
	// implemented, only version < 8.0.0 (real files seen: 4.0.0.0,
	// 5.0.0.0, 5.2.0.0).
	if (ctx->is_wiiu)
	{
		for (uint i = 0; i < num; i++)
		{
			if (offsets[i] > size || !rb_ok (ctx, offsets[i], 28 + 4 + 4 + 4))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			ptr = offsets[i];
			bf_node_t *mat = BFListAddNode (materials);
			if (!mat)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}

			char *name;
			BFE (rb_strn (ctx, d, size, ptr, 28, &name));
			BFE (BFNodeSetStr (mat, "name", name));
			FREE (name);
			ptr += 28;

			bf_node_t *color = BFNodeSetNode (mat, "foreground-color");
			if (!color)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (rb_color (ctx, d, size, ptr, color));
			ptr += 4;
			color = BFNodeSetNode (mat, "background-color");
			if (!color)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (rb_color (ctx, d, size, ptr, color));
			ptr += 4;

			u32 flags = rd32 (d + ptr, ctx->be);
			ptr += 4;
			BFE (BFNodeSetInt (mat, "flags", (int)flags));

			uint texref = (flags >> 0) & 3;
			uint texturesrt = (flags >> 2) & 3;
			uint mapsettings = (flags >> 4) & 3;
			uint combiners = (flags >> 6) & 3;
			bool alphacmp = (flags >> 9) & 1;
			// Tyulis/3DSkit's BFLYT.md documents bits 10-11 as a 2-bit "number
			// of blending modes" count, but real files disprove that: three
			// real materials in one real file (flags=0x815, bit11 set, bit10
			// clear) only balance their declared section size (byte-
			// accounting, zero slack) if blend-mode contributes 0 bytes here
			// -- a 2-bit count reading gives blendmode=2, which overshoots by
			// exactly one blend-mode struct (8 bytes short on a 72-byte
			// entry). Bit 10 alone as a single presence bit (0 or 1 struct,
			// matching the 3DS format's original single-bit design) is the
			// only reading that balances. Bit 11 is real (set in real data)
			// but its meaning is still unknown -- not decoded here, just left
			// unconsumed in the stored `flags` integer rather than guessed at.
			bool blendmode = (flags >> 10) & 1;
			uint alphablend = (flags >> 12) & 3;
			bool indirect = (flags >> 14) & 1;
			uint projection = (flags >> 15) & 3;
			bool shadow = (flags >> 17) & 1;

			for (uint k = 0; k < texref; k++)
			{
				if (!rb_ok (ctx, ptr, 4))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				uint tex = rd16 (d + ptr, ctx->be);
				u8 ws = d[ptr + 2], wt = d[ptr + 3];
				ptr += 4;
				if (tex >= ctx->texturenames.n || ws >= 8 || wt >= 8)
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				char key[24];
				snprintf (key, sizeof (key), "texref-%u", k);
				bf_node_t *fn = BFNodeSetNode (mat, key);
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetStr (fn, "file", ctx->texturenames.items[tex].u.s));
				BFE (BFNodeSetStr (fn, "wrap-S", WRAPS[ws]));
				BFE (BFNodeSetStr (fn, "wrap-T", WRAPS[wt]));
			}
			for (uint k = 0; k < texturesrt; k++)
			{
				if (!rb_ok (ctx, ptr, 20))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				char key[24];
				snprintf (key, sizeof (key), "textureSRT-%u", k);
				bf_node_t *fn = BFNodeSetNode (mat, key);
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetFloat (fn, "X-translate", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "Y-translate", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "rotate", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "X-scale", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "Y-scale", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
			}
			for (uint k = 0; k < mapsettings; k++)
			{
				if (!rb_ok (ctx, ptr, 8))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				u8 method = d[ptr + 1];
				if (method >= 5)
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				char key[24];
				snprintf (key, sizeof (key), "mapping-setting-%u", k);
				bf_node_t *fn = BFNodeSetNode (mat, key);
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetInt (fn, "unknown", d[ptr]));
				BFE (BFNodeSetStr (fn, "method", MAPPING_METHODS[method]));
				ptr += 8;
			}
			for (uint k = 0; k < combiners; k++)
			{
				if (!rb_ok (ctx, ptr, 4))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				u8 cb = d[ptr], ab = d[ptr + 1];
				if (cb >= 12 || ab >= 2)
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				char key[24];
				snprintf (key, sizeof (key), "texture-combiner-%u", k);
				bf_node_t *fn = BFNodeSetNode (mat, key);
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetStr (fn, "color-blending", COLOR_BLENDS[cb]));
				BFE (BFNodeSetStr (fn, "alpha-blending", BLENDS[ab]));
				BFE (BFNodeSetInt (fn, "unknown", rd16 (d + ptr + 2, ctx->be)));
				ptr += 4;
			}
			if (alphacmp)
			{
				if (!rb_ok (ctx, ptr, 8))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				u32 cond = d[ptr];
				if (cond >= 8)
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				bf_node_t *fn = BFNodeSetNode (mat, "alpha-compare");
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetStr (fn, "condition", ALPHA_COMPARE_CONDITIONS[cond]));
				BFE (BFNodeSetFloat (fn, "value", rdf32 (d + ptr + 4, ctx->be)));
				ptr += 8;
			}
			if (blendmode)
			{
				if (!rb_ok (ctx, ptr, 4))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				if (d[ptr] >= 6 || d[ptr + 1] >= 10 || d[ptr + 2] >= 10 || d[ptr + 3] >= 17)
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				bf_node_t *fn = BFNodeSetNode (mat, "blend-mode");
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetStr (fn, "blend-operation", BLEND_CALC_OPS[d[ptr]]));
				BFE (BFNodeSetStr (fn, "source", BLEND_CALC[d[ptr + 1]]));
				BFE (BFNodeSetStr (fn, "destination", BLEND_CALC[d[ptr + 2]]));
				BFE (BFNodeSetStr (fn, "logical-operation", LOGICAL_CALC_OPS[d[ptr + 3]]));
				ptr += 4;
			}
			for (uint k = 0; k < alphablend; k++)
			{
				if (!rb_ok (ctx, ptr, 4))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				if (d[ptr] >= 6 || d[ptr + 1] >= 10 || d[ptr + 2] >= 10 || d[ptr + 3] >= 17)
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				char key[24];
				snprintf (key, sizeof (key), "alpha-blend-mode-%u", k);
				bf_node_t *fn = BFNodeSetNode (mat, key);
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetStr (fn, "blend-operation", BLEND_CALC_OPS[d[ptr]]));
				BFE (BFNodeSetStr (fn, "source", BLEND_CALC[d[ptr + 1]]));
				BFE (BFNodeSetStr (fn, "destination", BLEND_CALC[d[ptr + 2]]));
				BFE (BFNodeSetStr (fn, "logical-operation", LOGICAL_CALC_OPS[d[ptr + 3]]));
				ptr += 4;
			}
			if (indirect)
			{
				if (!rb_ok (ctx, ptr, 12))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				bf_node_t *fn = BFNodeSetNode (mat, "indirect-adjustment");
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetFloat (fn, "rotate", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "X-warp", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "Y-warp", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
			}
			for (uint k = 0; k < projection; k++)
			{
				if (!rb_ok (ctx, ptr, 20))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				// NOTE: this struct is 20 bytes total (4 floats + option byte +
				// unknown-1 byte + unknown-2 u16). The option/unknown fields sit
				// at fixed offsets +16/+17/+18 from the struct START -- they must
				// be read relative to a saved base pointer, not ptr, because ptr
				// is advanced by the four preceding BFNodeSetFloat calls before
				// they're read. A prior version read them at ptr+16/17/18 *after*
				// those advances (i.e. base+32/33/34, past the struct entirely)
				// and then added another 20 to ptr on top of the 16 already
				// consumed by the floats, double-counting 16 bytes per entry.
				// That corrupted every subsequent material name/offset -- fixed
				// by anchoring the tail fields to the struct's base offset and
				// consuming exactly 20 bytes total (16 for floats + 4 for tail).
				uint base = ptr;
				u8 opt = d[base + 16];
				if (opt >= 7)
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				char key[24];
				snprintf (key, sizeof (key), "projection-mapping-%u", k);
				bf_node_t *fn = BFNodeSetNode (mat, key);
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (BFNodeSetFloat (fn, "X-translate", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "Y-translate", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "X-scale", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetFloat (fn, "Y-scale", rdf32 (d + ptr, ctx->be)));
				ptr += 4;
				BFE (BFNodeSetStr (fn, "option", PROJECTION_MAPPING_TYPES[opt]));
				BFE (BFNodeSetInt (fn, "unknown-1", d[base + 17]));
				BFE (BFNodeSetInt (fn, "unknown-2", rd16 (d + base + 18, ctx->be)));
				ptr += 4;
			}
			if (shadow)
			{
				if (!rb_ok (ctx, ptr, 8))
				{
					FREE (offsets);
					return ERR_INVALID_DATA;
				}
				bf_node_t *fn = BFNodeSetNode (mat, "shadow-blending");
				if (!fn)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				bf_node_t *col = BFNodeSetNode (fn, "black-blending");
				if (!col)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (rb_color (ctx, d, size, ptr, col));
				col = BFNodeSetNode (fn, "white-blending");
				if (!col)
				{
					FREE (offsets);
					return ERR_OUT_OF_MEMORY;
				}
				BFE (rb_color (ctx, d, size, ptr + 3, col));
				ptr += 8;
			}
		}
		FREE (offsets);
		if (ptr < size)
		{
			char *extra = bf_hex_encode (d + ptr, size - ptr);
			if (!extra)
				return ERR_OUT_OF_MEMORY;
			BFE (BFNodeSetStr (node, "extra", extra));
			FREE (extra);
		}
		bf_list_clear (&ctx->materials);
		for (uint i = 0; i < materials->n; i++)
		{
			bf_val_t *v;
			BFE (bf_list_alloc (&ctx->materials, &v));
			v->type = BF_T_NODE;
			v->u.node = materials->items[i].u.node;
		}
		return ERR_OK;
	}

	for (uint i = 0; i < num; i++)
	{
		if (offsets[i] > size || !rb_ok (ctx, offsets[i], 20 + 4 + 24 + 4))
		{
			FREE (offsets);
			return ERR_INVALID_DATA;
		}
		ptr = offsets[i];
		bf_node_t *mat = BFListAddNode (materials);
		if (!mat)
		{
			FREE (offsets);
			return ERR_OUT_OF_MEMORY;
		}
		// Layout verified byte-for-byte against real 3DS CLYT materials
		// (name width, color count, and every flag-bit width/meaning below)
		// using Gericom/EveryFileExplorer's mat1.cs as a ground-truth reference
		// decoder: a 20-byte name (not the Wii RLYT's 34), one buffer color
		// plus six const colors (28 bytes total, not two 4-byte colors), and a
		// flags bitfield whose tevStage count is 3 bits wide (not 2, which is
		// why real files with tevStage>=4 previously desynced everything after
		// it) and whose color/alpha blend mode are single presence bits (not
		// 2-bit counts). Two independent real materials (52 and 168 bytes)
		// were hand-decoded against this layout and both consumed their
		// chunk's bytes exactly, with zero slack.
		char *name;
		BFE (rb_strn (ctx, d, size, ptr, 20, &name));
		BFE (BFNodeSetStr (mat, "name", name));
		FREE (name);
		ptr += 20;
		if (!rb_ok (ctx, ptr, 4 + 24))
		{
			FREE (offsets);
			return ERR_INVALID_DATA;
		}
		bf_node_t *color = BFNodeSetNode (mat, "buffer-color");
		if (!color)
		{
			FREE (offsets);
			return ERR_OUT_OF_MEMORY;
		}
		BFE (rb_color (ctx, d, size, ptr, color));
		ptr += 4;
		for (uint k = 0; k < 6; k++)
		{
			char key[24];
			snprintf (key, sizeof (key), "const-color-%u", k);
			color = BFNodeSetNode (mat, key);
			if (!color)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (rb_color (ctx, d, size, ptr, color));
			ptr += 4;
		}
		u32 flags = rd32 (d + ptr, ctx->be);
		ptr += 4;
		BFE (BFNodeSetInt (mat, "flags", (int)flags));

		uint texref = (flags >> 0) & 3;
		uint textureSRT = (flags >> 2) & 3;
		uint texCoordGen = (flags >> 4) & 3;
		uint tevStage = (flags >> 6) & 7;
		bool alphaCompare = (flags >> 9) & 1;
		bool colorBlend = (flags >> 10) & 1;
		bool alphaBlend = (flags >> 12) & 1;
		bool indirect = (flags >> 14) & 1;
		uint projection = (flags >> 15) & 3;
		bool shadow = (flags >> 17) & 1;

		for (uint k = 0; k < texref; k++)
		{
			if (!rb_ok (ctx, ptr, 4))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			uint tex = rd16 (d + ptr, ctx->be);
			u8 ws = d[ptr + 2], wt = d[ptr + 3];
			ptr += 4;
			if (tex >= ctx->texturenames.n || ws >= 8 || wt >= 8)
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			char key[24];
			snprintf (key, sizeof (key), "texref-%u", k);
			bf_node_t *fn = BFNodeSetNode (mat, key);
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetStr (fn, "file", ctx->texturenames.items[tex].u.s));
			BFE (BFNodeSetStr (fn, "wrap-S", WRAPS[ws]));
			BFE (BFNodeSetStr (fn, "wrap-T", WRAPS[wt]));
		}
		for (uint k = 0; k < textureSRT; k++)
		{
			if (!rb_ok (ctx, ptr, 20))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			char key[24];
			snprintf (key, sizeof (key), "textureSRT-%u", k);
			bf_node_t *fn = BFNodeSetNode (mat, key);
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetFloat (fn, "X-translate", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "Y-translate", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "rotate", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "X-scale", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "Y-scale", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
		}
		// TexCoordGen: byte unknown-1, byte source (0..5), u16 unknown-2 -- 4
		// bytes total, not the Wii mapping-settings' 8-byte struct.
		for (uint k = 0; k < texCoordGen; k++)
		{
			if (!rb_ok (ctx, ptr, 4))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			u8 src = d[ptr + 1];
			if (src >= 6)
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			char key[24];
			snprintf (key, sizeof (key), "texcoord-gen-%u", k);
			bf_node_t *fn = BFNodeSetNode (mat, key);
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetInt (fn, "unknown-1", d[ptr]));
			BFE (BFNodeSetStr (fn, "source", TEXCOORD_GEN_SOURCE[src]));
			BFE (BFNodeSetInt (fn, "unknown-2", rd16 (d + ptr + 2, ctx->be)));
			ptr += 4;
		}
		// TevStage (the 3DS texture-combiner stage): two u32 combiner
		// descriptions (color/alpha sources+operators+mode+scale+save-flag,
		// packed 4 bits at a time) plus a u32 of constant-color indices --
		// 12 bytes total, not the Wii texture-combiner's 4-byte struct. Not
		// semantically decoded field-by-field (would need its own dedicated
		// verification pass); stored as three raw u32s so real files parse
		// and round-trip instead of being rejected.
		for (uint k = 0; k < tevStage; k++)
		{
			if (!rb_ok (ctx, ptr, 12))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			char key[24];
			snprintf (key, sizeof (key), "tev-stage-%u", k);
			bf_node_t *fn = BFNodeSetNode (mat, key);
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetInt (fn, "color-combiner", (int)rd32 (d + ptr, ctx->be)));
			BFE (BFNodeSetInt (fn, "alpha-combiner", (int)rd32 (d + ptr + 4, ctx->be)));
			BFE (BFNodeSetInt (fn, "const-colors", (int)rd32 (d + ptr + 8, ctx->be)));
			ptr += 12;
		}
		if (alphaCompare)
		{
			if (!rb_ok (ctx, ptr, 8))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			u32 cond = rd32 (d + ptr, ctx->be);
			if (cond >= 8)
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			bf_node_t *fn = BFNodeSetNode (mat, "alpha-compare");
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetStr (fn, "condition", ALPHA_COMPARE_CONDITIONS[cond]));
			BFE (BFNodeSetFloat (fn, "value", rdf32 (d + ptr + 4, ctx->be)));
			ptr += 8;
		}
		// Color/alpha blend mode are single presence bits in the 3DS format
		// (not the Wii's 2-bit counts) -- each, if present, is one 4-byte
		// BlendMode struct (operator/source/destination/logic-op bytes).
		if (colorBlend)
		{
			if (!rb_ok (ctx, ptr, 4))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			if (d[ptr] >= 6 || d[ptr + 1] >= 10 || d[ptr + 2] >= 10 || d[ptr + 3] >= 17)
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			bf_node_t *fn = BFNodeSetNode (mat, "color-blend-mode");
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetStr (fn, "blend-operation", BLEND_CALC_OPS[d[ptr]]));
			BFE (BFNodeSetStr (fn, "source", BLEND_CALC[d[ptr + 1]]));
			BFE (BFNodeSetStr (fn, "destination", BLEND_CALC[d[ptr + 2]]));
			BFE (BFNodeSetStr (fn, "logical-operation", LOGICAL_CALC_OPS[d[ptr + 3]]));
			ptr += 4;
		}
		if (alphaBlend)
		{
			if (!rb_ok (ctx, ptr, 4))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			if (d[ptr] >= 6 || d[ptr + 1] >= 10 || d[ptr + 2] >= 10)
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			bf_node_t *fn = BFNodeSetNode (mat, "alpha-blend-mode");
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetStr (fn, "blend-operation", BLEND_CALC_OPS[d[ptr]]));
			BFE (BFNodeSetStr (fn, "source", BLEND_CALC[d[ptr + 1]]));
			BFE (BFNodeSetStr (fn, "destination", BLEND_CALC[d[ptr + 2]]));
			BFE (BFNodeSetInt (fn, "unknown", d[ptr + 3]));
			ptr += 4;
		}
		if (indirect)
		{
			if (!rb_ok (ctx, ptr, 12))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			bf_node_t *fn = BFNodeSetNode (mat, "indirect-adjustment");
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetFloat (fn, "rotate", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "X-warp", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "Y-warp", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
		}
		for (uint k = 0; k < projection; k++)
		{
			if (!rb_ok (ctx, ptr, 20))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			u8 opt = d[ptr + 16];
			if (opt >= 7)
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			char key[24];
			snprintf (key, sizeof (key), "projection-mapping-%u", k);
			bf_node_t *fn = BFNodeSetNode (mat, key);
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (BFNodeSetFloat (fn, "X-translate", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "Y-translate", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "X-scale", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetFloat (fn, "Y-scale", rdf32 (d + ptr, ctx->be)));
			ptr += 4;
			BFE (BFNodeSetStr (fn, "option", PROJECTION_MAPPING_TYPES[opt]));
			BFE (BFNodeSetInt (fn, "unknown-1", d[ptr + 17]));
			BFE (BFNodeSetInt (fn, "unknown-2", rd16 (d + ptr + 18, ctx->be)));
			ptr += 20;
		}
		if (shadow)
		{
			if (!rb_ok (ctx, ptr, 8))
			{
				FREE (offsets);
				return ERR_INVALID_DATA;
			}
			bf_node_t *fn = BFNodeSetNode (mat, "shadow-blending");
			if (!fn)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			bf_node_t *col = BFNodeSetNode (fn, "black-blending");
			if (!col)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (rb_color (ctx, d, size, ptr, col));
			col = BFNodeSetNode (fn, "white-blending");
			if (!col)
			{
				FREE (offsets);
				return ERR_OUT_OF_MEMORY;
			}
			BFE (rb_color (ctx, d, size, ptr + 3, col));
			ptr += 8;
		}
	}
	FREE (offsets);
	if (ptr < size)
	{
		char *extra = bf_hex_encode (d + ptr, size - ptr);
		if (!extra)
			return ERR_OUT_OF_MEMORY;
		BFE (BFNodeSetStr (node, "extra", extra));
		FREE (extra);
	}
	// store the material names cache
	// NOTE: the cache holds POINTERS to the material nodes that are owned by
	// the tree. It must be cleared shallowly, never deep-freed.
	bf_list_clear (&ctx->materials);
	for (uint i = 0; i < materials->n; i++)
	{
		bf_val_t *v;
		BFE (bf_list_alloc (&ctx->materials, &v));
		// reuse the material nodes (shared)
		v->type = BF_T_NODE;
		v->u.node = materials->items[i].u.node;
	}
	return ERR_OK;
}

static enumError r_pan1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "pan1-%s", ctx->prevname);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	return readpane (ctx, d, size, &p, node);
}

static enumError r_pas1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "pas1-%s", ctx->prevname);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	rctx_push (ctx, node);
	return ERR_OK;
}

static enumError r_pae1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	rctx_pop (ctx);
	char key[64];
	snprintf (key, sizeof (key), "pae1-%s", ctx->prevname);
	char val[80];
	snprintf (val, sizeof (val), "End of %s", ctx->prevname);
	return BFNodeSetStr (ctx->actnode, key, val);
}

static enumError r_wnd1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "wnd1-%s", ctx->prevname);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	BFE (readpane (ctx, d, size, &p, node));
	if (!rb_ok (ctx, p, 40))
		return ERR_INVALID_DATA;
	BFE (BFNodeSetInt (node, "stretch-left", rd16 (d + p, ctx->be)));
	p += 2;
	BFE (BFNodeSetInt (node, "stretch-right", rd16 (d + p, ctx->be)));
	p += 2;
	BFE (BFNodeSetInt (node, "stretch-up", rd16 (d + p, ctx->be)));
	p += 2;
	BFE (BFNodeSetInt (node, "stretch-down", rd16 (d + p, ctx->be)));
	p += 2;
	BFE (BFNodeSetInt (node, "custom-left", rd16 (d + p, ctx->be)));
	p += 2;
	BFE (BFNodeSetInt (node, "custom-right", rd16 (d + p, ctx->be)));
	p += 2;
	BFE (BFNodeSetInt (node, "custom-up", rd16 (d + p, ctx->be)));
	p += 2;
	BFE (BFNodeSetInt (node, "custom-down", rd16 (d + p, ctx->be)));
	p += 2;
	uint framenum = d[p];
	BFE (BFNodeSetInt (node, "frame-count", (int)framenum));
	BFE (BFNodeSetInt (node, "flags", d[p + 1]));
	p += 4; // pad
	p += 8; // offset1, offset2
	bf_node_t *col;
	col = BFNodeSetNode (node, "color-1");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	col = BFNodeSetNode (node, "color-2");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	col = BFNodeSetNode (node, "color-3");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	col = BFNodeSetNode (node, "color-4");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	if (!rb_ok (ctx, p, 4))
		return ERR_INVALID_DATA;
	uint matnum = rd16 (d + p, ctx->be);
	p += 2;
	uint coordsnum = d[p];
	BFE (BFNodeSetInt (node, "coordinates-count", (int)coordsnum));
	p += 2;
	if (matnum >= ctx->materials.n)
		return ERR_INVALID_DATA;
	ccp mname = bf_get_str (ctx->materials.items[matnum].u.node, "name");
	if (!mname)
		return ERR_INVALID_DATA;
	BFE (BFNodeSetStr (node, "material", mname));
	for (uint i = 0; i < coordsnum; i++)
	{
		if (!rb_ok (ctx, p, 32))
			return ERR_INVALID_DATA;
		char ckey[24];
		snprintf (ckey, sizeof (ckey), "coords-%u", i);
		bf_node_t *cn = BFNodeSetNode (node, ckey);
		if (!cn)
			return ERR_OUT_OF_MEMORY;
		for (uint j = 0; j < 8; j++)
		{
			char tkey[24];
			snprintf (tkey, sizeof (tkey), "texcoord-%u", j);
			BFE (BFNodeSetFloat (cn, tkey, rdf32 (d + p, ctx->be)));
			p += 4;
		}
	}
	p += framenum * 4; // wnd4 offsets
	bf_list_t *wnd4 = BFNodeSetList (node, "wnd4-materials");
	if (!wnd4)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < framenum; i++)
	{
		if (!rb_ok (ctx, p, 4))
			return ERR_INVALID_DATA;
		uint m = rd16 (d + p, ctx->be);
		if (m >= ctx->materials.n)
			return ERR_INVALID_DATA;
		ccp mn = bf_get_str (ctx->materials.items[m].u.node, "name");
		if (!mn)
			return ERR_INVALID_DATA;
		bf_node_t *wn = BFListAddNode (wnd4);
		if (!wn)
			return ERR_OUT_OF_MEMORY;
		BFE (BFNodeSetStr (wn, "material", mn));
		BFE (BFNodeSetInt (wn, "index", d[p + 2]));
		p += 4;
	}
	return ERR_OK;
}

static enumError r_txt1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "txt1-%s", ctx->prevname);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	BFE (readpane (ctx, d, size, &p, node));
	// txt1 diverges by platform just like pan1/lyt1/grp1/mat1 -- found the
	// hard way: the Wii U-only rewrite below (offset-pointer indirection for
	// text/call-name, length-before-restrict-length field order) was
	// originally applied unconditionally, on the assumption txt1 was shared.
	// It broke 209/1980 real 3DS BCLYT files (all of them failing inside
	// this function, confirmed by tracing every chunk dispatch) that a
	// pre-existing, already-100%-verified 3DS reader (git history: 0c5a8a7)
	// had handled correctly with a *different* struct: restrict-length
	// before length, and the text is read inline right after the fixed
	// header using restrict-length as its byte count -- no offset pointer
	// at all. Now platform-gated like everywhere else; the 3DS branch below
	// is that original, still-100%-verified logic, untouched.
	if (ctx->is_wiiu)
	{
		// Wii U's txt1: confirmed against a real file (Cmn_SeqDRCOption_00.
		// bflyt): the byte range right after the fixed header (where the 3DS
		// reader assumes the text lives) is actually the top/bottom-color and
		// font-size fields that come *after* text-offset in the real struct;
		// the actual UTF-16 text lives wherever `text-offset` (an absolute
		// offset from the section start, like call-name already was meant to
		// be) points, which is not necessarily contiguous with the header at
		// all. Verified on two real entries: `text-offset` led to real UTF-16
		// text (once a literal "-5"-style placeholder value, once real
		// Japanese UI text), and `call-name-offset` led to a real ASCII name
		// ("L_CameraLevel_00-T_Header_00"). Cross-checked against
		// Tyulis/3DSkit's BFLYT.md, which also documents a trailing
		// "per character transform structure offset" field this reader didn't
		// read at all; now captured (as a raw offset, not decoded -- no real
		// sample with it non-zero was found to verify the pointed-to
		// structure's contents).
		if (!rb_ok (ctx, p, 80))
			return ERR_INVALID_DATA;
		uint length = rd16 (d + p, ctx->be);
		BFE (BFNodeSetInt (node, "length", (int)length));
		p += 2;
		uint restrict_len = rd16 (d + p, ctx->be);
		BFE (BFNodeSetInt (node, "restrict-length", (int)restrict_len));
		p += 2;
		uint matnum = rd16 (d + p, ctx->be);
		p += 2;
		uint fontnum = rd16 (d + p, ctx->be);
		p += 2;
		// 0xFFFF is a real "no override" sentinel here (found on a real file:
		// fontnum=0xFFFF with only 3 real fontnames registered) -- the same
		// sentinel convention seen elsewhere in this format today (BFLIM
		// pic1's MaterialId). A real, in-range index is still required when
		// it isn't the sentinel; only the sentinel itself is now accepted.
		if (matnum != 0xFFFF && matnum >= ctx->materials.n
			|| fontnum != 0xFFFF && fontnum >= ctx->fontnames.n)
			return ERR_INVALID_DATA;
		if (matnum != 0xFFFF)
		{
			ccp mname = bf_get_str (ctx->materials.items[matnum].u.node, "name");
			if (!mname)
				return ERR_INVALID_DATA;
			BFE (BFNodeSetStr (node, "material", mname));
		}
		if (fontnum != 0xFFFF)
			BFE (BFNodeSetStr (node, "font", ctx->fontnames.items[fontnum].u.s));
		u8 align = d[p];
		bf_node_t *an = BFNodeSetNode (node, "alignment");
		if (!an)
			return ERR_OUT_OF_MEMORY;
		BFE (BFNodeSetStr (an, "x", ORIG_X[align % 4]));
		BFE (BFNodeSetStr (an, "y", ORIG_Y[align / 4]));
		p += 1;
		u8 la = d[p];
		if (la >= 4)
			return ERR_INVALID_DATA;
		BFE (BFNodeSetStr (node, "line-alignment", TEXT_ALIGNS[la]));
		p += 1;
		BFE (BFNodeSetInt (node, "active-shadows", d[p]));
		p += 1;
		BFE (BFNodeSetInt (node, "unknown-1", d[p]));
		p += 1;
		BFE (BFNodeSetFloat (node, "italic-tilt", rdf32 (d + p, ctx->be)));
		p += 4;
		u32 text_off = rd32 (d + p, ctx->be);
		p += 4;
		bf_node_t *col;
		col = BFNodeSetNode (node, "top-color");
		if (!col)
			return ERR_OUT_OF_MEMORY;
		BFE (rb_color (ctx, d, size, p, col));
		p += 4;
		col = BFNodeSetNode (node, "bottom-color");
		if (!col)
			return ERR_OUT_OF_MEMORY;
		BFE (rb_color (ctx, d, size, p, col));
		p += 4;
		BFE (BFNodeSetFloat (node, "font-size-x", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (node, "font-size-y", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (node, "char-space", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (node, "line-space", rdf32 (d + p, ctx->be)));
		p += 4;
		u32 callname_off = rd32 (d + p, ctx->be);
		p += 4;
		bf_node_t *sh = BFNodeSetNode (node, "shadow");
		if (!sh)
			return ERR_OUT_OF_MEMORY;
		BFE (BFNodeSetFloat (sh, "offset-X", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (sh, "offset-Y", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (sh, "scale-X", rdf32 (d + p, ctx->be)));
		p += 4;
		BFE (BFNodeSetFloat (sh, "scale-Y", rdf32 (d + p, ctx->be)));
		p += 4;
		col = BFNodeSetNode (sh, "top-color");
		if (!col)
			return ERR_OUT_OF_MEMORY;
		BFE (rb_color (ctx, d, size, p, col));
		p += 4;
		col = BFNodeSetNode (sh, "bottom-color");
		if (!col)
			return ERR_OUT_OF_MEMORY;
		BFE (rb_color (ctx, d, size, p, col));
		p += 4;
		BFE (BFNodeSetFloat (node, "shadow-italic-tilt", rdf32 (d + p, ctx->be)));
		p += 4;
		u32 perchar_off = rd32 (d + p, ctx->be);
		p += 4;
		if (perchar_off)
			BFE (BFNodeSetInt (node, "per-char-transform-offset", (int)perchar_off));

		if (text_off && length)
		{
			if (text_off > size)
				return ERR_INVALID_DATA;
			uint n = 0;
			while (text_off + n + 1 < size && (d[text_off + n] || d[text_off + n + 1]))
				n += 2;
			char *text = utf16_decode (d + text_off, n, ctx->be);
			if (!text)
				return ERR_OUT_OF_MEMORY;
			BFE (BFNodeSetStr (node, "text", text));
			FREE (text);
		}
		if (callname_off)
		{
			char *callname;
			BFE (rb_str (ctx, d, size, callname_off, &callname));
			BFE (BFNodeSetStr (node, "call-name", callname));
			FREE (callname);
		}
		return ERR_OK;
	}

	// 3DS: original, already-100%-verified logic (git history: 0c5a8a7).
	if (!rb_ok (ctx, p, 76))
		return ERR_INVALID_DATA;
	uint restrict_len = rd16 (d + p, ctx->be);
	BFE (BFNodeSetInt (node, "restrict-length", (int)restrict_len));
	p += 2;
	BFE (BFNodeSetInt (node, "length", rd16 (d + p, ctx->be)));
	p += 2;
	uint matnum = rd16 (d + p, ctx->be);
	p += 2;
	uint fontnum = rd16 (d + p, ctx->be);
	p += 2;
	if (matnum >= ctx->materials.n || fontnum >= ctx->fontnames.n)
		return ERR_INVALID_DATA;
	ccp mname = bf_get_str (ctx->materials.items[matnum].u.node, "name");
	if (!mname)
		return ERR_INVALID_DATA;
	BFE (BFNodeSetStr (node, "material", mname));
	BFE (BFNodeSetStr (node, "font", ctx->fontnames.items[fontnum].u.s));
	u8 align = d[p];
	bf_node_t *an = BFNodeSetNode (node, "alignment");
	if (!an)
		return ERR_OUT_OF_MEMORY;
	BFE (BFNodeSetStr (an, "x", ORIG_X[align % 4]));
	BFE (BFNodeSetStr (an, "y", ORIG_Y[align / 4]));
	p += 1;
	u8 la = d[p];
	if (la >= 4)
		return ERR_INVALID_DATA;
	BFE (BFNodeSetStr (node, "line-alignment", TEXT_ALIGNS[la]));
	p += 1;
	BFE (BFNodeSetInt (node, "active-shadows", d[p]));
	p += 1;
	BFE (BFNodeSetInt (node, "unknown-1", d[p]));
	p += 1;
	BFE (BFNodeSetFloat (node, "italic-tilt", rdf32 (d + p, ctx->be)));
	p += 4;
	p += 4; // start offset
	bf_node_t *col;
	col = BFNodeSetNode (node, "top-color");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	col = BFNodeSetNode (node, "bottom-color");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	BFE (BFNodeSetFloat (node, "font-size-x", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "font-size-y", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "char-space", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "line-space", rdf32 (d + p, ctx->be)));
	p += 4;
	p += 4; // call-name offset
	bf_node_t *sh = BFNodeSetNode (node, "shadow");
	if (!sh)
		return ERR_OUT_OF_MEMORY;
	BFE (BFNodeSetFloat (sh, "offset-X", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (sh, "offset-Y", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (sh, "scale-X", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (sh, "scale-Y", rdf32 (d + p, ctx->be)));
	p += 4;
	col = BFNodeSetNode (sh, "top-color");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	col = BFNodeSetNode (sh, "bottom-color");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	BFE (BFNodeSetInt (node, "shadow-unknown-2", (int)rd32 (d + p, ctx->be)));
	p += 4;

	if (!rb_ok (ctx, p, restrict_len))
		return ERR_INVALID_DATA;
	char *text = utf16_decode (d + p, restrict_len, ctx->be);
	if (!text)
		return ERR_OUT_OF_MEMORY;
	BFE (BFNodeSetStr (node, "text", text));
	FREE (text);
	p += restrict_len;
	p = (p + 3) & ~3u;
	if (p > size)
		p = size;
	char *callname;
	BFE (rb_str (ctx, d, size, p, &callname));
	BFE (BFNodeSetStr (node, "call-name", callname));
	FREE (callname);
	return ERR_OK;
}

static enumError r_bnd1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "bnd1-%s", ctx->prevname);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	return readpane (ctx, d, size, &p, node);
}

static enumError r_pic1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "pic1-%s", ctx->prevname);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	BFE (readpane (ctx, d, size, &p, node));
	if (!rb_ok (ctx, p, 4 * 4 + 4))
		return ERR_INVALID_DATA;
	bf_node_t *col;
	col = BFNodeSetNode (node, "top-left-vtx-color");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	col = BFNodeSetNode (node, "top-right-vtx-color");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	col = BFNodeSetNode (node, "bottom-left-vtx-color");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	col = BFNodeSetNode (node, "bottom-right-vtx-color");
	if (!col)
		return ERR_OUT_OF_MEMORY;
	BFE (rb_color (ctx, d, size, p, col));
	p += 4;
	if (!rb_ok (ctx, p, 4))
		return ERR_INVALID_DATA;
	uint matnum = rd16 (d + p, ctx->be);
	uint coordsnum = d[p + 2];
	p += 4;
	if (matnum >= ctx->materials.n)
		return ERR_INVALID_DATA;
	ccp mname = bf_get_str (ctx->materials.items[matnum].u.node, "name");
	if (!mname)
		return ERR_INVALID_DATA;
	BFE (BFNodeSetStr (node, "material", mname));
	BFE (BFNodeSetInt (node, "tex-coords-number", (int)coordsnum));
	bf_list_t *coords = BFNodeSetList (node, "tex-coords");
	if (!coords)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < coordsnum; i++)
	{
		if (!rb_ok (ctx, p, 32))
			return ERR_INVALID_DATA;
		bf_node_t *en = BFListAddNode (coords);
		if (!en)
			return ERR_OUT_OF_MEMORY;
		static const char *const corners[4]
			= { "top-left", "top-right", "bottom-left", "bottom-right" };
		for (uint c = 0; c < 4; c++)
		{
			bf_node_t *cn = BFNodeSetNode (en, corners[c]);
			if (!cn)
				return ERR_OUT_OF_MEMORY;
			BFE (BFNodeSetFloat (cn, "s", rdf32 (d + p, ctx->be)));
			BFE (BFNodeSetFloat (cn, "t", rdf32 (d + p + 4, ctx->be)));
			p += 8;
		}
	}
	return ERR_OK;
}

static enumError r_usd1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "usd1-%s", ctx->prevname);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	if (!rb_ok (ctx, p, 4))
		return ERR_INVALID_DATA;
	uint entrynum = rd16 (d + p, ctx->be);
	BFE (BFNodeSetInt (node, "entry-number", (int)entrynum));
	BFE (BFNodeSetInt (node, "unknown", rd16 (d + p + 2, ctx->be)));
	p += 4;
	if (!rb_ok (ctx, p, entrynum * 12))
		return ERR_INVALID_DATA;
	bf_list_t *entries = BFNodeSetList (node, "entries");
	if (!entries)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < entrynum; i++)
	{
		uint entryoffset = p;
		u32 nameoff = rd32 (d + p, ctx->be) + entryoffset;
		u32 dataoff = rd32 (d + p + 4, ctx->be) + entryoffset;
		uint datanum = rd16 (d + p + 8, ctx->be);
		uint datatype = d[p + 10];
		p += 12;
		if (nameoff > size || dataoff > size)
			return ERR_INVALID_DATA;
		bf_node_t *en = BFListAddNode (entries);
		if (!en)
			return ERR_OUT_OF_MEMORY;
		char *name;
		BFE (rb_str (ctx, d, size, nameoff, &name));
		BFE (BFNodeSetStr (en, "name", name));
		FREE (name);
		if (datatype == 0)
		{
			if (datanum > size - dataoff)
				return ERR_INVALID_DATA;
			char *s = bf_strndup ((ccp)d + dataoff, datanum);
			if (!s)
				return ERR_OUT_OF_MEMORY;
			BFE (BFNodeSetStr (en, "data", s));
			FREE (s);
		}
		else if (datatype == 1)
		{
			if (datanum > (size - dataoff) / 4)
				return ERR_INVALID_DATA;
			bf_list_t *dl = BFNodeSetList (en, "data");
			if (!dl)
				return ERR_OUT_OF_MEMORY;
			for (uint j = 0; j < datanum; j++)
				BFE (BFListAddInt (dl, rds32 (d + dataoff + j * 4, ctx->be)));
		}
		else if (datatype == 2)
		{
			if (datanum > (size - dataoff) / 4)
				return ERR_INVALID_DATA;
			bf_list_t *dl = BFNodeSetList (en, "data");
			if (!dl)
				return ERR_OUT_OF_MEMORY;
			for (uint j = 0; j < datanum; j++)
				BFE (BFListAddFloat (dl, rdf32 (d + dataoff + j * 4, ctx->be)));
		}
		else
			return ERR_INVALID_DATA;
		BFE (BFNodeSetInt (en, "unknown", d[p - 1]));
	}
	return ERR_OK;
}

// dispatch a section (chunk includes the 8 byte header)
static enumError r_section (bf_rctx_t *ctx, u32 magic, const u8 *d, uint size);
static enumError r_prt1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "prt1-%s", ctx->prevname);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	BFE (readpane (ctx, d, size, &p, node));
	if (!rb_ok (ctx, p, 12))
		return ERR_INVALID_DATA;
	uint count = rd32 (d + p, ctx->be);
	BFE (BFNodeSetInt (node, "section-count", (int)count));
	p += 4;
	BFE (BFNodeSetFloat (node, "section-scale-X", rdf32 (d + p, ctx->be)));
	p += 4;
	BFE (BFNodeSetFloat (node, "section-scale-Y", rdf32 (d + p, ctx->be)));
	p += 4;
	if (!rb_ok (ctx, p, count * 40))
		return ERR_INVALID_DATA;
	bf_list_t *entries = BFNodeSetList (node, "entries");
	if (!entries)
		return ERR_OUT_OF_MEMORY;
	uint max_entry = 0, max_extra = 0;
	bool have_entry = false, have_extra = false;
	for (uint i = 0; i < count; i++)
	{
		bf_node_t *en = BFListAddNode (entries);
		if (!en)
			return ERR_OUT_OF_MEMORY;
		rctx_push (ctx, en);
		char *name;
		BFE (rb_strn (ctx, d, size, p, 24, &name));
		BFE (BFNodeSetStr (en, "name", name));
		FREE (name);
		BFE (BFNodeSetInt (en, "unknown-1", d[p + 24]));
		BFE (BFNodeSetInt (en, "flags", d[p + 25]));
		// Tyulis/3DSkit's BFLYT.md documents 3 offset fields here (sub-pane,
		// complementary/usd1, extra-data), not 2 -- confirmed against real
		// files: a real entry was found with all three simultaneously
		// non-zero and distinct (off1=244 off2=432 off3=472), and many real
		// entries have off2==0 while off3 carries real, non-zero data that
		// this reader was previously never reaching at all (it read "extra"
		// from the complementary/usd1 field's position instead of the real
		// extra-data field 4 bytes later).
		u32 entryoffset = rd32 (d + p + 28, ctx->be);
		u32 usd1offset = rd32 (d + p + 32, ctx->be);
		u32 extraoffset = rd32 (d + p + 36, ctx->be);
		p += 40;
		if (entryoffset != 0)
		{
			have_entry = true;
			if (entryoffset > size || entryoffset + 8 > size)
				return ERR_INVALID_DATA;
			uint length = rd32 (d + entryoffset + 4, ctx->be);
			if (length < 8 || entryoffset + length > size)
				return ERR_INVALID_DATA;
			u32 smagic = ((u32)d[entryoffset] << 24) | ((u32)d[entryoffset + 1] << 16)
				| ((u32)d[entryoffset + 2] << 8) | (u32)d[entryoffset + 3];
			enumError err = r_section (ctx, smagic, d + entryoffset, length);
			if (err)
				return err;
			if (entryoffset + length > max_entry)
				max_entry = entryoffset + length;
		}
		if (usd1offset != 0)
		{
			have_entry = true;
			if (usd1offset > size || usd1offset + 8 > size)
				return ERR_INVALID_DATA;
			uint length = rd32 (d + usd1offset + 4, ctx->be);
			if (length < 8 || usd1offset + length > size)
				return ERR_INVALID_DATA;
			u32 smagic = ((u32)d[usd1offset] << 24) | ((u32)d[usd1offset + 1] << 16)
				| ((u32)d[usd1offset + 2] << 8) | (u32)d[usd1offset + 3];
			enumError err = r_section (ctx, smagic, d + usd1offset, length);
			if (err)
				return err;
			if (usd1offset + length > max_entry)
				max_entry = usd1offset + length;
		}
		if (extraoffset != 0)
		{
			have_extra = true;
			if (extraoffset > size || extraoffset + 48 > size)
				return ERR_INVALID_DATA;
			char *extra = bf_hex_encode (d + extraoffset, 48);
			if (!extra)
				return ERR_OUT_OF_MEMORY;
			BFE (BFNodeSetStr (en, "extra", extra));
			FREE (extra);
			if (extraoffset + 48 > max_extra)
				max_extra = extraoffset + 48;
		}
		rctx_pop (ctx);
	}
	// The true end of consumed data is whichever of the entries table
	// itself / linked sub-pane-or-usd1 data / extra data reaches furthest
	// -- previously this only ever looked at max_extra when any extra
	// data existed at all, silently mislabeling real trailing sub-pane
	// data as an opaque "dump" whenever a later entry's linked section
	// ended after the last extra-data block. Not otherwise touched by the
	// 3-offset-field fix above, but that fix makes have_entry true more
	// often (via the usd1 offset), so this was worth fixing at the same
	// time rather than leaving a latent bug more exposed.
	uint end = p;
	if (have_entry && max_entry > end)
		end = max_entry;
	if (have_extra && max_extra > end)
		end = max_extra;
	if (end < size)
	{
		BFE (BFNodeSetBytes (node, "dump", d + end, size - end));
	}
	return ERR_OK;
}

static enumError r_grp1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	// Same is_wiiu platform split as readpane()/r_lyt1(): Wii U's grp1 has
	// a 24-byte name + u16 count (+2 padding) + 24-byte child-name entries
	// (kinnay/Nintendo-File-Formats' bflyt.md "Group"), not the 16-byte
	// name/u32 count/16-byte entries this code was verified against on 3DS
	// real files.
	uint namelen = ctx->is_wiiu ? 24 : 16;
	uint countoff = ctx->is_wiiu ? 0x20 : 24;
	uint firstoff = ctx->is_wiiu ? 0x24 : 28;

	if (!rb_ok (ctx, 8 + namelen, 4))
		return ERR_INVALID_DATA;
	char *name;
	BFE (rb_strn (ctx, d, size, 8, namelen, &name));
	char key[64];
	snprintf (key, sizeof (key), "grp1-%s", name);
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
	{
		FREE (name);
		return ERR_OUT_OF_MEMORY;
	}
	BFE (BFNodeSetStr (node, "name", name));
	FREE (name);
	uint subnum = ctx->is_wiiu ? rd16 (d + countoff, ctx->be) : rd32 (d + countoff, ctx->be);
	BFE (BFNodeSetInt (node, "subs-number", (int)subnum));
	bf_list_t *subs = BFNodeSetList (node, "subs");
	if (!subs)
		return ERR_OUT_OF_MEMORY;
	uint p = firstoff;
	if (!rb_ok (ctx, p, subnum * namelen))
		return ERR_INVALID_DATA;
	for (uint i = 0; i < subnum; i++)
	{
		char *s;
		BFE (rb_strn (ctx, d, size, p, namelen, &s));
		BFE (BFListAddStr (subs, s));
		FREE (s);
		p += namelen;
	}
	if (p < size)
		BFE (BFNodeSetBytes (node, "dump", d + p, size - p));
	return ERR_OK;
}

static enumError r_grs1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	char key[64];
	snprintf (key, sizeof (key), "grs1-%d", ctx->grsnum);
	ctx->grsnum++;
	bf_node_t *node = BFNodeSetNode (ctx->actnode, key);
	if (!node)
		return ERR_OUT_OF_MEMORY;
	rctx_push (ctx, node);
	return ERR_OK;
}

static enumError r_gre1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	rctx_pop (ctx);
	int num = ctx->grsnum - 1;
	char key[64], val[80];
	snprintf (key, sizeof (key), "gre1-%d", num);
	snprintf (val, sizeof (val), "End of grs1-%d", num);
	return BFNodeSetStr (ctx->actnode, key, val);
}

static enumError r_cnt1 (bf_rctx_t *ctx, const u8 *d, uint size)
{
	bf_node_t *node = BFNodeSetNode (ctx->actnode, "cnt1");
	if (!node)
		return ERR_OUT_OF_MEMORY;
	uint p = 8;
	if (!rb_ok (ctx, p, 24))
		return ERR_INVALID_DATA;
	// header layout (verified against real Splatoon Wii U samples):
	//   p+0  offset of the 2nd (duplicate) name copy
	//   p+4  offset2:      start of the part-name array
	//   p+8  partnum (u16)
	//   p+10 animnum (u16)
	//   p+12 offset_anim:  start of the anim-part block (count + name + table)
	//   p+16 offset_table: start of the animnum-entry offset table; each
	//                      entry is relative to offset_table itself
	//   p+20 name (NUL padded to a multiple of 4, stored twice back-to-back)
	u32 offset2 = rd32 (d + p + 4, ctx->be);
	uint partnum = rd16 (d + p + 8, ctx->be);
	uint animnum = rd16 (d + p + 10, ctx->be);
	u32 offset_anim = rd32 (d + p + 12, ctx->be);
	u32 offset_table = rd32 (d + p + 16, ctx->be);
	BFE (BFNodeSetInt (node, "part-number", (int)partnum));
	BFE (BFNodeSetInt (node, "anim-number", (int)animnum));
	char *name;
	BFE (rb_str (ctx, d, size, p + 20, &name));
	BFE (BFNodeSetStr (node, "name", name));
	uint namelen = (strlen (name) + 1 + 3) & ~3u;
	p = p + 20 + namelen * 2;
	FREE (name);
	if (partnum != 0)
	{
		p = offset2;
		if (!rb_ok (ctx, p, partnum * 24))
			return ERR_INVALID_DATA;
		bf_list_t *parts = BFNodeSetList (node, "parts");
		if (!parts)
			return ERR_OUT_OF_MEMORY;
		for (uint i = 0; i < partnum; i++)
		{
			char *s;
			BFE (rb_strn (ctx, d, size, p, 24, &s));
			BFE (BFListAddStr (parts, s));
			FREE (s);
			p += 24;
		}
	}
	uint dump_start = p;
	if (animnum != 0)
	{
		if (!rb_ok (ctx, offset_anim, 8))
			return ERR_INVALID_DATA;
		uint animpartnum = rd32 (d + offset_anim, ctx->be);
		char *animname;
		// the name field is preceded by a 4-byte length word; the string
		// itself follows immediately after (offset_anim+4 is that length
		// word, so the name starts at offset_anim+8)
		BFE (rb_str (ctx, d, size, offset_anim + 8, &animname));
		bf_node_t *an = BFNodeSetNode (node, "anim-part");
		if (!an)
			return ERR_OUT_OF_MEMORY;
		BFE (BFNodeSetInt (an, "anim-part-number", (int)animpartnum));
		BFE (BFNodeSetStr (an, "name", animname));
		FREE (animname);
		bf_list_t *anims = BFNodeSetList (an, "anims");
		if (!anims)
			return ERR_OUT_OF_MEMORY;
		if (!rb_ok (ctx, offset_table, animnum * 4))
			return ERR_INVALID_DATA;
		for (uint i = 0; i < animnum; i++)
		{
			u32 off = rd32 (d + offset_table + i * 4, ctx->be);
			char *s;
			BFE (rb_str (ctx, d, size, offset_table + off, &s));
			BFE (BFListAddStr (anims, s));
			FREE (s);
		}
		// the anim block's exact trailing layout beyond the offset table
		// isn't fully modeled; skip the tail dump rather than mis-slice it
		dump_start = size;
	}
	if (dump_start < size)
		BFE (BFNodeSetBytes (node, "dump", d + dump_start, size - dump_start));
	return ERR_OK;
}

// dispatch a section (chunk includes the 8 byte header)
static enumError r_section (bf_rctx_t *ctx, u32 magic, const u8 *d, uint size)
{
	if (ctx->is_rlyt)
	{
		char key[8];
		snprintf (key, sizeof (key), "%c%c%c%c", d[0], d[1], d[2], d[3]);
		bf_node_t *sn = BFNodeSetNode (ctx->actnode, key);
		if (!sn)
			return ERR_OUT_OF_MEMORY;
		if (size > 8)
			BFE (BFNodeSetBytes (sn, "data", d + 8, size - 8));
		return ERR_OK;
	}
	switch (magic)
	{
		case BFLYT_CHUNK_lyt1:
			return r_lyt1 (ctx, d, size);
		case BFLYT_CHUNK_txl1:
			return r_txl1 (ctx, d, size);
		case BFLYT_CHUNK_fnl1:
			return r_fnl1 (ctx, d, size);
		case BFLYT_CHUNK_mat1:
			return r_mat1 (ctx, d, size);
		case BFLYT_CHUNK_pan1:
			return r_pan1 (ctx, d, size);
		case BFLYT_CHUNK_pic1:
			return r_pic1 (ctx, d, size);
		case BFLYT_CHUNK_txt1:
			return r_txt1 (ctx, d, size);
		case BFLYT_CHUNK_wnd1:
			return r_wnd1 (ctx, d, size);
		case BFLYT_CHUNK_bnd1:
			return r_bnd1 (ctx, d, size);
		case BFLYT_CHUNK_grp1:
			return r_grp1 (ctx, d, size);
		case BFLYT_CHUNK_grs1:
			return r_grs1 (ctx, d, size);
		case BFLYT_CHUNK_gre1:
			return r_gre1 (ctx, d, size);
		case BFLYT_CHUNK_pas1:
			return r_pas1 (ctx, d, size);
		case BFLYT_CHUNK_pae1:
			return r_pae1 (ctx, d, size);
		case BFLYT_CHUNK_usd1:
			return r_usd1 (ctx, d, size);
		case BFLYT_CHUNK_prt1:
			return r_prt1 (ctx, d, size);
		case BFLYT_CHUNK_cnt1:
			return r_cnt1 (ctx, d, size);
		default:
		{
			char key[8];
			snprintf (key, sizeof (key), "%c%c%c%c", d[0], d[1], d[2], d[3]);
			bf_node_t *sn = BFNodeSetNode (ctx->actnode, key);
			if (!sn)
				return ERR_OUT_OF_MEMORY;
			if (size > 8)
				BFE (BFNodeSetBytes (sn, "data", d + 8, size - 8));
			return ERR_OK;
		}
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			binary scan			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError parse_binary (bflyt_t *bflyt, const u8 *data, uint data_size)
{
	bf_node_t *tree = &bflyt->tree;
	u32 fmagic = ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | (u32)data[3];
	bool is_rlyt = (fmagic == BRLYT_MAGIC_RLYT || fmagic == BRLYT_MAGIC_RLAN);
	if (data_size < (is_rlyt ? 16u : 20u))
		return ERR_INVALID_DATA;
	bool be = !(data[4] == 0xFF && data[5] == 0xFE);
	u32 version = is_rlyt ? rd16 (data + 6, be) : rd16 (data + 8, be);
	uint secnum = is_rlyt ? rd16 (data + 14, be) : rd16 (data + 16, be);

	BFE (BFNodeSetStr (tree, "byte-order", be ? ">" : "<"));
	BFE (BFNodeSetInt (tree, "version", (int)version));
	ccp magic_str;
	char m[5];
	if (fmagic == BFLYT_MAGIC_FLYT)
		magic_str = "FLYT";
	else if (fmagic == BCLYT_MAGIC_CLYT)
		magic_str = "CLYT";
	else if (fmagic == BFLYT_MAGIC_FLAN)
		magic_str = "FLAN";
	else if (fmagic == BCLYT_MAGIC_CLAN)
		magic_str = "CLAN";
	else if (fmagic == BRLYT_MAGIC_RLYT)
		magic_str = "RLYT";
	else if (fmagic == BRLYT_MAGIC_RLAN)
		magic_str = "RLAN";
	else
	{
		snprintf (m, sizeof (m), "%c%c%c%c", data[0], data[1], data[2], data[3]);
		magic_str = m;
	}
	BFE (BFNodeSetStr (tree, "magic", magic_str));

	bf_node_t *bflyt_node = BFNodeSetNode (tree, "BFLYT");
	if (!bflyt_node)
		return ERR_OUT_OF_MEMORY;

	bf_rctx_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	ctx.be = be;
	ctx.is_rlyt = is_rlyt;
	ctx.is_wiiu = (fmagic == BFLYT_MAGIC_FLYT || fmagic == BFLYT_MAGIC_FLAN);
	ctx.version = version;
	ctx.data = data;
	ctx.size = data_size;
	ctx.tree = tree;
	ctx.actnode = bflyt_node;
	ctx.prevname[0] = 0;

	uint pos = is_rlyt ? 0x10 : 0x14;
	for (uint i = 0; i < secnum; i++)
	{
		if (pos + 8 > data_size)
			return ERR_INVALID_DATA;
		u32 smagic = ((u32)data[pos] << 24) | ((u32)data[pos + 1] << 16) | ((u32)data[pos + 2] << 8)
			| (u32)data[pos + 3];
		u32 chunk_size = rd32 (data + pos + 4, be);
		if (chunk_size < 8 || pos + chunk_size > data_size)
			return ERR_INVALID_DATA;
		enumError err = r_section (&ctx, smagic, data + pos, chunk_size);
		if (err)
			return err;
		pos += chunk_size;
	}
	BFListFree (&ctx.texturenames);
	BFListFree (&ctx.fontnames);
	bf_list_clear (&ctx.materials);
	bflyt->magic = fmagic;
	bflyt->data_size = data_size;
	return ERR_OK;
}

// detect the txtree text format
static bool is_text_data (const u8 *data, uint size)
{
	uint lim = size < 64 ? size : 64;
	for (uint i = 0; i < lim; i++)
	{
		if (!memcmp (data + i, "byte-order", 10))
			return true;
		if (data[i] == '\n' || data[i] == '\r' || data[i] == ' ' || data[i] == '\t'
			|| data[i] == '#')
			continue;
	}
	return false;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			packer context			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct bf_pctx_t
{
	bool be;
	bool is_wiiu; // see readpane()'s is_wiiu comment
	u32 secnum;
	bf_list_t textures; // str list
	bf_list_t fontnames; // str list
	bf_list_t matnames; // str list
} bf_pctx_t;

static void pctx_free (bf_pctx_t *ctx)
{
	BFListFree (&ctx->textures);
	BFListFree (&ctx->fontnames);
	BFListFree (&ctx->matnames);
}

static enumError p_color (bf_buf_t *b, bool be, const bf_node_t *col)
{
	BFE (bf_buf_u8 (b, (u8)bf_get_int (col, "RED", 0)));
	BFE (bf_buf_u8 (b, (u8)bf_get_int (col, "GREEN", 0)));
	BFE (bf_buf_u8 (b, (u8)bf_get_int (col, "BLUE", 0)));
	if (bf_get_int (col, "ALPHA", -1) >= 0)
		BFE (bf_buf_u8 (b, (u8)bf_get_int (col, "ALPHA", 0)));
	return ERR_OK;
}

static enumError p_pane (bf_buf_t *b, bf_pctx_t *ctx, const bf_node_t *node)
{
	u8 flags = 0;
	if (bf_get_bool (node, "visible", false))
		flags |= 0x01;
	if (bf_get_bool (node, "transmit-alpha-to-children", false))
		flags |= 0x02;
	if (bf_get_bool (node, "position-adjustment", false))
		flags |= 0x04;
	BFE (bf_buf_u8 (b, flags));

	// "origin"/"parent-origin" are themselves NODEs (with "x"/"y" string
	// children), not strings -- bf_get_str() on them always returned NULL,
	// so this previously fell through to the "Center" default on every
	// single encode regardless of the real value, silently discarding
	// origin/parent-origin on every re-encode. Confirmed on a real 3DS
	// BCLYT round-trip: a real "y":"Up" pane origin came back as "Center"
	// after decode->encode->decode.
	ccp ox = bf_get_node (node, "origin") ? bf_get_str (bf_get_node (node, "origin"), "x") : 0;
	ccp oy = bf_get_node (node, "origin") ? bf_get_str (bf_get_node (node, "origin"), "y") : 0;
	ccp pox = bf_get_node (node, "parent-origin")
		? bf_get_str (bf_get_node (node, "parent-origin"), "x")
		: 0;
	ccp poy = bf_get_node (node, "parent-origin")
		? bf_get_str (bf_get_node (node, "parent-origin"), "y")
		: 0;
	int oix = bf_str_index (ORIG_X, 3, ox ? ox : "Center");
	int oiy = bf_str_index (ORIG_Y, 3, oy ? oy : "Center");
	int poix = bf_str_index (ORIG_X, 3, pox ? pox : "Center");
	int poiy = bf_str_index (ORIG_Y, 3, poy ? poy : "Center");
	if (oix < 0 || oiy < 0 || poix < 0 || poiy < 0)
		return ERR_INVALID_DATA;
	u8 main_origin = (u8)((oiy * 4) + oix);
	u8 parent_origin = (u8)((poiy * 4) + poix);
	BFE (bf_buf_u8 (b, (u8)((parent_origin * 16) + main_origin)));
	BFE (bf_buf_u8 (b, (u8)bf_get_int (node, "alpha", 0)));
	BFE (bf_buf_u8 (b, (u8)bf_get_int (node, "part-scale", 0)));
	BFE (bf_buf_str (b, bf_get_str (node, "name") ? bf_get_str (node, "name") : "", 24));
	if (ctx->is_wiiu)
		for (int i = 0; i < 8; i++)
			BFE (bf_buf_u8 (b, 0));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "X-translation", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "Y-translation", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "Z-translation", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "X-rotation", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "Y-rotation", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "Z-rotation", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "X-scale", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "Y-scale", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "width", 0)));
	BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "height", 0)));
	return ERR_OK;
}

// Build mat1 block flags. The decoder always stores the exact original
// 32-bit value under "flags" (bit-exact round-trip); item-count
// reconstruction is only a fallback for hand-authored text lacking it.
static u32 mat_flags (const bf_node_t *mat)
{
	bf_val_t *fv = BFNodeGet ((bf_node_t *)mat, "flags");
	if (fv && fv->type == BF_T_INT)
		return (u32)fv->u.i;

	u32 flags = 0;
	flags |= bf_magiccount (mat, "texref");
	flags |= bf_magiccount (mat, "textureSRT") << 2;
	flags |= bf_magiccount (mat, "texcoord-gen") << 4;
	flags |= bf_magiccount (mat, "tev-stage") << 6;
	if (bf_get_node (mat, "alpha-compare"))
		flags |= 1u << 9;
	if (bf_get_node (mat, "color-blend-mode"))
		flags |= 1u << 10;
	if (bf_get_node (mat, "alpha-blend-mode"))
		flags |= 1u << 12;
	if (bf_get_node (mat, "indirect-adjustment"))
		flags |= 1u << 14;
	flags |= bf_magiccount (mat, "projection-mapping") << 15;
	if (bf_get_node (mat, "shadow-blending"))
		flags |= 1u << 17;
	return flags;
}

static enumError p_mat_item (bf_buf_t *b, bf_pctx_t *ctx, ccp itemtype, const bf_node_t *dic)
{
	if (!strcmp (itemtype, "texref"))
	{
		int fi = bf_strlist_index (&ctx->textures, bf_get_str (dic, "file"));
		int ws = bf_str_index (WRAPS, 8, bf_get_str (dic, "wrap-S"));
		int wt = bf_str_index (WRAPS, 8, bf_get_str (dic, "wrap-T"));
		if (fi < 0 || ws < 0 || wt < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_u16 (b, ctx->be, (u16)fi));
		BFE (bf_buf_u8 (b, (u8)ws));
		BFE (bf_buf_u8 (b, (u8)wt));
	}
	else if (!strcmp (itemtype, "textureSRT"))
	{
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "X-translate", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "Y-translate", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "rotate", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "X-scale", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "Y-scale", 0)));
	}
	else if (!strcmp (itemtype, "texcoord-gen"))
	{
		int s = bf_str_index (TEXCOORD_GEN_SOURCE, 6, bf_get_str (dic, "source"));
		if (s < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_u8 (b, (u8)bf_get_int (dic, "unknown-1", 0)));
		BFE (bf_buf_u8 (b, (u8)s));
		BFE (bf_buf_u16 (b, ctx->be, (u16)bf_get_int (dic, "unknown-2", 0)));
	}
	else if (!strcmp (itemtype, "tev-stage"))
	{
		BFE (bf_buf_u32 (b, ctx->be, (u32)bf_get_int (dic, "color-combiner", 0)));
		BFE (bf_buf_u32 (b, ctx->be, (u32)bf_get_int (dic, "alpha-combiner", 0)));
		BFE (bf_buf_u32 (b, ctx->be, (u32)bf_get_int (dic, "const-colors", 0)));
	}
	else if (!strcmp (itemtype, "alpha-compare"))
	{
		int c = bf_str_index (ALPHA_COMPARE_CONDITIONS, 8, bf_get_str (dic, "condition"));
		if (c < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_u32 (b, ctx->be, (u32)c));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "value", 0)));
	}
	else if (!strcmp (itemtype, "color-blend-mode") || !strcmp (itemtype, "alpha-blend-mode"))
	{
		int op = bf_str_index (BLEND_CALC_OPS, 6, bf_get_str (dic, "blend-operation"));
		int src = bf_str_index (BLEND_CALC, 10, bf_get_str (dic, "source"));
		int dst = bf_str_index (BLEND_CALC, 10, bf_get_str (dic, "destination"));
		BFE (bf_buf_u8 (b, (u8)op));
		BFE (bf_buf_u8 (b, (u8)src));
		BFE (bf_buf_u8 (b, (u8)dst));
		if (!strcmp (itemtype, "color-blend-mode"))
		{
			int lg = bf_str_index (LOGICAL_CALC_OPS, 17, bf_get_str (dic, "logical-operation"));
			if (op < 0 || src < 0 || dst < 0 || lg < 0)
				return ERR_INVALID_DATA;
			BFE (bf_buf_u8 (b, (u8)lg));
		}
		else
		{
			if (op < 0 || src < 0 || dst < 0)
				return ERR_INVALID_DATA;
			BFE (bf_buf_u8 (b, (u8)bf_get_int (dic, "unknown", 0)));
		}
	}
	else if (!strcmp (itemtype, "indirect-adjustment"))
	{
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "rotate", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "X-warp", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "Y-warp", 0)));
	}
	else if (!strcmp (itemtype, "projection-mapping"))
	{
		int o = bf_str_index (PROJECTION_MAPPING_TYPES, 7, bf_get_str (dic, "option"));
		if (o < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "X-translate", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "Y-translate", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "X-scale", 0)));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (dic, "Y-scale", 0)));
		BFE (bf_buf_u8 (b, (u8)o));
		BFE (bf_buf_u8 (b, (u8)bf_get_int (dic, "unknown-1", 0)));
		BFE (bf_buf_u16 (b, ctx->be, (u16)bf_get_int (dic, "unknown-2", 0)));
	}
	else if (!strcmp (itemtype, "shadow-blending"))
	{
		const bf_node_t *black = bf_get_node (dic, "black-blending");
		const bf_node_t *white = bf_get_node (dic, "white-blending");
		if (!black || !white)
			return ERR_INVALID_DATA;
		BFE (p_color (b, ctx->be, black));
		BFE (p_color (b, ctx->be, white));
		BFE (bf_buf_u8 (b, 0));
	}
	return ERR_OK;
}

static enumError p_wiiu_blend (bf_buf_t *b, bf_pctx_t *ctx, const bf_node_t *node)
{
	int op = bf_str_index (BLEND_CALC_OPS, 6, bf_get_str (node, "blend-operation"));
	int src = bf_str_index (BLEND_CALC, 10, bf_get_str (node, "source"));
	int dst = bf_str_index (BLEND_CALC, 10, bf_get_str (node, "destination"));
	int logic = bf_str_index (LOGICAL_CALC_OPS, 17, bf_get_str (node, "logical-operation"));
	if (op < 0 || src < 0 || dst < 0 || logic < 0)
		return ERR_INVALID_DATA;
	BFE (bf_buf_u8 (b, (u8)op));
	BFE (bf_buf_u8 (b, (u8)src));
	BFE (bf_buf_u8 (b, (u8)dst));
	BFE (bf_buf_u8 (b, (u8)logic));
	return ERR_OK;
}

static enumError p_mat_item_wiiu (bf_buf_t *b, bf_pctx_t *ctx, ccp itemtype, const bf_node_t *node)
{
	if (!strcmp (itemtype, "texref"))
		return p_mat_item (b, ctx, itemtype, node);

	if (!strcmp (itemtype, "textureSRT"))
		return p_mat_item (b, ctx, itemtype, node);

	if (!strcmp (itemtype, "mapping-setting"))
	{
		int method = bf_str_index (MAPPING_METHODS, 5, bf_get_str (node, "method"));
		if (method < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_u8 (b, (u8)bf_get_int (node, "unknown", 0)));
		BFE (bf_buf_u8 (b, (u8)method));
		BFE (bf_buf_pad (b, 6));
		return ERR_OK;
	}

	if (!strcmp (itemtype, "texture-combiner"))
	{
		int color = bf_str_index (COLOR_BLENDS, 12, bf_get_str (node, "color-blending"));
		int alpha = bf_str_index (BLENDS, 2, bf_get_str (node, "alpha-blending"));
		if (color < 0 || alpha < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_u8 (b, (u8)color));
		BFE (bf_buf_u8 (b, (u8)alpha));
		BFE (bf_buf_u16 (b, ctx->be, (u16)bf_get_int (node, "unknown", 0)));
		return ERR_OK;
	}

	if (!strcmp (itemtype, "alpha-compare"))
	{
		int condition = bf_str_index (ALPHA_COMPARE_CONDITIONS, 8, bf_get_str (node, "condition"));
		if (condition < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_u8 (b, (u8)condition));
		BFE (bf_buf_pad (b, 3));
		BFE (bf_buf_f32 (b, ctx->be, (float)bf_get_float (node, "value", 0)));
		return ERR_OK;
	}

	if (!strcmp (itemtype, "blend-mode") || !strcmp (itemtype, "alpha-blend-mode"))
		return p_wiiu_blend (b, ctx, node);

	if (!strcmp (itemtype, "indirect-adjustment"))
		return p_mat_item (b, ctx, itemtype, node);

	if (!strcmp (itemtype, "projection-mapping"))
		return p_mat_item (b, ctx, itemtype, node);

	if (!strcmp (itemtype, "shadow-blending"))
	{
		const bf_node_t *black = bf_get_node (node, "black-blending");
		const bf_node_t *white = bf_get_node (node, "white-blending");
		if (!black || !white)
			return ERR_INVALID_DATA;

		// Wii U stores black RGB, then white RGBA, then one pad byte. The
		// decoder deliberately reads black as an overlapping RGBA value, so
		// black alpha is the first (red) byte of white.
		BFE (bf_buf_u8 (b, (u8)bf_get_int (black, "RED", 0)));
		BFE (bf_buf_u8 (b, (u8)bf_get_int (black, "GREEN", 0)));
		BFE (bf_buf_u8 (b, (u8)bf_get_int (black, "BLUE", 0)));
		BFE (p_color (b, ctx->be, white));
		BFE (bf_buf_u8 (b, 0));
		return ERR_OK;
	}

	return ERR_INVALID_DATA;
}

static enumError p_mat1_wiiu (bf_pctx_t *ctx, bf_buf_t *out, const bf_node_t *node)
{
	bf_list_t *materials = bf_get_list (node, "materials");
	const uint num = materials ? materials->n : 0;
	bf_buf_t body, offsets, records;
	memset (&body, 0, sizeof (body));
	memset (&offsets, 0, sizeof (offsets));
	memset (&records, 0, sizeof (records));
	BFListFree (&ctx->matnames);

	const uint records_off = 12 + num * 4;
	for (uint i = 0; i < num; i++)
	{
		if (materials->items[i].type != BF_T_NODE)
			return ERR_INVALID_DATA;
		const bf_node_t *mat = materials->items[i].u.node;
		const bf_node_t *fg = bf_get_node (mat, "foreground-color");
		const bf_node_t *bg = bf_get_node (mat, "background-color");
		if (!fg || !bg)
			return ERR_INVALID_DATA;

		BFE (bf_buf_u32 (&offsets, ctx->be, records_off + records.n));
		BFE (bf_buf_str (&records, bf_get_str (mat, "name") ? bf_get_str (mat, "name") : "", 28));
		BFE (p_color (&records, ctx->be, fg));
		BFE (p_color (&records, ctx->be, bg));

		const u32 flags = mat_flags (mat);
		BFE (bf_buf_u32 (&records, ctx->be, flags));

		struct counted_item_t
		{
			ccp name;
			uint count;
		} counted[] = {
			{ "texref", (flags >> 0) & 3 },
			{ "textureSRT", (flags >> 2) & 3 },
			{ "mapping-setting", (flags >> 4) & 3 },
			{ "texture-combiner", (flags >> 6) & 3 },
		};
		for (uint j = 0; j < sizeof (counted) / sizeof (*counted); j++)
			for (uint k = 0; k < counted[j].count; k++)
			{
				char key[40];
				snprintf (key, sizeof (key), "%s-%u", counted[j].name, k);
				const bf_node_t *item = bf_get_node (mat, key);
				if (!item)
					return ERR_INVALID_DATA;
				BFE (p_mat_item_wiiu (&records, ctx, counted[j].name, item));
			}

		if ((flags >> 9) & 1)
		{
			const bf_node_t *item = bf_get_node (mat, "alpha-compare");
			if (!item)
				return ERR_INVALID_DATA;
			BFE (p_mat_item_wiiu (&records, ctx, "alpha-compare", item));
		}
		if ((flags >> 10) & 1)
		{
			const bf_node_t *item = bf_get_node (mat, "blend-mode");
			if (!item)
				return ERR_INVALID_DATA;
			BFE (p_mat_item_wiiu (&records, ctx, "blend-mode", item));
		}
		for (uint k = 0, n = (flags >> 12) & 3; k < n; k++)
		{
			char key[40];
			snprintf (key, sizeof (key), "alpha-blend-mode-%u", k);
			const bf_node_t *item = bf_get_node (mat, key);
			if (!item)
				return ERR_INVALID_DATA;
			BFE (p_mat_item_wiiu (&records, ctx, "alpha-blend-mode", item));
		}
		if ((flags >> 14) & 1)
		{
			const bf_node_t *item = bf_get_node (mat, "indirect-adjustment");
			if (!item)
				return ERR_INVALID_DATA;
			BFE (p_mat_item_wiiu (&records, ctx, "indirect-adjustment", item));
		}
		for (uint k = 0, n = (flags >> 15) & 3; k < n; k++)
		{
			char key[40];
			snprintf (key, sizeof (key), "projection-mapping-%u", k);
			const bf_node_t *item = bf_get_node (mat, key);
			if (!item)
				return ERR_INVALID_DATA;
			BFE (p_mat_item_wiiu (&records, ctx, "projection-mapping", item));
		}
		if ((flags >> 17) & 1)
		{
			const bf_node_t *item = bf_get_node (mat, "shadow-blending");
			if (!item)
				return ERR_INVALID_DATA;
			BFE (p_mat_item_wiiu (&records, ctx, "shadow-blending", item));
		}

		BFE (BFListAddStr (
			&ctx->matnames, bf_get_str (mat, "name") ? bf_get_str (mat, "name") : ""));
	}

	BFE (bf_buf_u16 (&body, ctx->be, (u16)num));
	BFE (bf_buf_u16 (&body, ctx->be, 0));
	BFE (bf_buf_raw (&body, offsets.d, offsets.n));
	BFE (bf_buf_raw (&body, records.d, records.n));
	if (bf_get_str (node, "extra"))
		BFE (bf_hex_decode (bf_get_str (node, "extra"), &body));

	enumError err = bf_buf_sechdr (out, ctx->be, "mat1", body.n);
	if (!err)
		err = bf_buf_raw (out, body.d, body.n);
	bf_buf_free (&body);
	bf_buf_free (&offsets);
	bf_buf_free (&records);
	return err;
}

// forward declaration
static enumError repacktree (
	bf_pctx_t *ctx, const bf_node_t *tree, bf_buf_t *out, bool count_top, bool safe);

static enumError p_lyt1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	bf_buf_t b;
	memset (&b, 0, sizeof (b));
	if (ctx->is_wiiu) // see r_lyt1()'s is_wiiu comment
	{
		BFE (bf_buf_u8 (&b, bf_get_bool (node, "drawn-from-middle", false) ? 1 : 0));
		BFE (bf_buf_pad (&b, 3));
		BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "screen-width", 0)));
		BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "screen-height", 0)));
		BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "max-parts-height", 0)));
		BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "max-parts-width", 0)));
		ccp name = bf_get_str (node, "name");
		BFE (bf_buf_str (&b, name ? name : "", 0));
		// Section sizes are 4-byte aligned; a real file with this name
		// showed 16 bytes of trailing name buffer for a 10-char + NUL
		// name, i.e. padded past align-to-4, but since this reader already
		// just consumes size-p (whatever's left in the section) rather
		// than a hardcoded width, only the alignment matters for a valid
		// re-encode -- getting the exact same pad amount as the original
		// isn't required for correctness, just staying 4-byte aligned so
		// the following section's offset doesn't desync.
		BFE (bf_buf_pad (&b, (4 - b.n % 4) % 4));
		enumError err = bf_buf_sechdr (out, ctx->be, "lyt1", b.n);
		if (!err)
			err = bf_buf_raw (out, b.d, b.n);
		bf_buf_free (&b);
		return err;
	}
	ccp origin = bf_get_str (node, "screen-origin");
	BFE (bf_buf_u32 (&b, ctx->be, origin && !strcmp (origin, "Normal") ? 1 : 0));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "screen-width", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "screen-height", 0)));
	enumError err = bf_buf_sechdr (out, ctx->be, "lyt1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	return err;
}

static enumError p_name_list (
	bf_pctx_t *ctx, bf_buf_t *out, ccp magic, const bf_node_t *node, bf_list_t *cache)
{
	bf_list_t *names = bf_get_list (node, "file-names");
	uint num = names ? names->n : 0;
	bf_buf_t b, filetable, offsettbl;
	memset (&b, 0, sizeof (b));
	memset (&filetable, 0, sizeof (filetable));
	memset (&offsettbl, 0, sizeof (offsettbl));
	BFListFree (cache);
	for (uint i = 0; i < num; i++)
	{
		ccp s = (names->items[i].type == BF_T_STR) ? names->items[i].u.s : "";
		BFE (bf_buf_u32 (&offsettbl, ctx->be, filetable.n + num * 4));
		BFE (bf_buf_str (&filetable, s, 0));
		BFE (BFListAddStr (cache, s));
	}
	BFE (bf_buf_u16 (&b, ctx->be, (u16)num));
	BFE (bf_buf_u16 (&b, ctx->be, 0));
	BFE (bf_buf_raw (&b, offsettbl.d, offsettbl.n));
	BFE (bf_buf_raw (&b, filetable.d, filetable.n));
	if (b.n & 3)
		BFE (bf_buf_pad (&b, 4 - (b.n & 3)));
	enumError err = bf_buf_sechdr (out, ctx->be, magic, b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	bf_buf_free (&filetable);
	bf_buf_free (&offsettbl);
	return err;
}

static enumError p_txl1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	return p_name_list (ctx, out, "txl1", v->u.node, &ctx->textures);
}

static enumError p_fnl1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	return p_name_list (ctx, out, "fnl1", v->u.node, &ctx->fontnames);
}

static enumError p_mat1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	if (ctx->is_wiiu)
		return p_mat1_wiiu (ctx, out, node);
	bf_list_t *materials = bf_get_list (node, "materials");
	uint num = materials ? materials->n : 0;
	bf_buf_t b, offsettbl, matdata;
	memset (&b, 0, sizeof (b));
	memset (&offsettbl, 0, sizeof (offsettbl));
	memset (&matdata, 0, sizeof (matdata));
	BFListFree (&ctx->matnames);
	uint offset_tbl_length = 12 + num * 4;
	for (uint i = 0; i < num; i++)
	{
		const bf_node_t *mat = materials->items[i].u.node;
		BFE (bf_buf_u32 (&offsettbl, ctx->be, offset_tbl_length + matdata.n));
		BFE (bf_buf_str (&matdata, bf_get_str (mat, "name") ? bf_get_str (mat, "name") : "", 20));
		const bf_node_t *bufc = bf_get_node (mat, "buffer-color");
		if (!bufc)
		{
			bf_buf_free (&b);
			bf_buf_free (&offsettbl);
			bf_buf_free (&matdata);
			return ERR_INVALID_DATA;
		}
		BFE (p_color (&matdata, ctx->be, bufc));
		for (uint k = 0; k < 6; k++)
		{
			char key[24];
			snprintf (key, sizeof (key), "const-color-%u", k);
			const bf_node_t *cc = bf_get_node (mat, key);
			if (!cc)
			{
				bf_buf_free (&b);
				bf_buf_free (&offsettbl);
				bf_buf_free (&matdata);
				return ERR_INVALID_DATA;
			}
			BFE (p_color (&matdata, ctx->be, cc));
		}
		BFE (bf_buf_u32 (&matdata, ctx->be, mat_flags (mat)));
		// blocks in tree key order
		for (uint k = 0; k < mat->n; k++)
		{
			ccp key = mat->kv[k].key;
			if (!strcmp (key, "name") || !strcmp (key, "buffer-color") || !strcmp (key, "flags")
				|| !strcmp (key, "extra") || !strncmp (key, "const-color-", 12))
				continue;
			// Only a trailing numeric suffix ("-N") marks a counted/indexed
			// item whose index needs stripping to get the item type (e.g.
			// "tev-stage-2" -> "tev-stage"); a key that just happens to
			// contain a dash (e.g. "alpha-compare", "color-blend-mode") is a
			// single non-indexed item and must be matched in full, or it
			// silently fails to encode (a real pre-existing bug: this used to
			// always strip at the *last* dash regardless, so "alpha-compare"
			// -> itemtype "alpha", "indirect-adjustment" -> "indirect", and
			// "shadow-blending" -> "shadow" -- none of which matched any
			// p_mat_item() case, so those three blocks were silently dropped
			// on every encode).
			ccp itemtype = key;
			ccp dash = strrchr (key, '-');
			bool numbered = dash && dash[1];
			for (ccp c = dash ? dash + 1 : 0; numbered && *c; c++)
				if (!isdigit ((unsigned char)*c))
					numbered = false;
			if (numbered)
				itemtype = bf_strndup (key, (uint)(dash - key));
			else
				itemtype = bf_strdup (key);
			if (!itemtype)
			{
				bf_buf_free (&b);
				bf_buf_free (&offsettbl);
				bf_buf_free (&matdata);
				return ERR_OUT_OF_MEMORY;
			}
			if (mat->kv[k].val.type == BF_T_NODE)
			{
				enumError err = p_mat_item (&matdata, ctx, itemtype, mat->kv[k].val.u.node);
				FREE ((char *)itemtype);
				if (err)
				{
					bf_buf_free (&b);
					bf_buf_free (&offsettbl);
					bf_buf_free (&matdata);
					return err;
				}
			}
			else
				FREE ((char *)itemtype);
		}
		BFE (BFListAddStr (
			&ctx->matnames, bf_get_str (mat, "name") ? bf_get_str (mat, "name") : ""));
	}
	BFE (bf_buf_u16 (&b, ctx->be, (u16)num));
	BFE (bf_buf_u16 (&b, ctx->be, 0));
	BFE (bf_buf_raw (&b, offsettbl.d, offsettbl.n));
	BFE (bf_buf_raw (&b, matdata.d, matdata.n));
	if (bf_get_str (node, "extra"))
	{
		enumError err = bf_hex_decode (bf_get_str (node, "extra"), &b);
		if (err)
		{
			bf_buf_free (&b);
			bf_buf_free (&offsettbl);
			bf_buf_free (&matdata);
			return err;
		}
	}
	enumError err = bf_buf_sechdr (out, ctx->be, "mat1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	bf_buf_free (&offsettbl);
	bf_buf_free (&matdata);
	return err;
}

static enumError p_pane_sec (bf_pctx_t *ctx, bf_buf_t *out, ccp magic, const bf_node_t *node,
	enumError (*extra) (bf_pctx_t *, bf_buf_t *, const bf_node_t *))
{
	bf_buf_t b;
	memset (&b, 0, sizeof (b));
	BFE (p_pane (&b, ctx, node));
	if (extra)
		BFE (extra (ctx, &b, node));
	enumError err = bf_buf_sechdr (out, ctx->be, magic, b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	return err;
}

static enumError p_pan1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	return p_pane_sec (ctx, out, "pan1", v->u.node, 0);
}

static enumError p_pic1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	bf_buf_t b;
	memset (&b, 0, sizeof (b));
	BFE (p_pane (&b, ctx, node));
	static const char *const vc[4] = { "top-left-vtx-color", "top-right-vtx-color",
		"bottom-left-vtx-color", "bottom-right-vtx-color" };
	for (uint i = 0; i < 4; i++)
	{
		const bf_node_t *c = bf_get_node (node, vc[i]);
		if (!c)
		{
			bf_buf_free (&b);
			return ERR_INVALID_DATA;
		}
		BFE (p_color (&b, ctx->be, c));
	}
	ccp mat = bf_get_str (node, "material");
	int mi = mat ? bf_strlist_index (&ctx->matnames, mat) : -1;
	if (mi < 0)
	{
		bf_buf_free (&b);
		return ERR_INVALID_DATA;
	}
	bf_list_t *coords = bf_get_list (node, "tex-coords");
	uint num = coords ? coords->n : 0;
	BFE (bf_buf_u16 (&b, ctx->be, (u16)mi));
	BFE (bf_buf_u8 (&b, (u8)num));
	BFE (bf_buf_u8 (&b, 0));
	for (uint i = 0; i < num; i++)
	{
		const bf_node_t *en = coords->items[i].u.node;
		static const char *const corners[4]
			= { "top-left", "top-right", "bottom-left", "bottom-right" };
		for (uint c = 0; c < 4; c++)
		{
			const bf_node_t *cn = bf_get_node (en, corners[c]);
			if (!cn)
			{
				bf_buf_free (&b);
				return ERR_INVALID_DATA;
			}
			BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (cn, "s", 0)));
			BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (cn, "t", 0)));
		}
	}
	enumError err = bf_buf_sechdr (out, ctx->be, "pic1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	return err;
}

static enumError p_txt1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	bf_buf_t b;
	memset (&b, 0, sizeof (b));
	BFE (p_pane (&b, ctx, node));

	ccp text = bf_get_str (node, "text");
	if (!text)
		text = "";
	uint text_n;
	u8 *textb = utf8_to_utf16 (text, ctx->be, &text_n);
	if (!textb)
	{
		bf_buf_free (&b);
		return ERR_OUT_OF_MEMORY;
	}
	// code units
	uint units = 0;
	for (uint i = 0; i < text_n; i += 2)
	{
		u16 u = ctx->be ? (u16)((textb[i] << 8) | textb[i + 1])
						: (u16)(textb[i] | (textb[i + 1] << 8));
		if (u >= 0xD800 && u <= 0xDBFF)
			i += 2;
		units++;
	}

	ccp mat = bf_get_str (node, "material");
	ccp font = bf_get_str (node, "font");
	int mi = mat ? bf_strlist_index (&ctx->matnames, mat) : -1;
	int fi = font ? bf_strlist_index (&ctx->fontnames, font) : -1;
	if ((!ctx->is_wiiu && (mi < 0 || fi < 0)) || (ctx->is_wiiu && (mi < -1 || fi < -1)))
	{
		FREE (textb);
		bf_buf_free (&b);
		return ERR_INVALID_DATA;
	}

	// restrict-length is a reserved-buffer-capacity field, not "current text
	// byte length" -- real files reserve more than the current text uses
	// (room for later in-game edits), and the reader decodes exactly
	// restrict-length bytes as UTF16 (stopping at the embedded NUL, so the
	// extra reserved bytes are invisible). Recomputing it from the current
	// text (the old behaviour) silently shrank every real file's reserved
	// capacity on re-encode -- preserve the original stored value instead,
	// falling back to the actual text size only for hand-authored/new text
	// that has no prior "restrict-length" key.
	int stored_restrict = bf_get_int (node, "restrict-length", -1);
	int stored_length = bf_get_int (node, "length", -1);
	uint restrict_len = stored_restrict >= 0 ? (uint)stored_restrict : text_n;
	if (restrict_len < text_n)
		restrict_len = text_n; // never truncate real text
	uint length_val = stored_length >= 0 ? (uint)stored_length : units;

	if (ctx->is_wiiu)
	{
		BFE (bf_buf_u16 (&b, ctx->be, (u16)length_val));
		BFE (bf_buf_u16 (&b, ctx->be, (u16)restrict_len));
	}
	else
	{
		BFE (bf_buf_u16 (&b, ctx->be, (u16)restrict_len));
		BFE (bf_buf_u16 (&b, ctx->be, (u16)length_val));
	}
	BFE (bf_buf_u16 (&b, ctx->be, mi < 0 ? 0xffff : (u16)mi));
	BFE (bf_buf_u16 (&b, ctx->be, fi < 0 ? 0xffff : (u16)fi));
	const bf_node_t *al = bf_get_node (node, "alignment");
	int ax = -1, ay = -1;
	if (al)
	{
		ax = bf_str_index (ORIG_X, 3, bf_get_str (al, "x"));
		ay = bf_str_index (ORIG_Y, 3, bf_get_str (al, "y"));
	}
	if (ax < 0)
		ax = 0;
	if (ay < 0)
		ay = 0;
	int la = bf_str_index (TEXT_ALIGNS, 4, bf_get_str (node, "line-alignment"));
	if (la < 0)
		la = 0;
	BFE (bf_buf_u8 (&b, (u8)((ay * 4) + ax)));
	BFE (bf_buf_u8 (&b, (u8)la));
	BFE (bf_buf_u8 (&b, (u8)bf_get_int (node, "active-shadows", 0)));
	BFE (bf_buf_u8 (&b, (u8)bf_get_int (node, "unknown-1", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "italic-tilt", 0)));
	BFE (bf_buf_u32 (&b, ctx->be, 164));
	const bf_node_t *tc = bf_get_node (node, "top-color");
	const bf_node_t *btc = bf_get_node (node, "bottom-color");
	if (!tc || !btc)
	{
		FREE (textb);
		bf_buf_free (&b);
		return ERR_INVALID_DATA;
	}
	BFE (p_color (&b, ctx->be, tc));
	BFE (p_color (&b, ctx->be, btc));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "font-size-x", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "font-size-y", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "char-space", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "line-space", 0)));
	ccp callname = bf_get_str (node, "call-name");
	uint callname_off = 0;
	if (ctx->is_wiiu && callname)
		callname_off = (164 + restrict_len + 3) & ~3u;
	BFE (bf_buf_u32 (&b, ctx->be, callname_off));
	const bf_node_t *sh = bf_get_node (node, "shadow");
	if (!sh)
	{
		FREE (textb);
		bf_buf_free (&b);
		return ERR_INVALID_DATA;
	}
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (sh, "offset-X", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (sh, "offset-Y", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (sh, "scale-X", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (sh, "scale-Y", 0)));
	const bf_node_t *stc = bf_get_node (sh, "top-color");
	const bf_node_t *sbc = bf_get_node (sh, "bottom-color");
	if (!stc || !sbc)
	{
		FREE (textb);
		bf_buf_free (&b);
		return ERR_INVALID_DATA;
	}
	BFE (p_color (&b, ctx->be, stc));
	BFE (p_color (&b, ctx->be, sbc));
	if (ctx->is_wiiu)
	{
		BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "shadow-italic-tilt", 0)));
		BFE (bf_buf_u32 (&b, ctx->be, (u32)bf_get_int (node, "per-char-transform-offset", 0)));
	}
	else
		BFE (bf_buf_u32 (&b, ctx->be, (u32)bf_get_int (node, "shadow-unknown-2", 0)));
	BFE (bf_buf_raw (&b, textb, text_n));
	FREE (textb);
	if (restrict_len > text_n)
		BFE (bf_buf_pad (&b, restrict_len - text_n)); // reserved capacity beyond the current text
	if (b.n & 3)
		BFE (bf_buf_pad (&b, 4 - (b.n & 3)));
	if (callname)
	{
		BFE (bf_buf_str4 (&b, callname));
		if (b.n & 3)
			BFE (bf_buf_pad (&b, 4 - (b.n & 3)));
	}
	enumError err = bf_buf_sechdr (out, ctx->be, "txt1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	return err;
}

static enumError p_wnd1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	bf_buf_t b;
	memset (&b, 0, sizeof (b));
	BFE (p_pane (&b, ctx, node));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "stretch-left", 0)));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "stretch-right", 0)));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "stretch-up", 0)));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "stretch-down", 0)));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "custom-left", 0)));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "custom-right", 0)));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "custom-up", 0)));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "custom-down", 0)));
	bf_list_t *wnd4 = bf_get_list (node, "wnd4-materials");
	uint framenum = wnd4 ? wnd4->n : 0;
	uint coordsnum = bf_keyprefix_count (node, "coords-");
	BFE (bf_buf_u8 (&b, (u8)framenum));
	BFE (bf_buf_u8 (&b, (u8)bf_get_int (node, "flags", 0)));
	BFE (bf_buf_u16 (&b, ctx->be, 0));
	BFE (bf_buf_u32 (&b, ctx->be, 0x70));
	BFE (bf_buf_u32 (&b, ctx->be, 132 + 32 * coordsnum));
	const bf_node_t *col;
	col = bf_get_node (node, "color-1");
	if (!col)
		return ERR_INVALID_DATA;
	BFE (p_color (&b, ctx->be, col));
	col = bf_get_node (node, "color-2");
	if (!col)
		return ERR_INVALID_DATA;
	BFE (p_color (&b, ctx->be, col));
	col = bf_get_node (node, "color-3");
	if (!col)
		return ERR_INVALID_DATA;
	BFE (p_color (&b, ctx->be, col));
	col = bf_get_node (node, "color-4");
	if (!col)
		return ERR_INVALID_DATA;
	BFE (p_color (&b, ctx->be, col));
	ccp mat = bf_get_str (node, "material");
	int mi = mat ? bf_strlist_index (&ctx->matnames, mat) : -1;
	if (mi < 0)
		return ERR_INVALID_DATA;
	BFE (bf_buf_u16 (&b, ctx->be, (u16)mi));
	BFE (bf_buf_u8 (&b, (u8)coordsnum));
	BFE (bf_buf_u8 (&b, 0));
	for (uint i = 0; i < coordsnum; i++)
	{
		char ckey[24];
		snprintf (ckey, sizeof (ckey), "coords-%u", i);
		const bf_node_t *cn = bf_get_node (node, ckey);
		if (!cn)
			return ERR_INVALID_DATA;
		for (uint j = 0; j < 8; j++)
		{
			char tkey[24];
			snprintf (tkey, sizeof (tkey), "texcoord-%u", j);
			BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (cn, tkey, 0)));
		}
	}
	uint part1len = b.n;
	for (uint i = 0; i < framenum; i++)
		BFE (bf_buf_u32 (&b, ctx->be, part1len + 4 * framenum + 4 * i + 8));
	for (uint i = 0; i < framenum; i++)
	{
		const bf_node_t *wn = wnd4->items[i].u.node;
		ccp mname = bf_get_str (wn, "material");
		int wmi = mname ? bf_strlist_index (&ctx->matnames, mname) : -1;
		if (wmi < 0)
			return ERR_INVALID_DATA;
		BFE (bf_buf_u16 (&b, ctx->be, (u16)wmi));
		BFE (bf_buf_u8 (&b, (u8)bf_get_int (wn, "index", 0)));
		BFE (bf_buf_u8 (&b, 0));
	}
	enumError err = bf_buf_sechdr (out, ctx->be, "wnd1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	return err;
}

static enumError p_bnd1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	return p_pane_sec (ctx, out, "bnd1", v->u.node, 0);
}

static enumError p_grp1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	bf_buf_t b;
	memset (&b, 0, sizeof (b));
	// see r_grp1()'s is_wiiu comment: name/count/entry widths differ by
	// platform (this encoder previously used a 34-byte name width that
	// matched neither platform's reader -- a pre-existing, independent bug)
	uint namelen = ctx->is_wiiu ? 24 : 16;
	BFE (bf_buf_str (&b, bf_get_str (node, "name") ? bf_get_str (node, "name") : "", namelen));
	bf_list_t *subs = bf_get_list (node, "subs");
	uint num = subs ? subs->n : 0;
	if (ctx->is_wiiu)
	{
		BFE (bf_buf_u16 (&b, ctx->be, (u16)num));
		BFE (bf_buf_pad (&b, 2));
	}
	else
		BFE (bf_buf_u32 (&b, ctx->be, num));
	for (uint i = 0; i < num; i++)
	{
		ccp s = (subs->items[i].type == BF_T_STR) ? subs->items[i].u.s : "";
		BFE (bf_buf_str (&b, s, namelen));
	}
	bf_val_t *dump = BFNodeGet ((bf_node_t *)node, "dump");
	if (dump && dump->type == BF_T_BYTES)
		BFE (bf_buf_raw (&b, dump->u.by.d, dump->u.by.n));
	enumError err = bf_buf_sechdr (out, ctx->be, "grp1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	return err;
}

static enumError p_pas1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	// pas1 itself is an empty marker section; the panes nested "inside" it in
	// the tree are actually flat sibling sections between pas1 and pae1.
	enumError err = bf_buf_sechdr (out, ctx->be, "pas1", 0);
	if (!err)
		err = repacktree (ctx, v->u.node, out, true, false);
	return err;
}

static enumError p_grs1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	// grs1 itself is an empty marker section; nested groups are flat
	// sibling sections between grs1 and gre1.
	enumError err = bf_buf_sechdr (out, ctx->be, "grs1", 0);
	if (!err)
		err = repacktree (ctx, v->u.node, out, true, false);
	return err;
}

static enumError p_header_only (bf_pctx_t *ctx, bf_buf_t *out, ccp magic)
{
	return bf_buf_sechdr (out, ctx->be, magic, 0);
}

static enumError p_pae1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	return p_header_only (ctx, out, "pae1");
}

static enumError p_gre1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	return p_header_only (ctx, out, "gre1");
}

static enumError p_usd1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	bf_list_t *entries = bf_get_list (node, "entries");
	uint num = entries ? entries->n : 0;
	bf_buf_t b, datatbl, nametbl;
	memset (&b, 0, sizeof (b));
	memset (&datatbl, 0, sizeof (datatbl));
	memset (&nametbl, 0, sizeof (nametbl));
	uint *dataoffsets = (uint *)MALLOC (num ? num * 4 : 1);
	uint *nameoffsets = (uint *)MALLOC (num ? num * 4 : 1);
	u8 *datatypes = (u8 *)MALLOC (num ? num : 1);
	uint *datanums = (uint *)MALLOC (num ? num * 4 : 1);
	if (!dataoffsets || !nameoffsets || !datatypes || !datanums)
	{
		FREE (dataoffsets);
		FREE (nameoffsets);
		FREE (datatypes);
		FREE (datanums);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint i = 0; i < num; i++)
	{
		const bf_node_t *en = entries->items[i].u.node;
		nameoffsets[i] = nametbl.n;
		BFE (bf_buf_str (&nametbl, bf_get_str (en, "name") ? bf_get_str (en, "name") : "", 0));
		dataoffsets[i] = datatbl.n;
		bf_val_t *data = BFNodeGet ((bf_node_t *)en, "data");
		int dtype = 0;
		uint dnum = 0;
		if (data && data->type == BF_T_STR)
		{
			dtype = 0;
			dnum = strlen (data->u.s);
			BFE (bf_buf_raw (&datatbl, data->u.s, dnum));
		}
		else if (data && data->type == BF_T_LIST && data->u.list->n)
		{
			if (data->u.list->items[0].type == BF_T_FLOAT)
				dtype = 2;
			else if (data->u.list->items[0].type == BF_T_INT)
				dtype = 1;
			dnum = data->u.list->n;
			for (uint j = 0; j < dnum; j++)
			{
				bf_val_t *it = &data->u.list->items[j];
				if (dtype == 2)
					BFE (bf_buf_f32 (&datatbl, ctx->be, (float)it->u.f));
				else
					BFE (bf_buf_u32 (&datatbl, ctx->be, (u32)it->u.i));
			}
		}
		datatypes[i] = (u8)dtype;
		datanums[i] = dnum;
	}
	if (datatbl.n & 3)
		BFE (bf_buf_pad (&datatbl, 4 - (datatbl.n & 3)));
	if (nametbl.n & 3)
		BFE (bf_buf_pad (&nametbl, 4 - (nametbl.n & 3)));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)num));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)bf_get_int (node, "unknown", 0)));
	for (uint i = 0; i < num; i++)
	{
		// entry-relative offsets
		BFE (bf_buf_u32 (&b, ctx->be, 12 * (num - i) + datatbl.n + nameoffsets[i]));
		BFE (bf_buf_u32 (&b, ctx->be, 12 * (num - i) + dataoffsets[i]));
		BFE (bf_buf_u16 (&b, ctx->be, (u16)datanums[i]));
		BFE (bf_buf_u8 (&b, datatypes[i]));
		bf_val_t *unk = BFNodeGet ((bf_node_t *)entries->items[i].u.node, "unknown");
		BFE (bf_buf_u8 (&b, (u8)(unk && unk->type == BF_T_INT ? unk->u.i : 0)));
	}
	BFE (bf_buf_raw (&b, datatbl.d, datatbl.n));
	BFE (bf_buf_raw (&b, nametbl.d, nametbl.n));
	FREE (dataoffsets);
	FREE (nameoffsets);
	FREE (datatypes);
	FREE (datanums);
	enumError err = bf_buf_sechdr (out, ctx->be, "usd1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	bf_buf_free (&datatbl);
	bf_buf_free (&nametbl);
	return err;
}

static enumError p_prt1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	bf_list_t *entries = bf_get_list (node, "entries");
	uint num = entries ? entries->n : 0;
	bf_buf_t b, entrydata, extradata;
	memset (&b, 0, sizeof (b));
	memset (&entrydata, 0, sizeof (entrydata));
	memset (&extradata, 0, sizeof (extradata));
	uint *dataoffsets = (uint *)MALLOC (num ? num * 4 : 1);
	uint *usd1offsets = (uint *)MALLOC (num ? num * 4 : 1);
	uint *extraoffsets = (uint *)MALLOC (num ? num * 4 : 1);
	if (!dataoffsets || !usd1offsets || !extraoffsets)
	{
		FREE (dataoffsets);
		FREE (usd1offsets);
		FREE (extraoffsets);
		return ERR_OUT_OF_MEMORY;
	}
	// pass 1: build sections
	for (uint i = 0; i < num; i++)
	{
		const bf_node_t *en = entries->items[i].u.node;
		bf_buf_t sec;
		memset (&sec, 0, sizeof (sec));
		BFE (repacktree (ctx, en, &sec, false, true));
		dataoffsets[i] = 0;
		usd1offsets[i] = 0;
		if (sec.n)
		{
			uint base = 96 + 40 * num + entrydata.n;
			if (sec.n < 8)
				return ERR_INVALID_DATA;
			uint first_size = rd32 (sec.d + 4, ctx->be);
			if (first_size < 8 || first_size > sec.n)
				return ERR_INVALID_DATA;
			dataoffsets[i] = base;
			if (first_size < sec.n)
			{
				if (sec.n - first_size < 8)
					return ERR_INVALID_DATA;
				uint second_size = rd32 (sec.d + first_size + 4, ctx->be);
				if (second_size < 8 || first_size + second_size != sec.n)
					return ERR_INVALID_DATA;
				usd1offsets[i] = base + first_size;
			}
		}
		BFE (bf_buf_raw (&entrydata, sec.d, sec.n));
		bf_buf_free (&sec);
	}
	// pass 1b: extra data
	for (uint i = 0; i < num; i++)
	{
		const bf_node_t *en = entries->items[i].u.node;
		ccp extra = bf_get_str (en, "extra");
		if (extra)
		{
			extraoffsets[i] = 96 + 40 * num + entrydata.n + extradata.n;
			uint oldn = extradata.n;
			enumError err = bf_hex_decode (extra, &extradata);
			if (err)
			{
				FREE (dataoffsets);
				FREE (usd1offsets);
				FREE (extraoffsets);
				return err;
			}
			if (extradata.n - oldn < 48 && extradata.n & 3)
				BFE (bf_buf_pad (&extradata, 4 - (extradata.n & 3)));
		}
		else
			extraoffsets[i] = 0;
	}
	// pass 2: write records
	BFE (p_pane (&b, ctx, node));
	BFE (bf_buf_u32 (&b, ctx->be, num));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "section-scale-X", 0)));
	BFE (bf_buf_f32 (&b, ctx->be, (float)bf_get_float (node, "section-scale-Y", 0)));
	for (uint i = 0; i < num; i++)
	{
		const bf_node_t *en = entries->items[i].u.node;
		BFE (bf_buf_str (&b, bf_get_str (en, "name") ? bf_get_str (en, "name") : "", 24));
		BFE (bf_buf_u8 (&b, (u8)bf_get_int (en, "unknown-1", 0)));
		BFE (bf_buf_u8 (&b, (u8)bf_get_int (en, "flags", 0)));
		BFE (bf_buf_u16 (&b, ctx->be, 0));
		BFE (bf_buf_u32 (&b, ctx->be, dataoffsets[i]));
		BFE (bf_buf_u32 (&b, ctx->be, usd1offsets[i]));
		BFE (bf_buf_u32 (&b, ctx->be, extraoffsets[i]));
	}
	FREE (dataoffsets);
	FREE (usd1offsets);
	FREE (extraoffsets);
	if (entrydata.n & 3)
		BFE (bf_buf_pad (&entrydata, 4 - (entrydata.n & 3)));
	if (extradata.n & 3)
		BFE (bf_buf_pad (&extradata, 4 - (extradata.n & 3)));
	BFE (bf_buf_raw (&b, entrydata.d, entrydata.n));
	BFE (bf_buf_raw (&b, extradata.d, extradata.n));
	bf_val_t *dump = BFNodeGet ((bf_node_t *)node, "dump");
	if (dump && dump->type == BF_T_BYTES)
		BFE (bf_buf_raw (&b, dump->u.by.d, dump->u.by.n));
	enumError err = bf_buf_sechdr (out, ctx->be, "prt1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	bf_buf_free (&entrydata);
	bf_buf_free (&extradata);
	return err;
}

static enumError p_cnt1 (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v)
{
	if (v->type != BF_T_NODE)
		return ERR_INVALID_DATA;
	const bf_node_t *node = v->u.node;
	bf_buf_t b, sec1, sec2, sec3;
	memset (&b, 0, sizeof (b));
	memset (&sec1, 0, sizeof (sec1));
	memset (&sec2, 0, sizeof (sec2));
	memset (&sec3, 0, sizeof (sec3));
	uint partnum = 0, animnum = 0;
	bf_list_t *parts = bf_get_list (node, "parts");
	if (parts)
	{
		partnum = parts->n;
		for (uint i = 0; i < partnum; i++)
		{
			ccp s = parts->items[i].type == BF_T_STR ? parts->items[i].u.s : "";
			BFE (bf_buf_str (&sec1, s, 24));
		}
	}
	const bf_node_t *an = bf_get_node (node, "anim-part");
	if (an)
	{
		bf_list_t *anims = bf_get_list (an, "anims");
		animnum = anims ? anims->n : 0;
		ccp animname = bf_get_str (an, "name");
		if (!animname)
			animname = "";
		bf_buf_t anbuf;
		memset (&anbuf, 0, sizeof (anbuf));
		BFE (bf_buf_str4 (&anbuf, animname));
		BFE (bf_buf_u32 (&sec2, ctx->be, (u32)bf_get_int (an, "anim-part-number", (int)animnum)));
		BFE (bf_buf_u32 (&sec2, ctx->be, anbuf.n));
		BFE (bf_buf_raw (&sec2, anbuf.d, anbuf.n));
		bf_buf_free (&anbuf);
		if (animnum)
		{
			uint *offsets = (uint *)MALLOC (animnum * 4);
			if (!offsets)
			{
				bf_buf_free (&b);
				bf_buf_free (&sec1);
				bf_buf_free (&sec2);
				bf_buf_free (&sec3);
				return ERR_OUT_OF_MEMORY;
			}
			bf_buf_t names;
			memset (&names, 0, sizeof (names));
			offsets[0] = 4 * animnum;
			for (uint i = 0; i < animnum; i++)
			{
				ccp s = anims->items[i].type == BF_T_STR ? anims->items[i].u.s : "";
				if (i)
					offsets[i] = offsets[0] + names.n;
				BFE (bf_buf_str (&names, s, 0));
			}
			if (names.n & 3)
				BFE (bf_buf_pad (&names, 4 - (names.n & 3)));
			for (uint i = 0; i < animnum; i++)
				BFE (bf_buf_u32 (&sec3, ctx->be, offsets[i]));
			BFE (bf_buf_raw (&sec3, names.d, names.n));
			FREE (offsets);
			bf_buf_free (&names);
		}
	}
	bf_buf_t name;
	memset (&name, 0, sizeof (name));
	BFE (bf_buf_str4 (&name, bf_get_str (node, "name") ? bf_get_str (node, "name") : ""));
	uint nlen = name.n;
	uint offset1 = nlen + 28;
	uint offset2 = nlen * 2 + 28;
	uint offset3 = sec1.n + nlen * 2 + 28;
	uint offset4 = offset3 + sec2.n;
	BFE (bf_buf_u32 (&b, ctx->be, offset1));
	BFE (bf_buf_u32 (&b, ctx->be, offset2));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)partnum));
	BFE (bf_buf_u16 (&b, ctx->be, (u16)animnum));
	BFE (bf_buf_u32 (&b, ctx->be, offset3));
	BFE (bf_buf_u32 (&b, ctx->be, offset4));
	BFE (bf_buf_raw (&b, name.d, name.n));
	BFE (bf_buf_raw (&b, name.d, name.n));
	BFE (bf_buf_raw (&b, sec1.d, sec1.n));
	BFE (bf_buf_raw (&b, sec2.d, sec2.n));
	BFE (bf_buf_raw (&b, sec3.d, sec3.n));
	bf_val_t *dump = BFNodeGet ((bf_node_t *)node, "dump");
	if (dump && dump->type == BF_T_BYTES)
		BFE (bf_buf_raw (&b, dump->u.by.d, dump->u.by.n));
	bf_buf_free (&name);
	enumError err = bf_buf_sechdr (out, ctx->be, "cnt1", b.n);
	if (!err)
		err = bf_buf_raw (out, b.d, b.n);
	bf_buf_free (&b);
	bf_buf_free (&sec1);
	bf_buf_free (&sec2);
	bf_buf_free (&sec3);
	return err;
}

typedef enumError (*pack_func) (bf_pctx_t *ctx, bf_buf_t *out, const bf_val_t *v);

static pack_func pack_dispatch (ccp magic)
{
	switch (magic[0])
	{
		case 'l':
			if (!strcmp (magic, "lyt1"))
				return p_lyt1;
			break;
		case 't':
			if (!strcmp (magic, "txl1"))
				return p_txl1;
			if (!strcmp (magic, "txt1"))
				return p_txt1;
			break;
		case 'f':
			if (!strcmp (magic, "fnl1"))
				return p_fnl1;
			break;
		case 'm':
			if (!strcmp (magic, "mat1"))
				return p_mat1;
			break;
		case 'p':
			if (!strcmp (magic, "pan1"))
				return p_pan1;
			if (!strcmp (magic, "pic1"))
				return p_pic1;
			if (!strcmp (magic, "pas1"))
				return p_pas1;
			if (!strcmp (magic, "pae1"))
				return p_pae1;
			if (!strcmp (magic, "prt1"))
				return p_prt1;
			break;
		case 'w':
			if (!strcmp (magic, "wnd1"))
				return p_wnd1;
			break;
		case 'b':
			if (!strcmp (magic, "bnd1"))
				return p_bnd1;
			break;
		case 'g':
			if (!strcmp (magic, "grp1"))
				return p_grp1;
			if (!strcmp (magic, "grs1"))
				return p_grs1;
			if (!strcmp (magic, "gre1"))
				return p_gre1;
			break;
		case 'u':
			if (!strcmp (magic, "usd1"))
				return p_usd1;
			break;
		case 'c':
			if (!strcmp (magic, "cnt1"))
				return p_cnt1;
			break;
	}
	return 0;
}

static enumError repacktree (
	bf_pctx_t *ctx, const bf_node_t *tree, bf_buf_t *out, bool count_top, bool safe)
{
	for (uint i = 0; i < tree->n; i++)
	{
		ccp key = tree->kv[i].key;
		if (key[0] == '_' && key[1] == '_')
			continue;
		// magic = key up to first '-'
		ccp dash = strchr (key, '-');
		uint mlen = dash ? (uint)(dash - key) : strlen (key);
		char magic[32];
		if (mlen >= sizeof (magic))
			mlen = sizeof (magic) - 1;
		memcpy (magic, key, mlen);
		magic[mlen] = 0;
		if (tree->kv[i].val.type == BF_T_NODE)
		{
			bf_val_t *data_v = BFNodeGet (tree->kv[i].val.u.node, "data");
			if (data_v && data_v->type == BF_T_BYTES)
			{
				if (count_top)
					ctx->secnum++;
				BFE (bf_buf_raw (out, magic, 4));
				BFE (bf_buf_u32 (out, ctx->be, data_v->u.by.n + 8));
				if (data_v->u.by.n)
					BFE (bf_buf_raw (out, data_v->u.by.d, data_v->u.by.n));
				continue;
			}
		}
		pack_func pf = pack_dispatch (magic);
		if (!pf)
		{
			if (!safe)
			{
				ERROR0 (ERR_INVALID_DATA, "Invalid BFLYT section: %s\n", key);
				return ERR_INVALID_DATA;
			}
			continue;
		}
		if (count_top)
			ctx->secnum++;
		enumError err = pf (ctx, out, &tree->kv[i].val);
		if (err)
			return err;
	}
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			binary pack			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError build_binary (const bf_node_t *tree, u8 **dest, uint *dest_size)
{
	ccp byteorder = bf_get_str (tree, "byte-order");
	ccp magic = bf_get_str (tree, "magic");
	int version = bf_get_int (tree, "version", 1);
	const bf_node_t *bflyt_node = bf_get_node (tree, "BFLYT");
	if (!bflyt_node)
		return ERR_INVALID_DATA;
	if (!magic)
		magic = "FLYT";
	if (strlen (magic) != 4 || !byteorder || (byteorder[0] != '<' && byteorder[0] != '>'))
		return ERR_INVALID_DATA;
	bool be = (byteorder[0] == '>');
	if (version < 0 || version > 0xFFFF)
		return ERR_INVALID_DATA;

	bf_pctx_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	ctx.be = be;
	ctx.is_wiiu = (!strcmp (magic, "FLYT") || !strcmp (magic, "FLAN"));
	BFListInit (&ctx.textures);
	BFListInit (&ctx.fontnames);
	BFListInit (&ctx.matnames);

	bf_buf_t data;
	memset (&data, 0, sizeof (data));
	enumError err = repacktree (&ctx, bflyt_node, &data, true, false);
	if (err)
	{
		pctx_free (&ctx);
		bf_buf_free (&data);
		return err;
	}

	bf_buf_t hdr;
	memset (&hdr, 0, sizeof (hdr));
	BFE (bf_buf_raw (&hdr, magic, 4));
	if (be)
		BFE (bf_buf_raw (&hdr, "\xFE\xFF", 2));
	else
		BFE (bf_buf_raw (&hdr, "\xFF\xFE", 2));
	if (!strcmp (magic, "RLYT") || !strcmp (magic, "RLAN"))
	{
		BFE (bf_buf_u16 (&hdr, be, (u16)version));
		BFE (bf_buf_u32 (&hdr, be, data.n + 0x10));
		BFE (bf_buf_u16 (&hdr, be, 0x0010));
		BFE (bf_buf_u16 (&hdr, be, (u16)ctx.secnum));
	}
	else
	{
		BFE (bf_buf_u16 (&hdr, be, 0x14));
		BFE (bf_buf_u16 (&hdr, be, (u16)version));
		BFE (bf_buf_u16 (&hdr, be, 0x0702));
		BFE (bf_buf_u32 (&hdr, be, data.n + 0x14));
		BFE (bf_buf_u16 (&hdr, be, (u16)ctx.secnum));
		BFE (bf_buf_u16 (&hdr, be, 0));
	}

	u8 *out = (u8 *)MALLOC (hdr.n + data.n);
	if (!out)
	{
		pctx_free (&ctx);
		bf_buf_free (&hdr);
		bf_buf_free (&data);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (out, hdr.d, hdr.n);
	memcpy (out + hdr.n, data.d, data.n);
	*dest = out;
	*dest_size = hdr.n + data.n;
	pctx_free (&ctx);
	bf_buf_free (&hdr);
	bf_buf_free (&data);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			API			///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeBFLYT (bflyt_t *bflyt)
{
	memset (bflyt, 0, sizeof (*bflyt));
	BFNodeInit (&bflyt->tree);
}

void ResetBFLYT (bflyt_t *bflyt)
{
	if (!bflyt)
		return;
	BFNodeFree (&bflyt->tree);
	memset (bflyt, 0, sizeof (*bflyt));
	BFNodeInit (&bflyt->tree);
}

enumError ScanBFLYT (bflyt_t *bflyt, bool init, const u8 *data, uint data_size)
{
	if (init)
		InitializeBFLYT (bflyt);
	DASSERT (bflyt);
	if (data_size < 8)
		return ERR_INVALID_DATA;

	if (data_size >= 8 && !memcmp (data, "FZIP", 4))
	{
		u8 *dec = 0;
		uint dec_sz = 0;
		enumError derr = DecodeFZIP (&dec, &dec_sz, data, data_size);
		if (!derr && dec)
		{
			enumError ret = ScanBFLYT (bflyt, false, dec, dec_sz);
			FREE (dec);
			return ret;
		}
	}

	// binary magic?
	u32 fmagic = ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | (u32)data[3];
	if (fmagic == BFLYT_MAGIC_FLYT || fmagic == BCLYT_MAGIC_CLYT || fmagic == BFLYT_MAGIC_FLAN
		|| fmagic == BCLYT_MAGIC_CLAN || fmagic == BRLYT_MAGIC_RLYT || fmagic == BRLYT_MAGIC_RLAN)
		return parse_binary (bflyt, data, data_size);

	// txtree text?
	if (is_text_data (data, data_size))
	{
		enumError err = BFTreeLoad ((ccp)data, data_size, &bflyt->tree);
		if (err)
			return err;
		bf_val_t *magic_v = BFNodeGet (&bflyt->tree, "magic");
		ccp m = (magic_v && magic_v->type == BF_T_STR) ? magic_v->u.s : "FLYT";
		bflyt->magic = ((u32)m[0] << 24) | ((u32)m[1] << 16) | ((u32)m[2] << 8) | (u32)m[3];
		if (!strlen (m) || strlen (m) != 4)
			bflyt->magic = BFLYT_MAGIC_FLYT;
		return ERR_OK;
	}

	if (ErrorLogEnabled ())
		ERROR0 (ERR_INVALID_DATA, "No BFLYT/BCLYT/BFLAN/BCLAN file.\n");
	return ERR_INVALID_DATA;
}

enumError BuildBFLYT (const bflyt_t *bflyt, u8 **dest, uint *dest_size)
{
	DASSERT (bflyt);
	DASSERT (dest);
	DASSERT (dest_size);
	*dest = 0;
	*dest_size = 0;
	return build_binary (&bflyt->tree, dest, dest_size);
}

enumError SaveRawBFLYT (const bflyt_t *bflyt, ccp fname, bool set_time)
{
	DASSERT (bflyt);
	DASSERT (fname);
	u8 *data = 0;
	uint data_size = 0;
	enumError err = BuildBFLYT (bflyt, &data, &data_size);
	if (err)
		return err;
	DASSERT (data);
	DASSERT (data_size);

	File_t F;
	err = CreateFileOpt (&F, true, fname, testmode, fname);
	if (err > ERR_WARNING || !F.f)
		return err;

	if (fwrite (data, 1, data_size, F.f) != data_size)
		return FILEERROR1 (&F, ERR_WRITE_FAILED, "Write failed: %s\n", fname);
	FREE (data);
	return ResetFile (&F, set_time);
}

enumError SaveTextBFLYT (const bflyt_t *bflyt, ccp fname, bool set_time)
{
	DASSERT (bflyt);
	DASSERT (fname);
	char *text = BFTreeDump (&bflyt->tree);
	if (!text)
		return ERR_OUT_OF_MEMORY;

	// prepend the '#<magic>' comment line, e.g. '#FLYT'
	bf_val_t *magic_v = BFNodeGet ((bf_node_t *)&bflyt->tree, "magic");
	ccp m = (magic_v && magic_v->type == BF_T_STR) ? magic_v->u.s : "FLYT";
	uint text_size = 5 + strlen (text); // "#XXX\n" + text
	char *full = MALLOC (text_size + 1);
	if (!full)
	{
		FREE (text);
		return ERR_OUT_OF_MEMORY;
	}
	snprintf (full, text_size + 1, "#%.4s\n%s", m, text);
	FREE (text);

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, fname);
	if (err > ERR_WARNING || !F.f)
	{
		FREE (full);
		return err;
	}

	if (fwrite (full, 1, text_size, F.f) != text_size)
	{
		FREE (full);
		return FILEERROR1 (&F, ERR_WRITE_FAILED, "Write failed: %s\n", fname);
	}
	FREE (full);
	return ResetFile (&F, set_time);
}
