int ScanOptPBMode (ccp arg)
{
	if (!arg || !*arg)
	{
		patched_by_mode = PBY_AUTO;
		return 0;
	}

	static const KeywordTab_t tab[] = { { PBY_OFF, "OFF", 0, 0 }, { PBY_RESET, "RESET", 0, 0 },
		{ PBY_WIIMMFI, "WIIMMFI", 0, 0 }, { PBY_AUTO, "AUTO", "DEFAULT", 0 },
		{ PBY_ALWAYS, "ALWAYS", 0, 0 }, { 0, 0, 0, 0 } };

	const KeywordTab_t *cmd = ScanKeyword (0, arg, tab);
	if (cmd)
	{
		patched_by_mode = cmd->id;
		return 0;
	}

	ERROR0 (ERR_SYNTAX, "Invalid mode for option --pb-mode: '%s'\n", arg);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

#if defined(TEST) || defined(DEBUG) //|| HAVE_WIIMM_EXT
static void TraceGCT (ccp info)
{
	fprintf (stdlog, "%s#GCT: gecko=%u/%u, wc-job=%u/%u, wc-data=%u/%u%s%s%s\n", collog->b_cyan,
		gecko_data.used, gecko_data.size, wcode_job.used, wcode_job.size, wcode_data.used,
		wcode_data.size, info ? " : " : "", info ? info : "", collog->reset);
}
#else
static inline void TraceGCT (ccp info)
{
}
#endif

///////////////////////////////////////////////////////////////////////////////

static void InititializeGCT ()
{
	static bool done = false;
	if (!done)
	{
		done = true;

		InitializeMemMap (&gct_data_mem);
		InitializeGrowBuffer (&gecko_data, MiB);
		InitializeGrowBuffer (&wcode_job, MiB);
		InitializeGrowBuffer (&wcode_data, MiB);
		gecko_data.grow_size = 0x1000;
		wcode_job.grow_size = 0x0100;
		wcode_data.grow_size = 0x0400;

		if (logging >= 3)
			TraceGCT ("Init");
	}
}

///////////////////////////////////////////////////////////////////////////////

static void ResetGCT ()
{
	InititializeGCT ();

	ResetMemMap (&gct_data_mem);
	ClearGrowBuffer (&gecko_data);
	ClearGrowBuffer (&wcode_job);
	ClearGrowBuffer (&wcode_data);
}

///////////////////////////////////////////////////////////////////////////////

static enumError AddSectionHelper2 (
	// return ERR_OK | ERR_WARNING of created with warnings | ERR_ERROR on abort
	staticr_t *str, // valid pointer
	const wch_segment_t *seg, // segement info, local endian, data+size ignored
	const u8 *seg_data, // segement data
	uint seg_size, // segement data
	ccp fname // filename for messages
)
{
	//--- patching possible?

	const u32 patch_addr = GetVbiAddress (str);
	if (!patch_addr)
		return 0;

	//--- check overlaps

	dol_header_t *dh = (dol_header_t *)str->data;
	const uint overlap = CountOverlappedDolSections (dh, seg->addr, seg_size);
	if (overlap)
	{
		if (!force_count)
		{
			ERROR0 (ERR_WARNING, "New section would overlap %u existing section%s -> ignore: %s\n",
				overlap, overlap == 1 ? "" : "s", fname);
			return ERR_ERROR;
		}

		ERROR0 (ERR_WARNING, "New section overlaps %u existing section%s: %s\n", overlap,
			overlap == 1 ? "" : "s", fname);
	}

	//--- find first unused section

	int dol_seg_type;
	switch (seg->type)
	{
		case 't':
			dol_seg_type = 0;
			break; // Any segment, TEXT first
		case 'T':
			dol_seg_type = 1;
			break; // Only TEXT segment
		case 'D':
			dol_seg_type = 2;
			break; // Only DATA segment
		default:
			dol_seg_type = 3;
			break; // Any segment, DATA first (alias 'd')
	}

	dol_sect_info_t di;
	if (!FindFirstFreeDolSection (&di, dh, dol_seg_type))
	{
		ERROR0 (ERR_WARNING, "DOL: No free section available: %s\n", str->fname);
		return ERR_ERROR;
	}
	PRINT ("NEW SECTION: %s\n", di.name);

	//--- append seg_data to end of dol

	uint offset = str->data_size;
	str->data_size += seg_size;
	str->data = REALLOC (str->data, str->data_size);
	dh = (dol_header_t *)str->data;
	memcpy (str->data + offset, seg_data, seg_size);

	//--- create section

	PRINT (" - create section %s [%08x..%08x], f-off 0x%06x, size 0x%x\n", di.name, seg->addr,
		seg->addr + seg_size, offset, seg_size);

	if (verbose > 0)
		fprintf (stdlog, "- Create section %s [0x%08x..0x%08x], file offset 0x%06x, size 0x%x\n",
			di.name, seg->addr, seg->addr + seg_size, offset, seg_size);

	dh->sect_off[di.section] = htonl (offset);
	dh->sect_addr[di.section] = htonl (seg->addr);
	dh->sect_size[di.section] = htonl (seg_size);

	//--- patch entry point

	u32 addr = seg->main_entry_old;
	if (addr)
	{
		const u32 store = GetDolOffsetByAddr (dh, addr, sizeof (u32), 0);
		if (store)
			memcpy (str->data + store, &dh->entry_addr, sizeof (u32));
	}

	if (seg->main_entry)
	{
		if (verbose >= 1)
			fprintf (stdlog, "- Change entry point from 0x%08x to 0x%08x\n", ntohl (dh->entry_addr),
				seg->main_entry);
		write_be32 (&dh->entry_addr, seg->main_entry);
	}

	//--- patch VBI entry point

	if (seg->vbi_entry)
	{
		const u32 offset = GetDolOffsetByAddr (dh, patch_addr, sizeof (u32), 0);
		if (!offset)
		{
			ERROR0 (ERR_WARNING, "Can't patch DOL at address 0x%08x: %s\n", patch_addr, str->fname);
		}
		else
		{
			u32 addr = ntohl (seg->vbi_entry_old);
			if (addr)
			{
				const u32 store = GetDolOffsetByAddr (dh, addr, sizeof (u32), 0);
				if (!store)
				{
					ERROR0 (ERR_WARNING, "Can't store old VBI at DOL address 0x%08x: %s\n",
						seg->vbi_entry_old, str->fname);
				}
				else
					memcpy (str->data + store, str->data + offset, sizeof (u32));
			}

			const u32 code = (seg->vbi_entry - patch_addr) & 0x3ffffff | 0x48000000;
			PRINT ("PATCH address 0x%08x (file offset 0x%08x, VBI) from 0x%08x to 0x%08x\n",
				patch_addr, offset, be32 (str->data + offset), code);
			if (verbose > 0)
				fprintf (stdlog,
					"- Patch address 0x%08x (offset 0x%08x, VBI) from 0x%08x to 0x%08x\n",
					patch_addr, offset, be32 (str->data + offset), code);
			write_be32 (str->data + offset, code);
		}
	}

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

typedef struct gct_sect_t
{
	u16 off; // source offset
	u16 size; // size of section
	u16 idx; // initial section index
	u16 dest; // dest address of section (add 0x8000000 for real address)
} gct_sect_t;

//-----------------------------------------------------------------------------

static int sort_sect_list_by_size (const gct_sect_t *a, const gct_sect_t *b)
{
	return (int)b->size - (int)a->size;
}

//-----------------------------------------------------------------------------

static int sort_sect_list_by_dest (const gct_sect_t *a, const gct_sect_t *b)
{
	return (int)a->dest - (int)b->dest;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct cheat_code_loader_t
{
	u32 entry; // entry point => ASM: bl start
	u32 dummy_04; // ...

	//--- data, see cheat-code-loader.sh

	u32 return_addr; // absolute return address
	u32 var_version; // 'vars' version of current code loader
	u32 space; // needed space, must not < source_len

	u32 patch_addr; // address of patch list, relative to 'vars'
	u32 data_addr; // data address, relative to 'vars'
	u32 data_end; // data-end address, relative to 'vars'
	u32 gecko_off; // offset of first gecko code relative to data

	u32 verify_addr; // address to verify own codehandler
	u32 verify_val; // use this to verify & to decode
	u32 ch_patch_1; // abs address for first codehandler patch
	u32 ch_patch_2; // abs address for second codehandler patch
	u32 deny_user_gch; // 1: skip patching on !rSZS
	u32 info_flags; // info flags for NAS (8 bits)
} __attribute__ ((packed)) cheat_code_loader_t;

#define CCL_VAR_OFFSET offsetof (cheat_code_loader_t, return_addr)

///////////////////////////////////////////////////////////////////////////////

typedef enum
{
	CHT_NONE,
	CHT_ONLY,
	CHT_FULL
} code_handler_t;

//-----------------------------------------------------------------------------

static const u8 loader_asm_bz2[] = {
	// decompressed len: 0x338 = 824 bytes
	0x00,
	0x00,
	0x03,
	0x38,

	// bzipped data
	0x42,
	0x5a,
	0x68,
	0x39,
	0x31,
	0x41,
	0x59,
	0x26,
	0x53,
	0x59,
	0x9d,
	0xf6,
	0x23,
	0xf1,
	0x00,
	0x01,
	0x20,
	0xff,
	0xff,
	0xfd,
	0xde,
	0xc7,
	0xf6,
	0xc5,
	0xef,
	0xf5,
	0xff,
	0x64,
	0x5b,
	0xef,
	0xd0,
	0x6a,
	0xdc,
	0xc5,
	0xd7,
	0xfc,
	0xde,
	0x79,
	0xe0,
	0x53,
	0xd6,
	0x51,
	0x10,
	0x55,
	0xc3,
	0x41,
	0x65,
	0x45,
	0x84,
	0x65,
	0x44,
	0xc0,
	0x01,
	0xf5,
	0xc2,
	0xca,
	0xd2,
	0x1a,
	0xa6,
	0x8c,
	0xa6,
	0x90,
	0xf5,
	0x3d,
	0x27,
	0xa9,
	0x90,
	0x00,
	0xd0,
	0x06,
	0x26,
	0x83,
	0xd2,
	0x00,
	0x69,
	0x88,
	0x1a,
	0x00,
	0x19,
	0x00,
	0x1a,
	0x1a,
	0x32,
	0x01,
	0xa0,
	0xf4,
	0x41,
	0x90,
	0x32,
	0x00,
	0xc8,
	0x11,
	0x34,
	0x41,
	0x1a,
	0x4f,
	0xd5,
	0x3d,
	0x27,
	0x94,
	0xf5,
	0x32,
	0x1e,
	0xa0,
	0x19,
	0x34,
	0x19,
	0x00,
	0xd0,
	0xf4,
	0x80,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x0d,
	0x00,
	0xd0,
	0x06,
	0xd4,
	0x41,
	0x80,
	0x4c,
	0x46,
	0x09,
	0x93,
	0x08,
	0x30,
	0x98,
	0x98,
	0x13,
	0x00,
	0x43,
	0x08,
	0xc0,
	0x43,
	0x02,
	0x60,
	0x4c,
	0x26,
	0x08,
	0xc0,
	0x00,
	0x04,
	0xd0,
	0xc2,
	0x62,
	0x69,
	0x81,
	0x03,
	0x4d,
	0x42,
	0x98,
	0x50,
	0x09,
	0xb4,
	0x3d,
	0x40,
	0x69,
	0xa8,
	0xc9,
	0x80,
	0x68,
	0x00,
	0x01,
	0x32,
	0x34,
	0xd1,
	0xa6,
	0x00,
	0x11,
	0x81,
	0xa0,
	0x32,
	0x23,
	0x00,
	0x00,
	0x08,
	0x69,
	0x91,
	0xe9,
	0x0c,
	0xa2,
	0x54,
	0xaf,
	0x84,
	0x8f,
	0x0a,
	0x52,
	0x54,
	0xba,
	0xa7,
	0xd1,
	0x59,
	0x0d,
	0x00,
	0x4b,
	0x69,
	0xdf,
	0xb0,
	0xb0,
	0xb3,
	0xd3,
	0x4a,
	0x1c,
	0xa2,
	0x85,
	0xc9,
	0x8f,
	0xc7,
	0xb6,
	0x9e,
	0x7e,
	0x12,
	0x2b,
	0x1d,
	0x13,
	0x3f,
	0x9e,
	0x9e,
	0x4b,
	0xb0,
	0x31,
	0x30,
	0x83,
	0x55,
	0x67,
	0x43,
	0xde,
	0x80,
	0x53,
	0x40,
	0x4f,
	0xcd,
	0x7b,
	0x06,
	0x1c,
	0x04,
	0xd7,
	0x3e,
	0x0c,
	0xc5,
	0xd2,
	0x66,
	0x27,
	0xf5,
	0x19,
	0x65,
	0xeb,
	0x81,
	0x01,
	0x48,
	0xa9,
	0x51,
	0x42,
	0x9b,
	0xba,
	0x56,
	0x19,
	0x92,
	0x45,
	0x52,
	0x6e,
	0x22,
	0xe0,
	0xb5,
	0x97,
	0x2c,
	0x23,
	0xda,
	0x75,
	0x1c,
	0x16,
	0x2e,
	0xb1,
	0x88,
	0x8e,
	0x91,
	0xa0,
	0xcf,
	0x5a,
	0xc9,
	0x5e,
	0x99,
	0x27,
	0xe1,
	0x52,
	0x18,
	0x16,
	0x42,
	0x60,
	0x17,
	0x49,
	0x28,
	0xc2,
	0x8e,
	0x18,
	0x12,
	0x10,
	0xda,
	0x46,
	0x01,
	0x76,
	0x36,
	0xd4,
	0x07,
	0xe3,
	0x9d,
	0x42,
	0x5c,
	0xc2,
	0xba,
	0x55,
	0x7f,
	0xbd,
	0x14,
	0x02,
	0x70,
	0x29,
	0x37,
	0x17,
	0xf6,
	0xda,
	0x50,
	0x5b,
	0x05,
	0xa1,
	0xb9,
	0x06,
	0xcd,
	0xb0,
	0xde,
	0x93,
	0x88,
	0x8b,
	0x0f,
	0x9d,
	0x2a,
	0x4d,
	0xc8,
	0x6b,
	0x7c,
	0xf1,
	0x01,
	0xf2,
	0x14,
	0x6b,
	0x11,
	0x48,
	0x27,
	0x7d,
	0xb8,
	0xab,
	0x43,
	0x15,
	0x5d,
	0x46,
	0x21,
	0x1f,
	0x70,
	0x56,
	0x77,
	0x4a,
	0x82,
	0xcc,
	0x8b,
	0xf1,
	0xc9,
	0xc0,
	0x73,
	0xde,
	0x98,
	0x74,
	0x4b,
	0x96,
	0x39,
	0xb2,
	0x6b,
	0x99,
	0xee,
	0x23,
	0x0d,
	0xf0,
	0x69,
	0x16,
	0x12,
	0x26,
	0xf7,
	0x2c,
	0xe8,
	0x60,
	0x78,
	0xfa,
	0x13,
	0x3b,
	0x56,
	0xc8,
	0xaa,
	0x69,
	0x82,
	0x3e,
	0xc8,
	0x2b,
	0xee,
	0xad,
	0xb9,
	0x65,
	0x8e,
	0x44,
	0x98,
	0x27,
	0x77,
	0xcd,
	0xc5,
	0xcf,
	0x48,
	0x17,
	0x93,
	0x04,
	0x2a,
	0x82,
	0x96,
	0xa2,
	0x6d,
	0x64,
	0x1c,
	0x03,
	0x7d,
	0x91,
	0xd1,
	0x4f,
	0x38,
	0x78,
	0x7f,
	0x2a,
	0xfb,
	0x03,
	0xe6,
	0x2f,
	0xe3,
	0x9f,
	0xe5,
	0x0b,
	0x36,
	0x58,
	0x8c,
	0x70,
	0xba,
	0x9d,
	0x3b,
	0xf3,
	0x4b,
	0xfe,
	0xb9,
	0x53,
	0x6a,
	0x4c,
	0xe8,
	0x6c,
	0xb9,
	0xfc,
	0x32,
	0xa3,
	0x13,
	0xc9,
	0xed,
	0x29,
	0x10,
	0x4c,
	0x48,
	0xe1,
	0x32,
	0x81,
	0x0a,
	0x24,
	0x80,
	0x20,
	0x15,
	0x82,
	0x0d,
	0x89,
	0x26,
	0xa1,
	0x29,
	0x66,
	0x08,
	0x0a,
	0xd0,
	0x9e,
	0xe0,
	0xce,
	0x50,
	0x7b,
	0xc5,
	0xfb,
	0x95,
	0x66,
	0x42,
	0x65,
	0x84,
	0xda,
	0xdf,
	0x37,
	0x52,
	0x53,
	0x23,
	0x06,
	0x09,
	0x39,
	0xf6,
	0x1f,
	0x63,
	0xf1,
	0x0b,
	0x13,
	0xd6,
	0x9c,
	0xa2,
	0x63,
	0x2d,
	0xef,
	0xa2,
	0xb2,
	0x8d,
	0xd2,
	0xa0,
	0xdd,
	0xe8,
	0x39,
	0x9e,
	0xf5,
	0x71,
	0xa4,
	0xd1,
	0xc5,
	0x74,
	0x11,
	0x11,
	0xbb,
	0x42,
	0x20,
	0x53,
	0x92,
	0xff,
	0xc5,
	0x36,
	0x67,
	0x4e,
	0xa4,
	0x1b,
	0x55,
	0x95,
	0xef,
	0x3a,
	0x30,
	0x12,
	0x85,
	0xea,
	0xe2,
	0x75,
	0xbf,
	0x1a,
	0xb8,
	0xc8,
	0xa8,
	0x12,
	0x2d,
	0x19,
	0x17,
	0x91,
	0xdd,
	0xa7,
	0x23,
	0xbb,
	0xe7,
	0x18,
	0xb4,
	0x61,
	0x78,
	0xa8,
	0xc4,
	0x3b,
	0x84,
	0x5a,
	0x2d,
	0x35,
	0x5f,
	0xac,
	0xd8,
	0x82,
	0xae,
	0x21,
	0x8e,
	0x23,
	0x7d,
	0xf3,
	0x93,
	0x2d,
	0xff,
	0x7b,
	0xbc,
	0xbd,
	0x42,
	0x65,
	0xfc,
	0x1e,
	0xf9,
	0x2d,
	0xf9,
	0x69,
	0x5a,
	0x12,
	0x08,
	0x20,
	0x90,
	0x41,
	0x00,
	0xc9,
	0x62,
	0xd2,
	0x88,
	0xc2,
	0x2a,
	0xa2,
	0x95,
	0xc6,
	0x52,
	0xab,
	0x53,
	0x31,
	0xb5,
	0xae,
	0x62,
	0x36,
	0x07,
	0x94,
	0x8f,
	0x05,
	0x87,
	0x98,
	0x4a,
	0x26,
	0x1a,
	0x0f,
	0x1f,
	0xdb,
	0x2e,
	0x20,
	0xbb,
	0x00,
	0x3c,
	0xb8,
	0x74,
	0x4c,
	0xb2,
	0x04,
	0x8f,
	0x98,
	0xec,
	0x1d,
	0x43,
	0x10,
	0x97,
	0x08,
	0x33,
	0x05,
	0x0a,
	0xa0,
	0x48,
	0x23,
	0x04,
	0x50,
	0xc0,
	0xfd,
	0x68,
	0x2e,
	0x24,
	0x97,
	0xb1,
	0x4c,
	0xac,
	0xb6,
	0x12,
	0xd4,
	0x00,
	0x65,
	0xdb,
	0x5d,
	0x74,
	0x19,
	0x20,
	0x9e,
	0x58,
	0xc2,
	0x10,
	0x12,
	0x00,
	0x2b,
	0x52,
	0x00,
	0xea,
	0xa8,
	0xe0,
	0x54,
	0x84,
	0xd0,
	0x7e,
	0x52,
	0x38,
	0x08,
	0x2a,
	0x82,
	0x40,
	0x59,
	0x3c,
	0xde,
	0xc5,
	0x08,
	0x1c,
	0x8f,
	0x4a,
	0x79,
	0x27,
	0x98,
	0xbb,
	0x92,
	0x29,
	0xc2,
	0x84,
	0x84,
	0xef,
	0xb1,
	0x1f,
	0x88,
	// 0x2e1 = 737 Bytes
};

static BZ2Manager_t loader_asm_mgr = { loader_asm_bz2, sizeof (loader_asm_bz2), 0, 0 };

///////////////////////////////////////////////////////////////////////////////

static enumError SetupCodeLoader (
	// return ERR_OK | ERR_WARNING of created with warnings,
	// ERR_ERROR on abort, ERR_NOTHING_TO_DO if nothing to do

	staticr_t *str, // valid pointer
	const wch_segment_t *seg, // NULL or segement info, seg->data is ignored
	u8 *code_data, // NULL or code data
	uint code_size, // NULL or size of code data
	uint cheat_offset, // offset first cheat code
	code_handler_t ch_type, // code handler type
	ccp fname // filename for messages
)
{
	ASSERT (code_size >= cheat_offset);
	const uint total_data_size = wcode_job.used + wcode_data.used + code_size - cheat_offset;

#if defined(TEST) || defined(DEBUG) // || HAVE_WIIMM_EXT
	if (logging >= 3)
		fprintf (stderr, "SetupCodeLoader() %d+%d+%d = %d, c-off=0x%x, ch-type=%d, seg=%d => %s\n",
			code_size, wcode_job.used, wcode_data.used, total_data_size, cheat_offset, ch_type,
			seg != 0, total_data_size ? "\e[41;37;1m CREATE WIIMMFI CODE \e[0m" : "quit");
#endif

	if (!total_data_size)
		return ERR_NOTHING_TO_DO;

	//--- data

	GrowBuffer_t data;
	InitializeGrowBuffer (&data, MiB);
	data.grow_size = 0x1000;

	//--- code handler

	const u32 security_off = GCT_REG_OFFSET + 16; // gecko register 4
	const u32 security_val = 0xdc020463;

	enumError err = ERR_OK;
	if (code_size)
	{
		ASSERT (code_data);
		write_be32 (code_data + security_off, security_val);
		write_be32 (code_data + security_off + 8, security_val);
		write_be64 (code_data + cheat_offset, GCT_TERM_NUM);
		err = AddSectionHelper2 (str, seg, code_data, cheat_offset + 8, fname);
		if (err)
			goto abort;
		code_data += cheat_offset;
		code_size -= cheat_offset;
		write_be64 (code_data, GCT_MAGIC8_NUM);
		str->dol_flags[DPF_CHEAT] = 'l';
	}

	//--- setup data buffer

	DecodeBZIP2Manager (&loader_asm_mgr);
	InsertGrowBuffer (&data, loader_asm_mgr.data, loader_asm_mgr.size);
	cheat_code_loader_t ccl;
	memcpy (&ccl, data.ptr, sizeof (ccl));
	ccl.info_flags = htonl (str->dol_info_flags);

	uint code_offset = loader_asm_mgr.size - CCL_VAR_OFFSET;
	uint deny_user_gch = opt_allow_user_gch < OFFON_AUTO;
	if (wcode_job.used)
	{
		ccl.patch_addr = htonl (code_offset);
		InsertGrowBuffer (&data, wcode_job.ptr, wcode_job.used);
		InsertGrowBuffer (&data, EmptyString, 4);
		code_offset += wcode_job.used + 4;
		deny_user_gch = opt_allow_user_gch <= OFFON_AUTO;
	}
	str->dol_flags[DPF_ALLOW_USER] = deny_user_gch ? 'd' : 'a';
	if (deny_user_gch && code_size)
	{
		str->dol_flags[DPF_ALLOW_USER] = 's';
		deny_user_gch = 2;
	}
	ccl.deny_user_gch = htonl (deny_user_gch);
	ccl.data_addr = htonl (code_offset);
	ccl.gecko_off = htonl (wcode_data.used);

	if (wcode_data.used)
		InsertGrowBuffer (&data, wcode_data.ptr, wcode_data.used);

	if (code_size)
	{
		InsertGrowBuffer (&data, code_data, code_size);
		ccl.verify_addr = htonl (seg->addr + security_off);
	}
	ccl.data_end = htonl (data.used - CCL_VAR_OFFSET);
	const u32 space = data.used - CCL_VAR_OFFSET - code_offset;
	ccl.space = htonl (space > opt_gct_space ? space : opt_gct_space);
	ccl.verify_val = htonl (security_val);

	if (ch_type == CHT_FULL)
	{
		ccl.ch_patch_1 = htonl (seg->addr + 0x04DC);
		ccl.ch_patch_2 = htonl (seg->addr + 0x0758);
	}
	else if (ch_type == CHT_ONLY)
		ccl.ch_patch_1 = htonl (seg->addr + 0x0104);

	memcpy (data.ptr, &ccl, sizeof (ccl));

	//--- create segment

	wch_segment_t dseg;
	memset (&dseg, 0, sizeof (dseg));
	dseg.type = 't';
	dseg.size = data.used;
	dseg.addr = opt_gct_addr < 0x80000000
		? FindFreeBssSpaceDOL ((dol_header_t *)str->data, dseg.size, 32, 0)
		: opt_gct_addr;
	dseg.main_entry = dseg.addr;
	dseg.main_entry_old = dseg.addr + offsetof (cheat_code_loader_t, return_addr);

	u32 *ptr = (u32 *)(data.ptr + loader_asm_mgr.size);
	u32 *end = (u32 *)(data.ptr + data.used);
	u32 key = security_val;
	while (ptr < end)
	{
		const u32 orig = be32 (ptr);
		const u32 val = orig ^ key;
		write_be32 (ptr, val << 19 | val >> 13);
		key += orig >> 5;
		ptr++;
	}

	err = AddSectionHelper2 (str, &dseg, data.ptr, dseg.size, fname);
	str->gct_moved = true;

	//--- clean
abort:
	ResetGrowBuffer (&data);
	ClearGrowBuffer (&wcode_job);
	ClearGrowBuffer (&wcode_data);

	return err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError AddSectionHelper (
	// return ERR_OK | ERR_WARNING of created with warnings | ERR_ERROR on abort
	staticr_t *str, // valid pointer
	const wch_segment_t *seg, // segement info, seg->data is ignored
	const u8 *seg_data, // segement data
	ccp fname // filename for messages
)
{
	//--- patching possible?

	const u32 patch_addr = GetVbiAddress (str);
	if (!patch_addr)
		return 0;

	//--- check presence of 'codehandleronly' or 'codehandler'
	//--- and optional replace 'codehandleronly' by 'codehandler'

	u8 *new_data = 0;
	uint seg_size = seg->size;

	code_handler_t ch_type = CHT_NONE;
	uint cheat_offset = 0;

	BZ2Manager_t *cho = &codehandleronly_mgr;
	DecodeBZIP2Manager (cho);
	if (cho->size > sizeof (gch_header_t))
	{
		const u8 *cho_data = cho->data + sizeof (gch_header_t);
		const uint cho_size = cho->size - sizeof (gch_header_t);
		if (seg_size >= cho_size && !memcmp (cho_data, seg_data, cho_size))
		{
			ch_type = CHT_ONLY;
			cheat_offset = cho_size;

			if (opt_full_gch)
			{
				PRINT ("\e[35;1m Only: %u >= ( %u -> %u -> %u )\e[0m\n", seg_size,
					codehandleronly_mgr.src_size, codehandleronly_mgr.size, cho_size);

				BZ2Manager_t *chf = &codehandler_mgr;
				DecodeBZIP2Manager (chf);
				if (chf->size > sizeof (gch_header_t))
				{
					if (verbose > 0)
						fprintf (stdlog, "- Replace 'codehandleronly' by 'codehandler'\n");

					const u8 *chf_data = chf->data + sizeof (gch_header_t);
					const uint chf_size = chf->size - sizeof (gch_header_t);
					const uint new_size = seg_size - cho_size + chf_size;
					new_data = MALLOC (new_size);
					memcpy (new_data, chf_data, chf_size);
					memcpy (new_data + chf_size, seg_data + cho_size, seg_size - cho_size);
					seg_data = new_data;
					seg_size = new_size;

					ch_type = CHT_FULL;
					cheat_offset = chf_size;
				}
			}
		}
	}

	if (ch_type == CHT_NONE)
	{
		BZ2Manager_t *chf = &codehandler_mgr;
		DecodeBZIP2Manager (chf);
		if (chf->size > sizeof (gch_header_t))
		{
			const u8 *chf_data = chf->data + sizeof (gch_header_t);
			const uint chf_size = chf->size - sizeof (gch_header_t);
			if (seg_size >= chf_size && !memcmp (chf_data, seg_data, chf_size))
			{
				ch_type = CHT_FULL;
				cheat_offset = chf_size;
			}
		}
	}

	//--- find and remove separators

	enum
	{
		MAX_SECT = 200
	};
	gct_sect_t sect_list[MAX_SECT + 1], *sect = sect_list, *sect_end = sect_list + MAX_SECT;
	memset (sect_list, 0, sizeof (sect_list));

	if (cheat_offset > sizeof (gch_header_t) && opt_gct_scan_sep)
	{
		if (!new_data)
		{
			// we need modifiable data!
			new_data = MEMDUP (seg_data, seg_size);
			seg_data = new_data;
		}

		u8 *src = new_data + cheat_offset;
		u8 *dest = src;
		u8 *end = new_data + seg_size;

		while (src + 8 <= end)
		{
			PRINT (">> LINE %3zd => %3zd : %08x %08x\n", (src - new_data - cheat_offset) / 8,
				(dest - new_data - cheat_offset) / 8, be32 (src), be32 (src + 4));

			uint copy_lines = 0;
			if (be32 (src) == 0xf0000000 && !be32 (src + 4))
			{
				const u32 off = dest - new_data;
				sect->size = off - sect->off;
				sect++;
				sect->idx = sect - sect_list;
				sect->off = off;
				sect->size = 8;
				*(u64 *)dest = *(u64 *)src;
				src += 8;
				dest += 8;
				break;
			}
			else if (be32 (src) == GCT_SEP_NUM1 && !be16 (src + 4))
			{
				if (opt_gct_sep)
				{
					const u32 off = dest - new_data;
					if (sect->off != off && sect < sect_end)
					{
						sect->size = off - sect->off;
						sect++;
						sect->idx = sect - sect_list;
						sect->off = off;
						sect->size = 0;
					}
				}

				PRINT ("\t>> SEP#%zu found: %08x %08x\n", sect - sect_list, be32 (src),
					be32 (src + 4));
				copy_lines = be16 (src + 6);
				src += 8;
			}
			else if (*src == 0xc0 || *src == 0xc2 || *src == 0xd0 || *src == 0xd2)
			{
				if (opt_gct_asm_sep)
				{
					const u32 off = dest - new_data;
					if (sect->off != off && sect < sect_end)
					{
						sect->size = off - sect->off;
						sect++;
						sect->idx = sect - sect_list;
						sect->off = off;
						sect->size = 0;
					}
				}

				PRINT (
					">> ASM#%zu found: %08x %08x\n", sect - sect_list, be32 (src), be32 (src + 4));
				copy_lines = be32 (src + 4) + 1;
			}
			else if ((*src == 0x66 || *src == 0x68)
				&& (src[1] == 0x00 || src[1] == 0x10 || src[1] == 0x20))
			{
				PRINT (">> GOTO/GOSUB#%zu found: %08x %08x\n", sect - sect_list, be32 (src),
					be32 (src + 4));
				s16 go = be16 (src + 2);
				if (go >= 0)
					copy_lines = go + 1;
			}
			else
			{
				*(u64 *)dest = *(u64 *)src;
				src += 8;
				dest += 8;
				continue;
			}

			if (copy_lines > 0)
			{
				// use it this way to detect bad large values
				const uint max = (end - src) / 8;
				if (copy_lines > max)
					copy_lines = max;
				copy_lines *= 8; // now we have copy_bytes!

				memmove (dest, src, copy_lines);
				src += copy_lines;
				dest += copy_lines;
			}
		}

		// PRINT(">>> %zd %zd /%zd\n",dest-new_data,src-new_data,end-new_data);
		if (src >= end)
			seg_size = dest - new_data;
		else if (src > dest)
			memset (dest, 0, src - dest);
	}
	const int n_sect = sect - sect_list;

	//--- move GCT part

	str->dol_flags[DPF_CHEAT] = 'c';

	enumError err = ERR_OK;
	if (opt_gct_move >= OFFON_ON && cheat_offset >= 0x100)
	{
	use_loader:;
		if (!new_data)
		{
			// we need modifiable data!
			new_data = MEMDUP (seg_data, seg_size);
			seg_data = new_data;
		}

		err = SetupCodeLoader (str, seg, new_data, seg_size, cheat_offset, ch_type, fname);
		goto abort;
	}

	//--- split GCT part

	const u32 off_end = seg->addr + seg_size;
	PRINT ("N=%d, of=%08x\n", n_sect, off_end);
	if (n_sect > 2 && seg->addr + cheat_offset < 0x80002ff0 && off_end >= GCT_MAX
		&& off_end < GCT_END_CHECK)
	{
#ifdef TEST0
		{
			uint i;
			for (i = 0, sect = sect_list; i <= n_sect; i++, sect++)
				PRINT (" -> SECT %2u: %4x .. %4x, size %4x\n", i, sect->off, sect->off + sect->size,
					sect->size);
		}
#endif

		bool sorted = false;
	restart:;
		u8 data[0x4000];
		memset (data, 0, sizeof (data));

		const u32 base_addr = seg->addr - 0x80000000;
		memcpy (data + base_addr, seg_data, cheat_offset + 8);

		struct data_t
		{
			u32 used;
			u32 size;
			u8 *data;
		} data_tab[] = { { cheat_offset + 8, 0x2ff8 - base_addr, data + base_addr },
			{ 0, 0x37f8 - 0x3200, data + 0x3200 }, { 0, 0x3ff8 - 0x3880, data + 0x3880 },
			{ 0, 0, 0 } };

		uint i;
		struct data_t *dp;
		bool fail = false;

		if (opt_gct_list > 1)
		{
			fprintf (stdlog, "\nSummary of detected GCT sections:\n");
			for (i = 1, sect = sect_list + 1; i < n_sect; i++, sect++)
				fprintf (stdlog, "   S%02u: %04x .. %04x, size %04x = %4u/dec\n", sect->idx,
					sect->off, sect->off + sect->size, sect->size, sect->size);
		}

		for (i = 1, sect = sect_list + 1; i < n_sect; i++, sect++)
		{
			bool done = false;
			for (dp = data_tab; dp->data; dp++)
				if (sect->size <= dp->size - dp->used)
				{
					memcpy (dp->data + dp->used, seg_data + sect->off, sect->size);
					sect->dest = dp->data + dp->used - data;
					dp->used += sect->size;
					done = true;
					break;
				}
			if (!done)
			{
				sect->dest = M1 (sect->dest);
				ERROR0 (ERR_WARNING, "No space for cheat code section %u (size %x): %s\n", i,
					sect->size, fname);
				fail = true;
			}
		}

#ifdef TEST0
		{
			uint i;
			const struct data_t *dp;
			for (i = 0, dp = data_tab; dp->data; i++, dp++)
				printf (" -> %2u: %04zx .. %04zx : %4x / %4x : %4x\n", i, dp->data - data,
					dp->data - data + dp->used, dp->used, dp->size, dp->size - dp->used);
		}
#endif

		if (fail)
		{
			if (!sorted)
			{
				qsort (sect_list + 1, n_sect - 1, sizeof (*sect_list),
					(qsort_func)sort_sect_list_by_size);
				sorted = true;
				goto restart;
			}

			FREE (new_data);
			ERROR0 (ERR_ERROR, "Not enough space for cheat codes: %s\n", fname);
			return ERR_WARNING;
		}

		if (opt_gct_list > 0)
		{
			qsort (
				sect_list + 1, n_sect - 1, sizeof (*sect_list), (qsort_func)sort_sect_list_by_dest);
			fprintf (stdlog, "\nSummary of GCT section distribution:\n");
			for (i = 1, sect = sect_list + 1; i < n_sect; i++, sect++)
			{
				fprintf (stdlog, "   S%02u: %04x .. %04x, size %04x = %4u/dec => ", sect->idx,
					sect->off, sect->off + sect->size, sect->size, sect->size);
				if (IS_M1 (sect->dest))
					fprintf (stdlog, "FAILED!\n");
				else
					fprintf (stdlog, "%04x .. %04x\n", sect->dest, sect->dest + sect->size);
			}
			fputc ('\n', stdlog);
		}

		struct data_t *prev = 0;
		for (dp = data_tab; dp->data; dp++)
			if (dp->used)
			{
				if (prev)
				{
					u8 *dest = prev->data + prev->used;
					write_be16 (dest, 0x6620);
					write_be16 (dest + 2, (dp->data - dest - 8) / 8);
					write_be32 (dest + 4, 0);
					prev->used += 8;
				}
				prev = dp;
			}
		if (prev)
		{
			u8 *dest = prev->data + prev->used;
			write_be32 (dest, 0xf0000000ul);
			write_be32 (dest + 4, 0);
			prev->used += 8;
		}

#ifdef TEST0
		{
			uint i;
			const struct data_t *dp;
			for (i = 0, dp = data_tab; dp->data; i++, dp++)
				printf ("  -> %2u: %04zx .. %04zx : %4x / %4x : %4x\n", i, dp->data - data,
					dp->data - data + dp->used, dp->used, dp->size, dp->size - dp->used);
		}
#endif

		err = AddSectionHelper2 (str, seg, data_tab->data, data_tab->used, fname);
		if (err == ERR_OK)
		{
			wch_segment_t dseg;
			memset (&dseg, 0, sizeof (dseg));
			dseg.type = 'd';
			if (data_tab[1].used)
			{
				dseg.addr = data_tab[1].data - data + 0x80000000;
				const uint size = data_tab[2].used
					? data_tab[2].data + data_tab[2].used - data_tab[1].data
					: data_tab[1].used;
				err = AddSectionHelper2 (str, &dseg, data_tab[1].data, size, fname);
			}
			else if (data_tab[2].used)
			{
				dseg.addr = data_tab[2].data - data + 0x80000000;
				err = AddSectionHelper2 (str, &dseg, data_tab[2].data, data_tab[2].used, fname);
			}
		}
		return err;
	}

	//--- warn if overlapping 0x80003xxx

	if (ch_type != CHT_NONE && (seg->addr < GCT_END_CHECK && off_end > GCT_MAX))
	{
		if (opt_gct_move >= OFFON_AUTO && cheat_offset >= 0x100)
			goto use_loader;

		FREE (new_data);
		ERROR0 (ERR_ERROR, "Cheat codes must not overlay %#x..%#x: %s\n", GCT_MAX, GCT_END_CHECK,
			fname);
		return ERR_WARNING;
	}

	//--- standard: use 1 section

	err = AddSectionHelper2 (str, seg, seg_data, seg_size, fname);

abort:
	FREE (new_data);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError ReadCodeFile (staticr_t *str, ccp fname, u8 **data, uint *size, bool *alloced)
{
	DASSERT (str);
	DASSERT (fname);
	DASSERT (data);
	DASSERT (size);
	DASSERT (alloced);

	*data = 0;
	*size = 0;
	*alloced = false;

	if (fname && fname[0] == '@')
	{
		CompressManager_t *cm = 0;
		if (!strcmp (fname, "@LECODE"))
			cm = GetLecodeLoader (str->mode);
		else if (!strcmp (fname, "@OLDLECODE"))
			cm = GetOldLecodeLoader (str->mode);

		if (cm)
		{
			DecompressManager (cm);
			*data = cm->data;
			*size = cm->size;
			// HexDump16(stderr,0,0,*data,32);
			return ERR_OK;
		}
	}

	enumError err = OpenReadFILE (fname, 0, true, data, size, 0, 0);
	if (err)
	{
		char path_buf[PATH_MAX];
		if (ReplaceRegionChar (str, path_buf, sizeof (path_buf), fname))
			err = OpenReadFILE (path_buf, 0, true, data, size, 0, 0);

		if (err)
		{
			ERROR0 (ERR_WARNING, "Can't load file: %s\n", fname);
			return err;
		}
	}

	*alloced = true;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError AddWCode (cvp data, uint size)
{
	DASSERT (data);
	DASSERT (!(size & 7));
	InititializeGCT ();

	u8 *ptr = (u8 *)data;
	u8 *end = ptr + size;
	u32 buf[5];

	if (be64 (ptr) == GCT_MAGIC8_NUM)
		ptr += 8;

	while (ptr < end)
	{
		switch (*ptr)
		{
			case 0x04:
				buf[0] = htonl (0x04);
				buf[1] = htonl (be32 (ptr) & 0xffffff | 0x80000000);
				buf[2] = ((u32 *)ptr)[1];
				InsertGrowBuffer (&wcode_job, buf, 12);
				use_gct_loader = true;
				ptr += 8;
				break;

			case 0xc2:
			{
				const uint len = 8 * be32 (ptr + 4);
				buf[0] = htonl (0xc2);
				buf[1] = htonl (be32 (ptr) & 0xffffff | 0x80000000);
				buf[2] = htonl (wcode_data.used);
				buf[3] = htonl (wcode_data.used + len - 4);
				InsertGrowBuffer (&wcode_job, buf, 16);
				InsertGrowBuffer (&wcode_data, ptr + 8, len);
				use_gct_loader = true;
				ptr += 8 + len;
			}
			break;

			case 0xf0:
				if (be32 (ptr) != GCT_SEP_NUM1)
					goto term;
				ptr += 8; // ignore separators
				break;

			default:
				return ERROR0 (ERR_WARNING, "WCODE %02x not supported => ABORT\n", *ptr);
		}
	}
term:
	if (logging >= 3)
		TraceGCT ("WCode");
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError AddCheatCode (
	// returns ERR_OK, ERR_NOTHING_TO_DO, ERR_WARNING or ERR_INTERNAL

	const u8 *code_data, // cheat code data
	uint code_size, // cheat code size
	bool check_dup, // TRUE: check for duplicates
	ccp fname // file name for error messages
)
{
	DASSERT (code_data);

	//--- verify code_size and skip magic and terminator

	code_size &= ~7; // multiple of 8, cut additional bytes
	if (code_size >= 8 && be64 (code_data) == GCT_MAGIC8_NUM)
	{
		// skip magic
		code_data += 8;
		code_size -= 8;
	}

	if (code_size >= 8 && be64 (code_data + code_size - 8) == GCT_TERM_NUM)
	{
		// ignore terminator
		code_size -= 8;
	}

	if (!code_size)
		return ERR_NOTHING_TO_DO;

	//--- first code -> add code handler

	InititializeGCT ();

	if (!gct_data_mem.used)
	{
		BZ2Manager_t *mgr = opt_full_gch ? &codehandler_mgr : &codehandleronly_mgr;
		enumError err = DecodeBZIP2Manager (mgr);
		if (err)
			return ERROR0 (ERR_INTERNAL, 0);

		gct_data_mem.begin = GCT_ADDRESS;
		MemMapItem_t *mi;
		mi = InsertMemMap (&gct_data_mem, GCT_ADDRESS, mgr->size - sizeof (gch_header_t));
		StringCopyS (mi->info, sizeof (mi->info), "Gecko Code Handler");
		mi = InsertMemMap (&gct_data_mem, GCT_MM_BASE + mgr->size, 8);
		StringCopyS (mi->info, sizeof (mi->info), "GCT header");
		if (!use_gct_loader)
		{
			mi = InsertMemMap (&gct_data_mem, GCT_MAX, 0);
			StringCopyS (mi->info, sizeof (mi->info), "-- max possible --");
		}

		InsertGrowBuffer (&gecko_data, mgr->data, mgr->size);
		u64 magic = hton64 (GCT_MAGIC8_NUM);
		InsertGrowBuffer (&gecko_data, &magic, sizeof (magic));
		if (logging >= 3)
			TraceGCT ("Setup");
	}

	//--- check, if same code already inserted

	if (check_dup && code_size <= gecko_data.used)
	{
		const u8 *ptr = gecko_data.ptr;
		const u8 *end = ptr + gecko_data.used - code_size;

		while (ptr <= end)
		{
			if (!memcmp (ptr, code_data, code_size))
				return ERR_NOTHING_TO_DO;
			ptr += 8;
		}
	}

	//--- add cheat code

	MemMapItem_t *mi = InsertMemMap (&gct_data_mem, GCT_MM_BASE + gecko_data.used, code_size);
	if (fname && *fname)
	{
		const int space = sizeof (mi->info) - 1;
		ccp fn = fname;
		while (strlen (fn) > space)
		{
			ccp slash = strchr (fn, '/');
			if (!slash)
				break;
			fn = slash + 1;
		}
		if (strlen (fn) <= space - 6)
			StringCat2S (mi->info, sizeof (mi->info), "File: ", fn);
		else
			StringCopyS (mi->info, sizeof (mi->info), fn);
	}
	else
		snprintf (mi->info, sizeof (mi->info), "%u line%s of GCT file", code_size / 8,
			code_size == 8 ? "" : "s");

	if (opt_gct_scan_sep && opt_gct_sep && be32 (code_data) != GCT_SEP_NUM1)
	{
		u32 buf[2];
		buf[0] = htonl (GCT_SEP_NUM1);
		buf[1] = htonl (code_size / 8);
		InsertGrowBuffer (&gecko_data, buf, 8);
	}

	InsertGrowBuffer (&gecko_data, code_data, code_size);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError AddWCodeFile (staticr_t *str, ccp fname)
{
	DASSERT (str);
	if (!fname || !*fname)
		return ERR_OK;

	u8 *data;
	uint size;
	bool alloced;
	enumError err = ReadCodeFile (str, fname, &data, &size, &alloced);
	if (err)
		return err;

	// [[analyse-magic]]
	const file_format_t fform = GetByMagicFF (data, size, size);
	if (fform == FF_GCT)
	{
		err = opt_wcode == WCODE_GECKO ? AddCheatCode (data, size, true, fname)
									   : AddWCode (data, size);
	}
	else
		err = ERROR0 (ERR_WARNING, "Not a GCT file: %s\n", fname);

	if (alloced)
		FREE (data);
	return err;
}

