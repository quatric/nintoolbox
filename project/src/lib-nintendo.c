#include <zlib.h>
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-quicklz.h"
#include "lib-bflyt.h"
#include "lib-bntx.h"
#include "lib-gtx.h"
#include "lib-aes.h"

__attribute__((weak)) bool IsQuickLZ (const u8 *src, uint src_size) { (void)src; (void)src_size; return false; }
__attribute__((weak)) enumError DecodeQuickLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size) { (void)dest; (void)dest_size; (void)src; (void)src_size; return ERR_INVALID_DATA; }
__attribute__((weak)) enumError EncodeQuickLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size) { (void)dest; (void)dest_size; (void)src; (void)src_size; return ERR_INVALID_DATA; }
__attribute__((weak)) enumError EncodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool is_lz11) { (void)dest; (void)dest_size; (void)src; (void)src_size; (void)is_lz11; return ERR_INVALID_DATA; }
__attribute__((weak)) enumError DecodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size) { (void)dest; (void)dest_size; (void)src; (void)src_size; return ERR_INVALID_DATA; }

ccp GetNintendoFormatName (nfmt_type_t type)
{
	static const ccp tab[] = { "UNKNOWN", "DSB", "TPL", "STPL", "SARC", "LZ10", "LZ11", "HUFF4",
		"HUFF8", "RL", "ASH0", "Yay0", "LZH8", "BFLIM", "BCLIM", "NUTEXB", "BNR", "NCGR", "NCLR", "NCER",
		"NANR", "BRFNT", "BRFNA", "BCFNT", "BRLAN", "BRLYT", "BFLAN", "BFLYT", "BCLAN", "BCLYT",
		"PLT0", "MSBT", "BCRES", "BFRES", "BNTX", "GFA", "BCH", "QuickLZ", "PAC", "RNC", "romc",
		"PSDK", "AT7", "CTPK", "BYML", "NARC", "NSCR", "FZIP", "JARC", "jCMP", "BFMA", "Zlib", "MVDK",
		"VLX", "PuCrunch", "LZX", "Diff8", "Diff16", "NSBTX", "NFTR", "BNFR", "BNLL", "BNCL", "BNBL",
		"LZOvl", "ALAR", "DARC", "SADL", "HSF", "HSD", "BNFM", "XPCK", "XIMG", "HGO", "ZTAB", "GLG",
		"MDR", "PERS", "PVOL", "STPK", "G1M", "G1T", "G4PKM", "LMD" };
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
		if (size >= 8 && (!memcmp (d, "HSFV", 4) || !memcmp (d, "HSF\0", 4) || (d[0] == 'H' && d[1] == 'S' && d[2] == 'F' && d[3] == 'V')))
			return make_info (NFMT_HSF, true, false, 0);
		if (size >= 0x20)
		{
			const u32 fs = rd_be32 (d);
			const u32 ds = rd_be32 (d + 4);
			const u32 roots = rd_be32 (d + 12);
			const u32 refs = rd_be32 (d + 16);
			if (fs >= 0x20 && ds > 0 && ds <= fs && (roots > 0 || refs > 0) && roots < 0x10000 && refs < 0x10000
				&& (fs == size || (ext && (!strcasecmp (ext, ".dat") || !strcasecmp (ext, ".sys")))))
				return make_info (NFMT_HSD, true, false, 0);
		}
		if (size >= 12 && !memcmp (d, "BNFM", 4))
			return make_info (NFMT_BNFM, true, false, 0);
		if (!memcmp (d, "XPCK", 4) || !memcmp (d, "XPC2", 4))
			return make_info (NFMT_XPCK, false, false, 0);
		if (!memcmp (d, "XIM2", 4) || !memcmp (d, "XIMG", 4) || !memcmp (d, "XINF", 4) || !memcmp (d, "XI\0\0", 4))
			return make_info (NFMT_XIMG, false, false, 0);
		if (!memcmp (d, "0OGH", 4) || !memcmp (d, "0MXT", 4) || !memcmp (d, "0TST", 4) || !memcmp (d, "LBTN", 4)
			|| (size >= 12 && (!memcmp (d + 4, "0OGH", 4) || !memcmp (d + 8, "0OGH", 4))))
			return make_info (NFMT_HGO, true, false, 0);
		if (ext && !strcasecmp (ext, ".hgo"))
			return make_info (NFMT_HGO, true, false, 0);
		if (!memcmp (d, "ZTAB", 4))
			return make_info (NFMT_ZTAB, true, false, 0);
		if (ext && !strcasecmp (ext, ".glg"))
			return make_info (NFMT_GLG, true, false, 0);
		if (ext && !strcasecmp (ext, ".mdr"))
			return make_info (NFMT_MDR, true, false, 0);
		if ((size >= 8 && !memcmp (d, "PERS-SZP", 8)) || (size >= 16 && !memcmp (d + 8, "FRAGMENT", 8)) || !memcmp (d, "FRAGMENT", 8))
			return make_info (NFMT_PERS, true, false, 0);
		if (ext && !strcasecmp (ext, ".pers"))
			return make_info (NFMT_PERS, true, false, 0);
		if (ext && !strcasecmp (ext, ".pvol"))
			return make_info (NFMT_PVOL, false, false, 0);
		if (!memcmp (d, "STPK", 4) || !memcmp (d, "$CFH", 4) || !memcmp (d, "$RSF", 4))
			return make_info (NFMT_STPK, true, false, 0);
		if (!memcmp (d, "G1M_", 4) || !memcmp (d, "G1M\0", 4) || !memcmp (d, "_M1G", 4) || !memcmp (d, "SM1G", 4) || !memcmp (d, "GM1G", 4))
			return make_info (NFMT_G1M, false, false, 0);
		if (!memcmp (d, "G1T_", 4) || !memcmp (d, "G1T\0", 4) || !memcmp (d, "_T1G", 4) || !memcmp (d, "GT1G", 4))
			return make_info (NFMT_G1T, false, false, 0);
		if (ext && !strcasecmp (ext, ".g4pkm"))
			return make_info (NFMT_G4PKM, false, false, 0);
		if (ext && !strcasecmp (ext, ".lmd"))
			return make_info (NFMT_LMD, false, false, 0);
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

enumError AllocOutput (u8 **dest, uint *dest_size, u32 size)
{
	if (!dest || !dest_size || !size || size > NFMT_MAX_OUTPUT)
		return EFBIG;
	*dest = MALLOC (size);
	if (!*dest)
		return ERR_CANT_CREATE;
	*dest_size = size;
	return ERR_OK;
}

static inline enumError alloc_output (u8 **dest, uint *dest_size, u32 size)
{
	return AllocOutput (dest, dest_size, size);
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
bool OwnedEntryAdd (
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
// characters no real member name uses.
bool OwnedNameOk (ccp name)
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

enumError EncodeMVDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x3fffffff)
		return EINVAL;

	u8 *lz_data = 0;
	uint lz_size = 0;
	enumError err = EncodeLZ10LZ11 (&lz_data, &lz_size, src, src_size, false);
	if (err || !lz_data || lz_size < 4)
	{
		u8 *out = MALLOC (4 + src_size);
		if (!out) return ERR_CANT_CREATE;
		u32 hdr = (src_size << 2) | 0;
		out[0] = (u8)hdr; out[1] = (u8)(hdr >> 8); out[2] = (u8)(hdr >> 16); out[3] = (u8)(hdr >> 24);
		memcpy (out + 4, src, src_size);
		*dest = out;
		*dest_size = 4 + src_size;
		return ERR_OK;
	}

	u32 hdr = (src_size << 2) | 1;
	lz_data[0] = (u8)hdr;
	lz_data[1] = (u8)(hdr >> 8);
	lz_data[2] = (u8)(hdr >> 16);
	lz_data[3] = (u8)(hdr >> 24);

	*dest = lz_data;
	*dest_size = lz_size;
	return ERR_OK;
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
	if (!memcmp (src, "Yaz0", 4) || !memcmp (src, "Yay0", 4) || !memcmp (src, "YAY0", 4)
		|| !memcmp (src, "\x55\xaa\x38\x2d", 4) || !memcmp (src, "MESG", 4) || !memcmp (src, "RARC", 4)
		|| !memcmp (src, "RFNT", 4) || !memcmp (src, "RFNA", 4) || !memcmp (src, "CFNT", 4))
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

//-----------------------------------------------------------------------------
///////////////		Namco Museum SSZL LZSS0 Compression		///////////////
//-----------------------------------------------------------------------------

enumError DecodeSSZL (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 16 || memcmp (src, "SSZL", 4))
		return EINVAL;

	const u32 zsize = (u32)src[8] | ((u32)src[9] << 8) | ((u32)src[10] << 16) | ((u32)src[11] << 24);
	const u32 usize = (u32)src[12] | ((u32)src[13] << 8) | ((u32)src[14] << 16) | ((u32)src[15] << 24);

	if (!usize || usize > NFMT_MAX_OUTPUT || 16 + zsize > src_size)
		return EINVAL;

	u8 *out = MALLOC (usize);
	if (!out)
		return ERR_CANT_CREATE;

	u8 ring[4096];
	memset (ring, 0, sizeof (ring));
	uint r = 4096 - 18, ip = 0, op = 0;
	uint flags = 0;
	const u8 *in = src + 16;

	while (op < usize)
	{
		if (!(flags & 0x100))
		{
			if (ip >= zsize)
			{
				FREE (out);
				return EINVAL;
			}
			flags = in[ip++] | 0xff00;
		}
		if (flags & 1)
		{
			if (ip >= zsize)
			{
				FREE (out);
				return EINVAL;
			}
			out[op++] = ring[r] = in[ip++];
			r = (r + 1) & 0xfff;
		}
		else
		{
			if (ip + 1 >= zsize)
			{
				FREE (out);
				return EINVAL;
			}
			uint p = in[ip++];
			const uint b = in[ip++];
			p |= (b & 0xf0) << 4;
			uint n = (b & 0x0f) + 3;
			if (n > usize - op)
			{
				FREE (out);
				return EINVAL;
			}
			while (n--)
			{
				out[op++] = ring[r] = ring[p++ & 0xfff];
				r = (r + 1) & 0xfff;
			}
		}
		flags >>= 1;
	}

	*dest = out;
	*dest_size = usize;
	return ERR_OK;
}

enumError EncodeSSZL (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	const uint n_chunks = (src_size + 7) / 8;
	const uint total_sz = 16 + src_size + n_chunks;
	u8 *out = MALLOC (total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "SSZL", 4);
	out[4] = out[5] = out[6] = out[7] = 0;

	uint src_pos = 0;
	uint out_pos = 16;
	while (src_pos < src_size)
	{
		const uint take = src_size - src_pos > 8 ? 8 : src_size - src_pos;
		out[out_pos++] = 0xFF; // 8 literal bits
		for (uint i = 0; i < take; i++)
			out[out_pos++] = src[src_pos++];
	}

	const u32 zsize = out_pos - 16;
	out[8] = (u8)(zsize & 0xFF);
	out[9] = (u8)((zsize >> 8) & 0xFF);
	out[10] = (u8)((zsize >> 16) & 0xFF);
	out[11] = (u8)((zsize >> 24) & 0xFF);

	out[12] = (u8)(src_size & 0xFF);
	out[13] = (u8)((src_size >> 8) & 0xFF);
	out[14] = (u8)((src_size >> 16) & 0xFF);
	out[15] = (u8)((src_size >> 24) & 0xFF);

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;
}


