static enumError extract_bfres_switch_manifest (ccp arg)
{
	if (!is_ext (arg, ".bfres") && !is_ext (arg, ".fres"))
		return ERR_NOTHING_TO_DO;

	u8 *d = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &d, &size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (size < 0xF0 || memcmp (d, "FRES", 4) || le16 (d + 0x0C) != 0xFEFF)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	const uint version = le32 (d + 0x08);
	const uint vmajor = (version >> 16) & 0xFFFF;

	const s64 fmdl_arr = (s64)le64 (d + 0x28);
	// numModel sits right before the model dict-values are read; its
	// absolute offset depends on the same version-gated 32-byte reserved
	// block as everything else (see ResFileParser.Load()).
	const uint n_fmdl_off = vmajor >= 9 ? 0xDC : 0xBC;
	uint n_fmdl = (size_t)n_fmdl_off + 2 <= size ? le16 (d + n_fmdl_off) : 0;
	if (!n_fmdl && fmdl_arr > 0 && (size_t)fmdl_arr + 4 <= size
		&& !memcmp (d + fmdl_arr, "FMDL", 4))
		n_fmdl = 1; // model-count field missing/zero but a real FMDL is there -- be permissive
	if (!n_fmdl || fmdl_arr <= 0 || (size_t)fmdl_arr + 0x78 > size
		|| memcmp (d + fmdl_arr, "FMDL", 4))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".xml", false);
	else
		snprintf (dest, sizeof (dest), "%s.xml", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BFRES(Switch) structure:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
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

	const s64 fmdl = fmdl_arr;
	const uint fhdr = bfres_switch_hdr_extra (vmajor); // FMDL prologue size
	s64 p = fmdl + 4 + fhdr;
	const s64 name_field = p;
	p += 8;
	/* path_field */ p += 8;
	/* skel_field */ p += 8;
	/* vtxarr_field */ p += 8;
	const s64 shapes_val_field = p;
	p += 8;
	const s64 shapes_dict_field = p;
	p += 8;
	s64 mat_val_field, mat_dict_field;
	if (vmajor == 9)
	{
		mat_val_field = p;
		p += 8;
		p += 8; // unused padding/alternate-dict field (see ModelParser.Read())
		mat_dict_field = p;
		p += 8;
	}
	else
	{
		mat_val_field = p;
		p += 8;
		mat_dict_field = p;
		p += 8;
		if (vmajor >= 10)
			p += 8; // shader-assign offset (v10+ only)
	}
	/* userdata_val */ p += 8;
	/* userdata_dict */ p += 8;
	/* userptr */ p += 8;
	/* numVertexBuffer */ p += 2;
	const s64 numShape_off = p;
	p += 2;
	const s64 numMat_off = p;
	p += 2;

	(void)shapes_dict_field, (void)mat_dict_field;
	const s64 shapes_val
		= (size_t)shapes_val_field + 8 <= size ? (s64)le64 (d + shapes_val_field) : -1;
	const s64 mat_val = (size_t)mat_val_field + 8 <= size ? (s64)le64 (d + mat_val_field) : -1;
	const char *mname = rel_bfres_switch_string (d, size, (s64)le64 (d + name_field));

	fprintf (F.f,
		"<?xml version=\"1.0\"?>\n"
		"<!-- Switch BFRES (version-major %u): FMDL/FSHP/FMAT name resolution "
		"is decoded and verified against real retail samples -- see the "
		"comment above extract_bfres_switch_manifest() in wszst.c. Vertex/"
		"index geometry data is NOT decoded (location not verified yet); "
		"this only exports the structure (names/materials/vertex-attribute "
		"layout) as XML. -->\n"
		"<bfres-switch name=\"%s\" version-major=\"%u\" fmdl-count=\"%u\">\n",
		vmajor, mname ? mname : "", vmajor, n_fmdl);

	const uint n_fshp = (size_t)numShape_off + 2 <= size ? le16 (d + numShape_off) : 0;
	const uint n_fmat = (size_t)numMat_off + 2 <= size ? le16 (d + numMat_off) : 0;

	fprintf (F.f, "  <shapes count=\"%u\">\n", n_fshp);
	s64 sh = shapes_val;
	for (uint i = 0; i < n_fshp && sh >= 0 && (size_t)sh + 0x20 <= size; i++)
	{
		if (memcmp (d + sh, "FSHP", 4))
			break;
		const uint shdr = bfres_switch_hdr_extra (vmajor);
		const s64 sname_off = sh + 4 + shdr;
		const s64 fvtx_ptr = sname_off + 8;
		const char *sname = rel_bfres_switch_string (d, size, (s64)le64 (d + sname_off));
		const s64 fvtx = (s64)le64 (d + fvtx_ptr);
		fprintf (F.f, "    <shape name=\"%s\">\n", sname ? sname : "");
		if (fvtx > 0 && (size_t)fvtx + 0x60 <= size && !memcmp (d + fvtx, "FVTX", 4))
		{
			const uint vhdr = bfres_switch_hdr_extra (vmajor);
			const s64 attr_arr = (s64)le64 (d + fvtx + 4 + vhdr);
			// numVertexAttrib/numBuffer/local-BufferOffset sit right after
			// the two dict-pair fields (StrideOffset/SizeOffset), padding
			// s64, at fvtx+4+vhdr+0x40 (see VertexBufferParser.Load()).
			const s64 counts_off = fvtx + 4 + vhdr + 0x40;
			const s32 vb_local_off
				= (size_t)counts_off + 4 <= size ? (s32)le32 (d + counts_off) : 0;
			const uint n_attr = (size_t)counts_off + 8 <= size ? d[counts_off + 4] : 0;
			for (uint a = 0; a < n_attr; a++)
			{
				const s64 ae = attr_arr + (s64)a * 16;
				if (ae < 0 || (size_t)ae + 16 > size)
					break;
				const char *aname = rel_bfres_switch_string (d, size, (s64)le64 (d + ae));
				fprintf (F.f,
					"      <attribute name=\"%s\" format=\"0x%x\" "
					"buffer-offset=\"%u\" buffer-index=\"%u\"/>\n",
					aname ? aname : "", le32 (d + ae + 8), le16 (d + ae + 12), d[ae + 14]);
			}
			(void)vb_local_off; // local offset into the (unresolved) buffer pool
		}
		fprintf (F.f, "    </shape>\n");

		// FSHP entries aren't fixed-stride (each carries a variable-size
		// bounding/skin-index tail), so the next one is found the same way
		// Python's brute-force reference decode did: scan forward for the
		// next literal "FSHP" magic.
		const u8 *next = i + 1 < n_fshp ? memmem (d + sh + 4, size - (sh + 4), "FSHP", 4) : NULL;
		sh = next ? next - d : -1;
	}
	fprintf (F.f, "  </shapes>\n");

	fprintf (F.f, "  <materials count=\"%u\">\n", n_fmat);
	if (mat_val > 0 && (size_t)mat_val + 0x20 <= size && !memcmp (d + mat_val, "FMAT", 4))
	{
		const uint mhdr = bfres_switch_hdr_extra (vmajor);
		const char *matname = rel_bfres_switch_string (d, size, (s64)le64 (d + mat_val + 4 + mhdr));
		fprintf (F.f, "    <material name=\"%s\"/>\n", matname ? matname : "");
	}
	fprintf (F.f, "  </materials>\n");
	// Archive-level resource census (Legacy ResFile lists): FMDL plus the
	// animation classes FSKA/FMAA/FBVS/FSHA/FSCN and attached external
	// files (EXTF). Mirrors the WiiU RESOURCES line in
	// extract_bfres_textures() above, as XML.
	{
		bfres_archive_t arch;
		if (ParseBFRESArchive (d, size, &arch) && arch.n_slots)
		{
			fprintf (F.f, "  <resources archive=\"%s\">\n", arch.name);
			for (uint8_t i = 0; i < arch.n_slots; i++)
			{
				bool printable = true;
				for (uint8_t j = 0; j < 4; j++)
					if (arch.slots[i].magic[j] < 32 || arch.slots[i].magic[j] > 126)
						printable = false;
				if (printable)
					fprintf (F.f, "    <list class=\"%.*s\" count=\"%u\"/>\n", 4,
						arch.slots[i].magic, (unsigned)arch.slots[i].count_dict);
			}
			fprintf (F.f, "  </resources>\n");
		}
	}
	fprintf (F.f, "</bfres-switch>\n");

	if (ferror (F.f) && !err)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing XML failed: %s\n", dest);
	ResetFile (&F, opt_preserve);
	FREE (d);
	return err ? err : ERR_OK;
}
