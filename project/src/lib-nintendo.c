#include <zlib.h>
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-quicklz.h"
#include "lib-bflyt.h"
#include "lib-bntx.h"
#include "lib-gtx.h"

#define NFMT_MAX_OUTPUT (512u << 20)

static inline u32 rd_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline u32 rd_le32 (const u8 *p)
{
	return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}
static inline u16 rd_be16 (const u8 *p)
{
	return (u16)p[0] << 8 | p[1];
}
static inline u16 rd_le16 (const u8 *p)
{
	return (u16)p[1] << 8 | p[0];
}
static inline void wr_be16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = v;
}
static inline void wr_le16 (u8 *p, u16 v)
{
	p[0] = v;
	p[1] = v >> 8;
}
static inline void wr_be32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}
static inline void wr_le32 (u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

ccp GetNintendoFormatName (nfmt_type_t type)
{
	static const ccp tab[] = { "UNKNOWN", "DSB", "TPL", "STPL", "SARC", "LZ10", "LZ11", "HUFF4",
		"HUFF8", "RL", "ASH0", "Yay0", "LZH8", "BFLIM", "BCLIM", "NUTEXB", "BNR", "NCGR", "NCLR", "NCER",
		"NANR", "BRFNT", "BRFNA", "BCFNT", "BRLAN", "BRLYT", "BFLAN", "BFLYT", "BCLAN", "BCLYT",
		"PLT0", "MSBT", "BCRES", "BFRES", "BNTX", "GFA", "BCH", "QuickLZ", "PAC", "RNC", "romc",
		"PSDK", "AT7", "CTPK", "BYML", "NARC", "NSCR", "FZIP", "JARC", "jCMP", "BFMA", "Zlib", "MVDK",
		"VLX", "PuCrunch", "LZX", "Diff8", "Diff16", "NSBTX", "NFTR", "BNFR", "BNLL", "BNCL", "BNBL",
		"LZOvl", "ALAR", "DARC", "SADL" };
	return type < sizeof (tab) / sizeof (*tab) ? tab[type] : "UNKNOWN";
}

static nfmt_info_t make_info (nfmt_type_t type, bool be, bool compressed, u32 size)
{
	nfmt_info_t inf = { type, be, compressed, size };
	return inf;
}

nfmt_info_t DetectNintendoFormat (const void *vdata, uint size, ccp filename)
{
	const u8 *d = vdata;
	if (!d || !size)
		return make_info (NFMT_UNKNOWN, true, false, 0);
	if (size >= 4)
	{
		ccp ext = filename ? strrchr (filename, '.') : 0;
		if (ext && !strcasecmp (ext, ".romc") && d[0] && !d[1] && !d[2] && (d[3] & 3) == 1)
			return make_info (NFMT_ROMC, true, true, (u32)d[0] * 4 * 1024 * 1024);
		const u32 magic = rd_be32 (d);
		if (!memcmp (d, "jCMP", 4) || !memcmp (d, "JCMP", 4))
			return make_info (NFMT_JCMP, true, true, size >= 8 ? rd_be32 (d + 4) : 0);
		if (!memcmp (d, "jARC", 4) || !memcmp (d, "JARC", 4))
			return make_info (NFMT_JARC, true, false, 0);
		if (!memcmp (d, "FZIP", 4))
			return make_info (NFMT_FZIP, true, true, size >= 8 ? rd_be32 (d + 4) : 0);
		if (size >= 6 && IsZlib (d, size) >= 0 && ext
			&& (!strcasecmp (ext, ".zlib") || !strcasecmp (ext, ".deflate")
				|| !strcasecmp (ext, ".arc")))
			return make_info (NFMT_ZLIB, false, true, 0);
		if (!memcmp (d, "TXTR", 4))
			return make_info (NFMT_DSB, true, false, 0);
		if (magic == 0x0020af30)
			return make_info (NFMT_TPL, true, false, 0);
		if (!memcmp (d, "SARC", 4))
		{
			if (ext && !strcasecmp (ext, ".bfma"))
				return make_info (NFMT_BFMA, size >= 8 && d[6] == 0xfe, false, 0);
			return make_info (NFMT_SARC, size >= 8 && d[6] == 0xfe, false, 0);
		}
		if (!memcmp (d, "ASH0", 4))
			return make_info (NFMT_ASH0, true, true, size >= 8 ? rd_be32 (d + 4) : 0);
		if (!memcmp (d, "Yay0", 4))
			return make_info (NFMT_YAY0, true, true, size >= 8 ? rd_be32 (d + 4) : 0);
		if (!memcmp (d, "BNR1", 4) || !memcmp (d, "BNR2", 4))
			return make_info (NFMT_BNR, true, false, 0);
		if (!memcmp (d, "RGCN", 4))
			return make_info (NFMT_NCGR, true, false, 0);
		if (!memcmp (d, "RLCN", 4))
			return make_info (NFMT_NCLR, true, false, 0);
		if (!memcmp (d, "RECN", 4))
			return make_info (NFMT_NCER, true, false, 0);
		if (!memcmp (d, "RNAN", 4))
			return make_info (NFMT_NANR, true, false, 0);
		if (!memcmp (d, "RCSN", 4))
			return make_info (NFMT_NSCR, true, false, 0);
		if (!memcmp (d, "BTX0", 4) || !memcmp (d, "BMD0", 4))
			return make_info (NFMT_NSBTX, true, false, 0);
		if (!memcmp (d, "RTNF", 4) || !memcmp (d, "FNTR", 4))
			return make_info (NFMT_NFTR, true, false, 0);
		if (!memcmp (d, "RNFB", 4) || !memcmp (d, "BNFR", 4))
			return make_info (NFMT_BNFR, true, false, 0);
		if (!memcmp (d, "LLNB", 4) || !memcmp (d, "BNLL", 4))
			return make_info (NFMT_BNLL, true, false, 0);
		if (!memcmp (d, "LCNB", 4) || !memcmp (d, "BNCL", 4))
			return make_info (NFMT_BNCL, true, false, 0);
		if (!memcmp (d, "LBNB", 4) || !memcmp (d, "BNBL", 4))
			return make_info (NFMT_BNBL, true, false, 0);
		if (!memcmp (d, "ALAR", 4))
			return make_info (NFMT_ALAR, true, false, 0);
		if (!memcmp (d, "DARC", 4))
			return make_info (NFMT_DARC, true, false, 0);
		if (!memcmp (d, "SADL", 4))
			return make_info (NFMT_SADL, true, false, 0);
		if (CxIsCompressedLZOvl (d, size))
			return make_info (NFMT_LZOVL, false, true, 0);

		// WarioWare: D.I.Y. Showcase / "WarioWare Snapped!" (DSiWare, NTR-KUWE)
		// wraps every Nitro graphics resource (NCGR/NCLR/NCER/NANR) it stores
		// in a 4-byte little-endian size-prefix record BEFORE the resource's
		// own RGCN/RLCN/RECN/RNAN magic: byte 0 is always 0x00, bytes 1-2 are
		// a u16 LE holding (payload size), byte 3 is always 0x00, and the
		// resource itself -- magic and all -- starts at offset 4. Verified
		// against real assets (Style/StyleO.NCLR.bin, Style/Style_Head.NCGR.bin,
		// Style/Style_Head.NCER.bin, Style/Style_2P_01.NANR.bin,
		// Game/WarningB.NCLR.bin from the retail NTR-KUWE-USA ROM) after
		// LZ11-decompressing them: in every case bytes[1..2] as LE16 equalled
		// (decompressed size - 4) exactly, and the magic sat at offset 4.
		// This wrapper is why WarioWare Snapped's own assets used to report
		// as NFMT_UNKNOWN ("?") from `wszst FILETYPE` -- the auto-detector
		// only ever looked for the magic at offset 0.
		if (size >= 12 && !d[0] && !d[3])
		{
			const u32 declared = (u32)d[1] | (u32)d[2] << 8;
			if (declared == size - 4)
			{
				nfmt_type_t wrapped = NFMT_UNKNOWN;
				if (!memcmp (d + 4, "RGCN", 4))
					wrapped = NFMT_NCGR;
				else if (!memcmp (d + 4, "RLCN", 4))
					wrapped = NFMT_NCLR;
				else if (!memcmp (d + 4, "RECN", 4))
					wrapped = NFMT_NCER;
				else if (!memcmp (d + 4, "RNAN", 4))
					wrapped = NFMT_NANR;
				if (wrapped != NFMT_UNKNOWN)
				{
					nfmt_info_t inf = make_info (wrapped, true, false, 0);
					inf.payload_offset = 4;
					return inf;
				}
			}
		}
		if (!memcmp (d, "RFNT", 4))
			return make_info (NFMT_BRFNT, true, false, 0);
		if (!memcmp (d, "RFNA", 4))
			return make_info (NFMT_BRFNA, true, false, 0);
		// BCFNT (3DS) and BFFNT (Wii U) share the exact same "CFNT" container
		// and, for the common fontType==1 case, the exact same TGLP glyph-sheet
		// layout as Wii's RFNT -- verified against NintyFont's from-source
		// CFNT/FINF/TGLP reader (hadashisora/NintyFont). Endianness is
		// determined per-file from the BOM at +4, not from the magic, since
		// 3DS files are little endian and Wii U ones are big endian.
		if (!memcmp (d, "CFNT", 4))
			return make_info (NFMT_BCFNT, true, false, 0);
		if (!memcmp (d, "FFNT", 4))
			return make_info (
				NFMT_BCFNT, true, false, 0); // Wii U: real, different magic, same family
		if (!memcmp (d, "RLAN", 4))
			return make_info (NFMT_BRLAN, true, false, 0);
		if (!memcmp (d, "RLYT", 4))
			return make_info (NFMT_BRLYT, true, false, 0);
		if (!memcmp (d, "FLAN", 4))
			return make_info (NFMT_BFLAN, true, false, 0);
		if (!memcmp (d, "FLYT", 4))
			return make_info (NFMT_BFLYT, true, false, 0);
		if (!memcmp (d, "CLAN", 4))
			return make_info (NFMT_BCLAN, true, false, 0);
		if (!memcmp (d, "CLYT", 4))
			return make_info (NFMT_BCLYT, true, false, 0);
		if (!memcmp (d, "PLT0", 4))
			return make_info (NFMT_PLT0, true, false, 0);
		if (size >= 8 && !memcmp (d, "MsgStdBn", 8))
			return make_info (NFMT_MSBT, true, false, 0);
		if (!memcmp (d, "CGFX", 4))
			return make_info (NFMT_BCRES, true, false, 0);
		if (!memcmp (d, "FRES", 4))
			return make_info (NFMT_BFRES, true, false, 0);
		// BNTX (Switch texture container). Full pixel decode is implemented
		// in lib-bntx.c (DecodeBNTX_RGBA, wired into wimgt DECODE) -- see
		// that file's header comment for what's verified and how.
		if (!memcmp (d, "BNTX", 4))
			return make_info (NFMT_BNTX, false, false, 0);
		// GFA: Good-Feel archive (Wario Land: Shake It!, Kirby's Epic Yarn)
		if (!memcmp (d, "GFAC", 4))
			return make_info (NFMT_GFA, false, true, 0);
		// BCH: the 3DS CTR H3D container. Its magic is "BCH\0" -- it is a
		// different format from CGFX/BCRES, not a variant of it.
		if (!memcmp (d, "BCH\0", 4))
			return make_info (NFMT_BCH, false, false, 0);
		// PAC: Brawl's flat archive ("ARC\0" magic, per BrawlLib's
		// ARCHeader.Tag). Uncompressed, no name table.
		if (!memcmp (d, "ARC\0", 4))
			return make_info (NFMT_PAC, false, false, 0);

		// AT7 (Pokémon Mystery Dungeon WiiWare compressed stream)
		if (!memcmp (d, "AT7P", 4) || !memcmp (d, "AT7X", 4))
			return make_info (NFMT_AT7, false, true, 0);

		// CTPK (CTR Texture Package / 3DS texture container)
		if (!memcmp (d, "CTPK", 4))
			return make_info (NFMT_CTPK, false, false, 0);

		// BYML / BYAML (Binary YAML, 3DS / Wii U / Switch)
		if (size >= 16 && (!memcmp (d, "BY", 2) || !memcmp (d, "YB", 2)))
		{
			const bool be = (d[0] == 'B' && d[1] == 'Y');
			const u16 ver = be ? rd_be16 (d + 2) : rd_le16 (d + 2);
			if (ver >= 1 && ver <= 4)
				return make_info (NFMT_BYML, be, false, 0);
		}

		// NARC (Nitro Archive, DS / 3DS)
		if (size >= 16 && (!memcmp (d, "NARC", 4) || !memcmp (d, "CRAN", 4)))
			return make_info (NFMT_NARC, size >= 6 && d[4] == 0xfe, false, 0);

		// RNC (Rob Northen Compression, "RNC" + version 1..3) and PSDK
		// (Prosonic data, "PSDK") appear on GBA/DS homebrew and some
		// devkit-built payloads. RNC1/2 are supported; PSDK is recognized so
		// extraction reports an explicit unsupported codec.
		if (size >= 4 && !memcmp (d, "RNC", 3) && d[3] >= 1 && d[3] <= 3)
			return make_info (NFMT_RNC, true, true, 0);
		if (size >= 4 && !memcmp (d, "PSDK", 4))
			return make_info (NFMT_PSDK, false, true, 0);

		// Strong footer magics must be tested BEFORE the single-byte
		// compression heuristics below. BFLIM/BCLIM keep their magic in a
		// trailer, so their *payload* starts at offset 0 -- and compressed
		// texture data very often begins with 0x10/0x11/0x24/0x28/0x30/0x40,
		// exactly the bytes those heuristics key on. Testing the heuristics
		// first silently stole real BFLIMs (13 of 689 in a real corpus) and
		// reported them as LZ10/LZ11/LZH8 streams.
		if (size >= 0x28 && !memcmp (d + size - 0x28, "FLIM", 4))
			return make_info (NFMT_BFLIM, true, false, 0);
		if (size >= 0x28 && !memcmp (d + size - 0x28, "CLIM", 4))
			return make_info (NFMT_BCLIM, true, false, 0);

		// NUTEXB (Switch texture wrapper, e.g. Smash Ultimate): another
		// trailing-footer format, magic "XET" 7 bytes before EOF (3-byte
		// magic + 4-byte little-endian version), same reasoning as the
		// BFLIM/BCLIM check just above -- test it before the single-byte
		// compression heuristics so real texture payloads that happen to
		// start with 0x10/0x11/etc. aren't stolen by them. 0x70 (112) is
		// the fixed trailer size, so anything shorter can't be one.
		if (size >= 0x70 && !memcmp (d + size - 7, "XET", 3))
			return make_info (NFMT_NUTEXB, false, false, 0);

		// QuickLZ is checked before the single-byte heuristics: its test is
		// exact (the header's own recorded compressed length must equal the
		// buffer) whereas the tests below are one-byte guesses.
		if (IsQuickLZ (d, size))
			return make_info (NFMT_QLZ, false, true, 0);
		// Mario vs. Donkey Kong custom deflate (header low-2 bits == 2)
		if (size >= 4 && CxIsCompressedMvDK (d, size))
			return make_info (NFMT_MVDK, false, true, *(u32*)d >> 2);
		if (size >= 4 && CxIsCompressedVlx (d, size))
			return make_info (NFMT_VLX, false, true, 0);
		if (size >= 8 && CxIsCompressedPuCrunch (d, size))
			return make_info (NFMT_PUCRUNCH, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if (d[0] == 0x19 && size >= 4 && CxIsCompressedLZX (d, size))
			return make_info (NFMT_LZX, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if (d[0] == 0x80 && size >= 4)
			return make_info (NFMT_DIFF8, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if (d[0] == 0x81 && size >= 4)
			return make_info (NFMT_DIFF16, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if ((d[0] == 0x10 || d[0] == 0x11) && size >= 4)
			return make_info (d[0] == 0x10 ? NFMT_LZ10 : NFMT_LZ11, false, true,
				(u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		// Some BRRES-family members carry a short, unrecognized tag
		// immediately before an otherwise standard LZ10/LZ11 stream --
		// AquaSpace's (WiiWare) BRRES members are prefixed with "CX00",
		// origin unknown, verified byte-for-byte against a real disc. Mirror
		// the LZH8 wrapped-stream fallback just below: skip the unrecognized
		// prefix and look for the real compression magic just past it,
		// instead of special-casing the wrapper tag by name at every caller.
		if (d[0] != 0x10 && d[0] != 0x11 && size >= 8 && (d[4] == 0x10 || d[4] == 0x11))
		{
			nfmt_info_t inf = make_info (d[4] == 0x10 ? NFMT_LZ10 : NFMT_LZ11, false, true,
				(u32)d[5] | (u32)d[6] << 8 | (u32)d[7] << 16);
			inf.payload_offset = 4;
			return inf;
		}
		if ((d[0] == 0x24 || d[0] == 0x28) && size >= 5)
			return make_info (d[0] == 0x24 ? NFMT_HUFF4 : NFMT_HUFF8, false, true,
				(u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if (d[0] == 0x30 && size >= 4)
			return make_info (NFMT_RL, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		// LZH8: 0x40 followed by a 24-bit LE size.  WarioWare Snapped wraps
		// the stream in a 4-byte LE size prefix, so 0x40 may sit at offset 4.
		if (d[0] == 0x40 && size >= 4)
			return make_info (NFMT_LZH8, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if (d[0] != 0x40 && size >= 8 && d[4] == 0x40)
			return make_info (NFMT_LZH8, false, true, (u32)d[5] | (u32)d[6] << 8 | (u32)d[7] << 16);
		// Camelot header: codec 1/2 plus a three-byte output size. The extension
		// check prevents random binary files from being called STPL.
		if ((d[0] == 1 || d[0] == 2) && filename
			&& (strstr (filename, ".stpl") || strstr (filename, ".camelot")))
			return make_info (NFMT_STPL, true, true, ((u32)d[1] << 16) | ((u32)d[2] << 8) | d[3]);
	}
	if (size >= 0x28 && !memcmp (d + size - 0x28, "FLIM", 4))
		return make_info (NFMT_BFLIM, true, false, 0);
	if (size >= 0x28 && !memcmp (d + size - 0x28, "CLIM", 4))
		return make_info (NFMT_BCLIM, true, false, 0);
	if (size >= 0x70 && !memcmp (d + size - 7, "XET", 3))
		return make_info (NFMT_NUTEXB, false, false, 0);
	return make_info (NFMT_UNKNOWN, true, false, 0);
}

static enumError alloc_output (u8 **dest, uint *dest_size, u32 size)
{
	if (!dest || !dest_size || !size || size > NFMT_MAX_OUTPUT)
		return EFBIG;
	*dest = MALLOC (size);
	if (!*dest)
		return ERR_CANT_CREATE;
	*dest_size = size;
	return ERR_OK;
}

typedef struct ash_bits_t
{
	const u8 *src;
	uint size, pos, word, used;
} ash_bits_t;

static bool ash_feed (ash_bits_t *br)
{
	if (br->pos > br->size - 4)
		return false;
	br->word = rd_be32 (br->src + br->pos);
	br->pos += 4;
	br->used = 0;
	return true;
}

static bool ash_init (ash_bits_t *br, const u8 *src, uint size, uint pos)
{
	if (!br || pos > size)
		return false;
	br->src = src;
	br->size = size;
	br->pos = pos;
	br->word = br->used = 0;
	return ash_feed (br);
}

static bool ash_read (ash_bits_t *br, uint n, uint *value)
{
	if (!n || n > 24 || !value)
		return false;
	uint val = 0;
	while (n--)
	{
		val = val << 1 | br->word >> 31;
		if (++br->used == 32)
		{
			if (!ash_feed (br))
				return false;
		}
		else
			br->word <<= 1;
	}
	*value = val;
	return true;
}

static bool ash_tree (ash_bits_t *br, uint width, uint *left, uint *right, uint *root)
{
	const uint max = 1u << width, cap = 2 * max - 1;
	uint work[2 * 2048], work_used = 0, nodes = 0, next = max;
	for (;;)
	{
		uint bit, value;
		if (!ash_read (br, 1, &bit))
			return false;
		if (bit)
		{
			if (work_used + 2 > sizeof (work) / sizeof (*work) || next >= cap)
				return false;
			work[work_used++] = next | 0x80000000u;
			work[work_used++] = next | 0x40000000u;
			nodes += 2;
			next++;
			continue;
		}
		if (!ash_read (br, width, &value) || value >= max)
			return false;
		*root = value;
		while (nodes)
		{
			const uint node = work[--work_used], index = node & 0x3fffffffu;
			if (index >= cap)
				return false;
			nodes--;
			if (node & 0x80000000u)
			{
				right[index] = *root;
				*root = index;
			}
			else
			{
				left[index] = *root;
				break;
			}
		}
		if (!nodes)
			return true;
	}
}

static bool ash_symbol (
	ash_bits_t *br, uint root, uint max, const uint *left, const uint *right, uint *value)
{
	uint sym = root;
	while (sym >= max)
	{
		uint bit;
		if (sym >= 2 * max - 1 || !ash_read (br, 1, &bit))
			return false;
		sym = bit ? right[sym] : left[sym];
	}
	*value = sym;
	return true;
}

// ASH0's distance-tree bit width is a build-time choice baked into the
// encoder, not a field in the file header -- confirmed against
// NinjaCheetah/ASH0-tools (Decompressor/main.c), a from-scratch clean-room
// ASH0 codec whose CLI exposes it as a manual `-d` flag defaulting to 11
// ("These work for ASH0 files found in the System Menu and Animal Crossing:
// City Folk. ASH0 files found in My Pokémon Ranch require setting the
// distance tree bits to 15 instead.") -- there is no header bit to switch
// on. Since a real file gives no way to know up front, try the common case
// first and fall back to the one confirmed exception on failure, rather
// than guess a detection rule with no evidence behind it.
static enumError DecodeASH0Try (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size, uint dist_bits)
{
	const uint out_size = rd_be32 (src + 4) & 0x00ffffff;
	const uint dist_start = rd_be32 (src + 8);
	enumError err = alloc_output (dest, dest_size, out_size);
	if (err)
		return err;
	ash_bits_t syms, dists;
	const uint sym_max = 1u << 9, dist_max = 1u << dist_bits;
	uint *sl = CALLOC (2 * sym_max - 1, sizeof (*sl)), *sr = CALLOC (2 * sym_max - 1, sizeof (*sr));
	uint *dl = CALLOC (2 * dist_max - 1, sizeof (*dl)),
		 *dr = CALLOC (2 * dist_max - 1, sizeof (*dr));
	uint sym_root = 0, dist_root = 0;
	if (!sl || !sr || !dl || !dr || !ash_init (&syms, src, src_size, 0x0c)
		|| !ash_init (&dists, src, src_size, dist_start) || !ash_tree (&syms, 9, sl, sr, &sym_root)
		|| !ash_tree (&dists, dist_bits, dl, dr, &dist_root))
		goto invalid;
	for (uint pos = 0; pos < out_size;)
	{
		uint sym;
		if (!ash_symbol (&syms, sym_root, sym_max, sl, sr, &sym))
			goto invalid;
		if (sym < 0x100)
			(*dest)[pos++] = sym;
		else
		{
			uint distance;
			const uint len = sym - 0x100 + 3;
			if (!ash_symbol (&dists, dist_root, dist_max, dl, dr, &distance) || distance >= pos
				|| len > out_size - pos)
				goto invalid;
			for (uint n = 0; n < len; n++)
				(*dest)[pos + n] = (*dest)[pos - distance - 1 + n];
			pos += len;
		}
	}
	FREE (sl);
	FREE (sr);
	FREE (dl);
	FREE (dr);
	return ERR_OK;
invalid:
	FREE (sl);
	FREE (sr);
	FREE (dl);
	FREE (dr);
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError DecodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 0x10 || memcmp (src, "ASH0", 4))
		return EINVAL;
	const uint out_size = rd_be32 (src + 4) & 0x00ffffff;
	const uint dist_start = rd_be32 (src + 8);
	if (!out_size || out_size > NFMT_MAX_OUTPUT || dist_start > src_size - 4)
		return EINVAL;

	enumError err = DecodeASH0Try (dest, dest_size, src, src_size, 11);
	if (err)
		err = DecodeASH0Try (dest, dest_size, src, src_size, 15);
	return err;
}

typedef struct ash_writer_t
{
	u8 *data;
	uint size, bitpos;
} ash_writer_t;

static bool ash_write (ash_writer_t *bw, uint value, uint n)
{
	if (!n || n > 24 || bw->bitpos > bw->size * 8 - n)
		return false;
	while (n--)
	{
		if (value & (1u << n))
			bw->data[bw->bitpos / 8] |= 0x80 >> (bw->bitpos & 7);
		bw->bitpos++;
	}
	return true;
}

static bool ash_write_symbol_tree (ash_writer_t *bw, uint depth, uint value)
{
	if (depth == 9)
		return ash_write (bw, 0, 1) && ash_write (bw, value, 9);
	return ash_write (bw, 1, 1) && ash_write_symbol_tree (bw, depth + 1, value << 1)
		&& ash_write_symbol_tree (bw, depth + 1, value << 1 | 1);
}

enumError EncodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	// A full 9-bit literal tree keeps this initial encoder simple and fully
	// interoperable. A future optimiser can replace it with a frequency tree
	// without changing the decoder or on-disk framing.
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;
	const u64 sym_bits = 5631ull + 9ull * src_size;
	const u64 sym_size = (sym_bits + 7) / 8, dist_off = 12 + ((sym_size + 3) & ~3ull);
	const u64 total = dist_off + 4;
	if (total > NFMT_MAX_OUTPUT || total > UINT_MAX)
		return EFBIG;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "ASH0", 4);
	wr_be32 (out + 4, src_size);
	wr_be32 (out + 8, dist_off);
	ash_writer_t bw = { out + 12, (uint)sym_size, 0 };
	if (!ash_write_symbol_tree (&bw, 0, 0))
		goto invalid_ash_encode;
	for (uint i = 0; i < src_size; i++)
		if (!ash_write (&bw, src[i], 9))
			goto invalid_ash_encode;
	bw.data = out + dist_off;
	bw.size = 4;
	bw.bitpos = 0;
	if (!ash_write (&bw, 0, 1) || !ash_write (&bw, 0, 11))
		goto invalid_ash_encode;
	*dest = out;
	*dest_size = total;
	return ERR_OK;
invalid_ash_encode:
	FREE (out);
	return EFBIG;
}

enumError DecodeCamelot (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 5 || (src[0] != 1 && src[0] != 2))
		return EINVAL;
	const u32 out_len = ((u32)src[1] << 16) | ((u32)src[2] << 8) | src[3];
	enumError err = alloc_output (dest, dest_size, out_len);
	if (err)
		return err;
	uint sp = 4, dp = 0;
	while (sp < src_size && dp < out_len)
	{
		const u8 flags = src[sp++];
		for (uint bit = 0; bit < 8 && dp < out_len; bit++)
		{
			if (flags & (0x80 >> bit))
			{
				if (sp + 2 > src_size)
					goto invalid;
				const u8 a = src[sp++], b = src[sp++];
				const uint back = ((uint)(a >> 4) << 8) | b;
				uint len = a & 15;
				if (!len)
				{
					if (sp >= src_size)
						goto invalid;
					len = src[sp++] + 17;
				}
				else
					len++;
				if (!back || len > out_len - dp)
					goto invalid;

				// Camelot's window is zero-filled before the first output byte.
				// Early references in real STPL headers deliberately use that area.
				while (len--)
				{
					(*dest)[dp] = back <= dp ? (*dest)[dp - back] : 0;
					dp++;
				}
			}
			else
			{
				if (sp >= src_size)
					goto invalid;
				(*dest)[dp++] = src[sp++];
			}
		}
	}
	if (dp == out_len)
		return ERR_OK;
invalid:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeCamelot (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !src)
		return EINVAL;
	if (src_size > 0x00FFFFFF)
		return EFBIG;

	const uint max_out = 4 + src_size + (src_size + 7) / 8 + 32;
	u8 *out = MALLOC (max_out);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 1;
	out[1] = (u8)(src_size >> 16);
	out[2] = (u8)(src_size >> 8);
	out[3] = (u8)(src_size);

	uint out_pos = 4;
	uint p = 0;

	int head[65536];
	memset (head, -1, sizeof (head));
	int *prev = src_size ? MALLOC (src_size * sizeof (int)) : 0;
	if (src_size && !prev)
	{
		FREE (out);
		return ERR_CANT_CREATE;
	}

	while (p < src_size)
	{
		uint flags_pos = out_pos++;
		u8 flags = 0;
		for (uint bit = 0; bit < 8 && p < src_size; bit++)
		{
			uint best_len = 0, best_dist = 0;
			const uint max_len = (src_size - p > 272) ? 272 : (src_size - p);
			if (max_len >= 2)
			{
				u16 h = ((u16)src[p] << 8) | src[p + 1];
				int cand = head[h];
				uint chain_len = 64;
				while (cand >= 0 && chain_len-- > 0)
				{
					uint dist = p - cand;
					if (dist > 4095)
						break;
					uint l = 0;
					while (l < max_len && src[cand + l] == src[p + l])
						l++;
					if (l > best_len)
					{
						best_len = l;
						best_dist = dist;
						if (best_len == 272)
							break;
					}
					cand = prev[cand];
				}
			}

			if (best_len >= 2 && best_dist > 0)
			{
				flags |= (0x80 >> bit);
				if (best_len <= 16)
				{
					out[out_pos++] = (u8)(((best_dist >> 8) << 4) | (best_len - 1));
					out[out_pos++] = (u8)(best_dist & 0xFF);
				}
				else
				{
					out[out_pos++] = (u8)(((best_dist >> 8) << 4) | 0);
					out[out_pos++] = (u8)(best_dist & 0xFF);
					out[out_pos++] = (u8)(best_len - 17);
				}
				for (uint i = 0; i < best_len; i++)
				{
					if (p + i + 1 < src_size)
					{
						u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
						prev[p + i] = head[h];
						head[h] = p + i;
					}
				}
				p += best_len;
			}
			else
			{
				out[out_pos++] = src[p];
				if (p + 1 < src_size)
				{
					u16 h = ((u16)src[p] << 8) | src[p + 1];
					prev[p] = head[h];
					head[h] = p;
				}
				p++;
			}
		}
		out[flags_pos] = flags;
	}

	if (prev)
		FREE (prev);
	*dest = out;
	if (dest_size)
		*dest_size = out_pos;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// RNC (Rob Northen Compression) decoder, RNC1/RNC2 methods.
//
// Faithful port of the unpack paths from the decompiled RNC ProPack tool
// (github.com/lab313ru/rnc_propack_source; verbatim mirror used as reference:
// huderlem/carrotcrazy/tools/rnc.c).  Layout of the 18-byte header:
//
//   +0x00  "RNC" + method byte (1=RNC1, 2=RNC2)
//   +0x04  BE32 unpacked size
//   +0x08  BE32 packed size (bytes of compressed data after this header)
//   +0x0C  BE16 CRC16 of the unpacked data
//   +0x0E  BE16 CRC16 of the packed data
//   +0x10  byte leeway, +0x11 byte chunk count
//   +0x12  start of the compressed stream
//
// The two flag bits at the head of the stream select the decode method and
// signal encryption; encrypted streams need a key we do not carry, so they
// are rejected before any output is touched.

static const u16 rnc_crc_table[] = { 0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440, 0xCC01, 0x0CC0, 0x0D80, 0xCD41,
	0x0F00, 0xCFC1, 0xCE81, 0x0E40, 0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40, 0x1E00, 0xDEC1, 0xDF81, 0x1F40,
	0xDD01, 0x1DC0, 0x1C80, 0xDC41, 0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040, 0xF001, 0x30C0, 0x3180, 0xF141,
	0x3300, 0xF3C1, 0xF281, 0x3240, 0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41, 0xFA01, 0x3AC0, 0x3B80, 0xFB41,
	0x3900, 0xF9C1, 0xF881, 0x3840, 0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40, 0xE401, 0x24C0, 0x2580, 0xE541,
	0x2700, 0xE7C1, 0xE681, 0x2640, 0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240, 0x6600, 0xA6C1, 0xA781, 0x6740,
	0xA501, 0x65C0, 0x6480, 0xA441, 0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840, 0x7800, 0xB8C1, 0xB981, 0x7940,
	0xBB01, 0x7BC0, 0x7A80, 0xBA41, 0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640, 0x7200, 0xB2C1, 0xB381, 0x7340,
	0xB101, 0x71C0, 0x7080, 0xB041, 0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440, 0x9C01, 0x5CC0, 0x5D80, 0x9D41,
	0x5F00, 0x9FC1, 0x9E81, 0x5E40, 0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40, 0x4E00, 0x8EC1, 0x8F81, 0x4F40,
	0x8D01, 0x4DC0, 0x4C80, 0x8C41, 0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040 };

static const u8 rnc_match_offset_bits[] = { 0x00, 0x06, 0x08, 0x09, 0x15, 0x17, 0x1D, 0x1F, 0x28,
	0x29, 0x2C, 0x2D, 0x38, 0x39, 0x3C, 0x3D };
static const u8 rnc_match_offset_nbits[] = { 1, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6 };

typedef struct rnc_huftable_t
{
	u32 l1, l3;
	u16 l2;
	u16 bit_depth;
} rnc_huftable_t;

typedef struct rnc_state_t
{
	const u8 *src;
	uint src_size;
	uint in_pos, processed, input_size, dict_size;
	u16 match_count, match_offset, bit_count, unpacked_crc_real;
	u32 bit_buffer;
	u8 *mem1, *decoded, *window, *pack_block;
	u8 *out;
} rnc_state_t;

static u16 rnc_rotate_key (u16 x)
{
	return (x & 1) ? (u16)(0x8000 | (x >> 1)) : (u16)(x >> 1);
}

static u8 rnc_read_source (rnc_state_t *v)
{
	if (v->pack_block == &v->mem1[0xFFFD])
	{
		int left = (int)v->src_size - (int)v->in_pos;
		int n;
		if (left <= 0xFFFD)
			n = left;
		else
			n = 0xFFFD;
		v->pack_block = v->mem1;
		memcpy (v->pack_block, v->src + v->in_pos, n);
		v->in_pos += n;
		if (left - n > 2)
			left = 2;
		else
			left -= n;
		memcpy (v->pack_block + n, v->src + v->in_pos, left);
	}
	return *v->pack_block++;
}

static u32 rnc_input_bits_m2 (rnc_state_t *v, int count)
{
	u32 bits = 0;
	while (count-- > 0)
	{
		if (!v->bit_count)
		{
			v->bit_buffer = rnc_read_source (v);
			v->bit_count = 8;
		}
		bits <<= 1;
		if (v->bit_buffer & 0x80)
			bits |= 1;
		v->bit_buffer <<= 1;
		v->bit_count--;
	}
	return bits;
}

static u32 rnc_input_bits_m1 (rnc_state_t *v, int count)
{
	u32 bits = 0, prev_bits = 1;
	while (count-- > 0)
	{
		if (!v->bit_count)
		{
			const u8 b1 = rnc_read_source (v), b2 = rnc_read_source (v);
			v->bit_buffer = ((u32)v->pack_block[1] << 24) | ((u32)v->pack_block[0] << 16)
				| ((u32)b2 << 8) | b1;
			v->bit_count = 16;
		}
		if (v->bit_buffer & 1)
			bits |= prev_bits;
		v->bit_buffer >>= 1;
		prev_bits <<= 1;
		v->bit_count--;
	}
	return bits;
}

static void rnc_write_decoded (rnc_state_t *v, u8 b)
{
	if (v->window == &v->decoded[0xFFFF])
	{
		memcpy (v->out, &v->decoded[v->dict_size], 0xFFFF - v->dict_size);
		v->out += 0xFFFF - v->dict_size;
		memmove (v->decoded, &v->window[-(int)v->dict_size], v->dict_size);
		v->window = &v->decoded[v->dict_size];
	}
	*v->window++ = b;
	v->unpacked_crc_real
		= rnc_crc_table[(v->unpacked_crc_real ^ b) & 0xFF] ^ (v->unpacked_crc_real >> 8);
}

static void rnc_decode_match_offset (rnc_state_t *v)
{
	v->match_offset = 0;
	if (rnc_input_bits_m2 (v, 1))
	{
		v->match_offset = rnc_input_bits_m2 (v, 1);
		if (rnc_input_bits_m2 (v, 1))
		{
			v->match_offset = ((v->match_offset << 1) | rnc_input_bits_m2 (v, 1)) | 4;
			if (!rnc_input_bits_m2 (v, 1))
				v->match_offset = (v->match_offset << 1) | rnc_input_bits_m2 (v, 1);
		}
		else if (!v->match_offset)
			v->match_offset = rnc_input_bits_m2 (v, 1) + 2;
	}
	v->match_offset = ((v->match_offset << 8) | rnc_read_source (v)) + 1;
}

static void rnc_decode_match_count (rnc_state_t *v)
{
	v->match_count = rnc_input_bits_m2 (v, 1) + 4;
	if (rnc_input_bits_m2 (v, 1))
		v->match_count = ((v->match_count - 1) << 1) + rnc_input_bits_m2 (v, 1);
}

static void rnc_container_match (rnc_state_t *v)
{
	const uint count = v->match_count;
	v->processed += count;
	uint i = count;
	while (i-- > 0)
		rnc_write_decoded (v, v->window[-v->match_offset]);
}

static void rnc_unpack_data_m2 (rnc_state_t *v)
{
	while (v->processed < v->input_size)
	{
		for (;;)
		{
			if (!rnc_input_bits_m2 (v, 1))
			{
				rnc_write_decoded (v, rnc_read_source (v));
				v->processed++;
			}
			else
			{
				if (rnc_input_bits_m2 (v, 1))
				{
					if (rnc_input_bits_m2 (v, 1))
					{
						if (rnc_input_bits_m2 (v, 1))
						{
							v->match_count = rnc_read_source (v) + 8;
							if (v->match_count == 8)
							{
								rnc_input_bits_m2 (v, 1);
								break;
							}
						}
						else
							v->match_count = 3;
						rnc_decode_match_offset (v);
					}
					else
					{
						v->match_count = 2;
						v->match_offset = rnc_read_source (v) + 1;
					}
					rnc_container_match (v);
				}
				else
				{
					rnc_decode_match_count (v);
					if (v->match_count != 9)
					{
						rnc_decode_match_offset (v);
						rnc_container_match (v);
					}
					else
					{
						uint data_length = (rnc_input_bits_m2 (v, 4) << 2) + 12;
						v->processed += data_length;
						while (data_length-- > 0)
							rnc_write_decoded (v, rnc_read_source (v));
					}
				}
			}
		}
	}
}

static void rnc_clear_table (rnc_huftable_t *t, int count)
{
	for (int i = 0; i < count; i++)
	{
		t[i].l1 = 0;
		t[i].l2 = 0xFFFF;
		t[i].l3 = 0;
		t[i].bit_depth = 0;
	}
}

static u32 rnc_inverse_bits (u32 value, int count)
{
	u32 out = 0;
	while (count-- > 0)
	{
		out <<= 1;
		if (value & 1)
			out |= 1;
		value >>= 1;
	}
	return out;
}

static void rnc_proc_20 (rnc_huftable_t *t, int count)
{
	u32 val = 0, div = 0x80000000;
	int depth = 1;
	while (depth <= 16)
	{
		for (int i = 0; i < count; i++)
		{
			if (t[i].bit_depth == depth)
			{
				t[i].l3 = rnc_inverse_bits (val / div, depth);
				val += div;
			}
		}
		depth++;
		div >>= 1;
	}
}

static void rnc_make_huftable (rnc_state_t *v, rnc_huftable_t *t, int count)
{
	rnc_clear_table (t, count);
	int leaf_nodes = (int)rnc_input_bits_m1 (v, 5);
	if (leaf_nodes)
	{
		if (leaf_nodes > 16)
			leaf_nodes = 16;
		for (int i = 0; i < leaf_nodes; i++)
			t[i].bit_depth = (u16)rnc_input_bits_m1 (v, 4);
		rnc_proc_20 (t, leaf_nodes);
	}
}

static u32 rnc_decode_table_data (rnc_state_t *v, rnc_huftable_t *t)
{
	for (u32 i = 0;; i++)
	{
		if (t[i].bit_depth && t[i].l3 == (v->bit_buffer & ((1u << t[i].bit_depth) - 1)))
		{
			rnc_input_bits_m1 (v, t[i].bit_depth);
			if (i < 2)
				return i;
			return rnc_input_bits_m1 (v, i - 1) | (1u << (i - 1));
		}
	}
}

static void rnc_unpack_data_m1 (rnc_state_t *v)
{
	rnc_huftable_t raw[16], len[16], pos[16];
	while (v->processed < v->input_size)
	{
		rnc_make_huftable (v, raw, 16);
		rnc_make_huftable (v, len, 16);
		rnc_make_huftable (v, pos, 16);
		int subchunks = (int)rnc_input_bits_m1 (v, 16);
		while (subchunks-- > 0)
		{
			uint data_length = rnc_decode_table_data (v, raw);
			v->processed += data_length;
			if (data_length)
			{
				while (data_length-- > 0)
					rnc_write_decoded (v, rnc_read_source (v));
				v->bit_buffer = ((((u32)v->pack_block[2] << 16) | ((u32)v->pack_block[1] << 8)
									 | v->pack_block[0])
									<< v->bit_count)
					| (v->bit_buffer & ((1u << v->bit_count) - 1));
			}
			if (subchunks)
			{
				v->match_offset = rnc_decode_table_data (v, len) + 1;
				v->match_count = rnc_decode_table_data (v, pos) + 2;
				rnc_container_match (v);
			}
		}
	}
}

enumError DecodeRNC (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;
	if (!src || src_size < 0x14 || memcmp (src, "RNC", 3))
		return EINVAL;

	const u8 method = src[3];
	if (method != 1 && method != 2)
		return EINVAL;

	const u32 input_size = rd_be32 (src + 0x04);
	const u32 packed_size = rd_be32 (src + 0x08);
	if (!input_size || input_size > NFMT_MAX_OUTPUT)
		return EFBIG;
	if (packed_size > src_size - 0x12)
		return EINVAL;

	u16 packed_crc = 0;
	for (u32 i = 0; i < packed_size; i++)
		packed_crc = rnc_crc_table[(packed_crc ^ src[0x12 + i]) & 0xFF] ^ (packed_crc >> 8);
	if (packed_crc != rd_be16 (src + 0x0E))
		return EINVAL;

	enumError err = alloc_output (dest, dest_size, input_size);
	if (err)
		return err;

	rnc_state_t st;
	memset (&st, 0, sizeof (st));
	st.src = src;
	st.src_size = src_size;
	st.in_pos = 0x12;
	st.input_size = input_size;
	st.dict_size = method == 1 ? 0x8000 : 0x1000;
	st.out = *dest;
	st.mem1 = MALLOC (0xFFFF + 4);
	st.decoded = MALLOC (0xFFFF + 4);
	if (!st.mem1 || !st.decoded)
	{
		FREE (st.mem1);
		FREE (st.decoded);
		FREE (*dest);
		*dest = 0;
		*dest_size = 0;
		return ERR_CANT_CREATE;
	}
	st.pack_block = &st.mem1[0xFFFD];
	st.window = &st.decoded[st.dict_size];

	if (method == 1)
	{
		// flags: locked? + keyed?
		rnc_input_bits_m1 (&st, 1);
		if (rnc_input_bits_m1 (&st, 1))
		{
			FREE (st.mem1);
			FREE (st.decoded);
			FREE (*dest);
			*dest = 0;
			*dest_size = 0;
			return EINVAL;
		}
		rnc_unpack_data_m1 (&st);
	}
	else
	{
		rnc_input_bits_m2 (&st, 1);
		if (rnc_input_bits_m2 (&st, 1))
		{
			FREE (st.mem1);
			FREE (st.decoded);
			FREE (*dest);
			*dest = 0;
			*dest_size = 0;
			return EINVAL;
		}
		rnc_unpack_data_m2 (&st);
	}

	memcpy (st.out, &st.decoded[st.dict_size], st.window - &st.decoded[st.dict_size]);
	st.out += st.window - &st.decoded[st.dict_size];

	FREE (st.mem1);
	FREE (st.decoded);

	if (st.unpacked_crc_real != rd_be16 (src + 0x0C) || st.out - *dest != input_size)
	{
		FREE (*dest);
		*dest = 0;
		*dest_size = 0;
		return EINVAL;
	}

	return ERR_OK;
}

typedef struct rnc_writer_t
{
	u8 *buf;
	uint cap;
	uint len;
	int bit_pos;
	u8 bit_buf;
	uint bit_cnt;
} rnc_writer_t;

static void rnc_w_init (rnc_writer_t *w, uint initial_cap)
{
	w->cap = initial_cap ? initial_cap : 1024;
	w->buf = MALLOC (w->cap);
	w->len = 0;
	w->bit_pos = -1;
	w->bit_buf = 0;
	w->bit_cnt = 0;
}

static void rnc_w_put_bit (rnc_writer_t *w, int b)
{
	if (!w->bit_cnt)
	{
		if (w->len >= w->cap)
		{
			w->cap *= 2;
			w->buf = REALLOC (w->buf, w->cap);
		}
		w->bit_pos = (int)w->len++;
		w->buf[w->bit_pos] = 0;
		w->bit_buf = 0;
		w->bit_cnt = 8;
	}
	w->bit_cnt--;
	if (b)
		w->bit_buf |= (1u << w->bit_cnt);
	w->buf[w->bit_pos] = w->bit_buf;
}

static void rnc_w_put_bits (rnc_writer_t *w, u32 val, int n)
{
	for (int i = n - 1; i >= 0; i--)
		rnc_w_put_bit (w, (val >> i) & 1);
}

static void rnc_w_put_byte (rnc_writer_t *w, u8 byte)
{
	if (w->len >= w->cap)
	{
		w->cap *= 2;
		w->buf = REALLOC (w->buf, w->cap);
	}
	w->buf[w->len++] = byte;
}

static void rnc_w_put_match_offset (rnc_writer_t *w, uint dist)
{
	uint val = dist - 1;
	uint hi = (val >> 8) & 0x0F;
	uint lo = val & 0xFF;
	rnc_w_put_bits (w, rnc_match_offset_bits[hi], rnc_match_offset_nbits[hi]);
	rnc_w_put_byte (w, (u8)lo);
}

// Method 1 uses little-endian 16-bit bit tokens with literal bytes queued
// behind the token currently being assembled.  A literal-only stream is a
// fully conforming RNC1 stream and is deliberately used here: it gives us a
// simple, deterministic encoder without pretending the method-2 LZ stream
// is method 1. Compression quality can be improved independently later.
typedef struct rnc1_writer_t
{
	u8 *buf;
	uint cap, len, pending_len, pending_cap;
	u16 token;
	uint nbits;
	u8 *pending;
} rnc1_writer_t;

static bool rnc1_emit (rnc1_writer_t *w, u8 byte)
{
	if (w->len >= w->cap)
		return false;
	w->buf[w->len++] = byte;
	return true;
}

static bool rnc1_flush_token (rnc1_writer_t *w)
{
	if (!rnc1_emit (w, (u8)w->token) || !rnc1_emit (w, (u8)(w->token >> 8)))
		return false;
	for (uint i = 0; i < w->pending_len; i++)
		if (!rnc1_emit (w, w->pending[i]))
			return false;
	w->token = 0;
	w->nbits = 0;
	w->pending_len = 0;
	return true;
}

static bool rnc1_put_bits (rnc1_writer_t *w, u32 value, uint count)
{
	while (count--)
	{
		w->token >>= 1;
		if (value & 1)
			w->token |= 0x8000;
		value >>= 1;
		if (++w->nbits == 16 && !rnc1_flush_token (w))
			return false;
	}
	return true;
}

static bool rnc1_queue_byte (rnc1_writer_t *w, u8 byte)
{
	if (!w->nbits)
		return rnc1_emit (w, byte);
	if (w->pending_len >= w->pending_cap)
		return false;
	w->pending[w->pending_len++] = byte;
	return true;
}

static enumError rnc_encode_m1_literals (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src_size)
		return EINVAL;
	const uint blocks = (src_size + 0x2fff) / 0x3000;
	const u64 capacity = (u64)src_size + (u64)blocks * 32 + 64;
	if (capacity > NFMT_MAX_OUTPUT)
		return EFBIG;
	rnc1_writer_t w = { 0 };
	w.cap = (uint)capacity;
	w.pending_cap = 0x3000;
	w.buf = MALLOC (w.cap);
	w.pending = MALLOC (w.pending_cap);
	if (!w.buf || !w.pending)
	{
		FREE (w.buf);
		FREE (w.pending);
		return ERR_OUT_OF_MEMORY;
	}
	bool ok = rnc1_put_bits (&w, 0, 1) && rnc1_put_bits (&w, 0, 1); // unlocked, unkeyed
	uint pos = 0;
	while (ok && pos < src_size)
	{
		const uint count = src_size - pos > 0x3000 ? 0x3000 : src_size - pos;
		uint symbol = 0, tmp = count;
		while (tmp)
		{
			symbol++;
			tmp >>= 1;
		}
		// raw Huffman table: one one-bit symbol; empty offset/length tables;
		// one literal-only subchunk.
		ok = rnc1_put_bits (&w, symbol + 1, 5);
		for (uint i = 0; ok && i <= symbol; i++)
			ok = rnc1_put_bits (&w, i == symbol ? 1 : 0, 4);
		ok = ok && rnc1_put_bits (&w, 0, 5) && rnc1_put_bits (&w, 0, 5) && rnc1_put_bits (&w, 1, 16)
			&& rnc1_put_bits (&w, 0, 1);
		if (symbol > 1)
			ok = ok && rnc1_put_bits (&w, count - (1u << (symbol - 1)), symbol - 1);
		for (uint i = 0; ok && i < count; i++)
			ok = rnc1_queue_byte (&w, src[pos + i]);
		pos += count;
	}
	if (ok && (w.nbits || w.pending_len))
	{
		w.token >>= 16 - w.nbits;
		ok = rnc1_flush_token (&w);
	}
	FREE (w.pending);
	if (!ok)
	{
		FREE (w.buf);
		return ERR_OUT_OF_MEMORY;
	}
	u16 unpacked_crc = 0, packed_crc = 0;
	for (uint i = 0; i < src_size; i++)
		unpacked_crc = rnc_crc_table[(unpacked_crc ^ src[i]) & 255] ^ (unpacked_crc >> 8);
	for (uint i = 0; i < w.len; i++)
		packed_crc = rnc_crc_table[(packed_crc ^ w.buf[i]) & 255] ^ (packed_crc >> 8);
	u8 *out = MALLOC (0x12 + w.len);
	if (!out)
	{
		FREE (w.buf);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (out, "RNC\1", 4);
	wr_be32 (out + 4, src_size);
	wr_be32 (out + 8, w.len);
	wr_be16 (out + 12, unpacked_crc);
	wr_be16 (out + 14, packed_crc);
	out[16] = 0;
	out[17] = (u8)blocks;
	memcpy (out + 18, w.buf, w.len);
	FREE (w.buf);
	*dest = out;
	*dest_size = 0x12 + w.len;
	return ERR_OK;
}

enumError EncodeRNC (u8 **dest, uint *dest_size, const u8 *src, uint src_size, int method)
{
	if (!dest || !dest_size)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;
	if (!src && src_size)
		return ERR_SEMANTIC;
	if (method == 1)
		return rnc_encode_m1_literals (dest, dest_size, src, src_size);
	if (method != 2)
		return EINVAL;

	rnc_writer_t w;
	rnc_w_init (&w, src_size + 64);
	if (!w.buf)
		return ERR_OUT_OF_MEMORY;

	// init flags: locked=0, keyed=0
	rnc_w_put_bit (&w, 0);
	rnc_w_put_bit (&w, 0);

	int head[65536];
	memset (head, -1, sizeof (head));
	int *prev = src_size ? MALLOC (src_size * sizeof (int)) : 0;
	if (src_size && !prev)
	{
		FREE (w.buf);
		return ERR_OUT_OF_MEMORY;
	}

	uint p = 0;
	while (p < src_size)
	{
		uint best_len = 0, best_dist = 0;
		const uint max_l = (src_size - p > 263) ? 263 : (src_size - p);
		if (max_l >= 2)
		{
			u16 h = ((u16)src[p] << 8) | src[p + 1];
			int cand = head[h];
			uint chain = 64;
			while (cand >= 0 && chain-- > 0)
			{
				uint dist = p - cand;
				if (dist > 4095)
					break;
				uint l = 0;
				while (l < max_l && src[cand + l] == src[p + l])
					l++;
				if (l > best_len)
				{
					best_len = l;
					best_dist = dist;
					if (best_len == 263)
						break;
				}
				cand = prev[cand];
			}
		}

		if (best_len == 2 && best_dist <= 256)
		{
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 0);
			rnc_w_put_byte (&w, (u8)(best_dist - 1));
			for (uint i = 0; i < best_len; i++)
			{
				if (p + i + 1 < src_size)
				{
					u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
					prev[p + i] = head[h];
					head[h] = p + i;
				}
			}
			p += best_len;
		}
		else if (best_len == 3)
		{
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 0);
			rnc_w_put_match_offset (&w, best_dist);
			for (uint i = 0; i < best_len; i++)
			{
				if (p + i + 1 < src_size)
				{
					u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
					prev[p + i] = head[h];
					head[h] = p + i;
				}
			}
			p += best_len;
		}
		else if (best_len >= 4 && best_len <= 8)
		{
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 0);
			static const u8 cvals[] = { 0, 2, 2, 3, 6 };
			static const u8 cbits[] = { 2, 2, 3, 3, 3 };
			rnc_w_put_bits (&w, cvals[best_len - 4], cbits[best_len - 4]);
			rnc_w_put_match_offset (&w, best_dist);
			for (uint i = 0; i < best_len; i++)
			{
				if (p + i + 1 < src_size)
				{
					u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
					prev[p + i] = head[h];
					head[h] = p + i;
				}
			}
			p += best_len;
		}
		else if (best_len >= 9)
		{
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_bit (&w, 1);
			rnc_w_put_byte (&w, (u8)(best_len - 8));
			rnc_w_put_match_offset (&w, best_dist);
			for (uint i = 0; i < best_len; i++)
			{
				if (p + i + 1 < src_size)
				{
					u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
					prev[p + i] = head[h];
					head[h] = p + i;
				}
			}
			p += best_len;
		}
		else
		{
			rnc_w_put_bit (&w, 0);
			rnc_w_put_byte (&w, src[p]);
			if (p + 1 < src_size)
			{
				u16 h = ((u16)src[p] << 8) | src[p + 1];
				prev[p] = head[h];
				head[h] = p;
			}
			p++;
		}
	}

	if (prev)
		FREE (prev);

	// End of stream marker
	rnc_w_put_bit (&w, 1);
	rnc_w_put_bit (&w, 1);
	rnc_w_put_bit (&w, 1);
	rnc_w_put_bit (&w, 1);
	rnc_w_put_byte (&w, 0);
	rnc_w_put_bit (&w, 0);

	u16 unpacked_crc = 0;
	for (uint i = 0; i < src_size; i++)
		unpacked_crc = rnc_crc_table[(unpacked_crc ^ src[i]) & 0xFF] ^ (unpacked_crc >> 8);

	u16 packed_crc = 0;
	for (uint i = 0; i < w.len; i++)
		packed_crc = rnc_crc_table[(packed_crc ^ w.buf[i]) & 0xFF] ^ (packed_crc >> 8);

	uint out_total = 0x12 + w.len;
	u8 *out = MALLOC (out_total);
	if (!out)
	{
		FREE (w.buf);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (out, "RNC\x02", 4);
	out[0x04] = (u8)(src_size >> 24);
	out[0x05] = (u8)(src_size >> 16);
	out[0x06] = (u8)(src_size >> 8);
	out[0x07] = (u8)(src_size & 0xFF);
	out[0x08] = (u8)(w.len >> 24);
	out[0x09] = (u8)(w.len >> 16);
	out[0x0A] = (u8)(w.len >> 8);
	out[0x0B] = (u8)(w.len & 0xFF);
	out[0x0C] = (u8)(unpacked_crc >> 8);
	out[0x0D] = (u8)(unpacked_crc & 0xFF);
	out[0x0E] = (u8)(packed_crc >> 8);
	out[0x0F] = (u8)(packed_crc & 0xFF);
	out[0x10] = 0;
	out[0x11] = 1;
	memcpy (out + 0x12, w.buf, w.len);

	FREE (w.buf);
	*dest = out;
	*dest_size = out_total;
	return ERR_OK;
}

enumError DecodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 4 || (src[0] != 0x10 && src[0] != 0x11))
		return EINVAL;
	const bool lz11 = src[0] == 0x11;
	const u32 out_len = (u32)src[1] | (u32)src[2] << 8 | (u32)src[3] << 16;
	enumError err = alloc_output (dest, dest_size, out_len);
	if (err)
		return err;
	uint sp = 4, dp = 0;
	while (dp < out_len)
	{
		if (sp >= src_size)
			goto invalid;
		u8 flags = src[sp++];
		for (uint bit = 0; bit < 8 && dp < out_len; bit++, flags <<= 1)
			if (!(flags & 0x80))
			{
				if (sp >= src_size)
					goto invalid;
				(*dest)[dp++] = src[sp++];
			}
			else
			{
				if (sp + 2 > src_size)
					goto invalid;
				u8 a = src[sp++], b = src[sp++];
				uint len, back;
				if (!lz11)
				{
					len = (a >> 4) + 3;
					back = ((a & 15) << 8 | b) + 1;
				}
				else if (a >> 4 == 0)
				{
					if (sp >= src_size)
						goto invalid;
					len = ((a & 15) << 4 | b >> 4) + 0x11;
					back = ((b & 15) << 8 | src[sp++]) + 1;
				}
				else if (a >> 4 == 1)
				{
					if (sp + 2 > src_size)
						goto invalid;
					len = ((a & 15) << 12 | b << 4 | src[sp] >> 4) + 0x111;
					const u8 c = src[sp++], d = src[sp++];
					back = ((c & 15) << 8 | d) + 1;
				}
				else
				{
					len = (a >> 4) + 1;
					back = ((a & 15) << 8 | b) + 1;
				}
				if (back > dp || len > out_len - dp)
					goto invalid;
				while (len--)
					(*dest)[dp] = (*dest)[dp - back], dp++;
			}
	}
	return ERR_OK;
invalid:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool lz11)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0xffffff)
		return EINVAL;

	// Worst case is one flag byte per eight literals plus the four-byte
	// header.  A little extra also covers the final, partial group.
	const uint capacity = 4 + src_size + (src_size + 7) / 8;
	u8 *out = MALLOC (capacity);
	if (!out)
		return ERR_CANT_CREATE;
	out[0] = lz11 ? 0x11 : 0x10;
	out[1] = src_size;
	out[2] = src_size >> 8;
	out[3] = src_size >> 16;

	uint sp = 0, dp = 4;
	while (sp < src_size)
	{
		const uint flags_pos = dp++;
		u8 flags = 0;
		for (uint bit = 0; bit < 8 && sp < src_size; bit++)
		{
			uint best_len = 0, best_back = 0;
			const uint max_back = sp < 0x1000 ? sp : 0x1000;
			const uint max_len = lz11 ? (src_size - sp < 16 ? src_size - sp : 16)
									  : (src_size - sp < 18 ? src_size - sp : 18);
			// A backwards search is deliberately used: nearby matches tend
			// to give the same compact stream as Nintendo's common tools.
			for (uint back = 1; back <= max_back; back++)
			{
				uint len = 0;
				while (len < max_len && src[sp + len] == src[sp - back + len])
					len++;
				if (len > best_len)
				{
					best_len = len;
					best_back = back;
					if (len == max_len)
						break;
				}
			}
			if (best_len >= 3)
			{
				flags |= 0x80 >> bit;
				const uint disp = best_back - 1;
				if (lz11)
				{
					// The regular LZ11 token represents lengths 3..16.
					out[dp++] = (best_len - 1) << 4 | (disp >> 8);
					out[dp++] = disp;
				}
				else
				{
					out[dp++] = (best_len - 3) << 4 | (disp >> 8);
					out[dp++] = disp;
				}
				sp += best_len;
			}
			else
				out[dp++] = src[sp++];
		}
		out[flags_pos] = flags;
	}
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

enumError EncodeLZ10Raw (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	u8 *lz10 = 0;
	uint lz10_size = 0;
	enumError err = EncodeLZ10LZ11 (&lz10, &lz10_size, src, src_size, false);
	if (!err)
	{
		if (lz10_size > 4)
		{
			*dest_size = lz10_size - 4;
			*dest = MALLOC (*dest_size);
			if (*dest)
				memcpy (*dest, lz10 + 4, *dest_size);
			else
				err = ERR_CANT_CREATE;
		}
		else
			err = ERR_INVALID_DATA;
		FREE (lz10);
	}
	return err;
}

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
	enumError err = alloc_output (dest, dest_size, out_size);
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

enumError DecodeNintendoRL (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 4 || src[0] != 0x30)
		return EINVAL;
	const uint out_size = (uint)src[1] | (uint)src[2] << 8 | (uint)src[3] << 16;
	enumError err = alloc_output (dest, dest_size, out_size);
	if (err)
		return err;
	uint sp = 4, dp = 0;
	while (dp < out_size)
	{
		if (sp >= src_size)
			goto invalid_rl;
		const u8 control = src[sp++];
		const uint len = (control & 0x7f) + (control >> 7 ? 3 : 1);
		if (len > out_size - dp || sp + (control >> 7 ? 1 : len) > src_size)
			goto invalid_rl;
		if (control >> 7)
			memset (*dest + dp, src[sp++], len);
		else
		{
			memcpy (*dest + dp, src + sp, len);
			sp += len;
		}
		dp += len;
	}
	return ERR_OK;
invalid_rl:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeNintendoRL (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || !src_size || src_size > 0xffffff || !dest || !dest_size)
		return EINVAL;
	const uint cap = src_size + (src_size + 127) / 128 + 4;
	u8 *out = MALLOC (cap);
	if (!out)
		return ERR_CANT_CREATE;
	out[0] = 0x30;
	out[1] = src_size;
	out[2] = src_size >> 8;
	out[3] = src_size >> 16;
	uint sp = 0, dp = 4;
	while (sp < src_size)
	{
		uint run = 1;
		while (run < 130 && sp + run < src_size && src[sp + run] == src[sp])
			run++;
		if (run >= 3)
		{
			out[dp++] = 0x80 | (run - 3);
			out[dp++] = src[sp];
			sp += run;
			continue;
		}
		const uint start = sp++;
		while (sp - start < 128 && sp < src_size)
		{
			run = 1;
			while (run < 3 && sp + run < src_size && src[sp + run] == src[sp])
				run++;
			if (run >= 3)
				break;
			sp++;
		}
		const uint len = sp - start;
		out[dp++] = len - 1;
		memcpy (out + dp, src + start, len);
		dp += len;
	}
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

// BLZ ("backward LZSS"): used to compress a DS ROM's ARM9/ARM7 executable
// and overlay files, ported from CUE's reference blz.c
// (github.com/PeterLemon/Nintendo_DS_Compressors). Unlike this file's other
// LZ variants it has no magic/header at the *start* -- everything needed to
// decode is an 8-11 byte footer at the *end*, and the compressed span is
// itself byte-reversed (encode walks the source backward, building matches
// against what -- once reversed back -- reads as ordinary forward LZSS with
// a min match length of 3 stored as len-3 and a 12-bit back-reference).
// This means BLZ can't be identified by a header-magic table lookup the way
// the rest of DetectNintendoFormat() works: any file could coincidentally
// have a plausible-looking footer, so this decoder is only invoked where
// the caller already has other context that a file might be BLZ (an
// ndstool-staged arm9.bin/arm7.bin/overlay), not from generic dispatch.
enumError DecodeBLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 4)
		return EINVAL;

	const u32 inc_len = rd_le32 (src + src_size - 4);
	if (!inc_len)
	{
		// "not coded" marker: BLZ_Encode() writes this when compression
		// would have made the file bigger. Confirmed against the real
		// reference decoder rather than assumed: despite what the encoder
		// side suggests, "decoding" this case reproduces the *entire*
		// input verbatim, trailing 4-byte zero marker included, not the
		// marker-stripped plain content -- checked with `blz -d` on a
		// deliberately incompressible sample and diffed byte-for-byte.
		enumError err = alloc_output (dest, dest_size, src_size);
		if (err)
			return err;
		memcpy (*dest, src, src_size);
		return ERR_OK;
	}

	if (src_size < 8)
		return EINVAL;
	const uint hdr_len = src[src_size - 5];
	if (hdr_len < 8 || hdr_len > 11 || src_size <= hdr_len)
		return EINVAL;

	const u32 enc_len = rd_le32 (src + src_size - 8) & 0x00FFFFFF;
	if (enc_len > src_size || enc_len < hdr_len)
		return EINVAL;
	const u32 dec_len = (u32)src_size - enc_len; // leading plain span
	const u32 pak_len = enc_len - hdr_len; // compressed span
	if (dec_len + pak_len > src_size)
		return EINVAL;

	const u64 raw_len64 = (u64)dec_len + enc_len + inc_len;
	if (raw_len64 > 64 * 1024 * 1024)
		return EINVAL; // sanity cap
	const u32 raw_len = (u32)raw_len64;

	enumError err = alloc_output (dest, dest_size, raw_len);
	if (err)
		return err;
	u8 *raw = *dest;

	// Leading dec_len bytes are stored verbatim (not part of the
	// compressed span at all).
	memcpy (raw, src, dec_len);

	// Reverse a private copy of the compressed span so ordinary
	// forward-reading LZSS logic reproduces BLZ_Encode()'s backward walk.
	u8 *rev = MALLOC (pak_len ? pak_len : 1);
	for (u32 i = 0; i < pak_len; i++)
		rev[i] = src[dec_len + pak_len - 1 - i];

	u32 rp = 0, dp = dec_len;
	u8 flags = 0, mask = 0;
	bool bad = false;
	while (dp < raw_len)
	{
		if (!(mask >>= 1))
		{
			if (rp >= pak_len)
				break;
			flags = rev[rp++];
			mask = 0x80;
		}
		if (!(flags & mask))
		{
			if (rp >= pak_len)
			{
				bad = true;
				break;
			}
			raw[dp++] = rev[rp++];
		}
		else
		{
			if (rp + 1 >= pak_len)
			{
				bad = true;
				break;
			}
			uint pos = (uint)rev[rp] << 8 | rev[rp + 1];
			rp += 2;
			uint len = (pos >> 12) + 3;
			uint back = (pos & 0xFFF) + 3;
			if (back > dp - dec_len || dp + len > raw_len)
			{
				bad = true;
				break;
			}
			while (len--)
			{
				raw[dp] = raw[dp - back];
				dp++;
			}
		}
	}
	FREE (rev);

	if (bad || dp != raw_len)
	{
		FREE (*dest);
		*dest = 0;
		*dest_size = 0;
		return EINVAL;
	}

	// Un-reverse the newly-decoded tail back to normal forward order (the
	// leading dec_len verbatim span was never reversed and stays as-is).
	for (u32 i = dec_len, j = raw_len - 1; i < j; i++, j--)
	{
		u8 t = raw[i];
		raw[i] = raw[j];
		raw[j] = t;
	}

	return ERR_OK;
}

enumError EncodeBLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00FFFFFF)
		return EINVAL;

	u8 *rev_src = MALLOC (src_size);
	if (!rev_src)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < src_size; i++)
		rev_src[i] = src[src_size - 1 - i];

	const uint max_pak = src_size + (src_size + 7) / 8 + 32;
	u8 *rev_pak = MALLOC (max_pak);
	if (!rev_pak)
	{
		FREE (rev_src);
		return ERR_CANT_CREATE;
	}

	uint sp = 0, dp = 0;
	while (sp < src_size)
	{
		const uint flags_pos = dp++;
		u8 flags = 0;
		for (uint bit = 0; bit < 8 && sp < src_size; bit++)
		{
			uint best_len = 0, best_back = 0;
			const uint max_back = sp < 4098 ? sp : 4098;
			const uint max_len = src_size - sp < 18 ? src_size - sp : 18;
			for (uint back = 3; back <= max_back; back++)
			{
				uint len = 0;
				while (len < max_len && rev_src[sp + len] == rev_src[sp - back + len])
					len++;
				if (len > best_len)
				{
					best_len = len;
					best_back = back;
					if (len == max_len)
						break;
				}
			}

			if (best_len >= 3)
			{
				flags |= (0x80 >> bit);
				const uint pos = ((best_len - 3) << 12) | ((best_back - 3) & 0xFFF);
				rev_pak[dp++] = (pos >> 8) & 0xFF;
				rev_pak[dp++] = pos & 0xFF;
				sp += best_len;
			}
			else
			{
				rev_pak[dp++] = rev_src[sp++];
			}
		}
		rev_pak[flags_pos] = flags;
	}
	FREE (rev_src);

	const uint pak_len = dp;
	if (pak_len + 8 >= src_size)
	{
		FREE (rev_pak);
		u8 *out = CALLOC (1, src_size + 4);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, src, src_size);
		*dest = out;
		*dest_size = src_size + 4;
		return ERR_OK;
	}

	const uint enc_len = pak_len + 8;
	const uint inc_len = src_size - enc_len;
	u8 *out = MALLOC (enc_len);
	if (!out)
	{
		FREE (rev_pak);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < pak_len; i++)
		out[i] = rev_pak[pak_len - 1 - i];
	FREE (rev_pak);

	out[pak_len + 0] = enc_len & 0xFF;
	out[pak_len + 1] = (enc_len >> 8) & 0xFF;
	out[pak_len + 2] = (enc_len >> 16) & 0xFF;
	out[pak_len + 3] = 8;
	out[pak_len + 4] = inc_len & 0xFF;
	out[pak_len + 5] = (inc_len >> 8) & 0xFF;
	out[pak_len + 6] = (inc_len >> 16) & 0xFF;
	out[pak_len + 7] = (inc_len >> 24) & 0xFF;

	*dest = out;
	*dest_size = enc_len;
	return ERR_OK;
}

enumError DecodeYay0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 16 || memcmp (src, "Yay0", 4))
		return EINVAL;
	const u32 out_len = rd_be32 (src + 4), link = rd_be32 (src + 8), chunk = rd_be32 (src + 12);
	enumError err = alloc_output (dest, dest_size, out_len);
	if (err)
		return err;
	uint mask = 16, lp = link, cp = chunk, dp = 0, bits = 0;
	u32 code = 0;
	while (dp < out_len)
	{
		if (!bits)
		{
			if (mask + 4 > src_size)
				goto invalid;
			code = rd_be32 (src + mask);
			mask += 4;
			bits = 32;
		}
		if (code & 0x80000000)
		{
			if (cp >= src_size)
				goto invalid;
			(*dest)[dp++] = src[cp++];
		}
		else
		{
			if (lp + 2 > src_size)
				goto invalid;
			u16 v = (u16)src[lp] << 8 | src[lp + 1];
			lp += 2;
			uint len = v >> 12, back = (v & 0xfff) + 1;
			if (!len)
			{
				if (cp >= src_size)
					goto invalid;
				len = src[cp++] + 18;
			}
			else
				len += 2;
			if (back > dp || len > out_len - dp)
				goto invalid;
			while (len--)
				(*dest)[dp] = (*dest)[dp - back], dp++;
		}
		code <<= 1;
		bits--;
	}
	return ERR_OK;
invalid:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeYay0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > UINT_MAX / 2)
		return EINVAL;
	const uint max_masks = (src_size + 31) / 32 * 4;
	u8 *masks = CALLOC (1, max_masks);
	u8 *links = MALLOC (2 * src_size);
	u8 *chunks = MALLOC (2 * src_size);
	if (!masks || !links || !chunks)
	{
		FREE (masks);
		FREE (links);
		FREE (chunks);
		return ERR_CANT_CREATE;
	}
	uint sp = 0, mp = 0, lp = 0, cp = 0, bit = 0;
	u32 mask = 0;
	while (sp < src_size)
	{
		if (!bit)
		{
			mask = 0;
			mp += 4;
		}
		uint best_len = 0, best_back = 0;
		const uint max_back = sp < 0x1000 ? sp : 0x1000;
		const uint max_len = src_size - sp < 0x111 ? src_size - sp : 0x111;
		for (uint back = 1; back <= max_back; back++)
		{
			uint len = 0;
			while (len < max_len && src[sp + len] == src[sp - back + len])
				len++;
			if (len > best_len)
			{
				best_len = len;
				best_back = back;
				if (len == max_len)
					break;
			}
		}
		if (best_len >= 3)
		{
			const uint disp = best_back - 1;
			if (best_len >= 18)
			{
				links[lp++] = disp >> 8;
				links[lp++] = disp;
				chunks[cp++] = best_len - 18;
			}
			else
			{
				links[lp++] = (best_len - 2) << 4 | (disp >> 8);
				links[lp++] = disp;
			}
			sp += best_len;
		}
		else
		{
			mask |= 0x80000000u >> bit;
			chunks[cp++] = src[sp++];
		}
		bit = (bit + 1) & 31;
		if (!bit)
			wr_be32 (masks + mp - 4, mask);
	}
	if (bit)
		wr_be32 (masks + mp - 4, mask);
	const uint link_off = 16 + mp;
	const uint chunk_off = link_off + lp;
	if (chunk_off > UINT_MAX - cp)
	{
		FREE (masks);
		FREE (links);
		FREE (chunks);
		return EFBIG;
	}
	const uint total = chunk_off + cp;
	u8 *out = MALLOC (total);
	if (!out)
	{
		FREE (masks);
		FREE (links);
		FREE (chunks);
		return ERR_CANT_CREATE;
	}
	memcpy (out, "Yay0", 4);
	wr_be32 (out + 4, src_size);
	wr_be32 (out + 8, link_off);
	wr_be32 (out + 12, chunk_off);
	memcpy (out + 16, masks, mp);
	memcpy (out + link_off, links, lp);
	memcpy (out + chunk_off, chunks, cp);
	FREE (masks);
	FREE (links);
	FREE (chunks);
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

//
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
	enumError err = alloc_output (dest, dest_size, uncompressed_length);
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

static inline u8 expand5 (u8 value)
{
	return value << 3 | value >> 2;
}

enumError DecodeDSB_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size <= 0x60 || memcmp (src, "TXTR", 4))
		return EINVAL;

	// AC:WW's menu TXTR variant stores 32 little-endian RGB555 entries at
	// 0x20 and one A3I5 byte per pixel at 0x60.  The payload is square.
	const uint pixel_count = src_size - 0x60;
	uint side = 1;
	while (side <= pixel_count / side && side * side < pixel_count)
		side++;
	if (side * side != pixel_count || side > 1024)
		return EINVAL;

	u8 *rgba = MALLOC (pixel_count * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	const u8 *texel = src + 0x60;
	for (uint i = 0; i < pixel_count; i++)
	{
		const u8 value = texel[i];
		const uint poff = 0x20 + (value & 0x1f) * 2;
		const u16 color = src[poff] | (u16)src[poff + 1] << 8;
		rgba[4 * i + 0] = expand5 (color & 0x1f);
		rgba[4 * i + 1] = expand5 (color >> 5 & 0x1f);
		rgba[4 * i + 2] = expand5 (color >> 10 & 0x1f);
		rgba[4 * i + 3] = (value >> 5) * 255 / 7;
	}

	*dest = rgba;
	*width = *height = side;
	return ERR_OK;
}

enumError EncodeDSB_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	if (!dest || !dest_size || !rgba || width != 128 || height != 128)
		return EINVAL;

	const uint pixels = width * height;
	const uint total = 0x60 + pixels;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;

	static const u8 header[0x20] = { 'T', 'X', 'T', 'R', 0x10, 0x44, 0x60, 0x00, 0x60, 0x00, 0x10,
		0x20, 0x00, 0x01, 0x60, 0x00 };
	memcpy (out, header, sizeof (header));

	u16 palette[32] = { 0 };
	uint n_pal = 1;
	for (uint px = 0; px < pixels && n_pal < 32; px++)
	{
		const u8 *p = rgba + 4 * px;
		const u16 c = (u16)(p[0] >> 3) | (u16)(p[1] >> 3) << 5 | (u16)(p[2] >> 3) << 10;
		uint pi;
		for (pi = 0; pi < n_pal && palette[pi] != c; pi++)
		{
		}
		if (pi == n_pal)
			palette[n_pal++] = c;
	}
	for (uint pi = 0; pi < 32; pi++)
	{
		out[0x20 + 2 * pi] = palette[pi];
		out[0x21 + 2 * pi] = palette[pi] >> 8;
	}
	for (uint px = 0; px < pixels; px++)
	{
		const u8 *p = rgba + 4 * px;
		const int r = p[0] >> 3, g = p[1] >> 3, b = p[2] >> 3;
		uint best = 0, best_dist = UINT_MAX;
		for (uint pi = 0; pi < n_pal; pi++)
		{
			const int dr = r - (palette[pi] & 31);
			const int dg = g - (palette[pi] >> 5 & 31);
			const int db = b - (palette[pi] >> 10 & 31);
			const uint dist = dr * dr + dg * dg + db * db;
			if (dist < best_dist)
			{
				best = pi;
				best_dist = dist;
			}
		}
		out[0x60 + px] = ((p[3] * 7 + 127) / 255) << 5 | best;
	}
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

enumError DecodeBNR_RGBA (u8 **dest, const u8 *src, uint src_size)
{
	if (!dest || !src || src_size < 0x20 + 96 * 32 * 2
		|| (memcmp (src, "BNR1", 4) && memcmp (src, "BNR2", 4)))
		return EINVAL;
	u8 *rgba = MALLOC (96 * 32 * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	const u8 *pixels = src + 0x20;
	for (uint by = 0; by < 32 / 4; by++)
		for (uint bx = 0; bx < 96 / 4; bx++)
			for (uint y = 0; y < 4; y++)
				for (uint x = 0; x < 4; x++)
				{
					const uint pi = 16 * (by * (96 / 4) + bx) + 4 * y + x;
					const u16 c = rd_be16 (pixels + 2 * pi);
					u8 *d = rgba + 4 * ((4 * by + y) * 96 + 4 * bx + x);
					if (c & 0x8000)
					{
						d[0] = expand5 (c >> 10 & 31);
						d[1] = expand5 (c >> 5 & 31);
						d[2] = expand5 (c & 31);
						d[3] = 255;
					}
					else
					{
						d[0] = (c >> 8 & 15) * 17;
						d[1] = (c >> 4 & 15) * 17;
						d[2] = (c & 15) * 17;
						d[3] = (c >> 12 & 7) * 255 / 7;
					}
				}
	*dest = rgba;
	return ERR_OK;
}

enumError EncodeBNR_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	if (!dest || !dest_size || !rgba || width != 96 || height != 32)
		return EINVAL;
	// BNR1 is 0x1960 bytes: 0x20-byte header, 96x32 RGB5A3 icon, and six
	// zero-filled Shift-JIS title fields.  Empty fields are legal and make a
	// useful canonical banner when the input is a PNG rather than a BNR file.
	const uint size = 0x1960;
	u8 *out = CALLOC (1, size);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "BNR1", 4);
	u8 *pixels = out + 0x20;
	for (uint by = 0; by < 32 / 4; by++)
		for (uint bx = 0; bx < 96 / 4; bx++)
			for (uint y = 0; y < 4; y++)
				for (uint x = 0; x < 4; x++)
				{
					const u8 *s = rgba + 4 * ((4 * by + y) * 96 + 4 * bx + x);
					u16 c;
					if (s[3] >= 224)
						c = 0x8000 | (u16)(s[0] >> 3) << 10 | (u16)(s[1] >> 3) << 5 | (s[2] >> 3);
					else
						c = (u16)((s[3] * 7 + 127) / 255) << 12 | (u16)(s[0] >> 4) << 8
							| (u16)(s[1] >> 4) << 4 | (s[2] >> 4);
					const uint pi = 16 * (by * (96 / 4) + bx) + 4 * y + x;
					wr_be16 (pixels + 2 * pi, c);
				}
	*dest = out;
	*dest_size = size;
	return ERR_OK;
}

enumError EncodeBRFNT_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint cell_w, uint cell_h)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	if (!cell_w)
		cell_w = (width >= 16) ? 16 : width;
	if (!cell_h)
		cell_h = (height >= 16) ? 16 : height;
	uint cols = width / cell_w;
	uint rows = height / cell_h;
	if (!cols)
		cols = 1;
	if (!rows)
		rows = 1;
	uint n_chars = cols * rows;

	uint tw = (width + 3) & ~3u;
	uint th = (height + 3) & ~3u;
	uint tex_size = tw * th * 4;
	u8 *tex_data = CALLOC (1, tex_size);
	if (!tex_data)
		return ERR_OUT_OF_MEMORY;

	uint out_idx = 0;
	for (uint by = 0; by < th; by += 4)
	{
		for (uint bx = 0; bx < tw; bx += 4)
		{
			// 16 AR pairs
			for (uint y = 0; y < 4; y++)
			{
				for (uint x = 0; x < 4; x++)
				{
					uint px = bx + x;
					uint py = by + y;
					u8 r = 0, g = 0, b = 0, a = 0;
					if (px < width && py < height)
					{
						uint idx = (py * width + px) * 4;
						r = rgba[idx];
						g = rgba[idx + 1];
						b = rgba[idx + 2];
						a = rgba[idx + 3];
					}
					tex_data[out_idx++] = a;
					tex_data[out_idx++] = r;
				}
			}
			// 16 GB pairs
			for (uint y = 0; y < 4; y++)
			{
				for (uint x = 0; x < 4; x++)
				{
					uint px = bx + x;
					uint py = by + y;
					u8 r = 0, g = 0, b = 0, a = 0;
					if (px < width && py < height)
					{
						uint idx = (py * width + px) * 4;
						r = rgba[idx];
						g = rgba[idx + 1];
						b = rgba[idx + 2];
						a = rgba[idx + 3];
					}
					tex_data[out_idx++] = g;
					tex_data[out_idx++] = b;
				}
			}
		}
	}

	uint tglp_size = (0x30 + tex_size + 3) & ~3u;
	uint cwdh_payload_size = n_chars * 3;
	uint cwdh_size = (0x10 + cwdh_payload_size + 3) & ~3u;
	uint cmap_size = 0x14;
	uint total_size = 0x10 + 0x20 + tglp_size + cwdh_size + cmap_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (tex_data);
		return ERR_OUT_OF_MEMORY;
	}

	// Header (16 bytes)
	memcpy (out, "RFNT", 4);
	wr_be16 (out + 4, 0xFEFF); // BOM
	wr_be16 (out + 6, 0x0104); // version
	wr_be32 (out + 8, total_size);
	wr_be16 (out + 12, 0x0010); // header size
	wr_be16 (out + 14, 0x0004); // sections count

	// FINF (32 bytes) at offset 0x10
	u8 *finf = out + 0x10;
	memcpy (finf, "FINF", 4);
	wr_be32 (finf + 4, 0x00000020);
	finf[8] = 0; // glyph
	finf[9] = (u8)cell_h; // line feed
	wr_be16 (finf + 10, 0); // alter char
	finf[12] = 0; // left space
	finf[13] = (u8)cell_w; // glyph width
	finf[14] = (u8)cell_w; // char width
	finf[15] = 0; // UTF-8 encoding

	uint tglp_off = 0x30;
	uint cwdh_off = tglp_off + tglp_size;
	uint cmap_off = cwdh_off + cwdh_size;

	wr_be32 (finf + 16, tglp_off + 8); // ptr to TGLP data
	wr_be32 (finf + 20, cwdh_off + 8); // ptr to CWDH data
	wr_be32 (finf + 24, cmap_off + 8); // ptr to CMAP data
	finf[28] = (u8)cell_h; // height
	finf[29] = (u8)cell_w; // width
	finf[30] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // ascent
	finf[31] = 0; // reserved

	// TGLP at offset 0x30
	u8 *tglp = out + tglp_off;
	memcpy (tglp, "TGLP", 4);
	wr_be32 (tglp + 4, tglp_size);
	tglp[8] = (u8)cell_w;
	tglp[9] = (u8)cell_h;
	tglp[10] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // baseline
	tglp[11] = (u8)cell_w; // max char width
	wr_be32 (tglp + 12, tex_size); // sheet size
	wr_be16 (tglp + 16, 1); // sheet count
	wr_be16 (tglp + 18, 6); // format = RGBA8
	wr_be16 (tglp + 20, (u16)rows);
	wr_be16 (tglp + 22, (u16)cols);
	wr_be16 (tglp + 24, (u16)width);
	wr_be16 (tglp + 26, (u16)height);
	wr_be32 (tglp + 28, tglp_off + 0x30); // data offset
	memcpy (tglp + 0x30, tex_data, tex_size);
	FREE (tex_data);

	// CWDH at cwdh_off
	u8 *cwdh = out + cwdh_off;
	memcpy (cwdh, "CWDH", 4);
	wr_be32 (cwdh + 4, cwdh_size);
	wr_be16 (cwdh + 8, 0); // first index
	wr_be16 (cwdh + 10, (u16)(n_chars - 1)); // last index
	wr_be32 (cwdh + 12, 0); // next cwdh
	for (uint i = 0; i < n_chars; i++)
	{
		cwdh[16 + i * 3] = 0;
		cwdh[16 + i * 3 + 1] = (u8)cell_w;
		cwdh[16 + i * 3 + 2] = (u8)cell_w;
	}

	// CMAP at cmap_off
	u8 *cmap = out + cmap_off;
	memcpy (cmap, "CMAP", 4);
	wr_be32 (cmap + 4, cmap_size);
	wr_be16 (cmap + 8, 0x0020); // first char code
	wr_be16 (cmap + 10, (u16)(0x0020 + n_chars - 1)); // last char code
	wr_be16 (cmap + 12, 0); // direct mapping
	wr_be16 (cmap + 14, 0); // reserved
	wr_be32 (cmap + 16, 0); // next cmap
	wr_be32 (cmap + 20, 0); // index offset

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError EncodeBRFNA_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint cell_w, uint cell_h)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	if (!cell_w)
		cell_w = (width >= 16) ? 16 : width;
	if (!cell_h)
		cell_h = (height >= 16) ? 16 : height;
	uint cols = width / cell_w;
	uint rows = height / cell_h;
	if (!cols)
		cols = 1;
	if (!rows)
		rows = 1;
	uint n_chars = cols * rows;

	uint tw = (width + 3) & ~3u;
	uint th = (height + 3) & ~3u;
	uint tex_size = tw * th * 4;
	u8 *tex_data = CALLOC (1, tex_size);
	if (!tex_data)
		return ERR_OUT_OF_MEMORY;

	uint out_idx = 0;
	for (uint by = 0; by < th; by += 4)
	{
		for (uint bx = 0; bx < tw; bx += 4)
		{
			// 16 AR pairs
			for (uint y = 0; y < 4; y++)
			{
				for (uint x = 0; x < 4; x++)
				{
					uint px = bx + x;
					uint py = by + y;
					u8 r = 0, g = 0, b = 0, a = 0;
					if (px < width && py < height)
					{
						uint idx = (py * width + px) * 4;
						r = rgba[idx];
						g = rgba[idx + 1];
						b = rgba[idx + 2];
						a = rgba[idx + 3];
					}
					tex_data[out_idx++] = a;
					tex_data[out_idx++] = r;
				}
			}
			// 16 GB pairs
			for (uint y = 0; y < 4; y++)
			{
				for (uint x = 0; x < 4; x++)
				{
					uint px = bx + x;
					uint py = by + y;
					u8 r = 0, g = 0, b = 0, a = 0;
					if (px < width && py < height)
					{
						uint idx = (py * width + px) * 4;
						r = rgba[idx];
						g = rgba[idx + 1];
						b = rgba[idx + 2];
						a = rgba[idx + 3];
					}
					tex_data[out_idx++] = g;
					tex_data[out_idx++] = b;
				}
			}
		}
	}

	u8 *packed_sheet = 0;
	uint packed_size = 0;
	enumError lz_err = EncodeLZ10LZ11 (&packed_sheet, &packed_size, tex_data, tex_size, false);
	FREE (tex_data);
	if (lz_err)
		return lz_err;

	uint comp_chunk_size = 4 + packed_size;
	uint tglp_size = (0x30 + comp_chunk_size + 3) & ~3u;
	uint cwdh_payload_size = n_chars * 3;
	uint cwdh_size = (0x10 + cwdh_payload_size + 3) & ~3u;
	uint cmap_size = 0x14;
	uint total_size = 0x10 + 0x20 + tglp_size + cwdh_size + cmap_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (packed_sheet);
		return ERR_OUT_OF_MEMORY;
	}

	// Header (16 bytes)
	memcpy (out, "RFNA", 4);
	wr_be16 (out + 4, 0xFEFF); // BOM
	wr_be16 (out + 6, 0x0104); // version
	wr_be32 (out + 8, total_size);
	wr_be16 (out + 12, 0x0010); // header size
	wr_be16 (out + 14, 0x0004); // sections count

	// FINF (32 bytes) at offset 0x10
	u8 *finf = out + 0x10;
	memcpy (finf, "FINF", 4);
	wr_be32 (finf + 4, 0x00000020);
	finf[8] = 0; // glyph
	finf[9] = (u8)cell_h; // line feed
	wr_be16 (finf + 10, 0); // alter char
	finf[12] = 0; // left space
	finf[13] = (u8)cell_w; // glyph width
	finf[14] = (u8)cell_w; // char width
	finf[15] = 0; // UTF-8 encoding

	uint tglp_off = 0x30;
	uint cwdh_off = tglp_off + tglp_size;
	uint cmap_off = cwdh_off + cwdh_size;

	wr_be32 (finf + 16, tglp_off + 8); // ptr to TGLP data
	wr_be32 (finf + 20, cwdh_off + 8); // ptr to CWDH data
	wr_be32 (finf + 24, cmap_off + 8); // ptr to CMAP data
	finf[28] = (u8)cell_h; // height
	finf[29] = (u8)cell_w; // width
	finf[30] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // ascent
	finf[31] = 0; // reserved

	// TGLP at offset 0x30
	u8 *tglp = out + tglp_off;
	memcpy (tglp, "TGLP", 4);
	wr_be32 (tglp + 4, tglp_size);
	tglp[8] = (u8)cell_w;
	tglp[9] = (u8)cell_h;
	tglp[10] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // baseline
	tglp[11] = (u8)cell_w; // max char width
	wr_be32 (tglp + 12, tex_size); // uncompressed sheet size
	wr_be16 (tglp + 16, 1); // sheet count
	wr_be16 (tglp + 18, 0x8006); // format = compressed RGBA8
	wr_be16 (tglp + 20, (u16)rows);
	wr_be16 (tglp + 22, (u16)cols);
	wr_be16 (tglp + 24, (u16)width);
	wr_be16 (tglp + 26, (u16)height);
	wr_be32 (tglp + 28, tglp_off + 0x30); // data offset

	wr_be32 (tglp + 0x30, packed_size); // 4-byte BE compressed size prefix
	memcpy (tglp + 0x34, packed_sheet, packed_size);
	FREE (packed_sheet);

	// CWDH at cwdh_off
	u8 *cwdh = out + cwdh_off;
	memcpy (cwdh, "CWDH", 4);
	wr_be32 (cwdh + 4, cwdh_size);
	wr_be16 (cwdh + 8, 0); // first index
	wr_be16 (cwdh + 10, (u16)(n_chars - 1)); // last index
	wr_be32 (cwdh + 12, 0); // next cwdh
	for (uint i = 0; i < n_chars; i++)
	{
		cwdh[16 + i * 3] = 0;
		cwdh[16 + i * 3 + 1] = (u8)cell_w;
		cwdh[16 + i * 3 + 2] = (u8)cell_w;
	}

	// CMAP at cmap_off
	u8 *cmap = out + cmap_off;
	memcpy (cmap, "CMAP", 4);
	wr_be32 (cmap + 4, cmap_size);
	wr_be16 (cmap + 8, 0x0020); // first char code
	wr_be16 (cmap + 10, (u16)(0x0020 + n_chars - 1)); // last char code
	wr_be16 (cmap + 12, 0); // direct mapping
	wr_be16 (cmap + 14, 0); // reserved
	wr_be32 (cmap + 16, 0); // next cmap
	wr_be32 (cmap + 20, 0); // index offset

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError EncodeBCFNT_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height,
	uint cell_w, uint cell_h, bool is_wiiu)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	if (!cell_w)
		cell_w = (width >= 16) ? 16 : width;
	if (!cell_h)
		cell_h = (height >= 16) ? 16 : height;
	uint cols = width / cell_w;
	uint rows = height / cell_h;
	if (!cols)
		cols = 1;
	if (!rows)
		rows = 1;
	uint n_chars = cols * rows;

	uint tw = (width + 7) & ~7u;
	uint th = (height + 7) & ~7u;
	uint tex_size = tw * th * 4;
	u8 *tex_data = CALLOC (1, tex_size);
	if (!tex_data)
		return ERR_OUT_OF_MEMORY;

	for (uint y = 0; y < height; y++)
		memcpy (tex_data + y * width * 4, rgba + y * width * 4, width * 4);

	uint tglp_size = (0x20 + tex_size + 3) & ~3u;
	uint cwdh_payload_size = n_chars * 3;
	uint cwdh_size = (0x10 + cwdh_payload_size + 3) & ~3u;
	uint cmap_size = 0x14;
	uint header_size = 0x14;
	uint total_size = header_size + 0x20 + tglp_size + cwdh_size + cmap_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (tex_data);
		return ERR_OUT_OF_MEMORY;
	}

#define CF_W16(p, v)                                                                               \
	do                                                                                             \
	{                                                                                              \
		if (is_wiiu)                                                                               \
			wr_be16 (p, v);                                                                        \
		else                                                                                       \
			wr_le16 (p, v);                                                                        \
	} while (0)
#define CF_W32(p, v)                                                                               \
	do                                                                                             \
	{                                                                                              \
		if (is_wiiu)                                                                               \
			wr_be32 (p, v);                                                                        \
		else                                                                                       \
			wr_le32 (p, v);                                                                        \
	} while (0)

	// Header (20 bytes)
	memcpy (out, is_wiiu ? "FFNT" : "CFNT", 4);
	if (is_wiiu)
		wr_be16 (out + 4, 0xFEFF); // BOM
	else
		wr_le16 (out + 4, 0xFEFF);
	CF_W16 (out + 6, header_size);
	CF_W32 (out + 8, is_wiiu ? 0x04000000 : 0x03000000); // version
	CF_W32 (out + 12, total_size);
	CF_W16 (out + 16, 4); // section count
	CF_W16 (out + 18, 0); // reserved

	// FINF (32 bytes) at offset header_size (0x14)
	u8 *finf = out + header_size;
	memcpy (finf, "FINF", 4);
	CF_W32 (finf + 4, 0x00000020);
	finf[8] = 1; // font_type
	finf[9] = (u8)cell_h; // line feed
	CF_W16 (finf + 10, 0); // alter char
	finf[12] = 0; // left space
	finf[13] = (u8)cell_w; // glyph width
	finf[14] = (u8)cell_w; // char width
	finf[15] = 0; // UTF-8

	uint tglp_off = header_size + 0x20;
	uint cwdh_off = tglp_off + tglp_size;
	uint cmap_off = cwdh_off + cwdh_size;

	CF_W32 (finf + 16, 0);
	CF_W32 (finf + 20, tglp_off + 8); // ptr to TGLP data
	CF_W32 (finf + 24, cwdh_off + 8); // ptr to CWDH data
	CF_W32 (finf + 28, cmap_off + 8); // ptr to CMAP data

	// TGLP at offset tglp_off
	u8 *tglp = out + tglp_off;
	memcpy (tglp, "TGLP", 4);
	CF_W32 (tglp + 4, tglp_size);
	tglp[8] = (u8)cell_w;
	tglp[9] = (u8)cell_h;
	tglp[10] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // baseline
	tglp[11] = (u8)cell_w; // max char width
	CF_W32 (tglp + 12, tex_size); // sheet size
	CF_W16 (tglp + 16, 1); // sheet count
	CF_W16 (tglp + 18, 0); // format = RGBA8 (CTR/Cafe format 0)
	CF_W16 (tglp + 20, (u16)rows);
	CF_W16 (tglp + 22, (u16)cols);
	CF_W16 (tglp + 24, (u16)width);
	CF_W16 (tglp + 26, (u16)height);
	CF_W32 (tglp + 28, tglp_off + 0x20); // data offset
	memcpy (tglp + 0x20, tex_data, tex_size);
	FREE (tex_data);

	// CWDH at cwdh_off
	u8 *cwdh = out + cwdh_off;
	memcpy (cwdh, "CWDH", 4);
	CF_W32 (cwdh + 4, cwdh_size);
	CF_W16 (cwdh + 8, 0); // first index
	CF_W16 (cwdh + 10, (u16)(n_chars - 1)); // last index
	CF_W32 (cwdh + 12, 0); // next cwdh
	for (uint i = 0; i < n_chars; i++)
	{
		cwdh[16 + i * 3] = 0;
		cwdh[16 + i * 3 + 1] = (u8)cell_w;
		cwdh[16 + i * 3 + 2] = (u8)cell_w;
	}

	// CMAP at cmap_off
	u8 *cmap = out + cmap_off;
	memcpy (cmap, "CMAP", 4);
	CF_W32 (cmap + 4, cmap_size);
	CF_W16 (cmap + 8, 0x0020); // first char code
	CF_W16 (cmap + 10, (u16)(0x0020 + n_chars - 1)); // last char code
	CF_W16 (cmap + 12, 0); // direct mapping
	CF_W16 (cmap + 14, 0); // reserved
	CF_W32 (cmap + 16, 0); // next cmap
	CF_W32 (cmap + 20, 0); // index offset

#undef CF_W16
#undef CF_W32

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError DecodeNCGR_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 0x30 || memcmp (src, "RGCN", 4)
		|| memcmp (src + 0x10, "RAHC", 4))
		return EINVAL;
	// Nitro's RAHC header stores the data byte count and an offset relative
	// to RAHC+8.  The normal resource layout has data at RAHC+0x20.
	const u8 *rahc = src + 0x10;
	const uint num_y = rd_le16 (rahc + 0x08);
	const uint num_x = rd_le16 (rahc + 0x0a);
	const uint depth = rd_le32 (rahc + 0x0c);
	const uint data_size = rd_le32 (rahc + 0x18);
	const uint data_off = 8 + rd_le32 (rahc + 0x1c);
	const uint bpt = depth == 3 ? 32 : depth == 4 ? 64 : 0;
	if (!bpt || !data_size || data_size % bpt || data_off > src_size - 0x10
		|| data_size > src_size - (0x10 + data_off))
		return EINVAL;
	const uint n_tiles = data_size / bpt;
	uint cols = 16;
	if (num_x > 0 && num_x != 0xFFFF)
		cols = num_x;
	else if (n_tiles < 16)
		cols = n_tiles;
	else if (n_tiles % 32 == 0 && n_tiles >= 32)
		cols = 32;

	const uint rows = (num_y > 0 && num_y != 0xFFFF && num_x * num_y >= n_tiles)
		? num_y
		: (n_tiles + cols - 1) / cols;
	const uint w = 8 * cols, h = 8 * rows;
	if (!w || !h || (u64)w * h > NFMT_MAX_OUTPUT / 4)
		return EFBIG;
	u8 *out = CALLOC (1, w * h * 4);
	if (!out)
		return ERR_CANT_CREATE;
	const u8 *tiles = rahc + data_off;
	for (uint tile = 0; tile < n_tiles; tile++)
	{
		const uint tile_x = tile % cols;
		const uint tile_y = tile / cols;
		for (uint y = 0; y < 8; y++)
			for (uint x = 0; x < 8; x++)
			{
				const uint pos = tile * bpt + (depth == 3 ? 4 * y + x / 2 : 8 * y + x);
				const u8 index = depth == 3 ? (tiles[pos] >> (4 * (x & 1))) & 15 : tiles[pos];
				u8 *p = out + 4 * ((tile_y * 8 + y) * w + tile_x * 8 + x);
				p[0] = p[1] = p[2] = depth == 3 ? index * 17 : index;
				p[3] = index ? 255 : 0;
			}
	}
	*dest = out;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError DecodeNCLR_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 0x28 || memcmp (src, "RLCN", 4)
		|| memcmp (src + 0x10, "TTLP", 4))
		return EINVAL;

	// TTLP's data offset is relative to TTLP+8.  It is normally 0x10,
	// yielding palette data at file offset 0x28.
	const uint depth = rd_le32 (src + 0x18);
	const uint data_size = rd_le32 (src + 0x20);
	const uint data_off = 0x18 + rd_le32 (src + 0x24);
	if ((depth != 3 && depth != 4) || !data_size || data_size & 1 || data_off > src_size
		|| data_size > src_size - data_off)
		return EINVAL;
	const uint entries = data_size / 2;
	const uint max_entries = 1024;
	if (!entries || entries > max_entries)
		return EINVAL;

	const uint cell = 8, cols = 16, rows = (entries + cols - 1) / cols;
	const uint w = cols * cell, h = rows * cell;
	if ((u64)w * h > NFMT_MAX_OUTPUT / 4)
		return EFBIG;
	u8 *out = MALLOC (w * h * 4);
	if (!out)
		return ERR_CANT_CREATE;

	for (uint entry = 0; entry < entries; entry++)
	{
		const u16 c = rd_le16 (src + data_off + 2 * entry);
		const u8 r = (c & 31) * 255 / 31;
		const u8 g = ((c >> 5) & 31) * 255 / 31;
		const u8 b = ((c >> 10) & 31) * 255 / 31;
		for (uint y = 0; y < cell; y++)
			for (uint x = 0; x < cell; x++)
			{
				u8 *p = out + 4 * ((entry / cols * cell + y) * w + entry % cols * cell + x);
				p[0] = r;
				p[1] = g;
				p[2] = b;
				p[3] = 255;
			}
	}
	*dest = out;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError EncodeNCGR_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool is_8bpp)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;

	const uint cols = (width + 7) / 8;
	const uint rows = (height + 7) / 8;
	const uint n_tiles = cols * rows;
	if (!n_tiles || n_tiles > 65535)
		return EFBIG;

	const uint bpt = is_8bpp ? 64 : 32;
	const uint tile_data_size = n_tiles * bpt;
	const uint total_size = 0x30 + tile_data_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "RGCN", 4);
	wr_le16 (out + 4, 0xFFFE);
	wr_le16 (out + 6, 0x0101);
	wr_le32 (out + 8, total_size);
	wr_le16 (out + 12, 16);
	wr_le16 (out + 14, 1);

	u8 *rahc = out + 16;
	memcpy (rahc, "RAHC", 4);
	wr_le32 (rahc + 4, 0x20 + tile_data_size);
	wr_le16 (rahc + 8, (u16)rows);
	wr_le16 (rahc + 10, (u16)cols);
	wr_le32 (rahc + 12, is_8bpp ? 4 : 3);
	wr_le32 (rahc + 16, 0);
	wr_le32 (rahc + 20, 0);
	wr_le32 (rahc + 24, tile_data_size);
	wr_le32 (rahc + 28, 0x18);

	u8 *tiles = rahc + 0x20;
	for (uint tile = 0; tile < n_tiles; tile++)
	{
		const uint tile_col = tile % (cols < 16 ? cols : 16);
		const uint tile_row = tile / (cols < 16 ? cols : 16);
		for (uint y = 0; y < 8; y++)
		{
			for (uint x = 0; x < 8; x++)
			{
				const uint px = tile_col * 8 + x;
				const uint py = tile_row * 8 + y;
				u8 val = 0;
				if (px < width && py < height)
				{
					const u8 *p = rgba + 4 * (py * width + px);
					if (p[3] > 0)
					{
						if (is_8bpp)
							val = p[0];
						else
							val = (u8)((p[0] * 15 + 127) / 255);
					}
				}

				if (is_8bpp)
				{
					tiles[tile * 64 + 8 * y + x] = val;
				}
				else
				{
					const uint pos = tile * 32 + 4 * y + x / 2;
					if (x & 1)
						tiles[pos] |= (val & 15) << 4;
					else
						tiles[pos] = (val & 15);
				}
			}
		}
	}

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError EncodeNCLR_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;

	uint n_colors = 0;
	u16 colors[256] = { 0 };

	// If formatted as 8x8 swatch tiles (16 cols x N rows * 8)
	if (width >= 8 && height >= 8 && (width % 8 == 0) && (height % 8 == 0))
	{
		const uint cols = width / 8;
		const uint rows = height / 8;
		const uint total_swatches = cols * rows;
		n_colors = total_swatches > 256 ? 256 : total_swatches;
		for (uint i = 0; i < n_colors; i++)
		{
			const uint sx = (i % cols) * 8 + 4;
			const uint sy = (i / cols) * 8 + 4;
			const u8 *p = rgba + 4 * (sy * width + sx);
			const u16 r = (p[0] * 31 + 127) / 255;
			const u16 g = (p[1] * 31 + 127) / 255;
			const u16 b = (p[2] * 31 + 127) / 255;
			colors[i] = (r & 31) | ((g & 31) << 5) | ((b & 31) << 10);
		}
	}
	else
	{
		const uint total_pixels = width * height;
		n_colors = total_pixels > 256 ? 256 : total_pixels;
		for (uint i = 0; i < n_colors; i++)
		{
			const u8 *p = rgba + 4 * i;
			const u16 r = (p[0] * 31 + 127) / 255;
			const u16 g = (p[1] * 31 + 127) / 255;
			const u16 b = (p[2] * 31 + 127) / 255;
			colors[i] = (r & 31) | ((g & 31) << 5) | ((b & 31) << 10);
		}
	}

	if (n_colors == 0)
		n_colors = 16;
	const uint total_colors = n_colors <= 16 ? 16 : 256;
	const uint data_size = total_colors * 2;
	const uint total_size = 0x28 + data_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "RLCN", 4);
	wr_le16 (out + 4, 0xFFFE);
	wr_le16 (out + 6, 0x0100);
	wr_le32 (out + 8, total_size);
	wr_le16 (out + 12, 16);
	wr_le16 (out + 14, 1);

	u8 *ttlp = out + 16;
	memcpy (ttlp, "TTLP", 4);
	wr_le32 (ttlp + 4, 0x18 + data_size);
	wr_le32 (ttlp + 8, total_colors <= 16 ? 3 : 4);
	wr_le32 (ttlp + 12, 0);
	wr_le32 (ttlp + 16, data_size);
	wr_le32 (ttlp + 20, 0x10);

	for (uint i = 0; i < total_colors; i++)
		wr_le16 (ttlp + 0x18 + 2 * i, colors[i]);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError ScanNCER (nintendo_ncer_t *ncer, const u8 *data, uint size)
{
	if (!ncer || !data || size < 0x30 || memcmp (data, "RECN", 4)
		|| memcmp (data + 0x10, "KBEC", 4))
		return EINVAL;
	const u8 *kbec = data + 0x10;
	const uint chunk_size = rd_le32 (kbec + 4);
	const uint n_cells = rd_le16 (kbec + 8);
	const uint entry_kind = rd_le16 (kbec + 10);
	const uint cell_size = entry_kind == 0 ? 8 : entry_kind == 1 ? 16 : 0;
	const uint cell_off = 8 + rd_le32 (kbec + 12);
	if (!chunk_size || chunk_size > size - 0x10 || !n_cells || !cell_size || cell_off > chunk_size
		|| n_cells > (chunk_size - cell_off) / cell_size)
		return EINVAL;
	const uint objects_off = cell_off + n_cells * cell_size;
	if (objects_off > chunk_size)
		return EINVAL;
	memset (ncer, 0, sizeof (*ncer));
	ncer->data = data;
	ncer->size = size;
	ncer->n_cells = n_cells;
	ncer->cell_size = cell_size;
	ncer->cells = kbec + cell_off;
	ncer->objects = kbec + objects_off;
	ncer->objects_size = chunk_size - objects_off;
	ncer->mapping_mode = chunk_size >= 20 ? rd_le32 (kbec + 16) : 0;
	for (uint i = 0; i < n_cells; i++)
	{
		const u8 *cell = ncer->cells + i * cell_size;
		const uint n_obj = rd_le16 (cell);
		const uint obj_off = rd_le32 (cell + 4);
		if (obj_off > ncer->objects_size || n_obj > (ncer->objects_size - obj_off) / 6)
			return EINVAL;
	}
	return ERR_OK;
}

enumError GetNCERCell (
	const nintendo_ncer_t *ncer, uint index, uint *n_objects, const u8 **oam_records)
{
	if (!ncer || !n_objects || !oam_records || index >= ncer->n_cells)
		return EINVAL;
	const u8 *cell = ncer->cells + index * ncer->cell_size;
	const uint count = rd_le16 (cell);
	const uint off = rd_le32 (cell + 4);
	if (off > ncer->objects_size || count > (ncer->objects_size - off) / 6)
		return EINVAL;
	*n_objects = count;
	*oam_records = ncer->objects + off;
	return ERR_OK;
}

enumError ScanNANR (nintendo_nanr_t *nanr, const u8 *data, uint size)
{
	if (!nanr || !data || size < 0x38 || memcmp (data, "RNAN", 4)
		|| memcmp (data + 0x10, "KNBA", 4))
		return EINVAL;
	const u8 *knba = data + 0x10;
	const uint chunk_size = rd_le32 (knba + 4);
	const uint n_anims = rd_le16 (knba + 8), n_frames = rd_le16 (knba + 10);
	const uint anim_off = 8 + rd_le32 (knba + 12);
	const uint frame_off = 8 + rd_le32 (knba + 16);
	const uint data_off = 8 + rd_le32 (knba + 20);
	if (!chunk_size || chunk_size > size - 0x10 || !n_anims || !n_frames || anim_off > chunk_size
		|| n_anims > (chunk_size - anim_off) / 16 || frame_off > chunk_size
		|| n_frames > (chunk_size - frame_off) / 8 || data_off > chunk_size)
		return EINVAL;
	memset (nanr, 0, sizeof (*nanr));
	nanr->data = data;
	nanr->size = size;
	nanr->n_animations = n_anims;
	nanr->n_frames = n_frames;
	nanr->animations = knba + anim_off;
	nanr->frames = knba + frame_off;
	nanr->frames_size = n_frames * 8;
	nanr->frame_data = knba + data_off;
	nanr->frame_data_size = chunk_size - data_off;
	for (uint i = 0; i < n_anims; i++)
	{
		const u8 *anim = nanr->animations + 16 * i;
		const uint count = rd_le32 (anim);
		const uint off = rd_le32 (anim + 12);
		if (!count || off > nanr->frames_size || count > (nanr->frames_size - off) / 8)
			return EINVAL;
	}
	if (nanr->frame_data_size < 2)
		return EINVAL;
	for (uint i = 0; i < n_frames; i++)
	{
		const u8 *frame = nanr->frames + 8 * i;
		if (rd_le32 (frame) > nanr->frame_data_size - 2)
			return EINVAL;
	}
	return ERR_OK;
}

enumError GetNANRAnimation (
	const nintendo_nanr_t *nanr, uint index, uint *n_frames, const u8 **frame_records)
{
	if (!nanr || !n_frames || !frame_records || index >= nanr->n_animations)
		return EINVAL;
	const u8 *anim = nanr->animations + 16 * index;
	const uint count = rd_le32 (anim), off = rd_le32 (anim + 12);
	if (!count || off > nanr->frames_size || count > (nanr->frames_size - off) / 8)
		return EINVAL;
	*n_frames = count;
	*frame_records = nanr->frames + off;
	return ERR_OK;
}

static uint morton8 (uint x, uint y)
{
	return (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
}

// ETC1/ETC1A4 4x4 block decoder. The bit layout (base colors, table
// selection, per-pixel modifier index) was verified pixel-for-pixel
// against the independent `texture2ddecoder` reference decoder (both
// individual and differential color modes, both flip orientations) using
// real BFLIM sample data before this was written -- see commit message.
// Byte layout within the 16-byte ETC1A4 block (alpha first, then color)
// matches the documented Ohana3DS convention. The alpha nibble-to-pixel
// order and the block-to-tile arrangement for images larger than one
// 8x8-pixel tile are NOT independently verified (no oracle covers those);
// they follow the same tiling convention already used and verified for
// this codebase's other BFLIM pixel formats.
static const int16_t etc1_mod_table[8][4] = { { -8, -2, 2, 8 }, { -17, -5, 5, 17 },
	{ -29, -9, 9, 29 }, { -42, -13, 13, 42 }, { -60, -18, 18, 60 }, { -80, -24, 24, 80 },
	{ -106, -33, 33, 106 }, { -183, -47, 47, 183 } };

static inline u8 etc1_clamp255 (int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : (u8)v;
}

// data = 8 bytes ETC1 color block. out = 4x4 RGBA (row-major, 64 bytes).
static void decode_etc1_block (const u8 data[8], u8 *out)
{
	u64 v = 0;
	for (int i = 0; i < 8; i++)
		v = (v << 8) | data[7 - i];

	const int diffbit = (v >> 33) & 1;
	const int flipbit = (v >> 32) & 1;
	const int table1 = (v >> 37) & 7;
	const int table2 = (v >> 34) & 7;
	int r1, g1, b1, r2, g2, b2;

	if (!diffbit)
	{
		const int R1 = (v >> 60) & 0xF, R2 = (v >> 56) & 0xF, G1 = (v >> 52) & 0xF,
				  G2 = (v >> 48) & 0xF, B1 = (v >> 44) & 0xF, B2 = (v >> 40) & 0xF;
		r1 = (R1 << 4) | R1;
		g1 = (G1 << 4) | G1;
		b1 = (B1 << 4) | B1;
		r2 = (R2 << 4) | R2;
		g2 = (G2 << 4) | G2;
		b2 = (B2 << 4) | B2;
	}
	else
	{
		const int R1 = (v >> 59) & 0x1F, dR2 = (v >> 56) & 7;
		const int G1 = (v >> 51) & 0x1F, dG2 = (v >> 48) & 7;
		const int B1 = (v >> 43) & 0x1F, dB2 = (v >> 40) & 7;
		const int sR2 = R1 + (dR2 & 4 ? dR2 - 8 : dR2);
		const int sG2 = G1 + (dG2 & 4 ? dG2 - 8 : dG2);
		const int sB2 = B1 + (dB2 & 4 ? dB2 - 8 : dB2);
		r1 = (R1 << 3) | (R1 >> 2);
		g1 = (G1 << 3) | (G1 >> 2);
		b1 = (B1 << 3) | (B1 >> 2);
		r2 = (sR2 << 3) | (sR2 >> 2);
		g2 = (sG2 << 3) | (sG2 >> 2);
		b2 = (sB2 << 3) | (sB2 >> 2);
	}

	const u32 low = (u32)(v & 0xFFFFFFFF);
	const u16 msb_plane = (low >> 16) & 0xFFFF;
	const u16 lsb_plane = low & 0xFFFF;

	for (int x = 0; x < 4; x++)
		for (int y = 0; y < 4; y++)
		{
			const int p = x * 4 + y; // column-major pixel numbering
			const int msb = (msb_plane >> p) & 1;
			const int lsb = (lsb_plane >> p) & 1;
			const int sub = flipbit ? (y < 2 ? 0 : 1) : (x < 2 ? 0 : 1);
			const int table = sub == 0 ? table1 : table2;
			const int R = sub == 0 ? r1 : r2, G = sub == 0 ? g1 : g2, B = sub == 0 ? b1 : b2;
			int mod;
			if (msb && lsb)
				mod = etc1_mod_table[table][0];
			else if (msb && !lsb)
				mod = etc1_mod_table[table][1];
			else if (!msb && !lsb)
				mod = etc1_mod_table[table][2];
			else
				mod = etc1_mod_table[table][3];
			u8 *o = out + 4 * (y * 4 + x);
			o[0] = etc1_clamp255 (R + mod);
			o[1] = etc1_clamp255 (G + mod);
			o[2] = etc1_clamp255 (B + mod);
			o[3] = 255;
		}
}

// Decodes a plain ETC1 (BFLIM fmt 10, no alpha block -- opaque) tiled
// texture into RGBA8. Same block/tile arrangement as decode_etc1a4_tiled,
// just an 8-byte color-only block instead of 16 bytes.
static enumError decode_etc1_tiled (u8 *rgba, const u8 *src, uint w, uint h, uint data_size)
{
	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const uint bw = (tw + 3) / 4, bh = (th + 3) / 4;
	if ((u64)bw * bh * 8 > data_size)
		return EINVAL;
	for (uint by = 0; by < bh; by++)
		for (uint bx = 0; bx < bw; bx++)
		{
			const uint tile_idx = (by / 2) * (bw / 2) + bx / 2;
			const uint local = (bx & 1) | (by & 1) << 1;
			const uint block_idx = tile_idx * 4 + local;
			u8 px[64];
			decode_etc1_block (src + (u64)block_idx * 8, px);
			for (int ly = 0; ly < 4; ly++)
				for (int lx = 0; lx < 4; lx++)
				{
					const uint x = bx * 4 + lx, y = by * 4 + ly;
					if (x >= w || y >= h)
						continue;
					memcpy (rgba + 4 * (y * w + x), px + 4 * (ly * 4 + lx), 4);
				}
		}
	return ERR_OK;
}

// Decodes an ETC1A4 (BFLIM fmt 11) tiled texture into RGBA8. Block
// arrangement follows the same 8x8-tile Morton scheme as this file's other
// tiled BFLIM formats (morton8), applied at 4x4-block granularity.
static enumError decode_etc1a4_tiled (u8 *rgba, const u8 *src, uint w, uint h, uint data_size)
{
	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const uint bw = (tw + 3) / 4, bh = (th + 3) / 4; // blocks across/down (tile-padded)
	if ((u64)bw * bh * 16 > data_size)
		return EINVAL;
	for (uint by = 0; by < bh; by++)
		for (uint bx = 0; bx < bw; bx++)
		{
			const uint tile_idx = (by / 2) * (bw / 2) + bx / 2;
			const uint local = (bx & 1) | (by & 1) << 1;
			const uint block_idx = tile_idx * 4 + local;
			const u8 *block = src + (u64)block_idx * 16;
			u8 alpha4[8], color8[8];
			memcpy (alpha4, block, 8);
			memcpy (color8, block + 8, 8);
			u8 px[64];
			decode_etc1_block (color8, px);
			for (int ly = 0; ly < 4; ly++)
				for (int lx = 0; lx < 4; lx++)
				{
					const uint x = bx * 4 + lx, y = by * 4 + ly;
					if (x >= w || y >= h)
						continue;
					const int p = lx * 4 + ly; // same column-major numbering as color
					const u8 nib = (p & 1) ? (alpha4[p >> 1] >> 4) : (alpha4[p >> 1] & 0xF);
					u8 *d = rgba + 4 * (y * w + x);
					const u8 *s = px + 4 * (ly * 4 + lx);
					d[0] = s[0];
					d[1] = s[1];
					d[2] = s[2];
					d[3] = (u8)(nib * 17);
				}
		}
	return ERR_OK;
}

static void bc1_block_wrap (const u8 *b, u8 *out)
{
	decode_bc1_block (b, out, false);
}

// Decodes a BC1..BC5 (DXT-family) tiled BFLIM texture into RGBA8. Same
// 8x8-tile Morton scheme as the ETC1 decoders above; block_size is 8 bytes
// for BC1/BC4, 16 for BC2/BC3/BC5. Reuses the already-verified block
// decoders from lib-bntx.c (BC1..BC5 are a standard, platform-agnostic
// byte layout -- Switch BNTX and Wii U BFLIM both use the same block math,
// only the surrounding container/swizzle differs).
static enumError decode_bc_tiled (u8 *rgba, const u8 *src, uint w, uint h, uint data_size,
	uint block_size, void (*decode_block) (const u8 *, u8 *))
{
	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const uint bw = (tw + 3) / 4, bh = (th + 3) / 4;
	if ((u64)bw * bh * block_size > data_size)
		return EINVAL;
	for (uint by = 0; by < bh; by++)
		for (uint bx = 0; bx < bw; bx++)
		{
			const uint tile_idx = (by / 2) * (bw / 2) + bx / 2;
			const uint local = (bx & 1) | (by & 1) << 1;
			const uint block_idx = tile_idx * 4 + local;
			u8 px[64];
			decode_block (src + (u64)block_idx * block_size, px);
			for (int ly = 0; ly < 4; ly++)
				for (int lx = 0; lx < 4; lx++)
				{
					const uint x = bx * 4 + lx, y = by * 4 + ly;
					if (x >= w || y >= h)
						continue;
					memcpy (rgba + 4 * (y * w + x), px + 4 * (ly * 4 + lx), 4);
				}
		}
	return ERR_OK;
}

enumError DecodeFLIM_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 0x28)
		return EINVAL;
	const u8 *foot = src + src_size - 0x28;
	if ((memcmp (foot, "FLIM", 4) && memcmp (foot, "CLIM", 4))
		|| (foot[4] != 0xfe || foot[5] != 0xff) && (foot[4] != 0xff || foot[5] != 0xfe))
		return EINVAL;
	const bool be = foot[4] == 0xfe;
	u16 (*r16) (const u8 *) = be ? rd_be16 : rd_le16;
	u32 (*r32) (const u8 *) = be ? rd_be32 : rd_le32;
	if (r16 (foot + 6) != 0x14 || memcmp (foot + 0x14, "imag", 4) || r32 (foot + 0x18) != 0x10)
		return EINVAL;
	if (be)
	{
		const uint w = r16 (foot + 0x1c), h = r16 (foot + 0x1e);
		const uint bflim_fmt = foot[0x20];
		const uint flags = foot[0x23];
		const uint tile_mode = flags & 0x0f;
		const uint swizzle = (uint)((flags >> 4) & 7) << 16;
		const uint data_size = r32 (src + src_size - 4);
		if (!w || !h || w > 16384 || h > 16384 || data_size > src_size - 0x28)
			return EINVAL;

		uint gx2_fmt = 0;
		switch (bflim_fmt)
		{
			case 0x00: gx2_fmt = 0x0001; break; // R8_UNORM
			case 0x01: gx2_fmt = 0x0001; break; // R8_UNORM / L8
			case 0x02: gx2_fmt = 0x0001; break; // A8
			case 0x03: gx2_fmt = 0x0002; break; // R4_G4
			case 0x04: gx2_fmt = 0x0007; break; // R8_G8
			case 0x05: gx2_fmt = 0x0008; break; // R5_G6_B5
			case 0x06: gx2_fmt = 0x000a; break; // R5_G5_B5_A1
			case 0x07: gx2_fmt = 0x000b; break; // R4_G4_B4_A4
			case 0x08: gx2_fmt = 0x041a; break; // R8_G8_B8_A8_SRGB
			case 0x10: gx2_fmt = 0x0431; break; // BC1_SRGB
			case 0x11: gx2_fmt = 0x0432; break; // BC2_SRGB
			case 0x12: gx2_fmt = 0x0433; break; // BC3_SRGB
			case 0x13: gx2_fmt = 0x0034; break; // BC4_UNORM
			case 0x14: gx2_fmt = 0x0035; break; // BC5_UNORM
			case 0x20: gx2_fmt = 0x0433; break; // BC3_SRGB
			default: return EINVAL;
		}

		uint out_w = 0, out_h = 0;
		enumError err = DecodeGX2SurfaceSlice_RGBA (dest, &out_w, &out_h, 1, w, h, 1,
			gx2_fmt, 0, tile_mode, 0, swizzle, 0, 0, src, data_size);
		if (!err)
		{
			*width = out_w;
			*height = out_h;
			return ERR_OK;
		}
		return err;
	}

	const uint w = r16 (foot + 0x1c), h = r16 (foot + 0x1e);
	const uint fmt = foot[0x22], tile_mode = foot[0x23] & 31;
	const uint data_size = r32 (src + src_size - 4);

	if (fmt == 10 || fmt == 11) // ETC1 (fmt 10, opaque) / ETC1A4 (fmt 11): block-compressed
	{
		if (!w || !h || w > 16384 || h > 16384 || data_size > src_size - 0x28)
			return EINVAL;
		if ((u64)w * h > NFMT_MAX_OUTPUT / 4)
			return EINVAL;
		u8 *rgba = MALLOC (w * h * 4);
		if (!rgba)
			return ERR_CANT_CREATE;
		enumError err = fmt == 11 ? decode_etc1a4_tiled (rgba, src, w, h, data_size)
								  : decode_etc1_tiled (rgba, src, w, h, data_size);
		if (err)
		{
			FREE (rgba);
			return err;
		}
		*dest = rgba;
		*width = w;
		*height = h;
		return ERR_OK;
	}

	// BC1..BC5 (fmt 14=BC3, 15/16=BC4 [two IDs for the same format, per
	// Nintendo-File-Formats' documented BFLIM table], 17=BC5, and the
	// version-3.3.0.0 SRGB variants 21=BC1_SRGB/22=BC2_SRGB/23=BC3_SRGB --
	// SRGB only changes gamma interpretation, not the block bit layout, so
	// it decodes identically to the UNORM form here). fmt 12/13 are left as
	// the existing L4/A4 nibble path below: real Wii U BFLIM files using
	// those IDs would need BC1/BC2 instead per the same table, but no file
	// in any real corpus checked against this fork has been observed using
	// them, so that's flagged as an open question rather than guessed at.
	if (fmt == 14 || fmt == 15 || fmt == 16 || fmt == 17 || fmt == 21 || fmt == 22 || fmt == 23)
	{
		if (!w || !h || w > 16384 || h > 16384 || data_size > src_size - 0x28)
			return EINVAL;
		if ((u64)w * h > NFMT_MAX_OUTPUT / 4)
			return EINVAL;
		u8 *rgba = MALLOC (w * h * 4);
		if (!rgba)
			return ERR_CANT_CREATE;
		enumError err;
		switch (fmt)
		{
			case 14:
			case 23:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 16, decode_bc3_block);
				break;
			case 15:
			case 16:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 8, decode_bc4_block);
				break;
			case 17:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 16, decode_bc5_block);
				break;
			case 22:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 16, decode_bc2_block);
				break;
			default /*21*/:
				err = decode_bc_tiled (rgba, src, w, h, data_size, 8, bc1_block_wrap);
				break;
		}
		if (err)
		{
			FREE (rgba);
			return err;
		}
		*dest = rgba;
		*width = w;
		*height = h;
		return ERR_OK;
	}

	const bool nibble_fmt = fmt == 12 || fmt == 13; // L4 / A4: 4 bits/pixel
	uint bpp;
	switch (fmt)
	{
		case 0:
		case 1:
		case 2:
			bpp = 1;
			break;
		case 12:
		case 13:
			bpp = 0;
			break; // nibble_fmt: handled separately below
		case 3:
		case 5:
		case 7:
		case 8:
			bpp = 2;
			break;
		case 9:
		case 20:
			bpp = 4;
			break;
		default:
			return EINVAL;
	}
	if (!w || !h || w > 16384 || h > 16384 || data_size > src_size - 0x28)
		return EINVAL;
	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const u64 need = nibble_fmt ? ((u64)(tile_mode ? tw : w) * (tile_mode ? th : h) + 1) / 2
								: (u64)(tile_mode ? tw : w) * (tile_mode ? th : h) * bpp;
	if (need > data_size || (u64)w * h > NFMT_MAX_OUTPUT / 4)
		return EINVAL;
	u8 *rgba = MALLOC (w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			uint pos;
			if (tile_mode)
				pos = ((y / 8) * (tw / 8) + x / 8) * 64 + morton8 (x & 7, y & 7);
			else
				pos = y * w + x;
			u8 *d = rgba + 4 * (y * w + x);
			if (nibble_fmt)
			{
				const u8 byte = src[pos >> 1];
				const u8 nib = (pos & 1) ? (byte >> 4) : (byte & 0xF);
				const u8 v = (u8)(nib * 17);
				d[0] = d[1] = d[2] = d[3] = v;
				continue;
			}
			const u8 *p = src + pos * bpp;
			if (fmt == 0 || fmt == 1)
				d[0] = d[1] = d[2] = d[3] = p[0];
			else if (fmt == 2) // LA4: low nibble = luminance, high nibble = alpha
			{
				d[0] = d[1] = d[2] = (u8)((p[0] & 0xF) * 17);
				d[3] = (u8)((p[0] >> 4) * 17);
			}
			else if (fmt == 3) // LA8: byte0 = luminance, byte1 = alpha
			{
				d[0] = d[1] = d[2] = p[0];
				d[3] = p[1];
			}
			else if (fmt == 5)
			{
				const u16 c = r16 (p);
				d[0] = expand5 (c >> 11);
				d[1] = (c >> 5 & 63) * 255 / 63;
				d[2] = expand5 (c);
				d[3] = 255;
			}
			else if (fmt == 7)
			{
				const u16 c = r16 (p);
				d[0] = expand5 (c >> 11);
				d[1] = expand5 (c >> 6);
				d[2] = expand5 (c >> 1);
				d[3] = c & 1 ? 255 : 0;
			}
			else if (fmt == 8)
			{
				const u16 c = r16 (p);
				d[0] = (c >> 12) * 17;
				d[1] = (c >> 8 & 15) * 17;
				d[2] = (c >> 4 & 15) * 17;
				d[3] = (c & 15) * 17;
			}
			else // CTR/GX2 RGBA8 byte storage is A,B,G,R.
			{
				d[0] = p[3];
				d[1] = p[2];
				d[2] = p[1];
				d[3] = p[0];
			}
		}
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError EncodeFLIM_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool bclim)
{
	if (!dest || !dest_size || !rgba || !width || !height || width > 16384 || height > 16384)
		return EINVAL;
	const uint tw = (width + 7) & ~7u, th = (height + 7) & ~7u;
	const u64 pixels = (u64)tw * th;
	if (pixels > (NFMT_MAX_OUTPUT - 0x28) / 4)
		return EFBIG;
	const uint image_size = 4 * pixels, total = image_size + 0x28;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < height; y++)
		for (uint x = 0; x < width; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + x / 8) * 64 + morton8 (x & 7, y & 7);
			const u8 *s = rgba + 4 * (y * width + x);
			u8 *d = out + 4 * pos;
			d[0] = s[3];
			d[1] = s[2];
			d[2] = s[1];
			d[3] = s[0]; // A,B,G,R
		}
	u8 *foot = out + image_size;
	memcpy (foot, bclim ? "CLIM" : "FLIM", 4);
	foot[4] = 0xff;
	foot[5] = 0xfe; // little endian BOM
	wr_le16 (foot + 6, 0x14);
	wr_le32 (foot + 8, 0x00020002); // BFLIM v2.2, accepted by CTR readers
	wr_le32 (foot + 0x0c, total);
	wr_le16 (foot + 0x10, 1);
	memcpy (foot + 0x14, "imag", 4);
	wr_le32 (foot + 0x18, 0x10);
	wr_le16 (foot + 0x1c, width);
	wr_le16 (foot + 0x1e, height);
	wr_le16 (foot + 0x20, 1);
	foot[0x22] = 9; // RGBA8
	foot[0x23] = 1; // 8x8 Morton tiles
	wr_le32 (foot + 0x24, image_size);
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			NUTEXB (Switch texture wrapper)		///////////////
//-----------------------------------------------------------------------------

// NUTEXB layout, all fields little endian (struct verified field-by-field
// against Switch-Toolbox's NUTEXB.cs -- KillzXGaming/Switch-Toolbox,
// File_Formats/Texture/NUTEXB.cs -- which is the closest thing to a spec
// this format has; there's no equivalent to 3dbrew for it). Everything
// lives in a fixed 0x70-byte trailer at the end of the file:
//   size-0x70   1 byte padding, then a 3-byte tag (unused by any reader,
//               including this one), then a NUL-terminated texture name
//               (the trailing bytes of this 64-byte name field are padding)
//   size-0x30   u32 padding, u32 width, u32 height, u32 depth,
//               u16 NUTEXImageFormat, u16 padding, u32 unk,
//               u32 mip_count, u32 alignment, u32 array_count,
//               u32 image_size
//   size-8      1 byte padding
//   size-7      3-byte magic "XET"
//   size-4      u32 version
// image_size bytes of (Tegra block-linear swizzled) pixel data sit at
// offset 0 of the file; a per-array-slice table of mip_count u32 mip sizes
// (each slice's table padded to 0x40 bytes) follows at offset image_size.
// This decoder only reads array slice 0, mip 0 -- the same single-texture
// scope as DecodeBNTX_RGBA and DecodeFLIM_RGBA.
//
// The pixel formats NUTEXB actually carries (RGBA8/BGRA8/BC1-BC7) are the
// same ones lib-bntx.c's Tegra deswizzle and block decoders already handle,
// just spelled with different numeric format codes; rather than
// reimplementing that swizzle/block math a second time, this builds a
// one-texture synthetic bntx_t using BNTX's own format-word encoding and
// hands it to DecodeBNTX_RGBA. NUTEXB has no field equivalent to BNTX's
// block_height_log2/tile_mode, so those are derived from height using the
// same GOB-count heuristic this codebase's own EncodeBNTX_RGBA already
// applies for its RGBA8 encodes; that heuristic is unverified here against
// a real compressed (BC1-BC7) NUTEXB sample with non-power-of-two height
// (see the NUTEXB README row).
enumError DecodeNUTEXB_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 0x70)
		return EINVAL;
	if (memcmp (src + src_size - 7, "XET", 3))
		return EINVAL;

	// Header fields block: 0x28 (40) bytes starting 0x30 (48) bytes before
	// EOF -- i.e. "reader.Seek(pos - 48)" in NUTEXB.cs's Read().
	const u8 *hdr = src + src_size - 0x30;
	const u32 w = rd_le32 (hdr + 0x04);
	const u32 h = rd_le32 (hdr + 0x08);
	const u32 nutfmt = rd_le16 (hdr + 0x10);
	const u32 mip_count = rd_le32 (hdr + 0x18);
	const u32 image_size = rd_le32 (hdr + 0x24);
	if (!w || !h || !mip_count || (u64)image_size > src_size)
		return EINVAL;

	uint bntx_fmt = 0, bntx_type = 1, blk_h = 1;
	switch (nutfmt)
	{
		case 0x0400:
			bntx_fmt = 0x0b;
			break; // R8G8B8A8_UNORM
		case 0x0405:
			bntx_fmt = 0x0b;
			break; // R8G8B8A8_SRGB
		case 0x0450:
			bntx_fmt = 0x0c;
			break; // B8G8R8A8_UNORM
		case 0x0455:
			bntx_fmt = 0x0c;
			break; // B8G8R8A8_SRGB
		case 0x0480:
			bntx_fmt = 0x1a;
			blk_h = 4;
			break; // BC1_UNORM
		case 0x0485:
			bntx_fmt = 0x1a;
			blk_h = 4;
			break; // BC1_SRGB
		case 0x0490:
			bntx_fmt = 0x1b;
			blk_h = 4;
			break; // BC2_UNORM
		case 0x0495:
			bntx_fmt = 0x1b;
			blk_h = 4;
			break; // BC2_SRGB
		case 0x04a0:
			bntx_fmt = 0x1c;
			blk_h = 4;
			break; // BC3_UNORM
		case 0x04a5:
			bntx_fmt = 0x1c;
			blk_h = 4;
			break; // BC3_SRGB
		case 0x0180:
			bntx_fmt = 0x1d;
			blk_h = 4;
			break; // BC4_UNORM
		case 0x0185:
			bntx_fmt = 0x1d;
			blk_h = 4;
			bntx_type = 2;
			break; // BC4_SNORM
		case 0x0280:
			bntx_fmt = 0x1e;
			blk_h = 4;
			break; // BC5_UNORM
		case 0x0285:
			bntx_fmt = 0x1e;
			blk_h = 4;
			bntx_type = 2;
			break; // BC5_SNORM
		case 0x04d7:
			bntx_fmt = 0x1f;
			blk_h = 4;
			break; // BC6_UFLOAT
		case 0x04d8:
			bntx_fmt = 0x1f;
			blk_h = 4;
			bntx_type = 2;
			break; // BC6_SFLOAT
		case 0x04e0:
			bntx_fmt = 0x20;
			blk_h = 4;
			break; // BC7_UNORM
		case 0x04e5:
			bntx_fmt = 0x20;
			blk_h = 4;
			break; // BC7_SRGB
		default:
			// Includes R32G32B32A32_FLOAT (0x0434), which lib-bntx.c's
			// decoder has no equivalent for -- reported honestly rather
			// than guessed at.
			return ERROR0 (ERR_INVALID_IFORM,
				"Unsupported NUTEXB texture format 0x%04x\n", nutfmt);
	}

	// Same block-height-log2 derivation as EncodeBNTX_RGBA, generalized from
	// raw pixel height to element (block) height so it also covers the
	// BC-compressed formats above.
	const uint elem_h = (h + blk_h - 1) / blk_h;
	uint bh_log2;
	if (elem_h <= 16)
		bh_log2 = 0;
	else if (elem_h <= 32)
		bh_log2 = 1;
	else if (elem_h <= 64)
		bh_log2 = 2;
	else if (elem_h <= 128)
		bh_log2 = 3;
	else
		bh_log2 = 4;

	bntx_texture_t tex;
	memset (&tex, 0, sizeof (tex));
	tex.name = "nutexb";
	tex.width = w;
	tex.height = h;
	tex.format = bntx_fmt << 8 | bntx_type;
	tex.comp_sel = 0; // identity (R,G,B,A)
	tex.tile_mode = 0; // block-linear
	tex.block_height_log2 = bh_log2;
	tex.n_mips = 1;
	tex.data = src;
	tex.data_size = image_size;

	bntx_t bntx;
	memset (&bntx, 0, sizeof (bntx));
	bntx.data = src;
	bntx.size = src_size;
	bntx.n_textures = 1;
	bntx.textures = &tex;

	return DecodeBNTX_RGBA (dest, width, height, &bntx, 0);
}

// CTPK (CTR Texture Package, 3DS container)
enumError ScanCTPK (nintendo_ctpk_t *ctpk, const u8 *data, uint size)
{
	if (!ctpk || !data || size < 0x20 || memcmp (data, "CTPK", 4))
		return EINVAL;
	memset (ctpk, 0, sizeof (*ctpk));
	ctpk->data = data;
	ctpk->size = size;
	ctpk->version = rd_le16 (data + 4);
	ctpk->n_entries = rd_le16 (data + 6);
	ctpk->texture_offset = rd_le32 (data + 8);
	ctpk->texture_size = rd_le32 (data + 12);
	if (ctpk->texture_offset > size || ctpk->texture_size > size - ctpk->texture_offset)
		return EINVAL;
	if (0x20 + 0x20 * (u64)ctpk->n_entries > size)
		return EINVAL;
	return ERR_OK;
}

enumError GetCTPKEntry (const nintendo_ctpk_t *ctpk, uint index, nintendo_ctpk_entry_t *entry)
{
	if (!ctpk || !ctpk->data || index >= ctpk->n_entries || !entry)
		return EINVAL;
	memset (entry, 0, sizeof (*entry));
	const u8 *info = ctpk->data + 0x20 + 0x20 * index;
	const u32 path_off = rd_le32 (info);
	const u32 data_size = rd_le32 (info + 4);
	const u32 data_off = rd_le32 (info + 8);
	const u32 fmt = rd_le32 (info + 12);
	const u16 w = rd_le16 (info + 16);
	const u16 h = rd_le16 (info + 18);
	const u8 mip = info[20];
	const u8 type = info[21];

	if (path_off > 0 && path_off < ctpk->size)
	{
		const u8 *str = ctpk->data + path_off;
		const u8 *nul = memchr (str, 0, ctpk->size - path_off);
		if (nul)
		{
			size_t len = nul - str;
			if (len >= sizeof (entry->name))
				len = sizeof (entry->name) - 1;
			memcpy (entry->name, str, len);
			entry->name[len] = 0;
		}
	}

	const u64 abs_data_off = (u64)ctpk->texture_offset + data_off;
	if (abs_data_off > ctpk->size || data_size > ctpk->size - abs_data_off)
		return EINVAL;

	entry->width = w;
	entry->height = h;
	entry->format = fmt;
	entry->mip_level = mip ? mip : 1;
	entry->type = type;
	entry->data = ctpk->data + abs_data_off;
	entry->data_size = data_size;
	return ERR_OK;
}

enumError DecodePicaTexture (
	u8 **dest, uint *width, uint *height, const u8 *src, uint w, uint h, uint format, uint src_size)
{
	if (!dest || !width || !height || !src)
		return EINVAL;
	const uint fmt = format;
	const uint data_size = src_size;

	if (!w || !h || w > 16384 || h > 16384)
		return EINVAL;
	if ((u64)w * h > NFMT_MAX_OUTPUT / 4)
		return EINVAL;

	if (fmt == 12 || fmt == 13)
	{
		u8 *rgba = MALLOC (w * h * 4);
		if (!rgba)
			return ERR_CANT_CREATE;
		enumError err = (fmt == 13) ? decode_etc1a4_tiled (rgba, src, w, h, data_size)
									: decode_etc1_tiled (rgba, src, w, h, data_size);
		if (err)
		{
			FREE (rgba);
			return err;
		}
		*dest = rgba;
		*width = w;
		*height = h;
		return ERR_OK;
	}

	const bool nibble_fmt = (fmt == 10 || fmt == 11);
	uint bpp = 0;
	switch (fmt)
	{
		case 0:
			bpp = 4;
			break;
		case 1:
			bpp = 3;
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			bpp = 2;
			break;
		case 7:
		case 8:
		case 9:
			bpp = 1;
			break;
		case 10:
		case 11:
			bpp = 0;
			break;
		default:
			return EINVAL;
	}

	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const u64 need = nibble_fmt ? ((u64)tw * th + 1) / 2 : (u64)tw * th * bpp;
	if (need > data_size)
		return EINVAL;

	u8 *rgba = MALLOC (w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + (x / 8)) * 64 + morton8 (x & 7, y & 7);
			u8 *d = rgba + 4 * (y * w + x);
			if (nibble_fmt)
			{
				const u8 byte = src[pos >> 1];
				const u8 nib = (pos & 1) ? (byte >> 4) : (byte & 0xF);
				const u8 v = (u8)(nib * 17);
				if (fmt == 10)
				{
					d[0] = d[1] = d[2] = v;
					d[3] = 255;
				}
				else
				{
					d[0] = d[1] = d[2] = 255;
					d[3] = v;
				}
				continue;
			}

			const u8 *p = src + pos * bpp;
			if (fmt == 0) // RGBA8888
			{
				d[0] = p[0];
				d[1] = p[1];
				d[2] = p[2];
				d[3] = p[3];
			}
			else if (fmt == 1) // RGB888
			{
				d[0] = p[0];
				d[1] = p[1];
				d[2] = p[2];
				d[3] = 255;
			}
			else if (fmt == 2) // RGBA5551
			{
				const u16 c = rd_le16 (p);
				d[0] = expand5 (c >> 11);
				d[1] = expand5 (c >> 6);
				d[2] = expand5 (c >> 1);
				d[3] = (c & 1) ? 255 : 0;
			}
			else if (fmt == 3) // RGB565
			{
				const u16 c = rd_le16 (p);
				d[0] = expand5 (c >> 11);
				d[1] = (u8)(((c >> 5) & 63) * 255 / 63);
				d[2] = expand5 (c);
				d[3] = 255;
			}
			else if (fmt == 4) // RGBA4444
			{
				const u16 c = rd_le16 (p);
				d[0] = (u8)(((c >> 12) & 15) * 17);
				d[1] = (u8)(((c >> 8) & 15) * 17);
				d[2] = (u8)(((c >> 4) & 15) * 17);
				d[3] = (u8)((c & 15) * 17);
			}
			else if (fmt == 5) // LA88
			{
				d[0] = d[1] = d[2] = p[0];
				d[3] = p[1];
			}
			else if (fmt == 6) // HILO8
			{
				d[0] = p[0];
				d[1] = p[1];
				d[2] = 0;
				d[3] = 255;
			}
			else if (fmt == 7) // L8
			{
				d[0] = d[1] = d[2] = p[0];
				d[3] = 255;
			}
			else if (fmt == 8) // A8
			{
				d[0] = d[1] = d[2] = 255;
				d[3] = p[0];
			}
			else if (fmt == 9) // LA44
			{
				d[0] = d[1] = d[2] = (u8)((p[0] & 0xF) * 17);
				d[3] = (u8)((p[0] >> 4) * 17);
			}
		}

	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError DecodeCTPKTexture_RGBA (
	u8 **dest, uint *width, uint *height, const nintendo_ctpk_entry_t *entry)
{
	if (!entry)
		return EINVAL;
	return DecodePicaTexture (dest, width, height, entry->data, entry->width, entry->height,
		entry->format, entry->data_size);
}

enumError EncodeCTPK (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name)
{
	if (!dest || !dest_size || !rgba || !width || !height || width > 16384 || height > 16384)
		return EINVAL;

	const uint tw = (width + 7) & ~7u;
	const uint th = (height + 7) & ~7u;
	const u64 pixels = (u64)tw * th;
	if (pixels > (NFMT_MAX_OUTPUT - 0x100) / 4)
		return EFBIG;

	const uint image_size = 4 * pixels;
	u8 *tex_data = CALLOC (1, image_size);
	if (!tex_data)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < height; y++)
	{
		for (uint x = 0; x < width; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + (x / 8)) * 64 + morton8 (x & 7, y & 7);
			const u8 *s = rgba + 4 * (y * width + x);
			u8 *d = tex_data + 4 * pos;
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
			d[3] = s[3]; // RGBA8888
		}
	}

	ccp base_name = name ? strrchr (name, '/') : 0;
	base_name = base_name ? base_name + 1 : (name ? name : "tex_0.png");
	const size_t name_len = strlen (base_name) + 1;
	const uint name_area_size = (name_len + 3) & ~3u;

	const uint header_size = 0x20;
	const uint entry_size = 0x20;
	const uint texture_offset = (header_size + entry_size + name_area_size + 0x7F) & ~0x7Fu;
	const uint total_size = texture_offset + image_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (tex_data);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "CTPK", 4);
	wr_le16 (out + 4, 1);
	wr_le16 (out + 6, 1);
	wr_le32 (out + 8, texture_offset);
	wr_le32 (out + 12, image_size);

	u8 *e = out + 0x20;
	wr_le32 (e + 0, 0x40);
	wr_le32 (e + 4, image_size);
	wr_le32 (e + 8, 0);
	wr_le32 (e + 12, 0);
	wr_le16 (e + 16, (u16)width);
	wr_le16 (e + 18, (u16)height);
	e[20] = 1;
	e[21] = 0;

	memcpy (out + 0x40, base_name, strlen (base_name) + 1);
	memcpy (out + texture_offset, tex_data, image_size);
	FREE (tex_data);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError CreateCTPK (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	uint names_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "tex";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		names_size += (uint)strlen (name) + 1;
	}
	const uint header_size = 0x20;
	const uint entries_size = 0x20 * n_entries;
	const uint string_table_size = (names_size + 3) & ~3u;
	const uint texture_offset = (header_size + entries_size + string_table_size + 0x7F) & ~0x7Fu;

	uint total_tex_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		total_tex_size += entries[i].size;
		total_tex_size = (total_tex_size + 0x7F) & ~0x7Fu;
	}

	const uint total_size = texture_offset + total_tex_size;
	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "CTPK", 4);
	wr_le16 (out + 4, 1);
	wr_le16 (out + 6, (u16)n_entries);
	wr_le32 (out + 8, texture_offset);
	wr_le32 (out + 12, total_tex_size);

	uint str_off = header_size + entries_size;
	uint data_off = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		u8 *e = out + header_size + i * 0x20;
		ccp name = entries[i].name ? entries[i].name : "tex";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		size_t nlen = strlen (name);

		wr_le32 (e + 0, str_off);
		wr_le32 (e + 4, entries[i].size);
		wr_le32 (e + 8, data_off);
		wr_le32 (e + 12, 0); // format = RGBA8
		wr_le16 (e + 16, 64);
		wr_le16 (e + 18, 64);
		e[20] = 1;
		e[21] = 0;

		memcpy (out + str_off, name, nlen + 1);
		str_off += (uint)nlen + 1;

		if (entries[i].size > 0 && entries[i].data)
			memcpy (out + texture_offset + data_off, entries[i].data, entries[i].size);

		data_off += entries[i].size;
		data_off = (data_off + 0x7F) & ~0x7Fu;
	}

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

static inline u16 sarc16 (const nintendo_sarc_t *s, const u8 *p)
{
	return s->big_endian ? rd_be16 (p) : rd_le16 (p);
}
static inline u32 sarc32 (const nintendo_sarc_t *s, const u8 *p)
{
	return s->big_endian ? rd_be32 (p) : rd_le32 (p);
}

enumError ScanSARC (nintendo_sarc_t *sarc, const u8 *data, uint size)
{
	if (!sarc || !data || size < 0x20 || memcmp (data, "SARC", 4))
		return EINVAL;
	memset (sarc, 0, sizeof (*sarc));
	sarc->data = data;
	sarc->size = size;
	// The BOM is stored in the file's byte order, independently of host CPU.
	if (data[6] == 0xfe && data[7] == 0xff)
		sarc->big_endian = true;
	else if (data[6] == 0xff && data[7] == 0xfe)
		sarc->big_endian = false;
	else
		return EINVAL;
	const uint header_size = sarc16 (sarc, data + 4);
	const uint file_size = sarc32 (sarc, data + 8);
	sarc->data_offset = sarc32 (sarc, data + 0x0c);
	if (header_size < 0x14 || header_size > size || file_size > size
		|| sarc->data_offset > file_size || header_size + 12 > file_size
		|| memcmp (data + header_size, "SFAT", 4))
		return EINVAL;
	const uint sfat_size = sarc16 (sarc, data + header_size + 4);
	sarc->n_entries = sarc16 (sarc, data + header_size + 6);
	if (sfat_size < 12 || sarc->n_entries > (file_size - header_size - 12) / 16
		|| header_size + sfat_size + 16 * sarc->n_entries + 8 > file_size)
		return EINVAL;
	sarc->entries_offset = header_size + sfat_size;
	sarc->sfnt_offset = sarc->entries_offset + 16 * sarc->n_entries;
	if (memcmp (data + sarc->sfnt_offset, "SFNT", 4)
		|| sarc16 (sarc, data + sarc->sfnt_offset + 4) < 8)
		return EINVAL;
	return ERR_OK;
}

enumError GetSARCEntry (
	const nintendo_sarc_t *sarc, uint index, ccp *name, const u8 **data, uint *size)
{
	if (!sarc || !sarc->data || index >= sarc->n_entries)
		return EINVAL;
	const u8 *node = sarc->data + sarc->entries_offset + 16 * index;
	const u32 attr = sarc32 (sarc, node + 4);
	const uint begin = sarc32 (sarc, node + 8), end = sarc32 (sarc, node + 12);
	if (begin > end || end > sarc->size - sarc->data_offset)
		return EINVAL;
	if (name)
	{
		if (!(attr >> 24))
		{
			*name = 0;
		}
		else
		{
			const uint noff = sarc->sfnt_offset + 8 + 4 * (attr & 0x00ffffff);
			if (noff >= sarc->size || !memchr (sarc->data + noff, 0, sarc->size - noff))
				return EINVAL;
			*name = (ccp)sarc->data + noff;
		}
	}
	if (data)
		*data = sarc->data + sarc->data_offset + begin;
	if (size)
		*size = end - begin;
	return ERR_OK;
}

typedef struct sarc_sort_t
{
	const nintendo_sarc_entry_t *entry;
	u32 hash;
} sarc_sort_t;

static u32 hash_sarc_name (ccp name)
{
	u32 hash = 0;
	while (*name)
		hash = hash * 0x65 + (u8)*name++;
	return hash;
}

static int cmp_sarc_entry (const void *a, const void *b)
{
	const sarc_sort_t *sa = a, *sb = b;
	if (sa->hash != sb->hash)
		return sa->hash < sb->hash ? -1 : 1;
	return strcmp (sa->entry->name, sb->entry->name);
}

enumError CreateSARC (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
	uint n_entries, bool big_endian)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xffff)
		return EINVAL;
	sarc_sort_t *sorted = CALLOC (n_entries, sizeof (*sorted));
	if (!sorted)
		return ERR_CANT_CREATE;
	uint names_size = 8, data_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (!entries[i].name || !*entries[i].name || !entries[i].data)
		{
			FREE (sorted);
			return EINVAL;
		}
		const size_t name_len = strlen (entries[i].name) + 1;
		if (name_len > UINT_MAX || names_size > UINT_MAX - ((name_len + 3) & ~3u)
			|| data_size > UINT_MAX - entries[i].size)
		{
			FREE (sorted);
			return EFBIG;
		}
		sorted[i].entry = entries + i;
		sorted[i].hash = hash_sarc_name (entries[i].name);
		names_size += (name_len + 3) & ~3u;
		data_size += entries[i].size;
	}
	qsort (sorted, n_entries, sizeof (*sorted), cmp_sarc_entry);
	if (names_size > UINT_MAX - (0x20 + 16 * n_entries))
	{
		FREE (sorted);
		return EFBIG;
	}
	const uint tables_size = 0x20 + 16 * n_entries + names_size;
	const uint data_offset = (tables_size + 0xff) & ~0xffu;
	if (tables_size > UINT_MAX - 0xff || data_offset > UINT_MAX - data_size)
	{
		FREE (sorted);
		return EFBIG;
	}
	const uint total = data_offset + data_size;
	u8 *out = CALLOC (1, total);
	if (!out)
	{
		FREE (sorted);
		return ERR_CANT_CREATE;
	}
	void (*w16) (u8 *, u16) = big_endian ? wr_be16 : wr_le16;
	void (*w32) (u8 *, u32) = big_endian ? wr_be32 : wr_le32;
	memcpy (out, "SARC", 4);
	w16 (out + 4, 0x14);
	// Store the same BOM value in the file's byte order: FE FF means big,
	// FF FE means little in the raw byte stream.
	w16 (out + 6, 0xfeff);
	w32 (out + 8, total);
	w32 (out + 0x0c, data_offset);
	w16 (out + 0x10, 0x0100);
	memcpy (out + 0x14, "SFAT", 4);
	w16 (out + 0x18, 12);
	w16 (out + 0x1a, n_entries);
	w32 (out + 0x1c, 0x65);
	const uint sfnt = 0x20 + 16 * n_entries;
	memcpy (out + sfnt, "SFNT", 4);
	w16 (out + sfnt + 4, 8);
	uint name_pos = sfnt + 8, data_pos = data_offset;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *entry = sorted[i].entry;
		u8 *node = out + 0x20 + 16 * i;
		w32 (node, sorted[i].hash);
		w32 (node + 4, 0x01000000 | ((name_pos - (sfnt + 8)) / 4));
		w32 (node + 8, data_pos - data_offset);
		memcpy (out + name_pos, entry->name, strlen (entry->name) + 1);
		name_pos += (strlen (entry->name) + 1 + 3) & ~3u;
		memcpy (out + data_pos, entry->data, entry->size);
		data_pos += entry->size;
		w32 (node + 12, data_pos - data_offset);
	}
	FREE (sorted);
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		GFA (Good-Feel archive) support		///////////////
///////////////////////////////////////////////////////////////////////////////

// Raw LZ10: identical token format to the standard Nintendo LZ10 stream, but
// without the 4-byte (0x10 + 24-bit size) header, so the caller supplies the
// output size. This is what GFCP compression types 2 and 3 use.
enumError DecodeLZ10Raw (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest || !src)
		return EINVAL;
	uint sp = 0, dp = 0;
	while (dp < dest_size)
	{
		if (sp >= src_size)
			return EINVAL;
		u8 flags = src[sp++];
		for (uint bit = 0; bit < 8 && dp < dest_size; bit++, flags <<= 1)
		{
			if (!(flags & 0x80))
			{
				if (sp >= src_size)
					return EINVAL;
				dest[dp++] = src[sp++];
			}
			else
			{
				if (sp + 2 > src_size)
					return EINVAL;
				const u8 a = src[sp++], b = src[sp++];
				const uint len = (a >> 4) + 3, back = ((a & 15) << 8 | b) + 1;
				if (back > dp || len > dest_size - dp)
					return EINVAL;
				for (uint i = 0; i < len; i++, dp++)
					dest[dp] = dest[dp - back];
			}
		}
	}
	return ERR_OK;
}

// Byte Pair Encoding, GFCP compression type 1. Each block starts with a pair
// table: a control byte >= 0x80 means (byte-0x7F) literals follow, otherwise
// it introduces (byte+1) expansions for one key. Expanded bytes are pushed
// through a stack so nested pairs resolve recursively.
// Ported from QuickBMS's "BPE" comtype (compression/bpe.c, itself credited
// to Philip Gage's classic compress.c, C Users Journal Feb 1994) -- this is
// what aluigi's kirby_epic_yarn.bms uses for GFCP zip-mode 1. The encoder
// side (filewrite() in that source) is the only place this table encoding
// is actually documented, so the decoder here is derived by inverting it.
//
// The pair table for c=0..255 is written as a run-length stream where each
// marker byte is EITHER:
//   >127  a run of (marker-127) literal positions (table[c]==c, no bytes
//         follow for them) -- but the run always stops one short of a real
//         pair, and that ONE pair entry is written immediately after with
//         no marker of its own (the encoder's `len=0; ...; c==256?break:`
//         reset before falling into the shared write loop, which then
//         executes exactly once).
//   <=127 a run of (marker+1) consecutive table entries, each written as
//         1 byte (still literal, table[c]==c by coincidence) or 2 bytes
//         (a real pair, left+right).
// Getting the ">127 run implies exactly one trailing pair entry, not a
// fresh marker" part wrong is what silently desynced the whole stream on
// every previously-untested real sample (verified against retail Kirby's
// Epic Yarn GFA data, which round-trips byte-exact with this version but
// not the naive "each marker is independent" reading).
enumError DecodeBPE (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest || !src)
		return EINVAL;
	uint sp = 0, dp = 0;

	while (dp < dest_size)
	{
		u8 table[256][2];
		for (uint i = 0; i < 256; i++)
		{
			table[i][0] = (u8)i;
			table[i][1] = 0;
		}
		// A byte is "paired" when table[i][1] is used; track that separately
		// so a legitimate 0 expansion byte is not mistaken for "unpaired".
		bool paired[256];
		memset (paired, 0, sizeof (paired));

		// pair table
		uint c = 0;
		while (c < 256)
		{
			if (sp >= src_size)
				return EINVAL;
			uint marker = src[sp++];

			uint entries;
			if (marker > 127)
			{
				c += marker - 127; // these stay literal, no bytes for them
				if (c == 256)
					break;
				entries = 1; // the pair that terminated the literal run
			}
			else
				entries = marker + 1;

			for (uint i = 0; i < entries && c < 256; i++, c++)
			{
				if (sp >= src_size)
					return EINVAL;
				const u8 lc = src[sp++];
				table[c][0] = lc;
				if (lc != (u8)c)
				{
					if (sp >= src_size)
						return EINVAL;
					table[c][1] = src[sp++];
					paired[c] = true;
				}
			}
		}

		if (sp + 2 > src_size)
			return EINVAL;
		uint block_len = (uint)src[sp] << 8 | src[sp + 1];
		sp += 2;

		// Pair codes are assigned strictly downward from 255 and a code can
		// only reference lower codes, so a chain can nest at most 256 deep;
		// 128 was too small for real data -- found on a retail sample
		// (z100_tutorial01.gfa) that needs depth 139, one of 13 real Kirby's
		// Epic Yarn archives that silently failed to decode until this was
		// sized to the actual worst case instead of a guessed round number.
		u8 stack[256];
		uint sn = 0;
		while (block_len || sn)
		{
			u8 b;
			if (sn)
				b = stack[--sn];
			else
			{
				if (sp >= src_size)
					return EINVAL;
				b = src[sp++];
				block_len--;
			}

			if (paired[b])
			{
				if (sn + 2 > sizeof (stack))
					return EINVAL;
				stack[sn++] = table[b][1];
				stack[sn++] = table[b][0];
			}
			else
			{
				if (dp >= dest_size)
					return EINVAL;
				dest[dp++] = b;
			}
		}
	}
	return ERR_OK;
}

void ResetGFA (gfa_t *gfa)
{
	if (!gfa)
		return;
	FREE (gfa->blob);
	FREE (gfa->entries);
	FREE (gfa->names);
	memset (gfa, 0, sizeof (*gfa));
}

enumError ScanGFA (gfa_t *gfa, const u8 *data, uint size)
{
	if (!gfa || !data || size < 0x1c || memcmp (data, "GFAC", 4))
		return EINVAL;
	memset (gfa, 0, sizeof (*gfa));

	const u32 info_off = rd_le32 (data + 0x0c);
	const u32 data_off = rd_le32 (data + 0x14);
	const u32 data_size = rd_le32 (data + 0x18);

	if (info_off + 4 > size || data_off + 16 > size)
		return EINVAL;
	if ((u64)data_off + data_size > size)
		return EINVAL;

	const u32 n = rd_le32 (data + info_off);
	if (!n || n > 0x100000 || (u64)info_off + 4 + (u64)n * 16 > size)
		return EINVAL;

	// GFCP payload
	const u8 *gfcp = data + data_off;
	if (memcmp (gfcp, "GFCP", 4))
		return EINVAL;
	const u32 zip = rd_le32 (gfcp + 8);
	const u32 out_len = rd_le32 (gfcp + 12);
	const u32 zsize = rd_le32 (gfcp + 16);
	if (!out_len || out_len > NFMT_MAX_OUTPUT)
		return EFBIG;
	if ((u64)20 + zsize > data_size)
		return EINVAL;

	u8 *blob = MALLOC (out_len);
	if (!blob)
		return ERR_CANT_CREATE;
	enumError err;
	switch (zip)
	{
		case 1:
			err = DecodeBPE (blob, out_len, gfcp + 20, zsize);
			break;
		case 2:
		case 3:
			err = DecodeLZ10Raw (blob, out_len, gfcp + 20, zsize);
			break;
		default:
			err = EINVAL;
			break;
	}
	if (err)
	{
		FREE (blob);
		return err;
	}

	// entry table
	gfa_entry_t *entries = CALLOC (n, sizeof (*entries));
	char *names = CALLOC (1, size); // names live inside the source file
	if (!entries || !names)
	{
		FREE (blob);
		FREE (entries);
		FREE (names);
		return ERR_CANT_CREATE;
	}
	uint name_pos = 0;

	const u8 *rec = data + info_off + 4;
	for (uint i = 0; i < n; i++, rec += 16)
	{
		u32 name_off = rd_le32 (rec + 4) & 0x00ffffff;
		const u32 fsize = rd_le32 (rec + 8);
		u32 offset = rd_le32 (rec + 12);

		if (name_off >= size)
		{
			name_off = 0;
		}
		// copy the NUL-terminated name out of the source buffer
		entries[i].name = names + name_pos;
		if (name_off)
		{
			uint j = name_off;
			while (j < size && data[j] && name_pos + 1 < size)
				names[name_pos++] = (char)data[j++];
		}
		names[name_pos++] = 0;

		entries[i].size = fsize;
		entries[i].offset = offset >= data_off ? offset - data_off : offset;
		if (fsize && (entries[i].offset > out_len || fsize > out_len - entries[i].offset))
		{
			// Out-of-range member: clamp to empty rather than reading past the blob.
			entries[i].size = 0;
			entries[i].offset = 0;
		}
	}

	gfa->blob = blob;
	gfa->blob_size = out_len;
	gfa->entries = entries;
	gfa->n_entries = n;
	gfa->names = names;
	gfa->compression = zip;
	return ERR_OK;
}

enumError CreateGFA (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x100000)
		return EINVAL;

	uint payload_size = 0;
	uint names_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (!entries[i].name)
			return EINVAL;
		names_size += strlen (entries[i].name) + 1;
		payload_size += entries[i].size;
	}

	u8 *payload = CALLOC (1, payload_size ? payload_size : 1);
	if (!payload)
		return ERR_CANT_CREATE;

	uint current_offset = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (entries[i].size)
		{
			memcpy (payload + current_offset, entries[i].data, entries[i].size);
			current_offset += entries[i].size;
		}
	}

	u8 *zdata = 0;
	uint zsize = 0;
	enumError err = EncodeLZ10Raw (&zdata, &zsize, payload, payload_size);
	FREE (payload);
	if (err)
		return err;

	const uint info_off = 0x20;
	const uint names_off = info_off + 4 + 16 * n_entries;
	uint data_off = names_off + names_size;
	data_off = (data_off + 3) & ~3u;

	const uint data_size = 20 + zsize;
	const uint total_size = data_off + data_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (zdata);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "GFAC", 4);
	wr_le32 (out + 0x0c, info_off);
	wr_le32 (out + 0x14, data_off);
	wr_le32 (out + 0x18, data_size);

	wr_le32 (out + info_off, n_entries);

	uint name_pos = 0;
	current_offset = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *rec = out + info_off + 4 + 16 * i;
		wr_le32 (rec + 4, names_off + name_pos);
		wr_le32 (rec + 8, entries[i].size);
		wr_le32 (rec + 12, data_off + current_offset);

		size_t nlen = strlen (entries[i].name) + 1;
		memcpy (out + names_off + name_pos, entries[i].name, nlen);
		name_pos += nlen;
		current_offset += entries[i].size;
	}

	u8 *gfcp = out + data_off;
	memcpy (gfcp, "GFCP", 4);
	wr_le32 (gfcp + 8, 3);
	wr_le32 (gfcp + 12, payload_size);
	wr_le32 (gfcp + 16, zsize);
	memcpy (gfcp + 20, zdata, zsize);
	FREE (zdata);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		PAC (Brawl "ARC\0" archive) support	///////////////
//-----------------------------------------------------------------------------

void ResetPAC (pac_t *pac)
{
	if (!pac)
		return;
	FREE (pac->entries);
	memset (pac, 0, sizeof (*pac));
}

// ARCHeader (BrawlLib SSBB/Types/ARC.cs): tag(4)="ARC\0", _version(ushort,
// native -- both bytes 0x01 so the file's byte order never actually matters
// here), _numFiles(bushort, big-endian) at 0x06, two reserved u32 at
// 0x08/0x0c, then a 48-byte fixed name buffer at 0x10. Struct size 0x40; the
// first ARCFileHeader follows immediately.
//
// ARCFileHeader is 0x20 bytes: bshort type(0), bshort index(2), bint
// size(4), byte groupIndex(8), byte padding(9), bshort redirectIndex(10),
// then 20 bytes of reserved bint padding out to 0x20. Its data starts right
// after (offset+0x20) and the *next* header is
// round_up(data_offset + size, 0x20) -- BrawlLib computes this by aligning
// the raw data-end pointer to the header struct's own size, which happens
// to also be 32, i.e. plain 32-byte alignment from the start of the file
// (0x40 is itself 32-aligned, so relative and absolute alignment coincide).
//
// Verified field-by-field against a real retail file (SSSG2 Ultimate's
// FitIke.pac, 552416 bytes): tag="ARC\0", numFiles=2, name="FitPeach" (the
// dogfooded template name BrawlLib's tools left behind -- harmless, it's
// unused metadata), first entry type=1 (MiscData) size=0x00021ff1=139249,
// whose data (at header+0x20=0x60) begins with what looks like a
// SakuraiArchive block header, consistent with Brawl's per-character
// "moveset" data always being MiscData entry 0.
enumError ScanPAC (pac_t *pac, const u8 *data, uint size)
{
	if (!pac || !data || size < 0x40 || memcmp (data, "ARC\0", 4))
		return EINVAL;
	if (data[4] != 1 || data[5] != 1)
		return EINVAL; // version must be 0x0101

	const uint n = rd_be16 (data + 6);
	if (!n || n > 0x10000)
		return EINVAL;

	memset (pac, 0, sizeof (*pac));
	pac->data = data;
	pac->size = size;
	memcpy (pac->name, data + 0x10, sizeof (pac->name) - 1);
	pac->name[sizeof (pac->name) - 1] = 0;

	pac_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	uint off = 0x40, i;
	for (i = 0; i < n; i++)
	{
		if (off + 0x20 > size)
			break;
		const u8 *h = data + off;
		const u16 type = rd_be16 (h);
		const u16 index = rd_be16 (h + 2);
		const u32 fsize = rd_be32 (h + 4);
		const u8 group = h[8];
		const s16 redirect = (s16)rd_be16 (h + 10);

		const u32 data_off = off + 0x20;
		if ((u64)data_off + fsize > size)
			break;

		entries[i].type = type;
		entries[i].index = index;
		entries[i].group_index = group;
		entries[i].redirect_index = redirect;
		// The retail structure reserves h+0x10..0x1f, but CreatePAC has always
		// used it as a conservative 15-byte basename extension. Recover only a
		// terminated, path-safe value; arbitrary retail padding remains ignored.
		const u8 *np = h + 0x10;
		uint nl = 0;
		while (nl < sizeof (entries[i].name) && np[nl]
			&& (isalnum (np[nl]) || np[nl] == '_' || np[nl] == '-' || np[nl] == '.'))
			nl++;
		if (nl && nl < sizeof (entries[i].name) && !np[nl])
		{
			memcpy (entries[i].name, np, nl);
			entries[i].name[nl] = 0;
		}
		entries[i].size = fsize;
		entries[i].data = data + data_off;

		const u64 next = ((u64)data_off + fsize + 0x1f) & ~(u64)0x1f;
		if (next <= off || next > size)
		{
			i++;
			break;
		}
		off = (uint)next;
	}

	if (!i)
	{
		FREE (entries);
		return EINVAL;
	}
	pac->entries = entries;
	pac->n_entries = i;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	Gorilla Games ".pkg" (Bonsai Barber)		///////////////
//-----------------------------------------------------------------------------

void ResetGPKG (gpkg_t *pkg)
{
	if (!pkg)
		return;
	FREE (pkg->entries);
	FREE (pkg->data);
	memset (pkg, 0, sizeof (*pkg));
}

// Clean-room LZO1X decoder. This follows the byte layout described in the
// Linux kernel's Documentation/staging/lzo.rst, rather than using (or
// translating) liblzo. In particular, it accepts only the original stream
// version used by Retro's PAKs; the newer LZO-RLE zero-run extension is not
// part of that format and is rejected here.
enumError DecodeLZO1XGrow (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size)
		return ERR_INVALID_DATA;

	uint cap = src_size <= ((256u << 20) - 256) / 4 ? src_size * 4 + 256 : 256u << 20;
	u8 *out = MALLOC (cap);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	uint ip = 0, op = 0, state = 0;

	// Grow before every copy. The hard ceiling makes malformed streams unable
	// to turn a tiny CMPD segment into an unbounded allocation.
#define LZO_NEED_OUT(n)                                                                            \
	do                                                                                             \
	{                                                                                              \
		const uint lzo_need_ = (n);                                                                \
		if (lzo_need_ > (256u << 20) - op)                                                         \
			goto bad;                                                                              \
		const uint lzo_want_ = op + lzo_need_;                                                     \
		if (lzo_want_ > cap)                                                                       \
		{                                                                                          \
			uint lzo_cap_ = cap;                                                                   \
			while (lzo_cap_ < lzo_want_)                                                           \
			{                                                                                      \
				if (lzo_cap_ >= (256u << 20) / 2)                                                  \
				{                                                                                  \
					lzo_cap_ = 256u << 20;                                                         \
					break;                                                                         \
				}                                                                                  \
				lzo_cap_ *= 2;                                                                     \
			}                                                                                      \
			u8 *lzo_out_ = REALLOC (out, lzo_cap_);                                                \
			if (!lzo_out_)                                                                         \
				goto oom;                                                                          \
			out = lzo_out_;                                                                        \
			cap = lzo_cap_;                                                                        \
		}                                                                                          \
	} while (0)
#define LZO_COPY_LITERALS(n)                                                                       \
	do                                                                                             \
	{                                                                                              \
		const uint lzo_n_ = (n);                                                                   \
		if (lzo_n_ > src_size - ip)                                                                \
			goto bad;                                                                              \
		LZO_NEED_OUT (lzo_n_);                                                                     \
		memcpy (out + op, src + ip, lzo_n_);                                                       \
		ip += lzo_n_;                                                                              \
		op += lzo_n_;                                                                              \
	} while (0)

	// Returns an LZO variable length whose low bits were supplied by token.
	// Zero is the escape value: 15/7/31 respectively plus zero-byte steps.
#define LZO_LENGTH(bits, base, value, result)                                                      \
	do                                                                                             \
	{                                                                                              \
		uint lzo_v_ = (value);                                                                     \
		const uint lzo_mask_ = (1u << (bits)) - 1;                                                 \
		if (!lzo_v_)                                                                               \
		{                                                                                          \
			lzo_v_ = lzo_mask_;                                                                    \
			while (ip < src_size && src[ip] == 0)                                                  \
			{                                                                                      \
				if (lzo_v_ > UINT_MAX - 255)                                                       \
					goto bad;                                                                      \
				lzo_v_ += 255;                                                                     \
				ip++;                                                                              \
			}                                                                                      \
			if (ip >= src_size)                                                                    \
				goto bad;                                                                          \
			lzo_v_ += src[ip++];                                                                   \
		}                                                                                          \
		if (lzo_v_ > UINT_MAX - (base))                                                            \
			goto bad;                                                                              \
		(result) = lzo_v_ + (base);                                                                \
	} while (0)

	uint token = src[ip++];
	bool have_token = true;
	if (token > 17)
	{
		const uint n = token - 17;
		LZO_COPY_LITERALS (n);
		state = n < 4 ? n : 4;
		have_token = false;
	}

	for (;;)
	{
		if (!have_token)
		{
			if (ip >= src_size)
				goto bad;
			token = src[ip++];
		}
		have_token = false;
		uint len, dist;

		if (token < 16)
		{
			if (!state)
			{
				LZO_LENGTH (4, 3, token, len);
				LZO_COPY_LITERALS (len);
				state = 4;
				continue;
			}
			if (ip >= src_size)
				goto bad;
			len = state < 4 ? 2 : 3;
			dist = ((uint)src[ip++] << 2) + (token >> 2) + (state < 4 ? 1 : 2049);
			state = token & 3;
		}
		else if (token < 32)
		{
			LZO_LENGTH (3, 2, token & 7, len);
			if (src_size - ip < 2)
				goto bad;
			const uint d = src[ip] | (uint)src[ip + 1] << 8;
			ip += 2;
			dist = 16384 + ((token & 8) << 11) + d;
			state = d & 3;
			if (dist == 16384)
			{
				if (ip != src_size)
					goto bad; // a segment is exactly one LZO stream
				*dest = out;
				*dest_size = op;
				return ERR_OK;
			}
		}
		else if (token < 64)
		{
			LZO_LENGTH (5, 2, token & 31, len);
			if (src_size - ip < 2)
				goto bad;
			const uint d = src[ip] | (uint)src[ip + 1] << 8;
			ip += 2;
			dist = d + 1;
			state = d & 3;
		}
		else if (token < 128)
		{
			if (ip >= src_size)
				goto bad;
			len = 3 + ((token >> 5) & 1);
			dist = ((uint)src[ip++] << 3) + ((token >> 2) & 7) + 1;
			state = token & 3;
		}
		else
		{
			if (ip >= src_size)
				goto bad;
			len = 5 + ((token >> 5) & 3);
			dist = ((uint)src[ip++] << 3) + ((token >> 2) & 7) + 1;
			state = token & 3;
		}

		if (!dist || dist > op)
			goto bad;
		LZO_NEED_OUT (len);
		for (uint i = 0; i < len; i++)
			out[op + i] = out[op - dist + i];
		op += len;
		LZO_COPY_LITERALS (state);
	}

oom:
	FREE (out);
	return ERR_OUT_OF_MEMORY;
bad:
	FREE (out);
	return ERR_INVALID_DATA;
#undef LZO_LENGTH
#undef LZO_COPY_LITERALS
#undef LZO_NEED_OUT
}

enumError ScanGPKG (gpkg_t *pkg, const u8 *data, uint size)
{
	if (!pkg || !data || size < 2 || data[0] != 0x78)
		return ERR_NOTHING_TO_DO; // not zlib at all -- cheap reject before inflating

	memset (pkg, 0, sizeof (*pkg));
	u8 *dec = 0;
	uint dec_size = 0;
	if (DecodeZlibGrow (&dec, &dec_size, data, size) != ERR_OK || dec_size < 0x14)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	const u32 data_off = rd_be32 (dec + 4);
	const u32 zero = rd_be32 (dec + 8);
	const u32 n = rd_be32 (dec + 16);
	if (zero || !n || n > 0x100000 || data_off > dec_size)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	const u64 table_end = (u64)0x14 + (u64)n * 0x28;
	if (table_end > dec_size)
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	const u32 base_off = dec_size - data_off;

	gpkg_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
	{
		FREE (dec);
		return ERR_CANT_CREATE;
	}

	uint valid = 0;
	uint off = 0x14;
	for (uint i = 0; i < n; i++, off += 0x28)
	{
		const u8 *h = dec + off;
		memcpy (entries[i].name, h, 0x20);
		entries[i].name[0x20] = 0;

		const u32 entry_off = rd_be32 (h + 0x20);
		const u32 entry_size = rd_be32 (h + 0x24);
		const u64 real_off = (u64)entry_off + base_off;
		if (real_off + entry_size > dec_size)
			continue; // out-of-range entry: skip it, don't abort the whole archive

		entries[i].data = dec + real_off;
		entries[i].size = entry_size;
		valid++;
	}

	// Every real sample has every entry valid; if none are, this probably
	// isn't really our format (some unrelated zlib stream that happened to
	// pass the header-field sanity checks above by coincidence).
	if (!valid)
	{
		FREE (entries);
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}

	pkg->data = dec;
	pkg->size = dec_size;
	pkg->entries = entries;
	pkg->n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	2D Boy "master.pak" (World of Goo)		///////////////
//-----------------------------------------------------------------------------

void ResetGPAK (gpak_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	memset (pak, 0, sizeof (*pak));
}

enumError ScanGPAK (gpak_t *pak, const u8 *data, uint size)
{
	if (!pak || !data || size < 16)
		return ERR_NOTHING_TO_DO;

	memset (pak, 0, sizeof (*pak));

	const u32 n = rd_be32 (data);
	const u32 zero = rd_be32 (data + 8);
	if (zero || !n || n > 0x1000000)
		return ERR_NOTHING_TO_DO;

	const u64 table_size = (u64)(n + 1) * 16;
	if (table_size > size)
		return ERR_NOTHING_TO_DO;

	gpak_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = data + (i + 1) * 16;
		const u32 off = rd_be32 (h);
		const u32 esz = rd_be32 (h + 4);
		if ((u64)off + esz > size)
			continue; // out-of-range entry: reject the whole file below instead
		entries[i].data = data + off;
		entries[i].size = esz;
	}

	// Unlike ScanGPKG(), a single bad entry here is treated as "this isn't
	// really our format" rather than "skip that one entry" -- there's no
	// magic and no per-entry name to sanity-check against, so full
	// byte-accounting across every entry is the only signal this format has
	// at all; a real master.pak has zero out-of-range entries (verified on
	// a real 1731-entry sample), so any miss here means the structural
	// guess above was wrong for this file, not that one entry is malformed.
	for (uint i = 0; i < n; i++)
		if (!entries[i].data)
		{
			FREE (entries);
			return ERR_NOTHING_TO_DO;
		}

	pak->data = data;
	pak->size = size;
	pak->entries = entries;
	pak->n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	Koei Tecmo "LINKDATA*.BNS" (Samurai Warriors 3)	///////////////
//-----------------------------------------------------------------------------

void ResetBNS (bns_t *bns)
{
	if (!bns)
		return;
	FREE (bns->entries);
	memset (bns, 0, sizeof (*bns));
}

enumError ScanBNS (bns_t *bns, const u8 *data, uint size)
{
	if (!bns || !data || size < 16)
		return ERR_NOTHING_TO_DO;

	memset (bns, 0, sizeof (*bns));

	const u32 n = rd_be32 (data + 4);
	const u32 blk = rd_be32 (data + 8);
	const u32 zero = rd_be32 (data + 0xc);
	if (zero || !n || n > 0x1000000 || !blk || blk & (blk - 1))
		return ERR_NOTHING_TO_DO; // blk must be a nonzero power of two

	const u64 table_size = 16 + (u64)n * 8;
	if (table_size > size)
		return ERR_NOTHING_TO_DO;

	bns_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = data + 16 + i * 8;
		const u64 off = (u64)rd_be32 (h) * blk;
		const u32 esz = rd_be32 (h + 4);
		if (off + esz > size)
			continue; // out-of-range entry: reject the whole file below instead
		entries[i].data = data + off;
		entries[i].size = esz;
	}

	// Same all-or-nothing byte-accounting rule as ScanGPAK(): no magic and
	// no per-entry name to sanity-check against, so every real sample (5
	// retail LINKDATA*.BNS files, entry counts 1/18/41/45/5276) must resolve
	// with zero out-of-range entries or the structural guess is wrong.
	for (uint i = 0; i < n; i++)
		if (!entries[i].data)
		{
			FREE (entries);
			return ERR_NOTHING_TO_DO;
		}

	bns->data = data;
	bns->size = size;
	bns->entries = entries;
	bns->n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	Retro Studios ".pak" (DK Country Returns, Wii)	///////////////
//-----------------------------------------------------------------------------

void ResetRPAK (rpak_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	memset (pak, 0, sizeof (*pak));
}

enumError ScanRPAK (rpak_t *pak, const u8 *data, uint size)
{
	if (!pak || !data || size < 0x84)
		return ERR_NOTHING_TO_DO;

	memset (pak, 0, sizeof (*pak));

	const u32 strg_length = rd_be32 (data + 0x48);
	const u32 rshd_length = rd_be32 (data + 0x50);
	if (!strg_length && !rshd_length)
		return ERR_NOTHING_TO_DO;

	const u64 rshd_hdr_off = (u64)0x80 + strg_length;
	if (rshd_hdr_off + 4 > size)
		return ERR_NOTHING_TO_DO;
	const u32 n = rd_be32 (data + rshd_hdr_off);
	if (!n || n > 0x1000000)
		return ERR_NOTHING_TO_DO;

	const u64 table_off = rshd_hdr_off + 4;
	const u64 table_size = (u64)n * 24;
	if (table_off + table_size > size)
		return ERR_NOTHING_TO_DO;

	const u64 data_base = (u64)0x80 + strg_length + rshd_length;

	rpak_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = data + table_off + i * 24;
		const u32 compressed = rd_be32 (h);
		const u32 magic = rd_be32 (h + 4);
		const u32 id_hi = rd_be32 (h + 8);
		const u32 id_lo = rd_be32 (h + 0xc);
		const u32 dlen = rd_be32 (h + 0x10);
		const u32 ptr = rd_be32 (h + 0x14);

		const u64 off = data_base + ptr;
		if (off + dlen > size)
			continue; // out-of-range entry: reject the whole file below instead

		entries[i].data = data + off;
		entries[i].size = dlen;
		entries[i].magic = magic;
		entries[i].id_hi = id_hi;
		entries[i].id_lo = id_lo;
		entries[i].compressed = compressed != 0;
	}

	// Same all-or-nothing byte-accounting rule as ScanGPAK()/ScanBNS(): no
	// magic to check, so every real sample (verified on a 112-entry retail
	// MiscData.pak, zero out-of-range entries) must resolve fully or the
	// structural guess is wrong for this file.
	for (uint i = 0; i < n; i++)
		if (!entries[i].data)
		{
			FREE (entries);
			return ERR_NOTHING_TO_DO;
		}

	pak->data = data;
	pak->size = size;
	pak->entries = entries;
	pak->n_entries = n;
	return ERR_OK;
}

u8 *DecompressRPAKEntry (const u8 *data, uint size, uint *res_size)
{
	if (!data || size < 8 || memcmp (data, "CMPD", 4))
		return 0;

	const u32 blocks = rd_be32 (data + 4);
	if (blocks > 0x100000 || (u64)8 + (u64)blocks * 8 > size)
		return 0;

	// First pass: validate every block header in-bounds and total the
	// uncompressed size, same all-or-nothing rule as the archive scanners.
	u64 pos = 8 + (u64)blocks * 8;
	u64 total = 0;
	for (uint i = 0; i < blocks; i++)
	{
		const u8 *bh = data + 8 + i * 8;
		const u32 stored = ((u32)bh[1] << 16) | ((u32)bh[2] << 8) | bh[3]; // 24-bit stored_size
		const u32 usize = rd_be32 (bh + 4);
		if (pos + stored > size || total + usize < total)
			return 0;
		pos += stored;
		total += usize;
	}
	if (total > (256u << 20))
		return 0;

	u8 *out = MALLOC (total);
	if (!out)
		return 0;

	pos = 8 + (u64)blocks * 8;
	u64 opos = 0;
	for (uint i = 0; i < blocks; i++)
	{
		const u8 *bh = data + 8 + i * 8;
		const u32 stored = ((u32)bh[1] << 16) | ((u32)bh[2] << 8) | bh[3];
		const u32 usize = rd_be32 (bh + 4);

		if (stored == usize)
			memcpy (out + opos, data + pos, usize);
		else
		{
			// Prime 2/3 split a compressed CMPD block into signed-16-bit-size
			// segments. Negative sizes are stored bytes; positive sizes select
			// zlib or LZO1X from the first two segment bytes. DKCR predates that
			// convention and has one zlib stream for the whole block, so retain it
			// as a fallback when the strict segmented parse does not fit.
			uint sp = 0, so = 0;
			bool segmented = true;
			while (sp < stored)
			{
				if (stored - sp < 2)
				{
					segmented = false;
					break;
				}
				const uint word = (uint)data[pos + sp] << 8 | data[pos + sp + 1];
				sp += 2;
				// This codebase's `bool` is a packed 1-byte enum (see
				// dclib-types.h) -- assigning a wide bitmask test directly
				// truncates to the low 8 bits *before* the bool conversion,
				// so `word & 0x8000` (a bit entirely in the high byte) was
				// silently always 0/false here regardless of the real flag,
				// corrupting every raw (negative-size literal) segment in
				// every real CMPD stream. `!= 0` forces a proper 0-or-1
				// result first.
				const bool raw = (word & 0x8000) != 0;
				const uint sn = raw ? (0x10000 - word) : word;
				if (!sn || sn > stored - sp)
				{
					segmented = false;
					break;
				}
				if (raw)
				{
					if (sn > usize - so)
					{
						segmented = false;
						break;
					}
					memcpy (out + opos + so, data + pos + sp, sn);
					so += sn;
				}
				else
				{
					u8 *dec = 0;
					uint dec_size = 0;
					const u8 *seg = data + pos + sp;
					const bool zlib = sn >= 2 && seg[0] == 0x78
						&& (seg[1] == 0x01 || seg[1] == 0x9c || seg[1] == 0xda);
					const enumError derr = zlib ? DecodeZlibGrow (&dec, &dec_size, seg, sn)
												: DecodeLZO1XGrow (&dec, &dec_size, seg, sn);
					if (derr != ERR_OK || dec_size > usize - so)
					{
						FREE (dec);
						segmented = false;
						break;
					}
					memcpy (out + opos + so, dec, dec_size);
					so += dec_size;
					FREE (dec);
				}
				sp += sn;
			}
			if (segmented && sp == stored && so == usize)
				; // decoded above
			else
			{
				u8 *dec = 0;
				uint dec_size = 0;
				if (DecodeZlibGrow (&dec, &dec_size, data + pos, stored) != ERR_OK
					|| dec_size != usize)
				{
					FREE (dec);
					FREE (out);
					return 0;
				}
				memcpy (out + opos, dec, usize);
				FREE (dec);
			}
		}
		pos += stored;
		opos += usize;
	}

	*res_size = total;
	return out;
}

//-----------------------------------------------------------------------------
///////////////	Mistwalker ".pk"/".pkh" (The Last Story)	///////////////
//-----------------------------------------------------------------------------

void ResetLSPK (lspk_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	memset (pak, 0, sizeof (*pak));
}

enumError ScanLSPK (lspk_t *pak, const u8 *pkh_data, uint pkh_size, const u8 *pk_data, uint pk_size)
{
	if (!pak || !pkh_data || pkh_size < 4 || !pk_data)
		return ERR_NOTHING_TO_DO;

	memset (pak, 0, sizeof (*pak));

	const u32 n = rd_be32 (pkh_data);
	if (!n || n > 0x1000000)
		return ERR_NOTHING_TO_DO;

	const u64 table_size = (u64)pkh_size - 4;
	if (table_size % n)
		return ERR_NOTHING_TO_DO;
	const u32 row = (u32)(table_size / n);
	if (row != 16 && row != 24)
		return ERR_NOTHING_TO_DO;

	lspk_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = pkh_data + 4 + (u64)i * row;
		u32 hash, dec_size, com_size;
		u64 off;

		if (row == 16)
		{
			hash = rd_be32 (h);
			off = rd_be32 (h + 4);
			dec_size = rd_be32 (h + 8);
			com_size = rd_be32 (h + 0xc);
		}
		else // row == 24
		{
			hash = rd_be32 (h);
			const u32 off_hi = rd_be32 (h + 8);
			const u32 off_lo = rd_be32 (h + 0xc);
			off = (u64)off_hi << 32 | off_lo;
			dec_size = rd_be32 (h + 0x10);
			com_size = rd_be32 (h + 0x14);
		}

		const u32 stored_size = com_size ? com_size : dec_size;
		if (off + stored_size > pk_size)
			continue; // out-of-range entry: reject the whole file below instead

		entries[i].data = pk_data + off;
		entries[i].size = stored_size;
		entries[i].dec_size = com_size ? dec_size : 0;
		entries[i].hash = hash;
	}

	// Same all-or-nothing byte-accounting rule as ScanGPAK()/ScanBNS()/
	// ScanRPAK() above: no magic to check in the .pkh table itself, so
	// every real sample must resolve fully or the structural guess (which
	// of the two row layouts, and where offset_hi/offset_lo/dec/com sit
	// within it) is wrong for this file.
	for (uint i = 0; i < n; i++)
		if (!entries[i].data)
		{
			FREE (entries);
			return ERR_NOTHING_TO_DO;
		}

	pak->pk_data = pk_data;
	pak->pk_size = pk_size;
	pak->entries = entries;
	pak->n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	Embedded NW4R RLYT/RLAN fallback scan		///////////////
//-----------------------------------------------------------------------------

void ResetEmbeddedNW4R (nw4r_embedded_t *found)
{
	if (!found)
		return;
	FREE (found->entries);
	memset (found, 0, sizeof (*found));
}

void ScanEmbeddedNW4R (nw4r_embedded_t *found, const u8 *data, uint size)
{
	memset (found, 0, sizeof (*found));
	if (!data || size < 0x10)
		return;

	uint cap = 0;
	for (uint pos = 0; pos + 0x10 <= size; pos++)
	{
		const bool is_rlan = !memcmp (data + pos, "RLAN", 4);
		if (!is_rlan && memcmp (data + pos, "RLYT", 4))
			continue;

		if (rd_be16 (data + pos + 4) != 0xfeff)
			continue;
		const u32 fsize = rd_be32 (data + pos + 8);
		const u16 hsize = rd_be16 (data + pos + 12);
		if (hsize != 0x10 || fsize < 0x10 || (u64)pos + fsize > size)
			continue;

		if (found->n_entries == cap)
		{
			cap = cap ? cap * 2 : 16;
			found->entries = REALLOC (found->entries, cap * sizeof (*found->entries));
		}
		nw4r_embedded_entry_t *e = found->entries + found->n_entries++;
		e->data = data + pos;
		e->size = fsize;
		e->is_brlan = is_rlan;

		pos += fsize - 1; // skip past this resource; the outer loop's pos++ lands right after it
	}
}

//-----------------------------------------------------------------------------
///////////////		WARC (Game & Wario, Wii U)		///////////////
//-----------------------------------------------------------------------------

void ResetWARC (warc_t *warc)
{
	if (!warc)
		return;
	FREE (warc->entries);
	FREE (warc->names);
	memset (warc, 0, sizeof (*warc));
}

// Layout ported from aluigi's public QuickBMS script (game_wario.bms), all
// big-endian:
//
//   magic 'WARC'
//   u32 dummy, u32 zero, u32 warc_size, u32 info_size
//   u16 folders, u16 files
//   u32 x8 dummy
//   u32 entries, u32 dummy                       <- 64-byte header
//
//   files[FILES]:  5x u32 dummy, u32 size, u32 dummy, u32 offset  (32B each)
//   entries[ENTRIES+1]: u32, u16, u16, u32, u32                   (16B each,
//                       unused here -- BMS reads and discards them too)
//
//   folders[FOLDERS]: NUL-terminated string, padded to a 4-byte boundary
//   files[FILES]:     NUL-terminated string, padded to a 4-byte boundary,
//                     paired positionally with the offset/size table above
//
// `offset` is absolute into the file (there is no compression at all, unlike
// Excite's superficially-similar TOC/RES). Only the *first* folder-path
// string is actually used to prefix names -- WARC has no real directory
// tree, matching the BMS script's own "lame solution" comment.
//
// Verified against a real retail Bmp.warc (pulled from a decrypted USA
// "Game & Wario" disc via the standard wux2wud/wud2app/cdecrypt chain): 93
// entries, every offset+size in-bounds, payloads are valid 16x16x32bpp
// Windows BMPs.
enumError ScanWARC (warc_t *warc, const u8 *data, uint size)
{
	if (!warc || !data || size < 64 || memcmp (data, "WARC", 4))
		return EINVAL;
	memset (warc, 0, sizeof (*warc));

	uint off = 4;
	off += 16; // dummy, zero, warc_size, info_size
	const uint folders = rd_be16 (data + off);
	off += 2;
	const uint files = rd_be16 (data + off);
	off += 2;
	off += 8 * 4; // 8 dummy longs
	const u32 entries = rd_be32 (data + off);
	off += 4;
	off += 4; // dummy

	if (files > 0x100000 || folders > 0x10000)
		return EINVAL;
	if ((u64)off + (u64)files * 32 > size)
		return EINVAL;

	u32 *file_off = MALLOC (files * sizeof (u32));
	u32 *file_size = MALLOC (files * sizeof (u32));
	if (!file_off || !file_size)
	{
		FREE (file_off);
		FREE (file_size);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < files; i++, off += 32)
	{
		const u8 *h = data + off;
		file_size[i] = rd_be32 (h + 20);
		file_off[i] = rd_be32 (h + 28);
	}

	// entries table: (entries+1) records of 16 bytes each, ignored
	if ((u64)off + ((u64)entries + 1) * 16 > size)
	{
		FREE (file_off);
		FREE (file_size);
		return EINVAL;
	}
	off += ((uint)entries + 1) * 16;

	// one folder path string (only the first is used, per the BMS comment)
	char path[256] = "";
	for (uint i = 0; i < folders; i++)
	{
		uint start = off;
		while (off < size && data[off])
			off++;
		if (off >= size)
		{
			FREE (file_off);
			FREE (file_size);
			return EINVAL;
		}
		if (!i)
		{
			uint len = off - start;
			if (len >= sizeof (path))
				len = sizeof (path) - 1;
			memcpy (path, data + start, len);
			path[len] = 0;
		}
		off = off + 1; // skip NUL
		off = (off + 3) & ~3u; // pad to 4
	}

	warc_entry_t *out_entries = CALLOC (files, sizeof (*out_entries));
	char *names = CALLOC (1, size); // names live inside the source file, plus a little slack
	if (!out_entries || !names)
	{
		FREE (file_off);
		FREE (file_size);
		FREE (out_entries);
		FREE (names);
		return ERR_CANT_CREATE;
	}
	uint name_pos = 0;
	uint pathlen = (uint)strlen (path);

	uint i;
	for (i = 0; i < files; i++)
	{
		uint start = off;
		while (off < size && data[off])
			off++;
		if (off >= size)
			break;
		uint namelen = off - start;
		off = off + 1; // skip NUL
		off = (off + 3) & ~3u; // pad to 4

		if ((u64)file_off[i] + file_size[i] > size)
		{
			// out-of-range member: skip rather than reading past the buffer
			continue;
		}

		out_entries[i].name = names + name_pos;
		if (pathlen)
		{
			memcpy (names + name_pos, path, pathlen);
			name_pos += pathlen;
			names[name_pos++] = '/';
		}
		memcpy (names + name_pos, data + start, namelen);
		name_pos += namelen;
		names[name_pos++] = 0;

		out_entries[i].data = data + file_off[i];
		out_entries[i].size = file_size[i];
	}

	FREE (file_off);
	FREE (file_size);

	if (!i)
	{
		FREE (out_entries);
		FREE (names);
		return EINVAL;
	}
	warc->data = data;
	warc->size = size;
	warc->entries = out_entries;
	warc->n_entries = i;
	warc->names = names;
	return ERR_OK;
}

// Inverse of ScanWARC. The single flat folder-path string is taken from the
// directory portion of entries[0].name; entries sharing that same prefix get
// it stripped for their stored file name (matching what ScanWARC re-adds on
// read), everything else is stored under its full given name.
enumError CreateWARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x100000)
		return EINVAL;

	ccp folder = "";
	uint folder_len = 0;
	{
		ccp n0 = entries[0].name ? entries[0].name : "";
		ccp slash = strrchr (n0, '/');
		if (slash)
		{
			folder = n0;
			folder_len = (uint)(slash - n0);
		}
	}
	// WARC stores exactly one prefix which ScanWARC applies to every name.
	// Only factor it out when every input shares it; otherwise retain each
	// complete relative name and emit no prefix.
	for (uint i = 1; folder_len && i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		if (strncmp (name, folder, folder_len) || name[folder_len] != '/')
			folder_len = 0;
	}
	const uint folders = folder_len ? 1 : 0;

	ccp *fname = MALLOC (n_entries * sizeof (ccp));
	if (!fname)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		if (folder_len && !strncmp (name, folder, folder_len) && name[folder_len] == '/')
			fname[i] = name + folder_len + 1;
		else
			fname[i] = name;
	}

	u64 total = 64 + (u64)n_entries * 32 + ((u64)n_entries + 1) * 16;
	if (folders)
	{
		total += folder_len + 1;
		total = (total + 3) & ~(u64)3;
	}
	for (uint i = 0; i < n_entries; i++)
	{
		total += strlen (fname[i]) + 1;
		total = (total + 3) & ~(u64)3;
	}
	const u64 names_end = total;
	for (uint i = 0; i < n_entries; i++)
		total += entries[i].size;

	if (total > NFMT_MAX_OUTPUT)
	{
		FREE (fname);
		return EFBIG;
	}

	u8 *out = CALLOC (1, (size_t)total);
	if (!out)
	{
		FREE (fname);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "WARC", 4);
	wr_be32 (out + 4, 0); // dummy
	wr_be32 (out + 8, 0); // zero
	wr_be32 (out + 12, (u32)total); // warc_size
	wr_be32 (out + 16, (u32)names_end); // info_size
	wr_be16 (out + 20, (u16)folders);
	wr_be16 (out + 22, (u16)n_entries);
	for (uint i = 0; i < 8; i++)
		wr_be32 (out + 24 + i * 4, 0);
	wr_be32 (out + 56, n_entries); // entries
	wr_be32 (out + 60, 0); // dummy

	u32 *data_off = MALLOC (n_entries * sizeof (u32));
	if (!data_off)
	{
		FREE (fname);
		FREE (out);
		return ERR_CANT_CREATE;
	}

	uint off = 64;
	u32 cur_off = (u32)names_end;
	for (uint i = 0; i < n_entries; i++, off += 32)
	{
		u8 *h = out + off;
		wr_be32 (h + 20, entries[i].size);
		wr_be32 (h + 28, cur_off);
		data_off[i] = cur_off;
		cur_off += entries[i].size;
	}

	off += ((uint)n_entries + 1) * 16; // entries table stays all-zero (unused on read)

	if (folders)
	{
		memcpy (out + off, folder, folder_len);
		off += folder_len + 1;
		off = (off + 3) & ~3u;
	}

	for (uint i = 0; i < n_entries; i++)
	{
		size_t len = strlen (fname[i]);
		memcpy (out + off, fname[i], len);
		off += (uint)len + 1;
		off = (off + 3) & ~3u;
	}

	for (uint i = 0; i < n_entries; i++)
	{
		if (entries[i].size && entries[i].data)
			memcpy (out + data_off[i], entries[i].data, entries[i].size);
	}

	FREE (fname);
	FREE (data_off);
	*dest = out;
	*dest_size = (uint)total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		NCCARC (WarioWare: Touched! graphics)	///////////////
//-----------------------------------------------------------------------------

void ResetNCCARC (nccarc_t *nc)
{
	if (!nc)
		return;
	FREE (nc->entries);
	memset (nc, 0, sizeof (*nc));
}

// No magic, no size/count field, no public documentation -- the only prior
// art found is a single unanswered GBAtemp thread (2017) asking what this
// format even is. Reverse-engineered from scratch by byte-accounting on 307
// real "cg_*.nccarc"/"cg_*.nccarc_c" files from a real WarioWare: Touched!
// (USA) cartridge dump (post-LZ-decompression -- the "_c" suffix is this
// fork's already-working outer LZSS layer, unrelated to this format).
//
// Layout: a flat table of little-endian u32 offsets starting at file offset
// 0, no explicit count field. The table's own byte length equals its FIRST
// entry's value (i.e. entry[0] always points exactly past the end of the
// table itself), so the entry count is simply entry[0]/4. The table's LAST
// entry always equals the file size -- it's a terminator, not a real chunk,
// giving n-1 real chunks spanning [entry[i], entry[i+1]) for i in [0,n-2).
// Some entries have bit 31 set; masking it off (& 0x7fffffff) always restores
// a value that fits monotonically between its neighbours, so it's a per-
// chunk flag bit of unknown meaning layered on top of an otherwise-ordinary
// offset, not a different encoding -- preserved per-entry (nccarc_entry_t.
// flag) rather than guessed at or discarded.
//
// Verified: entry[0]==table byte length, entries monotonically
// non-decreasing after masking bit 31, and last entry==file size held
// exactly on 305/305 non-empty real samples (2 files were legitimately
// empty 4-byte placeholders, not a format violation). What's inside each
// chunk (raw tile/palette data, whole-screen illustrations, or something
// else -- chunk sizes vary widely, from ~192B pairs up to ~27KB) is NOT
// decoded here; this only recovers the container's own member boundaries,
// same scope as this fork's PAC/GFA support before their contents were
// individually understood.
enumError ScanNCCARC (nccarc_t *nc, const u8 *data, uint size)
{
	if (!nc || !data)
		return EINVAL;
	memset (nc, 0, sizeof (*nc));
	if (size < 8)
		return EINVAL; // a real empty container is 4 zero bytes; nothing to split

	const u32 table_bytes = rd_le32 (data);
	if (!table_bytes || table_bytes & 3 || table_bytes > size)
		return EINVAL;
	const uint n = table_bytes / 4;
	if (n < 2 || n > 0x40000)
		return EINVAL;

	u32 *off = CALLOC (n, sizeof (*off));
	bool *flag = CALLOC (n, sizeof (*flag));
	if (!off || !flag)
	{
		FREE (off);
		FREE (flag);
		return ERR_CANT_CREATE;
	}

	u32 prev = 0;
	uint i;
	for (i = 0; i < n; i++)
	{
		const u32 raw = rd_le32 (data + i * 4);
		const u32 masked = raw & 0x7fffffff;
		if (masked < prev || masked > size)
			break;
		off[i] = masked;
		flag[i] = (raw & 0x80000000) != 0;
		prev = masked;
	}
	if (i != n || off[0] != table_bytes || off[n - 1] != size)
	{
		FREE (off);
		FREE (flag);
		return EINVAL;
	}

	nccarc_entry_t *entries = CALLOC (n - 1, sizeof (*entries));
	if (!entries)
	{
		FREE (off);
		FREE (flag);
		return ERR_CANT_CREATE;
	}
	for (i = 0; i < n - 1; i++)
	{
		entries[i].data = data + off[i];
		entries[i].size = off[i + 1] - off[i];
		entries[i].flag = flag[i];
	}
	FREE (off);
	FREE (flag);

	nc->data = data;
	nc->size = size;
	nc->entries = entries;
	nc->n_entries = n - 1;
	return ERR_OK;
}

enumError CreateNCCARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	const uint n_offsets = n_entries + 1;
	const uint table_size = n_offsets * 4;

	u64 total_size = table_size;
	for (uint i = 0; i < n_entries; i++)
		total_size += entries[i].size;

	if (total_size > NFMT_MAX_OUTPUT)
		return EFBIG;

	u8 *out = CALLOC (1, (size_t)total_size);
	if (!out)
		return ERR_CANT_CREATE;

	u32 cur_off = table_size;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + i * 4, cur_off);
		if (entries[i].size > 0 && entries[i].data)
			memcpy (out + cur_off, entries[i].data, entries[i].size);
		cur_off += entries[i].size;
	}
	wr_le32 (out + n_entries * 4, cur_off);

	*dest = out;
	*dest_size = (uint)total_size;
	return ERR_OK;
}

enumError CreateAT7 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool compress)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	const uint toc_entry_size = 28;
	const uint n_toc_entries = n_entries + 1;
	const uint toc_size = n_toc_entries * toc_entry_size;

	u64 raw_total = toc_size;
	for (uint i = 0; i < n_entries; i++)
		raw_total += entries[i].size;

	if (raw_total > NFMT_MAX_OUTPUT)
		return EFBIG;

	u8 *raw = CALLOC (1, (size_t)raw_total);
	if (!raw)
		return ERR_CANT_CREATE;

	u32 cur_off = toc_size;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *t = raw + i * toc_entry_size;
		wr_be32 (t + 0, cur_off);
		wr_be32 (t + 4, entries[i].size);
		ccp name = entries[i].name ? entries[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		strncpy ((char *)t + 8, name, 20);

		if (entries[i].size > 0 && entries[i].data)
			memcpy (raw + cur_off, entries[i].data, entries[i].size);
		cur_off += entries[i].size;
	}
	u8 *sentinel = raw + n_entries * toc_entry_size;
	wr_be32 (sentinel + 0, 0);
	wr_be32 (sentinel + 4, 0);
	strncpy ((char *)sentinel + 8, "namesEnd", 20);

	if (compress)
	{
		u8 *comp = 0;
		uint comp_size = 0;
		enumError err = EncodeAT7 (&comp, &comp_size, raw, (uint)raw_total);
		FREE (raw);
		if (err)
			return err;
		*dest = comp;
		*dest_size = comp_size;
		return ERR_OK;
	}

	*dest = raw;
	*dest_size = (uint)raw_total;
	return ERR_OK;
}

enumError CreatePAC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	uint cur_size = 0x40;
	for (uint i = 0; i < n_entries; i++)
	{
		cur_size += 0x20;
		cur_size += entries[i].size;
		cur_size = (cur_size + 0x1F) & ~0x1Fu;
	}

	u8 *out = CALLOC (1, cur_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "ARC\0", 4);
	out[4] = 1;
	out[5] = 1;
	wr_be16 (out + 6, (u16)n_entries);

	uint off = 0x40;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp full_name = entries[i].name ? entries[i].name : "misc";
		ccp slash = strrchr (full_name, '/');
		ccp fname = slash ? slash + 1 : full_name;

		u16 type = 1; // MiscData
		if (entries[i].data && entries[i].size >= 4)
		{
			if (!memcmp (entries[i].data, "bres", 4) || !memcmp (entries[i].data, "MDL0", 4))
				type = 2; // ModelData
			else if (!memcmp (entries[i].data, "TEX0", 4))
				type = 3; // TextureData
			else if (!memcmp (entries[i].data, "ANIM", 4) || !memcmp (entries[i].data, "CHR0", 4)
				|| !memcmp (entries[i].data, "CLR0", 4) || !memcmp (entries[i].data, "PAT0", 4)
				|| !memcmp (entries[i].data, "SHP0", 4) || !memcmp (entries[i].data, "VIS0", 4)
				|| !memcmp (entries[i].data, "SCN0", 4))
				type = 5; // AnmGroup
		}

		u8 *h = out + off;
		wr_be16 (h + 0, type);
		wr_be16 (h + 2, (u16)i);
		wr_be32 (h + 4, entries[i].size);
		h[8] = 0;
		h[9] = 0;
		wr_be16 (h + 10, 0xFFFF);

		size_t nlen = strlen (fname);
		if (nlen > 15)
			nlen = 15;
		memcpy (h + 0x10, fname, nlen);
		h[0x10 + nlen] = 0;

		if (entries[i].size && entries[i].data)
			memcpy (out + off + 0x20, entries[i].data, entries[i].size);

		off += 0x20 + entries[i].size;
		off = (off + 0x1F) & ~0x1Fu;
	}

	*dest = out;
	*dest_size = cur_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		DARC (3DS "darc" archive) support	///////////////
//-----------------------------------------------------------------------------

void ResetDARC (darc_t *darc)
{
	if (!darc)
		return;
	if (darc->entries)
		for (uint i = 0; i < darc->n_entries; i++)
			FREE (darc->entries[i].name);
	FREE (darc->entries);
	memset (darc, 0, sizeof (*darc));
}

// NUL-terminated UTF-16LE -> malloc'd UTF-8. DARC names are game asset/path
// components (western titles observed so far are plain ASCII), so this only
// needs to be correct, not fast; surrogate pairs are handled for
// completeness even though no real sample has needed one yet.
static char *darc_utf16le_to_utf8 (const u8 *p, const u8 *end)
{
	uint cap = 4, len = 0;
	char *out = MALLOC (cap);
	if (!out)
		return 0;
	while (p + 2 <= end)
	{
		const u16 u = rd_le16 (p);
		p += 2;
		if (!u)
			break;
		u32 cp = u;
		if (u >= 0xD800 && u <= 0xDBFF && p + 2 <= end)
		{
			const u16 lo = rd_le16 (p);
			if (lo >= 0xDC00 && lo <= 0xDFFF)
			{
				cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				p += 2;
			}
			else
				cp = 0xFFFD;
		}
		else if (u >= 0xD800 && u <= 0xDFFF)
			cp = 0xFFFD;

		char enc[4];
		uint n;
		if (cp < 0x80)
		{
			enc[0] = (char)cp;
			n = 1;
		}
		else if (cp < 0x800)
		{
			enc[0] = 0xC0 | (cp >> 6);
			enc[1] = 0x80 | (cp & 0x3f);
			n = 2;
		}
		else if (cp < 0x10000)
		{
			enc[0] = 0xE0 | (cp >> 12);
			enc[1] = 0x80 | ((cp >> 6) & 0x3f);
			enc[2] = 0x80 | (cp & 0x3f);
			n = 3;
		}
		else
		{
			enc[0] = 0xF0 | (cp >> 18);
			enc[1] = 0x80 | ((cp >> 12) & 0x3f);
			enc[2] = 0x80 | ((cp >> 6) & 0x3f);
			enc[3] = 0x80 | (cp & 0x3f);
			n = 4;
		}

		if (len + n + 1 > cap)
		{
			cap = (len + n + 1) * 2;
			char *grown = REALLOC (out, cap);
			if (!grown)
			{
				FREE (out);
				return 0;
			}
			out = grown;
		}
		memcpy (out + len, enc, n);
		len += n;
	}
	out[len] = 0;
	return out;
}

// Header/entry layout: see the long comment above darc_t in lib-nintendo.h.
// Verified byte-for-byte against a real retail 3DS sample (Tomodachi Life,
// CTR-P-EC6E, romfs/layout/*.bin) as well as GBATEK, 3dbrew, and Tyulis/
// 3DSkit's reference unpacker.
enumError ScanDARC (darc_t *darc, const u8 *data, uint size)
{
	if (!darc || !data || size < 0x1c || memcmp (data, "darc", 4))
		return EINVAL;
	if (rd_le16 (data + 4) != 0xfeff)
		return EINVAL; // only little-endian DARC has ever been observed

	const uint header_size = rd_le16 (data + 6);
	const uint file_size = rd_le32 (data + 0xc);
	const uint table_offset = rd_le32 (data + 0x10);
	const uint table_size = rd_le32 (data + 0x14);
	if (header_size < 0x1c || file_size > size || table_offset < header_size || table_size < 12
		|| (u64)table_offset + table_size > size)
		return EINVAL;

	const uint n = table_size / 12; // upper bound; real count comes from entry 0 below
	if (!n || (u64)table_offset + 12 > size)
		return EINVAL;

	const u32 e0 = rd_le32 (data + table_offset);
	if (!(e0 & 0x01000000)) // entry 0 must be the root directory
		return EINVAL;
	const uint n_entries = rd_le32 (data + table_offset + 8); // root's end-index
	if (!n_entries || n_entries > n || (u64)table_offset + (u64)n_entries * 12 > size)
		return EINVAL;

	const u8 *name_area = data + table_offset + n_entries * 12;
	const u8 *name_area_end
		= data + (table_offset + table_size <= size ? table_offset + table_size : size);

	darc_entry_t *entries = CALLOC (n_entries, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *e = data + table_offset + i * 12;
		const u32 f0 = rd_le32 (e);
		const u32 f1 = rd_le32 (e + 4);
		const u32 f2 = rd_le32 (e + 8);
		const bool is_dir = (f0 & 0x01000000) != 0;
		const uint name_off = f0 & 0xffffff;

		entries[i].is_dir = is_dir;
		entries[i].parent_or_offset = f1;
		entries[i].end_or_size = f2;

		if (name_area + name_off < name_area_end)
			entries[i].name = darc_utf16le_to_utf8 (name_area + name_off, name_area_end);
		if (!entries[i].name)
			entries[i].name = STRDUP ("");

		if (!is_dir && ((u64)f1 + f2 > size))
		{
			for (uint k = 0; k <= i; k++)
				FREE (entries[k].name);
			FREE (entries);
			return EINVAL; // a file's data must stay inside the archive
		}
	}

	darc->data = data;
	darc->size = size;
	darc->entries = entries;
	darc->n_entries = n_entries;
	return ERR_OK;
}

typedef struct darc_build_node_t
{
	char *name;
	bool is_dir;
	uint parent_node_idx;
	uint end_subtree_idx;
	uint table_idx;
	uint orig_entry_idx;
	uint name_off;
	uint data_off;
	uint data_size;
	uint num_children;
	uint *child_indices;
} darc_build_node_t;

static void flatten_darc_node (
	darc_build_node_t *nodes, uint cur_node, uint *order, uint *order_count)
{
	uint my_table_idx = (*order_count)++;
	nodes[cur_node].table_idx = my_table_idx;
	order[my_table_idx] = cur_node;

	for (uint c = 0; c < nodes[cur_node].num_children; c++)
	{
		uint child = nodes[cur_node].child_indices[c];
		if (nodes[child].is_dir)
			flatten_darc_node (nodes, child, order, order_count);
		else
		{
			uint f_table_idx = (*order_count)++;
			nodes[child].table_idx = f_table_idx;
			order[f_table_idx] = child;
		}
	}

	nodes[cur_node].end_subtree_idx = *order_count;
}

enumError CreateDARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	darc_build_node_t nodes[1024];
	uint num_nodes = 1;
	memset (nodes, 0, sizeof (nodes));
	nodes[0].name = STRDUP (".");
	nodes[0].is_dir = true;
	nodes[0].parent_node_idx = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp full_name = entries[i].name ? entries[i].name : "file";
		char dir_part[PATH_MAX] = { 0 };
		char file_part[PATH_MAX] = { 0 };

		ccp slash = strrchr (full_name, '/');
		if (slash)
		{
			size_t dlen = slash - full_name;
			if (dlen >= sizeof (dir_part))
				dlen = sizeof (dir_part) - 1;
			memcpy (dir_part, full_name, dlen);
			dir_part[dlen] = 0;
			snprintf (file_part, sizeof (file_part), "%s", slash + 1);
		}
		else
		{
			snprintf (file_part, sizeof (file_part), "%s", full_name);
		}

		uint cur_dir = 0;
		if (dir_part[0])
		{
			char *p = dir_part;
			while (*p)
			{
				char seg[PATH_MAX];
				char *slash2 = strchr (p, '/');
				if (slash2)
				{
					size_t slen = slash2 - p;
					if (slen >= sizeof (seg))
						slen = sizeof (seg) - 1;
					memcpy (seg, p, slen);
					seg[slen] = 0;
					p = slash2 + 1;
				}
				else
				{
					snprintf (seg, sizeof (seg), "%s", p);
					p += strlen (p);
				}

				int found = -1;
				for (uint c = 0; c < nodes[cur_dir].num_children; c++)
				{
					uint cidx = nodes[cur_dir].child_indices[c];
					if (nodes[cidx].is_dir && !strcmp (nodes[cidx].name, seg))
					{
						found = (int)cidx;
						break;
					}
				}

				if (found < 0)
				{
					if (num_nodes >= 1024)
						break;
					uint new_d = num_nodes++;
					nodes[new_d].name = STRDUP (seg);
					nodes[new_d].is_dir = true;
					nodes[new_d].parent_node_idx = cur_dir;
					nodes[cur_dir].child_indices = REALLOC (nodes[cur_dir].child_indices,
						(nodes[cur_dir].num_children + 1) * sizeof (uint));
					nodes[cur_dir].child_indices[nodes[cur_dir].num_children++] = new_d;
					cur_dir = new_d;
				}
				else
				{
					cur_dir = (uint)found;
				}
			}
		}

		if (num_nodes < 1024)
		{
			uint new_f = num_nodes++;
			nodes[new_f].name = STRDUP (file_part);
			nodes[new_f].is_dir = false;
			nodes[new_f].parent_node_idx = cur_dir;
			nodes[new_f].orig_entry_idx = i;
			nodes[new_f].data_size = entries[i].size;
			nodes[cur_dir].child_indices = REALLOC (
				nodes[cur_dir].child_indices, (nodes[cur_dir].num_children + 1) * sizeof (uint));
			nodes[cur_dir].child_indices[nodes[cur_dir].num_children++] = new_f;
		}
	}

	uint order[1024];
	uint order_count = 0;
	flatten_darc_node (nodes, 0, order, &order_count);

	uint name_cap = 4096;
	u8 *name_table = MALLOC (name_cap);
	if (!name_table)
	{
		for (uint n = 0; n < num_nodes; n++)
		{
			FREE (nodes[n].name);
			FREE (nodes[n].child_indices);
		}
		return ERR_CANT_CREATE;
	}
	uint name_pos = 0;

	for (uint i = 0; i < order_count; i++)
	{
		uint nidx = order[i];
		nodes[nidx].name_off = name_pos;
		ccp nstr = nodes[nidx].name;
		size_t nlen = strlen (nstr);

		while (name_pos + 2 * nlen + 2 > name_cap)
		{
			name_cap *= 2;
			name_table = REALLOC (name_table, name_cap);
		}

		for (size_t c = 0; c < nlen; c++)
		{
			wr_le16 (name_table + name_pos, (u16)(u8)nstr[c]);
			name_pos += 2;
		}
		wr_le16 (name_table + name_pos, 0);
		name_pos += 2;
	}

	uint name_table_size = (name_pos + 3) & ~3u;

	const uint table_size = 12 * order_count + name_table_size;
	uint cur_data_off = (0x1C + table_size + 0x7F) & ~0x7Fu;
	const uint data_base_off = cur_data_off;

	for (uint i = 0; i < order_count; i++)
	{
		uint nidx = order[i];
		if (!nodes[nidx].is_dir)
		{
			nodes[nidx].data_off = cur_data_off;
			cur_data_off += (nodes[nidx].data_size + 3) & ~3u;
		}
	}

	const uint total_file_size = cur_data_off;
	u8 *out = CALLOC (1, total_file_size);
	if (!out)
	{
		FREE (name_table);
		for (uint n = 0; n < num_nodes; n++)
		{
			FREE (nodes[n].name);
			FREE (nodes[n].child_indices);
		}
		return ERR_CANT_CREATE;
	}

	memcpy (out, "darc", 4);
	wr_le16 (out + 4, 0xFEFF);
	wr_le16 (out + 6, 0x001C);
	wr_le32 (out + 8, 0x01000000);
	wr_le32 (out + 0x0C, total_file_size);
	wr_le32 (out + 0x10, 0x0000001C);
	wr_le32 (out + 0x14, table_size);
	wr_le32 (out + 0x18, data_base_off);

	for (uint i = 0; i < order_count; i++)
	{
		uint nidx = order[i];
		u8 *e = out + 0x1C + 12 * i;
		if (nodes[nidx].is_dir)
		{
			uint parent_tidx = nodes[nodes[nidx].parent_node_idx].table_idx;
			wr_le32 (e + 0, 0x01000000 | (nodes[nidx].name_off & 0x00FFFFFF));
			wr_le32 (e + 4, parent_tidx);
			wr_le32 (e + 8, nodes[nidx].end_subtree_idx);
		}
		else
		{
			wr_le32 (e + 0, nodes[nidx].name_off & 0x00FFFFFF);
			wr_le32 (e + 4, nodes[nidx].data_off);
			wr_le32 (e + 8, nodes[nidx].data_size);
		}
	}

	memcpy (out + 0x1C + 12 * order_count, name_table, name_pos);
	FREE (name_table);

	for (uint i = 0; i < order_count; i++)
	{
		uint nidx = order[i];
		if (!nodes[nidx].is_dir && nodes[nidx].data_size)
		{
			uint oidx = nodes[nidx].orig_entry_idx;
			if (entries[oidx].data)
				memcpy (out + nodes[nidx].data_off, entries[oidx].data, nodes[nidx].data_size);
		}
	}

	for (uint n = 0; n < num_nodes; n++)
	{
		FREE (nodes[n].name);
		FREE (nodes[n].child_indices);
	}

	*dest = out;
	*dest_size = total_file_size;
	return ERR_OK;
}

typedef struct
{
	char *name;
	bool is_dir;
	uint parent_node_idx;
	uint *child_indices;
	uint num_children;
	uint data_idx;
	uint node_idx;
} rarc_build_node_t;

static u16 CalcRARCHash (ccp s)
{
	u16 h = 0;
	while (*s)
		h = (u16)(h * 3 + (u8)*s++);
	return h;
}

enumError CreateRARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	rarc_build_node_t nodes[1024];
	uint num_nodes = 1;
	memset (nodes, 0, sizeof (nodes));
	nodes[0].name = STRDUP ("ROOT");
	nodes[0].is_dir = true;
	nodes[0].parent_node_idx = 0;
	nodes[0].node_idx = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp full_name = entries[i].name ? entries[i].name : "file";
		char dir_part[PATH_MAX] = { 0 };
		char file_part[PATH_MAX] = { 0 };

		ccp slash = strrchr (full_name, '/');
		if (slash)
		{
			size_t dlen = slash - full_name;
			if (dlen >= sizeof (dir_part))
				dlen = sizeof (dir_part) - 1;
			memcpy (dir_part, full_name, dlen);
			dir_part[dlen] = 0;
			snprintf (file_part, sizeof (file_part), "%s", slash + 1);
		}
		else
		{
			snprintf (file_part, sizeof (file_part), "%s", full_name);
		}

		uint cur_dir = 0;
		if (dir_part[0])
		{
			char *p = dir_part;
			while (*p)
			{
				char seg[PATH_MAX];
				char *slash2 = strchr (p, '/');
				if (slash2)
				{
					size_t slen = slash2 - p;
					if (slen >= sizeof (seg))
						slen = sizeof (seg) - 1;
					memcpy (seg, p, slen);
					seg[slen] = 0;
					p = slash2 + 1;
				}
				else
				{
					snprintf (seg, sizeof (seg), "%s", p);
					p += strlen (p);
				}

				int found = -1;
				for (uint c = 0; c < nodes[cur_dir].num_children; c++)
				{
					uint cidx = nodes[cur_dir].child_indices[c];
					if (nodes[cidx].is_dir && !strcmp (nodes[cidx].name, seg))
					{
						found = (int)cidx;
						break;
					}
				}

				if (found < 0)
				{
					if (num_nodes >= 1024)
						break;
					uint new_d = num_nodes++;
					nodes[new_d].name = STRDUP (seg);
					nodes[new_d].is_dir = true;
					nodes[new_d].parent_node_idx = cur_dir;
					nodes[cur_dir].child_indices = REALLOC (nodes[cur_dir].child_indices,
						(nodes[cur_dir].num_children + 1) * sizeof (uint));
					nodes[cur_dir].child_indices[nodes[cur_dir].num_children++] = new_d;
					cur_dir = new_d;
				}
				else
				{
					cur_dir = (uint)found;
				}
			}
		}

		if (num_nodes < 1024)
		{
			uint new_f = num_nodes++;
			nodes[new_f].name = STRDUP (file_part);
			nodes[new_f].is_dir = false;
			nodes[new_f].parent_node_idx = cur_dir;
			nodes[new_f].data_idx = i;
			nodes[cur_dir].child_indices = REALLOC (
				nodes[cur_dir].child_indices, (nodes[cur_dir].num_children + 1) * sizeof (uint));
			nodes[cur_dir].child_indices[nodes[cur_dir].num_children++] = new_f;
		}
	}

	uint dir_nodes[1024];
	uint num_dirs = 0;
	for (uint i = 0; i < num_nodes; i++)
	{
		if (nodes[i].is_dir)
		{
			nodes[i].node_idx = num_dirs;
			dir_nodes[num_dirs++] = i;
		}
	}

	u8 *str_pool = MALLOC (65536);
	uint str_pool_cap = 65536;
	uint str_pool_len = 0;

#define APPEND_STR(s, out_off)                                                                     \
	do                                                                                             \
	{                                                                                              \
		size_t _slen = strlen (s) + 1;                                                             \
		if (str_pool_len + _slen > str_pool_cap)                                                   \
		{                                                                                          \
			str_pool_cap = (str_pool_cap + (uint)_slen) * 2;                                       \
			str_pool = REALLOC (str_pool, str_pool_cap);                                           \
		}                                                                                          \
		out_off = str_pool_len;                                                                    \
		memcpy (str_pool + str_pool_len, s, _slen);                                                \
		str_pool_len += (uint)_slen;                                                               \
	} while (0)

	uint dot_off, dotdot_off;
	APPEND_STR (".", dot_off);
	APPEND_STR ("..", dotdot_off);

	uint total_file_data = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		total_file_data = (total_file_data + 31) & ~31u;
		total_file_data += entries[i].size;
	}
	u8 *file_data_buf = CALLOC (1, total_file_data ? total_file_data : 32);

	uint cur_file_offset = 0;
	uint *entry_data_offsets = CALLOC (n_entries, sizeof (uint));
	for (uint i = 0; i < n_entries; i++)
	{
		cur_file_offset = (cur_file_offset + 31) & ~31u;
		entry_data_offsets[i] = cur_file_offset;
		if (entries[i].data && entries[i].size)
			memcpy (file_data_buf + cur_file_offset, entries[i].data, entries[i].size);
		cur_file_offset += entries[i].size;
	}

	uint total_entries_count = 0;
	for (uint d = 0; d < num_dirs; d++)
	{
		uint d_idx = dir_nodes[d];
		total_entries_count += 2 + nodes[d_idx].num_children;
	}

	uint node_table_size = num_dirs * 0x10;
	uint entry_table_size = total_entries_count * 0x14;

	u8 *node_table = CALLOC (1, node_table_size);
	u8 *entry_table = CALLOC (1, entry_table_size);

	uint cur_entry_idx = 0;
	uint file_id_counter = 0;

	for (uint d = 0; d < num_dirs; d++)
	{
		uint d_idx = dir_nodes[d];
		rarc_build_node_t *dir = &nodes[d_idx];

		uint dir_name_off = 0;
		APPEND_STR (dir->name, dir_name_off);

		char short_name[5] = "    ";
		for (int k = 0; k < 4 && dir->name[k]; k++)
			short_name[k] = (char)toupper ((unsigned char)dir->name[k]);

		u8 *node_ptr = node_table + d * 0x10;
		memcpy (node_ptr, short_name, 4);
		wr_be32 (node_ptr + 4, dir_name_off);
		wr_be16 (node_ptr + 8, CalcRARCHash (dir->name));
		wr_be16 (node_ptr + 10, (u16)(2 + dir->num_children));
		wr_be32 (node_ptr + 12, cur_entry_idx);

		// Entry: '.'
		u8 *e_dot = entry_table + cur_entry_idx++ * 0x14;
		wr_be16 (e_dot + 0, 0xFFFF);
		wr_be16 (e_dot + 2, CalcRARCHash ("."));
		wr_be16 (e_dot + 4, 0x0200); // directory
		wr_be16 (e_dot + 6, (u16)dot_off);
		wr_be32 (e_dot + 8, d); // current dir node idx
		wr_be32 (e_dot + 12, 0x10);

		// Entry: '..'
		u8 *e_dotdot = entry_table + cur_entry_idx++ * 0x14;
		wr_be16 (e_dotdot + 0, 0xFFFF);
		wr_be16 (e_dotdot + 2, CalcRARCHash (".."));
		wr_be16 (e_dotdot + 4, 0x0200); // directory
		wr_be16 (e_dotdot + 6, (u16)dotdot_off);
		uint p_node_idx = (d == 0) ? 0xFFFFFFFF : nodes[dir->parent_node_idx].node_idx;
		wr_be32 (e_dotdot + 8, p_node_idx);
		wr_be32 (e_dotdot + 12, 0x10);

		// Entries: children (subdirs first, then files)
		for (uint c = 0; c < dir->num_children; c++)
		{
			uint c_idx = dir->child_indices[c];
			rarc_build_node_t *child = &nodes[c_idx];
			if (child->is_dir)
			{
				uint c_name_off = 0;
				APPEND_STR (child->name, c_name_off);
				u8 *e_child = entry_table + cur_entry_idx++ * 0x14;
				wr_be16 (e_child + 0, 0xFFFF);
				wr_be16 (e_child + 2, CalcRARCHash (child->name));
				wr_be16 (e_child + 4, 0x0200);
				wr_be16 (e_child + 6, (u16)c_name_off);
				wr_be32 (e_child + 8, child->node_idx);
				wr_be32 (e_child + 12, 0x10);
			}
		}
		for (uint c = 0; c < dir->num_children; c++)
		{
			uint c_idx = dir->child_indices[c];
			rarc_build_node_t *child = &nodes[c_idx];
			if (!child->is_dir)
			{
				uint c_name_off = 0;
				APPEND_STR (child->name, c_name_off);
				u8 *e_child = entry_table + cur_entry_idx++ * 0x14;
				wr_be16 (e_child + 0, (u16)file_id_counter++);
				wr_be16 (e_child + 2, CalcRARCHash (child->name));
				wr_be16 (e_child + 4, 0x1100); // file
				wr_be16 (e_child + 6, (u16)c_name_off);
				wr_be32 (e_child + 8, entry_data_offsets[child->data_idx]);
				wr_be32 (e_child + 12, entries[child->data_idx].size);
			}
		}
	}

	uint str_pool_aligned = (str_pool_len + 31) & ~31u;
	u8 *str_pool_buf = CALLOC (1, str_pool_aligned);
	memcpy (str_pool_buf, str_pool, str_pool_len);
	FREE (str_pool);

	uint header_data_size = 0x20 + node_table_size + entry_table_size + str_pool_aligned;
	header_data_size = (header_data_size + 31) & ~31u;

	uint total_rarc_size = 0x20 + header_data_size + cur_file_offset;
	u8 *out = CALLOC (1, total_rarc_size);

	// File header (32 bytes)
	memcpy (out, "RARC", 4);
	wr_be32 (out + 4, total_rarc_size);
	wr_be32 (out + 8, 0x20); // header_off
	wr_be32 (out + 12, header_data_size); // header_size

	// RARC header (32 bytes at offset 0x20)
	u8 *rh = out + 0x20;
	wr_be32 (rh + 0, num_dirs);
	wr_be32 (rh + 4, 0x20); // root_off (offset from rh)
	wr_be32 (rh + 8, total_entries_count);
	wr_be32 (rh + 12, 0x20 + node_table_size); // entry_off (offset from rh)
	wr_be32 (rh + 16, str_pool_aligned);
	wr_be32 (rh + 20, 0x20 + node_table_size + entry_table_size); // str_pool_off
	wr_be16 (rh + 24, (u16)total_entries_count);

	// Node table
	memcpy (rh + 0x20, node_table, node_table_size);
	// Entry table
	memcpy (rh + 0x20 + node_table_size, entry_table, entry_table_size);
	// String pool
	memcpy (rh + 0x20 + node_table_size + entry_table_size, str_pool_buf, str_pool_aligned);
	// File data
	memcpy (out + 0x20 + header_data_size, file_data_buf, cur_file_offset);

	// Cleanup
	FREE (node_table);
	FREE (entry_table);
	FREE (str_pool_buf);
	FREE (file_data_buf);
	FREE (entry_data_offsets);
	for (uint i = 0; i < num_nodes; i++)
	{
		FREE (nodes[i].name);
		FREE (nodes[i].child_indices);
	}

	*dest = out;
	*dest_size = total_rarc_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Monster Games RST ("0TSR" / "RST0") archive & TOC ("0SERCOTE" / "ETOCRES0")
// Used in Excitebots: Trick Racing, Excite Truck, NASCAR Heat, etc.
// Supports both Little-Endian and Big-Endian, plus QuickLZ ("PMCr") compression.
//-----------------------------------------------------------------------------

enumError ExtractRST (nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *car_data,
	uint car_size, const u8 *toc_data, uint toc_size)
{
	if (!out_entries || !out_n_entries || !car_data || car_size < 0x40)
		return EINVAL;

	bool be = false;
	if (!memcmp (car_data, "RST0", 4))
		be = true;
	else if (!memcmp (car_data, "0TSR", 4))
		be = false;
	else
		return EINVAL;

	u32 (*r32) (const u8 *) = be ? rd_be32 : rd_le32;

	u32 files_count = r32 (car_data + 0x20);
	if (!files_count || files_count > 20000)
		return EINVAL;

	u32 data_offset = r32 (car_data + 0x18);
	if (!data_offset || data_offset >= car_size)
		data_offset = 0x80;

	const u8 *payload_bytes = car_data + data_offset;
	uint payload_len = car_size > data_offset ? car_size - data_offset : 0;

	u8 *decompressed_payload = 0;
	uint decompressed_len = 0;

	// Two-region virtual address space, matching the retail encoder's split
	// between compressible and (mostly incompressible, e.g. streamed audio
	// and some large textures) assets: TOC offsets < decompressed_len index
	// into the QuickLZ-decompressed block; offsets >= decompressed_len index
	// into the *raw*, uncompressed bytes that follow the QuickLZ stream
	// in-file (tail_bytes/tail_len below). Every real retail TOC has entries
	// in both halves -- treating this as a single flat payload silently
	// drops every entry whose offset lands in the tail (large streamed
	// textures, music/SFX), so both must be considered.
	const u8 *tail_bytes = 0;
	uint tail_len = 0;

	// Check for QuickLZ ("PMCr" / "rMCP" wrapper)
	if (payload_len >= 16
		&& (!memcmp (payload_bytes, "PMCr", 4) || !memcmp (payload_bytes, "rMCP", 4)))
	{
		u32 qlz_hdr_len = r32 (payload_bytes + 8);
		const u8 *qlz_stream = payload_bytes + 16;
		uint qlz_len
			= (qlz_hdr_len > 0 && qlz_hdr_len <= payload_len - 16) ? qlz_hdr_len : payload_len - 16;
		enumError qerr
			= DecodeQuickLZ (&decompressed_payload, &decompressed_len, qlz_stream, qlz_len);
		if (!qerr && decompressed_payload)
		{
			uint tail_off = 16 + qlz_len;
			if (payload_len > tail_off)
			{
				tail_bytes = payload_bytes + tail_off;
				tail_len = payload_len - tail_off;
			}
			payload_bytes = decompressed_payload;
			payload_len = decompressed_len;
		}
	}

	if (toc_data && toc_size >= 0x0C + (files_count + 1) * 0x28)
	{
		const bool compact_toc = rd_le32 (toc_data) == 3 && toc_size >= 0x20;
		const bool toc_be = !compact_toc && !memcmp (toc_data, "ETOCRES0", 8);
		u32 (*tr32) (const u8 *) = toc_be ? rd_be32 : rd_le32;
		const uint toc_count = compact_toc ? tr32 (toc_data + 0x0c) : files_count;
		const uint entries_off = compact_toc ? 0x20 : 0x0c;
		const uint names_off = entries_off + (compact_toc ? toc_count : toc_count + 1) * 0x28;
		if (names_off >= toc_size)
		{
			if (decompressed_payload)
				FREE (decompressed_payload);
			return EINVAL;
		}

		nintendo_sarc_entry_t *entries = CALLOC (toc_count, sizeof (nintendo_sarc_entry_t));
		uint count = 0;

		for (uint i = 0; i < toc_count; i++)
		{
			uint entry_off = entries_off + (compact_toc ? i : i + 1) * 0x28;
			u32 name_rel = tr32 (toc_data + entry_off);
			u32 fsize = tr32 (toc_data + entry_off + 0x0C);
			u32 foff = tr32 (toc_data + entry_off + 0x10);

			if (fsize > 0)
			{
				// Retail TOC offsets are relative to the uncompressed payload,
				// not to the 0x80-byte RST file header. Offsets past the
				// decompressed block continue into the raw tail region (see
				// the tail_bytes/tail_len comment above).
				uint src_off = foff;
				const u8 *src_base;
				uint max_avail;

				if (src_off < payload_len)
				{
					src_base = payload_bytes;
					max_avail = payload_len;
				}
				else
				{
					src_base = tail_bytes;
					max_avail = tail_len;
					src_off -= payload_len;
				}

				if (src_base && fsize <= max_avail && src_off <= max_avail - fsize)
				{
					uint n_pos = names_off + name_rel;
					if (n_pos < toc_size)
					{
						ccp name_ptr = (ccp)(toc_data + n_pos);
						entries[count].name = STRDUP (name_ptr);
						entries[count].size = fsize;
						entries[count].data = MALLOC (fsize);
						memcpy ((void *)entries[count].data, src_base + src_off, fsize);
						count++;
					}
				}
			}
		}

		if (decompressed_payload)
			FREE (decompressed_payload);

		*out_entries = entries;
		*out_n_entries = count;
		return ERR_OK;
	}

	// WiiWare games such as Excitebike World Rally store the usual
	// per-resource TOCs in a compressed `tocres.res` RST.  Its payload is a
	// compact directory: FILES_COUNT 0x28-byte records followed by names,
	// without the standalone 0SERCOTE header.
	if (!toc_data && payload_len >= files_count * 0x28)
	{
		const uint names_off = files_count * 0x28;
		nintendo_sarc_entry_t *entries = CALLOC (files_count, sizeof (*entries));
		uint count = 0;
		for (uint i = 0; i < files_count; i++)
		{
			const u8 *rec = payload_bytes + i * 0x28;
			const uint name_rel = r32 (rec);
			const uint fsize = r32 (rec + 0x0c);
			const uint foff = r32 (rec + 0x10);
			if (!fsize || foff > payload_len || fsize > payload_len - foff
				|| name_rel > payload_len - names_off)
				continue;
			ccp name = (ccp)payload_bytes + names_off + name_rel;
			if (!memchr (name, 0, payload_len - names_off - name_rel))
				continue;
			entries[count].name = STRDUP (name);
			entries[count].size = fsize;
			entries[count].data = MALLOC (fsize);
			memcpy ((void *)entries[count].data, payload_bytes + foff, fsize);
			count++;
		}
		if (decompressed_payload)
			FREE (decompressed_payload);
		if (count)
		{
			*out_entries = entries;
			*out_n_entries = count;
			return ERR_OK;
		}
		FREE (entries);
	}

	if (decompressed_payload)
		FREE (decompressed_payload);

	return EINVAL;
}

enumError CreateRST (u8 **dest_car, uint *dest_car_size, u8 **dest_toc, uint *dest_toc_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, bool compress, bool big_endian)
{
	if (!dest_car || !dest_car_size || !dest_toc || !dest_toc_size || !entries || !n_entries)
		return EINVAL;

	void (*w32) (u8 *, u32) = big_endian ? wr_be32 : wr_le32;

	// Build TOC String Table
	u8 *str_pool = MALLOC (65536);
	uint str_pool_cap = 65536;
	uint str_pool_len = 0;
	uint *name_offsets = CALLOC (n_entries, sizeof (uint));

	for (uint i = 0; i < n_entries; i++)
	{
		ccp fn = entries[i].name ? entries[i].name : "file";
		ccp slash = strrchr (fn, '/');
		if (slash)
			fn = slash + 1;

		size_t slen = strlen (fn) + 1;
		if (str_pool_len + slen > str_pool_cap)
		{
			str_pool_cap = (str_pool_cap + (uint)slen) * 2;
			str_pool = REALLOC (str_pool, str_pool_cap);
		}
		name_offsets[i] = str_pool_len;
		memcpy (str_pool + str_pool_len, fn, slen);
		str_pool_len += (uint)slen;
	}

	// Build uncompressed raw payload (starts logically at offset 0x80)
	uint uncompressed_payload_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		uncompressed_payload_size = (uncompressed_payload_size + 0x7F) & ~0x7Fu;
		uncompressed_payload_size += entries[i].size;
	}
	uncompressed_payload_size = (uncompressed_payload_size + 0x7F) & ~0x7Fu;

	u8 *raw_payload = CALLOC (1, uncompressed_payload_size);
	uint *file_offsets = CALLOC (n_entries, sizeof (uint));
	uint cur_off = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		cur_off = (cur_off + 0x7F) & ~0x7Fu;
		file_offsets[i] = cur_off; // retail TOC offsets are relative to the uncompressed payload
		if (entries[i].data && entries[i].size)
			memcpy (raw_payload + cur_off, entries[i].data, entries[i].size);
		cur_off += entries[i].size;
	}

	u8 *final_payload = 0;
	uint final_payload_size = 0;
	uint compressed_size = uncompressed_payload_size;

	if (compress)
	{
		u8 *qlz_buf = 0;
		uint qlz_size = 0;
		enumError qerr
			= EncodeQuickLZ (&qlz_buf, &qlz_size, raw_payload, uncompressed_payload_size);
		if (!qerr && qlz_buf)
		{
			uint pmcr_len = 16 + qlz_size;
			uint pmcr_aligned = (pmcr_len + 0x7F) & ~0x7Fu;
			final_payload = CALLOC (1, pmcr_aligned);
			if (big_endian)
			{
				memcpy (final_payload, "rMCP", 4);
				wr_be32 (final_payload + 4, 0x19397e49);
				wr_be32 (final_payload + 8, qlz_size);
				wr_be32 (final_payload + 12, uncompressed_payload_size);
			}
			else
			{
				memcpy (final_payload, "PMCr", 4);
				wr_le32 (final_payload + 4, 0x19397e49);
				wr_le32 (final_payload + 8, qlz_size);
				wr_le32 (final_payload + 12, uncompressed_payload_size);
			}
			memcpy (final_payload + 16, qlz_buf, qlz_size);
			final_payload_size = pmcr_aligned;
			compressed_size = qlz_size;
			FREE (qlz_buf);
		}
		else
		{
			final_payload = raw_payload;
			final_payload_size = uncompressed_payload_size;
			raw_payload = 0;
		}
	}
	else
	{
		final_payload = raw_payload;
		final_payload_size = uncompressed_payload_size;
		raw_payload = 0;
	}

	if (raw_payload)
		FREE (raw_payload);

	// Build CAR buffer (0x80 header + final_payload)
	uint total_car_size = 0x80 + final_payload_size;
	u8 *car_buf = CALLOC (1, total_car_size);

	if (big_endian)
		memcpy (car_buf, "RST0", 4);
	else
		memcpy (car_buf, "0TSR", 4);

	w32 (car_buf + 0x04, 0x40); // header size
	w32 (car_buf + 0x08, 0x0e); // version
	w32 (car_buf + 0x0c, 0x03);
	w32 (car_buf + 0x10, total_car_size);
	w32 (car_buf + 0x14, 0x19397e49);
	w32 (car_buf + 0x18, 0x80); // data offset
	w32 (car_buf + 0x1c, 0);
	w32 (car_buf + 0x20, n_entries);
	w32 (car_buf + 0x24, uncompressed_payload_size);
	w32 (car_buf + 0x28, compressed_size);
	w32 (car_buf + 0x2c, compress ? 0x80 : 0);
	w32 (car_buf + 0x30, 0);
	w32 (car_buf + 0x34, 0x140);
	w32 (car_buf + 0x38, 0);
	w32 (car_buf + 0x3c, 0);

	memcpy (car_buf + 0x80, final_payload, final_payload_size);
	FREE (final_payload);

	// Build TOC buffer
	uint toc_hdr_size = 0x0C;
	uint toc_entries_size = (n_entries + 1) * 0x28;
	uint total_toc_size = toc_hdr_size + toc_entries_size + str_pool_len;
	u8 *toc_buf = CALLOC (1, total_toc_size);

	if (big_endian)
	{
		memcpy (toc_buf, "ETOCRES0", 8);
		wr_be32 (toc_buf + 0x08, 3);
	}
	else
	{
		memcpy (toc_buf, "0SERCOTE", 8);
		wr_le32 (toc_buf + 0x08, 3);
	}

	// Entry 0 (meta record)
	u8 *e0 = toc_buf + 0x0C;
	w32 (e0 + 0x00, 0);
	memcpy (e0 + 0x04, "!IGM", 4);
	w32 (e0 + 0x08, 0);
	w32 (e0 + 0x0C, 32);
	w32 (e0 + 0x10, 0x19397e49);
	w32 (e0 + 0x14, 0);
	w32 (e0 + 0x18, uncompressed_payload_size);
	w32 (e0 + 0x1C, compressed_size);
	w32 (e0 + 0x20, compress ? 0x80 : 0);
	w32 (e0 + 0x24, 0x140);

	// Entries 1..N
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *ei = toc_buf + 0x0C + (i + 1) * 0x28;
		w32 (ei + 0x00, name_offsets[i]);

		ccp fn = entries[i].name ? entries[i].name : "file";
		ccp ext = strrchr (fn, '.');
		char typ[5] = " ATD";
		if (ext)
		{
			if (!strcasecmp (ext, ".tex") || !strcasecmp (ext, ".tm0"))
				memcpy (typ, " XET", 4);
			else if (!strcasecmp (ext, ".mod"))
				memcpy (typ, "LDOM", 4);
			else if (!strcasecmp (ext, ".val"))
				memcpy (typ, "TLAV", 4);
			else if (!strcasecmp (ext, ".can"))
				memcpy (typ, "nAhC", 4);
			else if (!strcasecmp (ext, ".lyt"))
				memcpy (typ, "TYAL", 4);
			else if (!strcasecmp (ext, ".fnt"))
				memcpy (typ, "STMP", 4);
		}
		memcpy (ei + 0x04, typ, 4);
		w32 (ei + 0x08, 0);
		w32 (ei + 0x0C, entries[i].size);
		w32 (ei + 0x10, file_offsets[i]);
		w32 (ei + 0x14, 0);
		w32 (ei + 0x18, 0);
		w32 (ei + 0x1C, 0);
		w32 (ei + 0x20, 0);
		w32 (ei + 0x24, 0);
	}

	memcpy (toc_buf + toc_hdr_size + toc_entries_size, str_pool, str_pool_len);

	FREE (str_pool);
	FREE (name_offsets);
	FREE (file_offsets);

	*dest_car = car_buf;
	*dest_car_size = total_car_size;
	*dest_toc = toc_buf;
	*dest_toc_size = total_toc_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// THP Video File ("THP\0") Frame & Audio Extraction
// Used across GameCube and Wii games (e.g. Super Smash Bros. Brawl, Mario Kart Wii, etc.)
//-----------------------------------------------------------------------------

enumError ExtractTHP (
	nintendo_sarc_entry_t **out_entries, uint *out_n_entries, const u8 *thp_data, uint thp_size)
{
	if (!out_entries || !out_n_entries || !thp_data || thp_size < 0x30)
		return EINVAL;

	if (memcmp (thp_data, "THP\0", 4))
		return EINVAL;

	u32 frame_count = rd_be32 (thp_data + 0x14);
	u32 comp_data_off = rd_be32 (thp_data + 0x20);
	u32 movie_data_off = rd_be32 (thp_data + 0x28);

	if (!frame_count || frame_count > 500000 || movie_data_off >= thp_size)
		return EINVAL;

	// Component table
	u32 num_comps = 0;
	if (comp_data_off + 4 <= thp_size)
		num_comps = rd_be32 (thp_data + comp_data_off);
	if (!num_comps || num_comps > 16)
		num_comps = 1;

	// Allocate entries (up to frame_count * num_comps)
	nintendo_sarc_entry_t *entries
		= CALLOC (frame_count * num_comps, sizeof (nintendo_sarc_entry_t));
	uint count = 0;

	u32 cur_off = movie_data_off;
	for (u32 f = 0; f < frame_count && cur_off + 8 + num_comps * 4 <= thp_size; f++)
	{
		u32 next_frame_size = rd_be32 (thp_data + cur_off);
		u32 prev_frame_size = rd_be32 (thp_data + cur_off + 4);
		(void)prev_frame_size;

		u32 comp_sizes[16] = { 0 };
		for (u32 c = 0; c < num_comps; c++)
			comp_sizes[c] = rd_be32 (thp_data + cur_off + 8 + c * 4);

		u32 comp_payload_off = cur_off + 8 + num_comps * 4;
		for (u32 c = 0; c < num_comps; c++)
		{
			u32 csz = comp_sizes[c];
			// Perform the bounds check in 64-bit: a corrupt/truncated THP can
			// carry a component size near 0xFFFFFFFF, and the 32-bit sum
			// comp_payload_off + csz would wrap around and pass the check,
			// making MALLOC/memcpy read far past thp_size (crash).
			if (csz > 0 && (uint64_t)comp_payload_off + csz <= (uint64_t)thp_size)
			{
				char name[64];
				if (c == 0)
					snprintf (name, sizeof (name), "frame_%05u.jpg", f);
				else
					snprintf (name, sizeof (name), "audio_%05u_%u.bin", f, c);

				entries[count].name = STRDUP (name);
				entries[count].size = csz;
				entries[count].data = MALLOC (csz);
				memcpy ((void *)entries[count].data, thp_data + comp_payload_off, csz);
				count++;
				comp_payload_off += csz;
			}
		}

		if (next_frame_size == 0)
			break;
		cur_off += next_frame_size;
	}

	*out_entries = entries;
	*out_n_entries = count;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// AT7 (AT7P / AT7X / AT7E) compression, used by Pokémon Mystery Dungeon WiiWare
// (Chunsoft). Chunk-based stream supporting compressed blocks (AT7P) and raw
// blocks (AT7X), terminated by AT7E.
//-----------------------------------------------------------------------------

enumError DecodeAT7 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4)
		return EINVAL;

	*dest = 0;
	*dest_size = 0;

	if (memcmp (src, "AT7P", 4) && memcmp (src, "AT7X", 4) && memcmp (src, "AT7E", 4))
		return EINVAL;

	uint cap = src_size < 0x8000 ? 0x10000 : src_size * 2;
	if (cap < 0x10000)
		cap = 0x10000;
	u8 *out = MALLOC (cap);
	if (!out)
		return ERR_CANT_CREATE;
	uint out_pos = 0;

	uint pos = 0;
	while (pos < src_size)
	{
		if (pos + 4 > src_size)
			break;
		if (!memcmp (src + pos, "AT7E", 4))
		{
			pos += 4;
			break;
		}

		if (!memcmp (src + pos, "AT7X", 4))
		{
			if (pos + 6 > src_size)
				goto invalid_at7;
			uint block_size = rd_le16 (src + pos + 4);
			if (block_size < 6 || pos + block_size > src_size)
				goto invalid_at7;
			uint raw_len = block_size - 6;
			if (out_pos + raw_len > NFMT_MAX_OUTPUT)
				goto invalid_at7;
			if (out_pos + raw_len > cap)
			{
				cap = (out_pos + raw_len) * 2 + 0x10000;
				u8 *nout = REALLOC (out, cap);
				if (!nout)
					goto invalid_at7;
				out = nout;
			}
			memcpy (out + out_pos, src + pos + 6, raw_len);
			out_pos += raw_len;
			pos += block_size;
			continue;
		}

		if (!memcmp (src + pos, "AT7P", 4))
		{
			if (pos + 6 > src_size)
				goto invalid_at7;
			uint block_size = rd_le16 (src + pos + 4);
			if (block_size < 6 || pos + block_size > src_size)
				goto invalid_at7;
			uint block_end = pos + block_size;
			uint bpos = pos + 6;

			while (bpos < block_end)
			{
				u8 flag = src[bpos++];
				if (flag == 0xFF && (block_end - bpos >= 8))
				{
					if (out_pos + 8 > cap)
					{
						cap = cap * 2 + 0x10000;
						if (cap > NFMT_MAX_OUTPUT)
							goto invalid_at7;
						u8 *nout = REALLOC (out, cap);
						if (!nout)
							goto invalid_at7;
						out = nout;
					}
					memcpy (out + out_pos, src + bpos, 8);
					bpos += 8;
					out_pos += 8;
				}
				else
				{
					for (int bit = 7; bit >= 0; bit--)
					{
						if (bpos >= block_end)
							break;
						if ((flag >> bit) & 1)
						{
							if (out_pos + 1 > cap)
							{
								cap = cap * 2 + 0x10000;
								if (cap > NFMT_MAX_OUTPUT)
									goto invalid_at7;
								u8 *nout = REALLOC (out, cap);
								if (!nout)
									goto invalid_at7;
								out = nout;
							}
							out[out_pos++] = src[bpos++];
						}
						else
						{
							if (bpos + 2 > block_end)
								goto invalid_at7;
							u8 b0 = src[bpos++];
							u8 b1 = src[bpos++];
							uint control = (b0 >> 4) & 0x0F;
							uint match_len = 3 + control;
							uint dist_away = ((b0 & 0x0F) << 8) | b1;
							uint backtrack = 0x1000 - dist_away;
							if (backtrack == 0 || backtrack > out_pos)
								goto invalid_at7;
							if (out_pos + match_len > cap)
							{
								cap = cap * 2 + 0x10000 + match_len;
								if (cap > NFMT_MAX_OUTPUT)
									goto invalid_at7;
								u8 *nout = REALLOC (out, cap);
								if (!nout)
									goto invalid_at7;
								out = nout;
							}
							for (uint i = 0; i < match_len; i++)
							{
								out[out_pos] = out[out_pos - backtrack];
								out_pos++;
							}
						}
					}
				}
			}
			pos = block_end;
			continue;
		}

		goto invalid_at7;
	}

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;

invalid_at7:
	FREE (out);
	return EINVAL;
}

enumError EncodeAT7 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src)
		return EINVAL;

	*dest = 0;
	*dest_size = 0;

	if (src_size == 0)
	{
		u8 *out = MALLOC (4);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, "AT7E", 4);
		*dest = out;
		*dest_size = 4;
		return ERR_OK;
	}

	uint out_cap = src_size + (src_size / 8) + (src_size / 0xC000 + 2) * 16 + 64;
	u8 *out = MALLOC (out_cap);
	if (!out)
		return ERR_CANT_CREATE;
	uint out_pos = 0;

	int head[65536];
	int *prev = MALLOC (0xC000 * sizeof (int));
	if (!prev)
	{
		FREE (out);
		return ERR_CANT_CREATE;
	}

	uint pos = 0;
	while (pos < src_size)
	{
		uint chunk_size = src_size - pos;
		if (chunk_size > 0xC000)
			chunk_size = 0xC000;
		uint chunk_end = pos + chunk_size;

		memset (head, -1, sizeof (head));
		memset (prev, -1, 0xC000 * sizeof (int));

		uint block_start = out_pos;
		out_pos += 6; // Reserve 4 bytes "AT7P" + 2 bytes block_size

		uint cpos = pos;
		while (cpos < chunk_end)
		{
			uint flag_pos = out_pos++;
			u8 group_flags = 0;

			for (int step = 0; step < 8; step++)
			{
				if (cpos >= chunk_end)
					break;

				uint best_len = 0;
				uint best_dist = 0;
				uint max_len = chunk_end - cpos;
				if (max_len > 18)
					max_len = 18;

				if (max_len >= 3)
				{
					uint h
						= ((uint)src[cpos] << 8) ^ ((uint)src[cpos + 1] << 4) ^ (uint)src[cpos + 2];
					h &= 0xFFFF;
					int mpos = head[h];
					int min_pos = (int)cpos - 0xFFF;
					if (min_pos < (int)pos)
						min_pos = (int)pos;
					int chain_limit = 64;

					while (mpos >= min_pos && chain_limit-- > 0)
					{
						if (src[mpos + best_len] == src[cpos + best_len]
							&& !memcmp (src + mpos, src + cpos, 3))
						{
							uint l = 3;
							while (l < max_len && src[mpos + l] == src[cpos + l])
								l++;
							if (l > best_len)
							{
								best_len = l;
								best_dist = cpos - mpos;
								if (best_len == max_len)
									break;
							}
						}
						int next_mpos = prev[mpos - pos];
						if (next_mpos >= mpos)
							break;
						mpos = next_mpos;
					}
				}

				if (best_len >= 3)
				{
					uint dist_away = 0x1000 - best_dist;
					uint ctrl = best_len - 3;
					u8 b0 = (ctrl << 4) | ((dist_away >> 8) & 0x0F);
					u8 b1 = dist_away & 0xFF;
					out[out_pos++] = b0;
					out[out_pos++] = b1;

					for (uint k = 0; k < best_len; k++)
					{
						if (cpos + k + 2 < chunk_end)
						{
							uint h = ((uint)src[cpos + k] << 8) ^ ((uint)src[cpos + k + 1] << 4)
								^ (uint)src[cpos + k + 2];
							h &= 0xFFFF;
							prev[cpos + k - pos] = head[h];
							head[h] = cpos + k;
						}
					}
					cpos += best_len;
				}
				else
				{
					group_flags |= (1 << (7 - step));
					out[out_pos++] = src[cpos];

					if (cpos + 2 < chunk_end)
					{
						uint h = ((uint)src[cpos] << 8) ^ ((uint)src[cpos + 1] << 4)
							^ (uint)src[cpos + 2];
						h &= 0xFFFF;
						prev[cpos - pos] = head[h];
						head[h] = cpos;
					}
					cpos++;
				}
			}
			out[flag_pos] = group_flags;
		}

		uint block_size = out_pos - block_start;
		if (block_size > 0xFFFF)
		{
			FREE (prev);
			FREE (out);
			return EFBIG;
		}
		memcpy (out + block_start, "AT7P", 4);
		out[block_start + 4] = block_size & 0xFF;
		out[block_start + 5] = (block_size >> 8) & 0xFF;

		pos = chunk_end;
	}

	FREE (prev);

	memcpy (out + out_pos, "AT7E", 4);
	out_pos += 4;

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
///////////////                   BYML / BYAML                  ///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct byml_ctx_t
{
	const u8 *data;
	size_t size;
	bool is_le;
	u16 version;
	const char **hash_keys;
	uint n_hash_keys;
	const char **strings;
	uint n_strings;
	u32 visited_stack[256];
	uint visited_depth;
} byml_ctx_t;

static bool byml_is_visited (const byml_ctx_t *ctx, u32 off)
{
	for (uint i = 0; i < ctx->visited_depth; i++)
		if (ctx->visited_stack[i] == off)
			return true;
	return false;
}

static inline u32 byml_u24 (const u8 *p, bool is_le)
{
	return is_le ? ((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16))
				 : (((u32)p[0] << 16) | ((u32)p[1] << 8) | (u32)p[2]);
}

static inline u32 byml_u32 (const u8 *p, bool is_le)
{
	return is_le ? rd_le32 (p) : rd_be32 (p);
}

static inline u16 byml_u16 (const u8 *p, bool is_le)
{
	return is_le ? rd_le16 (p) : rd_be16 (p);
}

static inline u64 byml_u64 (const u8 *p, bool is_le)
{
	if (is_le)
		return (u64)rd_le32 (p) | ((u64)rd_le32 (p + 4) << 32);
	else
		return ((u64)rd_be32 (p) << 32) | (u64)rd_be32 (p + 4);
}

static bool is_valid_utf8 (const char *s)
{
	const u8 *p = (const u8 *)s;
	while (*p)
	{
		if (*p < 0x80)
		{
			p++;
		}
		else if ((*p & 0xE0) == 0xC0)
		{
			if ((p[1] & 0xC0) != 0x80)
				return false;
			p += 2;
		}
		else if ((*p & 0xF0) == 0xE0)
		{
			if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
				return false;
			p += 3;
		}
		else if ((*p & 0xF8) == 0xF0)
		{
			if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
				return false;
			p += 4;
		}
		else
			return false;
	}
	return true;
}

static void yaml_print_string (FILE *out, const char *s)
{
	if (!s || !*s)
	{
		fprintf (out, "\"\"");
		return;
	}

	bool valid_u8 = is_valid_utf8 (s);
	bool need_quote = !valid_u8;
	if (!need_quote)
	{
		if (s[0] == ' ' || s[strlen (s) - 1] == ' ' || s[0] == '-' || s[0] == '?' || s[0] == ':'
			|| s[0] == '%' || s[0] == '@' || s[0] == '`' || s[0] == '&' || s[0] == '*'
			|| s[0] == '!' || s[0] == '|' || s[0] == '>' || s[0] == '\'' || s[0] == '"'
			|| s[0] == '#' || s[0] == '[' || s[0] == ']' || s[0] == '{' || s[0] == '}')
			need_quote = true;
		else if (!strcmp (s, "true") || !strcmp (s, "false") || !strcmp (s, "null")
			|| !strcmp (s, "yes") || !strcmp (s, "no") || !strcmp (s, "on") || !strcmp (s, "off")
			|| !strcmp (s, "~"))
			need_quote = true;
		else
		{
			for (const char *p = s; *p; p++)
			{
				if (*p == ':' && (*(p + 1) == ' ' || *(p + 1) == '\0'))
				{
					need_quote = true;
					break;
				}
				if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\t' || *p == '"' || *p == '\\')
				{
					need_quote = true;
					break;
				}
			}
		}

		if (!need_quote)
		{
			char *endp = 0;
			strtod (s, &endp);
			if (endp && *endp == '\0' && endp != s)
				need_quote = true;
		}
	}

	if (need_quote)
	{
		fputc ('"', out);
		for (const u8 *p = (const u8 *)s; *p; p++)
		{
			if (*p == '"')
				fprintf (out, "\\\"");
			else if (*p == '\\')
				fprintf (out, "\\\\");
			else if (*p == '\n')
				fprintf (out, "\\n");
			else if (*p == '\r')
				fprintf (out, "\\r");
			else if (*p == '\t')
				fprintf (out, "\\t");
			else if (*p < 0x20 || (!valid_u8 && *p >= 0x80))
				fprintf (out, "\\x%02x", *p);
			else
				fputc (*p, out);
		}
		fputc ('"', out);
	}
	else
	{
		fprintf (out, "%s", s);
	}
}

static enumError byml_parse_str_table (
	byml_ctx_t *ctx, u32 off, const char ***table_out, uint *count_out)
{
	*table_out = 0;
	*count_out = 0;
	if (!off)
		return ERR_OK;
	if (off + 4 > ctx->size)
		return ERR_INVALID_DATA;
	const u8 *p = ctx->data + off;
	if (p[0] != 0xC2)
		return ERR_INVALID_DATA;
	uint count = byml_u24 (p + 1, ctx->is_le);
	if (!count)
		return ERR_OK;
	if (off + 4 + (count + 1) * 4 > ctx->size)
		return ERR_INVALID_DATA;

	const char **table = CALLOC (count, sizeof (char *));
	for (uint i = 0; i < count; i++)
	{
		u32 st_off = byml_u32 (p + 4 + i * 4, ctx->is_le);
		if (off + st_off >= ctx->size)
			continue;
		table[i] = (const char *)(ctx->data + off + st_off);
	}
	*table_out = table;
	*count_out = count;
	return ERR_OK;
}

static enumError byml_print_node (
	FILE *out, byml_ctx_t *ctx, u8 type, u32 val, int indent, int depth)
{
	if (depth > 128)
		return EFBIG;

	switch (type)
	{
		case 0xA0:
		case 0x20:
		{
			if (val < ctx->n_strings && ctx->strings[val])
				yaml_print_string (out, ctx->strings[val]);
			else
				fprintf (out, "\"\"");
			break;
		}
		case 0xA1:
		case 0x21:
		{
			fprintf (out, "\"<blob_idx_%u>\"", val);
			break;
		}
		case 0xD0:
		{
			fprintf (out, "%s", val ? "true" : "false");
			break;
		}
		case 0xD1:
		{
			int32_t sval = (int32_t)val;
			fprintf (out, "%d", sval);
			break;
		}
		case 0xD2:
		{
			fprintf (out, "%u", val);
			break;
		}
		case 0xD3:
		{
			float fval;
			memcpy (&fval, &val, 4);
			if (isnan (fval))
				fprintf (out, ".nan");
			else if (isinf (fval))
				fprintf (out, "%s.inf", fval < 0 ? "-" : "");
			else
			{
				char buf[64];
				snprintf (buf, sizeof (buf), "%.8g", fval);
				if (!strchr (buf, '.') && !strchr (buf, 'e') && !strchr (buf, 'E'))
					strcat (buf, ".0");
				fprintf (out, "%s", buf);
			}
			break;
		}
		case 0xD4:
		{
			if (val + 8 <= ctx->size)
			{
				long long sval = (long long)byml_u64 (ctx->data + val, ctx->is_le);
				fprintf (out, "%lld", sval);
			}
			else
				fprintf (out, "0");
			break;
		}
		case 0xD5:
		{
			if (val + 8 <= ctx->size)
			{
				unsigned long long uval
					= (unsigned long long)byml_u64 (ctx->data + val, ctx->is_le);
				fprintf (out, "%llu", uval);
			}
			else
				fprintf (out, "0");
			break;
		}
		case 0xD6:
		{
			if (val + 8 <= ctx->size)
			{
				double dval;
				u64 uv = byml_u64 (ctx->data + val, ctx->is_le);
				memcpy (&dval, &uv, 8);
				if (isnan (dval))
					fprintf (out, ".nan");
				else if (isinf (dval))
					fprintf (out, "%s.inf", dval < 0 ? "-" : "");
				else
				{
					char buf[64];
					snprintf (buf, sizeof (buf), "%.16g", dval);
					if (!strchr (buf, '.') && !strchr (buf, 'e') && !strchr (buf, 'E'))
						strcat (buf, ".0");
					fprintf (out, "%s", buf);
				}
			}
			else
				fprintf (out, "0.0");
			break;
		}
		case 0xFF:
		{
			fprintf (out, "null");
			break;
		}
		case 0xC0: // Array
		{
			u32 off = val;
			if (byml_is_visited (ctx, off))
			{
				fprintf (out, "*array_0x%x", off);
				break;
			}
			if (off + 4 > ctx->size)
			{
				fprintf (out, "[]");
				break;
			}
			const u8 *p = ctx->data + off;
			if (p[0] != 0xC0)
			{
				fprintf (out, "[]");
				break;
			}
			uint count = byml_u24 (p + 1, ctx->is_le);
			if (!count)
			{
				fprintf (out, "[]");
				break;
			}
			if (off + 4 + count > ctx->size)
			{
				fprintf (out, "[]");
				break;
			}

			const u8 *tags = p + 4;
			u32 val_start = off + 4 + ((count + 3) & ~3);
			if (val_start + count * 4 > ctx->size)
			{
				fprintf (out, "[]");
				break;
			}

			if (ctx->visited_depth < 256)
				((byml_ctx_t *)ctx)->visited_stack[((byml_ctx_t *)ctx)->visited_depth++] = off;

			for (uint i = 0; i < count; i++)
			{
				u8 elem_tag = tags[i];
				u32 elem_val = byml_u32 (ctx->data + val_start + i * 4, ctx->is_le);

				if (i > 0 || indent > 0)
				{
					for (int s = 0; s < indent; s++)
						fputc (' ', out);
				}
				fprintf (out, "- ");

				if (elem_tag == 0xC1)
				{
					u32 d_off = elem_val;
					if (byml_is_visited (ctx, d_off))
					{
						fprintf (out, "*dict_0x%x\n", d_off);
					}
					else if (d_off + 4 <= ctx->size && ctx->data[d_off] == 0xC1)
					{
						uint d_count = byml_u24 (ctx->data + d_off + 1, ctx->is_le);
						if (!d_count)
						{
							fprintf (out, "{}\n");
						}
						else
						{
							fprintf (out, "\n");
							byml_print_node (out, ctx, elem_tag, elem_val, indent + 2, depth + 1);
						}
					}
					else
						fprintf (out, "{}\n");
				}
				else if (elem_tag == 0xC0)
				{
					if (byml_is_visited (ctx, elem_val))
					{
						fprintf (out, "*array_0x%x\n", elem_val);
					}
					else
					{
						fprintf (out, "\n");
						byml_print_node (out, ctx, elem_tag, elem_val, indent + 2, depth + 1);
					}
				}
				else
				{
					byml_print_node (out, ctx, elem_tag, elem_val, indent + 2, depth + 1);
					fputc ('\n', out);
				}
			}

			if (ctx->visited_depth > 0 && ctx->visited_stack[ctx->visited_depth - 1] == off)
				((byml_ctx_t *)ctx)->visited_depth--;
			break;
		}
		case 0xC1: // Dictionary
		{
			u32 off = val;
			if (byml_is_visited (ctx, off))
			{
				fprintf (out, "*dict_0x%x", off);
				break;
			}
			if (off + 4 > ctx->size)
			{
				fprintf (out, "{}");
				break;
			}
			const u8 *p = ctx->data + off;
			if (p[0] != 0xC1)
			{
				fprintf (out, "{}");
				break;
			}
			uint count = byml_u24 (p + 1, ctx->is_le);
			if (!count)
			{
				fprintf (out, "{}");
				break;
			}
			if (off + 4 + count * 8 > ctx->size)
			{
				fprintf (out, "{}");
				break;
			}

			if (ctx->visited_depth < 256)
				((byml_ctx_t *)ctx)->visited_stack[((byml_ctx_t *)ctx)->visited_depth++] = off;

			for (uint i = 0; i < count; i++)
			{
				const u8 *entry = p + 4 + i * 8;
				uint key_idx = byml_u24 (entry, ctx->is_le);
				u8 val_type = entry[3];
				u32 child_val = byml_u32 (entry + 4, ctx->is_le);

				for (int s = 0; s < indent; s++)
					fputc (' ', out);
				if (key_idx < ctx->n_hash_keys && ctx->hash_keys[key_idx])
					yaml_print_string (out, ctx->hash_keys[key_idx]);
				else
					fprintf (out, "key_%u", key_idx);
				fprintf (out, ":");

				if (val_type == 0xC0 || val_type == 0xC1)
				{
					if (byml_is_visited (ctx, child_val))
					{
						fprintf (out, " *%s_0x%x\n", val_type == 0xC0 ? "array" : "dict", child_val);
					}
					else
					{
						bool is_empty = false;
						if (child_val + 4 <= ctx->size)
						{
							uint c_cnt = byml_u24 (ctx->data + child_val + 1, ctx->is_le);
							if (!c_cnt)
								is_empty = true;
						}
						if (is_empty)
						{
							fprintf (out, " %s\n", val_type == 0xC0 ? "[]" : "{}");
						}
						else
						{
							fprintf (out, "\n");
							byml_print_node (out, ctx, val_type, child_val, indent + 2, depth + 1);
						}
					}
				}
				else
				{
					fprintf (out, " ");
					byml_print_node (out, ctx, val_type, child_val, indent + 2, depth + 1);
					fputc ('\n', out);
				}
			}

			if (ctx->visited_depth > 0 && ctx->visited_stack[ctx->visited_depth - 1] == off)
				((byml_ctx_t *)ctx)->visited_depth--;
			break;
		}
		default:
			fprintf (out, "\"<unknown_0x%02x_%u>\"", type, val);
			break;
	}
	return ERR_OK;
}

enumError DecodeBYML_YAML (FILE *out, const u8 *data, size_t size)
{
	if (!out || !data || size < 16)
		return ERR_INVALID_DATA;
	bool is_le = false;
	if (!memcmp (data, "YB", 2))
		is_le = true;
	else if (!memcmp (data, "BY", 2))
		is_le = false;
	else
		return ERR_INVALID_DATA;

	u16 version = byml_u16 (data + 2, is_le);
	if (version < 1 || version > 4)
		return ERR_INVALID_DATA;

	u32 hash_key_table_off = byml_u32 (data + 4, is_le);
	u32 str_table_off = byml_u32 (data + 8, is_le);
	u32 root_node_off = byml_u32 (data + 12, is_le);

	byml_ctx_t ctx = { 0 };
	ctx.data = data;
	ctx.size = size;
	ctx.is_le = is_le;
	ctx.version = version;

	enumError err
		= byml_parse_str_table (&ctx, hash_key_table_off, &ctx.hash_keys, &ctx.n_hash_keys);
	if (err)
		return err;
	err = byml_parse_str_table (&ctx, str_table_off, &ctx.strings, &ctx.n_strings);
	if (err)
	{
		FREE (ctx.hash_keys);
		return err;
	}

	if (root_node_off < size)
	{
		u8 root_tag = data[root_node_off];
		err = byml_print_node (out, &ctx, root_tag, root_node_off, 0, 0);
	}
	else
	{
		fprintf (out, "{}\n");
	}

	FREE (ctx.hash_keys);
	FREE (ctx.strings);
	return err;
}

typedef struct str_list_t
{
	char **items;
	uint count;
	uint cap;
} str_list_t;

static void str_list_init (str_list_t *l)
{
	l->items = 0;
	l->count = 0;
	l->cap = 0;
}

static void str_list_free (str_list_t *l)
{
	if (l->items)
	{
		for (uint i = 0; i < l->count; i++)
			FREE (l->items[i]);
		FREE (l->items);
	}
	memset (l, 0, sizeof (*l));
}

static int str_list_find (const str_list_t *l, const char *s)
{
	for (uint i = 0; i < l->count; i++)
		if (!strcmp (l->items[i], s))
			return (int)i;
	return -1;
}

static int str_list_add (str_list_t *l, const char *s)
{
	int idx = str_list_find (l, s);
	if (idx >= 0)
		return idx;
	if (l->count >= l->cap)
	{
		l->cap = l->cap ? l->cap * 2 : 16;
		l->items = REALLOC (l->items, l->cap * sizeof (char *));
	}
	l->items[l->count] = STRDUP (s);
	return (int)l->count++;
}

static int str_cmp_qsort (const void *a, const void *b)
{
	const char *const *sa = a;
	const char *const *sb = b;
	return strcmp (*sa, *sb);
}

static void collect_byml_symbols (const bf_val_t *val, str_list_t *keys, str_list_t *strs)
{
	if (!val)
		return;
	if (val->type == BF_T_NODE && val->u.node)
	{
		const bf_node_t *node = val->u.node;
		for (uint i = 0; i < node->n; i++)
		{
			if (node->kv[i].key)
				str_list_add (keys, node->kv[i].key);
			collect_byml_symbols (&node->kv[i].val, keys, strs);
		}
	}
	else if (val->type == BF_T_LIST && val->u.list)
	{
		const bf_list_t *list = val->u.list;
		for (uint i = 0; i < list->n; i++)
			collect_byml_symbols (&list->items[i], keys, strs);
	}
	else if (val->type == BF_T_STR && val->u.s)
	{
		str_list_add (strs, val->u.s);
	}
}

typedef struct byml_writer_t
{
	u8 *buf;
	uint len;
	uint cap;
	bool is_le;
} byml_writer_t;

static void bw_init (byml_writer_t *w, bool is_le)
{
	w->cap = 1024;
	w->buf = CALLOC (1, w->cap);
	w->len = 0;
	w->is_le = is_le;
}

static void bw_align (byml_writer_t *w, uint alignment)
{
	uint rem = w->len % alignment;
	if (rem)
	{
		uint pad = alignment - rem;
		while (w->len + pad > w->cap)
		{
			w->cap *= 2;
			w->buf = REALLOC (w->buf, w->cap);
		}
		memset (w->buf + w->len, 0, pad);
		w->len += pad;
	}
}

static void bw_append (byml_writer_t *w, const void *data, uint size)
{
	while (w->len + size > w->cap)
	{
		w->cap *= 2;
		w->buf = REALLOC (w->buf, w->cap);
	}
	if (data)
		memcpy (w->buf + w->len, data, size);
	else
		memset (w->buf + w->len, 0, size);
	w->len += size;
}

static void bw_u8 (byml_writer_t *w, u8 v)
{
	bw_append (w, &v, 1);
}

static void bw_u24 (byml_writer_t *w, u32 v)
{
	u8 b[4];
	if (w->is_le)
	{
		wr_le32 (b, v);
		bw_append (w, b, 3);
	}
	else
	{
		wr_be32 (b, v);
		bw_append (w, b + 1, 3);
	}
}

static void bw_put_u32 (byml_writer_t *w, uint pos, u32 v)
{
	if (pos + 4 <= w->len)
	{
		if (w->is_le)
			wr_le32 (w->buf + pos, v);
		else
			wr_be32 (w->buf + pos, v);
	}
}

static uint write_byml_str_table (byml_writer_t *w, const str_list_t *list)
{
	if (!list || !list->count)
		return 0;
	bw_align (w, 4);
	uint start = w->len;
	bw_u8 (w, 0xC2);
	bw_u24 (w, list->count);
	uint offsets_pos = w->len;
	bw_append (w, 0, (list->count + 1) * 4);

	for (uint i = 0; i < list->count; i++)
	{
		uint str_off = w->len - start;
		bw_put_u32 (w, offsets_pos + i * 4, str_off);
		const char *s = list->items[i];
		bw_append (w, s, (uint)strlen (s) + 1);
	}
	bw_put_u32 (w, offsets_pos + list->count * 4, w->len - start);
	return start;
}

typedef struct kv_sort_entry_t
{
	uint key_idx;
	uint orig_idx;
} kv_sort_entry_t;

static int kv_sort_cmp (const void *a, const void *b)
{
	const kv_sort_entry_t *ea = a;
	const kv_sort_entry_t *eb = b;
	return (ea->key_idx > eb->key_idx) - (ea->key_idx < eb->key_idx);
}

static uint write_byml_node (
	byml_writer_t *w, const bf_val_t *val, const str_list_t *keys, const str_list_t *strs)
{
	bw_align (w, 4);
	uint start = w->len;

	if (val->type == BF_T_NODE && val->u.node)
	{
		const bf_node_t *node = val->u.node;
		bw_u8 (w, 0xC1);
		bw_u24 (w, node->n);

		kv_sort_entry_t *sort_tab = CALLOC (node->n, sizeof (kv_sort_entry_t));
		for (uint i = 0; i < node->n; i++)
		{
			sort_tab[i].orig_idx = i;
			sort_tab[i].key_idx
				= (uint)str_list_find (keys, node->kv[i].key ? node->kv[i].key : "");
		}
		if (node->n > 1)
			qsort (sort_tab, node->n, sizeof (kv_sort_entry_t), kv_sort_cmp);

		uint entries_pos = w->len;
		bw_append (w, 0, node->n * 8);

		for (uint i = 0; i < node->n; i++)
		{
			uint oi = sort_tab[i].orig_idx;
			uint ki = sort_tab[i].key_idx;
			const bf_val_t *child = &node->kv[oi].val;
			u8 vtype = 0xFF;
			u32 vval = 0;

			if (child->type == BF_T_NODE)
			{
				vtype = 0xC1;
				vval = write_byml_node (w, child, keys, strs);
			}
			else if (child->type == BF_T_LIST)
			{
				vtype = 0xC0;
				vval = write_byml_node (w, child, keys, strs);
			}
			else if (child->type == BF_T_STR)
			{
				vtype = 0xA0;
				vval = (u32)str_list_find (strs, child->u.s ? child->u.s : "");
			}
			else if (child->type == BF_T_BOOL)
			{
				vtype = 0xD0;
				vval = child->u.b ? 1 : 0;
			}
			else if (child->type == BF_T_INT)
			{
				vtype = 0xD1;
				vval = (u32)child->u.i;
			}
			else if (child->type == BF_T_FLOAT)
			{
				vtype = 0xD3;
				float f = (float)child->u.f;
				memcpy (&vval, &f, 4);
			}

			uint epos = entries_pos + i * 8;
			u8 b[4];
			if (w->is_le)
			{
				wr_le32 (b, ki);
				w->buf[epos] = b[0];
				w->buf[epos + 1] = b[1];
				w->buf[epos + 2] = b[2];
				w->buf[epos + 3] = vtype;
				wr_le32 (w->buf + epos + 4, vval);
			}
			else
			{
				wr_be32 (b, ki);
				w->buf[epos] = b[1];
				w->buf[epos + 1] = b[2];
				w->buf[epos + 2] = b[3];
				w->buf[epos + 3] = vtype;
				wr_be32 (w->buf + epos + 4, vval);
			}
		}
		FREE (sort_tab);
		return start;
	}
	else if (val->type == BF_T_LIST && val->u.list)
	{
		const bf_list_t *list = val->u.list;
		bw_u8 (w, 0xC0);
		bw_u24 (w, list->n);
		uint tags_pos = w->len;
		bw_append (w, 0, list->n);
		bw_align (w, 4);
		uint vals_pos = w->len;
		bw_append (w, 0, list->n * 4);

		for (uint i = 0; i < list->n; i++)
		{
			const bf_val_t *child = &list->items[i];
			u8 vtype = 0xFF;
			u32 vval = 0;

			if (child->type == BF_T_NODE)
			{
				vtype = 0xC1;
				vval = write_byml_node (w, child, keys, strs);
			}
			else if (child->type == BF_T_LIST)
			{
				vtype = 0xC0;
				vval = write_byml_node (w, child, keys, strs);
			}
			else if (child->type == BF_T_STR)
			{
				vtype = 0xA0;
				vval = (u32)str_list_find (strs, child->u.s ? child->u.s : "");
			}
			else if (child->type == BF_T_BOOL)
			{
				vtype = 0xD0;
				vval = child->u.b ? 1 : 0;
			}
			else if (child->type == BF_T_INT)
			{
				vtype = 0xD1;
				vval = (u32)child->u.i;
			}
			else if (child->type == BF_T_FLOAT)
			{
				vtype = 0xD3;
				float f = (float)child->u.f;
				memcpy (&vval, &f, 4);
			}

			w->buf[tags_pos + i] = vtype;
			bw_put_u32 (w, vals_pos + i * 4, vval);
		}
		return start;
	}
	return start;
}

static void yaml_eval_scalar (const char *s, bf_val_t *val)
{
	memset (val, 0, sizeof (*val));
	if (!s || !*s)
	{
		val->type = BF_T_NONE;
		return;
	}
	if (!strcmp (s, "true") || !strcmp (s, "True") || !strcmp (s, "TRUE"))
	{
		val->type = BF_T_BOOL;
		val->u.b = true;
		return;
	}
	if (!strcmp (s, "false") || !strcmp (s, "False") || !strcmp (s, "FALSE"))
	{
		val->type = BF_T_BOOL;
		val->u.b = false;
		return;
	}
	if (!strcmp (s, "null") || !strcmp (s, "~") || !strcmp (s, "None"))
	{
		val->type = BF_T_NONE;
		return;
	}
	if (!strcmp (s, ".nan") || !strcmp (s, "nan") || !strcmp (s, "NaN"))
	{
		val->type = BF_T_FLOAT;
		val->u.f = 0.0f / 0.0f;
		return;
	}
	if (!strcmp (s, ".inf") || !strcmp (s, "inf") || !strcmp (s, "Infinity"))
	{
		val->type = BF_T_FLOAT;
		val->u.f = 1.0f / 0.0f;
		return;
	}
	if (!strcmp (s, "-.inf") || !strcmp (s, "-inf") || !strcmp (s, "-Infinity"))
	{
		val->type = BF_T_FLOAT;
		val->u.f = -1.0f / 0.0f;
		return;
	}
	if ((s[0] == '"' && s[strlen (s) - 1] == '"') || (s[0] == '\'' && s[strlen (s) - 1] == '\''))
	{
		uint len = (uint)strlen (s);
		char *str = CALLOC (1, len);
		uint out_pos = 0;
		for (uint i = 1; i < len - 1; i++)
		{
			if (s[i] == '\\' && i + 1 < len - 1)
			{
				i++;
				if (s[i] == 'n')
					str[out_pos++] = '\n';
				else if (s[i] == 'r')
					str[out_pos++] = '\r';
				else if (s[i] == 't')
					str[out_pos++] = '\t';
				else if (s[i] == '\\')
					str[out_pos++] = '\\';
				else if (s[i] == '"')
					str[out_pos++] = '"';
				else if (s[i] == '\'')
					str[out_pos++] = '\'';
				else
					str[out_pos++] = s[i];
			}
			else
				str[out_pos++] = s[i];
		}
		val->type = BF_T_STR;
		val->u.s = str;
		return;
	}
	char *endp = 0;
	long long lval = strtoll (s, &endp, 0);
	if (endp && !*endp)
	{
		val->type = BF_T_INT;
		val->u.i = (int)lval;
		return;
	}
	double dval = strtod (s, &endp);
	if (endp && !*endp)
	{
		val->type = BF_T_FLOAT;
		val->u.f = dval;
		return;
	}
	val->type = BF_T_STR;
	val->u.s = STRDUP (s);
}

#include <yaml.h>

static enumError fill_bf_node_from_yaml(yaml_document_t *doc, int node_id, bf_node_t *out_dict);
static enumError fill_bf_list_from_yaml(yaml_document_t *doc, int node_id, bf_list_t *out_list);

static enumError fill_bf_list_from_yaml(yaml_document_t *doc, int node_id, bf_list_t *out_list) {
    yaml_node_t *node = yaml_document_get_node(doc, node_id);
    if (!node || node->type != YAML_SEQUENCE_NODE) return ERR_SEMANTIC;

    for (yaml_node_item_t *i = node->data.sequence.items.start; i < node->data.sequence.items.top; i++) {
        yaml_node_t *item_node = yaml_document_get_node(doc, *i);
        if (!item_node) continue;
        
        if (item_node->type == YAML_SCALAR_NODE) {
            bf_val_t sval;
            yaml_eval_scalar((const char *)item_node->data.scalar.value, &sval);
            if (sval.type == BF_T_STR) { BFListAddStr(out_list, sval.u.s); FREE(sval.u.s); }
            else if (sval.type == BF_T_INT) BFListAddInt(out_list, sval.u.i);
            else if (sval.type == BF_T_FLOAT) BFListAddFloat(out_list, sval.u.f);
            else if (sval.type == BF_T_BOOL) BFListAddBool(out_list, sval.u.b);
        }
        else if (item_node->type == YAML_SEQUENCE_NODE) {
            bf_list_t *cl = BFListAddList(out_list);
            if (!cl) return ERR_OUT_OF_MEMORY;
            enumError err = fill_bf_list_from_yaml(doc, *i, cl);
            if (err) return err;
        }
        else if (item_node->type == YAML_MAPPING_NODE) {
            bf_node_t *cn = BFListAddNode(out_list);
            if (!cn) return ERR_OUT_OF_MEMORY;
            enumError err = fill_bf_node_from_yaml(doc, *i, cn);
            if (err) return err;
        }
    }
    return ERR_OK;
}

static enumError fill_bf_node_from_yaml(yaml_document_t *doc, int node_id, bf_node_t *out_dict) {
    yaml_node_t *node = yaml_document_get_node(doc, node_id);
    if (!node || node->type != YAML_MAPPING_NODE) return ERR_SEMANTIC;

    for (yaml_node_pair_t *p = node->data.mapping.pairs.start; p < node->data.mapping.pairs.top; p++) {
        yaml_node_t *key_node = yaml_document_get_node(doc, p->key);
        yaml_node_t *val_node = yaml_document_get_node(doc, p->value);
        if (!key_node || key_node->type != YAML_SCALAR_NODE || !val_node) continue;
        const char *key_str = (const char *)key_node->data.scalar.value;

        if (val_node->type == YAML_SCALAR_NODE) {
            bf_val_t sval;
            yaml_eval_scalar((const char *)val_node->data.scalar.value, &sval);
            if (sval.type == BF_T_STR) { BFNodeSetStr(out_dict, key_str, sval.u.s); FREE(sval.u.s); }
            else if (sval.type == BF_T_INT) BFNodeSetInt(out_dict, key_str, sval.u.i);
            else if (sval.type == BF_T_FLOAT) BFNodeSetFloat(out_dict, key_str, sval.u.f);
            else if (sval.type == BF_T_BOOL) BFNodeSetBool(out_dict, key_str, sval.u.b);
            else if (sval.type == BF_T_NONE) BFNodeSetNone(out_dict, key_str);
        }
        else if (val_node->type == YAML_SEQUENCE_NODE) {
            bf_list_t *cl = BFNodeSetList(out_dict, key_str);
            if (!cl) return ERR_OUT_OF_MEMORY;
            enumError err = fill_bf_list_from_yaml(doc, p->value, cl);
            if (err) return err;
        }
        else if (val_node->type == YAML_MAPPING_NODE) {
            bf_node_t *cn = BFNodeSetNode(out_dict, key_str);
            if (!cn) return ERR_OUT_OF_MEMORY;
            enumError err = fill_bf_node_from_yaml(doc, p->value, cn);
            if (err) return err;
        }
    }
    return ERR_OK;
}

enumError EncodeBYML_Text ( u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le, u16 version )
{
    if (!dest || !dest_size || !text)
        return ERR_SEMANTIC;
    *dest = 0; *dest_size = 0;

    yaml_parser_t parser;
    yaml_document_t document;
    if (!yaml_parser_initialize(&parser)) return ERR_OUT_OF_MEMORY;
    yaml_parser_set_input_string(&parser, (const unsigned char *)text, text_len);
    if (!yaml_parser_load(&parser, &document)) {
        yaml_parser_delete(&parser);
        return ERR_SEMANTIC;
    }

    bf_node_t root;
    BFNodeInit(&root);

    yaml_node_t *root_node = yaml_document_get_root_node(&document);
    if (root_node) {
        if (root_node->type == YAML_MAPPING_NODE) {
            fill_bf_node_from_yaml(&document, yaml_document_get_root_node(&document) - document.nodes.start + 1, &root);
        } else {
            // Not a mapping at root, technically BYML requires mapping at root but let's ignore or error.
            // (If BYML accepts lists at root, we'd have to restructure. Assuming dict at root here.)
        }
    }

    yaml_document_delete(&document);
    yaml_parser_delete(&parser);

    str_list_t keys, strs;
    str_list_init(&keys);
    str_list_init(&strs);

    bf_val_t root_val;
    root_val.type = BF_T_NODE;
    root_val.u.node = &root;
    collect_byml_symbols(&root_val, &keys, &strs);

    if (keys.count > 1)
        qsort(keys.items, keys.count, sizeof(char*), str_cmp_qsort);

    byml_writer_t w;
    bw_init(&w, is_le);
    // Header placeholder: 16 bytes
    bw_append(&w, 0, 16);

    uint hash_key_off = write_byml_str_table(&w, &keys);
    uint str_table_off = write_byml_str_table(&w, &strs);
    uint root_off = write_byml_node(&w, &root_val, &keys, &strs);

    // Write header
    w.buf[0] = is_le ? 'Y' : 'B';
    w.buf[1] = is_le ? 'B' : 'Y';
    if (is_le)
    {
        wr_le16(w.buf + 2, version ? version : 1);
        wr_le32(w.buf + 4, hash_key_off);
        wr_le32(w.buf + 8, str_table_off);
        wr_le32(w.buf + 12, root_off);
    }
    else
    {
        wr_be16(w.buf + 2, version ? version : 1);
        wr_be32(w.buf + 4, hash_key_off);
        wr_be32(w.buf + 8, str_table_off);
        wr_be32(w.buf + 12, root_off);
    }

    str_list_free(&keys);
    str_list_free(&strs);
    BFNodeFree(&root);

    *dest = w.buf;
    *dest_size = w.len;
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
///////////////                      NARC                       ///////////////
///////////////////////////////////////////////////////////////////////////////

void ResetNARC (narc_t *narc)
{
	if (narc)
	{
		if (narc->entries)
		{
			for (uint i = 0; i < narc->n_entries; i++)
				FREE (narc->entries[i].name);
			FREE (narc->entries);
		}
		memset (narc, 0, sizeof (*narc));
	}
}

enumError ScanNARC (narc_t *narc, const u8 *data, size_t size)
{
	if (!narc || !data || size < 16)
		return ERR_INVALID_DATA;
	memset (narc, 0, sizeof (*narc));

	if (memcmp (data, "NARC", 4) && memcmp (data, "CRAN", 4))
		return ERR_INVALID_DATA;

	u16 bom = rd_le16 (data + 4);
	bool is_le = (bom == 0xFFFE || !memcmp (data, "CRAN", 4));
	narc->raw = data;
	narc->raw_size = size;
	narc->is_le = is_le;

	u32 off = 16;
	const u8 *fatb_data = 0;
	uint fatb_files = 0;
	const u8 *btnf_data = 0;
	uint btnf_size = 0;
	const u8 *fimg_data = 0;
	uint fimg_size = 0;

	while (off + 8 <= size)
	{
		char ch_magic[5] = { 0 };
		memcpy (ch_magic, data + off, 4);
		u32 ch_size = is_le ? rd_le32 (data + off + 4) : rd_be32 (data + off + 4);
		if (ch_size < 8 || off + ch_size > size)
			break;

		if (!memcmp (ch_magic, "BTAF", 4) || !memcmp (ch_magic, "FATB", 4))
		{
			fatb_data = data + off;
			fatb_files = (is_le ? rd_le32 (data + off + 8) : rd_be32 (data + off + 8)) & 0xFFFF;
		}
		else if (!memcmp (ch_magic, "BTNF", 4) || !memcmp (ch_magic, "FNTB", 4))
		{
			btnf_data = data + off;
			btnf_size = ch_size;
		}
		else if (!memcmp (ch_magic, "GMIF", 4) || !memcmp (ch_magic, "FIMG", 4))
		{
			fimg_data = data + off + 8;
			fimg_size = ch_size - 8;
		}
		off += ch_size;
	}

	if (!fatb_data || !fimg_data || !fatb_files)
		return ERR_INVALID_DATA;

	narc->n_entries = fatb_files;
	narc->entries = CALLOC (fatb_files, sizeof (narc_entry_t));
	narc->fimg_data = fimg_data;
	narc->fimg_size = fimg_size;

	for (uint i = 0; i < fatb_files; i++)
	{
		const u8 *entry_ptr = fatb_data + 12 + 8 * i;
		if (entry_ptr + 8 > fatb_data + (is_le ? rd_le32 (fatb_data + 4) : rd_be32 (fatb_data + 4)))
			break;
		u32 st = is_le ? rd_le32 (entry_ptr) : rd_be32 (entry_ptr);
		u32 en = is_le ? rd_le32 (entry_ptr + 4) : rd_be32 (entry_ptr + 4);
		narc->entries[i].offset = st;
		narc->entries[i].size = en >= st ? en - st : 0;
	}

	if (btnf_data && btnf_size >= 16)
	{
		uint num_dirs = (is_le ? rd_le16 (btnf_data + 14) : rd_be16 (btnf_data + 14)) & 0x0FFF;
		if (num_dirs > 0 && num_dirs < 4096)
		{
			typedef struct narc_dir_t
			{
				u32 sub;
				u16 first;
				u16 parent;
			} narc_dir_t;
			narc_dir_t *dirs = CALLOC (num_dirs, sizeof (narc_dir_t));
			char **dir_paths = CALLOC (num_dirs, sizeof (char *));

			for (uint d = 0; d < num_dirs; d++)
			{
				if (8 + 8 * d + 8 <= btnf_size)
				{
					dirs[d].sub
						= is_le ? rd_le32 (btnf_data + 8 + 8 * d) : rd_be32 (btnf_data + 8 + 8 * d);
					dirs[d].first = is_le ? rd_le16 (btnf_data + 8 + 8 * d + 4)
										  : rd_be16 (btnf_data + 8 + 8 * d + 4);
					dirs[d].parent = (is_le ? rd_le16 (btnf_data + 8 + 8 * d + 6)
											: rd_be16 (btnf_data + 8 + 8 * d + 6))
						& 0x0FFF;
				}
			}

			for (uint d = 0; d < num_dirs; d++)
			{
				const char *parent_path = dir_paths[d] ? dir_paths[d] : "";
				uint cur_file = dirs[d].first;
				u32 pos = 8 + dirs[d].sub;

				while (pos < btnf_size)
				{
					u8 len_byte = btnf_data[pos++];
					if (len_byte == 0)
						break;

					if (len_byte & 0x80)
					{
						uint name_len = len_byte & 0x7F;
						if (pos + name_len + 2 > btnf_size)
							break;
						char dname[PATH_MAX];
						snprintf (dname, sizeof (dname), "%.*s", (int)name_len, btnf_data + pos);
						pos += name_len;
						u16 subdir_id
							= (is_le ? rd_le16 (btnf_data + pos) : rd_be16 (btnf_data + pos))
							& 0x0FFF;
						pos += 2;

						if (subdir_id < num_dirs && !dir_paths[subdir_id])
						{
							char full_d[PATH_MAX];
							if (*parent_path)
								snprintf (full_d, sizeof (full_d), "%s/%s", parent_path, dname);
							else
								snprintf (full_d, sizeof (full_d), "%s", dname);
							dir_paths[subdir_id] = STRDUP (full_d);
						}
					}
					else
					{
						uint name_len = len_byte;
						if (pos + name_len > btnf_size)
							break;
						char fname[PATH_MAX];
						snprintf (fname, sizeof (fname), "%.*s", (int)name_len, btnf_data + pos);
						pos += name_len;

						if (cur_file < narc->n_entries && !narc->entries[cur_file].name)
						{
							char full_f[PATH_MAX];
							if (*parent_path)
								snprintf (full_f, sizeof (full_f), "%s/%s", parent_path, fname);
							else
								snprintf (full_f, sizeof (full_f), "%s", fname);
							narc->entries[cur_file].name = STRDUP (full_f);
						}
						cur_file++;
					}
				}
			}

			for (uint d = 0; d < num_dirs; d++)
				FREE (dir_paths[d]);
			FREE (dir_paths);
			FREE (dirs);
		}
	}

	return ERR_OK;
}

enumError CreateNARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool is_le)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	void (*w16) (u8 *, u16) = is_le ? wr_le16 : wr_be16;
	void (*w32) (u8 *, u32) = is_le ? wr_le32 : wr_be32;

	const uint btaf_header_size = 12;
	const uint btaf_entries_size = 8 * n_entries;
	const uint btaf_size = btaf_header_size + btaf_entries_size;

	typedef struct narc_build_dir_t
	{
		char *name;
		uint parent;
		uint first_file;
		uint num_files;
		uint *file_indices;
		uint num_subdirs;
		uint *subdir_indices;
	} narc_build_dir_t;

	narc_build_dir_t dirs[512];
	uint num_dirs = 1;
	memset (dirs, 0, sizeof (dirs));
	dirs[0].name = STRDUP ("");
	dirs[0].parent = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp full_name = entries[i].name ? entries[i].name : "file";
		char dir_part[PATH_MAX] = { 0 };
		char file_part[PATH_MAX] = { 0 };

		ccp slash = strrchr (full_name, '/');
		if (slash)
		{
			size_t dlen = slash - full_name;
			if (dlen >= sizeof (dir_part))
				dlen = sizeof (dir_part) - 1;
			memcpy (dir_part, full_name, dlen);
			dir_part[dlen] = 0;
			snprintf (file_part, sizeof (file_part), "%s", slash + 1);
		}
		else
		{
			snprintf (file_part, sizeof (file_part), "%s", full_name);
		}

		uint cur_dir = 0;
		if (dir_part[0])
		{
			char *p = dir_part;
			while (*p)
			{
				char seg[PATH_MAX];
				char *slash2 = strchr (p, '/');
				if (slash2)
				{
					size_t slen = slash2 - p;
					if (slen >= sizeof (seg))
						slen = sizeof (seg) - 1;
					memcpy (seg, p, slen);
					seg[slen] = 0;
					p = slash2 + 1;
				}
				else
				{
					snprintf (seg, sizeof (seg), "%s", p);
					p += strlen (p);
				}

				int found = -1;
				for (uint s = 0; s < dirs[cur_dir].num_subdirs; s++)
				{
					uint sidx = dirs[cur_dir].subdir_indices[s];
					if (!strcmp (dirs[sidx].name, seg))
					{
						found = (int)sidx;
						break;
					}
				}

				if (found < 0)
				{
					if (num_dirs >= 512)
						break;
					uint new_d = num_dirs++;
					dirs[new_d].name = STRDUP (seg);
					dirs[new_d].parent = cur_dir;
					dirs[cur_dir].subdir_indices = REALLOC (dirs[cur_dir].subdir_indices,
						(dirs[cur_dir].num_subdirs + 1) * sizeof (uint));
					dirs[cur_dir].subdir_indices[dirs[cur_dir].num_subdirs++] = new_d;
					cur_dir = new_d;
				}
				else
				{
					cur_dir = (uint)found;
				}
			}
		}

		dirs[cur_dir].file_indices
			= REALLOC (dirs[cur_dir].file_indices, (dirs[cur_dir].num_files + 1) * sizeof (uint));
		dirs[cur_dir].file_indices[dirs[cur_dir].num_files++] = i;
	}

	uint file_counter = 0;
	uint *file_order = CALLOC (n_entries, sizeof (uint));
	for (uint d = 0; d < num_dirs; d++)
	{
		dirs[d].first_file = file_counter;
		for (uint f = 0; f < dirs[d].num_files; f++)
			file_order[file_counter++] = dirs[d].file_indices[f];
	}

	dirs[0].parent = num_dirs;

	uint name_entries_cap = 4096;
	u8 *name_entries_buf = MALLOC (name_entries_cap);
	uint name_entries_len = 0;
	uint *dir_sub_offsets = CALLOC (num_dirs, sizeof (uint));

	for (uint d = 0; d < num_dirs; d++)
	{
		dir_sub_offsets[d] = 8 * num_dirs + name_entries_len;

		for (uint s = 0; s < dirs[d].num_subdirs; s++)
		{
			uint sidx = dirs[d].subdir_indices[s];
			ccp sname = dirs[sidx].name;
			size_t snlen = strlen (sname);
			if (snlen > 127)
				snlen = 127;

			while (name_entries_len + 1 + snlen + 2 + 1 > name_entries_cap)
			{
				name_entries_cap *= 2;
				name_entries_buf = REALLOC (name_entries_buf, name_entries_cap);
			}

			name_entries_buf[name_entries_len++] = (u8)(0x80 | snlen);
			memcpy (name_entries_buf + name_entries_len, sname, snlen);
			name_entries_len += snlen;
			if (is_le)
				wr_le16 (name_entries_buf + name_entries_len, (u16)(0xF000 | sidx));
			else
				wr_be16 (name_entries_buf + name_entries_len, (u16)(0xF000 | sidx));
			name_entries_len += 2;
		}

		for (uint f = 0; f < dirs[d].num_files; f++)
		{
			uint f_orig_idx = dirs[d].file_indices[f];
			ccp full_f = entries[f_orig_idx].name ? entries[f_orig_idx].name : "file";
			ccp slash = strrchr (full_f, '/');
			ccp fname = slash ? slash + 1 : full_f;
			size_t fnlen = strlen (fname);
			if (fnlen > 127)
				fnlen = 127;

			while (name_entries_len + 1 + fnlen + 1 > name_entries_cap)
			{
				name_entries_cap *= 2;
				name_entries_buf = REALLOC (name_entries_buf, name_entries_cap);
			}

			name_entries_buf[name_entries_len++] = (u8)fnlen;
			memcpy (name_entries_buf + name_entries_len, fname, fnlen);
			name_entries_len += fnlen;
		}

		name_entries_buf[name_entries_len++] = 0;
	}

	uint btnf_raw_size = 8 + 8 * num_dirs + name_entries_len;
	uint btnf_size = (btnf_raw_size + 3) & ~3u;

	uint *file_gmif_offsets = CALLOC (n_entries, sizeof (uint));
	uint gmif_cur_offset = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		uint orig_idx = file_order[i];
		file_gmif_offsets[i] = gmif_cur_offset;
		uint sz = entries[orig_idx].size;
		gmif_cur_offset += (sz + 3) & ~3u;
	}

	uint gmif_size = 8 + gmif_cur_offset;
	uint total_narc_size = 16 + btaf_size + btnf_size + gmif_size;

	u8 *out = CALLOC (1, total_narc_size);
	if (!out)
	{
		FREE (file_order);
		FREE (file_gmif_offsets);
		FREE (dir_sub_offsets);
		FREE (name_entries_buf);
		for (uint d = 0; d < num_dirs; d++)
		{
			FREE (dirs[d].name);
			FREE (dirs[d].subdir_indices);
			FREE (dirs[d].file_indices);
		}
		return ERR_CANT_CREATE;
	}

	memcpy (out, "NARC", 4);
	w16 (out + 4, is_le ? 0xFFFE : 0xFEFF);
	w16 (out + 6, 0x0100);
	w32 (out + 8, total_narc_size);
	w16 (out + 12, 16);
	w16 (out + 14, 3);

	u8 *btaf = out + 16;
	memcpy (btaf, "BTAF", 4);
	w32 (btaf + 4, btaf_size);
	w16 (btaf + 8, n_entries);
	w16 (btaf + 10, 0);

	for (uint i = 0; i < n_entries; i++)
	{
		uint orig_idx = file_order[i];
		uint st = file_gmif_offsets[i];
		uint en = st + entries[orig_idx].size;
		w32 (btaf + 12 + 8 * i, st);
		w32 (btaf + 12 + 8 * i + 4, en);
	}

	u8 *btnf = out + 16 + btaf_size;
	memcpy (btnf, "BTNF", 4);
	w32 (btnf + 4, btnf_size);

	for (uint d = 0; d < num_dirs; d++)
	{
		w32 (btnf + 8 + 8 * d, dir_sub_offsets[d]);
		w16 (btnf + 8 + 8 * d + 4, (u16)dirs[d].first_file);
		w16 (btnf + 8 + 8 * d + 6, (u16)(d == 0 ? dirs[0].parent : (0xF000 | dirs[d].parent)));
	}

	memcpy (btnf + 8 + 8 * num_dirs, name_entries_buf, name_entries_len);

	u8 *gmif = out + 16 + btaf_size + btnf_size;
	memcpy (gmif, "GMIF", 4);
	w32 (gmif + 4, gmif_size);

	for (uint i = 0; i < n_entries; i++)
	{
		uint orig_idx = file_order[i];
		if (entries[orig_idx].size && entries[orig_idx].data)
			memcpy (
				gmif + 8 + file_gmif_offsets[i], entries[orig_idx].data, entries[orig_idx].size);
	}

	FREE (file_order);
	FREE (file_gmif_offsets);
	FREE (dir_sub_offsets);
	FREE (name_entries_buf);
	for (uint d = 0; d < num_dirs; d++)
	{
		FREE (dirs[d].name);
		FREE (dirs[d].subdir_indices);
		FREE (dirs[d].file_indices);
	}

	*dest = out;
	*dest_size = total_narc_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Sound Archive Family Implementation (BCSAR, BFSAR, BCWAR, BFWAR, BCGRP, BFGRP)
//-----------------------------------------------------------------------------

void ResetSoundArchive (sound_archive_t *sar)
{
	if (!sar)
		return;
	if (sar->entries)
	{
		for (uint i = 0; i < sar->n_entries; i++)
			FREE (sar->entries[i].name);
		FREE (sar->entries);
	}
	memset (sar, 0, sizeof (*sar));
}

static inline u16 sar_r16 (const u8 *p, bool be)
{
	return be ? rd_be16 (p) : rd_le16 (p);
}
static inline u32 sar_r32 (const u8 *p, bool be)
{
	return be ? rd_be32 (p) : rd_le32 (p);
}
static inline s32 sar_rs32 (const u8 *p, bool be)
{
	return (s32)sar_r32 (p, be);
}
static inline void sar_w16 (u8 *p, u16 v, bool be)
{
	if (be)
		wr_be16 (p, v);
	else
		wr_le16 (p, v);
}
static inline void sar_w32 (u8 *p, u32 v, bool be)
{
	if (be)
		wr_be32 (p, v);
	else
		wr_le32 (p, v);
}

enumError ScanSoundArchive (sound_archive_t *sar, const u8 *data, size_t size)
{
	if (!sar || !data || size < 0x20)
		return EINVAL;

	memset (sar, 0, sizeof (*sar));
	sar->raw = data;
	sar->raw_size = size;

	memcpy (sar->magic, data, 4);
	sar->magic[4] = 0;

	bool is_csar = !memcmp (data, "CSAR", 4);
	bool is_fsar = !memcmp (data, "FSAR", 4);
	bool is_cwar = !memcmp (data, "CWAR", 4);
	bool is_fwar = !memcmp (data, "FWAR", 4);
	bool is_cgrp = !memcmp (data, "CGRP", 4);
	bool is_fgrp = !memcmp (data, "FGRP", 4);

	if (!is_csar && !is_fsar && !is_cwar && !is_fwar && !is_cgrp && !is_fgrp)
		return ERR_INVALID_DATA;

	sar->is_cafe_or_switch = (data[0] == 'F');
	u16 bom = rd_be16 (data + 4);
	sar->is_big_endian = (bom == 0xFEFF);
	bool be = sar->is_big_endian;

	sar->version = sar_r32 (data + 8, be);
	u32 file_size = sar_r32 (data + 0x0C, be);
	u16 num_blocks = sar_r16 (data + 0x10, be);

	if (num_blocks == 0 || num_blocks > 16 || file_size > size)
		return ERR_INVALID_DATA;

	// Scan block references
	u32 strg_off = 0, strg_size = 0;
	u32 info_off = 0, info_size = 0;
	u32 file_off = 0, file_size_block = 0;

	for (uint b = 0; b < num_blocks; b++)
	{
		const u8 *bp = data + 0x14 + b * 12;
		if (0x14 + b * 12 + 12 > size)
			break;
		u16 type_id = sar_r16 (bp, be);
		u32 boff = sar_r32 (bp + 4, be);
		u32 bsz = sar_r32 (bp + 8, be);

		if (boff + bsz <= size)
		{
			if (type_id == 0x2000 || !memcmp (data + boff, "STRG", 4))
			{
				strg_off = boff;
				strg_size = bsz;
			}
			else if (type_id == 0x2001 || type_id == 0x6800 || type_id == 0x7800
				|| !memcmp (data + boff, "INFO", 4))
			{
				info_off = boff;
				info_size = bsz;
			}
			else if (type_id == 0x2002 || type_id == 0x6801 || type_id == 0x7801
				|| !memcmp (data + boff, "FILE", 4))
			{
				file_off = boff;
				file_size_block = bsz;
			}
		}
	}

	if (!info_off || !file_off)
		return ERR_INVALID_DATA;

	const u8 *info_body = data + info_off + 8;
	const u8 *file_body = data + file_off + 8;
	size_t max_file_payload
		= (file_size_block > 8) ? (file_size_block - 8) : (size - (file_off + 8));

	// Handle CWAR / FWAR (Wave Archive)
	if (is_cwar || is_fwar)
	{
		if (info_size < 12)
			return ERR_INVALID_DATA;
		u32 n_waves = sar_r32 (info_body, be);
		if (n_waves > 0x10000)
			return ERR_INVALID_DATA;

		sar->n_entries = n_waves;
		sar->entries = CALLOC (n_waves, sizeof (sar_file_entry_t));
		for (uint i = 0; i < n_waves; i++)
		{
			const u8 *wp = info_body + 4 + i * 12;
			if (wp + 12 > data + size)
				break;
			sar->entries[i].file_id = i;
			sar->entries[i].offset = sar_r32 (wp + 4, be);
			sar->entries[i].size = sar_r32 (wp + 8, be);
			if (sar->entries[i].offset + sar->entries[i].size <= max_file_payload)
				sar->entries[i].data = file_body + sar->entries[i].offset;
			snprintf (
				sar->entries[i].ext, sizeof (sar->entries[i].ext), is_fwar ? ".bfwav" : ".bcwav");
		}
		return ERR_OK;
	}

	// Handle CGRP / FGRP (Group Archive)
	if (is_cgrp || is_fgrp)
	{
		if (info_size < 12)
			return ERR_INVALID_DATA;
		u32 n_files = sar_r32 (info_body, be);
		if (n_files > 0x10000)
			return ERR_INVALID_DATA;

		sar->n_entries = n_files;
		sar->entries = CALLOC (n_files, sizeof (sar_file_entry_t));
		for (uint i = 0; i < n_files; i++)
		{
			s32 ref_off = sar_rs32 (info_body + 4 + i * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 16 > info_size)
				continue;
			const u8 *ep = info_body + ref_off;
			u32 fid = sar_r32 (ep, be);
			u32 foff = sar_r32 (ep + 8, be);
			u32 fsz = sar_r32 (ep + 12, be);

			sar->entries[i].file_id = fid;
			sar->entries[i].offset = foff;
			sar->entries[i].size = fsz;
			if (foff + fsz <= max_file_payload)
			{
				sar->entries[i].data = file_body + foff;
				const u8 *d = sar->entries[i].data;
				if (fsz >= 4)
				{
					if (!memcmp (d, "CSEQ", 4))
						strcpy (sar->entries[i].ext, ".bcseq");
					else if (!memcmp (d, "FSEQ", 4))
						strcpy (sar->entries[i].ext, ".bfseq");
					else if (!memcmp (d, "CBNK", 4))
						strcpy (sar->entries[i].ext, ".bcbnk");
					else if (!memcmp (d, "FBNK", 4))
						strcpy (sar->entries[i].ext, ".bfbnk");
					else if (!memcmp (d, "CWAR", 4))
						strcpy (sar->entries[i].ext, ".bcwar");
					else if (!memcmp (d, "FWAR", 4))
						strcpy (sar->entries[i].ext, ".bfwar");
					else if (!memcmp (d, "CWSD", 4))
						strcpy (sar->entries[i].ext, ".bcwsd");
					else if (!memcmp (d, "FWSD", 4))
						strcpy (sar->entries[i].ext, ".bfwsd");
					else if (!memcmp (d, "CWAV", 4))
						strcpy (sar->entries[i].ext, ".bcwav");
					else if (!memcmp (d, "FWAV", 4))
						strcpy (sar->entries[i].ext, ".bfwav");
					else
						strcpy (sar->entries[i].ext, ".bin");
				}
				else
					strcpy (sar->entries[i].ext, ".bin");
			}
		}
		return ERR_OK;
	}

	// Handle CSAR / BFSAR (Sound Archive)
	// 1. Read String Table if present
	char **string_pool = 0;
	uint n_strings = 0;
	if (strg_off && strg_size > 16)
	{
		const u8 *strg_body = data + strg_off + 8;
		s32 str_tab_off = sar_rs32 (strg_body + 4, be);
		if (str_tab_off >= 0 && (uint)str_tab_off + 4 <= strg_size)
		{
			const u8 *str_tab = strg_body + str_tab_off;
			n_strings = sar_r32 (str_tab, be);
			if (n_strings > 0 && n_strings < 0x20000)
			{
				string_pool = CALLOC (n_strings, sizeof (char *));
				for (uint s = 0; s < n_strings; s++)
				{
					s32 s_off = sar_rs32 (str_tab + 4 + s * 8 + 4, be);
					if (s_off >= 0 && (uint)s_off < strg_size)
					{
						ccp str_ptr = (ccp)(str_tab + s_off);
						if (str_ptr < (ccp)(data + strg_off + strg_size))
							string_pool[s] = STRDUP (str_ptr);
					}
				}
			}
		}
	}

	// 2. Read INFO Block Section 6 (File Table)
	s32 file_tab_off = sar_rs32 (info_body + 6 * 8 + 4, be);
	if (file_tab_off < 0 || (uint)file_tab_off + 4 > info_size)
	{
		if (string_pool)
		{
			for (uint s = 0; s < n_strings; s++)
				FREE (string_pool[s]);
			FREE (string_pool);
		}
		return ERR_INVALID_DATA;
	}

	const u8 *file_tab = info_body + file_tab_off;
	u32 n_files = sar_r32 (file_tab, be);
	if (n_files > 0x20000)
	{
		if (string_pool)
		{
			for (uint s = 0; s < n_strings; s++)
				FREE (string_pool[s]);
			FREE (string_pool);
		}
		return ERR_INVALID_DATA;
	}

	sar->n_entries = n_files;
	sar->entries = CALLOC (n_files, sizeof (sar_file_entry_t));

	for (uint i = 0; i < n_files; i++)
	{
		sar->entries[i].file_id = i;
		s32 ref_off = sar_rs32 (file_tab + 4 + i * 8 + 4, be);
		if (ref_off < 0 || (uint)ref_off + 8 > info_size)
			continue;
		const u8 *fe = file_tab + ref_off;
		u16 loc_type = sar_r16 (fe, be);
		s32 loc_off = sar_rs32 (fe + 4, be);

		if (loc_type == 0x220c && loc_off >= 0 && (uint)loc_off + 12 <= info_size) // Internal
		{
			const u8 *ib = fe + loc_off;
			u32 foff = sar_r32 (ib + 4, be);
			u32 fsz = sar_r32 (ib + 8, be);
			sar->entries[i].offset = foff;
			sar->entries[i].size = fsz;
			if (foff + fsz <= max_file_payload)
				sar->entries[i].data = file_body + foff;
		}
	}

	// 3. Associate Symbol Names and Extensions
	// Check Sounds table (Section 0: 0x2100)
	s32 snd_tab_off = sar_rs32 (info_body + 0 * 8 + 4, be);
	if (snd_tab_off >= 0 && (uint)snd_tab_off + 4 <= info_size)
	{
		const u8 *snd_tab = info_body + snd_tab_off;
		u32 n_snds = sar_r32 (snd_tab, be);
		for (uint s = 0; s < n_snds && s < 0x20000; s++)
		{
			s32 ref_off = sar_rs32 (snd_tab + 4 + s * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 16 > info_size)
				continue;
			const u8 *sp = snd_tab + ref_off;
			u32 fid = sar_r32 (sp, be);
			if (fid < sar->n_entries)
			{
				// BitFlags at sp + 0x14
				u32 flag_bits = sar_r32 (sp + 0x14, be);
				if ((flag_bits & 1) && string_pool)
				{
					u32 s_idx = sar_r32 (sp + 0x18, be);
					if (s_idx < n_strings && string_pool[s_idx] && !sar->entries[fid].name)
						sar->entries[fid].name = STRDUP (string_pool[s_idx]);
				}
			}
		}
	}

	// Check Banks table (Section 1: 0x2101)
	s32 bnk_tab_off = sar_rs32 (info_body + 1 * 8 + 4, be);
	if (bnk_tab_off >= 0 && (uint)bnk_tab_off + 4 <= info_size)
	{
		const u8 *bnk_tab = info_body + bnk_tab_off;
		u32 n_bnks = sar_r32 (bnk_tab, be);
		for (uint b = 0; b < n_bnks && b < 0x20000; b++)
		{
			s32 ref_off = sar_rs32 (bnk_tab + 4 + b * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 12 > info_size)
				continue;
			const u8 *bp = bnk_tab + ref_off;
			u32 fid = sar_r32 (bp, be);
			if (fid < sar->n_entries)
			{
				u32 flag_bits = sar_r32 (bp + 0x08, be);
				if ((flag_bits & 1) && string_pool)
				{
					u32 s_idx = sar_r32 (bp + 0x0C, be);
					if (s_idx < n_strings && string_pool[s_idx] && !sar->entries[fid].name)
						sar->entries[fid].name = STRDUP (string_pool[s_idx]);
				}
			}
		}
	}

	// Check Wave Archive table (Section 3: 0x2103)
	s32 war_tab_off = sar_rs32 (info_body + 3 * 8 + 4, be);
	if (war_tab_off >= 0 && (uint)war_tab_off + 4 <= info_size)
	{
		const u8 *war_tab = info_body + war_tab_off;
		u32 n_wars = sar_r32 (war_tab, be);
		for (uint w = 0; w < n_wars && w < 0x20000; w++)
		{
			s32 ref_off = sar_rs32 (war_tab + 4 + w * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 12 > info_size)
				continue;
			const u8 *wp = war_tab + ref_off;
			u32 fid = sar_r32 (wp, be);
			if (fid < sar->n_entries)
			{
				u32 flag_bits = sar_r32 (wp + 0x08, be);
				if ((flag_bits & 1) && string_pool)
				{
					u32 s_idx = sar_r32 (wp + 0x0C, be);
					if (s_idx < n_strings && string_pool[s_idx] && !sar->entries[fid].name)
						sar->entries[fid].name = STRDUP (string_pool[s_idx]);
				}
			}
		}
	}

	// Check Group table (Section 5: 0x2105)
	s32 grp_tab_off = sar_rs32 (info_body + 5 * 8 + 4, be);
	if (grp_tab_off >= 0 && (uint)grp_tab_off + 4 <= info_size)
	{
		const u8 *grp_tab = info_body + grp_tab_off;
		u32 n_grps = sar_r32 (grp_tab, be);
		for (uint g = 0; g < n_grps && g < 0x20000; g++)
		{
			s32 ref_off = sar_rs32 (grp_tab + 4 + g * 8 + 4, be);
			if (ref_off < 0 || (uint)ref_off + 8 > info_size)
				continue;
			const u8 *gp = grp_tab + ref_off;
			u32 fid = sar_r32 (gp, be);
			if (fid < sar->n_entries)
			{
				u32 flag_bits = sar_r32 (gp + 0x04, be);
				if ((flag_bits & 1) && string_pool)
				{
					u32 s_idx = sar_r32 (gp + 0x08, be);
					if (s_idx < n_strings && string_pool[s_idx] && !sar->entries[fid].name)
						sar->entries[fid].name = STRDUP (string_pool[s_idx]);
				}
			}
		}
	}

	// 4. Sniff extensions for each file
	for (uint i = 0; i < sar->n_entries; i++)
	{
		if (sar->entries[i].data && sar->entries[i].size >= 4)
		{
			const u8 *d = sar->entries[i].data;
			if (!memcmp (d, "CSEQ", 4))
				strcpy (sar->entries[i].ext, ".bcseq");
			else if (!memcmp (d, "FSEQ", 4))
				strcpy (sar->entries[i].ext, ".bfseq");
			else if (!memcmp (d, "CBNK", 4))
				strcpy (sar->entries[i].ext, ".bcbnk");
			else if (!memcmp (d, "FBNK", 4))
				strcpy (sar->entries[i].ext, ".bfbnk");
			else if (!memcmp (d, "CWAR", 4))
				strcpy (sar->entries[i].ext, ".bcwar");
			else if (!memcmp (d, "FWAR", 4))
				strcpy (sar->entries[i].ext, ".bfwar");
			else if (!memcmp (d, "CWSD", 4))
				strcpy (sar->entries[i].ext, ".bcwsd");
			else if (!memcmp (d, "FWSD", 4))
				strcpy (sar->entries[i].ext, ".bfwsd");
			else if (!memcmp (d, "CGRP", 4))
				strcpy (sar->entries[i].ext, ".bcgrp");
			else if (!memcmp (d, "FGRP", 4))
				strcpy (sar->entries[i].ext, ".bfgrp");
			else if (!memcmp (d, "CWAV", 4))
				strcpy (sar->entries[i].ext, ".bcwav");
			else if (!memcmp (d, "FWAV", 4))
				strcpy (sar->entries[i].ext, ".bfwav");
			else if (!memcmp (d, "CSTM", 4))
				strcpy (sar->entries[i].ext, ".bcstm");
			else if (!memcmp (d, "FSTM", 4))
				strcpy (sar->entries[i].ext, ".bfstm");
			else
				strcpy (sar->entries[i].ext, ".bin");
		}
		else
			strcpy (sar->entries[i].ext, ".bin");
	}

	if (string_pool)
	{
		for (uint s = 0; s < n_strings; s++)
			FREE (string_pool[s]);
		FREE (string_pool);
	}

	return ERR_OK;
}

// Decode RWAV (Wii)/FWAV (Wii U/Switch)/CWAV (3DS) NintendoWare wave audio
// to a standard PCM WAV file. Layout verified byte-for-byte against a real
// retail BFWAV (Super Mario 64 [NABE01] wiiu_shared.bfsar, wave 0000: PCM16
// mono/32000Hz/436 samples) and cross-checked against vgmstream's bfwav.c
// (RWAV/FWAV/CWAV share the INFO/DATA block scheme; only RWAV's INFO layout
// and the FWAV/CWAV block-table position differ -- see per-type branches
// below). ADPCM decode starts from zero history every call (matches
// vgmstream, which also never restores loop-point history for this format
// family), so a looped ADPCM stream's post-loop samples will drift slightly
// from a hardware decode; PCM8/PCM16 and the non-looping case are exact.
enumError DecodeBXWAV (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 0x20)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;

	enum
	{
		T_RWAV,
		T_FWAV,
		T_CWAV
	} type;
	if (!memcmp (src, "RWAV", 4))
		type = T_RWAV;
	else if (!memcmp (src, "FWAV", 4))
		type = T_FWAV;
	else if (!memcmp (src, "CWAV", 4))
		type = T_CWAV;
	else
		return EINVAL;

	bool be;
	if (rd_be16 (src + 4) == 0xFEFF)
		be = true;
	else if (rd_le16 (src + 4) == 0xFEFF)
		be = false;
	else
		return EINVAL;
#define BW16(off) (be ? rd_be16 (src + (off)) : rd_le16 (src + (off)))
#define BW32(off) (be ? rd_be32 (src + (off)) : rd_le32 (src + (off)))

	u32 file_size, info_off, data_off;
	if (type == T_RWAV)
	{
		if (src_size < 0x20)
			return EINVAL;
		file_size = BW32 (0x08);
		info_off = BW32 (0x10);
		data_off = BW32 (0x18);
	}
	else
	{
		if (src_size < 0x2C)
			return EINVAL;
		file_size = BW32 (0x0C);
		info_off = BW32 (0x18);
		data_off = BW32 (0x24);
	}
	if (file_size > src_size || file_size < 0x20)
		return EINVAL;
	if ((u64)info_off + 0x20 > src_size || (u64)data_off + 8 > src_size)
		return EINVAL;
	if (memcmp (src + info_off, "INFO", 4) || memcmp (src + data_off, "DATA", 4))
		return EINVAL;

	u8 codec, loop_flag;
	u32 sample_rate;
	s32 num_samples;
	u32 chtb_off;
	if (type == T_RWAV)
	{
		if (info_off + 0x1C > src_size)
			return EINVAL;
		codec = src[info_off + 0x08];
		loop_flag = src[info_off + 0x09];
		u8 channels_hdr = src[info_off + 0x0A];
		(void)channels_hdr; // channel count is re-derived from the channel table below
		sample_rate = BW16 (info_off + 0x0C);
		num_samples = (s32)BW32 (info_off + 0x14);
		chtb_off = BW32 (info_off + 0x18) + info_off + 0x08;
		// RWAV loop/sample counts are in DSP nibble units (2 nibbles/byte,
		// 1 header nibble per 8-sample frame): samples = (n/16)*14 + n%16 - 2
		num_samples = (num_samples / 16) * 14 + num_samples % 16 - 2;
	}
	else
	{
		if (info_off + 0x20 > src_size)
			return EINVAL;
		codec = src[info_off + 0x08];
		loop_flag = src[info_off + 0x09];
		sample_rate = BW32 (info_off + 0x0C);
		num_samples = (s32)BW32 (info_off + 0x14);
		chtb_off = info_off + 0x1C;
	}
	(void)loop_flag;
	if (num_samples < 0 || (u64)chtb_off + 4 > src_size)
		return EINVAL;

	u32 channels;
	if (type == T_RWAV)
	{
		// Channel table for RWAV has no explicit count field; derive it from
		// the channel-table size stored right before it (already consumed
		// above via chtb_off), so fall back to the header byte instead.
		channels = src[info_off + 0x0A];
	}
	else
	{
		channels = BW32 (chtb_off + 0x00);
	}
	if (channels == 0 || channels > 8)
		return EINVAL;

	if (codec > 3)
		return ERROR0 (ERR_WRONG_FILE_TYPE, "BXWAV: unsupported codec 0x%02x\n", codec);

	// Resolve each channel's sample-data offset (and DSP coefs, if ADPCM).
	u32 ch_data_off[8];
	s16 ch_coef[8][16];
	for (uint ch = 0; ch < channels; ch++)
	{
		u32 chnf_off, coef_off = 0;
		bool has_adpcm = false;
		if (type == T_RWAV)
		{
			if ((u64)chtb_off + ch * 4 + 4 > src_size)
				return EINVAL;
			chnf_off = BW32 (chtb_off + ch * 4) + info_off + 0x08;
			if ((u64)chnf_off + 8 > src_size)
				return EINVAL;
			ch_data_off[ch] = BW32 (chnf_off + 0x00) + data_off + 8;
			u32 adpcm_ref = BW32 (chnf_off + 0x04);
			if (adpcm_ref != 0xFFFFFFFF)
			{
				coef_off = adpcm_ref + info_off + 0x08;
				has_adpcm = true;
			}
		}
		else
		{
			if ((u64)chtb_off + 4 + ch * 8 + 8 > src_size)
				return EINVAL;
			chnf_off = BW32 (chtb_off + 4 + ch * 8 + 4) + chtb_off;
			if ((u64)chnf_off + 0x10 > src_size)
				return EINVAL;
			if ((BW16 (chnf_off + 0x00) & 0x1F00) != 0x1F00)
				return EINVAL;
			ch_data_off[ch] = BW32 (chnf_off + 0x04) + data_off + 8;
			u32 adpcm_ref = BW32 (chnf_off + 0x0C);
			if (adpcm_ref != 0xFFFFFFFF)
			{
				coef_off = adpcm_ref + chnf_off;
				has_adpcm = true;
			}
		}
		if (codec == 2) // DSP-ADPCM: coefs are mandatory
		{
			if (!has_adpcm || (u64)coef_off + 0x20 > src_size)
				return EINVAL;
			for (uint i = 0; i < 16; i++)
				ch_coef[ch][i] = (s16)BW16 (coef_off + i * 2);
		}
		else if (codec == 3) // IMA-ADPCM: hist1(s16) + step_index(s16) seed
		{
			if (!has_adpcm || (u64)coef_off + 4 > src_size)
				return EINVAL;
			ch_coef[ch][0] = (s16)BW16 (coef_off + 0); // initial history
			ch_coef[ch][1] = (s16)BW16 (coef_off + 2); // initial step index
		}
	}

	// Decode every channel to 16-bit signed PCM (or keep PCM8 native for the
	// 8-bit case) into a planar buffer, then interleave into the WAV body.
	uint bytes_per_sample = codec == 0 ? 1 : 2;
	if ((u64)num_samples * channels * bytes_per_sample > NFMT_MAX_OUTPUT)
		return ERR_FILE_TOO_BIG;

	u16 *pcm16[8] = { 0 };
	u8 *pcm8[8] = { 0 };
	enumError err = ERR_OK;
	for (uint ch = 0; ch < channels && !err; ch++)
	{
		switch (codec)
		{
			case 0: // PCM8 (unsigned in the WAV convention, signed on disk)
			{
				if ((u64)ch_data_off[ch] + num_samples > src_size)
				{
					err = EINVAL;
					break;
				}
				pcm8[ch] = MALLOC (num_samples);
				for (s32 i = 0; i < num_samples; i++)
					pcm8[ch][i] = (u8)(src[ch_data_off[ch] + i] ^ 0x80);
				break;
			}
			case 1: // PCM16
			{
				if ((u64)ch_data_off[ch] + (u64)num_samples * 2 > src_size)
				{
					err = EINVAL;
					break;
				}
				pcm16[ch] = MALLOC (num_samples * 2);
				for (s32 i = 0; i < num_samples; i++)
					pcm16[ch][i] = BW16 (ch_data_off[ch] + i * 2);
				break;
			}
			case 2: // Standard GC/Wii/3DS DSP-ADPCM: 8-byte frames, 14 samples each
			{
				pcm16[ch] = MALLOC ((size_t)num_samples * 2 + 2);
				s16 hist1 = 0, hist2 = 0;
				u32 src_off = ch_data_off[ch];
				s32 done = 0;
				while (done < num_samples)
				{
					if (src_off + 8 > src_size)
					{
						err = EINVAL;
						break;
					}
					u8 frame_hdr = src[src_off];
					s32 scale = 1 << (frame_hdr & 0x0F);
					uint coef_idx = frame_hdr >> 4;
					if (coef_idx > 7)
					{
						err = EINVAL;
						break;
					}
					s16 c1 = ch_coef[ch][coef_idx * 2], c2 = ch_coef[ch][coef_idx * 2 + 1];
					for (uint i = 0; i < 14 && done < num_samples; i++, done++)
					{
						u8 byte = src[src_off + 1 + (i >> 1)];
						u8 nib = (i & 1) ? (byte & 0x0F) : (byte >> 4);
						s8 snib = (s8)(nib << 4) >> 4;
						s32 raw = (((s32)snib * scale) << 11) + 1024 + (c1 * hist1 + c2 * hist2);
						raw >>= 11;
						s16 sample = raw > 0x7FFF ? 0x7FFF : raw < -0x8000 ? -0x8000 : (s16)raw;
						pcm16[ch][done] = (u16)sample;
						hist2 = hist1;
						hist1 = sample;
					}
					src_off += 8;
				}
				break;
			}
			case 3: // IMA-ADPCM (mono-style, per channel): hist16 + step_index seed, 4-bit nibbles
			{
				static const int ima_index_table[16]
					= { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
				static const int ima_step_table[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21,
					23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
					143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
					658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
					2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
					8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
					24623, 27086, 29794, 32767 };
				uint bytes_needed = (num_samples + 1) / 2;
				if ((u64)ch_data_off[ch] + bytes_needed > src_size)
				{
					err = EINVAL;
					break;
				}
				pcm16[ch] = MALLOC ((size_t)num_samples * 2 + 2);
				s32 predictor = ch_coef[ch][0];
				int step_index = ch_coef[ch][1];
				if (step_index < 0)
					step_index = 0;
				if (step_index > 88)
					step_index = 88;
				u32 src_off = ch_data_off[ch];
				for (s32 i = 0; i < num_samples; i++)
				{
					u8 byte = src[src_off + (i >> 1)];
					uint nib = (i & 1) ? (byte >> 4) : (byte & 0x0F);
					int step = ima_step_table[step_index];
					int diff = step >> 3;
					if (nib & 1)
						diff += step >> 2;
					if (nib & 2)
						diff += step >> 1;
					if (nib & 4)
						diff += step;
					if (nib & 8)
						predictor -= diff;
					else
						predictor += diff;
					predictor = predictor > 32767 ? 32767 : predictor < -32768 ? -32768 : predictor;
					step_index += ima_index_table[nib];
					step_index = step_index < 0 ? 0 : step_index > 88 ? 88 : step_index;
					pcm16[ch][i] = (u16)(s16)predictor;
				}
				break;
			}
			default:
				err = ERROR0 (ERR_WRONG_FILE_TYPE, "BXWAV: unsupported codec 0x%02x\n", codec);
		}
	}

	if (err)
	{
		for (uint ch = 0; ch < channels; ch++)
		{
			FREE (pcm16[ch]);
			FREE (pcm8[ch]);
		}
		return err;
	}

	// Build a canonical 44-byte-header PCM WAV.
	uint bits = bytes_per_sample * 8;
	uint block_align = channels * bytes_per_sample;
	uint data_size = (uint)num_samples * block_align;
	uint total = 44 + data_size;
	u8 *out = MALLOC (total);
	memcpy (out + 0, "RIFF", 4);
	wr_le32 (out + 4, total - 8);
	memcpy (out + 8, "WAVE", 4);
	memcpy (out + 12, "fmt ", 4);
	wr_le32 (out + 16, 16);
	wr_le16 (out + 20, 1); // PCM
	wr_le16 (out + 22, (u16)channels);
	wr_le32 (out + 24, sample_rate);
	wr_le32 (out + 28, sample_rate * block_align);
	wr_le16 (out + 32, (u16)block_align);
	wr_le16 (out + 34, (u16)bits);
	memcpy (out + 36, "data", 4);
	wr_le32 (out + 40, data_size);

	u8 *dp = out + 44;
	for (s32 i = 0; i < num_samples; i++)
		for (uint ch = 0; ch < channels; ch++)
		{
			if (codec == 0)
			{
				*dp++ = pcm8[ch][i];
			}
			else
			{
				wr_le16 (dp, pcm16[ch][i]);
				dp += 2;
			}
		}

	for (uint ch = 0; ch < channels; ch++)
	{
		FREE (pcm16[ch]);
		FREE (pcm8[ch]);
	}
	*dest = out;
	*dest_size = total;
#undef BW16
#undef BW32
	return ERR_OK;
}

enumError CreateSoundArchive (u8 **dest, uint *dest_size, const sound_archive_t *sar)
{
	if (!dest || !dest_size || !sar || sar->n_entries == 0)
		return EINVAL;

	bool be = sar->is_big_endian;
	bool is_cafe = sar->is_cafe_or_switch;
	bool is_war = !strcmp (sar->magic, "CWAR") || !strcmp (sar->magic, "FWAR");
	bool is_grp = !strcmp (sar->magic, "CGRP") || !strcmp (sar->magic, "FGRP");

	// Calculate file offsets within FILE block body (aligned to 32 bytes)
	u32 *file_offsets = CALLOC (sar->n_entries, sizeof (u32));
	u32 cur_file_offset = 0;
	for (uint i = 0; i < sar->n_entries; i++)
	{
		file_offsets[i] = cur_file_offset;
		cur_file_offset += (sar->entries[i].size + 0x1F) & ~0x1F;
	}
	u32 file_payload_size = cur_file_offset;
	u32 file_block_size = 8 + file_payload_size;

	// Handle Wave Archive (CWAR / FWAR)
	if (is_war)
	{
		u32 info_payload_size = 4 + sar->n_entries * 12;
		u32 info_block_size = (8 + info_payload_size + 0x1F) & ~0x1F;
		u32 header_size = 0x20;
		u32 total_size = header_size + info_block_size + file_block_size;

		u8 *out = CALLOC (total_size, 1);

		// Header
		memcpy (out, is_cafe ? "FWAR" : "CWAR", 4);
		sar_w16 (out + 4, be ? 0xFEFF : 0xFFFE, true);
		sar_w16 (out + 6, (u16)header_size, be);
		sar_w32 (out + 8, sar->version ? sar->version : (is_cafe ? 0x00010000 : 0x01000000), be);
		sar_w32 (out + 0x0C, total_size, be);
		sar_w16 (out + 0x10, 2, be); // 2 blocks (INFO, FILE)

		// Block 0: INFO
		sar_w16 (out + 0x14, 0x6800, be);
		sar_w32 (out + 0x14 + 4, header_size, be);
		sar_w32 (out + 0x14 + 8, info_block_size, be);

		// Block 1: FILE
		sar_w16 (out + 0x20, 0x6801, be);
		sar_w32 (out + 0x20 + 4, header_size + info_block_size, be);
		sar_w32 (out + 0x20 + 8, file_block_size, be);

		// INFO block body
		u8 *inf = out + header_size;
		memcpy (inf, "INFO", 4);
		sar_w32 (inf + 4, info_block_size, be);
		sar_w32 (inf + 8, sar->n_entries, be);
		for (uint i = 0; i < sar->n_entries; i++)
		{
			u8 *wp = inf + 12 + i * 12;
			sar_w16 (wp, 0x1F00, be);
			sar_w32 (wp + 4, file_offsets[i], be);
			sar_w32 (wp + 8, sar->entries[i].size, be);
		}

		// FILE block body
		u8 *fil = out + header_size + info_block_size;
		memcpy (fil, "FILE", 4);
		sar_w32 (fil + 4, file_block_size, be);
		for (uint i = 0; i < sar->n_entries; i++)
		{
			if (sar->entries[i].data && sar->entries[i].size)
				memcpy (fil + 8 + file_offsets[i], sar->entries[i].data, sar->entries[i].size);
		}

		FREE (file_offsets);
		*dest = out;
		*dest_size = total_size;
		return ERR_OK;
	}

	// Handle Full Sound Archive (CSAR / BFSAR / CGRP / BFGRP)
	// 1. Build STRG block
	uint n_strings = 0;
	for (uint i = 0; i < sar->n_entries; i++)
		if (sar->entries[i].name && *sar->entries[i].name)
			n_strings++;

	u32 strg_table_size = 4 + n_strings * 8;
	u32 strg_strings_size = 0;
	for (uint i = 0; i < sar->n_entries; i++)
		if (sar->entries[i].name && *sar->entries[i].name)
			strg_strings_size += (u32)strlen (sar->entries[i].name) + 1;

	u32 strg_payload_size = 16 + strg_table_size + strg_strings_size;
	u32 strg_block_size = (8 + strg_payload_size + 0x1F) & ~0x1F;
	if (strg_block_size < 0x20)
		strg_block_size = 0x20;

	// 2. Build INFO block
	// Header has 7 references (0 to 6) = 56 bytes
	// File table at offset 56: 4 bytes count, n_entries * 8 bytes refs, n_entries * 24 bytes file
	// entries
	u32 file_tab_size = 4 + sar->n_entries * 8 + sar->n_entries * 24;
	u32 info_payload_size = 56 + file_tab_size;
	u32 info_block_size = (8 + info_payload_size + 0x1F) & ~0x1F;

	u32 header_size = 0x40;
	u32 total_size = header_size + strg_block_size + info_block_size + file_block_size;

	u8 *out = CALLOC (total_size, 1);

	// Header
	const char *hdr_magic = is_grp ? (is_cafe ? "FGRP" : "CGRP") : (is_cafe ? "FSAR" : "CSAR");
	memcpy (out, hdr_magic, 4);
	sar_w16 (out + 4, be ? 0xFEFF : 0xFFFE, true);
	sar_w16 (out + 6, (u16)header_size, be);
	sar_w32 (out + 8, sar->version ? sar->version : (is_cafe ? 0x00020200 : 0x02000000), be);
	sar_w32 (out + 0x0C, total_size, be);
	sar_w16 (out + 0x10, 3, be); // 3 blocks (STRG, INFO, FILE)

	// SizedReference 0: STRG
	sar_w16 (out + 0x14, 0x2000, be);
	sar_w32 (out + 0x14 + 4, header_size, be);
	sar_w32 (out + 0x14 + 8, strg_block_size, be);

	// SizedReference 1: INFO
	sar_w16 (out + 0x20, 0x2001, be);
	sar_w32 (out + 0x20 + 4, header_size + strg_block_size, be);
	sar_w32 (out + 0x20 + 8, info_block_size, be);

	// SizedReference 2: FILE
	sar_w16 (out + 0x2C, 0x2002, be);
	sar_w32 (out + 0x2C + 4, header_size + strg_block_size + info_block_size, be);
	sar_w32 (out + 0x2C + 8, file_block_size, be);

	// STRG block
	u8 *stb = out + header_size;
	memcpy (stb, "STRG", 4);
	sar_w32 (stb + 4, strg_block_size, be);
	sar_w16 (stb + 8, 0x2400, be); // Reference to String Table
	sar_w32 (stb + 8 + 4, 16, be); // String table starts at offset 16 from STRG body
	sar_w16 (stb + 16, 0x2401, be); // Patricia tree (null ref = -1)
	sar_w32 (stb + 16 + 4, 0xFFFFFFFF, be);

	u8 *str_tab = stb + 8 + 16;
	sar_w32 (str_tab, n_strings, be);
	u32 cur_str_off = strg_table_size;
	uint s_idx = 0;
	for (uint i = 0; i < sar->n_entries; i++)
	{
		if (sar->entries[i].name && *sar->entries[i].name)
		{
			u8 *sref = str_tab + 4 + s_idx * 8;
			sar_w16 (sref, 0x1F01, be);
			sar_w32 (sref + 4, cur_str_off, be);
			size_t slen = strlen (sar->entries[i].name);
			memcpy (str_tab + cur_str_off, sar->entries[i].name, slen + 1);
			cur_str_off += (u32)slen + 1;
			s_idx++;
		}
	}

	// INFO block
	u8 *inf = out + header_size + strg_block_size;
	memcpy (inf, "INFO", 4);
	sar_w32 (inf + 4, info_block_size, be);

	// Set null references for Sections 0..5 (-1 offset)
	for (uint sec = 0; sec < 6; sec++)
	{
		sar_w16 (inf + 8 + sec * 8, 0x2100 + sec, be);
		sar_w32 (inf + 8 + sec * 8 + 4, 0xFFFFFFFF, be);
	}

	// Section 6: File Table
	sar_w16 (inf + 8 + 6 * 8, 0x2106, be);
	sar_w32 (inf + 8 + 6 * 8 + 4, 56, be); // starts right after the 7 references

	u8 *ftab = inf + 8 + 56;
	sar_w32 (ftab, sar->n_entries, be);
	u32 cur_entry_rel = 4 + sar->n_entries * 8;
	for (uint i = 0; i < sar->n_entries; i++)
	{
		u8 *fref = ftab + 4 + i * 8;
		sar_w16 (fref, 0x220A, be); // SAR_Info_File
		sar_w32 (fref + 4, cur_entry_rel, be);

		u8 *fe = ftab + cur_entry_rel;
		sar_w16 (fe, 0x220C, be); // SAR_Info_InternalFile
		sar_w32 (fe + 4, 12, be); // internal struct at +12

		u8 *fib = fe + 12;
		sar_w16 (fib, 0x1F00, be);
		sar_w32 (fib + 4, file_offsets[i], be);
		sar_w32 (fib + 8, sar->entries[i].size, be);

		cur_entry_rel += 24;
	}

	// FILE block
	u8 *fil = out + header_size + strg_block_size + info_block_size;
	memcpy (fil, "FILE", 4);
	sar_w32 (fil + 4, file_block_size, be);
	for (uint i = 0; i < sar->n_entries; i++)
	{
		if (sar->entries[i].data && sar->entries[i].size)
			memcpy (fil + 8 + file_offsets[i], sar->entries[i].data, sar->entries[i].size);
	}

	FREE (file_offsets);
	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		Arika INFO.DAT / GAME.DAT archives		///////////////
///////////////////////////////////////////////////////////////////////////////

// See the header comment above ExtractArika() for the full layout. Ported
// from GBATEK's "DS Encrypted Arika Archives with ALZ1 compression" page
// plus aluigi's arika.bms/endless_ocean.bms (for the RF2 sub-container and
// the older "ZALZ"-tagged variant, neither of which GBATEK documents).

enumError DecryptArikaInfo (u8 *buf, uint size)
{
	if (!buf)
		return EINVAL;
	if (size < 0x10 || buf[0] == 0)
		return ERR_OK; // already unencrypted
	u8 key[0x10];
	memcpy (key, buf, 0x10);
	for (uint i = 0x10; i < size; i++)
	{
		u8 b = buf[i];
		b = (u8)(b >> 4 | b << 4); // ror 4 (== rol 4 on a byte)
		b ^= 0xff;
		b = (u8)(b - key[i & 0xf]);
		buf[i] = b;
	}
	return ERR_OK;
}

enumError EncryptArikaInfo (u8 *buf, uint size)
{
	if (!buf)
		return EINVAL;
	if (size < 0x10 || buf[0] == 0)
		return ERR_OK; // stays unencrypted
	u8 key[0x10];
	memcpy (key, buf, 0x10);
	for (uint i = 0x10; i < size; i++)
	{
		u8 b = buf[i];
		b = (u8)(b + key[i & 0xf]);
		b ^= 0xff;
		b = (u8)(b >> 4 | b << 4); // rol 4 (== ror 4 on a byte)
		buf[i] = b;
	}
	return ERR_OK;
}

enumError DecodeALZ1 (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest && dest_size)
		return EINVAL;
	if (!src && src_size)
		return EINVAL;
	u8 ring[0x1000];
	memset (ring, 0, sizeof (ring));
	uint p = 0xfee;
	uint sp = 0, dp = 0;
	uint flags = 0, flag_bits = 0;

	while (dp < dest_size)
	{
		if (!flag_bits)
		{
			if (sp >= src_size)
				return EINVAL;
			flags = src[sp++];
			flag_bits = 8;
		}
		const bool literal = flags & 1;
		flags >>= 1;
		flag_bits--;

		if (literal)
		{
			if (sp >= src_size)
				return EINVAL;
			const u8 b = src[sp++];
			dest[dp++] = b;
			ring[p & 0xfff] = b;
			p++;
		}
		else
		{
			if (sp + 2 > src_size)
				return EINVAL;
			const u8 b0 = src[sp], b1 = src[sp + 1];
			sp += 2;
			uint q = b0 | (uint)(b1 >> 4) << 8;
			uint len = (b1 & 0xf) + 3;
			for (uint i = 0; i < len && dp < dest_size; i++)
			{
				const u8 b = ring[q & 0xfff];
				dest[dp++] = b;
				ring[p & 0xfff] = b;
				p++;
				q++;
			}
		}
	}
	return ERR_OK;
}

enumError EncodeALZ1 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size)
		return EINVAL;
	if (!src && src_size)
		return EINVAL;

	// Worst case: every byte literal -> one flag bit + one byte per byte,
	// flags reloaded every 8 tokens.
	const uint capacity = src_size + (src_size + 7) / 8 + 8;
	u8 *out = MALLOC (capacity ? capacity : 1);
	if (!out)
		return ERR_CANT_CREATE;

	uint dp = 0, sp = 0;
	while (sp < src_size)
	{
		const uint flag_pos = dp++;
		u8 flag = 0;
		for (uint bit = 0; bit < 8 && sp < src_size; bit++)
		{
			uint best_len = 0, best_back = 0;
			const uint max_back = sp < 0x1000 ? sp : 0x1000;
			const uint max_len = src_size - sp < 18 ? src_size - sp : 18;
			// Same backward search style as EncodeLZ10LZ11(): nearby matches
			// first, matches may self-overlap (back < len) exactly like the
			// ring-buffer decoder above naturally allows.
			for (uint back = 1; back <= max_back; back++)
			{
				uint len = 0;
				while (len < max_len && src[sp + len] == src[sp - back + len])
					len++;
				if (len > best_len)
				{
					best_len = len;
					best_back = back;
					if (len == max_len)
						break;
				}
			}
			if (best_len >= 3)
			{
				const uint p = 0xfee + sp;
				const uint q = (p - best_back) & 0xfff;
				out[dp++] = (u8)q;
				out[dp++] = (u8)((q >> 8 & 0xf) << 4 | (best_len - 3));
				sp += best_len;
				// flag bit left 0 -- 0 means "compressed" for ALZ1
			}
			else
			{
				flag |= (u8)(1u << bit);
				out[dp++] = src[sp++];
			}
		}
		out[flag_pos] = flag;
	}
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

typedef struct arika_list_t
{
	nintendo_sarc_entry_t *entries;
	uint n, cap;
} arika_list_t;

static void arika_list_push (arika_list_t *list, ccp name, const u8 *data, u32 size)
{
	if (list->n == list->cap)
	{
		list->cap = list->cap ? list->cap * 2 : 16;
		list->entries = REALLOC (list->entries, list->cap * sizeof (*list->entries));
	}
	nintendo_sarc_entry_t *e = list->entries + list->n++;
	e->name = STRDUP (name);
	u8 *copy = size ? MALLOC (size) : 0;
	if (copy)
		memcpy (copy, data, size);
	e->data = copy;
	e->size = size;
}

// Splits an "RF2"-tagged sub-container (Endless Ocean: Blue World groups
// related assets this way) into its member entries, recursing into any
// member that is itself another RF2 container. Falls back to a plain leaf
// push when [off] isn't actually an RF2 tag -- this is also how ordinary,
// non-grouped members reach the list. Ported from EXTRACT_RF2() in aluigi's
// arika.bms/endless_ocean.bms.
//
// RF2 header (16 bytes), all fields little-endian:
//   00h 3   "RF2" signature
//   03h 3   Type tag (unused here)
//   06h 2   Version
//   08h 3   Info size (sub-entry table size in bytes == n_sub * 20h)
//   0Bh 1   Unused
//   0Ch 4   Header size (unused here)
// Sub-entry (20h bytes): 14h-byte name, 4-byte size, 4-byte offset (relative
// to the RF2 tag's own offset), 4-byte flags (unused).
static void walk_rf2_or_leaf (
	arika_list_t *list, const u8 *data, uint size, uint off, u32 leaf_size, ccp name)
{
	if (off + 16 <= size && !memcmp (data + off, "RF2", 3))
	{
		const uint base_off = off;
		const u32 info_size = data[off + 8] | (u32)data[off + 9] << 8 | (u32)data[off + 10] << 16;
		const uint n_sub = info_size / 0x20;
		if (n_sub && n_sub <= 0x10000 && (u64)off + 16 + (u64)n_sub * 0x20 <= size)
		{
			const u8 *rec = data + off + 16;
			for (uint i = 0; i < n_sub; i++, rec += 0x20)
			{
				char raw[21];
				memcpy (raw, rec, 20);
				raw[20] = 0;
				char sub[300];
				snprintf (sub, sizeof (sub), "%s/%.20s", name, raw);
				const u32 ssize = rd_le32 (rec + 20);
				const u32 soff = rd_le32 (rec + 24) + base_off;
				if (ssize)
					walk_rf2_or_leaf (list, data, size, soff, ssize, sub);
			}
			return;
		}
	}
	if (off <= size && leaf_size <= size - off)
		arika_list_push (list, name, data + off, leaf_size);
}

enumError ExtractArika (nintendo_sarc_entry_t **out_entries, uint *out_n_entries,
	const u8 *info_data, uint info_size, const u8 *game_data, uint game_size)
{
	if (!out_entries || !out_n_entries || !info_data || info_size < 0x30 || !game_data)
		return EINVAL;

	u8 *info = MALLOC (info_size);
	if (!info)
		return ERR_CANT_CREATE;
	memcpy (info, info_data, info_size);
	DecryptArikaInfo (info, info_size);

	u32 sector_size = rd_le32 (info + 0x24);
	if (!sector_size)
		sector_size = 0x800;
	const u32 n_entries = rd_le32 (info + 0x2c);
	if (n_entries > 0x40000 || (u64)0x30 + (u64)n_entries * 0x30 > info_size)
	{
		FREE (info);
		return EINVAL;
	}

	arika_list_t list;
	memset (&list, 0, sizeof (list));

	const u8 *rec = info + 0x30;
	for (u32 i = 0; i < n_entries; i++, rec += 0x30)
	{
		char name[0x21];
		memcpy (name, rec, 0x20);
		name[0x20] = 0;
		if (!*name)
			continue; // unused directory slot

		const u32 zsize = rd_le32 (rec + 0x20);
		const u64 byte_off = (u64)rd_le32 (rec + 0x24) * sector_size;
		const u32 dsize = rd_le32 (rec + 0x2c);

		if (!zsize && !dsize)
			continue;
		if (byte_off > game_size)
			continue;
		const u32 avail = (u32)(game_size - byte_off);

		if (zsize == dsize)
		{
			// Stored raw (and possibly itself an RF2 group).
			if (zsize <= avail)
				walk_rf2_or_leaf (&list, game_data, game_size, (uint)byte_off, zsize, name);
			continue;
		}

		if (!dsize || dsize > NFMT_MAX_OUTPUT || zsize > avail)
			continue; // out-of-range entry: skip it, don't abort the archive

		const u8 *e = game_data + byte_off;
		uint hdr;
		if (zsize >= 4 && !memcmp (e, "ALZ1", 4))
			hdr = 4;
		else if (zsize >= 8 && !memcmp (e, "ZALZ", 4))
			hdr = 8; // older titles; same LZSS bitstream (see arika.bms)
		else
			continue; // unrecognised magic on a size-mismatched entry

		u8 *blob = MALLOC (dsize);
		if (!blob)
			continue;
		if (!DecodeALZ1 (blob, dsize, e + hdr, zsize - hdr))
			walk_rf2_or_leaf (&list, blob, dsize, 0, dsize, name);
		FREE (blob);
	}

	FREE (info);
	*out_entries = list.entries;
	*out_n_entries = list.n;
	return ERR_OK;
}

enumError CreateArika (u8 **dest_info, uint *dest_info_size, u8 **dest_game, uint *dest_game_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, ccp title, bool compress)
{
	if (!dest_info || !dest_info_size || !dest_game || !dest_game_size || !entries || !n_entries
		|| n_entries > 0x40000)
		return EINVAL;

	const u32 sector_size = 0x800;
	const uint info_size = 0x30 + n_entries * 0x30;
	u8 *info = CALLOC (1, info_size);
	if (!info)
		return ERR_CANT_CREATE;

	if (title)
		memcpy (info, title, strnlen (title, 0x10));

	wr_le32 (info + 0x24, sector_size);
	wr_le32 (info + 0x28, 1);
	wr_le32 (info + 0x2c, n_entries);

	u8 **payload = CALLOC (n_entries, sizeof (u8 *));
	uint *psize = CALLOC (n_entries, sizeof (uint));
	if (!payload || !psize)
	{
		FREE (info);
		FREE (payload);
		FREE (psize);
		return ERR_CANT_CREATE;
	}

	u64 game_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *src = entries[i].data;
		const uint ssize = entries[i].size;

		u8 *zdata = 0;
		uint zsize = 0;
		if (compress && ssize)
			EncodeALZ1 (&zdata, &zsize, src, ssize);

		if (zdata && zsize + 4 < ssize)
		{
			payload[i] = MALLOC (zsize + 4);
			memcpy (payload[i], "ALZ1", 4);
			memcpy (payload[i] + 4, zdata, zsize);
			psize[i] = zsize + 4;
		}
		else
		{
			payload[i] = ssize ? MALLOC (ssize) : 0;
			if (ssize)
				memcpy (payload[i], src, ssize);
			psize[i] = ssize;
		}
		FREE (zdata);

		const uint blocks = (psize[i] + sector_size - 1) / sector_size;
		game_size += (u64)blocks * sector_size;
	}

	if (game_size > NFMT_MAX_OUTPUT)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (payload[i]);
		FREE (payload);
		FREE (psize);
		FREE (info);
		return EFBIG;
	}

	u8 *game = CALLOC (1, game_size ? game_size : 1);
	if (!game)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (payload[i]);
		FREE (payload);
		FREE (psize);
		FREE (info);
		return ERR_CANT_CREATE;
	}

	uint cur = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *rec = info + 0x30 + i * 0x30;
		ccp nm = entries[i].name ? entries[i].name : "";
		strncpy ((char *)rec, nm, 0x1f);

		wr_le32 (rec + 0x20, psize[i]);
		wr_le32 (rec + 0x24, cur / sector_size);
		const uint blocks = (psize[i] + sector_size - 1) / sector_size;
		wr_le32 (rec + 0x28, blocks);
		wr_le32 (rec + 0x2c, entries[i].size);

		if (psize[i])
			memcpy (game + cur, payload[i], psize[i]);
		FREE (payload[i]);
		cur += blocks * sector_size;
	}
	FREE (payload);
	FREE (psize);

	EncryptArikaInfo (info, info_size);

	*dest_info = info;
	*dest_info_size = info_size;
	*dest_game = game;
	*dest_game_size = (uint)game_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// JARC / jCMP (Ganbarion archive & compression container, Wii / Wii U / 3DS)

void ResetJARC (jarc_t *jarc)
{
	if (!jarc)
		return;
	if (jarc->entries)
		FREE (jarc->entries);
	if (jarc->decomp_buffer)
		FREE (jarc->decomp_buffer);
	memset (jarc, 0, sizeof (*jarc));
}

enumError DecodeJCMP (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 16)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;

	if (memcmp (src, "jCMP", 4) && memcmp (src, "JCMP", 4))
		return EINVAL;

	const u8 comp_type = src[8];
	const u8 *payload = src + 16;
	uint payload_size = src_size >= 16 ? src_size - 16 : 0;

	// Check if header is 20 bytes (e.g. 0x14)
	if (src_size >= 20 && (src[16] == 0x78 || !memcmp (src + 16, "jARC", 4) || !memcmp (src + 16, "JARC", 4)))
	{
		payload = src + 16;
		payload_size = src_size - 16;
	}
	else if (src_size >= 24 && (src[20] == 0x78 || !memcmp (src + 20, "jARC", 4) || !memcmp (src + 20, "JARC", 4)))
	{
		payload = src + 20;
		payload_size = src_size - 20;
	}

	if (comp_type == 6 || (payload_size > 0 && payload[0] == 0x78))
	{
		uint out_cap = src_size * 4 + 65536;
		if (out_cap < 256 * 1024)
			out_cap = 256 * 1024;
		u8 *out = MALLOC (out_cap);
		if (!out)
			return ERR_CANT_CREATE;

		z_stream zs;
		memset (&zs, 0, sizeof (zs));
		zs.next_in = (Bytef *)payload;
		zs.avail_in = payload_size;

		int zrc = inflateInit2 (&zs, 15 + 32);
		if (zrc != Z_OK)
			zrc = inflateInit2 (&zs, -15);
		if (zrc != Z_OK)
		{
			FREE (out);
			return EINVAL;
		}

		zs.next_out = (Bytef *)out;
		zs.avail_out = out_cap;

		while (1)
		{
			int ret = inflate (&zs, Z_NO_FLUSH);
			if (ret == Z_STREAM_END)
				break;
			if (ret != Z_OK)
			{
				inflateEnd (&zs);
				if (payload != src + 20 && src_size >= 24)
				{
					memset (&zs, 0, sizeof (zs));
					zs.next_in = (Bytef *)(src + 20);
					zs.avail_in = src_size - 20;
					if (inflateInit2 (&zs, -15) == Z_OK || inflateInit2 (&zs, 15 + 32) == Z_OK)
					{
						zs.next_out = (Bytef *)out;
						zs.avail_out = out_cap;
						ret = inflate (&zs, Z_FINISH);
						inflateEnd (&zs);
						if (ret == Z_STREAM_END || ret == Z_OK)
							break;
					}
				}
				FREE (out);
				return EINVAL;
			}
			if (zs.avail_out == 0)
			{
				uint old_size = out_cap;
				out_cap *= 2;
				if (out_cap > 512 * 1024 * 1024)
				{
					inflateEnd (&zs);
					FREE (out);
					return ERR_FILE_TOO_BIG;
				}
				u8 *nout = REALLOC (out, out_cap);
				if (!nout)
				{
					inflateEnd (&zs);
					FREE (out);
					return ERR_CANT_CREATE;
				}
				out = nout;
				zs.next_out = (Bytef *)(out + old_size);
				zs.avail_out = out_cap - old_size;
			}
		}

		uint total_out = (uint)zs.total_out;
		inflateEnd (&zs);
		*dest = out;
		*dest_size = total_out;
		return ERR_OK;
	}
	else if (comp_type == 1 || comp_type == 0)
	{
		u8 *out = MALLOC (payload_size ? payload_size : 1);
		if (!out)
			return ERR_CANT_CREATE;
		if (payload_size)
			memcpy (out, payload, payload_size);
		*dest = out;
		*dest_size = payload_size;
		return ERR_OK;
	}
	else if (comp_type == 2)
	{
		return DecodeLZ10LZ11 (dest, dest_size, payload, payload_size);
	}

	return EINVAL;
}

static const char *infer_jarc_ext (const u8 *data, uint size)
{
	if (!data || size < 4)
		return "bin";
	if (!memcmp (data, "jMDL", 4) || !memcmp (data, "JMDL", 4))
		return "jmdl";
	if (!memcmp (data, "jTEX", 4) || !memcmp (data, "JTEX", 4))
		return "jtex";
	if (!memcmp (data, "jIMG", 4) || !memcmp (data, "JIMG", 4))
		return "jimg";
	if (!memcmp (data, "jMOT", 4) || !memcmp (data, "JMOT", 4))
		return "jmot";
	if (!memcmp (data, "jMSG", 4) || !memcmp (data, "JMSG", 4))
		return "jmsg";
	if (!memcmp (data, "jCLT", 4) || !memcmp (data, "JCLT", 4))
		return "jclt";
	if (!memcmp (data, "jEFC", 4) || !memcmp (data, "JEFC", 4))
		return "jefc";
	if (!memcmp (data, "jSCN", 4) || !memcmp (data, "JSCN", 4))
		return "jscn";
	if (!memcmp (data, "jWAT", 4) || !memcmp (data, "JWAT", 4))
		return "jwat";
	if (!memcmp (data, "jSND", 4) || !memcmp (data, "JSND", 4))
		return "jsnd";
	if (!memcmp (data, "jCMP", 4) || !memcmp (data, "JCMP", 4))
		return "jcmp";
	if (!memcmp (data, "jARC", 4) || !memcmp (data, "JARC", 4))
		return "jarc";
	if (!memcmp (data, "FRES", 4))
		return "bfres";
	if (!memcmp (data, "SARC", 4))
		return "sarc";
	if (!memcmp (data, "Yaz0", 4))
		return "szs";
	return "bin";
}

enumError ScanJARC (jarc_t *jarc, const u8 *data, size_t size)
{
	if (!jarc || !data || size < 16)
		return EINVAL;

	memset (jarc, 0, sizeof (*jarc));
	jarc->raw = data;
	jarc->raw_size = size;

	const u8 *buf = data;
	uint bsize = (uint)size;

	if (!memcmp (buf, "jCMP", 4) || !memcmp (buf, "JCMP", 4))
	{
		u8 *decomp = 0;
		uint dsize = 0;
		if (DecodeJCMP (&decomp, &dsize, buf, bsize) == ERR_OK && decomp && dsize >= 4)
		{
			jarc->decomp_buffer = decomp;
			jarc->decomp_size = dsize;
			buf = decomp;
			bsize = dsize;
		}
	}

	bool is_be = true;
	if (memcmp (buf, "jARC", 4) && memcmp (buf, "JARC", 4))
	{
		if (jarc->decomp_buffer)
		{
			jarc->entries = CALLOC (1, sizeof (jarc_entry_t));
			if (!jarc->entries)
				return ERR_CANT_CREATE;
			jarc->n_entries = 1;
			jarc->entries[0].data = buf;
			jarc->entries[0].size = bsize;
			jarc->entries[0].offset = 0;
			const char *ext = infer_jarc_ext (buf, bsize);
			snprintf (jarc->entries[0].ext, sizeof (jarc->entries[0].ext), "%s", ext);
			snprintf (jarc->entries[0].name, sizeof (jarc->entries[0].name), "file_0000.%s", ext);
			return ERR_OK;
		}
		return EINVAL;
	}

	u32 fcount_be = bsize >= 12 ? rd_be32 (buf + 8) : 0;
	u32 fcount_le = bsize >= 12 ? rd_le32 (buf + 8) : 0;
	uint n_files = 0;
	if (fcount_be > 0 && fcount_be < 50000 && (fcount_be * 8 <= bsize))
	{
		is_be = true;
		n_files = fcount_be;
	}
	else if (fcount_le > 0 && fcount_le < 50000 && (fcount_le * 8 <= bsize))
	{
		is_be = false;
		n_files = fcount_le;
	}
	else
	{
		u32 f4_be = bsize >= 8 ? rd_be32 (buf + 4) : 0;
		u32 f4_le = bsize >= 8 ? rd_le32 (buf + 4) : 0;
		if (f4_be > 0 && f4_be < 50000 && (f4_be * 8 <= bsize))
		{
			is_be = true;
			n_files = f4_be;
		}
		else if (f4_le > 0 && f4_le < 50000 && (f4_le * 8 <= bsize))
		{
			is_be = false;
			n_files = f4_le;
		}
	}
	jarc->is_big_endian = is_be;

	uint toc_offset = 0x10;
	if (bsize >= 0x20)
	{
		u32 t_off = is_be ? rd_be32 (buf + 12) : rd_le32 (buf + 12);
		if (t_off >= 0x10 && t_off < bsize)
			toc_offset = t_off;
	}

	if (n_files > 0 && (u64)toc_offset + (u64)n_files * 8 <= bsize)
	{
		jarc->entries = CALLOC (n_files, sizeof (jarc_entry_t));
		if (!jarc->entries)
			return ERR_CANT_CREATE;
		jarc->n_entries = n_files;

		for (uint i = 0; i < n_files; i++)
		{
			const u8 *entry_ptr = buf + toc_offset + i * 16;
			u32 off = 0, len = 0;
			if (toc_offset + (i + 1) * 16 <= bsize)
			{
				off = is_be ? rd_be32 (entry_ptr) : rd_le32 (entry_ptr);
				len = is_be ? rd_be32 (entry_ptr + 4) : rd_le32 (entry_ptr + 4);
			}
			else
			{
				entry_ptr = buf + toc_offset + i * 8;
				off = is_be ? rd_be32 (entry_ptr) : rd_le32 (entry_ptr);
				len = is_be ? rd_be32 (entry_ptr + 4) : rd_le32 (entry_ptr + 4);
			}

			if (off < bsize && (u64)off + len <= bsize && len > 0)
			{
				jarc->entries[i].data = buf + off;
				jarc->entries[i].size = len;
				jarc->entries[i].offset = off;
				const char *ext = infer_jarc_ext (buf + off, len);
				snprintf (jarc->entries[i].ext, sizeof (jarc->entries[i].ext), "%s", ext);
				snprintf (jarc->entries[i].name, sizeof (jarc->entries[i].name), "file_%04u.%s", i, ext);
			}
			else
			{
				jarc->entries[i].offset = off;
				jarc->entries[i].size = 0;
				snprintf (jarc->entries[i].name, sizeof (jarc->entries[i].name), "file_%04u.bin", i);
			}
		}
		return ERR_OK;
	}

	uint max_chunks = 2048;
	jarc_entry_t *chunks = CALLOC (max_chunks, sizeof (jarc_entry_t));
	if (!chunks)
		return ERR_CANT_CREATE;
	uint n_chunks = 0;

	for (uint pos = 4; pos + 8 <= bsize; pos += 4)
	{
		const u8 *p = buf + pos;
		if (!memcmp (p, "jMDL", 4) || !memcmp (p, "JMDL", 4)
			|| !memcmp (p, "jTEX", 4) || !memcmp (p, "JTEX", 4)
			|| !memcmp (p, "jIMG", 4) || !memcmp (p, "JIMG", 4)
			|| !memcmp (p, "jMOT", 4) || !memcmp (p, "JMOT", 4)
			|| !memcmp (p, "jMSG", 4) || !memcmp (p, "JMSG", 4)
			|| !memcmp (p, "jCLT", 4) || !memcmp (p, "JCLT", 4)
			|| !memcmp (p, "jEFC", 4) || !memcmp (p, "JEFC", 4)
			|| !memcmp (p, "jSCN", 4) || !memcmp (p, "JSCN", 4)
			|| !memcmp (p, "jWAT", 4) || !memcmp (p, "JWAT", 4)
			|| !memcmp (p, "jSND", 4) || !memcmp (p, "JSND", 4)
			|| !memcmp (p, "jCMP", 4) || !memcmp (p, "JCMP", 4))
		{
			if (n_chunks > 0 && chunks[n_chunks - 1].size == 0)
				chunks[n_chunks - 1].size = pos - chunks[n_chunks - 1].offset;

			if (n_chunks < max_chunks)
			{
				chunks[n_chunks].offset = pos;
				chunks[n_chunks].data = p;
				chunks[n_chunks].size = 0;
				const char *ext = infer_jarc_ext (p, bsize - pos);
				snprintf (chunks[n_chunks].ext, sizeof (chunks[n_chunks].ext), "%s", ext);
				snprintf (chunks[n_chunks].name, sizeof (chunks[n_chunks].name), "file_%04u.%s", n_chunks, ext);
				n_chunks++;
			}
		}
	}

	if (n_chunks > 0)
	{
		if (chunks[n_chunks - 1].size == 0)
			chunks[n_chunks - 1].size = bsize - chunks[n_chunks - 1].offset;
		jarc->entries = chunks;
		jarc->n_entries = n_chunks;
		return ERR_OK;
	}

	FREE (chunks);
	return EINVAL;
}

//-----------------------------------------------------------------------------
///////////////	  QuickBMS-derived flat archive ports		///////////////
//-----------------------------------------------------------------------------

void ResetOwnedEntries (nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!entries)
		return;
	for (uint i = 0; i < n_entries; i++)
	{
		FREE ((void *)entries[i].name);
		FREE ((void *)entries[i].data);
	}
	FREE (entries);
}

// Append one entry with owned copies of both name and payload.
static bool owned_entry_add (
	nintendo_sarc_entry_t *entries, uint idx, ccp name, const u8 *data, uint size)
{
	char *nm = MALLOC (strlen (name) + 1);
	u8 *pl = size ? MALLOC (size) : CALLOC (1, 1);
	if (!nm || !pl)
	{
		FREE (nm);
		FREE (pl);
		return false;
	}
	strcpy (nm, name);
	if (size)
		memcpy (pl, data, size);
	entries[idx].name = nm;
	entries[idx].data = pl;
	entries[idx].size = size;
	return true;
}

// Reject names that would escape the extraction directory or contain
// characters no real member name uses.  Kept local so each scanner can drop
// a hostile record instead of the caller having to re-validate.
static bool owned_name_ok (ccp name)
{
	if (!name || !*name || *name == '/')
		return false;
	if (strstr (name, "..") || strchr (name, '\\') || strchr (name, ':'))
		return false;
	for (ccp p = name; *p; p++)
		if ((u8)*p < 0x20 || (u8)*p == 0x7f)
			return false;
	return strlen (name) < 240;
}

//-----------------------------------------------------------------------------
///////////////	    Star Fox Zero DAT (Wii U, "DAT\0")		///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public QuickBMS script (star_fox_zero_dat.bms),
// all big-endian:
//
//   'DAT\0'
//   u32 files
//   u32 offsets_off      -> u32 offset[files]      (absolute into the file)
//   u32 exts_off         -> per-file type/extension string
//   u32 names_off        -> u32 name_stride, then per-file name string
//   u32 sizes_off        -> u32 size[files]
//   u32 hashmap_off      -> hash lookup table, not needed for extraction
//
// The BMS reads the extension and name sections as back-to-back
// NUL-terminated strings.  That is only *incidentally* right: this is
// PlatinumGames' DAT container, whose two string sections are fixed-stride
// record arrays -- 4 bytes per extension, and `name_stride` bytes per name
// (that is exactly what the script's unexplained `get DUMMY long` before the
// names is).  Sequential string reads happen to land correctly whenever the
// stored strings fill their record, which holds for the 3-character
// extensions this game uses but not for names.  So both sections are read
// here as strided records, with a packed-string fallback if the strided
// interpretation does not fit inside the section.
//
// Entry names are emitted as "<ext>/<name>", matching the script.
enumError ScanSFZDAT (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 32 || memcmp (data, "DAT\0", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 files = rd_be32 (data + 4);
	const u32 off_offsets = rd_be32 (data + 8);
	const u32 off_exts = rd_be32 (data + 12);
	const u32 off_names = rd_be32 (data + 16);
	const u32 off_sizes = rd_be32 (data + 20);

	if (!files || files > 0x40000)
		return EINVAL;
	if ((u64)off_offsets + (u64)files * 4 > size)
		return EINVAL;
	if ((u64)off_sizes + (u64)files * 4 > size)
		return EINVAL;
	if (off_exts >= size || (u64)off_names + 4 > size)
		return EINVAL;

	// Names are a fixed-stride array preceded by the stride.  Fall back to
	// packed NUL-terminated strings when the strided array does not fit.
	u32 name_stride = rd_be32 (data + off_names);
	const u32 names_base = off_names + 4;
	if (!name_stride || name_stride > 256
		|| (u64)names_base + (u64)files * name_stride > size)
		name_stride = 0;

	// Extensions are 4-byte records in every known file; same fallback.
	u32 ext_stride = 4;
	if ((u64)off_exts + (u64)files * 4 > size)
		ext_stride = 0;

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	uint ext_pos = off_exts, name_pos = names_base;
	for (u32 i = 0; i < files; i++)
	{
		char ext[64] = "", name[256] = "";

		if (ext_stride)
		{
			const uint base = off_exts + i * ext_stride;
			uint len = 0;
			while (len < ext_stride && data[base + len])
				len++;
			memcpy (ext, data + base, len);
			ext[len] = 0;
		}
		else
		{
			uint start = ext_pos;
			while (ext_pos < size && data[ext_pos])
				ext_pos++;
			if (ext_pos >= size)
				break;
			uint len = ext_pos - start;
			if (len >= sizeof (ext))
				len = sizeof (ext) - 1;
			memcpy (ext, data + start, len);
			ext[len] = 0;
			ext_pos++;
		}

		if (name_stride)
		{
			const uint base = names_base + i * name_stride;
			uint len = 0;
			while (len < name_stride && data[base + len])
				len++;
			if (len >= sizeof (name))
				len = sizeof (name) - 1;
			memcpy (name, data + base, len);
			name[len] = 0;
		}
		else
		{
			uint start = name_pos;
			while (name_pos < size && data[name_pos])
				name_pos++;
			if (name_pos >= size)
				break;
			uint len = name_pos - start;
			if (len >= sizeof (name))
				len = sizeof (name) - 1;
			memcpy (name, data + start, len);
			name[len] = 0;
			name_pos++;
		}

		const u32 foff = rd_be32 (data + off_offsets + i * 4);
		const u32 fsize = rd_be32 (data + off_sizes + i * 4);
		if ((u64)foff + fsize > size)
			continue;

		char full[352];
		if (*ext && owned_name_ok (ext))
			snprintf (full, sizeof (full), "%s/%s", ext, name);
		else
			snprintf (full, sizeof (full), "%s", name);
		if (!owned_name_ok (full))
			snprintf (full, sizeof (full), "%04u.bin", i);

		if (!owned_entry_add (out, n, full, data + foff, fsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

// Inverse of ScanSFZDAT, writing the strided form of both string sections.
//
// The sixth header word points at PlatinumGames' hash-lookup section, which
// this tool never reads (extraction walks the name array directly) and whose
// hash function has not been recovered here.  Rather than emit a bogus one,
// the section is written as a well-formed *empty* map: the four offsets are
// present and internally consistent, and the bucket table is a single
// all-ones (== "no entry") slot.  Archives rebuilt by this function
// therefore round-trip through this tool, but are NOT asserted to satisfy a
// game-side hash lookup.
enumError CreateSFZDAT (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x40000)
		return EINVAL;

	ccp *ext = MALLOC (n_entries * sizeof (ccp));
	ccp *name = MALLOC (n_entries * sizeof (ccp));
	char *extbuf = MALLOC (n_entries * 8);
	if (!ext || !name || !extbuf)
	{
		FREE (ext);
		FREE (name);
		FREE (extbuf);
		return ERR_CANT_CREATE;
	}

	uint name_stride = 1;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp n0 = entries[i].name ? entries[i].name : "";
		ccp slash = strrchr (n0, '/');
		char *e = extbuf + i * 8;
		if (slash)
		{
			uint len = (uint)(slash - n0);
			if (len > 7)
				len = 7;
			memcpy (e, n0, len);
			e[len] = 0;
			name[i] = slash + 1;
		}
		else
		{
			// No "<ext>/" prefix: derive the type from the suffix.
			ccp dot = strrchr (n0, '.');
			uint len = dot ? (uint)strlen (dot + 1) : 0;
			if (len > 7)
				len = 7;
			if (len)
				memcpy (e, dot + 1, len);
			e[len] = 0;
			name[i] = n0;
		}
		ext[i] = e;
		const uint nl = (uint)strlen (name[i]) + 1;
		if (nl > name_stride)
			name_stride = nl;
	}
	name_stride = (name_stride + 3) & ~3u;
	if (name_stride > 256)
	{
		FREE (ext);
		FREE (name);
		FREE (extbuf);
		return EINVAL;
	}

	const u32 off_offsets = 32;
	const u32 off_exts = off_offsets + n_entries * 4;
	const u32 off_names = off_exts + n_entries * 4;
	const u32 off_sizes = off_names + 4 + n_entries * name_stride;
	const u32 off_hash = off_sizes + n_entries * 4;
	const u32 hash_size = 16 + 4; // header + one empty bucket slot (padded)
	u64 total = (u64)off_hash + hash_size;
	total = (total + 15) & ~(u64)15;
	const u64 data_start = total;
	for (uint i = 0; i < n_entries; i++)
	{
		total += entries[i].size;
		total = (total + 15) & ~(u64)15;
	}
	if (total > NFMT_MAX_OUTPUT)
	{
		FREE (ext);
		FREE (name);
		FREE (extbuf);
		return EFBIG;
	}

	u8 *out = CALLOC (1, (size_t)total);
	if (!out)
	{
		FREE (ext);
		FREE (name);
		FREE (extbuf);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "DAT\0", 4);
	wr_be32 (out + 4, n_entries);
	wr_be32 (out + 8, off_offsets);
	wr_be32 (out + 12, off_exts);
	wr_be32 (out + 16, off_names);
	wr_be32 (out + 20, off_sizes);
	wr_be32 (out + 24, off_hash);
	wr_be32 (out + 28, 0);

	wr_be32 (out + off_names, name_stride);
	u64 cur = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_be32 (out + off_offsets + i * 4, (u32)cur);
		wr_be32 (out + off_sizes + i * 4, entries[i].size);
		uint el = (uint)strlen (ext[i]);
		if (el > 3)
			el = 3;
		memcpy (out + off_exts + i * 4, ext[i], el);
		uint nl = (uint)strlen (name[i]);
		if (nl > name_stride - 1)
			nl = name_stride - 1;
		memcpy (out + off_names + 4 + i * name_stride, name[i], nl);
		if (entries[i].size)
			memcpy (out + cur, entries[i].data, entries[i].size);
		cur += entries[i].size;
		cur = (cur + 15) & ~(u64)15;
	}

	// Empty hash map: preHashShift, then three section offsets relative to
	// the map, then one bucket slot holding -1 ("empty").
	wr_be32 (out + off_hash, 31);
	wr_be32 (out + off_hash + 4, 16);
	wr_be32 (out + off_hash + 8, 20);
	wr_be32 (out + off_hash + 12, 20);
	wr_be16 (out + off_hash + 16, 0xffff);

	FREE (ext);
	FREE (name);
	FREE (extbuf);
	*dest = out;
	*dest_size = (uint)total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	    BG4 (Mario & Luigi: Paper Jam, 3DS)		///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public QuickBMS script (mario_luigi_paper.bms),
// all little-endian:
//
//   'BG4\0', u16 dummy, u16 files, u32 data_off, u32 dummy   <- 16-byte header
//   entry[files]: u32 offset, u32 size, u32 crc, u16 name_off (14 bytes each)
//   names: NUL-terminated strings, name_off is relative to the end of the
//          entry table
//
// offset==0 marks an unused slot.  Bit 31 of offset marks a BLZ ("backward
// LZSS", the DS/3DS ARM-binary compression) member; the flag is masked off
// and the payload decompressed with this tool's existing DecodeBLZ.
enumError ScanBG4 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 16 || memcmp (data, "BG4\0", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const uint files = rd_le16 (data + 6);
	if (!files)
		return EINVAL;
	const u64 tab = 16;
	const u64 names_off = tab + (u64)files * 14;
	if (names_off > size)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint i = 0; i < files; i++)
	{
		const u8 *e = data + tab + i * 14;
		u32 off = rd_le32 (e);
		const u32 fsize = rd_le32 (e + 4);
		const uint noff = rd_le16 (e + 12);
		if (!off)
			continue;
		const bool compressed = (off & 0x80000000u) != 0;
		off &= 0x7fffffffu;
		if ((u64)off + fsize > size)
			continue;

		char name[256];
		const u64 nabs = names_off + noff;
		if (nabs >= size)
			continue;
		uint len = 0;
		while (nabs + len < size && data[nabs + len] && len < sizeof (name) - 1)
			len++;
		memcpy (name, data + nabs, len);
		name[len] = 0;
		if (!owned_name_ok (name))
			snprintf (name, sizeof (name), "%04u.bin", i);

		bool ok;
		if (compressed)
		{
			u8 *dec = 0;
			uint dec_size = 0;
			if (DecodeBLZ (&dec, &dec_size, data + off, fsize) != ERR_OK || !dec)
			{
				// Keep the raw member rather than losing it entirely.
				ok = owned_entry_add (out, n, name, data + off, fsize);
			}
			else
			{
				ok = owned_entry_add (out, n, name, dec, dec_size);
				FREE (dec);
			}
		}
		else
			ok = owned_entry_add (out, n, name, data + off, fsize);

		if (!ok)
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	   Hyrule Warriors Legends (3DS) .idx/.bin	///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public hyrule_warriors_legends.bms: the .idx
// is nothing but an array of {u32 size, u32 offset} little-endian records
// addressing the sibling .bin, with size==0 marking a hole.  There is no
// magic, no header and no name table, so the caller must gate on the
// filename pair; every structural constraint that *can* be checked is
// checked here (record alignment, in-bounds members, at least one live
// entry) so a same-named but unrelated .idx cannot produce garbage output.
enumError ScanHWLegends (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *idx,
	uint idx_size, const u8 *bin, uint bin_size)
{
	if (!entries || !n_entries || !idx || !bin)
		return EINVAL;
	if (!idx_size || idx_size % 8 || idx_size > 8u * 0x100000)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const uint files = idx_size / 8;
	uint live = 0;
	for (uint i = 0; i < files; i++)
	{
		const u32 fsize = rd_le32 (idx + i * 8);
		const u32 foff = rd_le32 (idx + i * 8 + 4);
		if (!fsize)
			continue;
		if ((u64)foff + fsize > bin_size)
			return EINVAL; // an out-of-range member means this is not an .idx
		live++;
	}
	if (!live)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (live, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint i = 0; i < files; i++)
	{
		const u32 fsize = rd_le32 (idx + i * 8);
		const u32 foff = rd_le32 (idx + i * 8 + 4);
		if (!fsize)
			continue;
		char name[32];
		snprintf (name, sizeof (name), "%05u.bin", i);
		if (!owned_entry_add (out, n, name, bin + foff, fsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	  Xenoblade Chronicles 3D "cram" .arc (3DS)	///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public xenoblade_arc.bms, all little-endian:
//
//   'cram', u32 files, u32 dummy (0x80), u32 names_off
//   entry[files]: u32 crc, char type[4], u32 offset, u32 size
//   u32 name_off[files]      -- each relative to names_off
//   name blob (NUL-terminated strings)
//
// Flat, named and uncompressed.  Creation is not implemented: the per-entry
// name checksum's algorithm is not recovered and could not be confirmed
// against a retail sample, so writing a file with fabricated checksums would
// be a guess.
enumError ScanCramARC (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 16 || memcmp (data, "cram", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 files = rd_le32 (data + 4);
	const u32 names_off = rd_le32 (data + 12);
	if (!files || files > 0x100000)
		return EINVAL;
	const u64 tab = 16;
	if (tab + (u64)files * 16 + (u64)files * 4 > size)
		return EINVAL;
	if (names_off >= size)
		return EINVAL;
	const u64 nametab = tab + (u64)files * 16;

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (u32 i = 0; i < files; i++)
	{
		const u8 *e = data + tab + (u64)i * 16;
		const u32 foff = rd_le32 (e + 8);
		const u32 fsize = rd_le32 (e + 12);
		const u32 noff = rd_le32 (data + nametab + (u64)i * 4);
		if ((u64)foff + fsize > size)
			continue;

		char name[256];
		const u64 nabs = (u64)names_off + noff;
		uint len = 0;
		if (nabs < size)
		{
			while (nabs + len < size && data[nabs + len] && len < sizeof (name) - 1)
				len++;
			memcpy (name, data + nabs, len);
		}
		name[len] = 0;
		if (!owned_name_ok (name))
			snprintf (name, sizeof (name), "%05u.bin", i);

		if (!owned_entry_add (out, n, name, data + foff, fsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	   Mii Maker "SA01" / amiibo "CA01"		///////////////
//-----------------------------------------------------------------------------

// Peel the outer wrapper off a Mii Maker / amiibo container.  Three accepted
// shapes, all of which must yield an inner "SA01"/"CA01" image -- that
// requirement is what makes the heuristic first form safe:
//
//   * bare "SA01"/"CA01": returned as a copy
//   * "ZCMP": 0x80-byte header, zlib payload at 0x80 (amiibo.bms)
//   * big-endian u32 uncompressed size followed by a zlib stream at offset 4
//     (mii_maker.bms)
enumError DecodeSA01Container (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 8)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;

	if (!memcmp (src, "SA01", 4) || !memcmp (src, "CA01", 4))
	{
		u8 *copy = MALLOC (src_size);
		if (!copy)
			return ERR_CANT_CREATE;
		memcpy (copy, src, src_size);
		*dest = copy;
		*dest_size = src_size;
		return ERR_OK;
	}

	uint payload = 0;
	if (!memcmp (src, "ZCMP", 4))
	{
		if (src_size <= 0x80)
			return EINVAL;
		payload = 0x80;
	}
	else if (src[4] == 0x78 && ((u32)src[4] << 8 | src[5]) % 31 == 0)
	{
		// Mii Maker's wrapper is just a big-endian uncompressed size in
		// front of a raw zlib stream.  Before paying for a full inflate on
		// every file the tree walker hands us, require that size to be
		// self-consistent: non-zero, within this tool's output cap, and at
		// least as large as the compressed remainder (zlib never expands a
		// real payload below its own input size here).
		const u32 raw_size = rd_be32 (src);
		if (!raw_size || raw_size > NFMT_MAX_OUTPUT || raw_size + 4 < src_size)
			return EINVAL;
		payload = 4;
	}
	else
		return EINVAL;

	u8 *dec = 0;
	uint dec_size = 0;
	if (DecodeZlibGrow (&dec, &dec_size, src + payload, src_size - payload) != ERR_OK || !dec)
	{
		FREE (dec);
		return EINVAL;
	}
	if (dec_size < 12 || (memcmp (dec, "SA01", 4) && memcmp (dec, "CA01", 4)))
	{
		FREE (dec);
		return EINVAL;
	}
	*dest = dec;
	*dest_size = dec_size;
	return ERR_OK;
}

// Inner archive, layout ported from aluigi's public mii_maker.bms /
// amiibo.bms:
//
//   'SA01' or 'CA01', u32 files, u32 base_off
//   u32 offset[files]   (each relative to base_off)
//   u32 size[files]
//   char name[files][0x80]   -- SA01 only; CA01 has no name section
//
// The scripts read Mii Maker big-endian but `endian guess` the amiibo one,
// so the word order is recovered here from the file count instead of assumed.
enumError ScanSA01 (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 12)
		return EINVAL;
	const bool named = !memcmp (data, "SA01", 4);
	if (!named && memcmp (data, "CA01", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	// "endian guess": pick whichever byte order gives a sane file count.
	const u32 be = rd_be32 (data + 4), le = rd_le32 (data + 4);
	bool big;
	if (be && be <= 0x10000)
		big = true;
	else if (le && le <= 0x10000)
		big = false;
	else
		return EINVAL;

	const u32 files = big ? be : le;
	const u32 base_off = big ? rd_be32 (data + 8) : rd_le32 (data + 8);
	const u32 rec = named ? 0x80 : 0;
	const u64 off_tab = 12;
	const u64 size_tab = off_tab + (u64)files * 4;
	const u64 name_tab = size_tab + (u64)files * 4;
	if (name_tab + (u64)files * rec > size)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (u32 i = 0; i < files; i++)
	{
		const u8 *op = data + off_tab + (u64)i * 4;
		const u8 *sp = data + size_tab + (u64)i * 4;
		const u64 foff = (u64)(big ? rd_be32 (op) : rd_le32 (op)) + base_off;
		const u32 fsize = big ? rd_be32 (sp) : rd_le32 (sp);
		if (foff + fsize > size)
			continue;

		char name[0x88];
		if (named)
		{
			const u8 *np = data + name_tab + (u64)i * rec;
			uint len = 0;
			while (len < rec && np[len])
				len++;
			memcpy (name, np, len);
			name[len] = 0;
			if (!owned_name_ok (name))
				snprintf (name, sizeof (name), "%05u.bin", i);
		}
		else
			snprintf (name, sizeof (name), "%05u.sar", i);

		if (!owned_entry_add (out, n, name, data + foff, fsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////	   Metroid: Samus Returns (3DS) archive		///////////////
//-----------------------------------------------------------------------------

// Layout ported from aluigi's public metroid_sr_3ds.bms, all little-endian:
//
//   u32 info_size, u32 data_size, u32 files
//   entry[files]: u32 crc, u32 offset, u32 end_offset
//
// There is no magic and no name table, so detection is purely structural.
// Every constraint the format implies is enforced, and all of them must hold
// at once: the declared info section must be exactly the size of the header
// plus the table, info+data must account for the whole file, the members
// must start where the table ends, run in non-decreasing order, and stay
// in-bounds.  A file that satisfies all of that while not being this format
// is vanishingly unlikely, but callers should still run this scanner last.
enumError ScanMetroidSR (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 24)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 info_size = rd_le32 (data);
	const u32 data_size = rd_le32 (data + 4);
	const u32 files = rd_le32 (data + 8);

	if (!files || files > 0x100000)
		return EINVAL;
	if ((u64)12 + (u64)files * 12 != info_size)
		return EINVAL;
	if ((u64)info_size + data_size != size)
		return EINVAL;

	u32 prev = info_size;
	for (u32 i = 0; i < files; i++)
	{
		const u8 *e = data + 12 + (u64)i * 12;
		const u32 off = rd_le32 (e + 4);
		const u32 end = rd_le32 (e + 8);
		if (off < info_size || end < off || end > size || off < prev)
			return EINVAL;
		prev = end;
	}
	if (rd_le32 (data + 12 + 4) != info_size)
		return EINVAL; // members must begin immediately after the table

	nintendo_sarc_entry_t *out = CALLOC (files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	for (u32 i = 0; i < files; i++)
	{
		const u8 *e = data + 12 + (u64)i * 12;
		const u32 off = rd_le32 (e + 4);
		const u32 end = rd_le32 (e + 8);
		char name[32];
		snprintf (name, sizeof (name), "%05u.bin", i);
		if (!owned_entry_add (out, i, name, data + off, end - off))
		{
			ResetOwnedEntries (out, i);
			return ERR_CANT_CREATE;
		}
	}
	*entries = out;
	*n_entries = files;
	return ERR_OK;
}

// ============================================================
// Mario vs. Donkey Kong custom deflate (MVDK) decompressor
// Ported from Garhoogin/NitroPaint compression.c (MIT licence)
// Only the decompressor and validator are ported; encoder stubs
// out to NULL for now (compress support can be added later).
// ============================================================
// Temporarily restore standard allocators overridden by dclib.
#undef free
#undef calloc
#undef malloc
#undef realloc
#include <stdint.h>
#ifndef min
#  define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#  define max(a,b) ((a)>(b)?(a):(b))
#endif


static void *CxiShrink(void *block, unsigned int to) {
	void *newblock = realloc(block, to);
	if (newblock == NULL) {
		//alloc fail, return old block
		return block;
	}
	return newblock;
}

static uint32_t CxiBitReverse32(uint32_t x) {
	x = ((x & 0xFFFF0000) >> 16) | ((x & ~0xFFFF0000) << 16);
	x = ((x & 0xFF00FF00) >> 8) | ((x & ~0xFF00FF00) << 8);
	x = ((x & 0xF0F0F0F0) >> 4) | ((x & ~0xF0F0F0F0) << 4);
	x = ((x & 0xCCCCCCCC) >> 2) | ((x & ~0xCCCCCCCC) << 2);
	x = ((x & 0xAAAAAAAA) >> 1) | ((x & ~0xAAAAAAAA) << 1);
	return x;
}

static unsigned char CxiBitReverse8(unsigned char x) {
	return CxiBitReverse32(x << 24);
}

static uint32_t CxiByteSwap(uint32_t x) {
	return ((x & 0xFF000000) >> 24)
		| ((x & 0x00FF0000) >> 8)
		| ((x & 0x0000FF00) << 8)
		| ((x & 0x000000FF) << 24);
}

typedef struct CxiLzNode_ {
	uint32_t distance : 15;    // distance of node if reference
	uint32_t length   : 17;    // length of node
	uint32_t weight;           // weight of node
} CxiLzNode;

//struct for representing tokenized LZ data
typedef struct CxiLzToken_ {
	uint8_t isReference;
	union {
		uint8_t symbol;
		struct {
			int16_t length;
			int16_t distance;
		};
	};
} CxiLzToken;

//struct for keeping track of LZ sliding window
typedef struct CxiLzState_ {
	const unsigned char *buffer;
	unsigned int size;
	unsigned int pos;
	unsigned int minLength;
	unsigned int maxLength;
	unsigned int minDistance;
	unsigned int maxDistance;
	unsigned int symLookup[512];
	unsigned int *chain;
} CxiLzState;

static unsigned int CxiLzHash3(const unsigned char *p) {
	unsigned char c0 = p[0];         // A
	unsigned char c1 = p[0] ^ p[1];  // A ^ B
	unsigned char c2 = p[0] ^ p[2];  // (A ^ B) ^ (B ^ C)
	return (c0 ^ (c1 << 1) ^ (c2 << 2) ^ (c2 >> 7)) & 0x1FF;
}

static void CxiLzStateInit(CxiLzState *state, const unsigned char *buffer, unsigned int size, unsigned int minLength, unsigned int maxLength, unsigned int minDistance, unsigned int maxDistance) {
	state->buffer = buffer;
	state->size = size;
	state->pos = 0;
	state->minLength = minLength;
	state->maxLength = maxLength;
	state->minDistance = minDistance;
	state->maxDistance = maxDistance;

	for (unsigned int i = 0; i < 512; i++) {
		//init symbol lookup to empty
		state->symLookup[i] = UINT_MAX;
	}

	state->chain = (unsigned int *) calloc(state->maxDistance, sizeof(unsigned int));
	for (unsigned int i = 0; i < state->maxDistance; i++) {
		state->chain[i] = UINT_MAX;
	}
}

static void CxiLzStateFree(CxiLzState *state) {
	free(state->chain);
}

static unsigned int CxiLzStateGetChainIndex(CxiLzState *state, unsigned int index) {
	return (state->pos - index) % state->maxDistance;
}

static unsigned int CxiLzStateGetChain(CxiLzState *state, int index) {
	unsigned int chainIndex = CxiLzStateGetChainIndex(state, index);

	return state->chain[chainIndex];
}

static void CxiLzStatePutChain(CxiLzState *state, unsigned int index, unsigned int data) {
	unsigned int chainIndex = CxiLzStateGetChainIndex(state, index);

	state->chain[chainIndex] = data;
}

static void CxiLzStateSlideByte(CxiLzState *state) {
	if (state->pos >= state->size) return; // cannot slide

	//only update search structures when we have enough space left to necessitate searching.
	if ((state->size - state->pos) >= 3) {
		//fetch next 3 bytes' hash
		unsigned int next = CxiLzHash3(state->buffer + state->pos);

		//get the distance back to the next byte before sliding. If it exists in the window,
		//we'll have nextDelta less than UINT_MAX. We'll take this first occurrence and it 
		//becomes the offset from the current byte. Bear in mind the chain is 0-indexed starting
		//at a distance of 1. 
		unsigned int nextDelta = state->symLookup[next];
		if (nextDelta != UINT_MAX) {
			nextDelta++;
			if (nextDelta >= state->maxDistance) {
				nextDelta = UINT_MAX;
			}
		}
		CxiLzStatePutChain(state, 0, nextDelta);

		//increment symbol lookups
		for (int i = 0; i < 512; i++) {
			if (state->symLookup[i] != UINT_MAX) {
				state->symLookup[i]++;
				if (state->symLookup[i] > state->maxDistance) state->symLookup[i] = UINT_MAX;
			}
		}
		state->symLookup[next] = 0; // update entry for the current byte to the start of the chain
	}

	state->pos++;
}

static void CxiLzStateSlide(CxiLzState *state, unsigned int nSlide) {
	while (nSlide--) CxiLzStateSlideByte(state);
}

static unsigned int CxiCompareMemory(const unsigned char *b1, const unsigned char *b2, unsigned int nMax) {
	//compare nAbsoluteMax bytes, do not perform any looping.
	unsigned int nSame = 0;
	while (nMax > 0) {
		if (*(b1++) != *(b2++)) break;
		nMax--;
		nSame++;
	}
	return nSame;
}

static int CxiLzConfirmMatch(const unsigned char *buffer, unsigned int size, unsigned int pos, unsigned int distance, unsigned int length) {
	(void) size;

	//compare string match
	return memcmp(buffer + pos, buffer + pos - distance, length) == 0;
}

static unsigned int CxiLzSearch(CxiLzState *state, unsigned int *pDistance) {
	unsigned int nBytesLeft = state->size - state->pos;
	if (nBytesLeft < 3 || nBytesLeft < state->minLength) {
		*pDistance = 0;
		return 1;
	}

	unsigned int firstMatch = state->symLookup[CxiLzHash3(state->buffer + state->pos)];
	if (firstMatch == UINT_MAX) {
		//return byte literal
		*pDistance = 0;
		return 1;
	}

	unsigned int distance = firstMatch + 1;
	unsigned int bestLength = 1, bestDistance = 0;

	unsigned int nMaxCompare = state->maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//search backwards
	const unsigned char *curp = state->buffer + state->pos;
	while (distance <= state->maxDistance) {
		//check only if distance is at least minDistance
		if (distance >= state->minDistance) {
			unsigned int matchLen = CxiCompareMemory(curp - distance, curp, nMaxCompare);

			if (matchLen > bestLength) {
				bestLength = matchLen;
				bestDistance = distance;
				if (bestLength == nMaxCompare) break;
			}
		}

		if (distance == state->maxDistance) break;
		unsigned int next = CxiLzStateGetChain(state, distance);
		if (next == UINT_MAX) break;
		distance += next;
	}

	if (bestLength < state->minLength) {
		bestLength = 1;
		distance = 0;
	}
	*pDistance = bestDistance;
	return bestLength;
}

static unsigned int CxiSearchLZ(const unsigned char *buffer, unsigned int size, unsigned int curpos, unsigned int minDistance, unsigned int maxDistance, unsigned int maxLength, unsigned int *pDistance) {
	//nProcessedBytes = curpos
	unsigned int nBytesLeft = size - curpos;

	//the maximum distance we can search backwards is limited by how far into the buffer we are. It won't
	//make sense to a decoder to copy bytes from before we've started.
	if (maxDistance > curpos) maxDistance = curpos;

	//the longest string we can match, including repetition by overwriting the source.
	unsigned int nMaxCompare = maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//begin searching backwards.
	unsigned int bestLength = 0, bestDistance = 0;
	for (unsigned int i = minDistance; i <= maxDistance; i++) {
		unsigned int nMatched = CxiCompareMemory(buffer + curpos - i, buffer + curpos, nMaxCompare);
		if (nMatched > bestLength) {
			bestLength = nMatched;
			bestDistance = i;
			if (bestLength == nMaxCompare) break;
		}
	}

	*pDistance = bestDistance;
	return bestLength;
}


// ----- Bit reader routines

typedef struct CxiBitReader_ {
	const unsigned char *start;
	const unsigned char *end;
	const unsigned char *pos;
	uint32_t current;
	uint8_t nBitsBuffered;
	uint8_t error;
	uint8_t beBits  : 1;  // big-endian bit order
	uint8_t beBytes : 1;  // big-endian byte order (requires full word buffer)
	uint32_t nBitsRead;
} CxiBitReader;

static void CxiBitReaderFetch(CxiBitReader *reader) {
	//when bit and byte endianness do not match, we must fetch full words. When they match,
	//we can get by with fetching one byte at a time.
	int fullWords = reader->beBits != reader->beBytes;
	unsigned int unitSize = fullWords ? 4 : 1;

	if ((reader->pos + unitSize) <= reader->end) {
		if (!fullWords) {
			//fetch byte
			reader->current = *reader->pos;
		} else {
			//fetch word
			reader->current = reader->pos[0] | (reader->pos[1] << 8) | (reader->pos[2] << 16) | (reader->pos[3] << 24);
			if (reader->beBytes) {
				reader->current = CxiByteSwap(reader->current);
			}
		}
		reader->nBitsBuffered = 8 * unitSize;
		reader->pos += unitSize;

		//in big endian bit order we internally reverse the bit buffer
		if (reader->beBits) {
			if (!fullWords) {
				reader->current = CxiBitReverse8(reader->current);
			} else {
				reader->current = CxiBitReverse32(reader->current);
			}
		}
	} else {
		//out of bounds access
		reader->error = 1;
	}
}

static void CxiBitReaderInit(CxiBitReader *reader, const unsigned char *pos, const unsigned char *end, int beBits, int beBytes) {
	reader->pos = pos;
	reader->end = end;
	reader->start = pos;
	reader->beBits = beBits;
	reader->beBytes = beBytes;
	reader->nBitsBuffered = 0;
	reader->nBitsRead = 0;
	reader->current = 0;
	reader->error = 0;
}

static uint32_t CxiBitReaderReadBit(CxiBitReader *reader) {
	if (reader->nBitsBuffered == 0) {
		//fetch next bits
		CxiBitReaderFetch(reader);
	}

	uint32_t current = reader->current;
	reader->current >>= 1;
	reader->nBitsBuffered--;
	reader->nBitsRead++;
	return current & 1;
}

static uint32_t CxiBitReaderReadBits(CxiBitReader *reader, unsigned int nBits) {
	uint32_t string = 0, i = 0;
	for (i = 0; i < nBits; i++) {
		uint32_t bit = CxiBitReaderReadBit(reader);
		if (reader->error) return string;

		if (reader->beBits) {
			string <<= 1;
			string |= bit;
		} else {
			string |= bit << i;
		}
	}

	return string;
}


// ----- Bit writer routines

typedef struct CxiBitWriter_ {
	uint32_t *bits;
	unsigned int nWords;
	unsigned int nBitsInLastWord;
	unsigned int nWordsAlloc;
	unsigned int length;
} CxiBitWriter;

static void CxiBitWriterInit(CxiBitWriter *writer) {
	writer->nWords = 0;
	writer->length = 0;
	writer->nBitsInLastWord = 32;
	writer->nWordsAlloc = 16;
	writer->bits = (uint32_t *) calloc(writer->nWordsAlloc, 4);
}

static void CxiBitWriterFree(CxiBitWriter *writer) {
	free(writer->bits);
}

static void CxiBitWriterWriteBit(CxiBitWriter *writer, int bit) {
	if (writer->nBitsInLastWord == 32) {
		writer->nBitsInLastWord = 0;
		writer->nWords++;
		if (writer->nWords > writer->nWordsAlloc) {
			unsigned int newAllocSize = (writer->nWordsAlloc + 2) * 3 / 2;
			writer->bits = realloc(writer->bits, newAllocSize * 4);
			writer->nWordsAlloc = newAllocSize;
		}
		writer->bits[writer->nWords - 1] = 0;
	}

	writer->bits[writer->nWords - 1] |= bit << (31 - writer->nBitsInLastWord);
	writer->nBitsInLastWord++;
	writer->length++;
}

static void *CxiBitWriterGetBytes(CxiBitWriter *writer, int wordAlign, int beBytes, int beBits, unsigned int *size) {
	//allocate buffer
	unsigned int outSize = writer->nWords * 4;
	if (!wordAlign && beBytes != beBits) {
		//nBitsInLast word is 32 if last word is full, 0 if empty.
		if (writer->nBitsInLastWord <= 24) outSize--;
		if (writer->nBitsInLastWord <= 16) outSize--;
		if (writer->nBitsInLastWord <=  8) outSize--;
		if (writer->nBitsInLastWord <=  0) outSize--;
	}
	unsigned char *outbuf = (unsigned char *) calloc(outSize, 1);

	//this function handles converting byte and bit orders from the internal
	//representation. Internally, we store the bit sequence as an array of
	//words, where the first bits are inserted at the most significant bit.
	for (unsigned int i = 0; i < outSize; i++) {
		uint32_t word = writer->bits[i / 4];
		if (beBytes) word = CxiByteSwap(word);

		//if little endian bit order, swap here
		uint8_t byte = (word >> (8 * (i % 4))) & 0xFF;
		if (!beBits) byte = CxiBitReverse8(byte);
		outbuf[i] = byte;
	}

	*size = outSize;
	return outbuf;
}

static void CxiBitWriterWriteBits(CxiBitWriter *writer, uint32_t bits, unsigned int nBits) {
	for (unsigned int i = 0; i < nBits; i++) CxiBitWriterWriteBit(writer, (bits >> i) & 1);
}

static void CxiBitWriterWriteBitsBE(CxiBitWriter *writer, uint32_t bits, unsigned int nBits) {
	for (unsigned int i = 0; i < nBits; i++) CxiBitWriterWriteBit(writer, (bits >> (nBits - 1 - i)) & 1);
}


// ----- Huffman coding routines

typedef struct CxiHuffNode_ {
	uint16_t sym;
	uint16_t symMin;
	uint16_t symMax;
	int freq;
	struct CxiHuffNode_ *left;
	struct CxiHuffNode_ *right;
} CxiHuffNode;

typedef struct CxiHuffCode_ {
	uint16_t value;
	uint16_t length;
	uint32_t encoding;
} CxiHuffCode;

#define ISLEAF(n) ((n)->left==NULL&&(n)->right==NULL)

static int CxiHuffFrequencyComparator(const void *p1, const void *p2) {
	const CxiHuffNode *n1 = (const CxiHuffNode *) p1;
	const CxiHuffNode *n2 = (const CxiHuffNode *) p2;

	//sort first according to descending frequency
	if (n2->freq != n1->freq) return n2->freq - n1->freq;

	//sort secondarily by symbol value (low symbols first)
	if (n1->sym < n2->sym) return -1;
	if (n1->sym > n2->sym) return  1;
	return 0;
}

static int CxiHuffCanonicalComparator(const void *p1, const void *p2) {
	const CxiHuffCode *c1 = (const CxiHuffCode *) p1;
	const CxiHuffCode *c2 = (const CxiHuffCode *) p2;

	//force 0-length (excluded) symbols to the end
	if (c1->length == 0) return 1;
	if (c2->length == 0) return -1;

	if (c1->length < c2->length) return -1;
	if (c1->length > c2->length) return 1;
	if (c1->value < c2->value) return -1;
	if (c1->value > c2->value) return 1;
	return 0;
}

static int CxiHuffSymbolComparator(const void *p1, const void *p2) {
	const CxiHuffCode *c1 = (const CxiHuffCode *) p1;
	const CxiHuffCode *c2 = (const CxiHuffCode *) p2;

	if (c1->value < c2->value) return -1;
	if (c1->value > c2->value) return 1;
	return 0;
}

static int CxiHuffmanHasSymbol(CxiHuffNode *node, uint16_t sym) {
	if (ISLEAF(node)) return node->sym == sym;
	if (sym < node->symMin || sym > node->symMax) return 0;
	
	return CxiHuffmanHasSymbol(node->left, sym) || CxiHuffmanHasSymbol(node->right, sym);
}

static void CxiHuffmanWriteSymbol(CxiBitWriter *bits, uint16_t sym, const CxiHuffNode *tree) {
	if (ISLEAF(tree)) return;
	
	if (CxiHuffmanHasSymbol(tree->left, sym)) {
		CxiBitWriterWriteBit(bits, 0);
		CxiHuffmanWriteSymbol(bits, sym, tree->left);
	} else {
		CxiBitWriterWriteBit(bits, 1);
		CxiHuffmanWriteSymbol(bits, sym, tree->right);
	}
}

static unsigned int CxiHuffmanConstructTree(CxiHuffNode *nodes, unsigned int nNodes, unsigned int nNodeMin) {
	//initialize symMin, symMax
	for (unsigned int i = 0; i < nNodes; i++) {
		nodes[i].symMin = nodes[i].symMax = nodes[i].sym;
	}

	//sort by frequency, then cut off the remainder (freq=0).
	qsort(nodes, nNodes, sizeof(CxiHuffNode), CxiHuffFrequencyComparator);
	for (unsigned int i = 0; i < nNodes; i++) {
		if (nodes[i].freq == 0) {
			nNodes = i;
			break;
		}
	}
	if (nNodes < nNodeMin) nNodes = nNodeMin;

	//unflatten the histogram into a huffman tree. 
	int nRoots = nNodes;
	int nTotalNodes = nNodes;
	while (nRoots > 1) {
		//copy bottom two nodes to just outside the current range
		CxiHuffNode *srcA = nodes + nRoots - 2;
		CxiHuffNode *destA = nodes + nTotalNodes;
		memcpy(destA, srcA, sizeof(CxiHuffNode));

		CxiHuffNode *left = destA;
		CxiHuffNode *right = nodes + nRoots - 1;
		CxiHuffNode *branch = srcA;

		branch->freq = left->freq + right->freq;
		branch->sym = 0;
		branch->left = left;
		branch->right = right;
		branch->symMin = min(left->symMin, right->symMin);
		branch->symMax = max(right->symMax, left->symMax);

		nRoots--;
		nTotalNodes++;
		qsort(nodes, nRoots, sizeof(CxiHuffNode), CxiHuffFrequencyComparator);
	}

	return nNodes;
}

static int CxiHuffAppendCanonicalCode(CxiHuffNode *tree, CxiHuffCode *codes, uint32_t encoding, int depth) {
	if (ISLEAF(tree)) {
		codes[tree->sym].length = depth;
		return 1;
	}

	//recurse
	int nl = CxiHuffAppendCanonicalCode(tree->left, codes, (encoding << 1) | 0, depth + 1);
	int nr = CxiHuffAppendCanonicalCode(tree->right, codes, (encoding << 1) | 1, depth + 1);
	return nl + nr;
}

static void CxiHuffMakeCanonicalCodes(CxiHuffNode *tree, CxiHuffCode *codes, int nMaxNodes) {
	//first, recursively append to the list.
	int nNodes = CxiHuffAppendCanonicalCode(tree, codes, 0, 1);
	for (int i = 0; i < nMaxNodes; i++) {
		codes[i].value = i;
	}

	//next, apply sort. Unassigned codes are pushed to the end of the list.
	qsort(codes, nMaxNodes, sizeof(CxiHuffCode), CxiHuffCanonicalComparator);

	//next, we can start assigning codes.
	uint32_t curcode = 0, curbits = 0, curmask = 0;
	for (int i = 0; i < nNodes; i++) {
		//shift code
		while (curbits < codes[i].length) {
			curcode <<= 1;
			curmask = (curmask << 1) | 1;
			curbits++;
		}
		codes[i].encoding = curcode;

		//increment current code
		curcode++;
		if ((curcode & curmask) == 0) {
			curmask = (curmask << 1) | 1;
			curbits++;
		}
	}

	//sort codes by symbol value again (for constant code lookup time)
	qsort(codes, nMaxNodes, sizeof(CxiHuffCode), CxiHuffSymbolComparator);
}


int CxIsCompressedLZ(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;
	if (*buffer != 0x10) return 0;
	uint32_t length = (*(uint32_t *) buffer) >> 8;
	if ((length / 144) * 17 + 4 > size) return 0;

	//start a dummy decompression
	uint32_t offset = 4;
	uint32_t dstOffset = 0;
	while (1) {
		uint8_t head = buffer[offset];
		offset++;

		//loop 8 times
		for (int i = 0; i < 8; i++) {
			int flag = head >> 7;
			head <<= 1;

			if (!flag) {
				if (dstOffset >= length || offset >= size) return 0;
				dstOffset++, offset++;
				if (dstOffset == length) goto checkSize;
			} else {
				if (offset + 1 >= size) return 0;
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];

				//length of uncompressed chunk and offset
				uint32_t offs = (((high & 0xF) << 8) | low) + 1;
				uint32_t len = (high >> 4) + 3;

				if (dstOffset < offs) return 0;
				for (uint32_t j = 0; j < len; j++) {
					if (dstOffset >= length) return 0;
					dstOffset++;
					if (dstOffset == length) goto checkSize;
				}
			}
		}
	}

	//check the size of the remaining data
	unsigned int remaining;
checkSize:
	remaining = size - offset;
	if (remaining > 7) return 0;

	return 1;
}
int CxIsCompressedRL(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;
	if (*buffer != 0x30) return 0;
	uint32_t header = *(uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;

	unsigned int dstOfs = 0;
	unsigned int srcOfs = 4;
	while (dstOfs < uncompSize) {
		if (srcOfs >= size) return 0;
		unsigned char head = buffer[srcOfs++];

		int compressed = head >> 7;
		if (compressed) {
			int chunkLen = (head & 0x7F) + 3;
			if (srcOfs >= size) return 0;
			srcOfs++;

			for (int i = 0; i < chunkLen; i++) {
				dstOfs++;
			}
		} else {
			int chunkLen = (head & 0x7F) + 1;
			for (int i = 0; i < chunkLen; i++) {
				if (srcOfs >= size) return 0;
				dstOfs++;
				srcOfs++;
			}
		}

		if (dstOfs > uncompSize) return 0;
	}

	//allow up to 3 bytes padding
	if (size - srcOfs > 3) return 0;

	return 1;
}
// ----- MvDK Routines

#define MVDK_DUMMY       0
#define MVDK_LZ          1
#define MVDK_DEFLATE     2
#define MVDK_RLE         3
#define MVDK_INVALID     -1

typedef struct DEFLATE_TABLE_ENTRY_ {
	uint16_t nMinorBits;
	uint16_t majorPart;
} DEFLATE_TABLE_ENTRY;

typedef struct DEFLATE_TREE_NODE {
	struct DEFLATE_TREE_NODE *left;
	struct DEFLATE_TREE_NODE *right;
	uint8_t depth;
	uint8_t isLeaf;
	uint16_t value;
	uint32_t path;
} DEFLATE_TREE_NODE;

typedef struct DEFLATE_WORK_BUFFER_ {
	DEFLATE_TREE_NODE symbolNodeBuffer[855];
	DEFLATE_TREE_NODE lengthNodeBuffer[855];
	DEFLATE_TREE_NODE *nextAvailable;
} DEFLATE_WORK_BUFFER;

static const DEFLATE_TABLE_ENTRY sDeflateLengthTable[] = {
	{ 0, 0x00 }, { 0, 0x01 }, { 0, 0x02 }, { 0, 0x03 }, { 0, 0x04 }, { 0, 0x05 }, { 0, 0x06 }, { 0, 0x07 },
	{ 1, 0x08 }, { 1, 0x0A }, { 1, 0x0C }, { 1, 0x0E }, { 2, 0x10 }, { 2, 0x14 }, { 2, 0x18 }, { 2, 0x1C },
	{ 3, 0x20 }, { 3, 0x28 }, { 3, 0x30 }, { 3, 0x38 }, { 4, 0x40 }, { 4, 0x50 }, { 4, 0x60 }, { 4, 0x70 },
	{ 5, 0x80 }, { 5, 0xA0 }, { 5, 0xC0 }, { 5, 0xE0 }, { 0, 0xFF }
};

static const DEFLATE_TABLE_ENTRY sDeflateOffsetTable[] = {
	{ 0,  0x0000 }, { 0,  0x0001 }, { 0,  0x0002 }, { 0,  0x0003 },
	{ 1,  0x0004 }, { 1,  0x0006 }, { 2,  0x0008 }, { 2,  0x000C },
	{ 3,  0x0010 }, { 3,  0x0018 }, { 4,  0x0020 }, { 4,  0x0030 },
	{ 5,  0x0040 }, { 5,  0x0060 }, { 6,  0x0080 }, { 6,  0x00C0 },
	{ 7,  0x0100 }, { 7,  0x0180 }, { 8,  0x0200 }, { 8,  0x0300 },
	{ 9,  0x0400 }, { 9,  0x0600 }, { 10, 0x0800 }, { 10, 0x0C00 },
	{ 11, 0x1000 }, { 11, 0x1800 }, { 12, 0x2000 }, { 12, 0x3000 },
	{ 13, 0x4000 }, { 13, 0x6000 }
};

// deflate decompress (inflate?)


// ----- Huffman tree construction

void CxiHuffmanInsertNode(DEFLATE_WORK_BUFFER *auxBuffer, DEFLATE_TREE_NODE *root, DEFLATE_TREE_NODE *node2, unsigned int depth) {
	//0 for left, 1 for right
	int pathbit = (node2->path >> depth) & 1;

	//depth=0 means insert here
	if (depth == 0) {
		if (pathbit) {
			root->right = node2;
		} else {
			root->left = node2;
		}
		return;
	}

	if (pathbit) {
		//create a right node if it doesn't exist
		if (root->right == NULL) {
			DEFLATE_TREE_NODE *available = auxBuffer->nextAvailable;
			auxBuffer->nextAvailable++;
			root->right = available;
		}
		CxiHuffmanInsertNode(auxBuffer, root->right, node2, depth - 1);
	} else {
		//create a left node if it doesn't exist
		if (root->left == NULL) {
			DEFLATE_TREE_NODE *available = auxBuffer->nextAvailable;
			auxBuffer->nextAvailable++;
			root->left = available;
		}
		CxiHuffmanInsertNode(auxBuffer, root->left, node2, depth - 1);
	}
}


DEFLATE_TREE_NODE *CxiHuffmanReadTree(DEFLATE_WORK_BUFFER *auxBuffer, CxiBitReader *reader, DEFLATE_TREE_NODE *nodeBuffer, unsigned int nNodes) {
	unsigned int i, j;
	int paths[32];
	int depthCounts[32];

	//clear buffers
	memset(nodeBuffer, 0, nNodes * 2 * sizeof(DEFLATE_TREE_NODE));
	memset(depthCounts, 0, sizeof(depthCounts));
	memset(paths, 0, sizeof(paths));

	i = 0;
	while (i < nNodes) {
		//Read 1 bit - determines format of node structure?
		if (CxiBitReaderReadBit(reader)) {
			//read 7-bit number from 2 to 129 (number of loop iterations)
			unsigned int nNodesBlock = CxiBitReaderReadBits(reader, 7) + 2;
			if (reader->error) return NULL;
			if (i + nNodesBlock > nNodes) return NULL;

			//this 5-bit value gets put into the depth of all nodes written here
			unsigned int depth = CxiBitReaderReadBits(reader, 5);
			if (reader->error) return NULL;

			for (j = 0; j < nNodesBlock; j++) {
				nodeBuffer[i + j].depth = depth;
				depthCounts[depth]++;
			}
			i += nNodesBlock;
		} else {
			//read 7-bit number from 1 to 128. Number of loop iterations.
			unsigned int nNodesBlock = CxiBitReaderReadBits(reader, 7) + 1;
			if (reader->error) return NULL;
			if (i + nNodesBlock > nNodes) return NULL;

			for (j = 0; j < nNodesBlock; j++) {
				uint8_t depth = CxiBitReaderReadBits(reader, 5);
				if (reader->error) return NULL;

				nodeBuffer[i + j].depth = depth;
				depthCounts[depth]++;
			}
			i += nNodesBlock;
		}
	}

	//written too many nodes
	if (i > nNodes) return NULL;

	int depth = 0;
	depthCounts[0] = 0;
	for (i = 1; i < 32; i++) {
		depth = (depth + depthCounts[i - 1]) << 1;
		paths[i] = depth;
	}

	DEFLATE_TREE_NODE *root = nodeBuffer + nNodes;
	auxBuffer->nextAvailable = root + 1;

	for (i = 0; i < nNodes; i++) {
		DEFLATE_TREE_NODE *node = nodeBuffer + i;
		node->isLeaf = 1;

		if (node->depth > 0) {
			node->path = paths[node->depth];
			node->value = i;
			paths[node->depth]++;
			CxiHuffmanInsertNode(auxBuffer, root, node, node->depth - 1);
		}
	}
	return root;
}

uint32_t CxiLookupTreeNode(DEFLATE_TREE_NODE *node, CxiBitReader *reader) {
	if (node == NULL) return (uint32_t) -1;

	while (!node->isLeaf) {
		if (CxiBitReaderReadBit(reader)) {
			node = node->right;
		} else {
			node = node->left;
		}
		if (reader->error || node == NULL) return (uint32_t) -1;
	}
	return node->value;
}

unsigned char *CxiDecompressDeflateChunk(DEFLATE_WORK_BUFFER *auxBuffer, unsigned char *destBase, const unsigned char **pPos, unsigned char *dest, 
		unsigned char *end, const unsigned char *srcEnd, int write) {
	//init reader
	CxiBitReader reader;
	const unsigned char *pos = *pPos;
	uint32_t nBytesConsumed = 0;
	CxiBitReaderInit(&reader, pos, srcEnd, 0, 0);

	int isCompressed = CxiBitReaderReadBit(&reader);
	if (reader.error) return NULL;
	uint32_t chunkLen = CxiBitReaderReadBits(&reader, 31);
	if (reader.error) return NULL;

	if (!isCompressed) {
		//uncompressed chunk, just memcpy out
		if ((dest + chunkLen) > end || (dest + chunkLen) < destBase || (pos + 4 + chunkLen) > srcEnd) return NULL;
		if (write) memcpy(dest, pos + 4, chunkLen);

		nBytesConsumed = chunkLen + 4;
		dest += chunkLen;
	} else {
		const unsigned char *tableBase = reader.pos;

		//Consume a Huffman tree. The length of the tree data (in bits) is given by the next 16 bits in the stream.
		uint32_t lzLen2 = CxiBitReaderReadBits(&reader, 16);
		uint32_t table1SizeBytes = (lzLen2 + 7) >> 3;
		const unsigned char *postTree = reader.pos + table1SizeBytes;
		DEFLATE_TREE_NODE *huffRoot1 = CxiHuffmanReadTree(auxBuffer, &reader, auxBuffer->symbolNodeBuffer, 0x11D);
		if (huffRoot1 == NULL) return NULL; // Huffman tree error
		if (postTree > srcEnd) return NULL; // Validate tree size

		//Reposition stream after the Huffman tree. Read out the LZ distance tree next.
		//Its size in bits is given by the following 16 bits from the stream.
		CxiBitReaderInit(&reader, postTree, srcEnd, 0, 0);
		reader.nBitsRead = (postTree - pos) * 8;
		lzLen2 = CxiBitReaderReadBits(&reader, 16);
		uint32_t table2SizeBytes = (lzLen2 + 7) >> 3;

		postTree = reader.pos + table2SizeBytes;
		DEFLATE_TREE_NODE *huffDistancesRoot = CxiHuffmanReadTree(auxBuffer, &reader, auxBuffer->lengthNodeBuffer, 0x1E);
		if (huffDistancesRoot == NULL) return NULL; // Huffman tree error
		if (postTree > srcEnd) return NULL;         // Validate tree size

		//Reposition stream after this tree to prepare for reading the compressed sequence.
		CxiBitReaderInit(&reader, postTree, srcEnd, 0, 0);
		reader.nBitsRead = (reader.pos - pos) * 8;

		while (reader.nBitsRead < chunkLen && dest < end) {
			uint32_t huffVal = CxiLookupTreeNode(huffRoot1, &reader);
			if (huffVal == (uint32_t) -1) return NULL;

			if (huffVal < 0x100) {
				//simple byte value Huffman
				if (write) *dest = (unsigned char) huffVal;
				dest++;
			} else {
				//LZ part Huffman

				//read out length
				uint32_t nLengthMinorBits = sDeflateLengthTable[huffVal - 0x100].nMinorBits;
				uint32_t lzLen1 = sDeflateLengthTable[huffVal - 0x100].majorPart;
				uint32_t lzLen2 = CxiBitReaderReadBits(&reader, nLengthMinorBits);
				uint32_t lzLen = lzLen1 + lzLen2 + 3;

				//read out offset
				uint32_t nodeVal2 = CxiLookupTreeNode(huffDistancesRoot, &reader);
				if (nodeVal2 == (uint32_t) -1) return NULL;

				uint32_t nOffsetMinorBits = sDeflateOffsetTable[nodeVal2].nMinorBits;
				uint32_t lzOffset1 = sDeflateOffsetTable[nodeVal2].majorPart;
				uint32_t lzOffset2 = CxiBitReaderReadBits(&reader, nOffsetMinorBits);
				uint32_t lzOffset = lzOffset1 + lzOffset2 + 1;

				size_t curoffs = dest - destBase;
				size_t remaining = end - dest;
				if (lzOffset > curoffs) return NULL;
				if (lzLen > remaining) return NULL;

				unsigned char *lzSrc = dest - lzOffset;
				unsigned int i;
				for (i = 0; i < lzLen && dest < end; i++) {
					if (write) *dest = *lzSrc;
					dest++, lzSrc++;
				}
			}
		}
		nBytesConsumed = (chunkLen + 7) >> 3;
	}

	*pPos = pos + nBytesConsumed;
	return dest;
}


void CxDecompressDeflate(const unsigned char *filebuf, unsigned char *dest, void *auxBuffer, unsigned int size) {
	const unsigned char *pos = filebuf + 4;
	unsigned char *destBase = dest;
	unsigned char *end = dest + ((*(uint32_t *) filebuf) >> 2);

	while (dest < end) {
		dest = CxiDecompressDeflateChunk((DEFLATE_WORK_BUFFER *) auxBuffer, destBase, &pos, dest, end, filebuf + size, 1);
	}
}
static int CxiMvdkIsValidLZ(const unsigned char *buffer, unsigned int size) {
	//same format as standard LZ, with different header
	uint32_t uncompSize = (*(uint32_t *) buffer) >> 2;
	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x10 | (uncompSize << 8);
	int valid = CxIsCompressedLZ(copy, size);
	free(copy);
	return valid;
}

static int CxiMvdkIsValidRL(const unsigned char *buffer, unsigned int size) {
	//same format as standard LZ, with different header
	uint32_t uncompSize = (*(uint32_t *) buffer) >> 2;
	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x30 | (uncompSize << 8);
	int valid = CxIsCompressedRL(copy, size);
	free(copy);
	return valid;
}

static int CxiMvdkIsValidDeflate(const unsigned char *buffer, unsigned int size) {
	const unsigned char *pos = buffer + 4;
	unsigned char *dest = NULL; //won't be written to
	unsigned char *destBase = dest;
	unsigned char *end = dest + ((*(uint32_t *) buffer) >> 2); //for address comparison
	DEFLATE_WORK_BUFFER *work = (DEFLATE_WORK_BUFFER *) calloc(1, sizeof(DEFLATE_WORK_BUFFER));

	while (dest < end) {
		dest = CxiDecompressDeflateChunk(work, destBase, &pos, dest, end, buffer + size, 0);
		if (dest == NULL) {
			free(work);
			return 0;
		}
	}
	free(work);

	//test buffer remaining (allow up to 3 bytes trailing for 4-byte aligned file size)
	unsigned int nConsumed = pos - buffer;
	nConsumed = (nConsumed + 3) & ~3;

	//check bytes unconsumed (Nintendo's encoder sometimes adds 4 bytes? uncompressed block indicator?)
	if ((nConsumed + 4) < ((size + 3) & ~3)) return 0;

	return 1;
}

// --- additional NitroPaint helpers required by CxDecompressMvDK ---
unsigned char *CxDecompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize){
	if (size < 4) return NULL;

	//find the length of the decompressed buffer.
	uint32_t length = (*(uint32_t *) buffer) >> 8;

	//create a buffer for the decompressed buffer
	unsigned char *result = (unsigned char *) malloc(length);
	if (result == NULL) return NULL;
	*uncompressedSize = length;

	//initialize variables
	uint32_t offset = 4;
	uint32_t dstOffset = 0;
	while (1) {
		uint8_t head = buffer[offset];
		offset++;
		//loop 8 times
		for (int i = 0; i < 8; i++) {
			int flag = head >> 7;
			head <<= 1;

			if (!flag) {
				result[dstOffset] = buffer[offset];
				dstOffset++, offset++;
				if(dstOffset == length) return result;
			} else {
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];

				//length of uncompressed chunk and offset
				uint32_t offs = (((high & 0xF) << 8) | low) + 1;
				uint32_t len = (high >> 4) + 3;
				for (uint32_t j = 0; j < len; j++) {
					result[dstOffset] = result[dstOffset - offs];
					dstOffset++;
					if(dstOffset == length) return result;
				}
			}
		}
	}
	return result;
}

unsigned char *CxDecompressRL(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	unsigned int uncompSize = (*(uint32_t *) buffer) >> 8;
	unsigned char *out = (unsigned char *) calloc(uncompSize, 1);
	*uncompressedSize = uncompSize;

	unsigned int dstOfs = 0;
	unsigned int srcOfs = 4;
	while (dstOfs < uncompSize) {
		unsigned char head = buffer[srcOfs++];

		int compressed = head >> 7;
		if (compressed) {
			int chunkLen = (head & 0x7F) + 3;
			unsigned char b = buffer[srcOfs++];
			for (int i = 0; i < chunkLen; i++) {
				out[dstOfs++] = b;
			}
		} else {
			int chunkLen = (head & 0x7F) + 1;
			for (int i = 0; i < chunkLen; i++) {
				out[dstOfs++] = buffer[srcOfs++];
			}
		}
	}

	return out;
}

static int CxiMvdkGetCompressionType(const unsigned char *buffer, unsigned int size) {
	return (*(uint32_t *) buffer) & 3;
}


int CxIsCompressedMvDK(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;

	uint32_t uncompSize = (*(uint32_t *) buffer) >> 2;
	int type = CxiMvdkGetCompressionType(buffer, size);
	switch (type) {
		case MVDK_DUMMY:
			//check size
			return (((size - 4 + 3) & ~3) == ((uncompSize + 3) & ~3)) && ((size - 4) >= uncompSize);
		case MVDK_LZ:
			return CxiMvdkIsValidLZ(buffer, size);
		case MVDK_RLE:
			return CxiMvdkIsValidRL(buffer, size);
		case MVDK_DEFLATE:
			return CxiMvdkIsValidDeflate(buffer, size);
	}
	return 0;
}

static unsigned char *CxiMvdkDecompressDummy(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outlen = (*(uint32_t *) buffer) >> 2;
	unsigned char *out = (unsigned char *) malloc(outlen);
	*uncompressedSize = outlen;

	memcpy(out, buffer + 4, outlen);
	return out;
}

static unsigned char *CxiMvdkDecompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outlen = (*(uint32_t *) buffer) >> 2;

	unsigned char *copy = (unsigned char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x10 | (outlen << 8);
	unsigned char *out = CxDecompressLZ(copy, size, uncompressedSize);
	free(copy);

	return out;
}

static unsigned char *CxiMvdkDecompressRL(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outlen = (*(uint32_t *) buffer) >> 2;

	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x30 | (outlen << 8);
	char *out = CxDecompressRL(copy, size, uncompressedSize);
	free(copy);

	return out;
}

static unsigned char *CxiMvdkDecompressDeflate(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outlen = (*(uint32_t *) buffer) >> 2;
	*uncompressedSize = outlen;
	char *dest = malloc(outlen);

	void *aux = calloc(1, sizeof(DEFLATE_WORK_BUFFER));
	CxDecompressDeflate(buffer, dest, aux, size);
	free(aux);
	return dest;
}

unsigned char *CxDecompressMvDK(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	int type = (*(uint32_t *) buffer) & 3;
	switch (type) {
		case MVDK_DUMMY:
			return CxiMvdkDecompressDummy(buffer, size, uncompressedSize);
		case MVDK_LZ:
			return CxiMvdkDecompressLZ(buffer, size, uncompressedSize);
		case MVDK_RLE:
			return CxiMvdkDecompressRL(buffer, size, uncompressedSize);
		case MVDK_DEFLATE:
			return CxiMvdkDecompressDeflate(buffer, size, uncompressedSize);
	}
	*uncompressedSize = 0;
	return NULL;
}

// ------- Wrappers using this codebase's error conventions -------
enumError DecodeMVDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (src_size < 4) return EINVAL;
	unsigned int uncomp_size;
	unsigned char *res = CxDecompressMvDK (src, src_size, &uncomp_size);
	if (!res) return EINVAL;
	enumError err = alloc_output (dest, dest_size, uncomp_size);
	if (err) { free (res); return err; }
	memcpy (*dest, res, uncomp_size);
	free (res);
	// Restore sentinels after using free()
#define calloc  do_not_use_calloc
#define malloc  do_not_use_malloc
#define realloc do_not_use_realloc
	return ERR_OK;
}
#define free do_not_use_free

// EncodeMVDK: compression not yet implemented (compressor requires
// NitroPaint internal StList/BSTREAM infrastructure not ported here).
enumError EncodeMVDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	(void)dest; (void)dest_size; (void)src; (void)src_size;
	return EINVAL;
}

//-----------------------------------------------------------------------------
///////////////		VLX Compression (Namco / Pac-Man World)	///////////////
//-----------------------------------------------------------------------------

typedef struct vlx_tree_node_t
{
	uint value;
	uint mask_bits;
	u32 encoding;
	u32 mask;
} vlx_tree_node_t;

typedef struct vlx_buffer_t
{
	const u8 *src;
	uint srcpos, size;
	u32 cur_word;
	int bits_left;
	bool error;
} vlx_buffer_t;

static void vlx_buf_init (vlx_buffer_t *b, const u8 *src, uint pos, uint size)
{
	b->src = src;
	b->srcpos = pos;
	b->size = size;
	b->cur_word = 0;
	b->bits_left = 0;
	b->error = false;
}

static u32 vlx_read_bits (vlx_buffer_t *b, int n)
{
	if (n <= 0) return 0;
	while (b->bits_left < n)
	{
		if (b->srcpos + 4 > b->size)
		{
			// Read remaining bytes with zero padding
			u32 w = 0;
			for (uint i = 0; i < 4; i++)
				if (b->srcpos + i < b->size)
					w |= (u32)b->src[b->srcpos + i] << (i * 8);
			b->cur_word |= w << b->bits_left;
			b->srcpos = b->size;
			b->bits_left += 32;
			break;
		}
		const u32 w = (u32)b->src[b->srcpos] | ((u32)b->src[b->srcpos + 1] << 8)
			| ((u32)b->src[b->srcpos + 2] << 16) | ((u32)b->src[b->srcpos + 3] << 24);
		b->srcpos += 4;
		b->cur_word |= w << b->bits_left;
		b->bits_left += 32;
	}
	const u32 val = b->cur_word & ((1u << n) - 1);
	b->cur_word >>= n;
	b->bits_left -= n;
	return val;
}

static uint vlx_read_next_val (vlx_buffer_t *b, const vlx_tree_node_t *nodes, uint n_nodes)
{
	while (b->bits_left < 16 && b->srcpos < b->size)
	{
		const u8 byte = b->src[b->srcpos++];
		b->cur_word |= (u32)byte << b->bits_left;
		b->bits_left += 8;
	}
	for (uint i = 0; i < n_nodes; i++)
	{
		const uint mb = nodes[i].mask_bits;
		if ((int)mb > b->bits_left)
			continue;
		const u32 code = b->cur_word & ((1u << mb) - 1);
		if ((code << (32 - mb)) == nodes[i].encoding)
		{
			b->cur_word >>= mb;
			b->bits_left -= mb;
			return nodes[i].value;
		}
	}
	b->error = true;
	return (uint)-1;
}

static int vlx_try_decompress (const u8 *src, uint size, u8 *dest, uint *out_len_ptr)
{
	if (!src || size < 2)
		return 0;
	if (src[0] & 0xF0)
		return 0;

	const uint lenlen = src[0] & 0xF;
	uint outlen = 0;
	if (lenlen == 1)
	{
		if (size < 3) return 0;
		outlen = src[1];
	}
	else if (lenlen == 2)
	{
		if (size < 4) return 0;
		outlen = (uint)src[1] | ((uint)src[2] << 8);
	}
	else if (lenlen == 4)
	{
		if (size < 6) return 0;
		outlen = (uint)src[1] | ((uint)src[2] << 8) | ((uint)src[3] << 16) | ((uint)src[4] << 24);
	}
	else
		return 0;

	if (!outlen || outlen > NFMT_MAX_OUTPUT)
		return 0;

	uint srcpos = lenlen + 1;
	if (srcpos >= size) return 0;

	const u8 byte1 = src[srcpos++];
	const uint hi4 = (byte1 >> 4) & 0xF;
	const uint lo4 = byte1 & 0xF;
	if (hi4 > 12 || lo4 > 12 || srcpos + (hi4 + lo4) * 2 > size)
		return 0;

	vlx_tree_node_t len_nodes[12] = { { 0 } };
	vlx_tree_node_t dist_nodes[12] = { { 0 } };

	for (uint i = 0; i < hi4; i++)
	{
		const u16 hw = (u16)src[srcpos] | ((u16)src[srcpos + 1] << 8);
		srcpos += 2;
		len_nodes[i].value = hw >> 12;
		int mb = 11;
		if ((hw & 0xFFF) == 0) return 0;
		while (!((hw & 0xFFF) & (1 << mb)) && mb > 0) mb--;
		if (mb == 0) return 0;
		len_nodes[i].mask_bits = mb;
		len_nodes[i].encoding = (hw & 0xFFF) & ((1u << mb) - 1);
	}

	for (uint i = 0; i < lo4; i++)
	{
		const u16 hw = (u16)src[srcpos] | ((u16)src[srcpos + 1] << 8);
		srcpos += 2;
		dist_nodes[i].value = hw >> 12;
		int mb = 11;
		if ((hw & 0xFFF) == 0) return 0;
		while (!((hw & 0xFFF) & (1 << mb)) && mb > 0) mb--;
		if (mb == 0) return 0;
		dist_nodes[i].mask_bits = mb;
		dist_nodes[i].encoding = (hw & 0xFFF) & ((1u << mb) - 1);
	}

	vlx_buffer_t buf;
	vlx_buf_init (&buf, src, srcpos, size);
	uint outpos = 0;

	while (outpos < outlen && !buf.error)
	{
		const uint n_len_bits = vlx_read_next_val (&buf, len_nodes, hi4);
		if (n_len_bits == (uint)-1) return 0;

		if (n_len_bits == 0)
		{
			const u8 b = (u8)vlx_read_bits (&buf, 8);
			if (dest) dest[outpos] = b;
			outpos++;
		}
		else
		{
			const uint copylen = (1u << n_len_bits) + vlx_read_bits (&buf, n_len_bits);
			const uint n_dist_bits = vlx_read_next_val (&buf, dist_nodes, lo4);
			if (n_dist_bits == (uint)-1) return 0;
			const uint dist = (1u << n_dist_bits) + vlx_read_bits (&buf, n_dist_bits) - 1;
			if (dist > outpos || dist == 0 || copylen > outlen - outpos)
				return 0;

			if (dest)
			{
				for (uint i = 0; i < copylen; i++)
					dest[outpos + i] = dest[outpos - dist + i];
			}
			outpos += copylen;
		}
	}

	if (out_len_ptr) *out_len_ptr = outlen;
	return outpos == outlen && !buf.error;
}

int CxIsCompressedVlx (const unsigned char *src, unsigned int size)
{
	return vlx_try_decompress (src, size, 0, 0);
}

enumError DecodeVLX (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4)
		return EINVAL;
	uint uncomp_sz = 0;
	if (!vlx_try_decompress (src, src_size, 0, &uncomp_sz))
		return EINVAL;
	enumError err = alloc_output (dest, dest_size, uncomp_sz);
	if (err)
		return err;
	if (!vlx_try_decompress (src, src_size, *dest, 0))
	{
		FREE (*dest);
		*dest = 0;
		*dest_size = 0;
		return EINVAL;
	}
	return ERR_OK;
}

enumError EncodeVLX (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	// Build a valid literal VLX stream with lenlen=4
	// Header: [0]=4, [1..4]=src_size, [5]=0x10 (hi4=1, lo4=0), [6..7]=hw(node 0, enc 0 with sentinel bit 1 => 2)
	const uint total_sz = 5 + 1 + 2 + src_size * 2 + 8;
	u8 *out = CALLOC (1, total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 4; // 4-byte uncompressed size
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);
	out[4] = (u8)((src_size >> 24) & 0xFF);
	out[5] = 0x10; // hi4=1, lo4=0
	out[6] = 0x02; // hw = (value 0 << 12) | (1 << 1 sentinel) | (code 0) = 2
	out[7] = 0x00;

	// Write literal tokens (1 bit '0' length token + 8 bits data)
	uint bitpos = 0;
	u8 *bitstream = out + 8;
	for (uint i = 0; i < src_size; i++)
	{
		// 1 bit '0' for length node value 0
		bitpos++; // bit remains 0
		// 8 bits of literal byte
		for (uint b = 0; b < 8; b++)
		{
			if (src[i] & (1 << b))
				bitstream[bitpos / 8] |= 0x01 << (bitpos & 7);
			bitpos++;
		}
	}

	const uint final_size = 8 + (bitpos + 7) / 8;
	*dest = out;
	*dest_size = final_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		PuCrunch Compression (Griptonite Games)	///////////////
//-----------------------------------------------------------------------------

typedef struct pc_bit_reader_t
{
	const u8 *src, *end;
	uint bitpos;
	bool error;
} pc_bit_reader_t;

static inline bool pc_read_bit (pc_bit_reader_t *r)
{
	const u8 *p = r->src + (r->bitpos / 8);
	if (p >= r->end)
	{
		r->error = true;
		return false;
	}
	const bool b = (*p & (0x80 >> (r->bitpos & 7))) != 0;
	r->bitpos++;
	return b;
}

static inline uint pc_read_bits (pc_bit_reader_t *r, uint n)
{
	uint val = 0;
	for (uint i = 0; i < n; i++)
		val = (val << 1) | (pc_read_bit (r) ? 1 : 0);
	return val;
}

static inline uint pc_read_gamma (pc_bit_reader_t *r)
{
	uint count = 0;
	while (pc_read_bit (r) && !r->error && count < 32)
		count++;
	if (r->error) return 0;
	if (count == 0) return 1;
	return (1u << count) | pc_read_bits (r, count);
}

int CxIsCompressedPuCrunch (const unsigned char *buffer, unsigned int size)
{
	if (!buffer || size < 8 || buffer[0] != 0x60)
		return 0;
	const uint uncomp_size = ((uint)buffer[1]) | ((uint)buffer[2] << 8) | ((uint)buffer[3] << 16);
	if (!uncomp_size || uncomp_size > NFMT_MAX_OUTPUT)
		return 0;

	const u8 *info = buffer + 4;
	const uint freq_tbl_size = info[0];
	const uint esc_bits = info[3];
	const uint lz_extra = info[2];
	if (freq_tbl_size > size - 8 || (freq_tbl_size & 3) || freq_tbl_size > 0x20 || !freq_tbl_size)
		return 0;
	if (esc_bits > 8 || lz_extra > 24)
		return 0;
	return 1;
}

enumError DecodePuCrunch (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !CxIsCompressedPuCrunch (src, src_size))
		return EINVAL;

	const uint uncomp_size = ((uint)src[1]) | ((uint)src[2] << 8) | ((uint)src[3] << 16);
	enumError err = alloc_output (dest, dest_size, uncomp_size);
	if (err)
		return err;

	u8 *out = *dest;
	const u8 *info = src + 4;
	const uint freq_tbl_size = info[0];
	u8 esc = info[1];
	const uint lz_extra = info[2];
	const uint esc_bits = info[3];
	const u8 *freq_table = src + 8;
	const u8 *bit_stream = info + 4 + freq_tbl_size;

	pc_bit_reader_t reader = { bit_stream, src + src_size, 0, false };
	const uint n_lz_bits = 8 + lz_extra;
	uint outpos = 0;

	while (outpos < uncomp_size && !reader.error)
	{
		const u8 init_bits = (u8)pc_read_bits (&reader, esc_bits);
		if (init_bits != esc)
		{
			const u8 rest = (u8)pc_read_bits (&reader, 8 - esc_bits);
			out[outpos++] = (init_bits << (8 - esc_bits)) | rest;
		}
		else
		{
			const uint x = pc_read_gamma (&reader) + 1;
			if (x > 2)
			{
				const uint hi = pc_read_gamma (&reader) - 1;
				if (hi == 0xFE) break; // EOF
				const uint offset = ((hi << n_lz_bits) | pc_read_bits (&reader, n_lz_bits)) + 1;
				if (offset > outpos || x > uncomp_size - outpos)
					goto fail;
				for (uint i = 0; i < x; i++)
					out[outpos + i] = out[outpos - offset + i];
				outpos += x;
			}
			else if (!pc_read_bit (&reader))
			{
				const uint offset = pc_read_bits (&reader, 8) + 1;
				if (offset > outpos || 2 > uncomp_size - outpos)
					goto fail;
				out[outpos + 0] = out[outpos - offset + 0];
				out[outpos + 1] = out[outpos - offset + 1];
				outpos += 2;
			}
			else if (!pc_read_bit (&reader))
			{
				const u8 new_esc = (u8)pc_read_bits (&reader, esc_bits);
				out[outpos++] = (esc << (8 - esc_bits)) | (u8)pc_read_bits (&reader, 8 - esc_bits);
				esc = new_esc;
			}
			else
			{
				uint rl_len = pc_read_gamma (&reader);
				if (rl_len >= 0x80)
				{
					rl_len = ((rl_len << 1) + (pc_read_bit (&reader) ? 1 : 0)) & 0xFF;
					rl_len |= (pc_read_gamma (&reader) - 1) << 8;
				}
				rl_len++;
				uint b_repeat = pc_read_gamma (&reader);
				if (b_repeat < 32)
				{
					if (b_repeat == 0 || b_repeat - 1 >= freq_tbl_size)
						goto fail;
					b_repeat = freq_table[b_repeat - 1];
				}
				else
				{
					b_repeat = ((b_repeat << 3) | pc_read_bits (&reader, 3)) & 0xFF;
				}
				if (rl_len > uncomp_size - outpos)
					goto fail;
				for (uint i = 0; i < rl_len; i++)
					out[outpos + i] = (u8)b_repeat;
				outpos += rl_len;
			}
		}
	}

	if (outpos == uncomp_size && !reader.error)
		return ERR_OK;
fail:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodePuCrunch (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	// Build a valid literal PuCrunch stream
	const uint freq_tbl_size = 4;
	const uint total = 8 + freq_tbl_size + (src_size * 10 + 32) / 8 + 4;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 0x60;
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);

	out[4] = (u8)freq_tbl_size;
	out[5] = 0x00; // esc value
	out[6] = 0x00; // lz extra
	out[7] = 0x02; // esc bits = 2

	// Frequency table
	out[8] = 0; out[9] = 0; out[10] = 0; out[11] = 0;

	// Bitstream: write each literal with non-escape prefix (0b01)
	uint bitpos = 0;
	u8 *stm = out + 8 + freq_tbl_size;
	for (uint i = 0; i < src_size; i++)
	{
		const u8 b = src[i];
		// If high 2 bits are 0b00 (escape match), write escaped literal:
		// esc (0b00) + gamma(1) -> 0b0 + bit(1) + bit(0) + new_esc(0b00) + rest
		if ((b >> 6) == 0x00)
		{
			// esc: 0b00
			bitpos += 2;
			// gamma 1: bit 0
			bitpos += 1;
			// bit 1: not 2-byte LZ
			stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
			bitpos++;
			// bit 0: escaped literal
			bitpos++;
			// new escape = 0b00
			bitpos += 2;
			// 6 low bits of literal
			for (int bit = 5; bit >= 0; bit--)
			{
				if (b & (1 << bit))
					stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
				bitpos++;
			}
		}
		else
		{
			// 8 bits of literal directly (high 2 bits != 0)
			for (int bit = 7; bit >= 0; bit--)
			{
				if (b & (1 << bit))
					stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
				bitpos++;
			}
		}
	}

	// End of stream marker: esc (0b00) + gamma(2) [0b100] + gamma(0xFF) [0xFE]
	// esc 0b00
	bitpos += 2;
	// gamma 2 (x=3 > 2): 0b100
	stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
	bitpos += 3;
	// gamma for hi=0xFE (255): 8 bits of 1 + 0 + 8-bit val
	for (uint k = 0; k < 8; k++)
	{
		stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
		bitpos++;
	}
	bitpos++; // 0 bit
	for (int bit = 7; bit >= 0; bit--)
	{
		if (0xFF & (1 << bit))
			stm[bitpos / 8] |= 0x80 >> (bitpos & 7);
		bitpos++;
	}

	const uint final_sz = 8 + freq_tbl_size + (bitpos + 7) / 8;
	*dest = out;
	*dest_size = final_sz;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		LZX Compression (0x19 Extended LZ11)		///////////////
//-----------------------------------------------------------------------------

int CxIsCompressedLZX (const unsigned char *buffer, unsigned int size)
{
	if (!buffer || size < 4 || buffer[0] != 0x19)
		return 0;
	const uint uncomp_size = ((uint)buffer[1]) | ((uint)buffer[2] << 8) | ((uint)buffer[3] << 16);
	return (uncomp_size > 0 && uncomp_size <= NFMT_MAX_OUTPUT);
}

enumError DecodeLZX (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !CxIsCompressedLZX (src, src_size))
		return EINVAL;

	const uint uncomp_size = ((uint)src[1]) | ((uint)src[2] << 8) | ((uint)src[3] << 16);
	enumError err = alloc_output (dest, dest_size, uncomp_size);
	if (err)
		return err;

	u8 *out = *dest;
	uint offset = 4, dst_offset = 0;

	while (offset < src_size && dst_offset < uncomp_size)
	{
		u8 head = src[offset++];
		for (int i = 0; i < 8 && dst_offset < uncomp_size; i++)
		{
			const bool is_ref = (head & 0x80) != 0;
			head <<= 1;

			if (!is_ref)
			{
				if (offset >= src_size) goto fail_lzx;
				out[dst_offset++] = src[offset++];
			}
			else
			{
				if (offset + 2 > src_size) goto fail_lzx;
				const u8 high = src[offset++];
				const u8 low = src[offset++];
				const uint mode = high >> 4;
				uint len = 0, offs = 0;

				if (mode == 0)
				{
					if (offset >= src_size) goto fail_lzx;
					const u8 low2 = src[offset++];
					len = ((high << 4) | (low >> 4)) + 0x11;
					offs = (((low & 0xF) << 8) | low2) + 1;
				}
				else if (mode == 1)
				{
					if (offset + 2 > src_size) goto fail_lzx;
					const u8 low2 = src[offset++];
					const u8 low3 = src[offset++];
					len = (((high & 0xF) << 12) | (low << 4) | (low2 >> 4)) + 0x111;
					offs = (((low2 & 0xF) << 8) | low3) + 1;
				}
				else
				{
					len = (high >> 4) + 1;
					offs = (((high & 0xF) << 8) | low) + 1;
				}

				if (offs > dst_offset || len > uncomp_size - dst_offset)
					goto fail_lzx;

				for (uint j = 0; j < len; j++)
				{
					out[dst_offset] = out[dst_offset - offs];
					dst_offset++;
				}
			}
		}
	}

	if (dst_offset == uncomp_size)
		return ERR_OK;
fail_lzx:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeLZX (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	// Use short/long token encoder similar to LZ11 with 0x19 header
	const uint max_out = 4 + src_size + (src_size / 8 + 1) * 2 + 16;
	u8 *out = CALLOC (1, max_out);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 0x19;
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);

	uint srcpos = 0, dstpos = 4;
	while (srcpos < src_size)
	{
		u8 *flag_byte = out + dstpos++;
		*flag_byte = 0;

		for (int bit = 7; bit >= 0 && srcpos < src_size; bit--)
		{
			// Search for LZ match
			uint best_len = 0, best_dist = 0;
			const uint max_dist = srcpos < 4096 ? srcpos : 4096;
			const uint max_len = src_size - srcpos < 0xFFFF + 0x111 ? src_size - srcpos : 0xFFFF + 0x111;

			if (max_len >= 3)
			{
				for (uint d = 1; d <= max_dist; d++)
				{
					uint l = 0;
					while (l < max_len && src[srcpos + l] == src[srcpos - d + l])
						l++;
					if (l > best_len && l >= 3)
					{
						best_len = l;
						best_dist = d;
						if (best_len >= 256)
							break;
					}
				}
			}

			if (best_len >= 3)
			{
				*flag_byte |= (1 << bit);
				const uint d = best_dist - 1;
				if (best_len <= 16)
				{
					// Mode 2..15: 4-bit length - 1, 12-bit offset
					const u8 hi = (u8)(((best_len - 1) << 4) | ((d >> 8) & 0xF));
					const u8 lo = (u8)(d & 0xFF);
					out[dstpos++] = hi;
					out[dstpos++] = lo;
				}
				else if (best_len <= 0xFF + 0x11)
				{
					// Mode 0: 8-bit length - 0x11, 12-bit offset
					const uint adj = best_len - 0x11;
					out[dstpos++] = (u8)(adj >> 4);
					out[dstpos++] = (u8)(((adj & 0xF) << 4) | ((d >> 8) & 0xF));
					out[dstpos++] = (u8)(d & 0xFF);
				}
				else
				{
					// Mode 1: 16-bit length - 0x111, 12-bit offset
					const uint adj = best_len - 0x111;
					out[dstpos++] = (u8)(0x10 | ((adj >> 12) & 0xF));
					out[dstpos++] = (u8)((adj >> 4) & 0xFF);
					out[dstpos++] = (u8)(((adj & 0xF) << 4) | ((d >> 8) & 0xF));
					out[dstpos++] = (u8)(d & 0xFF);
				}
				srcpos += best_len;
			}
			else
			{
				out[dstpos++] = src[srcpos++];
			}
		}
	}

	*dest = out;
	*dest_size = dstpos;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		Differential Filter (0x80 / 0x81)			///////////////
//-----------------------------------------------------------------------------

enumError DecodeDiff8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4 || src[0] != 0x80)
		return EINVAL;
	const uint uncomp_size = ((uint)src[1]) | ((uint)src[2] << 8) | ((uint)src[3] << 16);
	if (!uncomp_size || uncomp_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	enumError err = alloc_output (dest, dest_size, uncomp_size);
	if (err)
		return err;

	u8 *out = *dest;
	u8 prev = 0;
	for (uint i = 0; i < uncomp_size && 4 + i < src_size; i++)
	{
		prev += src[4 + i];
		out[i] = prev;
	}
	return ERR_OK;
}

enumError EncodeDiff8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	u8 *out = CALLOC (1, 4 + src_size);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 0x80;
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);

	u8 prev = 0;
	for (uint i = 0; i < src_size; i++)
	{
		out[4 + i] = src[i] - prev;
		prev = src[i];
	}

	*dest = out;
	*dest_size = 4 + src_size;
	return ERR_OK;
}

enumError DecodeDiff16 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4 || src[0] != 0x81)
		return EINVAL;
	const uint uncomp_size = ((uint)src[1]) | ((uint)src[2] << 8) | ((uint)src[3] << 16);
	if (!uncomp_size || uncomp_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	enumError err = alloc_output (dest, dest_size, uncomp_size);
	if (err)
		return err;

	u8 *out = *dest;
	u16 prev = 0;
	for (uint i = 0; i + 1 < uncomp_size && 4 + i + 1 < src_size; i += 2)
	{
		const u16 diff = (u16)src[4 + i] | ((u16)src[4 + i + 1] << 8);
		prev += diff;
		out[i] = (u8)(prev & 0xFF);
		out[i + 1] = (u8)(prev >> 8);
	}
	return ERR_OK;
}

enumError EncodeDiff16 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	const uint out_sz = ((src_size + 1) & ~1u) + 4;
	u8 *out = CALLOC (1, out_sz);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 0x81;
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);

	u16 prev = 0;
	for (uint i = 0; i < src_size; i += 2)
	{
		const u16 val = (i + 1 < src_size)
			? ((u16)src[i] | ((u16)src[i + 1] << 8))
			: (u16)src[i];
		const u16 diff = val - prev;
		out[4 + i] = (u8)(diff & 0xFF);
		out[4 + i + 1] = (u8)(diff >> 8);
		prev = val;
	}

	*dest = out;
	*dest_size = out_sz;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		NDS Reverse Overlay Compression (LZOvl)	///////////////
//-----------------------------------------------------------------------------

int CxIsCompressedLZOvl (const unsigned char *src, unsigned int size)
{
	if (!src || size < 8)
		return 0;
	const u32 extra = (u32)src[size - 4] | ((u32)src[size - 3] << 8)
		| ((u32)src[size - 2] << 16) | ((u32)src[size - 1] << 24);
	if (extra == 0 || extra > NFMT_MAX_OUTPUT)
		return 0;
	const u8 hdr_len = src[size - 5];
	if (hdr_len < 8 || hdr_len > size)
		return 0;
	const u32 comp_len = (u32)src[size - 8] | ((u32)src[size - 7] << 8) | ((u32)src[size - 6] << 16);
	if (comp_len > size - hdr_len)
		return 0;
	return 1;
}

enumError DecodeLZOvl (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 8)
		return EINVAL;

	const u32 extra = (u32)src[src_size - 4] | ((u32)src[src_size - 3] << 8)
		| ((u32)src[src_size - 2] << 16) | ((u32)src[src_size - 1] << 24);
	if (extra == 0)
	{
		const uint out_len = src_size - 4;
		u8 *out = MALLOC (out_len);
		if (!out) return ERR_CANT_CREATE;
		memcpy (out, src, out_len);
		*dest = out;
		*dest_size = out_len;
		return ERR_OK;
	}

	const u8 hdr_len = src[src_size - 5];
	if (hdr_len < 8 || hdr_len > src_size)
		return EINVAL;

	const u32 comp_len = (u32)src[src_size - 8] | ((u32)src[src_size - 7] << 8) | ((u32)src[src_size - 6] << 16);
	const u32 uncomp_len = src_size - hdr_len - comp_len;
	const u32 total_out = src_size + extra;

	if (total_out > NFMT_MAX_OUTPUT || total_out < uncomp_len)
		return EINVAL;

	u8 *out = MALLOC (total_out);
	if (!out)
		return ERR_CANT_CREATE;

	if (uncomp_len)
		memcpy (out, src, uncomp_len);

	uint src_pos = src_size - hdr_len;
	uint out_pos = total_out;

	while (out_pos > uncomp_len && src_pos > uncomp_len)
	{
		const u8 flags = src[--src_pos];
		for (int b = 7; b >= 0 && out_pos > uncomp_len; b--)
		{
			if ((flags & (1 << b)) == 0)
			{
				if (src_pos <= uncomp_len) break;
				out[--out_pos] = src[--src_pos];
			}
			else
			{
				if (src_pos < uncomp_len + 2) break;
				const u8 b1 = src[--src_pos];
				const u8 b2 = src[--src_pos];
				const uint len = (b1 >> 4) + 3;
				const uint disp = (((b1 & 0xF) << 8) | b2) + 3;
				if (out_pos + disp > total_out || len > out_pos - uncomp_len)
					break;
				for (uint i = 0; i < len; i++)
				{
					out_pos--;
					out[out_pos] = out[out_pos + disp];
				}
			}
		}
	}

	*dest = out;
	*dest_size = total_out;
	return ERR_OK;
}

enumError EncodeLZOvl (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	// Uncompressed overlay format: raw bytes followed by 4 zero bytes
	const uint total_sz = src_size + 4;
	u8 *out = MALLOC (total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, src, src_size);
	out[src_size + 0] = 0;
	out[src_size + 1] = 0;
	out[src_size + 2] = 0;
	out[src_size + 3] = 0;

	*dest = out;
	*dest_size = total_sz;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		Jump Ultimate Stars Archive (ALAR)		///////////////
//-----------------------------------------------------------------------------

enumError DecodeALAR (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 16 || memcmp (src, "ALAR", 4))
		return EINVAL;

	const u8 type = src[4];
	if (type == 2)
	{
		const uint num_files = (uint)src[6] | ((uint)src[7] << 8);
		if (!num_files || 16 + num_files * 16 > src_size)
			return EINVAL;
		const uint ofs0 = (uint)src[16 + 4] | ((uint)src[16 + 5] << 8) | ((uint)src[16 + 6] << 16) | ((uint)src[16 + 7] << 24);
		const uint sz0 = (uint)src[16 + 8] | ((uint)src[16 + 9] << 8) | ((uint)src[16 + 10] << 16) | ((uint)src[16 + 11] << 24);
		if (ofs0 + sz0 <= src_size && sz0 > 0)
		{
			u8 *out = MALLOC (sz0);
			if (!out) return ERR_CANT_CREATE;
			memcpy (out, src + ofs0, sz0);
			*dest = out;
			*dest_size = sz0;
			return ERR_OK;
		}
	}
	else if (type == 3)
	{
		const uint num_files = (uint)src[6] | ((uint)src[7] << 8) | ((uint)src[8] << 16) | ((uint)src[9] << 24);
		if (!num_files || src_size < 32)
			return EINVAL;
		const uint ofs0 = (uint)src[16 + 4] | ((uint)src[16 + 5] << 8) | ((uint)src[16 + 6] << 16) | ((uint)src[16 + 7] << 24);
		const uint sz0 = (uint)src[16 + 8] | ((uint)src[16 + 9] << 8) | ((uint)src[16 + 10] << 16) | ((uint)src[16 + 11] << 24);
		if (ofs0 + sz0 <= src_size && sz0 > 0)
		{
			u8 *out = MALLOC (sz0);
			if (!out) return ERR_CANT_CREATE;
			memcpy (out, src + ofs0, sz0);
			*dest = out;
			*dest_size = sz0;
			return ERR_OK;
		}
	}
	return EINVAL;
}

//-----------------------------------------------------------------------------
///////////////		Level-5 / Layton Archive (DARC)			///////////////
//-----------------------------------------------------------------------------

enumError DecodeDARC (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 12 || memcmp (src, "DARC", 4))
		return EINVAL;

	const uint num_files = (uint)src[4] | ((uint)src[5] << 8) | ((uint)src[6] << 16) | ((uint)src[7] << 24);
	if (!num_files || 8 + num_files * 4 > src_size)
		return EINVAL;

	const uint rel_ofs0 = (uint)src[8] | ((uint)src[9] << 8) | ((uint)src[10] << 16) | ((uint)src[11] << 24);
	const uint abs_ofs0 = 8 + 4 + rel_ofs0;
	if (abs_ofs0 >= 4 && abs_ofs0 <= src_size)
	{
		const uint sz0 = (uint)src[abs_ofs0 - 4] | ((uint)src[abs_ofs0 - 3] << 8)
			| ((uint)src[abs_ofs0 - 2] << 16) | ((uint)src[abs_ofs0 - 1] << 24);
		if (abs_ofs0 + sz0 <= src_size && sz0 > 0)
		{
			u8 *out = MALLOC (sz0);
			if (!out) return ERR_CANT_CREATE;
			memcpy (out, src + abs_ofs0, sz0);
			*dest = out;
			*dest_size = sz0;
			return ERR_OK;
		}
	}
	return EINVAL;
}

//-----------------------------------------------------------------------------
///////////////		Level-5 SADL Audio Stream -> WAV		///////////////
//-----------------------------------------------------------------------------

enumError DecodeSADL_WAV (u8 **dest_wav, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest_wav || !dest_size || !src || src_size < 0x100 || memcmp (src, "SADL", 4))
		return EINVAL;

	const uint channels = src[0x32] ? src[0x32] : 1;
	const u8 coding = src[0x33];
	const uint sample_rate = (coding & 6) == 4 ? 32728 : 16364;
	const uint file_sz = (uint)src[0x40] | ((uint)src[0x41] << 8) | ((uint)src[0x42] << 16) | ((uint)src[0x43] << 24);
	const uint data_sz = file_sz > 0x100 && file_sz <= src_size ? file_sz - 0x100 : src_size - 0x100;
	const uint num_samples = (data_sz / channels) * 2;

	if (!num_samples || num_samples > (64u << 20))
		return EINVAL;

	const uint wav_hdr_sz = 44;
	const uint pcm_bytes = num_samples * channels * 2;
	u8 *wav = CALLOC (1, wav_hdr_sz + pcm_bytes);
	if (!wav)
		return ERR_CANT_CREATE;

	// Write WAV Header
	memcpy (wav, "RIFF", 4);
	const u32 riff_sz = 36 + pcm_bytes;
	wav[4] = (u8)(riff_sz & 0xFF); wav[5] = (u8)((riff_sz >> 8) & 0xFF);
	wav[6] = (u8)((riff_sz >> 16) & 0xFF); wav[7] = (u8)((riff_sz >> 24) & 0xFF);
	memcpy (wav + 8, "WAVEfmt ", 8);
	wav[16] = 16;
	wav[20] = 1; // PCM
	wav[22] = (u8)channels;
	wav[24] = (u8)(sample_rate & 0xFF); wav[25] = (u8)((sample_rate >> 8) & 0xFF);
	wav[26] = (u8)((sample_rate >> 16) & 0xFF); wav[27] = (u8)((sample_rate >> 24) & 0xFF);
	const u32 byte_rate = sample_rate * channels * 2;
	wav[28] = (u8)(byte_rate & 0xFF); wav[29] = (u8)((byte_rate >> 8) & 0xFF);
	wav[30] = (u8)((byte_rate >> 16) & 0xFF); wav[31] = (u8)((byte_rate >> 24) & 0xFF);
	wav[32] = (u8)(channels * 2);
	wav[34] = 16;
	memcpy (wav + 36, "data", 4);
	wav[40] = (u8)(pcm_bytes & 0xFF); wav[41] = (u8)((pcm_bytes >> 8) & 0xFF);
	wav[42] = (u8)((pcm_bytes >> 16) & 0xFF); wav[43] = (u8)((pcm_bytes >> 24) & 0xFF);

	short *pcm = (short *)(wav + wav_hdr_sz);
	const u8 *in_data = src + 0x100;
	static const short index_table[16] = {
		-1, -1, -1, -1, 2, 4, 6, 8,
		-1, -1, -1, -1, 2, 4, 6, 8
	};
	static const short stepsize_table[89] = {
		7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
		50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
		253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
		1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
		3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
		11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
	};

	int sample = 0;
	int index = 0;
	uint s_idx = 0;
	for (uint i = 0; i < data_sz && s_idx < num_samples; i++)
	{
		const u8 byte = in_data[i];
		for (int nib = 0; nib < 2 && s_idx < num_samples; nib++)
		{
			const u8 delta = nib == 0 ? (byte & 0x0F) : (byte >> 4);
			int step = stepsize_table[index];
			int diff = step >> 3;
			if (delta & 1) diff += step >> 2;
			if (delta & 2) diff += step >> 1;
			if (delta & 4) diff += step;
			if (delta & 8) sample -= diff; else sample += diff;
			if (sample > 32767) sample = 32767;
			if (sample < -32768) sample = -32768;
			index += index_table[delta];
			if (index < 0) index = 0;
			if (index > 88) index = 88;
			pcm[s_idx++] = (short)sample;
		}
	}

	*dest_wav = wav;
	*dest_size = wav_hdr_sz + pcm_bytes;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		Prosonic SDK (PSDK) Compression			///////////////
//-----------------------------------------------------------------------------

enumError DecodePSDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 8 || memcmp (src, "PSDK", 4))
		return EINVAL;

	const u32 uncomp_sz = (u32)src[4] | ((u32)src[5] << 8) | ((u32)src[6] << 16) | ((u32)src[7] << 24);
	if (!uncomp_sz || uncomp_sz > NFMT_MAX_OUTPUT)
		return EINVAL;

	u8 *out = MALLOC (uncomp_sz);
	if (!out)
		return ERR_CANT_CREATE;

	uint src_pos = 8;
	if (src_size >= 12 && src[8] == 0 && src[9] == 0 && src[10] == 0 && src[11] == 0)
		src_pos = 12;

	uint out_pos = 0;
	while (out_pos < uncomp_sz && src_pos < src_size)
	{
		const u8 flags = src[src_pos++];
		for (int b = 0; b < 8 && out_pos < uncomp_sz && src_pos < src_size; b++)
		{
			if (flags & (1 << b))
			{
				out[out_pos++] = src[src_pos++];
			}
			else
			{
				if (src_pos + 1 >= src_size)
					break;
				const u8 b1 = src[src_pos++];
				const u8 b2 = src[src_pos++];
				const uint disp = (((b2 & 0xF0) << 4) | b1) + 1;
				const uint len = (b2 & 0x0F) + 3;
				if (disp > out_pos)
					break;
				for (uint i = 0; i < len && out_pos < uncomp_sz; i++)
				{
					out[out_pos] = out[out_pos - disp];
					out_pos++;
				}
			}
		}
	}

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;
}

enumError EncodePSDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	const uint n_chunks = (src_size + 7) / 8;
	const uint total_sz = 8 + src_size + n_chunks;
	u8 *out = MALLOC (total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "PSDK", 4);
	out[4] = (u8)(src_size & 0xFF);
	out[5] = (u8)((src_size >> 8) & 0xFF);
	out[6] = (u8)((src_size >> 16) & 0xFF);
	out[7] = (u8)((src_size >> 24) & 0xFF);

	uint src_pos = 0;
	uint out_pos = 8;
	while (src_pos < src_size)
	{
		const uint take = src_size - src_pos > 8 ? 8 : src_size - src_pos;
		out[out_pos++] = 0xFF; // all literals
		for (uint i = 0; i < take; i++)
			out[out_pos++] = src[src_pos++];
	}

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;
}

