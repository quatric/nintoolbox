#include <zlib.h>
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-retro-txtr.h"
#include "lib-quicklz.h"
#include "lib-bflyt.h"
#include "lib-bntx.h"
#include "lib-gtx.h"
#include "lib-aes.h"

__attribute__ ((weak)) bool IsQuickLZ (const u8 *src, uint src_size)
{
	(void)src;
	(void)src_size;
	return false;
}

// Retro Studios TXTR sniffers live in lib-retro-txtr.o (XOBJ_IMAGE), which
// not every link target pulls in (cf. IsQuickLZ above). These stubs keep
// SZS_O-only helpers linkable; image tools override them with the real
// parsers and therefore report the real NFMT.
__attribute__ ((weak)) bool IsRetroTXTR (const u8 *data, uint size)
{
	(void)data;
	(void)size;
	return false;
}
__attribute__ ((weak)) bool IsTropicalTXTR (const u8 *data, uint size)
{
	(void)data;
	(void)size;
	return false;
}
// Same pattern for the Remastered TXTR probe (also lib-retro-txtr.o).
__attribute__ ((weak)) bool IsMPRTXTR (const u8 *data, uint size)
{
	(void)data;
	(void)size;
	return false;
}
// Same pattern for the Remastered CMDL probe (lib-mpr-cmdl.o lives in
// XOBJ_LIB with the other model parsers, likewise not linked everywhere).
__attribute__ ((weak)) bool IsMPRCMDL (const u8 *data, uint size)
{
	(void)data;
	(void)size;
	return false;
}
// Same pattern for the G1T wrapper probe (lib-g1t.o lives in XOBJ_LIB
// with the other archive formats, likewise not linked everywhere).
__attribute__ ((weak)) bool IsG1TGZ (const u8 *data, uint size)
{
	(void)data;
	(void)size;
	return false;
}
__attribute__ ((weak)) enumError DecodeQuickLZ (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	(void)dest;
	(void)dest_size;
	(void)src;
	(void)src_size;
	return ERR_INVALID_DATA;
}
__attribute__ ((weak)) enumError EncodeQuickLZ (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	(void)dest;
	(void)dest_size;
	(void)src;
	(void)src_size;
	return ERR_INVALID_DATA;
}
__attribute__ ((weak)) enumError EncodeLZ10LZ11 (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool is_lz11)
{
	(void)dest;
	(void)dest_size;
	(void)src;
	(void)src_size;
	(void)is_lz11;
	return ERR_INVALID_DATA;
}
__attribute__ ((weak)) enumError DecodeLZ10LZ11 (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	(void)dest;
	(void)dest_size;
	(void)src;
	(void)src_size;
	return ERR_INVALID_DATA;
}

ccp GetNintendoFormatName (nfmt_type_t type)
{
	static const ccp tab[] = { "UNKNOWN", "DSB", "TPL", "STPL", "SARC", "LZ10", "LZ11", "HUFF4",
		"HUFF8", "RL", "ASH0", "Yay0", "LZH8", "BFLIM", "BCLIM", "NUTEXB", "BNR", "NCGR", "NCLR",
		"NCER", "NANR", "BRFNT", "BRFNA", "BCFNT", "BRLAN", "BRLYT", "BFLAN", "BFLYT", "BCLAN",
		"BCLYT", "PLT0", "MSBT", "BCRES", "BFRES", "BNTX", "GFA", "BCH", "QuickLZ", "PAC", "RNC",
		"romc", "PSDK", "AT7", "CTPK", "BYML", "NARC", "NSCR", "FZIP", "JARC", "jCMP", "BFMA",
		"Zlib", "MVDK", "VLX", "PuCrunch", "LZX", "Diff8", "Diff16", "NSBTX", "NFTR", "BNFR",
		"BNLL", "BNCL", "BNBL", "LZOvl", "ALAR", "DARC", "SADL", "HSF", "HSD", "BNFM", "XPCK",
		"XIMG", "ZTAB", "GLG", "MDR", "PERS", "PVOL", "STPK", "G1M", "G1T", "G4PKM", "LMD", "MSH",
		"MOD", "GAR", "TEX3DS", "BCSTM", "BFSTM", "BCWAV", "BFWAV", "BNSH", "GFBMDL", "GFBANM",
		"BNSTX", "AAMP", 		"MIO", "ZDAT", "SFX", "VFF", "TM0", "RETRO-TXTR", "TROPICAL-TXTR",
		"MPR-PACK",
		"MPR-TXTR",
		"MPR-CMDL" };
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
		if ((size == 65536 || size == 14336 || size == 8192) && size >= 16
			&& !memcmp (d + 8, "DSMIO_S\0", 8))
			return make_info (NFMT_MIO, false, false, (u32)size);
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
		// RFRM is a strong 4-byte magic owned by Retro Studios'
		// form family (Tropical Freeze + Remastered resources and
		// containers). Resolve it here, completely, and never let it
		// fall through to the single-byte compression guesses below:
		// an RFRM form size with 0x10/0x11/0x40 in its low byte trips
		// the CX00-prefix LZ10/LZ11 and LZH8 checks (found live: real
		// Remastered TXTRs misreported as LZ10/LZ11 streams, aborting
		// image decode). No genuine compressed stream starts with
		// RFRM, so UNKNOWN is the honest answer for anything the
		// family probes decline.
		if (!memcmp (d, "RFRM", 4))
		{
			if (IsTropicalTXTR (d, size))
				return make_info (NFMT_TROPICAL_TXTR, true, false, 0);
			if (size >= 0x20 && !memcmp (d + 0x14, "PACK", 4) && IsMPRPACK (d, size))
				return make_info (NFMT_MPR_PACK, false, false, 0);
			if (IsMPRTXTR (d, size))
				return make_info (NFMT_MPR_TXTR, false, false, 0);
			if (size >= 0x20
				&& (!memcmp (d + 0x14, "CMDL", 4) || !memcmp (d + 0x14, "SMDL", 4))
				&& IsMPRCMDL (d, size))
				return make_info (NFMT_MPR_CMDL, false, false, 0);
			return make_info (NFMT_UNKNOWN, true, false, 0);
		}
		if (IsRetroTXTR (d, size))
			return make_info (NFMT_RETRO_TXTR, true, false, 0);
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
		if (!memcmp (d, "RTNF", 4) || !memcmp (d, "FNTR", 4)
			|| !memcmp (d, "RTFN", 4) || !memcmp (d, "NFTR", 4))
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
		if (size >= 8
			&& (!memcmp (d, "HSFV", 4) || !memcmp (d, "HSF\0", 4)
				|| (d[0] == 'H' && d[1] == 'S' && d[2] == 'F' && d[3] == 'V')))
			return make_info (NFMT_HSF, true, false, 0);
		if (size >= 0x20)
		{
			const u32 fs = rd_be32 (d);
			const u32 ds = rd_be32 (d + 4);
			const u32 roots = rd_be32 (d + 12);
			const u32 refs = rd_be32 (d + 16);
			if (fs >= 0x20 && ds > 0 && ds <= fs && (roots > 0 || refs > 0) && roots < 0x10000
				&& refs < 0x10000
				&& (fs == size
					|| (ext && (!strcasecmp (ext, ".dat") || !strcasecmp (ext, ".sys")))))
				return make_info (NFMT_HSD, true, false, 0);
		}
		if (size >= 12 && !memcmp (d, "BNFM", 4))
			return make_info (NFMT_BNFM, true, false, 0);
		if (!memcmp (d, "XPCK", 4) || !memcmp (d, "XPC2", 4))
			return make_info (NFMT_XPCK, false, false, 0);
		if (!memcmp (d, "XIM2", 4) || !memcmp (d, "XIMG", 4) || !memcmp (d, "XINF", 4)
			|| !memcmp (d, "XI\0\0", 4))
			return make_info (NFMT_XIMG, false, false, 0);
		// Animal Crossing: Pocket Camp asset container. Header offsets are
		// 16-bit: the entry array starts at 0x20 and the names begin directly
		// after it, so the name offset is fixed by the entry count. Checking
		// that relationship keeps four common letters from claiming unrelated
		// data, and holds however many files the container carries.
		if (size >= 0x30 && !memcmp (d, "ZDAT", 4) && rd_le16 (d + 0x06) == 0x20
			&& rd_le16 (d + 0x12) > 0 && rd_le16 (d + 0x0a) == 0x20 + rd_le16 (d + 0x12) * 16)
			return make_info (NFMT_ZDAT, false, false, 0);
		// Monster Games .sfx audio: no magic, so it must identify itself by
		// its own arithmetic -- a 0x80 header whose payload size accounts for
		// the rest of the file, at a rate a console mixer uses, with the byte
		// rate the decoded 16-bit mono form implies.
		if (size > 0x80 && rd_le32 (d + 4) == 0x80 && (u64)rd_le32 (d) + 0x80 == size)
		{
			const u32 rate = rd_le32 (d + 0x10);
			if (rate && rate <= 48000 && rd_le32 (d + 0x14) == rate * 2)
				return make_info (NFMT_SFX, false, false, 0);
		}
		// A VFF volume: the magic plus a byte-order mark, checked together so
		// the four letters alone cannot claim a file.
		if (size > 0x20 && !memcmp (d, "VFF ", 4)
			&& (rd_be16 (d + 4) == 0xfeff || rd_be16 (d + 4) == 0xfffe))
			return make_info (NFMT_VFF, true, false, 0);
		// Monster Games .tm0 texture: no magic either, so it is identified
		// the same way ScanTM0() in lib-excite.c does -- a header at 0x80
		// (u16 width, u16 height, u8 levels, u8 renderer code) whose CMPR
		// mip-chain size (one chain for renderer code 0x42, two -- colour
		// plus a same-size I4 stencil chain, each 128 bytes shorter than a
		// full chain -- for 0x44) exactly accounts for the remaining
		// payload. Duplicated here (rather than calling ScanTM0()) since
		// this detector must stay self-contained, not pull the image
		// decoder into every tool that links this file.
		if (size > 0x100)
		{
			static const uint tm0_dims[] = { 4, 8, 16, 32, 64, 128, 256, 512, 1024 };
			const u8 *hdr = d + 0x80;
			const uint w = rd_le16 (hdr), h = rd_le16 (hdr + 2);
			const uint levels = hdr[4], code = hdr[5];
			bool wok = false, hok = false;
			for (uint i = 0; i < sizeof (tm0_dims) / sizeof (tm0_dims[0]); i++)
			{
				if (tm0_dims[i] == w)
					wok = true;
				if (tm0_dims[i] == h)
					hok = true;
			}
			if (wok && hok && levels >= 1 && levels <= 10 && code >= 0x40 && code <= 0x4f)
			{
				u64 chain = 0;
				for (uint i = 0; i < levels; i++)
				{
					const uint lw = w >> i ? w >> i : 1, lh = h >> i ? h >> i : 1;
					const uint tw = (lw + 7) / 8 * 8, th = (lh + 7) / 8 * 8;
					chain += (u64)tw * th / 2; // CMPR: 4bpp over 8x8 tiles
				}
				if (chain > 0x80)
				{
					const u64 tail = chain - 0x80;
					const u64 psize = size - 0x100;
					if (psize == tail || psize == chain + tail)
						return make_info (NFMT_TM0, false, false, 0);
				}
			}
		}
		if (!memcmp (d, "ZTAB", 4))
			return make_info (NFMT_ZTAB, true, false, 0);
		// Next Level Games container (Super Mario Strikers' .glg, Mario
		// Strikers Charged's .rlg): tag 0x8001b0xx for models and
		// 0x8001b1xx for stages, then a u32 length covering the rest of the
		// file. These are never compressed, but the leading 0x80 and the
		// bytes after it keep tripping the one-byte compression guesses
		// further down -- as LZOVL, DIFF8, LZ10 and LZMA in turn -- and each
		// then fails to decompress and aborts the load before any model code
		// sees the file. Match on content here, since the extension test
		// just below never fires from GetByMagicFF(), which passes no name.
		if (size >= 8 && d[0] == 0x80 && d[1] == 0x01 && (d[2] & 0xf0) == 0xb0
			&& (u64)rd_be32 (d + 4) + 8 <= size)
			return make_info (NFMT_GLG, true, false, 0);
		if (ext && (!strcasecmp (ext, ".glg") || !strcasecmp (ext, ".rlg")))
			return make_info (NFMT_GLG, true, false, 0);
		if (ext && !strcasecmp (ext, ".mdr"))
			return make_info (NFMT_MDR, true, false, 0);
		if ((size >= 8 && !memcmp (d, "PERS-SZP", 8))
			|| (size >= 16 && !memcmp (d + 8, "FRAGMENT", 8)) || !memcmp (d, "FRAGMENT", 8))
			return make_info (NFMT_PERS, true, false, 0);
		if (ext && !strcasecmp (ext, ".pers"))
			return make_info (NFMT_PERS, true, false, 0);
		if (ext && !strcasecmp (ext, ".pvol"))
			return make_info (NFMT_PVOL, false, false, 0);
		if (!memcmp (d, "STPK", 4) || !memcmp (d, "$CFH", 4) || !memcmp (d, "$RSF", 4))
			return make_info (NFMT_STPK, true, false, 0);
		if (!memcmp (d, "G1M_", 4) || !memcmp (d, "G1M\0", 4) || !memcmp (d, "_M1G", 4)
			|| !memcmp (d, "SM1G", 4) || !memcmp (d, "GM1G", 4))
			return make_info (NFMT_G1M, false, false, 0);
		if (!memcmp (d, "G1T_", 4) || !memcmp (d, "G1T\0", 4) || !memcmp (d, "_T1G", 4)
			|| !memcmp (d, "GT1G", 4) || !memcmp (d, "G1TG", 4))
			return make_info (NFMT_G1T, false, false, 0);
		if (IsG1TGZ (d, size))
			return make_info (NFMT_G1T, false, false, 0);
		if (ext && !strcasecmp (ext, ".g4pkm"))
			return make_info (NFMT_G4PKM, false, false, 0);
		if (ext && !strcasecmp (ext, ".lmd"))
			return make_info (NFMT_LMD, false, false, 0);
		if (ext && !strcasecmp (ext, ".msh"))
			return make_info (NFMT_MSH, false, false, 0);
		if (ext && !strcasecmp (ext, ".mod"))
			return make_info (NFMT_MOD, false, false, 0);
		if (ext && !strcasecmp (ext, ".tex") && size >= 0x80)
			return make_info (NFMT_TEX3DS, false, false, 0);

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
		if (!memcmp (d, "CSTM", 4))
			return make_info (NFMT_BCSTM, true, false, 0);
		if (!memcmp (d, "FSTM", 4))
			return make_info (NFMT_BFSTM, true, false, 0);
		if (!memcmp (d, "CWAV", 4))
			return make_info (NFMT_BCWAV, true, false, 0);
		if (!memcmp (d, "FWAV", 4))
			return make_info (NFMT_BFWAV, true, false, 0);
		if (!memcmp (d, "BNSH", 4))
			return make_info (NFMT_BNSH, false, false, 0);
		if (!memcmp (d, "PLT0", 4))
			return make_info (NFMT_PLT0, true, false, 0);
		if (size >= 8 && !memcmp (d, "MsgStdBn", 8))
			return make_info (NFMT_MSBT, true, false, 0);
		if (!memcmp (d, "CGFX", 4))
			return make_info (NFMT_BCRES, true, false, 0);
		if (!memcmp (d, "FRES", 4))
			return make_info (NFMT_BFRES, true, false, 0);
		if (!memcmp (d, "BNTX", 4))
			return make_info (NFMT_BNTX, false, false, 0);
		if (!memcmp (d, "NSTX", 4))
			return make_info (NFMT_BNSTX, false, false, 0);
		if (!memcmp (d, "AAMP", 4))
			return make_info (NFMT_AAMP, false, false, 0);
		if ((size == 65536 || size == 14336 || size == 8192) && !memcmp (d + 8, "DSMIO_S\0", 8))
			return make_info (NFMT_MIO, false, false, (u32)size);
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

		// Grezzo 3DS Archive (ZAR\x01 / GAR\x02 / GAR\x03 / GAR\x04 / GAR\x05)
		if (size >= 0x20
			&& (!memcmp (d, "ZAR\x01", 4) || (!memcmp (d, "GAR", 3) && d[3] >= 2 && d[3] <= 5)))
			return make_info (NFMT_GAR, false, false, rd_le32 (d + 4));

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
			return make_info (NFMT_MVDK, false, true, *(u32 *)d >> 2);
		if (size >= 4 && CxIsCompressedVlx (d, size))
			return make_info (NFMT_VLX, false, true, 0);
		if (size >= 8 && CxIsCompressedPuCrunch (d, size))
			return make_info (
				NFMT_PUCRUNCH, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if (d[0] == 0x19 && size >= 4 && CxIsCompressedLZX (d, size))
			return make_info (NFMT_LZX, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if (d[0] == 0x80 && size >= 4)
			return make_info (
				NFMT_DIFF8, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if (d[0] == 0x81 && size >= 4)
			return make_info (
				NFMT_DIFF16, false, true, (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16);
		if ((d[0] == 0x10 || d[0] == 0x11) && size >= 4)
		{
			u32 usize = (u32)d[1] | (u32)d[2] << 8 | (u32)d[3] << 16;
			if (!usize && size >= 8)
				usize = (u32)d[4] | (u32)d[5] << 8 | (u32)d[6] << 16 | (u32)d[7] << 24;
			return make_info (d[0] == 0x10 ? NFMT_LZ10 : NFMT_LZ11, false, true, usize);
		}
		// Some BRRES-family members carry a short, unrecognized tag
		// immediately before an otherwise standard LZ10/LZ11 stream --
		// AquaSpace's (WiiWare) BRRES members are prefixed with "CX00",
		// origin unknown, verified byte-for-byte against a real disc. Mirror
		// the LZH8 wrapped-stream fallback just below: skip the unrecognized
		// prefix and look for the real compression magic just past it,
		// instead of special-casing the wrapper tag by name at every caller.
		// Same false-positive risk class as the LZMA fix above: confirmed on
		// this repo's own re-encoded ExciteBots "excite_gpmesh.msh" (Monster
		// Games collision mesh), whose raw header happens to carry the
		// unrelated 32-bit value 0x00000010 at this offset, satisfying the
		// CX00-prefix pattern and sending a plain model file through LZ10
		// decompression instead of the MSH decoder. A genuine LZ10/LZ11
		// stream always declares a nonzero decompressed size; reject the
		// zero-size case as the cheap disambiguator.
		if (d[0] != 0x10 && d[0] != 0x11 && size >= 8 && (d[4] == 0x10 || d[4] == 0x11)
			&& ((u32)d[5] | (u32)d[6] << 8 | (u32)d[7] << 16))
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
		// LZOvl (NDS reverse-overlay LZSS) has no magic of its own and could
		// only ever be recognised from a plausible trailer, which misfired on
		// unrelated files constantly. It is never autodetected -- callers that
		// genuinely have an LZOvl stream must ask for NFMT_LZOVL explicitly.
	}
	if (size >= 0x28 && !memcmp (d + size - 0x28, "FLIM", 4))
		return make_info (NFMT_BFLIM, true, false, 0);
	if (size >= 0x28 && !memcmp (d + size - 0x28, "CLIM", 4))
		return make_info (NFMT_BCLIM, true, false, 0);
	if (filename)
	{
		ccp ext = strrchr (filename, '.');
		if (ext)
		{
			if (!strcasecmp (ext, ".gfbmdl"))
				return make_info (NFMT_GFBMDL, false, false, 0);
			if (!strcasecmp (ext, ".gfbanm"))
				return make_info (NFMT_GFBANM, false, false, 0);
			if (!strcasecmp (ext, ".tex"))
				return make_info (NFMT_TEX3DS, false, false, 0);
		}
	}
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
