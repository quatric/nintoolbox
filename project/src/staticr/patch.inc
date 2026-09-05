///////////////////////////////////////////////////////////////////////////////

#if HAVE_XSRC
#include "xsrc/x-staticr.c"
#endif

///////////////////////////////////////////////////////////////////////////////

static exmem_t ConvertGctToBin (
	// Returns an exmem_t struct.
	// data.len is 0 on error
	// Use FreeExMem(&result) to free possible alloced result.

	cvp src, // NULL or source
	int src_len, // size of 'src'. If -1: Use strlen(src)
	bool try_circ // use circ-buffer, if result is small enough
)
{
	if (!src)
		return NullExMem;

	if (src_len < 0)
		src_len = strlen (src);
	if (!src_len)
		return NullExMem;

	ccp ptr = src, end_src = ptr + src_len;

	FastBuf_t code;
	InitializeFastBuf (&code, sizeof (code));
	AppendFastBuf (&code, "\0\xd0\xc0\xde\0\xd0\xc0\xde", 8);

	while (ptr < end_src)
	{
		while (ptr < end_src && (u8)*ptr <= ' ')
			ptr++;

		char *end;
		const u32 n1 = strtoul (ptr, &end, 16);
		if (end <= end_src && end == ptr + 8)
		{
			ptr = end;
			if (ptr < end_src && *ptr == ' ')
			{
				ptr++;
				const u32 n2 = strtoul (ptr, &end, 16);
				if (end <= end_src && end == ptr + 8)
				{
					ptr = end;
					char *dest = GetSpaceFastBuf (&code, 8);
					write_be32 (dest, n1);
					write_be32 (dest + 4, n2);
				}
			}
		}
		while (ptr < end_src && *ptr != '\n')
			ptr++;
	}

	exmem_t res = { 0 };
	const int len = GetFastBufLen (&code);
	if (len > 8)
	{
		if (try_circ && len <= CIRC_BUF_MAX_ALLOC)
		{
			res.data.ptr = CopyCircBuf0 (code.buf, len);
			res.is_circ_buf = true;
		}
		else
		{
			res.data.ptr = MEMDUP (code.buf, len);
			res.is_alloced = true;
		}
		res.data.len = len;
	}
	else
		res = NullExMem;

	ResetFastBuf (&code);
	return res;
}

///////////////////////////////////////////////////////////////////////////////

static uint AddSectionGCH (staticr_t *str, // valid pointer
	u8 *gh_data, // GCH data
	uint gh_size, // GHC size
	ccp fname, // filename for messages
	bool get_min_addr // true: do nothing and get minimal section addr
)
{
	DASSERT (str);
	DASSERT (str->data_size);
	DASSERT (str->data);
	DASSERT (str->is_dol);
	DASSERT (gh_data || !gh_size);

	//--- get param

	const u32 patch_addr = GetVbiAddress (str);
	if (!patch_addr)
		return 0;

	//--- check for GCT-TXT

	// [[analyse-magic]]
	const file_format_t fform = GetByMagicFF (gh_data, gh_size, gh_size);
	if (fform == FF_GCT_TXT)
	{
		exmem_t exm = ConvertGctToBin (gh_data, gh_size, true);
		if (exm.data.len)
		{
			HexDump16 (stdlog, 0, 0, exm.data.ptr, exm.data.len < 64 ? exm.data.len : 64);
			AddCheatCode ((u8 *)exm.data.ptr, exm.data.len, false, fname);
		}
		FreeExMem (&exm);
		return 0;
	}

	//--- check for GCT

	if (fform == FF_GCT)
	{
		AddCheatCode (gh_data, gh_size, false, fname);
		return 0;
	}

	const gch_header_t *gh = (gch_header_t *)gh_data;
	if (!IsValidGCH (gh, gh_size))
	{
#if HAVE_XSRC
		return IsNotGCH (str, fname, gh_data, gh_size, get_min_addr);
#else
		ERROR0 (ERR_WARNING, "Not a valid GCH file: %s\n", fname);
		return 0;
#endif
	}

	gh_size -= sizeof (gch_header_t); // from now we only need the raw data size

	if (get_min_addr)
		return ntohl (gh->addr);

	wch_segment_t seg;
	memset (&seg, 0, sizeof (seg));
	seg.type = 't';
	seg.addr = ntohl (gh->addr);
	seg.size = gh_size;
	seg.vbi_entry = ntohl (gh->vbi_entry);

	const enumError err = AddSectionHelper (str, &seg, gh->data, fname);
	return err < ERR_ERROR;
}

///////////////////////////////////////////////////////////////////////////////

static uint AddSectionDOL (staticr_t *str, // valid pointer
	ccp fname, // filename to open
	bool get_min_addr // true: do nothing and get minimal section addr
)
{
	DASSERT (str);
	DASSERT (str->data_size);
	DASSERT (str->data);
	DASSERT (str->is_dol);

	if (!fname || !*fname)
		return 0;

	//--- get param

	const u32 patch_addr = GetVbiAddress (str);
	if (!patch_addr)
		return 0;

	//--- load file

	u8 *gh_data = 0;
	uint gh_size = 0;
	bool gh_alloced;
	enumError err = ReadCodeFile (str, fname, &gh_data, &gh_size, &gh_alloced);
	if (err)
		return err;

	const uint stat = AddSectionGCH (str, gh_data, gh_size, fname, get_min_addr);
	if (gh_alloced)
		FREE (gh_data);
	return stat;
}

///////////////////////////////////////////////////////////////////////////////

static uint AddSectionsDOL (staticr_t *str, // valid pointer
	bool get_min_addr // true: do nothing and get minimal section addr
)
{
	DASSERT (str);
	DASSERT (str->data_size);
	DASSERT (str->data);
	DASSERT (str->is_dol);

	uint i, count = get_min_addr ? 0xffffffff : 0;
	for (i = 0; i < opt_sect_list.used; i++)
	{
		ccp fname = opt_sect_list.field[i];
		const uint stat = AddSectionDOL (str, fname, get_min_addr);
		if (!get_min_addr)
			count += stat;
		else if (stat && count > stat)
			count = stat;
	}

	if (gecko_data.used)
	{
		if (logging >= 1)
			fprintf (
				stdlog, ">> GCT DATA/GROW, %u = 0x%x bytes\n", gecko_data.used, gecko_data.used);

		if (!get_min_addr)
		{
			// terminate section and add it to dol
			u32 gct_end = GCT_MM_BASE + gecko_data.used;
			MemMapItem_t *mi = InsertMemMap (&gct_data_mem, gct_end, 8);
			StringCopyS (mi->info, sizeof (mi->info), "GCT terminator");
			gct_end += 8;

			// insert max usage
			if (gct_end < GCT_MAX)
			{
				mi = InsertMemMap (&gct_data_mem, gct_end, GCT_MAX - gct_end);
				StringCopyS (mi->info, sizeof (mi->info), "Unused available memory");
			}
			else if (gct_end > GCT_MAX)
			{
				mi = InsertMemMap (&gct_data_mem, GCT_MAX, gct_end - GCT_MAX);
				StringCopyS (mi->info, sizeof (mi->info), "FAIL: Use of reserved memory");
			}
			else
			{
				mi = InsertMemMap (&gct_data_mem, GCT_MAX, 0);
				StringCopyS (mi->info, sizeof (mi->info), "Start of unusable memory");
			}

			u64 term = hton64 (GCT_TERM_NUM);
			InsertGrowBuffer (&gecko_data, &term, sizeof (term));
			count += AddSectionGCH (str, gecko_data.ptr, gecko_data.used, "GCT", false);

			if (verbose >= 2 || logging >= 2)
			{
				if (str->gct_moved)
					fputs ("\n   Memory map of code handler and GCT files before moving data to "
						   "heap:\n",
						stdlog);
				else
					fputs ("\n   Memory map of code handler and GCT files:\n", stdlog);
				PrintMemMap (&gct_data_mem, stdlog, 5, "part or source file");
				fputc ('\n', stdlog);
			}
		}
		else if (count > 0x80001800)
			count = 0x80001800;
		ResetGCT ();
	}

	return count;
}

///////////////////////////////////////////////////////////////////////////////

static uint CreateSectionDOL (staticr_t *str // valid pointer
)
{
	DASSERT (str);
	DASSERT (str->data_size);
	DASSERT (str->data);
	DASSERT (str->is_dol);

	enum
	{
		OFF_SECT_ADDR = 0x10,
		OFF_REL_ENTRY = 0x14,
		OFF_ORIG_ENTRY = 0x18,
		OFF_RDOL_ADDR = 0x1c,
		OFF_END_PARAM = 0x20,

		RED_DOL_SIZE = 0xe4 - 0x48, // reduced DOL size
	};

	u32 dol_sect_offset = 0; // file offset of DOL section
	u32 dol_sect_addr = 0; // address of DOL section
	dol_header_t *dh = (dol_header_t *)str->data;

	//--- find max dol address

	u32 max_addr = dh->bss_addr + dh->bss_size;
	uint sect;
	for (sect = 0; sect < DOL_N_SECTIONS; sect++)
	{
		const u32 size = ntohl (dh->sect_size[sect]);
		if (size)
		{
			const u32 off = ntohl (dh->sect_addr[sect]) + size;
			if (max_addr < off)
				max_addr = off;
		}
	}
	max_addr = ALIGN32 (max_addr, 4);

	//--- iterate new section list

	uint count;
	for (count = sect = 0; sect < n_section_list; sect++)
	{
		dol_sect_select_t *sl = section_list + sect;
		sl->offset = 0;
		if (sl->find_mode < 0)
			continue;

		dol_sect_info_t si;
		if (!FindDolSectionBySelector (&si, dh, sl))
		{
			ERROR0 (ERR_WARNING, "DOL section '%s' not available: %s\n", sl->name, str->fname);
			return 0;
		}

		bool is_dol_info = !strcasecmp (sl->fname, ".dol");
		if (is_dol_info)
		{
			sl->use_param = 0;
			sl->size = RED_DOL_SIZE;
		}
		else if (!sl->data)
		{
			if (OpenReadFILE (sl->fname, 0, true, &sl->data, &sl->size, 0, 0))
			{
				ERROR0 (ERR_WARNING, "Can't load file for '%s': %s\n", sl->name, sl->fname);
				sl->find_mode = -1;
				continue;
			}
		}
		count++;

		uint offset = str->data_size;
		sl->offset = offset;
		str->data_size += sl->size;
		str->data = REALLOC (str->data, str->data_size);
		dh = (dol_header_t *)str->data;
		u8 *dest = str->data + offset;

		u32 sect_addr = !sl->addr && sl->use_param ? be32 (sl->data + OFF_SECT_ADDR) : sl->addr;

		if (is_dol_info)
		{
			sl->data = (u8 *)&dh->sect_addr;
			if (!sect_addr)
				sect_addr = GCT_END_CHECK - RED_DOL_SIZE;
			dol_sect_addr = sect_addr;
			dol_sect_offset = offset;
		}

		if (!sect_addr)
			sect_addr = max_addr;
		const u32 end_addr = ALIGN32 (sect_addr + sl->size, 4);
		if (max_addr < end_addr)
			max_addr = end_addr;

		u32 new_entry = 0;
		bool param_valid = false;
		if (sl->use_param && sl->size >= OFF_END_PARAM)
		{
			new_entry = be32 (sl->data + OFF_REL_ENTRY);
			if (!new_entry)
			{
				param_valid = true;
			}
			else if (!(new_entry & 3) && new_entry < sl->size)
			{
				param_valid = true;
				new_entry += sect_addr;
			}
			else
				new_entry = 0;
		}
		if (!param_valid)
			sl->use_param = false;

		//--- create section

		PRINT (" - create section %s[%d] [%08x..%08x], f-off 0x%06x, size 0x%x, par=0x%x\n",
			si.name, si.section, sect_addr, sect_addr + sl->size, offset, sl->size, new_entry);

		if (verbose > 0)
		{
			fprintf (stdlog, "- Create section %s [%08x..%08x], file offset 0x%06x, size 0x%x",
				si.name, sect_addr, sect_addr + sl->size, offset, sl->size);
			if (new_entry)
				fprintf (stdlog, ", entry 0x%x\n", new_entry);
			else if (param_valid)
				fprintf (stdlog, ", use param\n");
			else
				fputc ('\n', stdlog);
		}

		dh->sect_off[si.section] = htonl (offset);
		dh->sect_addr[si.section] = htonl (sect_addr);
		dh->sect_size[si.section] = htonl (sl->size);
		memcpy (dest, sl->data, sl->size);

		//--- store original entry point

		memcpy (dest + OFF_ORIG_ENTRY, &dh->entry_addr, sizeof (u32));

		//--- use relative entry point of new section

		if (new_entry)
			dh->entry_addr = htonl (new_entry);
	}

	//--- final '.dol' section support

	if (dol_sect_offset)
	{
		//- refresh dol section

		PRINT ("UPDATE RED-DOL\n");
		dol_header_t *dh = (dol_header_t *)str->data;
		memcpy (str->data + dol_sect_offset, &dh->sect_addr, RED_DOL_SIZE);

		//- update sections with 'use_param'

		for (sect = 0; sect < n_section_list; sect++)
		{
			dol_sect_select_t *sl = section_list + sect;
			PRINT ("UPDATE sect %s [off=%x,param=%d] := %x\n", sl->name, sl->offset + OFF_RDOL_ADDR,
				sl->use_param, dol_sect_addr);
			if (sl->offset && sl->use_param)
				write_be32 (str->data + sl->offset + OFF_RDOL_ADDR, dol_sect_addr);
		}
	}

	return count;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static uint PatchCTCODEDOL (staticr_t *str // valid pointer
)
{
	DASSERT (str);
	DASSERT (str->data_size);
	DASSERT (str->data);
	DASSERT (str->is_dol);

	if (!opt_add_ctcode)
		return 0;

	if (!(str->dol_status & DOL_S_ORIG))
	{
		ERROR0 (ERR_WARNING, "Can't add CT-CODE to modified DOL: %s\n", str->fname);
		return 0;
	}

	ct_dol_ext_t *ce = GetCtExtDOL (str->mode);
	if (!ce)
	{
		ERROR0 (ERR_WARNING, "Can't add CT-CODE: %s\n", str->fname);
		return 0;
	}

	BZ2Source_t *bs = &ce->t2_src;
	SearchBZ2S (bs, ct_dir_list.field, ct_dir_list.used, SEA_LOG_OFF);
	if (!bs->data || !bs->size)
	{
	missing_ctcode:;
		ccp sem = strchr (bs->search, ';');
		ERROR0 (ERR_WARNING, "Missing %.*s => Can't add CT-CODE: %s\n\n",
			(int)(sem ? sem - bs->search : strlen (bs->search)), bs->search, str->fname);
		return 0;
	}

	bs = &ce->d8_src;
	SearchBZ2S (&ce->d8_src, ct_dir_list.field, ct_dir_list.used, SEA_LOG_OFF);
	if (!bs->data || !bs->size)
		goto missing_ctcode;

	const BZ2Source_t *t2 = &ce->t2_src;
	const BZ2Source_t *d8 = &ce->d8_src;

	//--- check data address

	bool move_d8 = opt_move_d8 || opt_sect_list.used;
	u32 d8_addr = move_d8 ? 0x802a8000 : ce->d8_addr;

	dol_header_t *dh = (dol_header_t *)str->data;
	if (CountOverlappedDolSections (dh, d8_addr, d8->size))
	{
		move_d8 = true;
		d8_addr = 0x802a8000;
		if (CountOverlappedDolSections (dh, d8_addr, d8->size))
		{
			ERROR0 (ERR_WARNING, "No space for CTCODE/D8 section: %s\n", str->fname);
			return 0;
		}
	}

	if (CountOverlappedDolSections (dh, ce->t2_addr, t2->size))
	{
		ERROR0 (ERR_WARNING, "No space for CTCODE/T2 section: %s\n", str->fname);
		return 0;
	}

	dol_sect_info_t t2info;
	if (!FindFirstFreeDolSection (&t2info, dh, 0))
	{
	err2sect:
		ERROR0 (ERR_WARNING, "Need 2 unused sections for CTCODE: %s\n", str->fname);
		return 0;
	}
	const int T_SECT = t2info.section;
	dh->sect_size[T_SECT] = htonl (t2->size); // mark used for next search

	dol_sect_info_t d8info;
	if (!FindFirstFreeDolSection (&d8info, dh, 3))
	{
		dh->sect_size[T_SECT] = htonl (0);
		goto err2sect;
	}
	const int D_SECT = d8info.section;

	//--- logging

	uint offset = str->data_size;
	if (verbose > 0)
	{
		fprintf (stdlog, "- Create section %s [%08x..%08x], file offset 0x%06x, size 0x%x\n",
			t2info.name, ce->t2_addr, ce->t2_addr + t2->size, offset, t2->size);
		fprintf (stdlog, "- Create section %s [%08x..%08x], file offset 0x%06x, size 0x%x\n",
			d8info.name, ce->d8_addr, ce->d8_addr + d8->size, offset + t2->size, d8->size);
	}

	//--- assign data and add section

	str->data_size += t2->size + d8->size;
	str->data = REALLOC (str->data, str->data_size);
	dh = (dol_header_t *)str->data;

	PRINT ("%s: off=%6x, addr=%8x, size=%6x\n", t2info.name, offset, ce->t2_addr, t2->size);
	memcpy (str->data + offset, t2->data, t2->size);
	write_be32 (str->data + offset + 0x14, d8_addr);
	dh->sect_off[T_SECT] = htonl (offset);
	dh->sect_addr[T_SECT] = htonl (ce->t2_addr);
	dh->sect_size[T_SECT] = htonl (t2->size);

	offset += t2->size;
	PRINT ("%s: off=%6x, addr=%8x, size=%6x\n", d8info.name, offset, ce->d8_addr, d8->size);
	memcpy (str->data + offset, d8->data, d8->size);
	dh->sect_off[D_SECT] = htonl (offset);
	dh->sect_addr[D_SECT] = htonl (d8_addr);
	dh->sect_size[D_SECT] = htonl (d8->size);

	memcpy (&dh->entry_addr, t2->data + 0x0c, 4);

	ClearBZ2S (&ce->t2_src, 1);
	ClearBZ2S (&ce->d8_src, 1);
	str->dol_flags[DPF_CTCODE] = 'C';
	str->dol_info_flags |= DIF_CTCODE;
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

uint PatchDOL (
	// returns: number of patches

	staticr_t *str // valid pointer
)
{
	DASSERT (str);
	if (!str->data_size)
		return 0;
	DASSERT (str->data);
	DASSERT (str->is_dol);
	if (!str->is_dol)
		return 0;

	uint clean_count = 0;
	if (opt_clean_dol && CleanDol (str))
		clean_count++;

	if (!(str->dol_status & DOL_S_ANALYZED))
		AnalyzeDOL (str, false);

	InititializeGCT ();
	if (opt_wcode >= WCODE_ENABLED)
	{
		if (wifi_domain_is_wiimmfi)
		{
			BZ2Manager_t *wcm = GetWiimmfiCode (str->mode);
			if (wcm)
			{
				DecodeBZIP2Manager (wcm);
				if (opt_wcode == WCODE_GECKO)
				{
					str->dol_flags[DPF_WCODE] = 'w';
					AddCheatCode (wcm->data, wcm->size, true, "WCODE");
				}
				else
				{
					str->dol_flags[DPF_WCODE] = 'W';
					AddWCode (wcm->data, wcm->size);
				}
			}
		}

		uint i;
		for (i = 0; i < opt_wcode_list.used; i++)
			if (AddWCodeFile (str, opt_wcode_list.field[i]))
				opt_wcode_list.field[i] = 0;
	}

	uint patch_count = 0;
	patch_count += PatchHTTPS (str);
	patch_count += PatchCTCODEDOL (str);
	patch_count += AddSectionsDOL (str, false);
	patch_count += CreateSectionDOL (str);
	patch_count += PatchByListWPF (str);

	if (use_gct_loader)
	{
		enumError err = SetupCodeLoader (str, 0, 0, 0, 0, CHT_NONE, 0);
		if (err == ERR_OK || err == ERR_WARNING)
			patch_count++;
	}

	//-- sdkver by region

	if (opt_str_vs_region >= 0)
	{
		const u32 vers_addr = GetSdkverAddress (str->mode);
		if (vers_addr)
		{
			dol_header_t *dh = (dol_header_t *)str->data;
			const u32 offset = GetDolOffsetByAddr (dh, vers_addr, 8, 0);
			if (offset)
			{
				u8 old[8];
				memcpy (old, str->data + offset, sizeof (old));

				char buf[20];
				snprintf (buf, sizeof (buf), "%05u", opt_str_vs_region & 0xffff);
				memcpy (str->data + offset + 1, buf, 6);
				if (str->data[offset] == '0')
					str->data[offset]++;

				if (memcmp (old, str->data + offset, sizeof (old)))
					patch_count++;
			}
		}
	}

	//--- patch second host

	bool wiimmfi_patched = false;
	if (wifi_domain_is_wiimmfi || opt_wcode >= WCODE_ENABLED)
	{
		u32 *h2 = GetSecondHostPointer (str, 0);
		noPRINT ("h2=%p %x\n", h2, h2 ? ntohl (*h2) : 0);
		if (h2 && ntohl (*h2) != ASM_NOP)
		{
			write_be32 (h2, ASM_NOP);
			patch_count++;
			wiimmfi_patched = true;
		}
	}

	//-- patched by

	patched_by_t pb_mode = patched_by_mode;
	switch ((int)pb_mode)
	{
		case PBY_WIIMMFI:
			if (wiimmfi_patched)
				pb_mode = PBY_ALWAYS;
			break;

		case PBY_AUTO:
			if (patch_count)
				pb_mode = PBY_ALWAYS;
			break;
	}

	if (pb_mode == PBY_RESET)
	{
		char *dest = GetIdPointer (str);
		if (dest)
			memset (dest, '*', PATCHED_BY_SIZE);
	}
	else if (pb_mode == PBY_ALWAYS)
	{
		char *dest = GetIdPointer (str);
		if (dest)
		{
			char *end = dest + PATCHED_BY_SIZE;
			if (*str->dol_flags)
				dest += snprintf (dest, PATCHED_BY_SIZE, "%.*s [%s]", PATCHED_BY_SIZE - 3 - DPF__N,
					opt_patched_by, str->dol_flags);
			else
				dest = StringCopyE (dest, end, opt_patched_by);
			while (dest < end)
				*dest++ = ' ';
		}
	}

	return clean_count + patch_count;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			patch via WPF			///////////////
///////////////////////////////////////////////////////////////////////////////

uint PatchByListWPF (staticr_t *str // valid pointer
)
{
	DASSERT (str);
	DASSERT (str->data_size);
	DASSERT (str->data);

	uint i, count = 0;
	for (i = 0; i < opt_wpf_list.used; i++)
	{
		ccp fname = opt_wpf_list.field[i];
		if (fname && *fname)
		{
			const int stat = PatchByFileWPF (str, fname);
			if (stat < 0)
			{
				FreeString (opt_wpf_list.field[i]);
				opt_wpf_list.field[i] = 0;
			}
			else if (stat > 0)
				count++;
		}
	}

	return count;
}

///////////////////////////////////////////////////////////////////////////////

int PatchByFileWPF (staticr_t *str, // valid pointer
	ccp fname)
{
	DASSERT (str);
	DASSERT (fname);

	//--- load file

	u8 *data = 0;
	uint size = 0;
	if (OpenReadFILE (fname, 0, true, &data, &size, 0, 0))
	{
		ERROR0 (ERR_WARNING, "Can't load WPF file: %s\n", fname);
		return -1;
	}

	//--- analyse file

	// [[analyse-magic]]
	const file_format_t fform = GetByMagicFF (data, size, size);
	PRINT (">>> [%s,%u]: %s\n", GetNameFF (fform, 0), size, fname);
	bool done = false;
	int stat = -1;

	if (size > sizeof (wpf_head_t) && be32 (data + 4) <= size)
	{
		wpf_t *wpf = (wpf_t *)(data + sizeof (wpf_head_t));
		if (fform == FF_WPF)
		{
			done = true;
			stat = PatchByWPF (str, wpf, size - sizeof (wpf_head_t), fname);
		}

#if HAVE_XSRC
		if (fform == FF_XPF)
		{
			done = true;
			stat = PatchByXPF (str, wpf, size - sizeof (wpf_head_t), fname);
		}
#endif
	}

	if (!done)
		ERROR0 (ERR_WARNING, "Not a WPF file:: %s\n", fname);
	FREE (data);
	return stat;
}

///////////////////////////////////////////////////////////////////////////////

int PatchByWPF (staticr_t *str, // valid pointer
	const wpf_t *wpf, // wpf patch list
	uint size, // size of data beginning at 'wpf'
	ccp fname // filename for error messages
)
{
	DASSERT (str);
	DASSERT (fname);
	printf ("PatchByWPF(): %s\n", fname);

	HexDump16 (stdout, 0, 0, wpf, size < 32 ? size : 32);
	return -1;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    options			///////////////
///////////////////////////////////////////////////////////////////////////////

bool opt_clean_dol = false; // true: remove additional DOL sections
bool opt_add_ctcode = false; // true: ad CT-CODE to main.dol
int opt_all_ranks = 0; // -1:off, 0:don't patch, 1:on
bool opt_move_d8 = false; // true: move CT-CODE D8 section to another place
bool opt_full_gch = false; // true: use full- of only- 'codehandler'

bool opt_gct_scan_sep = true; // true: scan 'F0000001 0000xxxx' separators
bool opt_gct_sep = false; // true: search cheat code separators
bool opt_gct_asm_sep = false; // true: use beginning of ASM blocks as separator
uint opt_gct_list = false; // >0: print a summary / >1: be verbose
int opt_gct_move = OFFON_AUTO; // move GCT to heap
u32 opt_gct_addr = 0x802c0000; // address for opt_gct_move
u32 opt_gct_space = 0; // minimal code space for opt_gct_move
int opt_allow_user_gch = OFFON_AUTO; // allow user define Gecko Code Handler

StringField_t opt_sect_list = { 0 }; // files with section data of type GCT|GCH|WCH
StringField_t opt_wpf_list = { 0 }; // files with patches of type WPF|XPF
StringField_t opt_wcode_list = { 0 }; // GCT files for wcode jobs

int opt_str_vs_region = SREG_NONE; // versus region patch mode
bool opt_str_xvs_region = false; // true: patch also test offsets
bool opt_str_tvs_region = false; // true: enable test mode with incremented regions

int opt_str_bt_region = SREG_NONE; // battle region patch mode
bool opt_str_xbt_region = false; // true: patch also test offsets
bool opt_str_tbt_region = false; // true: enable test mode with incremented regions

bool opt_meno = false; // true: patch 'Menu*' to 'MenO*'

///////////////////////////////////////////////////////////////////////////////

int ScanOptRegionHelper (ccp arg, bool is_battle, ccp prefix)
{
	region_info_t ri;
	SetupRegionInfo (&ri, 0, is_battle);

	DASSERT (ri.opt_region);
	if (!arg || !*arg)
	{
		*ri.opt_region = SREG_NONE;
		return 0;
	}

	DASSERT (ri.opt_x);
	*ri.opt_x = *arg == 'x' || *arg == 'X';
	if (*ri.opt_x)
		arg++;

	DASSERT (ri.opt_t);
	*ri.opt_t = (*arg == 't' || *arg == 'T') && arg[1] >= '0' && arg[1] <= '9';
	if (*ri.opt_t)
		arg++;

	if (*arg >= '0' && *arg <= '9')
	{
		u32 num;
		const enumError err = ScanSizeOptU32 (&num, // u32 * num
			arg, // ccp source
			1, // default_factor1
			0, // int force_base
			"region", // ccp opt_name
			0, // u64 min
			0xffff, // u64 max
			1, // u32 multiple
			0, // u32 pow2
			false // bool print_err
		);

		if (err == ERR_OK)
		{
			*ri.opt_region = num;
			return 0;
		}
	}

	static const KeywordTab_t tab[] = { { SREG_JAPAN, "JAPAN", 0, 0 },
		{ SREG_AMERICA, "AMERICA", "USA", 0 }, { SREG_EUROPE, "EUROPE", 0, 0 },

		{ SREG_AUSTRALIA, "AUSTRALIA", "OCEANIA", 0 }, { SREG_KOREA, "KOREA", 0, 0 },
		{ SREG_TAIWAN, "TAIWAN", 0, 0 }, { SREG_CHINA, "CHINA", 0, 0 },

		{ SREG_NONE, "NONE", 0, 0 }, { SREG_RESTORE, "RESTORE", "NINTENDO", 0 },
		{ SREG_TEST, "TEST", 0, 9001 }, { 0, 0, 0, 0 } };

	const KeywordTab_t *cmd = ScanKeyword (0, arg, tab);
	if (cmd)
	{
		*ri.opt_region = cmd->id;
		if (*ri.opt_region == SREG_TEST)
		{
			*ri.opt_region = cmd->opt;
			*ri.opt_t = true;
		}
		return 0;
	}

	ERROR0 (ERR_SYNTAX, "Invalid `region' mode (option --%sregion): '%s'\n", prefix, arg);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptVersusRegion (ccp arg)
{
	return ScanOptRegionHelper (arg, false, "vs-");
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptBattleRegion (ccp arg)
{
	return ScanOptRegionHelper (arg, true, "bt-");
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptRegion (ccp arg)
{
	const int stat = ScanOptRegionHelper (arg, false, "");

	opt_str_bt_region = opt_str_vs_region;
	opt_str_xbt_region = opt_str_xvs_region;
	opt_str_tbt_region = opt_str_tvs_region;
	if (opt_str_tbt_region)
		opt_str_bt_region += N_TAB_VS_REGION_PATCH;

	return stat;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int ScanOptAllRanks (ccp arg)
{
	if (!arg || !*arg)
	{
		opt_all_ranks = 1;
		return 0;
	}

	static const KeywordTab_t tab[] = { { -1, "RESTORE", "NINTENDO", 0 }, { 0, "NONE", 0, 0 },
		{ 1, "ON", 0, 0 }, { 0, 0, 0, 0 } };

	const KeywordTab_t *cmd = ScanKeyword (0, arg, tab);
	if (cmd)
	{
		opt_all_ranks = cmd->id;
		return 0;
	}

	ERROR0 (ERR_SYNTAX, "Invalid `all-ranks' mode (option --all-ranks): '%s'\n", arg);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const KeywordTab_t HttpsOptions[]
	= { { HTTPS_NONE, "NONE", 0, 0 }, { HTTPS_RESTORE, "RESTORE", "NINTENDO", 0 },
		  { HTTPS_HTTP, "HTTP", 0, 0 }, { HTTPS_DOMAIN, "DOMAIN", 0, 0 },
		  { HTTPS_SAKE0, "SAKE0", "S0", 0 }, { HTTPS_SAKE1, "SAKE1", "S1", 0 }, { 0, 0, 0, 0 } };

https_mode_t opt_https;
bool opt_wc24 = false;
char mkw_domain[100] = "mariokartwii.gs.wiimmfi.de";
ccp wifi_domain = mkw_domain + 16;
bool wifi_domain_is_wiimmfi = false;
wcode_t opt_wcode = WCODE_AUTO;
ccp opt_port_db = 0;
ccp opt_order = 0;
bool opt_no_0x = false;
bool opt_upper = false;

///////////////////////////////////////////////////////////////////////////////

int ScanOptHttps (ccp arg)
{
	const KeywordTab_t *cmd = ScanKeyword (0, arg, HttpsOptions);
	if (cmd)
	{
		opt_https = cmd->id;
		return 0;
	}

	ERROR0 (ERR_SYNTAX, "Invalid mode for option --https: '%s'\n", arg);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

ccp GetHttpsName (https_mode_t mode, ccp return_if_failed)
{
	const KeywordTab_t *cmd;
	for (cmd = HttpsOptions; cmd->name1; cmd++)
		if (cmd->id == mode)
			return cmd->name1;
	return return_if_failed;
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptDomain (ccp https, ccp arg)
{
	if (arg)
		snprintf (mkw_domain, sizeof (mkw_domain), "mariokartwii.gs.%s", arg);

	int stat = https ? ScanOptHttps (https) : 0;
	wifi_domain_is_wiimmfi = opt_https >= HTTPS_DOMAIN
		&& (!strcmp (wifi_domain, "wiimmfi.de") || !strcmp (wifi_domain, "test.wiimmfi.de"));
	return stat;
}

///////////////////////////////////////////////////////////////////////////////

const KeywordTab_t wcode_keytab[]
	= { { WCODE_OFF, "OFF", "0", 0 }, { WCODE_AUTO, "AUTO", "DEFAULT", 0 },
		  { WCODE_GECKO, "GECKO", "CHEAT", 0 }, { WCODE_ON, "ON", "1", 0 }, { 0, 0, 0, 0 } };

int ScanOptWCode (ccp arg)
{
	if (!arg)
	{
		opt_wcode = WCODE_ON;
		return 0;
	}

	int status;
	const KeywordTab_t *key = ScanKeyword (&status, arg, wcode_keytab);
	if (key)
	{
		opt_wcode = key->id;
		return 0;
	}

	PrintKeywordError (wcode_keytab, arg, status, 0, "option --wcode");
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptPatchedBy (ccp arg)
{
	if (!*arg)
		arg = "";

	while (*arg > 0 && *arg <= ' ')
		arg++;

	if (!*arg)
		StringCopyS (opt_patched_by, sizeof (opt_patched_by), STD_PATCHED_BY);
	else if (*arg == '+')
		snprintf (opt_patched_by, sizeof (opt_patched_by), "%s [%s]", STD_PATCHED_BY, arg + 1);
	else
		StringCopyS (opt_patched_by, sizeof (opt_patched_by), arg);

	uchar *ptr;
	for (ptr = (uchar *)opt_patched_by; *ptr; ptr++)
		if (*ptr < ' ' || *ptr == 0xa0)
			*ptr = ' ';

	noPRINT ("< %s\n> %s\n", arg, opt_patched_by);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

enum VsBtMode patch_vs_mode = VSBT_OFF;
enum VsBtMode patch_bt_mode = VSBT_OFF;
char patch_vs_str[2] = { 0 };
char patch_bt_str[2] = { 0 };

int ScanOptVS (bool is_bt, bool allow_2, ccp arg)
{
	if (!arg)
		return 0;

	ccp std_str;
	char *str;
	enum VsBtMode *mode;
	if (is_bt)
	{
		std_str = "bt";
		str = patch_bt_str;
		mode = &patch_bt_mode;
	}
	else
	{
		std_str = "vs";
		str = patch_vs_str;
		mode = &patch_vs_mode;
	}

	switch (strlen (arg))
	{
		case 0:
			*mode = VSBT_OFF;
			return 0;

		case 1:
			*mode = VSBT_PATCH;
			str[0] = std_str[0];
			str[1] = arg[0];
			return 0;

		case 2:
			if (allow_2)
			{
				*mode = VSBT_PATCH;
				str[0] = arg[0];
				str[1] = arg[1];
				return 0;
			}
			break;

		case 4:
			if (!strcasecmp (arg, "test"))
			{
				*mode = VSBT_TEST;
				return 0;
			}
			break;
	}

	if (allow_2)
		ERROR0 (ERR_SYNTAX, "Only 1 or 2 characters allowed for option --%s2: %s\n", std_str, arg);
	else
		ERROR0 (ERR_SYNTAX, "Only 1 character allowed for option --%s: %s\n", std_str, arg);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptCannon (ccp arg)
{
	if (!arg || !*arg)
	{
		memset (cannon_param, 0, sizeof (cannon_param));
		return 0;
	}

	char *end;
	uint index = str2ul (arg, &end, 10);
	if (end == arg)
		return ERROR0 (ERR_SYNTAX, "Option --cannon: Invalid argument: %s", arg);
	if (index >= CANNON_N)
		return ERROR0 (ERR_SYNTAX, "Option --cannon: Invalid index: %d", index);

	cannon_param_t *cp = cannon_param + index;
	memset (cp, 0, sizeof (*cp));
	const u8 *orig = CannonDataLEX + 4 + index * CANNON_SIZE;
	uint i;
	for (i = 0; i < CANNON_N_PARAM; i++, orig += sizeof (float))
		cp->param[i] = bef4 (orig);

	for (i = 0; i < CANNON_N_PARAM; i++)
	{
		arg = end;
		while (*arg == ' ' || *arg == '\t' || *arg == ',')
			arg++;
		double d = strtod (arg, &end);
		if (end == arg)
			break;
		cp->param[i] = d;
	}
	if (i > 0)
		cp->valid = true;

#if HAVE_PRINT
	{
		uint i;
		cannon_param_t *cp = cannon_param;
		for (i = 0; i < CANNON_N; i++, cp++)
			PRINT (">CANNON[%u]: valid=%d, %10f %10f %10f %10f\n", i, cp->valid, cp->param[0],
				cp->param[1], cp->param[2], cp->param[3]);
	}
#endif

	return 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			Option --create-sect		///////////////
///////////////////////////////////////////////////////////////////////////////

dol_sect_select_t section_list[DOL_N_SECTIONS];
uint n_section_list = 0;

///////////////////////////////////////////////////////////////////////////////

int ScanOptGctMove (ccp arg)
{
	const int stat = ScanKeywordOffAutoOn (arg, OFFON_ON, OFFON_FORCE, "Option --gct-move");
	if (stat == OFFON_ERROR)
		return 1;

	opt_gct_move = stat;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptAllowUserGch (ccp arg)
{
	const int stat = ScanKeywordOffAutoOn (arg, OFFON_ON, OFFON_FORCE, "Option --allow-user-gch");
	if (stat == OFFON_ERROR)
		return 1;

	opt_allow_user_gch = stat;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptGctAddr (ccp arg)
{
	return ScanSizeOptU32 (&opt_gct_addr, // u32 * num
			   arg, // ccp source
			   1, // default_factor1
			   0, // int force_base
			   "gct-addr", // ccp opt_name
			   0x00000000, // u64 min
			   0x817fffff, // u64 max
			   4, // u32 multiple
			   0, // u32 pow2
			   true // bool print_err
			   )
		!= ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptGctSpace (ccp arg)
{
	return ScanSizeOptU32 (&opt_gct_space, // u32 * num
			   arg, // ccp source
			   1, // default_factor1
			   0, // int force_base
			   "gct-space", // ccp opt_name
			   0, // u64 min
			   0x1000000, // u64 max
			   4, // u32 multiple
			   0, // u32 pow2
			   true // bool print_err
			   )
		!= ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptCreateSect (ccp arg)
{
	if (!arg || !*arg)
		return 0;

	if (n_section_list >= DOL_N_SECTIONS)
	{
		ERROR0 (ERR_SEMANTIC, "Section list for --create-sect is full.\n");
		return 1;
	}

	//--- check for 'P'

	ccp arg0 = arg;
	bool use_param = *arg == 'P' || *arg == 'p';
	if (use_param)
	{
		arg++;
		while (*arg == ' ' || *arg == '\t' || *arg == ',')
			arg++;
	}

	//--- check for 'T' 'D' 'T#' 'D#'

	int find_mode = 0, idx = -1;
	if (*arg == 'T' || *arg == 't')
	{
		find_mode = 1;
		arg++;
		if (*arg >= '0' && *arg < '0' + DOL_N_TEXT_SECTIONS)
			idx = *arg++ - '0';
	}
	else if (*arg == 'D' || *arg == 'd')
	{
		find_mode = 2;
		arg++;
		if (*arg >= '0' && *arg < '0' + DOL_N_DATA_SECTIONS)
			idx = *arg++ - '0' + DOL_N_TEXT_SECTIONS;
	}

	if (find_mode)
	{
		ccp start = arg;
		while (*arg == ' ' || *arg == '\t' || *arg == ',')
			arg++;
		if (arg == start && *arg != '=')
		{
			arg = arg0;
			find_mode = 0;
			idx = -1;
		}
	}

	//--- check for an addr

	char *end;
	uint addr = str2ul (arg, &end, 16);
	if (end > arg)
	{
		arg = end;
		if (addr)
			addr |= 0x80000000;
	}
	else
		addr = 0;

	if (*arg != '=')
	{
		arg = arg0;
		use_param = false;
		find_mode = 0;
		idx = -1;
		addr = 0;
	}
	else
		arg++;

	dol_sect_select_t *sl = section_list + n_section_list++;
	memset (sl, 0, sizeof (*sl));
	sl->find_mode = find_mode;
	sl->sect_idx = idx;
	sl->use_param = use_param;
	sl->addr = addr;
	sl->fname = arg;

	switch (sl->find_mode)
	{
		case 1:
			sl->name[0] = 'T';
			sl->name[1] = sl->sect_idx < 0 ? '*' : '0' + sl->sect_idx;
			break;

		case 2:
			sl->name[0] = 'D';
			sl->name[1] = sl->sect_idx < 0 ? '*' : '0' + sl->sect_idx - DOL_N_TEXT_SECTIONS;
			break;

		default:
			sl->name[0] = '*';
			sl->name[1] = '*';
			break;
	}

	PRINT ("CREATE-SECTION[%s,%c]: %u,%2d %#10x : %s\n", sl->name, sl->use_param ? 'W' : '-',
		sl->find_mode, sl->sect_idx, sl->addr, sl->fname);

	return 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    DumpSTR()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError DumpSTR (FILE *f, // dump to this file
	int indent, // indention
	staticr_t *str, // valid pointer
	bool use_c // true: print in C language fragments
)
{
	DASSERT (f);
	DASSERT (str);

	if (str->is_dol)
		return DumpDOL (f, indent, str, use_c);

	DASSERT (str->data);
	DASSERT (str->data_size >= sizeof (rel_header_t));

	indent = NormalizeIndent (indent);
	const rel_info_t *rinfo = GetInfoREL (str->mode);
	const u32 load_off = rinfo ? rinfo->load_addr : 0;

	//----- dump header

	rel_header_t hd;
	be32n ((u32 *)&hd, (u32 *)str->data, sizeof (rel_header_t) / sizeof (u32));

	ccp mod_name = "-";
	if (hd.mod_name && hd.mod_name + hd.mod_name_len < str->data_size)
		mod_name = (ccp)str->data + hd.mod_name;

	fprintf (f, "%*sId:           %8u = 0x%x\n", indent, "", hd.id, hd.id);
	fprintf (f, "%*sN(sections):  %8u\n", indent, "", hd.n_section);
	fprintf (f, "%*sModule Name:  %8s\n", indent, "", mod_name);
	fprintf (f, "%*sReloc Offset: %8x\n", indent, "", hd.reloc_off);
	if (rinfo)
		fprintf (f, "%*sLoad Address: %8x\n", indent, "", rinfo->load_addr);

	fprintf (f, "\n%*sSections (0..%u) [size:offset:address]:\n", indent, "", hd.n_section - 1);
	rel_sect_info_t *si = (rel_sect_info_t *)(str->data + hd.section_off);
	uint s;
	for (s = 0; s < hd.n_section; s++, si++)
	{
		u32 size = be32 (&si->size);
		if (size)
		{
			u32 offset = be32 (&si->offset);
			if (!offset)
				fprintf (f, "%*s%4u. BSS  %#9x\n", indent, "", s, size);
			else
			{
				ccp type = offset & 1 ? "TEXT" : "DATA";
				offset &= ~1;
				fprintf (f, "%*s%4u. %s %#10x : %#10x .. %#10x", indent, "", s, type, size, offset,
					offset + size);
				if (rinfo)
					fprintf (f, " : %#10x .. %#10x\n", load_off + offset, load_off + offset + size);
				else
					fputc ('\n', f);
			}
		}
	}

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    DumpDOL()			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError DumpDOL_c (FILE *f, // dump to this file
	staticr_t *str // valid pointer
)
{
	DASSERT (f);
	DASSERT (str);
	DASSERT (str->data);
	DASSERT (str->data_size >= sizeof (dol_header_t));

	dol_header_t dol;
	if (str->data_size < sizeof (dol))
		return ERR_WARNING;
	ntoh_dol_header (&dol, (dol_header_t *)str->data);

	fputs ("\nstatic const dol_sect_info_t dol_info_xxx[DOL_NN_SECTIONS] =\n{\n", f);
	char name[8];

	uint sect;
	for (sect = 0; sect < DOL_NN_SECTIONS; sect++)
	{
		dol_sect_info_t info;
		GetSectionDOL (str, &info, sect, true);
		snprintf (name, sizeof (name), "\"%s\",", info.name);
		fprintf (f, "  { %2u, %-7s%#8x, %#10x, %#8x, %u,0,%u, 0,", sect, name, info.off, info.addr,
			info.size, info.sect_valid, info.hash_valid);

		if (info.hash_valid)
		{
			u8 *ptr = info.hash;
			uint line;
			for (line = 0; line < 2; line++)
			{
				ccp sep = line ? ",\n\t\t  " : "\n\t\t{ ";
				uint idx;
				for (idx = 0; idx < sizeof (info.hash) / 2; idx++)
				{
					fprintf (
						f, "%s%s0x%02x", sep, idx == sizeof (info.hash) / 4 ? " " : "", *ptr++);
					sep = ",";
				}
			}
			fputs (" }},\n", f);
		}
		else
			fputs (" {0} },\n", f);
	}

	fputs ("};\n\n", f);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError DumpDOL (FILE *f, // dump to this file
	int indent, // indention
	staticr_t *str, // valid pointer
	bool use_c // true: print in C language fragments
)
{
	DASSERT (f);
	DASSERT (str);

	if (use_c)
		return DumpDOL_c (f, str);

	DASSERT (str->data);
	DASSERT (str->data_size >= sizeof (dol_header_t));

	DumpDolHeader (f, indent, (dol_header_t *)str->data, str->data_size,
		long_count > 1		 ? 7
			: long_count > 0 ? 3
							 : 1);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    ExtractSTR()		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExtractSTR (staticr_t *str, // valid pointer
	ccp dest_dir // valid destination directory
)
{
	DASSERT (str);
	DASSERT (str->data);
	DASSERT (dest_dir);

	if (str->is_dol)
		return ExtractDOL (str, dest_dir);

	//--- setup

	const rel_info_t *ri = GetInfoREL (str->mode);
	if (!ri)
		return ERROR0 (ERR_INTERNAL, 0);

	CreatePath (dest_dir, true);

	//--- open log file

	char path_buf[PATH_MAX];
	ccp path = PathCatPP (path_buf, sizeof (path_buf), dest_dir, "rel-setup.txt");
	FILE *log = fopen (path, "wb");
	if (!log)
		return ERROR0 (ERR_CANT_CREATE, "Can't create setup file: %s\n", path);

	static ccp sep = "#---------------------------------------------------------------------\n";
	fprintf (log, "\n%s#sect offset  address  size  sha1 checksum\n%s", sep, sep);

	//--- extract sections

	uint sect;
	for (sect = 0; sect < REL_SECT__N; sect++)
	{
		dol_sect_info_t info = ri->sect[sect];
		fprintf (log, "%-4s= %06x %08x %06x", info.name, info.off, info.addr, info.size);

		info.hash_valid = false;
		info.data_valid = str->data && info.off + info.size <= str->data_size;
		if (info.data_valid)
		{
			const u8 *data = str->data + info.off;
			info.hash_valid = true;
			SHA1 (data, info.size, info.hash);

			if (info.hash_valid)
			{
				fputc (' ', log);
				uint i;
				for (i = 0; i < sizeof (info.hash); i++)
					fprintf (log, "%02x", info.hash[i]);
			}

			path = PathCatPPE (path_buf, sizeof (path_buf), dest_dir, info.name, ".bin");
			FILE *f = fopen (path, "wb");
			if (!f)
			{
				fclose (log);
				return ERROR0 (ERR_CANT_CREATE, "Can't create data file: %s\n", path);
			}
			fwrite (data, 1, info.size, f);
			fclose (f);
		}
		fputc ('\n', log);
	}
	fputs (sep, log);

	//--- close log and return

	fprintf (log, "\n");
	fclose (log);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    ExtractDOL()		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExtractDOL (staticr_t *str, // valid pointer
	ccp dest_dir // valid destination directory
)
{
	DASSERT (str);
	DASSERT (str->data);
	DASSERT (str->is_dol);
	DASSERT (dest_dir);

	CreatePath (dest_dir, true);

	//--- open log file

	char path_buf[PATH_MAX];
	ccp path = PathCatPP (path_buf, sizeof (path_buf), dest_dir, "dol-setup.txt");
	FILE *log = fopen (path, "wb");
	if (!log)
		return ERROR0 (ERR_CANT_CREATE, "Can't create setup file: %s\n", path);

	static ccp sep = "#---------------------------------------------------------------------\n";
	fprintf (log, "\n%s#sect offset  address  size  sha1 checksum\n%s", sep, sep);

	//--- extract 'dol-header.bin'

	path = PathCatPP (path_buf, sizeof (path_buf), dest_dir, "dol-header.bin");
	FILE *f = fopen (path, "wb");
	if (!f)
	{
		fclose (log);
		return ERROR0 (ERR_CANT_CREATE, "Can't create data file: %s\n", path);
	}
	fwrite (str->data, 1, 0x100, f);
	fclose (f);

	//--- extract sections

	uint sect;
	for (sect = 0; sect < DOL_NN_SECTIONS; sect++)
	{
		if (sect == DOL_N_TEXT_SECTIONS || sect == DOL_N_SECTIONS)
			fputs (sep, log);
		dol_sect_info_t info;
		GetSectionDOL (str, &info, sect, true);
		if (!info.sect_valid)
		{
			// fprintf(log,"%-4s= -\n",info.name);
			continue;
		}

		fprintf (log, "%-4s= %06x %08x %06x", info.name, info.off, info.addr, info.size);
		if (info.data_valid)
		{
			if (info.hash_valid)
			{
				fputc (' ', log);
				uint i;
				for (i = 0; i < sizeof (info.hash); i++)
					fprintf (log, "%02x", info.hash[i]);
			}

			path = PathCatPPE (path_buf, sizeof (path_buf), dest_dir, info.name, ".bin");
			FILE *f = fopen (path, "wb");
			if (!f)
			{
				fclose (log);
				return ERROR0 (ERR_CANT_CREATE, "Can't create data file: %s\n", path);
			}
			fwrite (info.data, 1, info.size, f);
			fclose (f);
		}
		fputc ('\n', log);
	}
	fputs (sep, log);

	//--- close log and return

	fprintf (log, "\n");
	fclose (log);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ctcode_data_tab[]		///////////////
///////////////////////////////////////////////////////////////////////////////

#undef DEF_DATA
#undef DEF_BZIP
#undef DEF_COMPRESS

#define DEF_DATA(a) { #a, 0, a, sizeof (a) },
#define DEF_BZIP(a) { #a, 1, a##_bz2, sizeof (a##_bz2) },
#define DEF_COMPRESS(a) { #a, 2, (u8 *)(&a##_mgr), 0 },

const data_tab_t maindol_data_tab[] = { DEF_BZIP (ctcode_boot_code_pal)
		DEF_BZIP (ctcode_boot_code_usa) DEF_BZIP (ctcode_boot_code_jap)

			DEF_BZIP (ctcode_boot_data_pal) DEF_BZIP (ctcode_boot_data_usa) DEF_BZIP (
				ctcode_boot_data_jap)

				DEF_BZIP (codehandler) DEF_BZIP (codehandleronly)

					DEF_COMPRESS (old_lecode_loader_pal) DEF_COMPRESS (old_lecode_loader_usa)
						DEF_COMPRESS (old_lecode_loader_jap) DEF_COMPRESS (old_lecode_loader_kor)

							DEF_COMPRESS (lecode_loader_pal) DEF_COMPRESS (lecode_loader_usa)
								DEF_COMPRESS (lecode_loader_jap) DEF_COMPRESS (lecode_loader_kor)

									{ 0, 0, 0 } };

#undef DEF_DATA
#undef DEF_BZIP
// #undef DEF_PATCH

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    END				///////////////
///////////////////////////////////////////////////////////////////////////////
