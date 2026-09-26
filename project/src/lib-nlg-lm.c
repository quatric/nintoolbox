// SPDX-License-Identifier: GPL-2.0+
// Next Level Games LM2 / LM3 / SANIM support -- see lib-nlg-lm.h.
// Wire layouts re-implemented from KillzXGaming/NextLevelLibrary (MIT).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C"
{
#endif
#include "types.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-szs.h"
#include "lib-zstd.h"
#include "lib-image.h"
#include "lib-archive-util.h"
#include "lib-model-glb.h"
#include "lib-ctpk.h"
#include "lib-bntx.h"
#include "lib-fedforce.h"
#include "lib-nlg-lm.h"
#include "astc/astc_wrapper.h"
#include "bcn-decoder/bcn_wrapper.h"
#include <zlib.h>
#ifdef __cplusplus
}
#endif

//-----------------------------------------------------------------------------
// NLG hash-name reverse table (generated wordlists).
//-----------------------------------------------------------------------------

#include "lib-nlg-names.inc"

ccp NLGHashName (u32 hash, char out[16])
{
	uint lo = 0, hi = sizeof (nlg_name_tab) / sizeof (nlg_name_tab[0]);
	while (lo < hi)
	{
		uint mid = lo + (hi - lo) / 2;
		if (nlg_name_tab[mid].hash == hash)
			return nlg_name_str[nlg_name_tab[mid].name];
		if (nlg_name_tab[mid].hash < hash)
			lo = mid + 1;
		else
			hi = mid;
	}
	// Fall back to the Federation Force built-in presets before hex.
	char hex[16];
	ccp fed = FedForceHashName (hash, hex);
	if (fed)
		return fed;
	if (out)
		snprintf (out, 16, "%X", hash);
	return 0;
}

//-----------------------------------------------------------------------------
// Variant detection + LM2/LM3 dictionary scans.
//
// Upstream tells LM3 apart by "BE u32 @12 == 0x78340300", but @12..@15 in
// its own LM3 layout are the numFiles/numChunkInfos/numStrings/pad bytes --
// that value is one retail file's block counts (120 files, 52 chunk infos,
// 3 strings), not a magic. Detect structurally instead: the parse whose
// counts all fit and whose strings all terminate wins. FedForce keeps its
// dedicated 0x297B947A@16 rule (lib-fedforce.c); Strikers-BLF keeps the
// existing @0x40 heuristic in lib-nlgdict.c.
//-----------------------------------------------------------------------------

static bool nlg_strings_fit (const u8 *dict, uint dict_size, size_t p, uint n_strings)
{
	for (uint i = 0; i < n_strings; i++)
	{
		if (p >= dict_size)
			return false;
		const u8 *nul = memchr (dict + p, 0, dict_size - p);
		if (!nul || (size_t)(nul - dict) - p > 256)
			return false;
		p += (size_t)(nul - dict) - p + 1;
	}
	return true;
}

enumError ScanLM3Dict (const u8 *dict, uint dict_size, nlg_block_t **blocks, uint *n_blocks,
	const char ***strings, uint *n_strings, bool *compressed)
{
	if (blocks)
		*blocks = 0;
	if (n_blocks)
		*n_blocks = 0;
	if (strings)
		*strings = 0;
	if (n_strings)
		*n_strings = 0;
	if (!dict || dict_size < 16)
		return EINVAL;
	// Identifier 0x5824F3A9 (either endianness; retail is BE).
	u32 m_be = rd_be32 (dict), m_le = rd_le32 (dict);
	if (m_be != 0x5824F3A9 && m_le != 0x5824F3A9)
		return ERR_NOTHING_TO_DO;
	bool comp = dict[6] == 1;
	uint n_files = dict[12], n_chunk = dict[13], n_str = dict[14];
	if (n_files == 0 || n_files > 100000 || n_str > 10000 || n_chunk > 10000)
		return EINVAL;
	size_t p = 16 + (size_t)n_chunk * 24;
	if (p + (size_t)n_files * 16 > dict_size)
		return EINVAL;
	nlg_block_t *bl = CALLOC (n_files ? n_files : 1, sizeof (*bl));
	if (!bl)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < n_files; i++)
	{
		const u8 *b = dict + p + (size_t)i * 16;
		bl[i].offset = rd_le32 (b);
		bl[i].decomp_size = rd_le32 (b + 4);
		bl[i].comp_size = rd_le32 (b + 8);
		bl[i].flags = rd_le32 (b + 12);
		bl[i].source_index = (u8)((bl[i].flags >> 16) & 0xFF);
		// Upstream LM3 rule: TABLE iff low byte is 0x03 with top byte 1.
		u8 lo = (u8)(bl[i].flags & 0xFF), hi = (u8)((bl[i].flags >> 24) & 0xFF);
		bl[i].source_type = (lo == 0x03 && hi == 1) ? 1 : (lo ? 2 : 0);
	}
	p += (size_t)n_files * 16;
	if (!nlg_strings_fit (dict, dict_size, p, n_str))
	{
		FREE (bl);
		return EINVAL;
	}
	const char **strs = 0;
	if (n_str)
	{
		strs = CALLOC (n_str, sizeof (*strs));
		if (!strs)
		{
			FREE (bl);
			return ERR_CANT_CREATE;
		}
		for (uint i = 0; i < n_str; i++)
		{
			const u8 *s = dict + p;
			size_t len = (size_t)((const u8 *)memchr (s, 0, dict_size - p) - s);
			char *cp = MALLOC (len + 1);
			if (!cp)
			{
				for (uint k = 0; k < i; k++)
					FREE ((void *)strs[k]);
				FREE (strs);
				FREE (bl);
				return ERR_CANT_CREATE;
			}
			memcpy (cp, s, len);
			cp[len] = 0;
			strs[i] = cp;
			p += len + 1;
		}
	}
	if (blocks)
		*blocks = bl;
	else
		FREE (bl);
	if (n_blocks)
		*n_blocks = n_files;
	if (strings)
		*strings = strs;
	else if (strs)
	{
		for (uint i = 0; i < n_str; i++)
			FREE ((void *)strs[i]);
		FREE (strs);
	}
	if (n_strings)
		*n_strings = n_str;
	if (compressed)
		*compressed = comp;
	return ERR_OK;
}

enumError ScanLM2Dict (const u8 *dict, uint dict_size, nlg_block_t **blocks, uint *n_blocks,
	const char ***strings, uint *n_strings, bool *compressed)
{
	if (blocks)
		*blocks = 0;
	if (n_blocks)
		*n_blocks = 0;
	if (strings)
		*strings = 0;
	if (n_strings)
		*n_strings = 0;
	if (!dict || dict_size < 20)
		return EINVAL;
	u32 m_be = rd_be32 (dict), m_le = rd_le32 (dict);
	if (m_be != 0x5824F3A9 && m_le != 0x5824F3A9)
		return ERR_NOTHING_TO_DO;
	bool comp = dict[6] == 1;
	u32 n_files = rd_le32 (dict + 8);
	if (n_files == 0 || n_files > 100000)
		return EINVAL;
	uint n_chunk = dict[18], n_str = dict[19];
	if (n_str > 10000 || n_chunk > 10000)
		return EINVAL;
	// Header 20 bytes, ChunkInfos 12 bytes each, Unknowns numFiles bytes.
	size_t p = 20 + (size_t)n_chunk * 12 + n_files;
	if (p + (size_t)n_files * 16 > dict_size)
		return EINVAL;
	nlg_block_t *bl = CALLOC (n_files, sizeof (*bl));
	if (!bl)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < n_files; i++)
	{
		const u8 *b = dict + p + (size_t)i * 16;
		bl[i].offset = rd_le32 (b);
		bl[i].decomp_size = rd_le32 (b + 4);
		bl[i].comp_size = rd_le32 (b + 8);
		bl[i].flags = rd_le32 (b + 12);
		bl[i].source_index = (u8)((bl[i].flags >> 16) & 0xFF);
		// Upstream LM2 rule: TABLE iff low byte is 0x08 with top byte 1.
		u8 lo = (u8)(bl[i].flags & 0xFF), hi = (u8)((bl[i].flags >> 24) & 0xFF);
		bl[i].source_type = (lo == 0x08 && hi == 1) ? 1 : (lo ? 2 : 0);
	}
	p += (size_t)n_files * 16;
	if (!nlg_strings_fit (dict, dict_size, p, n_str))
	{
		FREE (bl);
		return EINVAL;
	}
	const char **strs = 0;
	if (n_str)
	{
		strs = CALLOC (n_str ? n_str : 1, sizeof (*strs));
		if (!strs)
		{
			FREE (bl);
			return ERR_CANT_CREATE;
		}
		for (uint i = 0; i < n_str; i++)
		{
			const u8 *s = dict + p;
			size_t len = (size_t)((const u8 *)memchr (s, 0, dict_size - p) - s);
			char *cp = MALLOC (len + 1);
			if (!cp)
			{
				for (uint k = 0; k < i; k++)
					FREE ((void *)strs[k]);
				FREE (strs);
				FREE (bl);
				return ERR_CANT_CREATE;
			}
			memcpy (cp, s, len);
			cp[len] = 0;
			strs[i] = cp;
			p += len + 1;
		}
	}
	if (blocks)
		*blocks = bl;
	else
		FREE (bl);
	if (n_blocks)
		*n_blocks = n_files;
	if (strings)
		*strings = strs;
	else if (strs)
	{
		for (uint i = 0; i < n_str; i++)
			FREE ((void *)strs[i]);
		FREE (strs);
	}
	if (n_strings)
		*n_strings = n_str;
	if (compressed)
		*compressed = comp;
	return ERR_OK;
}

void FreeNLGDict (nlg_block_t *blocks, const char **strings, uint n_strings)
{
	if (blocks)
		FREE (blocks);
	if (strings)
	{
		for (uint i = 0; i < n_strings; i++)
			FREE ((void *)strings[i]);
		FREE (strings);
	}
}

nlg_variant_t NLGDetectVariant (const u8 *dict, uint dict_size)
{
	if (!dict || dict_size < 20)
		return NLG_UNKNOWN;
	u32 m_be = rd_be32 (dict), m_le = rd_le32 (dict);
	if (m_be != 0x5824F3A9 && m_le != 0x5824F3A9)
		return NLG_UNKNOWN;
	// Federation Force first: its @16 rule is exact either endianness.
	if (dict_size >= 20 && (rd_be32 (dict + 16) == 0x297B947A || rd_le32 (dict + 16) == 0x297B947A))
		return NLG_FEDFORCE;
	// LM3's header is shorter with byte counts; it validates strictly.
	// Try it before LM2 so a small LM3 file is not misread as LM2.
	if (ScanLM3Dict (dict, dict_size, 0, 0, 0, 0, 0) == ERR_OK)
		return NLG_LM3;
	if (ScanLM2Dict (dict, dict_size, 0, 0, 0, 0, 0) == ERR_OK)
		return NLG_LM2;
	return NLG_UNKNOWN;
}

//-----------------------------------------------------------------------------
// Block decoding (zlib / zstd sniff, like ExtractNLGDictArchive) + chunk scan.
//-----------------------------------------------------------------------------

#define NLG_MAX_BLOCK (512u << 20)

static u8 *nlg_block_decode (
	const u8 *src, size_t avail, u32 comp_size, u32 decomp_size, bool is_compressed, uint *out_size)
{
	if (out_size)
		*out_size = 0;
	if (!src || !avail || !decomp_size || decomp_size > NLG_MAX_BLOCK)
		return 0;
	if (!is_compressed || !comp_size || comp_size > avail)
	{
		if (decomp_size > avail)
			return 0;
		u8 *out = MALLOC (decomp_size ? decomp_size : 1);
		if (!out)
			return 0;
		memcpy (out, src, decomp_size);
		if (out_size)
			*out_size = decomp_size;
		return out;
	}
	if (comp_size >= 4 && IsZSTD (src, comp_size) > 0)
	{
		u8 *dec = MALLOC (decomp_size);
		if (dec)
		{
			uint written = 0;
			if (DecodeZSTDpart (dec, decomp_size, &written, src, comp_size) == ERR_OK)
			{
				if (out_size)
					*out_size = written ? written : decomp_size;
				return dec;
			}
			FREE (dec);
		}
	}
	if (comp_size >= 2 && src[0] == 0x78
		&& (src[1] == 0x9c || src[1] == 0xda || src[1] == 0x01 || src[1] == 0x5e))
	{
		u8 *dec = MALLOC (decomp_size);
		if (dec)
		{
			uLongf dl = decomp_size;
			if (uncompress (dec, &dl, src, comp_size) == Z_OK)
			{
				if (out_size)
					*out_size = (uint)dl;
				return dec;
			}
			FREE (dec);
		}
	}
	return 0;
}

enumError ScanNLGChunks (nlg_chunk_t **chunks, uint *n, const u8 *data, uint size)
{
	if (chunks)
		*chunks = 0;
	if (n)
		*n = 0;
	if (!chunks || !n || !data || size < 12 || size % 12 != 0)
		return EINVAL;
	uint count = size / 12;
	if (count == 0 || count > 100000)
		return EINVAL;
	nlg_chunk_t *out = CALLOC (count, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = data + (size_t)i * 12;
		out[i].type = rd_le16 (e);
		out[i].flags = rd_le16 (e + 2);
		out[i].size = rd_le32 (e + 4);
		out[i].offset = rd_le32 (e + 8);
		out[i].is_file = (out[i].type == 0x1301);
	}
	*chunks = out;
	*n = count;
	return ERR_OK;
}

typedef struct
{
	u8 *data;
	uint size;
} nlg_buf_t;

static void nlg_free_bufs (nlg_buf_t *bufs, uint n)
{
	if (!bufs)
		return;
	for (uint i = 0; i < n; i++)
		FREE (bufs[i].data);
	FREE (bufs);
}

// Decode block BI of BL into BUFS[POS]. Missing/out-of-range blocks stay empty.
static void nlg_load_buf (nlg_buf_t *bufs, uint pos, const nlg_block_t *bl, uint n_blocks,
	const u8 *data_raw, size_t data_raw_size, uint bi, bool is_compressed)
{
	if (!bufs || bi >= n_blocks)
		return;
	if (!data_raw || !data_raw_size || bl[bi].offset >= data_raw_size)
		return;
	if (!bl[bi].decomp_size)
		return;
	uint got = 0;
	u8 *dec = nlg_block_decode (data_raw + bl[bi].offset, data_raw_size - bl[bi].offset,
		bl[bi].comp_size, bl[bi].decomp_size, is_compressed, &got);
	if (dec && got)
	{
		FREE (bufs[pos].data);
		bufs[pos].data = dec;
		bufs[pos].size = got;
	}
}

// Slice [OFF, OFF+SIZE) of BUFS[POS]; NULL when out of range.
static const u8 *nlg_slice (const nlg_buf_t *bufs, uint n_bufs, uint pos, u32 off, u32 size)
{
	if (!bufs || pos >= n_bufs || !bufs[pos].data || !size)
		return 0;
	if ((u64)off + size > bufs[pos].size)
		return 0;
	return bufs[pos].data + off;
}

// LM3 file block positions (DATA_Parser: indices into the dict block list).
static const uint lm3_want[] = { 0, 52, 53, 54, 55, 58, 63, 64, 65, 68, 69 };

// LM3 leaf/chunk buffer by chunk flags (ChunkBlockFlags + TextureData rule).
static uint lm3_buf_for_flags (u16 type, u16 flags)
{
	static const struct
	{
		uint flag, pos;
	} map[] = {
		{ 128, 52 },
		{ 257, 52 },
		{ 129, 52 },
		{ 192, 53 },
		{ 193, 52 },
		{ 194, 53 },
		{ 2241, 53 },
		{ 2242, 53 },
		{ 2264, 53 },
		{ 2305, 53 },
		{ 4353, 54 },
		{ 6721, 55 },
		{ 8386, 68 },
		{ 14530, 63 },
		{ 14528, 63 },
		{ 14529, 63 },
		{ 16577, 64 },
		{ 16641, 64 },
		{ 16642, 64 },
		{ 16578, 64 },
		{ 21057, 65 },
		{ 32961, 65 },
		{ 35073, 65 },
		{ 35009, 65 },
	};
	if (type == 0xB502)
		return 65;
	uint a201 = type;
	if (a201 >= 0xA201 && a201 <= 0xA207)
		return 64;
	for (uint i = 0; i < sizeof (map) / sizeof (map[0]); i++)
		if (map[i].flag == flags)
			return map[i].pos;
	return 0;
}

// LM3 file-header parse stream by file type (DATA_Parser fileTableBlock/2).
static uint lm3_head_buf (u16 type)
{
	if (type == 0xB500 || type == 0x5000 || type == 0xE000 || type == 0x6510)
		return 63;
	return 52;
}

// LM3 leaf buffer by file type for childless files.
static uint lm3_leaf_buf (u16 type)
{
	if (type == 0x0001)
		return 58;
	if (type == 0x7200)
		return 54;
	if (type == 0xF000 || type == 0x7020)
		return 69;
	if (type == 0x4300)
		return 53;
	return 53;
}

//-----------------------------------------------------------------------------
// Resolved logical file: header pair + children or leaf payload.
//-----------------------------------------------------------------------------

typedef struct nlg_file_t
{
	u16 type;
	u32 hash_type, path_hash;
	bool has_children;
	// leaf payload (valid when !has_children)
	const u8 *data;
	uint data_size;
	// children (valid when has_children): indices into the chunk table
	uint child_start, child_count;
} nlg_file_t;

// Chunk types that are pure containers (their own payload is meaningless;
// upstream links their SubData instead). Never resolved as leaf data.
static bool nlg_is_container_type (u16 type)
{
	return type == 0xB100 || type == 0xC800 || type == 0x6200 || type == 0x6500;
}

// File-entry parent test: Federation Force uses bit 15; LM2/LM3 inherit
// the reference rule (flags>>12)>2 over the file-table entries.
static bool nlg_file_has_children (nlg_variant_t variant, u16 flags)
{
	if (variant == NLG_FEDFORCE)
		return ((flags >> 15) & 1) != 0;
	return (flags >> 12) > 2;
}

// Child payload resolver per variant. Returns NULL when unavailable.
static const u8 *nlg_child_data (nlg_variant_t variant, const nlg_chunk_t *tab, uint ti,
	const nlg_buf_t *bufs, uint n_bufs, uint *size)
{
	if (size)
		*size = 0;
	if (!tab || !bufs)
		return 0;
	u16 type = tab[ti].type;
	u16 flags = tab[ti].flags;
	u32 sz = tab[ti].size, off = tab[ti].offset;
	if (nlg_is_container_type (type))
		return 0;
	const u8 *d = 0;
	if (variant == NLG_LM3)
	{
		uint pos = lm3_buf_for_flags (type, flags);
		// Position index into bufs[]: bufs array is indexed by block
		// position directly (sparse, sized 70).
		d = nlg_slice (bufs, n_bufs, pos, off, sz);
	}
	else // LM2 + FedForce share the 3-bit block-index rule
	{
		uint bi = NLGChunkBlockIndex (flags);
		if (variant == NLG_LM2)
			d = bi < 2 ? nlg_slice (bufs, n_bufs, bi == 0 ? 2 : 3, off, sz) : 0;
		else
			d = nlg_slice (bufs, n_bufs, bi, off, sz);
	}
	if (d && size)
		*size = sz;
	return d;
}

// Find the first child of TYPE in [START, START+COUNT), resolve payload.
static const u8 *nlg_find_child (nlg_variant_t variant, const nlg_chunk_t *tab, uint n_tab,
	uint start, uint count, u16 type, const nlg_buf_t *bufs, uint n_bufs, uint *size)
{
	if (size)
		*size = 0;
	for (uint c = 0; c < count; c++)
	{
		uint ti = start + c;
		if (ti >= n_tab)
			continue;
		if (nlg_is_container_type (tab[ti].type))
			continue;
		if (tab[ti].type != type)
			continue;
		return nlg_child_data (variant, tab, ti, bufs, n_bufs, size);
	}
	return 0;
}

static void nlg_base_name (char *dst, size_t cap, u32 path_hash, uint idx)
{
	char hex[16], hx2[16];
	ccp nm = NLGHashName (path_hash, hex);
	if (nm && nm != hex)
	{
		// Sanitize path separators from wordlist entries.
		char clean[128];
		size_t n = 0;
		for (ccp p = nm; *p && n + 1 < sizeof (clean); p++)
			clean[n++] = (*p == '/' || *p == '\\') ? '_' : *p;
		clean[n] = 0;
		snprintf (dst, cap, "%s_%08X", clean, path_hash);
	}
	else
	{
		(void)hx2;
		snprintf (dst, cap, "nlg_%08X_%u", path_hash, idx);
	}
}

//-----------------------------------------------------------------------------
// FEDM / FEDS / FEDT containers, versions 1..3.
//
// Same TLV shape lib-fedforce.c defines ("FEDM" u16le ver u16le nchunks,
// then nchunks x u16le type + u16le 0 + u32le size, then payloads; FEDT is
// a fixed 24-byte header + pixels). Version 1 is Federation Force,
// 2 is LM2, 3 is LM3; only the payload *interpretation* differs.
//-----------------------------------------------------------------------------

typedef struct
{
	u16 type;
	const u8 *data;
	u32 size;
} nlg_part_t;

static enumError nlg_parse_container (
	const u8 *data, size_t size, const char magic[4], nlg_part_t **parts, uint *n_parts, u16 *ver)
{
	if (parts)
		*parts = 0;
	if (n_parts)
		*n_parts = 0;
	if (ver)
		*ver = 0;
	if (!data || size < 8 || memcmp (data, magic, 4))
		return EINVAL;
	u16 v = rd_le16 (data + 4);
	if (v < 1 || v > 3)
		return EINVAL;
	uint n = rd_le16 (data + 6);
	if (n == 0 || n > 4096 || 8 + (size_t)n * 8 > size)
		return EINVAL;
	nlg_part_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	size_t p = 8 + (size_t)n * 8;
	for (uint i = 0; i < n; i++)
	{
		u16 ty = rd_le16 (data + 8 + (size_t)i * 8);
		u32 sz = rd_le32 (data + 8 + (size_t)i * 8 + 4);
		if ((u64)p + sz > size)
		{
			FREE (out);
			return EINVAL;
		}
		out[i].type = ty;
		out[i].data = data + p;
		out[i].size = sz;
		p += sz;
	}
	if (parts)
		*parts = out;
	else
		FREE (out);
	if (n_parts)
		*n_parts = n;
	if (ver)
		*ver = v;
	return ERR_OK;
}

static const nlg_part_t *nlg_find_part (const nlg_part_t *parts, uint n, u16 type)
{
	for (uint i = 0; i < n; i++)
		if (parts[i].type == type)
			return &parts[i];
	return 0;
}

static enumError nlg_build_container (u8 **dest, uint *dest_size, const char magic[4], u16 ver,
	const u16 *types, const u8 **datas, const uint *sizes, uint n)
{
	if (!dest || !dest_size || !types || !datas || !sizes || !n)
		return EINVAL;
	size_t total = 0;
	for (uint i = 0; i < n; i++)
		total += sizes[i];
	size_t hdr = 8 + (size_t)n * 8;
	if (hdr + total > NLG_MAX_BLOCK)
		return EFBIG;
	u8 *out = MALLOC (hdr + total);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, magic, 4);
	wr_le16 (out + 4, ver);
	wr_le16 (out + 6, (u16)n);
	size_t dp = hdr;
	for (uint i = 0; i < n; i++)
	{
		wr_le16 (out + 8 + (size_t)i * 8, types[i]);
		wr_le16 (out + 8 + (size_t)i * 8 + 2, 0);
		wr_le32 (out + 8 + (size_t)i * 8 + 4, sizes[i]);
		memcpy (out + dp, datas[i], sizes[i]);
		dp += sizes[i];
	}
	*dest = out;
	*dest_size = (uint)(hdr + total);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// LM2 vertex layouts (VertexLoaderExtension): hash -> stride + field reader.
// Position/UV scales match the reference; normals are s8/127 renormalized
// by the caller. Layout 6 (geometry-shader) carries no CPU vertices.
//-----------------------------------------------------------------------------

static uint nlg_lm2_stride (u32 hash)
{
	switch (hash)
	{
		case 0xC88C1762u:
		case 0x72D28D0Du:
			return 0x46;
		case 1879155460u:
			return 0x4A;
		case 170572476u:
			return 0x24;
		case 0x11E26127u:
		case 0x679BEB7Cu:
		case 0x8980687Fu:
		case 0xB3F4492Eu:
			return 0x16;
		case 0x0FFA5BDEu:
			return 0x1A;
		case 0x4821B2DFu:
		case 3695673818u:
		case 3767596423u:
		case 2860802050u:
		case 1968188335u:
			return 0x14;
		case 0x5576A693u:
			return 0x10;
		case 0xDF24890Du:
		case 0xF4F13EB1u:
		case 0xFADABB22u:
		case 0x87C2B716u:
		case 0x333626D9u:
			return 0x18;
		case 0x1090E6EBu:
		case 0xA856FBF7u:
		case 0xF07FC596u:
		case 0xDDCC31B7u:
		case 3276728684u:
		case 2808796972u:
			return 0x1C;
		case 3483481649u:
			return 0x0C;
		default:
			return 0;
	}
}

static float nlg_s16_ushort_decode (s16 v)
{
	u16 u = (u16)v;
	return (s8)(u >> 8) + (float)(u & 0xFF) / 256.0f;
}

typedef struct
{
	float px, py, pz, nx, ny, nz, u0, v0, u1, v1;
	float r, g, b, a;
	bool has_n, has_uv0, has_uv1, has_c;
} nlg_vert_t;

// Returns bytes consumed, 0 when the layout is unsupported.
static uint nlg_lm2_decode_vert (nlg_vert_t *v, const u8 *p, uint avail, u32 hash)
{
	uint stride = nlg_lm2_stride (hash);
	if (!v || !p || !stride || avail < stride)
		return 0;
	memset (v, 0, sizeof (*v));
	v->r = v->g = v->b = v->a = 1.0f;
	switch (hash)
	{
		// Layout 1: skinned short-pos + s8 normal + uv + bone bytes/weights.
		case 0xC88C1762u:
		case 0x72D28D0Du:
		case 0x11E26127u:
		case 0x679BEB7Cu:
		case 0x8980687Fu:
		case 0xB3F4492Eu:
		case 0x0FFA5BDEu:
		case 1879155460u:
			v->px = nlg_s16_ushort_decode ((s16)rd_le16 (p));
			v->py = nlg_s16_ushort_decode ((s16)rd_le16 (p + 2));
			v->pz = nlg_s16_ushort_decode ((s16)rd_le16 (p + 4));
			v->nx = (s8)p[6] / 255.0f;
			v->ny = (s8)p[7] / 255.0f;
			v->nz = (s8)p[8] / 255.0f;
			v->has_n = true;
			v->u0 = (s16)rd_le16 (p + 10) / 1024.0f;
			v->v0 = (s16)rd_le16 (p + 12) / 1024.0f;
			v->has_uv0 = true;
			return stride;
		// Layout 2: float pos + 2x uv + optional color.
		case 0x333626D9u:
		case 3767596423u:
		case 2860802050u:
		case 1968188335u:
			memcpy (&v->px, p, 4);
			memcpy (&v->py, p + 4, 4);
			memcpy (&v->pz, p + 8, 4);
			v->u0 = (s16)rd_le16 (p + 12) / 1024.0f;
			v->v0 = (s16)rd_le16 (p + 14) / 1024.0f;
			v->u1 = (s16)rd_le16 (p + 16) / 1024.0f;
			v->v1 = (s16)rd_le16 (p + 18) / 1024.0f;
			v->has_uv0 = v->has_uv1 = true;
			if (stride > 20)
			{
				v->r = p[20] / 255.0f;
				v->g = p[21] / 255.0f;
				v->b = p[22] / 255.0f;
				v->a = p[23] / 255.0f;
				v->has_c = true;
			}
			return stride;
		// Layout 3/5: float pos + s8 normal + 2x uv + optional color.
		case 3695673818u:
		case 0x5576A693u:
		case 0xDF24890Du:
		case 0xF4F13EB1u:
		case 0xFADABB22u:
		case 0x87C2B716u:
		case 0x1090E6EBu:
		case 0xA856FBF7u:
		case 0xF07FC596u:
		case 0xDDCC31B7u:
		case 3276728684u:
		case 2808796972u:
		case 170572476u:
			memcpy (&v->px, p, 4);
			memcpy (&v->py, p + 4, 4);
			memcpy (&v->pz, p + 8, 4);
			v->nx = (s8)p[12] / 255.0f;
			v->ny = (s8)p[13] / 255.0f;
			v->nz = (s8)p[14] / 255.0f;
			v->has_n = true;
			v->u0 = (s16)rd_le16 (p + 16) / 1024.0f;
			v->v0 = (s16)rd_le16 (p + 18) / 1024.0f;
			v->u1 = (s16)rd_le16 (p + 20) / 1024.0f;
			v->v1 = (s16)rd_le16 (p + 22) / 1024.0f;
			v->has_uv0 = v->has_uv1 = true;
			if (stride >= 0x1C && hash != 170572476u)
			{
				v->r = p[0x1C] / 255.0f;
				v->g = p[0x1D] / 255.0f;
				v->b = p[0x1E] / 255.0f;
				v->a = p[0x1F] / 255.0f;
				v->has_c = true;
			}
			return stride;
		// Layout 4: position only.
		case 0x4821B2DFu:
			memcpy (&v->px, p, 4);
			memcpy (&v->py, p + 4, 4);
			memcpy (&v->pz, p + 8, 4);
			return stride;
		default:
			return 0;
	}
}

/*__APPEND3__*/

//-----------------------------------------------------------------------------
// model_t construction helpers.
//-----------------------------------------------------------------------------

typedef struct
{
	vec3_t *pos;
	vec3_t *nrm;
	vec2_t *uv0;
	vec2_t *uv1;
	color4_t *col;
	vec3_t *tan;
	uint n_pos, n_nrm, n_uv0, n_uv1, n_col, n_tan;
	uint cap_pos, cap_nrm, cap_uv0, cap_uv1, cap_col, cap_tan;
	vertex_t *tri;
	size_t n_tri, cap_tri;
} nlg_geo_t;

static void nlg_geo_free (nlg_geo_t *g)
{
	FREE (g->pos);
	FREE (g->nrm);
	FREE (g->uv0);
	FREE (g->uv1);
	FREE (g->col);
	FREE (g->tan);
	FREE (g->tri);
	memset (g, 0, sizeof (*g));
}

#define NLG_GEO_GROW(f, n, cap, T)                                                                 \
	do                                                                                             \
	{                                                                                              \
		if ((n) >= (cap))                                                                          \
		{                                                                                          \
			uint nc = (cap) ? (cap) * 2 : 64;                                                      \
			T *nn = REALLOC (g->f, nc * sizeof (*nn));                                             \
			if (!nn)                                                                               \
				return false;                                                                      \
			g->f = nn;                                                                             \
			(cap) = nc;                                                                            \
		}                                                                                          \
	} while (0)

static bool nlg_geo_vert (nlg_geo_t *g, const nlg_vert_t *v, float m[16], bool use_m)
{
	// Position through the model matrix (row-vector convention, like the
	// reference) then the repo-standard -90 deg X rotation into Y-up.
	float x = v->px, y = v->py, z = v->pz;
	if (use_m)
	{
		float nx = x * m[0] + y * m[4] + z * m[8] + m[12];
		float ny = x * m[1] + y * m[5] + z * m[9] + m[13];
		float nz = x * m[2] + y * m[6] + z * m[10] + m[14];
		x = nx;
		y = ny;
		z = nz;
	}
	if (!isfinite (x) || !isfinite (y) || !isfinite (z))
	{
		x = y = z = 0.0f;
	}
	NLG_GEO_GROW (pos, g->n_pos, g->cap_pos, vec3_t);
	NLG_GEO_GROW (tri, g->n_tri, g->cap_tri, vertex_t);
	g->pos[g->n_pos].x = x;
	g->pos[g->n_pos].y = z;
	g->pos[g->n_pos].z = -y;
	vertex_t *vx = &g->tri[g->n_tri];
	vx->position_idx = (int)g->n_pos;
	vx->matrix_idx = -1;
	vx->texcoord_idx = vx->normal_idx = vx->tangent_idx = -1;
	vx->color_idx[0] = vx->color_idx[1] = -1;
	for (int e = 0; e < 7; e++)
		vx->extra_texcoord_idx[e] = -1;
	g->n_pos++;
	if (v->has_n)
	{
		float nl = sqrtf (v->nx * v->nx + v->ny * v->ny + v->nz * v->nz);
		float qx = v->nx, qy = v->ny, qz = v->nz;
		if (nl > 1e-6f && isfinite (nl))
		{
			qx /= nl;
			qy /= nl;
			qz /= nl;
		}
		else
		{
			qx = 0.0f;
			qy = 1.0f;
			qz = 0.0f;
		}
		NLG_GEO_GROW (nrm, g->n_nrm, g->cap_nrm, vec3_t);
		g->nrm[g->n_nrm].x = qx;
		g->nrm[g->n_nrm].y = qz;
		g->nrm[g->n_nrm].z = -qy;
		vx->normal_idx = (int)g->n_nrm;
		g->n_nrm++;
	}
	if (v->has_uv0)
	{
		NLG_GEO_GROW (uv0, g->n_uv0, g->cap_uv0, vec2_t);
		g->uv0[g->n_uv0].u = v->u0;
		g->uv0[g->n_uv0].v = v->v0;
		vx->texcoord_idx = (int)g->n_uv0;
		g->n_uv0++;
	}
	if (v->has_uv1)
	{
		NLG_GEO_GROW (uv1, g->n_uv1, g->cap_uv1, vec2_t);
		g->uv1[g->n_uv1].u = v->u1;
		g->uv1[g->n_uv1].v = v->v1;
		vx->extra_texcoord_idx[0] = (int)g->n_uv1;
		g->n_uv1++;
	}
	if (v->has_c)
	{
		NLG_GEO_GROW (col, g->n_col, g->cap_col, color4_t);
		g->col[g->n_col].r = v->r;
		g->col[g->n_col].g = v->g;
		g->col[g->n_col].b = v->b;
		g->col[g->n_col].a = v->a;
		vx->color_idx[0] = (int)g->n_col;
		g->n_col++;
	}
	g->n_tri++;
	return true;
}

// Commit one mesh + material into the model arrays.
static bool nlg_commit_mesh (model_t *model, nlg_geo_t *g, ccp mesh_name, ccp mat_name, ccp tex_png)
{
	if (!g->n_tri)
		return true; // nothing (valid) to emit; not fatal
	g->n_tri = g->n_tri / 3 * 3;
	if (!g->n_tri)
		return true;
	mesh_t *nmeshes = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*nmeshes));
	material_t *nmats = REALLOC (model->materials, (model->num_materials + 1) * sizeof (*nmats));
	if (!nmeshes || !nmats)
	{
		if (nmeshes)
			model->meshes = nmeshes;
		if (nmats)
			model->materials = nmats;
		return false;
	}
	model->meshes = nmeshes;
	model->materials = nmats;
	mesh_t *dm = &model->meshes[model->num_meshes];
	material_t *mt = &model->materials[model->num_materials];
	memset (dm, 0, sizeof (*dm));
	memset (mt, 0, sizeof (*mt));
	snprintf (dm->name, sizeof (dm->name), "%s", mesh_name);
	snprintf (mt->name, sizeof (mt->name), "%s", mat_name);
	dm->positions = g->pos;
	dm->num_positions = g->n_pos;
	dm->position_node = CALLOC (g->n_pos ? g->n_pos : 1, sizeof (int));
	if (g->n_pos && !dm->position_node)
		return false;
	for (uint i = 0; i < g->n_pos; i++)
		dm->position_node[i] = -1;
	dm->normals = g->nrm;
	dm->num_normals = g->n_nrm;
	dm->texcoords = g->uv0;
	dm->num_texcoords = g->n_uv0;
	if (g->n_uv1)
	{
		dm->extra_texcoords[0] = g->uv1;
		dm->num_extra_texcoords[0] = g->n_uv1;
		g->uv1 = 0;
	}
	if (g->n_col)
	{
		dm->colors[0] = g->col;
		dm->num_colors[0] = g->n_col;
		g->col = 0;
	}
	dm->vertices = g->tri;
	dm->num_vertices = g->n_tri;
	dm->material_idx = (int)model->num_materials;
	g->pos = g->nrm = 0;
	g->uv0 = 0;
	g->tri = 0;
	memset (g, 0, sizeof (*g));
	mt->num_textures = 0;
	if (tex_png && tex_png[0])
	{
		snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s", tex_png);
		mt->num_textures = 1;
		mt->texture_coord[0] = 0;
		mt->wrap_s[0] = mt->wrap_t[0] = 1;
		mt->min_filter[0] = mt->mag_filter[0] = 1;
	}
	model->num_meshes++;
	model->num_materials++;
	return true;
}

static void nlg_quat_to_euler (
	float qx, float qy, float qz, float qw, float *rx, float *ry, float *rz)
{
	float sinr = 2.0f * (qw * qx + qy * qz);
	float cosr = 1.0f - 2.0f * (qx * qx + qy * qy);
	float roll = atan2f (sinr, cosr);
	float sinp = 2.0f * (qw * qy - qz * qx);
	float pitch = (fabsf (sinp) >= 1.0f) ? copysignf (1.57079632679f, sinp) : asinf (sinp);
	float siny = 2.0f * (qw * qz + qx * qy);
	float cosy = 1.0f - 2.0f * (qy * qy + qz * qz);
	*rx = roll;
	*ry = pitch;
	*rz = atan2f (siny, cosy);
}

/*__APPEND4__*/

//-----------------------------------------------------------------------------
// LM2 models: B002 model headers (16B), B003 meshes (0x28), B004 vertex
// pointers, B005 index/vertex buffers, B006 materials, B007 material LUT.
//-----------------------------------------------------------------------------

// LM2 material preset -> material-blob pointer index (MaterialLoaderHelper).
static int nlg_lm2_tex_slot (ccp preset)
{
	static const struct
	{
		ccp name;
		int slot;
	} tab[] = {
		{ "morphluigimaterial/default", 11 },
		{ "windowmaterial/default", 9 },
		{ "luigieyematerial/default", 7 },
		{ "luigimaterial/default", 7 },
		{ "environmentmaterial/default", 6 },
		{ "environmentspecularmaterial/default", 6 },
		{ "morphghostmaterial/default", 6 },
		{ "pestmaterial/default", 6 },
		{ "ghostmaterial/default", 6 },
		{ "environmentspecularrigidskin/default", 7 },
		{ "uvslidingmaterial/default", 4 },
		{ "uvslidingmaterialgs/default", 3 },
		{ "diffuseskin/depthshell", 3 },
		{ "diffusevertcolor/default", 2 },
	};
	for (uint i = 0; i < sizeof (tab) / sizeof (tab[0]); i++)
		if (!strcmp (preset, tab[i].name))
			return tab[i].slot;
	return -1;
}

static float nlg_f32 (const u8 *p)
{
	float v;
	memcpy (&v, p, 4);
	return v;
}

static bool nlg_tex_known (const nlg_texset_t *ts, u32 h)
{
	if (!ts)
		return false;
	for (uint i = 0; i < ts->n; i++)
		if (ts->hash[i] == h)
			return true;
	return false;
}

// Resolve the diffuse texture hash for one LM2 mesh.
static u32 nlg_lm2_diffuse (ccp preset, const u32 *ptrs, uint n_ptrs, const u8 *b006,
	uint b006_size, const nlg_texset_t *ts)
{
	if (!ptrs || !b006)
		return 0;
	int slot = nlg_lm2_tex_slot (preset);
	if (slot >= 0 && (uint)slot < n_ptrs)
	{
		u32 at = ptrs[slot];
		bool shadow = !strcmp (preset, "environmentmaterial/default");
		if (!strcmp (preset, "pestmaterial/default"))
			at += 4;
		if (at + (shadow ? 8 : 4) <= b006_size)
		{
			u32 dif = rd_le32 (b006 + at);
			if (dif && dif != 0xFFFFFFFF)
				return dif;
		}
	}
	// Fallback (SearchTextureLookups): first slot pointing at a known hash.
	for (uint i = 0; i < n_ptrs; i++)
	{
		u32 at = ptrs[i];
		if (at == 0 || at == 0xFFFFFFFF || at + 4 > b006_size)
			continue;
		u32 cand = rd_le32 (b006 + at);
		if (cand && cand != 0xFFFFFFFF && nlg_tex_known (ts, cand))
			return cand;
	}
	return 0;
}

static bool nlg_parse_lm2_model (model_t *model, const u8 *b001, uint b001_size, const u8 *b002,
	uint b002_size, const u8 *b003, uint b003_size, const u8 *b004, uint b004_size, const u8 *b005,
	uint b005_size, const u8 *b006, uint b006_size, const u8 *b007, uint b007_size,
	const nlg_texset_t *ts)
{
	if (!model || !b002 || !b003 || !b004 || !b005)
		return false;
	if (b002_size < 16 || b003_size < 0x28)
		return false;
	uint n_models = b002_size / 16;
	// Model header i: hash u32, meshcount u32, unk, pad.
	// Meshes in B003 run back to back across all models.
	uint mesh_total = b003_size / 0x28;
	if (!n_models || !mesh_total)
		return false;
	// B001 matrices, 64 bytes each (optional).
	uint n_mat = b001 && b001_size >= 64 ? b001_size / 64 : 0;
	// B004 vertex pointers: one u32 per mesh, read sequentially.
	uint b004_n = b004_size / 4;
	uint mesh_idx = 0, ptr_idx = 0;
	for (uint mi = 0; mi < n_models; mi++)
	{
		const u8 *mh = b002 + (size_t)mi * 16;
		u32 mcount = rd_le32 (mh + 4);
		float m[16];
		bool use_m = false;
		if (b001 && mi < n_mat)
		{
			for (int k = 0; k < 16; k++)
				m[k] = nlg_f32 (b001 + (size_t)mi * 64 + k * 4);
			use_m = true;
		}
		for (uint mj = 0; mj < mcount; mj++, mesh_idx++)
		{
			if (mesh_idx >= mesh_total || ptr_idx >= b004_n)
				return mesh_idx > 0;
			const u8 *e = b003 + (size_t)mesh_idx * 0x28;
			u32 index_off = rd_le32 (e);
			u16 index_count = rd_le16 (e + 4);
			u16 index_fmt = rd_le16 (e + 6);
			u32 vfmt = rd_le32 (e + 12);
			u32 mathash = rd_le32 (e + 16);
			u32 lut_index = rd_le32 (e + 28);
			u16 vert_count = rd_le16 (e + 32);
			u32 vert_ptr = rd_le32 (b004 + (size_t)ptr_idx * 4);
			ptr_idx++;
			uint stride = nlg_lm2_stride (vfmt);
			if (!stride || !vert_count || !index_count)
				continue;
			if ((u64)vert_ptr + (u64)vert_count * stride > b005_size)
				continue;
			uint idx_size = (index_fmt == 0x8000) ? 1 : 2;
			if ((u64)index_off + (u64)index_count * idx_size > b005_size)
				continue;
			// Material pointers via the LUT.
			const u32 *ptrs = 0;
			uint n_ptrs = 0;
			{
				// Collect all LUT indices first (sizes from successive gaps).
				// Re-scan B007 per mesh is O(meshes^2); meshes are few.
				uint total_idx = b007 ? b007_size / 4 : 0;
				if (b007 && lut_index < total_idx)
				{
					uint end = total_idx;
					// Find the next mesh's lut_index to bound this one.
					for (uint sk = mesh_idx + 1; sk < mesh_total; sk++)
					{
						const u8 *se = b003 + (size_t)sk * 0x28;
						uint si = rd_le32 (se + 28);
						if (si > lut_index && si < end)
							end = si;
					}
					if (end > total_idx)
						end = total_idx;
					if (end > lut_index)
					{
						ptrs = (const u32 *)(b007 + (size_t)lut_index * 4);
						n_ptrs = end - lut_index;
					}
				}
			}
			char hex[16];
			ccp preset = NLGHashName (mathash, hex);
			if (!preset)
				preset = hex;
			// Copy pointer values (unaligned-safe) for the resolver.
			u32 *pcopy = 0;
			if (n_ptrs)
			{
				pcopy = MALLOC (n_ptrs * sizeof (*pcopy));
				if (!pcopy)
					return false;
				for (uint k = 0; k < n_ptrs; k++)
					pcopy[k] = rd_le32 ((const u8 *)&ptrs[k]);
			}
			u32 dif = nlg_lm2_diffuse (preset, pcopy, n_ptrs, b006, b006_size, ts);
			FREE (pcopy);
			char mesh_name[64], mat_name[64], tex_png[128];
			char mhex[16];
			ccp mn = NLGHashName (mathash, mhex);
			snprintf (mesh_name, sizeof (mesh_name), "mesh_%u_%s", mesh_idx, mn ? mn : mhex);
			snprintf (mat_name, sizeof (mat_name), "%s", preset);
			tex_png[0] = 0;
			if (dif)
				snprintf (tex_png, sizeof (tex_png), "nlg_%08X_tex.fedtex.png", dif);
			nlg_geo_t g;
			memset (&g, 0, sizeof (g));
			bool ok = true;
			for (uint k = 0; k < index_count && ok; k++)
			{
				uint vi = (index_fmt == 0x8000) ? b005[index_off + k]
												: rd_le16 (b005 + index_off + (size_t)k * 2);
				if (vi >= vert_count)
					vi %= vert_count;
				nlg_vert_t vv;
				const u8 *vp = b005 + vert_ptr + (size_t)vi * stride;
				if (!nlg_lm2_decode_vert (&vv, vp, b005_size - (uint)(vp - b005), vfmt))
				{
					ok = false;
					break;
				}
				if (!nlg_geo_vert (&g, &vv, m, use_m))
				{
					ok = false;
					break;
				}
			}
			if (ok)
				ok = nlg_commit_mesh (model, &g, mesh_name, mat_name, tex_png[0] ? tex_png : 0);
			else
				nlg_geo_free (&g);
			if (!ok)
				return false;
		}
	}
	return model->num_meshes > 0;
}

/*__APPEND5__*/

//-----------------------------------------------------------------------------
// LM3 models: B002 headers (12B), B003 meshes (0x40), B004 vertex pointers,
// B005 index/vertex buffers (+ skinning, ignored: static-geometry
// discipline like the Federation Force parser), B006 materials,
// B007 material lookup.
//-----------------------------------------------------------------------------

// LM3 material preset -> {slot, pointer-index} (MaterialLoaderHelper).
static int nlg_lm3_tex_ptr (ccp preset)
{
	static const struct
	{
		ccp name;
		int ptr;
	} tab[] = {
		{ "base_metal_map", 16 },
		{ "base_luigi", 16 },
		{ "base_environment", 16 },
		{ "base_skin", 16 },
		{ "base_translucent", 16 },
		{ "egadd_glasses_lens", 16 },
		{ "kingboo_eyes2", 16 },
		{ "ghost_vip_dj_hair", 16 },
		{ "base_luigi_fabric", 16 },
		{ "complex_emissive", 16 },
		{ "golddark", 16 },
		{ "treeshader_cutout_01", 16 },
		{ "treeshader_base_01", 16 },
		{ "base_environment_detailn", 16 },
		{ "intromovie_terrain_01", 16 },
		{ "base_prop", 17 },
		{ "baseghostmaterial", 17 },
		{ "luigi_g_poltergust_body", 17 },
	};
	for (uint i = 0; i < sizeof (tab) / sizeof (tab[0]); i++)
		if (!strcmp (preset, tab[i].name))
			return tab[i].ptr;
	return -1;
}

// LM3 diffuse offset inside pointers[tex_ptr] (TEXTURE_SLOT table).
static int nlg_lm3_dif_off (ccp preset)
{
	static const struct
	{
		ccp name;
		int off;
	} tab[] = {
		{ "base_metal_map", 0 },
		{ "base_prop", 0 },
		{ "base_environment", 0 },
		{ "base_environment_detailn", 0 },
		{ "kingboo_eyes2", 0 },
		{ "egadd_glasses_lens", 0 },
		{ "ghost_vip_dj_hair", 0 },
		{ "base_luigi", 0 },
		{ "base_luigi_fabric", 24 },
		{ "base_translucent", 0 },
		{ "ghost_boo", 0 },
		{ "complex_emissive", 0 },
		{ "baseghostmaterial", 24 },
	};
	for (uint i = 0; i < sizeof (tab) / sizeof (tab[0]); i++)
		if (!strcmp (preset, tab[i].name))
			return tab[i].off;
	return 0; // default: DIFFUSE at +0
}

static u32 nlg_lm3_diffuse (ccp preset, const u32 *ptrs, uint n_ptrs, const u8 *b006,
	uint b006_size, const nlg_texset_t *ts)
{
	if (ptrs && b006)
	{
		int pi = nlg_lm3_tex_ptr (preset);
		if (pi >= 0 && (uint)pi < n_ptrs)
		{
			u32 at = ptrs[pi] + (u32)nlg_lm3_dif_off (preset);
			if (at + 4 <= b006_size)
			{
				u32 dif = rd_le32 (b006 + at);
				if (dif && dif != 0xFFFFFFFF && dif != 0x81800000)
					return dif;
			}
		}
		// Fallback: first slot pointing at a known texture hash.
		for (uint i = 0; i < n_ptrs; i++)
		{
			u32 at = ptrs[i];
			if (at == 0 || at == 0xFFFFFFFF || at + 4 > b006_size)
				continue;
			u32 cand = rd_le32 (b006 + at);
			if (cand && cand != 0xFFFFFFFF && cand != 0x81800000 && nlg_tex_known (ts, cand))
				return cand;
		}
	}
	return 0;
}

static bool nlg_parse_lm3_model (model_t *model, const u8 *b001, uint b001_size, const u8 *b002,
	uint b002_size, const u8 *b003, uint b003_size, const u8 *b004, uint b004_size, const u8 *b005,
	uint b005_size, const u8 *b006, uint b006_size, const u8 *b007, uint b007_size,
	const nlg_texset_t *ts)
{
	if (!model || !b002 || !b003 || !b004 || !b005)
		return false;
	if (b002_size < 12 || b003_size < 0x40)
		return false;
	uint n_models = b002_size / 12;
	uint mesh_total = b003_size / 0x40;
	if (!n_models || !mesh_total)
		return false;
	uint n_mat = b001 && b001_size >= 64 ? b001_size / 64 : 0;
	// B004 pointer records: 3 u32s per mesh, 4 when skinned. Walk by mesh.
	size_t p004 = 0;
	uint mesh_idx = 0;
	for (uint mi = 0; mi < n_models; mi++)
	{
		const u8 *mh = b002 + (size_t)mi * 12;
		u16 mcount = rd_le16 (mh + 8);
		float m[16];
		bool use_m = false;
		if (b001 && mi < n_mat)
		{
			for (int k = 0; k < 16; k++)
				m[k] = nlg_f32 (b001 + (size_t)mi * 64 + k * 4);
			use_m = true;
		}
		for (uint mj = 0; mj < mcount; mj++, mesh_idx++)
		{
			if (mesh_idx >= mesh_total)
				return mesh_idx > 0;
			const u8 *e = b003 + (size_t)mesh_idx * 0x40;
			u32 index_off = rd_le32 (e + 4);
			u32 index_flags = rd_le32 (e + 8);
			u32 vert_count = rd_le32 (e + 12);
			u8 n_lookup = e[17];
			u32 mathash = rd_le32 (e + 24);
			u32 skin_flags = rd_le32 (e + 36);
			uint index_count = index_flags & 0xFFFFFF;
			uint index_type = index_flags >> 24;
			bool skinned = skin_flags != 0xFFFFFF;
			uint rec_words = skinned ? 4 : 3;
			if (p004 + rec_words * 4 > b004_size)
				return mesh_idx > 0;
			u32 vert_ptr;
			if (skinned)
				vert_ptr = rd_le32 (b004 + p004 + 4);
			else
				vert_ptr = rd_le32 (b004 + p004);
			p004 += rec_words * 4;
			if (!vert_count || !index_count || vert_count > (4u << 20) || index_count > (16u << 20))
				continue;
			// Fixed 48-byte vertices: pos3f + u + nrm3f + v + tan4f.
			if ((u64)vert_ptr + (u64)vert_count * 48 > b005_size)
				continue;
			uint idx_size = (index_type == 0x80) ? 1 : (index_type == 0x40 ? 4 : 2);
			if ((u64)index_off + (u64)index_count * idx_size > b005_size)
				continue;
			// Material lookup pointers: sequential u32s in B007.
			u32 *pcopy = 0;
			if (n_lookup && b007)
			{
				// Sequential stream: mesh mesh_idx starts after the
				// previous meshes' counts. Recompute from the start.
				size_t q = 0;
				for (uint sk = 0; sk < mesh_idx; sk++)
				{
					const u8 *se = b003 + (size_t)sk * 0x40;
					q += se[17] * 4;
				}
				if (q + n_lookup * 4 <= b007_size)
				{
					pcopy = MALLOC (n_lookup * sizeof (*pcopy));
					if (!pcopy)
						return false;
					for (uint k = 0; k < n_lookup; k++)
						pcopy[k] = rd_le32 (b007 + q + k * 4);
				}
			}
			char hex[16];
			ccp preset = NLGHashName (mathash, hex);
			if (!preset)
				preset = hex;
			u32 dif = nlg_lm3_diffuse (preset, pcopy, pcopy ? n_lookup : 0, b006, b006_size, ts);
			FREE (pcopy);
			char mesh_name[64], mat_name[64], tex_png[64];
			char mhex[16];
			ccp mn = NLGHashName (mathash, mhex);
			snprintf (mesh_name, sizeof (mesh_name), "mesh_%u_%s", mesh_idx, mn ? mn : mhex);
			snprintf (mat_name, sizeof (mat_name), "%s", preset);
			tex_png[0] = 0;
			if (dif)
				snprintf (tex_png, sizeof (tex_png), "nlg_%08X_tex.fedtex.png", dif);
			nlg_geo_t g;
			memset (&g, 0, sizeof (g));
			bool ok = true;
			for (uint k = 0; k < index_count && ok; k++)
			{
				uint vi;
				if (idx_size == 1)
					vi = b005[index_off + k];
				else if (idx_size == 4)
					vi = rd_le32 (b005 + index_off + (size_t)k * 4);
				else
					vi = rd_le16 (b005 + index_off + (size_t)k * 2);
				if (vi >= vert_count)
					vi %= vert_count;
				const u8 *vp = b005 + vert_ptr + (size_t)vi * 48;
				nlg_vert_t vv;
				memset (&vv, 0, sizeof (vv));
				vv.r = vv.g = vv.b = vv.a = 1.0f;
				vv.px = nlg_f32 (vp);
				vv.py = nlg_f32 (vp + 4);
				vv.pz = nlg_f32 (vp + 8);
				vv.u0 = nlg_f32 (vp + 12);
				vv.nx = nlg_f32 (vp + 16);
				vv.ny = nlg_f32 (vp + 20);
				vv.nz = nlg_f32 (vp + 24);
				vv.v0 = nlg_f32 (vp + 28);
				vv.has_n = vv.has_uv0 = true;
				// Tangent (4f @+32) is carried for completeness but the
				// repo's GLB path keys it the same as UV0; skip storing.
				if (!nlg_geo_vert (&g, &vv, m, use_m))
				{
					ok = false;
					break;
				}
			}
			if (ok)
				ok = nlg_commit_mesh (model, &g, mesh_name, mat_name, tex_png[0] ? tex_png : 0);
			else
				nlg_geo_free (&g);
			if (!ok)
				return false;
		}
	}
	return model->num_meshes > 0;
}

//-----------------------------------------------------------------------------
// Skeletons -> joints. LM2 BoneInfo carries ParentIndex; LM3 parenting
// arrives in the separate 0x7106 chunk. Rotations convert quat -> euler
// (joint_t convention); translations are raw bind offsets.
//-----------------------------------------------------------------------------

static uint nlg_fill_joints (model_t *model, const char **names, const int *parents,
	const float *quats, const float *trans, uint n)
{
	if (!model || !names || !parents || !quats || !trans || !n || n > 4096)
		return 0;
	model->joints = CALLOC (n, sizeof (joint_t));
	if (!model->joints)
		return 0;
	model->num_joints = n;
	for (uint i = 0; i < n; i++)
	{
		joint_t *j = &model->joints[i];
		snprintf (j->name, sizeof (j->name), "%s", names[i] ? names[i] : "?");
		int p = parents[i];
		j->parent_idx = (p < 0 || (uint)p >= n || (uint)p == i) ? -1 : p;
		float qx = quats[i * 4], qy = quats[i * 4 + 1], qz = quats[i * 4 + 2],
			  qw = quats[i * 4 + 3];
		float ql = sqrtf (qx * qx + qy * qy + qz * qz + qw * qw);
		if (ql > 1e-6f && isfinite (ql))
		{
			qx /= ql;
			qy /= ql;
			qz /= ql;
			qw /= ql;
		}
		else
		{
			qx = qy = qz = 0.0f;
			qw = 1.0f;
		}
		nlg_quat_to_euler (qx, qy, qz, qw, &j->rotate.x, &j->rotate.y, &j->rotate.z);
		j->translate.x = trans[i * 3];
		j->translate.y = trans[i * 3 + 1];
		j->translate.z = trans[i * 3 + 2];
		if (!isfinite (j->translate.x) || !isfinite (j->translate.y) || !isfinite (j->translate.z))
			j->translate.x = j->translate.y = j->translate.z = 0.0f;
		j->scale.x = j->scale.y = j->scale.z = 1.0f;
	}
	return n;
}

// LM2: 0x7101 header (BoneCount @0), 0x7102 infos (12B: hash, parent s16,
// totalchild s16, boneidx u16, childcount, unk), 0x7103 transforms
// (quat4 + trans3 + scale3 = 40B).
static uint nlg_parse_lm2_skel (model_t *model, const u8 *s101, uint s101_size, const u8 *s102,
	uint s102_size, const u8 *s103, uint s103_size)
{
	if (!model || !s101 || !s102 || !s103 || s101_size < 4)
		return 0;
	u32 n = rd_le32 (s101);
	if (!n || n > 4096 || s102_size < n * 12 || s103_size < n * 40)
		return 0;
	const char **names = CALLOC (n, sizeof (*names));
	int *parents = MALLOC (n * sizeof (*parents));
	float *quats = MALLOC (n * 4 * sizeof (*quats));
	float *trans = MALLOC (n * 3 * sizeof (*trans));
	if (!names || !parents || !quats || !trans)
	{
		FREE (names);
		FREE (parents);
		FREE (quats);
		FREE (trans);
		return 0;
	}
	char (*hexbufs)[16] = CALLOC (n, 16);
	if (!hexbufs)
	{
		FREE (names);
		FREE (parents);
		FREE (quats);
		FREE (trans);
		return 0;
	}
	for (uint i = 0; i < n; i++)
	{
		const u8 *bi = s102 + (size_t)i * 12;
		u32 hash = rd_le32 (bi);
		ccp nm = NLGHashName (hash, hexbufs[i]);
		names[i] = nm ? nm : hexbufs[i];
		parents[i] = (s16)rd_le16 (bi + 4);
		const u8 *bt = s103 + (size_t)i * 40;
		for (int k = 0; k < 4; k++)
			quats[i * 4 + k] = nlg_f32 (bt + k * 4);
		for (int k = 0; k < 3; k++)
			trans[i * 3 + k] = nlg_f32 (bt + 16 + k * 4);
	}
	uint got = nlg_fill_joints (model, names, parents, quats, trans, n);
	FREE (names);
	FREE (parents);
	FREE (quats);
	FREE (trans);
	FREE (hexbufs);
	return got;
}

// LM3: 0x7101 header (BoneCount @20), 0x7102 infos (8B: hash,
// totalchild s16, childcount, unk), 0x7103 transforms (quat4 + trans3),
// 0x7106 parenting (s16 per bone). 0x7104 index list is informational.
static uint nlg_parse_lm3_skel (model_t *model, const u8 *s101, uint s101_size, const u8 *s102,
	uint s102_size, const u8 *s103, uint s103_size, const u8 *s106, uint s106_size)
{
	if (!model || !s101 || !s102 || !s103 || !s106 || s101_size < 28)
		return 0;
	u32 n = rd_le32 (s101 + 20);
	if (!n || n > 4096 || s102_size < n * 8 || s103_size < n * 28 || s106_size < n * 2)
		return 0;
	const char **names = CALLOC (n, sizeof (*names));
	int *parents = MALLOC (n * sizeof (*parents));
	float *quats = MALLOC (n * 4 * sizeof (*quats));
	float *trans = MALLOC (n * 3 * sizeof (*trans));
	if (!names || !parents || !quats || !trans)
	{
		FREE (names);
		FREE (parents);
		FREE (quats);
		FREE (trans);
		return 0;
	}
	char (*hexbufs)[16] = CALLOC (n, 16);
	if (!hexbufs)
	{
		FREE (names);
		FREE (parents);
		FREE (quats);
		FREE (trans);
		return 0;
	}
	for (uint i = 0; i < n; i++)
	{
		const u8 *bi = s102 + (size_t)i * 8;
		u32 hash = rd_le32 (bi);
		ccp nm = NLGHashName (hash, hexbufs[i]);
		names[i] = nm ? nm : hexbufs[i];
		parents[i] = (s16)rd_le16 (s106 + (size_t)i * 2);
		const u8 *bt = s103 + (size_t)i * 28;
		for (int k = 0; k < 4; k++)
			quats[i * 4 + k] = nlg_f32 (bt + k * 4);
		for (int k = 0; k < 3; k++)
			trans[i * 3 + k] = nlg_f32 (bt + 16 + k * 4);
	}
	uint got = nlg_fill_joints (model, names, parents, quats, trans, n);
	FREE (names);
	FREE (parents);
	FREE (quats);
	FREE (trans);
	FREE (hexbufs);
	return got;
}

/*__APPEND6__*/

//-----------------------------------------------------------------------------
// Textures -> RGBA8. LM2 is CTR PICA (DecodePicaTexture); LM3 is Switch
// block-linear (BntxDeswizzle + BCn/ASTC block decoders).
//
// The Tegra block-height selector is not stored in the LM3 header, so a
// tightest-fitting heuristic is used (smallest log2 with bh*8 >= height,
// capped at 4) and documented as such; synthetic round-trips verify the
// path, retail block heights remain unconfirmed.
//-----------------------------------------------------------------------------

static uint nlg_lm3_bh_log2 (uint height_blocks, uint blk_h)
{
	uint h = height_blocks * blk_h;
	uint log2 = 0;
	while (log2 < 4 && (8u << log2) < h)
		log2++;
	return log2;
}

static enumError nlg_decode_lm3_pixels (
	u8 **dest, uint *width, uint *height, const u8 *src, uint src_size, uint w, uint h, uint fmt)
{
	if (!dest || !width || !height || !src || !w || !h || w > 16384 || h > 16384)
		return EINVAL;
	uint blk_w = 4, blk_h = 4, bpp = 16;
	int kind = -1; // 0=RGBA8 copy, 1=BC1, 2=BC2, 3=BC3, 4=BC4, 5=BC5s,
				   // 6=BC6u, 7=BC7, 8=ASTC
	uint astc_w = 4, astc_h = 4;
	switch (fmt)
	{
		case 0x00:
		case 0x01:
		case 0x05:
		case 0x0D:
		case 0x0E:
			blk_w = blk_h = 1;
			bpp = 4;
			kind = 0;
			break;
		case 0x11:
		case 0x12:
			bpp = 8;
			kind = 1;
			break;
		case 0x13:
			kind = 2;
			break;
		case 0x14:
			kind = 3;
			break;
		case 0x15:
			bpp = 8;
			kind = 4;
			break;
		case 0x16:
			kind = 5;
			break;
		case 0x17:
			kind = 6;
			break;
		case 0x18:
			kind = 7;
			break;
		case 0x19:
			astc_w = 4;
			astc_h = 4;
			kind = 8;
			break;
		case 0x1A:
			astc_w = 5;
			astc_h = 4;
			kind = 8;
			break;
		case 0x1B:
			astc_w = 5;
			astc_h = 5;
			kind = 8;
			break;
		case 0x1C:
			astc_w = 6;
			astc_h = 5;
			kind = 8;
			break;
		case 0x1D:
			astc_w = 6;
			astc_h = 6;
			kind = 8;
			break;
		case 0x1E:
			astc_w = 8;
			astc_h = 5;
			kind = 8;
			break;
		case 0x1F:
			astc_w = 8;
			astc_h = 6;
			kind = 8;
			break;
		case 0x20:
			astc_w = 8;
			astc_h = 8;
			kind = 8;
			break;
		default:
			return ERR_NOTHING_TO_DO;
	}
	if (kind == 8)
	{
		blk_w = astc_w;
		blk_h = astc_h;
		bpp = 16;
	}
	uint wb = (w + blk_w - 1) / blk_w, hb = (h + blk_h - 1) / blk_h;
	u8 *linear = 0;
	uint linear_size = 0;
	enumError err = BntxDeswizzle (&linear, &linear_size, src, src_size, w, h, blk_w, blk_h, bpp, 0,
		nlg_lm3_bh_log2 (hb, blk_h), false);
	if (err)
		return err;
	u8 *rgba = MALLOC ((size_t)w * h * 4);
	if (!rgba)
	{
		FREE (linear);
		return ERR_CANT_CREATE;
	}
	if (kind == 0)
	{
		for (uint y = 0; y < h; y++)
			memcpy (rgba + (size_t)y * w * 4, linear + (size_t)y * wb * blk_w * 4, (size_t)w * 4);
	}
	else if (kind == 6)
	{
		if (szs_decode_bc6 (linear, w, h, 0, rgba) != 0)
		{
			FREE (linear);
			FREE (rgba);
			return ERR_INVALID_DATA;
		}
	}
	else if (kind == 7)
	{
		if (szs_decode_bc7 (linear, w, h, rgba) != 0)
		{
			FREE (linear);
			FREE (rgba);
			return ERR_INVALID_DATA;
		}
	}
	else
	{
		for (uint by = 0; by < hb; by++)
			for (uint bx = 0; bx < wb; bx++)
			{
				const u8 *blk = linear + ((size_t)by * wb + bx) * bpp;
				u8 tile[16 * 16 * 4];
				memset (tile, 0, sizeof (tile));
				uint tw = blk_w, th = blk_h;
				if (kind == 8)
				{
					astc_decompress_block (tile, blk, (int)tw, (int)th);
				}
				else
				{
					tw = th = 4;
					switch (kind)
					{
						case 1:
							decode_bc1_block (blk, tile, true);
							break;
						case 2:
							decode_bc2_block (blk, tile);
							break;
						case 3:
							decode_bc3_block (blk, tile);
							break;
						case 4:
							decode_bc4_block (blk, tile);
							break;
						case 5:
							decode_bc5_signed_block (blk, tile);
							break;
						default:
							break;
					}
				}
				for (uint y = 0; y < th && by * th + y < h; y++)
					for (uint x = 0; x < tw && bx * tw + x < w; x++)
						memcpy (rgba + (((size_t)(by * th + y) * w) + bx * tw + x) * 4,
							tile + ((size_t)y * tw + x) * 4, 4);
			}
	}
	FREE (linear);
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

// LM2 B501 header: ImageSize@0, Hash@4, W@16, H@18, Format@44 (48 bytes).
static enumError nlg_decode_lm2_pixels (
	u8 **dest, uint *width, uint *height, const u8 *hd, uint hd_size, const u8 *pd, uint pd_size)
{
	if (!dest || !width || !height || !hd || hd_size < 48 || !pd || !pd_size)
		return EINVAL;
	u32 image_size = rd_le32 (hd);
	u16 w = rd_le16 (hd + 16), h = rd_le16 (hd + 18);
	u8 fmt = hd[44];
	if (!w || !h || w > 16384 || h > 16384)
		return EINVAL;
	if (!image_size || image_size > pd_size)
		image_size = pd_size;
	return DecodePicaTexture (dest, width, height, pd, w, h, fmt, image_size);
}

enumError DecodeNLGTexture (u8 **dest, uint *width, uint *height, const u8 *data, size_t size)
{
	if (!dest || !width || !height)
		return EINVAL;
	*dest = 0;
	if (!IsNLGTexture (data, size))
		return EINVAL;
	u16 ver = rd_le16 (data + 4);
	if (ver == 1)
		return DecodeFedForceTexture (dest, width, height, data, size);
	uint fmt = rd_le16 (data + 6);
	uint w = rd_le16 (data + 8), h = rd_le16 (data + 10);
	uint ds = rd_le32 (data + 20);
	const u8 *px = data + 24;
	if (ver == 2)
		return DecodePicaTexture (dest, width, height, px, w, h, fmt, ds);
	if (ver == 3)
		return nlg_decode_lm3_pixels (dest, width, height, px, ds, w, h, fmt);
	return EINVAL;
}

//-----------------------------------------------------------------------------
// Animations (0x7000) -> text. LM3 track types: 0 scale, 1 rotation,
// 3 translation. LM2: 0 rotation, 1 translation, 2 scale (rotation opcodes
// 0x14/0x16 only; everything else is recorded, not decoded -- the reference
// leaves LM2 translation/scale equally stubbed).
//-----------------------------------------------------------------------------

static const char *nlg_anim_type (nlg_variant_t variant, uint type)
{
	if (variant == NLG_LM2)
		return type == 0 ? "rotation" : (type == 1 ? "translation" : (type == 2 ? "scale" : "?"));
	return type == 0 ? "scale" : (type == 1 ? "rotation" : (type == 3 ? "translation" : "?"));
}

// Key-count estimator for known translation/rotation opcodes; returns 0
// when the opcode is unknown (caller records it as opaque).
static uint nlg_anim_keys (
	const u8 *blob, uint blob_size, uint at, uint opcode, uint frames, bool is_rot)
{
	if (at >= blob_size)
		return 0;
	if (!is_rot)
	{
		switch (opcode)
		{
			case 0x06:
			case 0x08:
				return frames;
			case 0x09:
				return at + 4 <= blob_size ? rd_le32 (blob + at) : 0;
			case 0x0A:
				return at + 2 <= blob_size ? rd_le16 (blob + at) : 0;
			case 0x0B:
			case 0x0C:
				return 1;
			case 0x0D:
				return at + 16 <= blob_size ? rd_le32 (blob + at + 12) : 0;
			case 0x0E:
				return at + 8 <= blob_size ? rd_le16 (blob + at + 6) : 0;
			default:
				return 0;
		}
	}
	switch (opcode)
	{
		case 0x0F:
		case 0x18:
		case 0x19:
			return frames;
		case 0x13:
		case 0x14:
			return at + 4 <= blob_size ? rd_le32 (blob + at) : 0;
		case 0x15:
		case 0x16:
		case 0x17:
			return 1;
		default:
			return 0;
	}
}

static enumError nlg_dump_anim (u8 **dest, uint *dest_size, const u8 *blob, uint blob_size,
	nlg_variant_t variant, u32 path_hash)
{
	if (!dest || !dest_size || !blob || blob_size < 16)
		return EINVAL;
	u16 n_tracks = rd_le16 (blob + 4);
	u16 n_frames = rd_le16 (blob + 6);
	float dur = nlg_f32 (blob + 8);
	if (!n_tracks)
		return EINVAL;
	if (16 + (size_t)n_tracks * 12 > blob_size)
		return EINVAL;
	char hbuf[16];
	size_t cap = 256 + (size_t)n_tracks * 128, len = 0;
	char *out = MALLOC (cap);
	if (!out)
		return ERR_CANT_CREATE;
	len += snprintf (out + len, cap - len,
		"NLG animation %08X\ntracks %u frames %u duration %.3f fps %.3f\n", path_hash, n_tracks,
		n_frames, dur, dur > 0.0f ? n_frames / dur : 0.0f);
	for (uint i = 0; i < n_tracks; i++)
	{
		const u8 *t = blob + 16 + (size_t)i * 12;
		u32 hash = rd_le32 (t);
		u8 idx = t[4], type = t[6], op = t[7];
		u32 at = rd_le32 (t + 8);
		char th[16];
		ccp nm = NLGHashName (hash, th);
		bool is_rot = (variant == NLG_LM2) ? (type == 0) : (type == 1);
		bool is_key = (variant == NLG_LM2) ? (type <= 2) : (type == 0 || type == 1 || type == 3);
		uint keys = is_key ? nlg_anim_keys (blob, blob_size, at, op, n_frames, is_rot) : 0;
		if (len + 160 > cap)
		{
			cap *= 2;
			char *no = REALLOC (out, cap);
			if (!no)
			{
				FREE (out);
				return ERR_CANT_CREATE;
			}
			out = no;
		}
		if (is_key && keys)
			len += snprintf (out + len, cap - len,
				"track %u hash %08X (%s) type %s opcode 0x%02X keys %u\n", idx, hash, nm ? nm : th,
				nlg_anim_type (variant, type), op, keys);
		else
			len += snprintf (out + len, cap - len,
				"track %u hash %08X (%s) type %s opcode 0x%02X opaque\n", idx, hash, nm ? nm : th,
				nlg_anim_type (variant, type), op);
		(void)hbuf;
	}
	*dest = (u8 *)out;
	*dest_size = (uint)len;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Scripts (0x5000 data/info/table + 0x6500 hash tables) -> text.
// Element records are 12 bytes; ScriptData blobs hold u32 ref sections, a
// u16 opcode stream and a trailing string table (LM2 and LM3 differ only in
// the blob's section-count prefix).
//-----------------------------------------------------------------------------

static enumError nlg_dump_script (u8 **dest, uint *dest_size, const nlg_chunk_t *tab, uint n_tab,
	uint start, uint count, nlg_variant_t variant, const nlg_buf_t *bufs, uint n_bufs,
	const nlg_file_t *files, uint n_files, u32 path_hash)
{
	if (!dest || !dest_size)
		return EINVAL;
	size_t cap = 1024, len = 0;
	char *out = MALLOC (cap);
	if (!out)
		return ERR_CANT_CREATE;
#define NLG_SAPPEND(...)                                                                           \
	do                                                                                             \
	{                                                                                              \
		int _n = snprintf (out + len, cap - len, __VA_ARGS__);                                     \
		if (_n < 0)                                                                                \
		{                                                                                          \
			FREE (out);                                                                            \
			return ERR_CANT_CREATE;                                                                \
		}                                                                                          \
		len += (size_t)_n;                                                                         \
		if (len + 512 > cap)                                                                       \
		{                                                                                          \
			cap *= 2;                                                                              \
			char *_no = REALLOC (out, cap);                                                        \
			if (!_no)                                                                              \
			{                                                                                      \
				FREE (out);                                                                        \
				return ERR_CANT_CREATE;                                                            \
			}                                                                                      \
			out = _no;                                                                             \
		}                                                                                          \
	} while (0)
	NLG_SAPPEND ("NLG script %08X\n", path_hash);
	for (uint c = 0; c < count; c++)
	{
		uint ti = start + c;
		if (ti >= n_tab || nlg_is_container_type (tab[ti].type))
			continue;
		uint sz = 0;
		const u8 *d = nlg_child_data (variant, tab, ti, bufs, n_bufs, &sz);
		if (!d || !sz)
			continue;
		u16 type = tab[ti].type;
		if (type == 0x5014 || type == 0x6501)
		{
			uint n_el = sz / 12;
			NLG_SAPPEND ("table %04X entries %u\n", type, n_el);
			for (uint e = 0; e < n_el && e < 100000; e++)
			{
				u32 h = rd_le32 (d + e * 12);
				u32 a = rd_le32 (d + e * 12 + 4);
				char th[16];
				ccp nm = NLGHashName (h, th);
				NLG_SAPPEND ("  %08X (%s) %u\n", h, nm ? nm : th, a);
			}
		}
		else if (type == 0x6503)
		{
			if (sz >= 4)
				NLG_SAPPEND ("filehash %08X\n", rd_le32 (d));
		}
		else if (type == 0x5012)
		{
			// u32 ref section + u16 opcodes + strings. LM2 has one leading
			// u32 before the two section sizes; LM3 has two.
			uint hdr = (variant == NLG_LM2) ? 16 : 20;
			if (sz < hdr)
			{
				NLG_SAPPEND ("data %u bytes (short)\n", sz);
				continue;
			}
			// LM2: value, size2, size1, strtab. LM3: value, value2,
			// size1, size2, strtab.
			uint s1 = rd_le32 (d + 8);
			uint s2 = (variant == NLG_LM2) ? rd_le32 (d + 4) : rd_le32 (d + 12);
			uint n_u32 = s1 / 4, n_u16 = s2 / 2;
			NLG_SAPPEND ("data refs %u opcodes %u strings @%u\n", n_u32 < 100000 ? n_u32 : 0,
				n_u16 < 1000000 ? n_u16 : 0, hdr + s1 + s2);
			const u8 *rp = d + hdr;
			for (uint k = 0; k < n_u32 && k < 100000; k++)
			{
				if (hdr + (size_t)(k + 1) * 4 > sz)
					break;
				u32 ref = rd_le32 (rp + k * 4);
				ccp what = 0;
				char wh[16];
				for (uint f = 0; f < n_files; f++)
					if (files[f].path_hash == ref)
					{
						snprintf (wh, sizeof (wh), "type_%04X", files[f].type);
						what = wh;
						break;
					}
				if (ref && what)
					NLG_SAPPEND ("  ref %08X -> %s\n", ref, what);
			}
			size_t sp = hdr + (size_t)s1 + s2;
			while (sp < sz)
			{
				const u8 *nul = memchr (d + sp, 0, sz - sp);
				if (!nul)
					break;
				if (nul != d + sp)
					NLG_SAPPEND ("  str \"%.*s\"\n", (int)(nul - (d + sp)), d + sp);
				sp += (size_t)(nul - (d + sp)) + 1;
				if (sp > sz + 1000000)
					break;
			}
		}
		else if (type == 0x5013 || type == 0x5011 || type == 0x5015)
		{
			NLG_SAPPEND ("info %04X %u bytes\n", type, sz);
		}
	}
	*dest = (u8 *)out;
	*dest_size = (uint)len;
	return ERR_OK;
#undef NLG_SAPPEND
}

/*__APPEND7__*/

//-----------------------------------------------------------------------------
// FEDM/FEDS/FEDT assembly from raw chunks + standalone parse entry points.
//-----------------------------------------------------------------------------

static enumError nlg_build_fedm (u8 **dest, uint *dest_size, u16 ver, const u8 *b008, uint s008,
	const u8 *b009, uint s009, const u8 *b001, uint s001, const u8 *b003, uint s003, const u8 *b004,
	uint s004, const u8 *b005, uint s005, const u8 *b006, uint s006, const u8 *b007, uint s007,
	const u8 *b002, uint s002, const u8 *s101, uint z101, const u8 *s102, uint z102, const u8 *s103,
	uint z103, const u8 *s104, uint z104, const u8 *s105, uint z105, const u8 *s106, uint z106)
{
	u16 types[15];
	const u8 *datas[15];
	uint sizes[15], n = 0;
#define NLG_PART(t, d, s)                                                                          \
	do                                                                                             \
	{                                                                                              \
		if ((d) && (s))                                                                            \
		{                                                                                          \
			types[n] = (t);                                                                        \
			datas[n] = (d);                                                                        \
			sizes[n] = (s);                                                                        \
			n++;                                                                                   \
		}                                                                                          \
	} while (0)
	NLG_PART (0xB008, b008, s008);
	NLG_PART (0xB009, b009, s009);
	NLG_PART (0xB001, b001, s001);
	NLG_PART (0xB003, b003, s003);
	NLG_PART (0xB004, b004, s004);
	NLG_PART (0xB005, b005, s005);
	NLG_PART (0xB006, b006, s006);
	NLG_PART (0xB007, b007, s007);
	NLG_PART (0xB002, b002, s002);
	NLG_PART (0x7101, s101, z101);
	NLG_PART (0x7102, s102, z102);
	NLG_PART (0x7103, s103, z103);
	NLG_PART (0x7104, s104, z104);
	NLG_PART (0x7105, s105, z105);
	NLG_PART (0x7106, s106, z106);
#undef NLG_PART
	if (!n)
		return EINVAL;
	return nlg_build_container (dest, dest_size, "FEDM", ver, types, datas, sizes, n);
}

static enumError nlg_build_feds (u8 **dest, uint *dest_size, u16 ver, const u8 *s101, uint z101,
	const u8 *s102, uint z102, const u8 *s103, uint z103, const u8 *s104, uint z104, const u8 *s105,
	uint z105, const u8 *s106, uint z106)
{
	u8 *fedm = 0;
	uint fedm_size = 0;
	enumError err = nlg_build_fedm (&fedm, &fedm_size, ver, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, s101, z101, s102, z102, s103, z103, s104, z104, s105, z105, s106, z106);
	if (err)
		return err;
	memcpy (fedm, "FEDS", 4);
	*dest = fedm;
	*dest_size = fedm_size;
	return ERR_OK;
}

static enumError nlg_build_fedt (u8 **dest, uint *dest_size, u16 ver, uint fmt, uint w, uint h,
	u32 hash, const u8 *px, uint px_size)
{
	if (!dest || !dest_size || !px || !px_size || !w || !h)
		return EINVAL;
	u8 *out = MALLOC (24 + px_size);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "FEDT", 4);
	wr_le16 (out + 4, ver);
	wr_le16 (out + 6, (u16)fmt);
	wr_le16 (out + 8, (u16)w);
	wr_le16 (out + 10, (u16)h);
	wr_le16 (out + 12, 1);
	wr_le16 (out + 14, 0);
	wr_le32 (out + 16, hash);
	wr_le32 (out + 20, px_size);
	memcpy (out + 24, px, px_size);
	*dest = out;
	*dest_size = 24 + px_size;
	return ERR_OK;
}

model_t *ParseNLGModel (const u8 *data, size_t size, const nlg_texset_t *ts)
{
	nlg_part_t *parts = 0;
	uint n = 0;
	u16 ver = 0;
	if (nlg_parse_container (data, size, "FEDM", &parts, &n, &ver) != ERR_OK)
		return 0;
	model_t *model = 0;
	if (ver == 1)
	{
		model = ParseFedForceModel (data, size);
		FREE (parts);
		return model;
	}
	const nlg_part_t *b001 = nlg_find_part (parts, n, 0xB001);
	const nlg_part_t *b002 = nlg_find_part (parts, n, 0xB002);
	const nlg_part_t *b003 = nlg_find_part (parts, n, 0xB003);
	const nlg_part_t *b004 = nlg_find_part (parts, n, 0xB004);
	const nlg_part_t *b005 = nlg_find_part (parts, n, 0xB005);
	const nlg_part_t *b006 = nlg_find_part (parts, n, 0xB006);
	const nlg_part_t *b007 = nlg_find_part (parts, n, 0xB007);
	const nlg_part_t *b008 = nlg_find_part (parts, n, 0xB008);
	const nlg_part_t *b009 = nlg_find_part (parts, n, 0xB009);
	const nlg_part_t *s101 = nlg_find_part (parts, n, 0x7101);
	const nlg_part_t *s102 = nlg_find_part (parts, n, 0x7102);
	const nlg_part_t *s103 = nlg_find_part (parts, n, 0x7103);
	(void)b008;
	(void)b009;
	model = CALLOC (1, sizeof (*model));
	if (!model)
	{
		FREE (parts);
		return 0;
	}
	bool ok = false;
	if (b002 && b003 && b004 && b005)
	{
		if (ver == 2)
			ok = nlg_parse_lm2_model (model, b001 ? b001->data : 0, b001 ? b001->size : 0,
				b002->data, b002->size, b003->data, b003->size, b004->data, b004->size, b005->data,
				b005->size, b006 ? b006->data : 0, b006 ? b006->size : 0, b007 ? b007->data : 0,
				b007 ? b007->size : 0, ts);
		else
			ok = nlg_parse_lm3_model (model, b001 ? b001->data : 0, b001 ? b001->size : 0,
				b002->data, b002->size, b003->data, b003->size, b004->data, b004->size, b005->data,
				b005->size, b006 ? b006->data : 0, b006 ? b006->size : 0, b007 ? b007->data : 0,
				b007 ? b007->size : 0, ts);
	}
	if (ok && s101 && s102 && s103)
	{
		if (ver == 2)
			nlg_parse_lm2_skel (
				model, s101->data, s101->size, s102->data, s102->size, s103->data, s103->size);
		else
		{
			const nlg_part_t *s106 = nlg_find_part (parts, n, 0x7106);
			if (s106)
				nlg_parse_lm3_skel (model, s101->data, s101->size, s102->data, s102->size,
					s103->data, s103->size, s106->data, s106->size);
		}
	}
	FREE (parts);
	if (!ok)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}

model_t *ParseNLGSkeleton (const u8 *data, size_t size)
{
	nlg_part_t *parts = 0;
	uint n = 0;
	u16 ver = 0;
	if (nlg_parse_container (data, size, "FEDS", &parts, &n, &ver) != ERR_OK)
		return 0;
	model_t *model = 0;
	if (ver == 1)
	{
		model = ParseFedForceSkeleton (data, size);
		FREE (parts);
		return model;
	}
	const nlg_part_t *s101 = nlg_find_part (parts, n, 0x7101);
	const nlg_part_t *s102 = nlg_find_part (parts, n, 0x7102);
	const nlg_part_t *s103 = nlg_find_part (parts, n, 0x7103);
	model = CALLOC (1, sizeof (*model));
	if (!model)
	{
		FREE (parts);
		return 0;
	}
	bool ok = false;
	if (s101 && s102 && s103)
	{
		if (ver == 2)
			ok = nlg_parse_lm2_skel (
					 model, s101->data, s101->size, s102->data, s102->size, s103->data, s103->size)
				> 0;
		else
		{
			const nlg_part_t *s106 = nlg_find_part (parts, n, 0x7106);
			if (s106)
				ok = nlg_parse_lm3_skel (model, s101->data, s101->size, s102->data, s102->size,
						 s103->data, s103->size, s106->data, s106->size)
					> 0;
		}
	}
	FREE (parts);
	if (!ok)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}

/*__APPEND8__*/

//-----------------------------------------------------------------------------
// Typed extraction driver: assemble buffers, pair FileHeaders, export.
//-----------------------------------------------------------------------------

static void nlg_save (ccp path, const u8 *data, uint size)
{
	if (!testmode && data && size)
		SaveFile (path, 0, 0, data, size, 0);
}

static bool nlg_is_printable (const u8 *d, uint n)
{
	if (!d || !n)
		return false;
	uint check = n < 512 ? n : 512, text = 0;
	for (uint i = 0; i < check; i++)
		if ((d[i] >= 32 && d[i] < 127) || d[i] == '\n' || d[i] == '\r' || d[i] == '\t' || !d[i])
			text++;
	return text * 4 >= check * 3;
}

enumError ExtractNLGTyped (ccp dest, const u8 *dict, uint dict_size, const u8 *data_raw,
	size_t data_raw_size, nlg_variant_t variant, bool is_compressed)
{
	if (!dest || !dict || !dict_size || variant == NLG_UNKNOWN)
		return EINVAL;
	if (!data_raw || !data_raw_size)
		return ERR_OK; // block dumps already written; nothing typed to do

	// ---- 1. blocks + buffers ----
	nlg_block_t *bl = 0;
	uint n_blocks = 0;
	const char **strings = 0;
	uint n_strings = 0;
	fed_dict_block_t *fed_blocks = 0;
	uint fed_n_blocks = 0;
	fed_dict_ref_t fed_ref;
	memset (&fed_ref, 0, sizeof (fed_ref));
	if (variant == NLG_FEDFORCE)
	{
		bool is_fed = false;
		if (ScanFedForceDict (dict, dict_size, &is_fed, &fed_blocks, &fed_n_blocks, &fed_ref,
				&strings, &n_strings)
				!= ERR_OK
			|| !is_fed)
		{
			FreeFedForceDict (fed_blocks, strings, n_strings);
			return ERR_OK;
		}
		// Normalize to nlg_block_t (same leading fields, no aliasing).
		bl = CALLOC (fed_n_blocks ? fed_n_blocks : 1, sizeof (*bl));
		if (!bl)
		{
			FreeFedForceDict (fed_blocks, strings, n_strings);
			return ERR_CANT_CREATE;
		}
		for (uint i = 0; i < fed_n_blocks; i++)
		{
			bl[i].offset = fed_blocks[i].offset;
			bl[i].decomp_size = fed_blocks[i].decomp_size;
			bl[i].comp_size = fed_blocks[i].comp_size;
			bl[i].flags = fed_blocks[i].flags;
			bl[i].source_index = fed_blocks[i].source_index;
		}
		n_blocks = fed_n_blocks;
		FreeFedForceDict (fed_blocks, 0, 0);
		fed_blocks = 0;
	}
	else if (variant == NLG_LM3)
	{
		if (ScanLM3Dict (dict, dict_size, &bl, &n_blocks, &strings, &n_strings, 0) != ERR_OK)
			return ERR_OK;
	}
	else
	{
		if (ScanLM2Dict (dict, dict_size, &bl, &n_blocks, &strings, &n_strings, 0) != ERR_OK)
			return ERR_OK;
	}

	nlg_buf_t *bufs = CALLOC (70, sizeof (*bufs));
	if (!bufs)
	{
		FreeNLGDict (bl, strings, n_strings);
		FreeFedForceDict (fed_blocks, strings, n_strings);
		return ERR_CANT_CREATE;
	}
	const u8 *table_data = 0;
	uint table_size = 0;
	if (variant == NLG_FEDFORCE)
	{
		nlg_load_buf (bufs, 0, bl, n_blocks, data_raw, data_raw_size, 0, is_compressed);
		for (uint pos = 0; pos < 8; pos++)
		{
			uint bi = fed_ref.block_indices[pos];
			if (!bi || bi >= n_blocks)
				continue;
			nlg_load_buf (bufs, pos, bl, n_blocks, data_raw, data_raw_size, bi, is_compressed);
		}
		table_data = bufs[0].data;
		table_size = bufs[0].size;
	}
	else if (variant == NLG_LM3)
	{
		for (uint i = 0; i < sizeof (lm3_want) / sizeof (lm3_want[0]); i++)
			nlg_load_buf (bufs, lm3_want[i], bl, n_blocks, data_raw, data_raw_size, lm3_want[i],
				is_compressed);
		table_data = bufs[0].data;
		table_size = bufs[0].size;
	}
	else
	{
		for (uint i = 0; i < 4 && i < n_blocks; i++)
			nlg_load_buf (bufs, i, bl, n_blocks, data_raw, data_raw_size, i, is_compressed);
		table_data = bufs[0].data;
		table_size = bufs[0].size;
	}

	// ---- 2. chunk table + header pairing ----
	nlg_chunk_t *tab = 0;
	uint n_tab = 0;
	if (!table_data || table_size < 12
		|| ScanNLGChunks (&tab, &n_tab, table_data, table_size) != ERR_OK)
		goto done;
	// Federation Force table size hint (0 = trust the buffer).
	if (variant == NLG_FEDFORCE && fed_ref.file_section_count && fed_ref.file_section_count < n_tab)
		n_tab = fed_ref.file_section_count;
	nlg_file_t *files = CALLOC (n_tab + 1, sizeof (*files));
	if (!files)
	{
		FREE (tab);
		goto done;
	}
	uint n_files = 0;
	for (uint i = 0; i < n_tab; i++)
	{
		if (tab[i].type != 0x1301 || i + 1 >= n_tab)
			continue;
		nlg_chunk_t *body = &tab[i + 1];
		// Header payload: hash type + path hash.
		const u8 *hbuf = 0;
		if (variant == NLG_FEDFORCE)
			hbuf = nlg_slice (bufs, 70, NLGChunkBlockIndex (tab[i].flags), tab[i].offset, 8);
		else if (variant == NLG_LM3)
			hbuf = nlg_slice (bufs, 70, lm3_head_buf (body->type), tab[i].offset, 8);
		else
		{
			if (tab[i].size != 8)
			{
				i++;
				continue;
			}
			hbuf = nlg_slice (bufs, 70, 2, tab[i].offset, 8);
		}
		if (!hbuf)
		{
			i++;
			continue;
		}
		nlg_file_t *f = &files[n_files++];
		f->type = body->type;
		f->hash_type = rd_le32 (hbuf);
		f->path_hash = rd_le32 (hbuf + 4);
		f->has_children = nlg_file_has_children (variant, body->flags);
		f->child_start = body->offset;
		f->child_count = body->size;
		if (!f->has_children)
		{
			uint sz = 0;
			const u8 *d = 0;
			if (variant == NLG_FEDFORCE)
				d = nlg_slice (
					bufs, 70, NLGChunkBlockIndex (body->flags), body->offset, body->size);
			else if (variant == NLG_LM3)
				d = nlg_slice (bufs, 70, lm3_leaf_buf (f->type), body->offset, body->size);
			else
				d = nlg_slice (bufs, 70, 3, body->offset, body->size);
			(void)sz;
			f->data = d;
			f->data_size = d ? body->size : 0;
		}
		i++; // bodies always follow their headers
	}

	// ---- 3. texture hash set (material diffuse resolution) ----
	u32 *tex_hashes = 0;
	uint n_tex_hashes = 0, cap_tex = 0;
	for (uint fi = 0; fi < n_files; fi++)
	{
		if (files[fi].type != 0xB500)
			continue;
		uint hs = 0;
		const u8 *hd = 0;
		if (files[fi].has_children)
			hd = nlg_find_child (variant, tab, n_tab, files[fi].child_start, files[fi].child_count,
				0xB501, bufs, 70, &hs);
		else if (files[fi].data_size >= 16)
		{
			hd = files[fi].data;
			hs = files[fi].data_size;
		}
		if (!hd || hs < 16)
			continue;
		u32 th = (variant == NLG_LM3) ? rd_le32 (hd) : rd_le32 (hd + 4);
		bool dup = false;
		for (uint k = 0; k < n_tex_hashes; k++)
			if (tex_hashes[k] == th)
			{
				dup = true;
				break;
			}
		if (!dup)
		{
			if (n_tex_hashes >= cap_tex)
			{
				uint nc = cap_tex ? cap_tex * 2 : 64;
				u32 *nn = REALLOC (tex_hashes, nc * sizeof (*nn));
				if (!nn)
					break;
				tex_hashes = nn;
				cap_tex = nc;
			}
			tex_hashes[n_tex_hashes++] = th;
		}
	}
	nlg_texset_t ts = { tex_hashes, n_tex_hashes };

	// ---- 4. per-file export ----
	for (uint fi = 0; fi < n_files; fi++)
	{
		nlg_file_t *f = &files[fi];
		char base[160];
		nlg_base_name (base, sizeof (base), f->path_hash, fi);
		char path[PATH_MAX];
		u16 ver = (variant == NLG_LM3) ? 3 : (variant == NLG_LM2 ? 2 : 1);
		if (f->type == 0xB000 && f->has_children)
		{
			uint s;
			const u8 *c001 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB001, bufs, 70, &s);
			uint z001 = c001 ? s : 0;
			const u8 *c002 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB002, bufs, 70, &s);
			uint z002 = c002 ? s : 0;
			const u8 *c003 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB003, bufs, 70, &s);
			uint z003 = c003 ? s : 0;
			const u8 *c004 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB004, bufs, 70, &s);
			uint z004 = c004 ? s : 0;
			const u8 *c005 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB005, bufs, 70, &s);
			uint z005 = c005 ? s : 0;
			const u8 *c006 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB006, bufs, 70, &s);
			uint z006 = c006 ? s : 0;
			const u8 *c007 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB007, bufs, 70, &s);
			uint z007 = c007 ? s : 0;
			const u8 *c008 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB008, bufs, 70, &s);
			uint z008 = c008 ? s : 0;
			const u8 *c009 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB009, bufs, 70, &s);
			uint z009 = c009 ? s : 0;
			if (!c002 || !c003 || !c004 || !c005)
				continue;
			// Sibling skeleton chunks sharing the model hash.
			const u8 *g101 = 0, *g102 = 0, *g103 = 0, *g104 = 0, *g105 = 0, *g106 = 0;
			uint z101 = 0, z102 = 0, z103 = 0, z104 = 0, z105 = 0, z106 = 0;
			for (uint sj = 0; sj < n_files; sj++)
			{
				if (files[sj].type != 0x7100 || files[sj].path_hash != f->path_hash
					|| !files[sj].has_children)
					continue;
				g101 = nlg_find_child (variant, tab, n_tab, files[sj].child_start,
					files[sj].child_count, 0x7101, bufs, 70, &z101);
				g102 = nlg_find_child (variant, tab, n_tab, files[sj].child_start,
					files[sj].child_count, 0x7102, bufs, 70, &z102);
				g103 = nlg_find_child (variant, tab, n_tab, files[sj].child_start,
					files[sj].child_count, 0x7103, bufs, 70, &z103);
				g104 = nlg_find_child (variant, tab, n_tab, files[sj].child_start,
					files[sj].child_count, 0x7104, bufs, 70, &z104);
				g105 = nlg_find_child (variant, tab, n_tab, files[sj].child_start,
					files[sj].child_count, 0x7105, bufs, 70, &z105);
				g106 = nlg_find_child (variant, tab, n_tab, files[sj].child_start,
					files[sj].child_count, 0x7106, bufs, 70, &z106);
				break;
			}
			u8 *fedm = 0;
			uint fedm_size = 0;
			if (nlg_build_fedm (&fedm, &fedm_size, ver, c008, z008, c009, z009, c001, z001, c003,
					z003, c004, z004, c005, z005, c006, z006, c007, z007, c002, z002, g101, z101,
					g102, z102, g103, z103, g104, z104, g105, z105, g106, z106)
				!= ERR_OK)
				continue;
			snprintf (path, sizeof (path), "%s/%s_model.fedmodel", dest, base);
			nlg_save (path, fedm, fedm_size);
			model_t *model = ParseNLGModel (fedm, fedm_size, &ts);
			FREE (fedm);
			if (model)
			{
				snprintf (path, sizeof (path), "%s/%s_model.fedmodel.glb", dest, base);
				if (!testmode)
					ExportModelToGLB (model, path);
				FreeModel (model);
				if (verbose >= 0)
					fprintf (stdlog, "%sEXTRACT NLG-MODEL:%08X -> %s\n", verbose > 0 ? "\n" : "",
						f->path_hash, path);
			}
		}
		else if (f->type == 0xB500 && f->has_children)
		{
			uint hs = 0, ps = 0;
			const u8 *hd = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB501, bufs, 70, &hs);
			const u8 *pd = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0xB502, bufs, 70, &ps);
			if (!hd || hs < 16 || !pd || !ps)
				continue;
			u32 tex_hash = 0, w = 0, h = 0, fmt = 0, image_size = 0;
			if (variant == NLG_LM3)
			{
				tex_hash = rd_le32 (hd);
				w = rd_le16 (hd + 4);
				h = rd_le16 (hd + 6);
				fmt = hd[12];
				image_size = ps;
			}
			else
			{
				if (hs < 48)
					continue;
				image_size = rd_le32 (hd);
				tex_hash = rd_le32 (hd + 4);
				w = rd_le16 (hd + 16);
				h = rd_le16 (hd + 18);
				fmt = hd[44];
				if (!image_size || image_size > ps)
					image_size = ps;
			}
			if (!w || !h || w > 16384 || h > 16384)
				continue;
			if (!tex_hash)
				tex_hash = f->path_hash;
			u8 *fedt = 0;
			uint fedt_size = 0;
			if (nlg_build_fedt (&fedt, &fedt_size, ver, fmt, w, h, tex_hash, pd, image_size)
				!= ERR_OK)
				continue;
			snprintf (path, sizeof (path), "%s/nlg_%08X_tex.fedtex", dest, tex_hash);
			nlg_save (path, fedt, fedt_size);
			u8 *rgba = 0;
			uint rw = 0, rh = 0;
			if (DecodeNLGTexture (&rgba, &rw, &rh, fedt, fedt_size) == ERR_OK && rgba)
			{
				snprintf (path, sizeof (path), "%s/nlg_%08X_tex.fedtex.png", dest, tex_hash);
				if (!testmode)
					SaveDecodedRGBAToPNG (rgba, rw, rh, &le_func, path, 0, true);
				else
					FREE (rgba);
				if (verbose >= 0)
					fprintf (stdlog, "%sEXTRACT NLG-TEXTURE:%08X -> %s\n", verbose > 0 ? "\n" : "",
						tex_hash, path);
			}
			FREE (fedt);
		}
		else if (f->type == 0x7100 && f->has_children)
		{
			// Owned by a model sibling above? Then skip the standalone.
			bool owned = false;
			for (uint sj = 0; sj < n_files; sj++)
				if (files[sj].type == 0xB000 && files[sj].path_hash == f->path_hash)
				{
					owned = true;
					break;
				}
			if (owned)
				continue;
			uint z101 = 0, z102 = 0, z103 = 0, z104 = 0, z105 = 0, z106 = 0;
			const u8 *g101 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0x7101, bufs, 70, &z101);
			const u8 *g102 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0x7102, bufs, 70, &z102);
			const u8 *g103 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0x7103, bufs, 70, &z103);
			const u8 *g104 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0x7104, bufs, 70, &z104);
			const u8 *g105 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0x7105, bufs, 70, &z105);
			const u8 *g106 = nlg_find_child (
				variant, tab, n_tab, f->child_start, f->child_count, 0x7106, bufs, 70, &z106);
			if (!g101 || !g102 || !g103)
				continue;
			u8 *feds = 0;
			uint feds_size = 0;
			if (nlg_build_feds (&feds, &feds_size, ver, g101, z101, g102, z102, g103, z103, g104,
					z104, g105, z105, g106, z106)
				!= ERR_OK)
				continue;
			snprintf (path, sizeof (path), "%s/%s_skel.fedskel", dest, base);
			nlg_save (path, feds, feds_size);
			FREE (feds);
		}
		else if (f->type == 0x7000)
		{
			// Animation blob: leaf payload, else concatenated children.
			const u8 *blob = f->data;
			uint bs = f->data_size;
			u8 *cat = 0;
			if (f->has_children)
			{
				size_t total = 0;
				for (uint c = 0; c < f->child_count; c++)
				{
					uint ti = f->child_start + c, sz = 0;
					if (ti < n_tab)
						nlg_child_data (variant, tab, ti, bufs, 70, &sz);
					total += sz;
				}
				if (!total || total > NLG_MAX_BLOCK)
					continue;
				cat = MALLOC (total);
				if (!cat)
					continue;
				size_t p = 0;
				for (uint c = 0; c < f->child_count; c++)
				{
					uint ti = f->child_start + c, sz = 0;
					const u8 *d = 0;
					if (ti < n_tab)
						d = nlg_child_data (variant, tab, ti, bufs, 70, &sz);
					if (d && sz)
					{
						memcpy (cat + p, d, sz);
						p += sz;
					}
				}
				blob = cat;
				bs = (uint)p;
			}
			if (blob && bs >= 16)
			{
				u8 *txt = 0;
				uint txt_size = 0;
				if (nlg_dump_anim (&txt, &txt_size, blob, bs, variant, f->path_hash) == ERR_OK)
				{
					snprintf (path, sizeof (path), "%s/%s_anim.txt", dest, base);
					nlg_save (path, txt, txt_size);
					FREE (txt);
				}
			}
			FREE (cat);
		}
		else if (f->type == 0x5000 || f->type == 0x6500)
		{
			if (!f->has_children)
			{
				snprintf (path, sizeof (path), "%s/%s_type_%04X.bin", dest, base, f->type);
				nlg_save (path, f->data, f->data_size);
				continue;
			}
			u8 *txt = 0;
			uint txt_size = 0;
			if (nlg_dump_script (&txt, &txt_size, tab, n_tab, f->child_start, f->child_count,
					variant, bufs, 70, files, n_files, f->path_hash)
				== ERR_OK)
			{
				snprintf (path, sizeof (path), "%s/%s_script.txt", dest, base);
				nlg_save (path, txt, txt_size);
				FREE (txt);
			}
		}
		else if (f->type == 0x7010)
		{
			// Font payload: concatenated children, else leaf.
			const u8 *payload = f->data;
			uint psize = f->data_size;
			u8 *cat = 0;
			if (f->has_children)
			{
				uint sz = 0;
				const u8 *d711 = nlg_find_child (
					variant, tab, n_tab, f->child_start, f->child_count, 0x7011, bufs, 70, &sz);
				if (d711 && sz)
				{
					payload = d711;
					psize = sz;
				}
				else
				{
					size_t total = 0;
					for (uint c = 0; c < f->child_count; c++)
					{
						uint ti = f->child_start + c, s2 = 0;
						if (ti < n_tab)
							nlg_child_data (variant, tab, ti, bufs, 70, &s2);
						total += s2;
					}
					if (total && total <= NLG_MAX_BLOCK)
					{
						cat = MALLOC (total);
						if (cat)
						{
							size_t p = 0;
							for (uint c = 0; c < f->child_count; c++)
							{
								uint ti = f->child_start + c, s2 = 0;
								const u8 *d2 = 0;
								if (ti < n_tab)
									d2 = nlg_child_data (variant, tab, ti, bufs, 70, &s2);
								if (d2 && s2)
								{
									memcpy (cat + p, d2, s2);
									p += s2;
								}
							}
							payload = cat;
							psize = (uint)p;
						}
					}
				}
			}
			if (payload && psize)
			{
				if (IsFedForceFont (payload, psize))
				{
					snprintf (path, sizeof (path), "%s/%s_font.nlgfont", dest, base);
					nlg_save (path, payload, psize);
				}
				else
				{
					snprintf (path, sizeof (path), "%s/%s_type_7010.bin", dest, base);
					nlg_save (path, payload, psize);
				}
			}
			FREE (cat);
		}
		else if (!f->has_children && f->data && f->data_size)
		{
			if (f->type == 0x7020 && IsFedForceNLOC (f->data, f->data_size))
			{
				snprintf (path, sizeof (path), "%s/%s_type_%04X.bin", dest, base, f->type);
				nlg_save (path, f->data, f->data_size);
				fed_nloc_msg_t *msgs = 0;
				uint nmsg = 0;
				if (ScanFedForceNLOC (f->data, f->data_size, &msgs, &nmsg, 0) == ERR_OK && nmsg)
				{
					u8 *txt = 0;
					uint txt_size = 0;
					if (FedForceNLOCToText (&txt, &txt_size, msgs, nmsg) == ERR_OK)
					{
						snprintf (path, sizeof (path), "%s/%s_type_%04X.txt", dest, base, f->type);
						nlg_save (path, txt, txt_size);
						FREE (txt);
					}
					FreeFedForceNLOC (msgs, nmsg);
				}
			}
			else if (f->type == 0x0031 && nlg_is_printable (f->data, f->data_size))
			{
				snprintf (path, sizeof (path), "%s/%s_config.txt", dest, base);
				nlg_save (path, f->data, f->data_size);
			}
			else
			{
				snprintf (path, sizeof (path), "%s/%s_type_%04X.bin", dest, base, f->type);
				nlg_save (path, f->data, f->data_size);
			}
		}
		else if (f->has_children)
		{
			// Parent with no typed handler (bundles, physics, shaders,
			// audio, cutscenes, UI layouts...): one raw dump per child.
			for (uint c = 0; c < f->child_count; c++)
			{
				uint ti = f->child_start + c, sz = 0;
				const u8 *d = 0;
				if (ti < n_tab)
					d = nlg_child_data (variant, tab, ti, bufs, 70, &sz);
				if (!d || !sz)
					continue;
				snprintf (path, sizeof (path), "%s/%s_chunk_%04X.bin", dest, base, tab[ti].type);
				nlg_save (path, d, sz);
			}
		}
	}
	FREE (tex_hashes);
	FREE (files);
	FREE (tab);
done:
	nlg_free_bufs (bufs, 70);
	FreeNLGDict (bl, strings, n_strings);
	return ERR_OK;
}

/*__APPEND9__*/

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Mario Strikers SANIM text dumps (probes live in lib-nlg-probe.c).
//-----------------------------------------------------------------------------

typedef struct
{
	char name[128];
	u32 tracks, frames;
	u32 p1n, p2n, p3n, p4n;
	uint rot_keys, tr_keys;
	uint anim_no;
	bool in_anim;
} sanim_state_t;

static void nlg_sanim_flush (sanim_state_t *st, char **out, size_t *len, size_t *cap)
{
	if (!st->in_anim)
		return;
	int n = snprintf (*out + *len, *cap - *len,
		"animation %u name \"%s\" frames %u tracks %u rot_keys %u tr_keys %u params %u/%u/%u/%u\n",
		st->anim_no, st->name, st->frames, st->tracks, st->rot_keys, st->tr_keys, st->p1n, st->p2n,
		st->p3n, st->p4n);
	if (n < 0)
		return;
	*len += (size_t)n;
	if (*len + 512 > *cap)
	{
		*cap *= 2;
		char *no = REALLOC (*out, *cap);
		if (no)
			*out = no;
	}
	st->anim_no++;
	st->in_anim = false;
}

static enumError nlg_dump_sanim_rec (
	const u8 *d, uint size, char **out, size_t *len, size_t *cap, int depth, sanim_state_t *st)
{
	uint p = 0;
	while (p + 8 <= size)
	{
		u16 magic = rd_be16 (d + p + 2);
		u32 sz = rd_be32 (d + p + 4);
		const u8 *pl = d + p + 8;
		switch (magic)
		{
			case 0x7000:
				nlg_sanim_flush (st, out, len, cap);
				memset (st->name, 0, sizeof (st->name));
				st->tracks = st->frames = 0;
				st->p1n = st->p2n = st->p3n = st->p4n = 0;
				st->rot_keys = st->tr_keys = 0;
				st->in_anim = true;
				if (sz && nlg_dump_sanim_rec (pl, sz, out, len, cap, depth + 1, st))
					return ERR_CANT_CREATE;
				break;
			case 0x7001:
				if (sz >= 16)
				{
					st->frames = rd_be32 (pl + 8);
					st->tracks = rd_be32 (pl + 12);
				}
				break;
			case 0x7002:
			{
				uint n = sz < sizeof (st->name) - 1 ? sz : sizeof (st->name) - 1;
				memcpy (st->name, pl, n);
				st->name[n] = 0;
			}
			break;
			case 0x7003:
				st->p1n = sz / 4;
				break;
			case 0x7004:
				st->p2n = sz / 4;
				break;
			case 0x7005:
				st->p3n = sz / 4;
				break;
			case 0x7006:
				st->p4n = sz / 4;
				break;
			case 0x7100:
				if (sz && nlg_dump_sanim_rec (pl, sz, out, len, cap, depth + 1, st))
					return ERR_CANT_CREATE;
				break;
			case 0x7101:
				st->rot_keys += sz / 6;
				break;
			case 0x7102:
				st->tr_keys += sz / 12;
				break;
			default:
				break;
		}
		p += 8 + sz;
		p = (p + 3) & ~3u;
		(void)depth;
	}
	if (depth == 0)
		nlg_sanim_flush (st, out, len, cap);
	return ERR_OK;
}

enumError ExtractSANIMArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".sanim") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (!IsSANIM (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SANIM:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (!testmode)
	{
		size_t cap = 1024, len = 0;
		char *out = MALLOC (cap);
		if (out)
		{
			len += snprintf (out + len, cap - len, "SANIM %s\n", arg);
			{
				sanim_state_t st;
				memset (&st, 0, sizeof (st));
				if (!nlg_dump_sanim_rec (raw, (uint)raw_size, &out, &len, &cap, 0, &st))
				{
					char path[PATH_MAX];
					snprintf (path, sizeof (path), "%s/anim.txt", dest);
					SaveFile (path, 0, 0, out, (uint)len, 0);
				}
			}
			FREE (out);
		}
	}
	FREE (raw);
	(void)depth;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Buffer assembly: decompressed block buffers per variant.
//-----------------------------------------------------------------------------

/*__APPEND__*/
