// Export the structural half of a 3DS/Wii U bitmap font (BCFNT/BFFNT) as
// XML. Wii's own font family (RFNT/RFNA, container magic "RFNT"/"RFNA") is
// already fully decoded to PNG via the TGLP branch in AssignIMG() --
// lib-image-load.c. The 3DS/Wii U family reuses the exact same FINF/TGLP
// section shapes (confirmed field-for-field against hadashisora/NintyFont's
// from-source CFNT/FINF/TGLP reader for the container header and TGLP
// itself), but is NOT simply "BRFNT with a different container magic":
//   - 3DS uses magic "CFNT"; Wii U uses "FFNT" -- a real, different magic,
//     not a NintyFont naming quirk (NintyFont's "CFNT" reader is 3DS-only
//     and does not cover "FFNT" at all).
//   - The FINF pointer fields (ptrGlyph/ptrWidth/ptrMap) sit 4 bytes later
//     than NintyFont's declared struct says (FINF+0x14/+0x18/+0x1C, not
//     NintyFont's documented +0x10/+0x14/+0x18) -- caught by cross-checking
//     against two real retail Wii U .bffnt samples (DynaFont_NW_Demo.bffnt,
//     CafeStd_25.bffnt): NintyFont's offsets point at garbage, the +4
//     shifted ones land exactly on a real "TGLP"/"CWDH"/"CMAP" magic. This
//     fork's own decode uses the verified real offsets, not the reference
//     tool's.
//   - TGLP's sheetFormat is a 3DS/Cafe GPU texture-format id, a completely
//     different numbering from the Wii GX ids GetImageGeometry() (used by
//     the BRFNT decode path) already understands -- reusing that table
//     would silently decode the wrong pixel format. No 3DS/Cafe format
//     table exists in this fork yet, so pixel decode is left undone rather
//     than guessed; this only exports the verified structural fields
//     (cell/sheet geometry, sheet count/format id, pointers), the same
//     "don't ship an unverified guess" scope as the Switch BFRES manifest
//     above. No real .bcfnt sample was found to verify the 3DS side
//     specifically, but the container/TGLP shape is shared with .bffnt --
//     see PLAN.md.
static enumError extract_cfnt_manifest (ccp arg)
{
	if (!is_ext (arg, ".bffnt") && !is_ext (arg, ".bcfnt") && !is_ext (arg, ".ffnt")
		&& !is_ext (arg, ".cfnt"))
		return ERR_NOTHING_TO_DO;

	u8 *d = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &d, &size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	const bool is_cfnt = size >= 4 && !memcmp (d, "CFNT", 4);
	const bool is_ffnt = size >= 4 && !memcmp (d, "FFNT", 4);
	if ((!is_cfnt && !is_ffnt) || size < 0x14)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	const bool be = d[4] == 0xFE && d[5] == 0xFF; // BOM: FEFF=big, FFFE=little
	if (!be && !(d[4] == 0xFF && d[5] == 0xFE))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	} // neither BOM reading is valid

#define CF16(p) (be ? be16 (p) : le16 (p))
#define CF32(p) (be ? be32 (p) : le32 (p))

	const uint header_size = CF16 (d + 6);
	if (header_size < 0x14 || (size_t)header_size + 0x14 > size
		|| memcmp (d + header_size, "FINF", 4))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const size_t finf = header_size;
	const uint finf_len = CF32 (d + finf + 4);
	if ((finf_len != 0x1C && finf_len != 0x20) || finf + finf_len > size)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const uint font_type = d[finf + 8];

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s structure:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", is_cfnt ? "BCFNT" : "BFFNT", arg, dest);
	if (testmode)
	{
		FREE (d);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (!F.f)
	{
		FREE (d);
		return err;
	}
	fprintf (F.f,
		"<?xml version=\"1.0\"?>\n"
		"<!-- %s structure manifest. Pixel sheets are decoded separately to PNG "
		"by decode_cfnt_if_possible() via AssignIMG(NFMT_BCFNT). CTR format 0 "
		"(RGBA8 linear) is fully supported; formats 3/5/7/9/10 (RGB565/IA8/I8/"
		"IA4/I4) are decoded via the GX tile path (correct for linear data, "
		"potentially misordered for real retail CTR/Cafe hardware-tiled sheets). "
		"-->\n<%s font-type=\"%u\">\n",
		is_cfnt ? "BCFNT" : "BFFNT", is_cfnt ? "bcfnt" : "bffnt", font_type);

	if (font_type == 1 && finf_len >= 0x1C && (size_t)finf + 0x14 <= size)
	{
		uint ptr_glyph = CF32 (d + finf + 0x10);
		size_t tglp = (ptr_glyph >= 8) ? (size_t)ptr_glyph - 8 : (size_t)-1;
		// tglp is (size_t)-1 for ptr_glyph < 8; check it before adding to it
		if (tglp >= size || tglp + 0x20 > size || memcmp (d + tglp, "TGLP", 4))
		{
			ptr_glyph
				= (finf_len >= 0x20 && (size_t)finf + 0x18 <= size) ? CF32 (d + finf + 0x14) : 0;
			tglp = (ptr_glyph >= 8) ? (size_t)ptr_glyph - 8 : (size_t)-1;
		}
		if (ptr_glyph >= 8 && tglp < size && tglp + 0x20 <= size && !memcmp (d + tglp, "TGLP", 4))
		{
			fprintf (F.f,
				"  <tglp cell-width=\"%u\" cell-height=\"%u\" baseline=\"%u\" "
				"max-char-width=\"%u\" sheet-size=\"%u\" sheet-count=\"%u\" "
				"sheet-format=\"0x%x\" cells-per-row=\"%u\" cells-per-column=\"%u\" "
				"sheet-width=\"%u\" sheet-height=\"%u\" sheet-data-offset=\"0x%x\"/>\n",
				d[tglp + 8], d[tglp + 9], d[tglp + 10], d[tglp + 11], CF32 (d + tglp + 0xC),
				CF16 (d + tglp + 0x10), CF16 (d + tglp + 0x12), CF16 (d + tglp + 0x14),
				CF16 (d + tglp + 0x16), CF16 (d + tglp + 0x18), CF16 (d + tglp + 0x1A),
				CF32 (d + tglp + 0x1C));
		}
		else
			fprintf (F.f,
				"  <!-- fontType==1 (TGLP) but ptrGlyph-8 (0x%zx) "
				"isn't a valid TGLP -- truncated or unexpected file -->\n",
				tglp);
	}
	else
		fprintf (F.f,
			"  <!-- fontType==%u (bitmap/CGLP glyphs), not the TGLP "
			"case this fork decodes -->\n",
			font_type);

	fprintf (F.f, "</%s>\n", is_cfnt ? "bcfnt" : "bffnt");
#undef CF16
#undef CF32

	if (ferror (F.f) && !err)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (d);
	return err ? err : ERR_OK;
}
