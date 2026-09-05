int GetRelOffsetByAddrM (str_mode_t mode, // region mode
	u32 addr, // address to search
	u32 size, // >0: wanted size
	u32 *valid_size // not NULL: return valid size
)
{
	const rel_info_t *ri = GetInfoREL (mode);
	if (ri)
	{
		addr &= ~0x40000000; // remove mirror bit

		if (addr >= ri->load_addr && addr < ri->load_addr + ri->offset_sect)
			return addr - ri->load_addr;

		const dol_sect_info_t *sect;
		for (sect = ri->sect; sect->section >= 0; sect++)
		{
			if (addr >= sect->addr && addr < sect->addr + sect->size)
			{
				u32 max_size = sect->addr + sect->size - addr;
				if (!size)
				{
					if (valid_size)
						*valid_size = max_size;
				}
				else if (size <= max_size)
				{
					if (valid_size)
						*valid_size = size;
				}
				else if (valid_size)
					*valid_size = max_size;
				else
					return 0;

				return sect->off + addr - sect->addr;
			}
		}
	}

	if (valid_size)
		*valid_size = 0;
	return ~0;
}

//-----------------------------------------------------------------------------

u32 GetRelAddrByOffsetM (str_mode_t mode, // region mode
	u32 off, // offset to search
	u32 size, // >0: wanted size
	u32 *valid_size // not NULL: return valid size
)
{
	const rel_info_t *ri = GetInfoREL (mode);
	if (ri)
	{
		if (off < ri->sect[0].off)
			return ri->load_addr + off;

		const dol_sect_info_t *sect;
		for (sect = ri->sect; sect->section >= 0; sect++)
		{
			if (off >= sect->off && off < sect->off + sect->size)
			{
				u32 max_size = sect->off + sect->size - off;
				if (!size)
				{
					if (valid_size)
						*valid_size = max_size;
				}
				else if (size <= max_size)
				{
					if (valid_size)
						*valid_size = size;
				}
				else if (valid_size)
					*valid_size = max_size;
				else
					return 0;

				return sect->addr + off - sect->off;
			}
		}
	}

	if (valid_size)
		*valid_size = 0;
	return 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			struct staticr_t		///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeSTR (staticr_t *str)
{
	memset (str, 0, sizeof (*str));
	memset (str->dol_flags, '-', sizeof (str->dol_flags) - 1);
	str->fname = EmptyString;
}

///////////////////////////////////////////////////////////////////////////////

void ResetSTR (staticr_t *str)
{
	if (str)
	{
		if (str->data_alloced)
			FREE (str->data);
		FreeString (str->fname);

		InitializeSTR (str);
	}
}

///////////////////////////////////////////////////////////////////////////////

static bool CheckSizeSTR (staticr_t *str)
{
	DASSERT (str);

	switch (str->data_size)
	{
		case 4903876:
			str->mode = STR_M_PAL;
			break;

		case 4902804:
			str->mode = STR_M_USA;
			break;

		case 4901564:
			str->mode = STR_M_JAP;
			break;

		case 4905468:
			str->mode = STR_M_KOR;
			break;

		default:
			str->mode = STR_M_UNKNOWN;
			str->str_status = STR_S_ANALYZED | STR_S_WRONG_SIZE;
			return false;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////

enumError LoadSTR (staticr_t *str, // valid pointer
	bool initialize, // true: initialize 'bmg'
	ccp fname, // filename of source
	bool ignore_no_file // ignore if file does not exists
						// and return warning ERR_NOT_EXISTS
)
{
	DASSERT (str);
	DASSERT (fname);
	TRACE ("LoadSTR(%d,%d) fbname=%s\n", initialize, ignore_no_file, fname);

	if (initialize)
		InitializeSTR (str);
	else
		ResetSTR (str);

	str->data_alloced = true;
	enumError err = OpenReadFILE (
		fname, 0, ignore_no_file, &str->data, &str->data_size, &str->fname, &str->fatt);
	if (err)
		return err;

	// [[analyse-magic]]
	str->fform = GetByMagicFF (str->data, str->data_size, str->data_size);
	switch (str->fform)
	{
		case FF_STATICR:
			if (!CheckSizeSTR (str))
				break;
			str->is_staticr = true;
			return ERR_OK;

		case FF_DOL:
			str->is_dol = true;
			return ERR_OK;

		default:
			break;
	}

	if (!ignore_no_file)
		ERROR0 (ERR_WARNING, "Neither StaticR nor DOL file: %s\n", fname);
	return ERR_WARNING;
}

///////////////////////////////////////////////////////////////////////////////

static bool ReplaceRegionChar (staticr_t *str, char *buf, uint buf_size, ccp fname)
{
	DASSERT (str);
	DASSERT (buf);
	DASSERT (buf_size > 100);

	bool stat = false;
	const char reg_char = GetStrModeRegionChar (str->mode);

	if (reg_char)
	{
		StringCopyS (buf, buf_size, fname);
		char *p = strrchr (buf, '/');
		if (!p)
			p = buf;

		for (; *p; p++)
			if (*p == '@')
			{
				*p = reg_char;
				stat = true;
			}
	}
	return stat;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			dol_sect_info_t			///////////////
///////////////////////////////////////////////////////////////////////////////

static const dol_sect_info_t dol_info_pal[DOL_NN_SECTIONS] = {
	{ 0, "T0", 0x100, 0x80004000, 0x2460, 1, 0, 1, 0,
		{ 0x67, 0xa0, 0xfd, 0x48, 0x8f, 0x9b, 0xe2, 0xb8, 0xa1, 0x35, 0xed, 0x61, 0x48, 0xe7, 0x41,
			0x0b, 0xb6, 0x5b, 0xdf, 0x2f } },
	{ 1, "T1", 0x2560, 0x800072c0, 0x23db20, 1, 0, 1, 0,
		{ 0x1c, 0xbe, 0x24, 0xaf, 0x98, 0x6a, 0x6e, 0x3e, 0xbe, 0xa1, 0x5c, 0xc9, 0x66, 0x49, 0xce,
			0xc7, 0x7d, 0x73, 0x75, 0x7b } },
	{ 2, "T2", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 3, "T3", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 4, "T4", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 5, "T5", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 6, "T6", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 7, "D0", 0x240080, 0x80006460, 0x5c0, 1, 0, 1, 0,
		{ 0xc8, 0xbf, 0xf1, 0x5d, 0x1d, 0x5a, 0x05, 0x77, 0xbc, 0xec, 0x42, 0x05, 0xe7, 0x11, 0x76,
			0x64, 0xa1, 0x87, 0xfd, 0x3d } },
	{ 8, "D1", 0x240640, 0x80006a20, 0x8a0, 1, 0, 1, 0,
		{ 0x6c, 0xe1, 0x67, 0x91, 0x92, 0x92, 0x44, 0x05, 0x12, 0x12, 0x64, 0xff, 0x57, 0x7f, 0xae,
			0x81, 0xe5, 0xa8, 0xf8, 0x7a } },
	{ 9, "D2", 0x240ee0, 0x80244de0, 0xc0, 1, 0, 1, 0,
		{ 0xf6, 0x7f, 0x2b, 0x81, 0x56, 0x65, 0x9d, 0xac, 0xd2, 0x56, 0xb0, 0xcd, 0x6c, 0xca, 0xfd,
			0x72, 0x39, 0xe5, 0x3d, 0x2e } },
	{ 10, "D3", 0x240fa0, 0x80244ea0, 0x20, 1, 0, 1, 0,
		{ 0xeb, 0x28, 0xbc, 0x20, 0x23, 0x0a, 0xce, 0xa4, 0xc9, 0xe0, 0x6d, 0x67, 0xee, 0x25, 0x20,
			0x1c, 0xa1, 0x55, 0x39, 0x7d } },
	{ 11, "D4", 0x240fc0, 0x80244ec0, 0x136c0, 1, 0, 1, 0,
		{ 0x4f, 0xd6, 0xcf, 0x0f, 0x65, 0x76, 0xc1, 0x23, 0x96, 0x86, 0xc8, 0x37, 0x1b, 0x95, 0x93,
			0x9f, 0xba, 0x72, 0xf6, 0xc5 } },
	{ 12, "D5", 0x254680, 0x80258580, 0x4bac0, 1, 0, 1, 0,
		{ 0x4a, 0x9a, 0x76, 0x06, 0x69, 0x35, 0x0b, 0xd2, 0x54, 0xd2, 0xbf, 0x22, 0xac, 0xd9, 0xf9,
			0xc8, 0x13, 0xcf, 0x95, 0xd8 } },
	{ 13, "D6", 0x2a0140, 0x80384c00, 0x13c0, 1, 0, 1, 0,
		{ 0xaa, 0x9a, 0x25, 0x46, 0xb8, 0xd9, 0x57, 0x5e, 0x22, 0x6d, 0x72, 0x2f, 0x71, 0xff, 0x86,
			0x49, 0xc6, 0x12, 0x2d, 0xcf } },
	{ 14, "D7", 0x2a1500, 0x80386fa0, 0x21a0, 1, 0, 1, 0,
		{ 0x43, 0x0d, 0x6f, 0xd3, 0xe0, 0x12, 0x72, 0x2f, 0x44, 0x5b, 0x9e, 0xdc, 0x92, 0x1b, 0xe0,
			0x62, 0x8e, 0x72, 0xe8, 0x07 } },
	{ 15, "D8", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 16, "D9", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 17, "D10", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 18, "BSS", 0, 0x802a4080, 0xe50fc, 1, 0, 0, 0, { 0 } },
	{ 19, "ENT", 0, 0x800060a4, 0, 1, 0, 0, 0, { 0 } },
};

///////////////////////////////////////////////////////////////////////////////

static const dol_sect_info_t dol_info_usa[DOL_NN_SECTIONS] = {
	{ 0, "T0", 0x100, 0x80004000, 0x2460, 1, 0, 1, 0,
		{ 0x5e, 0x16, 0x94, 0x9f, 0x50, 0x5f, 0xc6, 0xc3, 0x6e, 0xca, 0x76, 0x5b, 0x8a, 0x07, 0x6e,
			0x23, 0x68, 0x77, 0x84, 0x67 } },
	{ 1, "T1", 0x2560, 0x800072c0, 0x23da80, 1, 0, 1, 0,
		{ 0x3a, 0x6d, 0x28, 0xc3, 0xdd, 0xc8, 0x10, 0xcc, 0xf0, 0xb2, 0x7f, 0xe7, 0xd8, 0x7d, 0x12,
			0x8f, 0x62, 0x72, 0xd0, 0xed } },
	{ 2, "T2", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 3, "T3", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 4, "T4", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 5, "T5", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 6, "T6", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 7, "D0", 0x23ffe0, 0x80006460, 0x5c0, 1, 0, 1, 0,
		{ 0x47, 0x6d, 0x32, 0x6c, 0x21, 0xaa, 0x65, 0xf2, 0x9e, 0x69, 0xf9, 0xde, 0xd2, 0xaa, 0xb6,
			0xc2, 0x76, 0x94, 0x19, 0x09 } },
	{ 8, "D1", 0x2405a0, 0x80006a20, 0x8a0, 1, 0, 1, 0,
		{ 0x73, 0x5b, 0x90, 0x85, 0x58, 0x13, 0x3f, 0x1e, 0x56, 0x67, 0xeb, 0x61, 0x3a, 0xa9, 0xe9,
			0x13, 0xc4, 0xd3, 0xd5, 0x6f } },
	{ 9, "D2", 0x240e40, 0x80244d40, 0xc0, 1, 0, 1, 0,
		{ 0x4a, 0x87, 0x49, 0xbd, 0xa1, 0x78, 0x9f, 0x8c, 0x9c, 0x9e, 0x25, 0x6a, 0x6b, 0xda, 0x19,
			0x7f, 0x7f, 0xc7, 0xf5, 0x7f } },
	{ 10, "D3", 0x240f00, 0x80244e00, 0x20, 1, 0, 1, 0,
		{ 0xe1, 0x05, 0x64, 0x2d, 0x86, 0x59, 0xc9, 0xac, 0x76, 0xde, 0x8c, 0x84, 0xd0, 0x67, 0xc6,
			0x9a, 0x37, 0x2f, 0xd5, 0xab } },
	{ 11, "D4", 0x240f20, 0x80244e40, 0x13420, 1, 0, 1, 0,
		{ 0xbd, 0x62, 0x9a, 0x5e, 0x1b, 0xda, 0xf5, 0x46, 0xe8, 0x04, 0xcb, 0xa1, 0xfa, 0xca, 0xac,
			0x38, 0xac, 0x7a, 0xe6, 0xaf } },
	{ 12, "D5", 0x254340, 0x80258260, 0x47aa0, 1, 0, 1, 0,
		{ 0x9b, 0x08, 0x11, 0xf2, 0xb5, 0xed, 0x17, 0x69, 0x7e, 0x4b, 0xb3, 0xef, 0xb4, 0x65, 0x23,
			0x78, 0x21, 0x24, 0x27, 0xc1 } },
	{ 13, "D6", 0x29bde0, 0x80380880, 0x13c0, 1, 0, 1, 0,
		{ 0x98, 0x55, 0xd3, 0xa2, 0x15, 0x10, 0xe6, 0xd4, 0x78, 0xa8, 0x40, 0x51, 0xe5, 0x51, 0x77,
			0x04, 0x10, 0x18, 0x29, 0x63 } },
	{ 14, "D7", 0x29d1a0, 0x80382c20, 0x21a0, 1, 0, 1, 0,
		{ 0x9b, 0x2c, 0x27, 0x2b, 0x6b, 0x43, 0xff, 0xce, 0x11, 0x05, 0x92, 0x29, 0x42, 0x26, 0x6f,
			0x9c, 0x59, 0xd6, 0x55, 0xe8 } },
	{ 15, "D8", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 16, "D9", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 17, "D10", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 18, "BSS", 0, 0x8029fd00, 0xe50fc, 1, 0, 0, 0, { 0 } },
	{ 19, "ENT", 0, 0x800060a4, 0, 1, 0, 0, 0, { 0 } },
};

///////////////////////////////////////////////////////////////////////////////

static const dol_sect_info_t dol_info_jap[DOL_NN_SECTIONS] = {
	{ 0, "T0", 0x100, 0x80004000, 0x2460, 1, 0, 1, 0,
		{ 0xa6, 0x86, 0x2e, 0xbc, 0x13, 0xdd, 0x9e, 0x7e, 0xfa, 0xa9, 0x0d, 0x49, 0x19, 0x5f, 0x9b,
			0xe6, 0x30, 0x19, 0x01, 0x4e } },
	{ 1, "T1", 0x2560, 0x800072c0, 0x23da40, 1, 0, 1, 0,
		{ 0x93, 0xb4, 0x6b, 0xbe, 0xab, 0xf5, 0x7d, 0x98, 0x93, 0xf2, 0x16, 0x07, 0x4f, 0xc0, 0x44,
			0x82, 0x01, 0xd1, 0x42, 0x40 } },
	{ 2, "T2", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 3, "T3", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 4, "T4", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 5, "T5", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 6, "T6", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 7, "D0", 0x23ffa0, 0x80006460, 0x5c0, 1, 0, 1, 0,
		{ 0x7f, 0xcd, 0xe8, 0xc8, 0x06, 0xfb, 0x10, 0xbc, 0x81, 0x9c, 0xb8, 0xbb, 0xa0, 0x72, 0x6a,
			0xd8, 0x75, 0xb2, 0xb7, 0x6a } },
	{ 8, "D1", 0x240560, 0x80006a20, 0x8a0, 1, 0, 1, 0,
		{ 0x5a, 0x2c, 0x70, 0x39, 0x11, 0x58, 0xa0, 0x71, 0xc0, 0xb9, 0x09, 0xac, 0x3e, 0xde, 0x55,
			0x64, 0xb0, 0x70, 0xb7, 0x52 } },
	{ 9, "D2", 0x240e00, 0x80244d00, 0xc0, 1, 0, 1, 0,
		{ 0xb4, 0x63, 0xae, 0x32, 0x03, 0x5d, 0xcc, 0xa7, 0xb0, 0x64, 0xee, 0x4a, 0x1c, 0x24, 0xd3,
			0x10, 0x84, 0xd9, 0xf7, 0xe1 } },
	{ 10, "D3", 0x240ec0, 0x80244dc0, 0x20, 1, 0, 1, 0,
		{ 0x40, 0x65, 0xb7, 0x71, 0x65, 0x6e, 0xbc, 0x7f, 0x04, 0x1c, 0xe3, 0x44, 0xb5, 0x32, 0x42,
			0x0d, 0x43, 0x05, 0x76, 0xa8 } },
	{ 11, "D4", 0x240ee0, 0x80244e00, 0x13120, 1, 0, 1, 0,
		{ 0xba, 0x43, 0xcc, 0x17, 0x8b, 0x01, 0x44, 0xc0, 0x5b, 0x38, 0x59, 0x9d, 0xfc, 0x2c, 0x14,
			0xcd, 0xa6, 0x9b, 0xc1, 0x8a } },
	{ 12, "D5", 0x254000, 0x80257f20, 0x4bac0, 1, 0, 1, 0,
		{ 0x16, 0x24, 0x5d, 0xad, 0xb9, 0x7f, 0xe9, 0x5f, 0x00, 0x7f, 0xef, 0x6f, 0xec, 0xf6, 0x24,
			0xe5, 0x35, 0xc1, 0x7c, 0x38 } },
	{ 13, "D6", 0x29fac0, 0x80384580, 0x13c0, 1, 0, 1, 0,
		{ 0xb7, 0x6b, 0x13, 0x92, 0x1c, 0xad, 0x77, 0x78, 0x92, 0xe8, 0xff, 0xe4, 0x5d, 0x45, 0xe6,
			0x1c, 0x06, 0xb8, 0x4d, 0xdb } },
	{ 14, "D7", 0x2a0e80, 0x80386920, 0x21a0, 1, 0, 1, 0,
		{ 0x4d, 0xc4, 0xf5, 0x81, 0x3b, 0x8b, 0x9e, 0x5d, 0xc3, 0x36, 0xfb, 0x91, 0xa9, 0xc0, 0x5b,
			0xd7, 0x59, 0x11, 0xee, 0x25 } },
	{ 15, "D8", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 16, "D9", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 17, "D10", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 18, "BSS", 0, 0x802a3a00, 0xe50fc, 1, 0, 0, 0, { 0 } },
	{ 19, "ENT", 0, 0x800060a4, 0, 1, 0, 0, 0, { 0 } },
};

///////////////////////////////////////////////////////////////////////////////

static const dol_sect_info_t dol_info_kor[DOL_NN_SECTIONS] = {
	{ 0, "T0", 0x100, 0x80004000, 0x2460, 1, 0, 1, 0,
		{ 0x8e, 0x71, 0x21, 0xdf, 0x02, 0x9f, 0xff, 0x15, 0x45, 0x5b, 0xc9, 0x30, 0xdd, 0xda, 0x1a,
			0x84, 0x35, 0x41, 0xe8, 0xc3 } },
	{ 1, "T1", 0x2560, 0x800072c0, 0x23dea0, 1, 0, 1, 0,
		{ 0xfc, 0xf6, 0x28, 0x91, 0x85, 0x47, 0x11, 0xc3, 0x4e, 0xac, 0x2e, 0x61, 0x47, 0x53, 0x5e,
			0xe1, 0x7c, 0x3c, 0xb8, 0xe6 } },
	{ 2, "T2", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 3, "T3", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 4, "T4", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 5, "T5", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 6, "T6", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 7, "D0", 0x240400, 0x80006460, 0x5c0, 1, 0, 1, 0,
		{ 0x2d, 0x62, 0xc1, 0xdb, 0x2a, 0x82, 0x64, 0x0a, 0xa7, 0x61, 0xdd, 0x6d, 0xfc, 0x4a, 0x4d,
			0x59, 0xb4, 0x51, 0x6c, 0x27 } },
	{ 8, "D1", 0x2409c0, 0x80006a20, 0x8a0, 1, 0, 1, 0,
		{ 0x8b, 0x99, 0x7f, 0x81, 0xdf, 0x8b, 0x0e, 0x1d, 0x96, 0x3e, 0xbf, 0xe7, 0xa3, 0xb2, 0x0e,
			0x92, 0x7d, 0xcc, 0x5f, 0x5b } },
	{ 9, "D2", 0x241260, 0x80245160, 0xc0, 1, 0, 1, 0,
		{ 0x7f, 0xcd, 0xfb, 0xdd, 0x3e, 0x2b, 0xf4, 0xf2, 0x35, 0xd0, 0x4a, 0x57, 0xcb, 0x9a, 0xb0,
			0xa2, 0x2a, 0x86, 0xf0, 0xa5 } },
	{ 10, "D3", 0x241320, 0x80245220, 0x20, 1, 0, 1, 0,
		{ 0xcd, 0x27, 0xcf, 0xe9, 0xce, 0x3f, 0x28, 0xbc, 0x64, 0xb3, 0xb2, 0x05, 0x70, 0x10, 0xaf,
			0x9a, 0x5c, 0xe6, 0x04, 0xf7 } },
	{ 11, "D4", 0x241340, 0x80245240, 0x13100, 1, 0, 1, 0,
		{ 0x23, 0x8c, 0xc9, 0x84, 0xd1, 0x07, 0x8a, 0x1b, 0xc4, 0x56, 0x1b, 0x56, 0x30, 0xe0, 0x1a,
			0x4d, 0xbc, 0xa1, 0x48, 0x0c } },
	{ 12, "D5", 0x254440, 0x80258340, 0x39d00, 1, 0, 1, 0,
		{ 0x4a, 0x10, 0xfb, 0x98, 0x57, 0xb0, 0xa5, 0x96, 0xb7, 0xf6, 0x92, 0x06, 0xed, 0xf2, 0xdf,
			0x04, 0x5e, 0x6d, 0x63, 0x55 } },
	{ 13, "D6", 0x28e140, 0x80372c00, 0x13e0, 1, 0, 1, 0,
		{ 0xad, 0x8f, 0x5f, 0x3d, 0xf0, 0x9d, 0xa5, 0x26, 0x9f, 0x71, 0xfb, 0xeb, 0x7b, 0x68, 0x99,
			0x72, 0x71, 0xdc, 0x65, 0xcc } },
	{ 14, "D7", 0x28f520, 0x80374fc0, 0x21a0, 1, 0, 1, 0,
		{ 0xc3, 0x04, 0xfc, 0x7f, 0xf8, 0x6f, 0xf0, 0x39, 0xa9, 0xe7, 0xda, 0x78, 0xf3, 0x1b, 0xfa,
			0x41, 0x91, 0x7d, 0xf1, 0x3c } },
	{ 15, "D8", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 16, "D9", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 17, "D10", 0, 0, 0, 0, 0, 0, 0, { 0 } },
	{ 18, "BSS", 0, 0x80292080, 0xe511c, 1, 0, 0, 0, { 0 } },
	{ 19, "ENT", 0, 0x800060a4, 0, 1, 0, 0, 0, { 0 } },
};

///////////////////////////////////////////////////////////////////////////////

const dol_sect_info_t *GetInfoDOL (str_mode_t mode)
{
	switch (mode)
	{
		case STR_M_PAL:
			return dol_info_pal;
		case STR_M_USA:
			return dol_info_usa;
		case STR_M_JAP:
			return dol_info_jap;
		case STR_M_KOR:
			return dol_info_kor;
		default:
			return 0;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool GetSectionDOL (staticr_t *str, // valid pointer
	dol_sect_info_t *ret_info, // valid pointer: returned data
	uint section, // 0 .. DOL_NN_SECTIONS
				  //	(7*TEXT, 11*DATA, BSS, ENTRY )
	bool calc_hash // true: calculate SHA1 hash if data available

)
{
	DASSERT (str);
	DASSERT (ret_info);

	bool stat = GetDolSectionInfo (ret_info, (dol_header_t *)str->data, str->data_size, section);
	if (calc_hash && stat && ret_info->data_valid)
	{
		ret_info->hash_valid = true;
		SHA1 (ret_info->data, ret_info->size, ret_info->hash);
	}
	return stat;
}

///////////////////////////////////////////////////////////////////////////////

const DolSectionMap_t *FindSectionDOL (uint size, u8 *hash)
{
	if (!size || !hash)
		return 0;

	uint beg = 0;
	uint end = N_DOL_SECTION_MAP - 1;

	while (beg <= end)
	{
		const uint idx = (beg + end) / 2;
		const DolSectionMap_t *dsm = DolSectionMap + idx;
		if (size < dsm->size)
			end = idx - 1;
		else if (size > dsm->size)
			beg = idx + 1;
		else
		{
			const int stat = memcmp (hash, dsm->hash, sizeof (dsm->hash));
			if (stat < 0)
				end = idx - 1;
			else if (stat > 0)
				beg = idx + 1;
			else
				return dsm;
		}
	}
	return 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    dol header			///////////////
///////////////////////////////////////////////////////////////////////////////

static const u8 dol_header_bz2[] = {
	// decompressed size: 0x400 = 1024 bytes
	0x00,
	0x00,
	0x04,
	0x00,

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
	0x84,
	0xf7,
	0x10,
	0x70,
	0x00,
	0x00,
	0x22,
	0xff,
	0xff,
	0xef,
	0x71,
	0xfa,
	0x54,
	0x6e,
	0x74,
	0x25,
	0xd0,
	0x4f,
	0x07,
	0xf0,
	0x00,
	0x44,
	0x30,
	0x90,
	0x10,
	0xda,
	0x00,
	0x00,
	0x02,
	0x44,
	0x00,
	0x00,
	0x12,
	0x40,
	0x00,
	0x20,
	0x19,
	0x60,
	0x00,
	0x02,
	0x16,
	0xb0,
	0x01,
	0x3b,
	0x16,
	0x02,
	0x54,
	0xd5,
	0x34,
	0xda,
	0x1a,
	0xa3,
	0xd4,
	0xf4,
	0xd0,
	0x8d,
	0x32,
	0x30,
	0xd0,
	0x46,
	0x41,
	0x8d,
	0x4f,
	0x53,
	0x4c,
	0x46,
	0x02,
	0x33,
	0xd4,
	0x6a,
	0x09,
	0x12,
	0xa9,
	0xfe,
	0xa9,
	0xe9,
	0x26,
	0x9a,
	0x19,
	0x00,
	0xd0,
	0x06,
	0x80,
	0xd0,
	0x19,
	0x31,
	0x00,
	0x00,
	0x00,
	0x25,
	0x4d,
	0x28,
	0xf5,
	0x1a,
	0x68,
	0x00,
	0x1a,
	0x00,
	0x00,
	0x03,
	0x40,
	0x00,
	0x00,
	0x01,
	0xab,
	0x7c,
	0xb4,
	0x09,
	0x0b,
	0x17,
	0xcd,
	0x46,
	0x50,
	0x5d,
	0xb7,
	0xcc,
	0xe2,
	0x0d,
	0x00,
	0xae,
	0x28,
	0x0d,
	0x03,
	0x60,
	0xd0,
	0x39,
	0x61,
	0x8a,
	0x53,
	0x01,
	0x11,
	0xfb,
	0x7a,
	0x01,
	0x52,
	0x48,
	0x41,
	0xf3,
	0x0a,
	0xde,
	0x9e,
	0xa5,
	0x60,
	0xb4,
	0xd1,
	0x9a,
	0x0d,
	0x94,
	0x84,
	0x05,
	0xa6,
	0xd8,
	0x25,
	0x6c,
	0x2c,
	0x58,
	0x99,
	0x99,
	0x8a,
	0xa8,
	0xe2,
	0x93,
	0x35,
	0xaa,
	0x97,
	0x49,
	0xa0,
	0x42,
	0x98,
	0xaa,
	0x66,
	0x01,
	0x1b,
	0x90,
	0x17,
	0xf0,
	0x1c,
	0x58,
	0x2b,
	0x85,
	0xd1,
	0xb6,
	0x17,
	0x9d,
	0x76,
	0x99,
	0x74,
	0x1c,
	0x25,
	0xcc,
	0xd1,
	0x0c,
	0x18,
	0xc2,
	0x61,
	0x28,
	0x04,
	0xd3,
	0x08,
	0x61,
	0x8b,
	0xde,
	0xc6,
	0xfc,
	0xb0,
	0x1d,
	0xa1,
	0x79,
	0xc7,
	0x77,
	0x06,
	0xde,
	0x4d,
	0x01,
	0x88,
	0x10,
	0x01,
	0xc0,
	0x56,
	0x10,
	0x58,
	0x0b,
	0x04,
	0x11,
	0x14,
	0x88,
	0xa4,
	0x3e,
	0x6a,
	0xe0,
	0x07,
	0x00,
	0xab,
	0xc6,
	0x6f,
	0x7b,
	0xf1,
	0xe7,
	0x6e,
	0x86,
	0x42,
	0x0c,
	0x92,
	0x4a,
	0x9c,
	0x21,
	0x29,
	0x22,
	0x5b,
	0x50,
	0x53,
	0x88,
	0x28,
	0x2a,
	0x88,
	0x20,
	0x4b,
	0x26,
	0xc0,
	0xbb,
	0xc2,
	0x20,
	0x18,
	0x80,
	0x88,
	0x1e,
	0xd0,
	0x32,
	0xb7,
	0x9f,
	0xa2,
	0xf5,
	0xe6,
	0x3e,
	0x93,
	0x47,
	0x45,
	0x69,
	0x09,
	0x41,
	0xe0,
	0x7c,
	0x44,
	0x83,
	0x2b,
	0x92,
	0xfc,
	0x3a,
	0xb4,
	0xa2,
	0xb1,
	0x6c,
	0x0a,
	0x0e,
	0x3b,
	0x7d,
	0x68,
	0x38,
	0xef,
	0xf6,
	0xda,
	0x6a,
	0xfc,
	0x6e,
	0xb0,
	0xfd,
	0xe6,
	0xf8,
	0x72,
	0x9e,
	0xe4,
	0xde,
	0x51,
	0x85,
	0x92,
	0x6e,
	0x2d,
	0x0a,
	0x80,
	0xf8,
	0x73,
	0x25,
	0x53,
	0x6f,
	0x49,
	0x0c,
	0xf8,
	0xe2,
	0x85,
	0xa1,
	0x04,
	0x70,
	0x13,
	0xbd,
	0xd0,
	0x7a,
	0x75,
	0x23,
	0xe7,
	0xf8,
	0xbb,
	0x92,
	0x29,
	0xc2,
	0x84,
	0x84,
	0x27,
	0xb8,
	0x83,
	0x80,
	// 0x168 = 360 data bytes (36%)
};

static BZ2Manager_t dol_header_mgr = { dol_header_bz2, sizeof (dol_header_bz2), 0, 0 };

//-----------------------------------------------------------------------------

const dol_header_t *GetDolHeader (str_mode_t mode)
{
	if (mode < STR_M_PAL || mode >= STR_M__N)
		return 0;

	DecodeBZIP2Manager (&dol_header_mgr);
	return (dol_header_t *)dol_header_mgr.data + (mode - STR_M_PAL);
}

//-----------------------------------------------------------------------------

u32 GetDolOffsetByAddrM (str_mode_t mode, // region mode
	u32 addr, // address to search
	u32 size, // >0: wanted size
	u32 *valid_size // not NULL: return valid size
)
{
	const dol_header_t *dh = GetDolHeader (mode);
	return dh ? GetDolOffsetByAddr (dh, addr, size, valid_size) : 0;
}

//-----------------------------------------------------------------------------

u32 GetDolAddrByOffsetM (str_mode_t mode, // region mode
	u32 off, // offset to search
	u32 size, // >0: wanted size
	u32 *valid_size // not NULL: return valid size
)
{
	const dol_header_t *dh = GetDolHeader (mode);
	return dh ? GetDolAddrByOffset (dh, off, size, valid_size) : 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ct-code dol extensions		///////////////
///////////////////////////////////////////////////////////////////////////////
// [[ct_dol_ext_t]]

typedef struct ct_dol_ext_t
{
	u32 t2_addr;
	BZ2Source_t t2_src;

	u32 d8_addr;
	BZ2Source_t d8_src;
} ct_dol_ext_t;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

ct_dol_ext_t ct_dol_ext_pal = {
	0x802a5000,
	{ "rmcp/boot_code.bin;boot_code.bin", ctcode_boot_code_pal_bz2,
		sizeof (ctcode_boot_code_pal_bz2), FF_CT0_CODE },

	0x80002800,
	{ "rmcp/boot_data.bin;boot_data.bin", ctcode_boot_data_pal_bz2,
		sizeof (ctcode_boot_data_pal_bz2), FF_CT0_DATA },
};

//-----------------------------------------------------------------------------

ct_dol_ext_t ct_dol_ext_usa = {
	0x802a5000,
	{ "rmce/boot_code.bin;boot_code.bin", ctcode_boot_code_usa_bz2,
		sizeof (ctcode_boot_code_usa_bz2), FF_CT0_CODE },

	0x80002800,
	{ "rmce/boot_data.bin;boot_data.bin", ctcode_boot_data_usa_bz2,
		sizeof (ctcode_boot_data_usa_bz2), FF_CT0_DATA },
};

//-----------------------------------------------------------------------------

ct_dol_ext_t ct_dol_ext_jap = {
	0x802a5000,
	{ "rmcj/boot_code.bin;boot_code.bin", ctcode_boot_code_jap_bz2,
		sizeof (ctcode_boot_code_jap_bz2), FF_CT0_CODE },

	0x80002800,
	{ "rmcj/boot_data.bin;boot_data.bin", ctcode_boot_data_jap_bz2,
		sizeof (ctcode_boot_data_jap_bz2), FF_CT0_DATA },
};

//-----------------------------------------------------------------------------

ct_dol_ext_t ct_dol_ext_kor = {
	0x802a5000,
	{ "rmck/boot_code.bin;boot_code.bin", 0, 0, // [[ct-korea]]
		FF_CT0_CODE },

	0x80002800,
	{ "rmck/boot_data.bin;boot_data.bin", 0, 0, // [[ct-korea]]
		FF_CT0_DATA },
};

//-----------------------------------------------------------------------------

ct_dol_ext_t *GetCtExtDOL (str_mode_t mode)
{
	switch (mode)
	{
		case STR_M_PAL:
			return &ct_dol_ext_pal;
		case STR_M_USA:
			return &ct_dol_ext_usa;
		case STR_M_JAP:
			return &ct_dol_ext_jap;
		case STR_M_KOR:
			return &ct_dol_ext_kor;
		default:
			return 0;
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			 PatchHTTPS()			///////////////
///////////////////////////////////////////////////////////////////////////////

static uint PatchHTTPS_HTTP (staticr_t *str, // valid pointer
	https_mode_t mode)
{
	DASSERT (str);
	char buf[200], domain_buf[100];

	static const char nwn[] = "nintendowifi.net";
	static uint nwn_len = sizeof (nwn) - 1;

	const bool mode_sake = mode == HTTPS_SAKE0 || mode == HTTPS_SAKE1;
	const bool repl_domain = mode == HTTPS_DOMAIN || mode_sake;

	if (repl_domain && strlen (wifi_domain) > nwn_len)
	{
		ERROR0 (ERR_SEMANTIC,
			"Domain must be smaller than %u characters if using --https=DOMAIN: %s", nwn_len + 1,
			wifi_domain);
		opt_https = mode = HTTPS_HTTP; // fall back
	}

	uint count = 0;
	const str_server_patch_t *pat;
	const str_flags_t dol_flag = str->is_dol ? STR_F_IS_DOL : 0;

	for (pat = ServerPatch; pat->mode; pat++)
	{
		// dirty code: pat->flags is either 0(srel), 1(mdol), 2(mdol+special)
		if (pat->mode != str->mode || (pat->flags & STR_F_IS_DOL) != dol_flag)
			continue;
		if (pat->flags & STR_F_IS_OPTIONAL && opt_wc24)
			continue;

		const str_server_list_t *sl = ServerList + pat->serv_type;
		memset (buf, 0, sizeof (buf));

		if (pat->flags & STR_F_RESTORE_HTTPS)
		{
			strncpy (buf, "https://", sizeof (buf));
		}
		else if (pat->flags & STR_F_IS_SPECIAL && wifi_domain_is_wiimmfi)
		{
			switch (pat->serv_type)
			{
				case SERV_NASW:
					snprintf (buf, sizeof (buf), "http://naswii.%s/ac", wifi_domain);
					break;

				case SERV_NASW_DEV:
					snprintf (buf, sizeof (buf), "https://main.nas.%s/p%c", wifi_domain,
						tolower ((int)VersusPointsInfo[pat->mode].region_code));
					break;

				case SERV_NASW_TEST:
					snprintf (buf, sizeof (buf), "http://ca.nas.%s/ca", wifi_domain);
					break;

				default:
					ASSERT (0);
					break;
			}
			PRINT ("STR_F_IS_SPECIAL: %u %s\n", pat->serv_type, buf);
		}
		else if (mode_sake && pat->serv_type == SERV_SAKE)
		{
			// special case for SAKE servers

			if (!pat->is_http)
				snprintf (buf, sizeof (buf), "gs.%s%s", nwn, pat->param);
			else
			{
				snprintf (buf, sizeof (buf), "%s://%s%s",
					mode == HTTPS_SAKE0 && pat->is_http == 2 ? "https" : "http", sl->domain,
					pat->param);
			}
		}
		else if (pat->serv_type == SERV_NONE)
		{
			DASSERT (pat->is_http == 2);
			char *dest = StringCopyS (buf, sizeof (buf), mode == HTTPS_RESTORE ? "https" : "http");
			if (pat->url_len == 8)
				StringCopyE (dest, buf + sizeof (buf), "://");
		}
		else if (!pat->is_http)
		{
			snprintf (buf, sizeof (buf), "gs.%s%s", repl_domain ? wifi_domain : nwn, pat->param);
		}
		else if (repl_domain)
		{
			snprintf (buf, sizeof (buf), "http://%.*s%s%s", sl->domain_len - nwn_len, sl->domain,
				wifi_domain, pat->param);
		}
		else
		{
			ccp domain = sl->domain;
			if (mode == HTTPS_HTTP && !memcmp (domain, "naswii.", 7))
			{
				snprintf (domain_buf, sizeof (domain_buf), "nas.%s", domain + 7);
				domain = domain_buf;
			}

			snprintf (buf, sizeof (buf), "%s://%s%s",
				mode == HTTPS_RESTORE && pat->is_http == 2 ? "https" : "http", domain, pat->param);
		}

		char *data = (char *)str->data + pat->offset;
		if (memcmp (data, buf, pat->url_len + 1))
		{
			// HexDump16(stderr,0,0,data,pat->url_len+1);
			// HexDump16(stderr,0,0,buf,pat->url_len+1);
			memcpy (data, buf, pat->url_len + 1);
			count++;
		}
	}
	return count;
}

///////////////////////////////////////////////////////////////////////////////

static uint PatchHTTPS (staticr_t *str // valid pointer
)
{
	switch (opt_https)
	{
		case HTTPS_RESTORE:
		case HTTPS_HTTP:
		case HTTPS_DOMAIN:
		case HTTPS_SAKE0:
		case HTTPS_SAKE1:
			return PatchHTTPS_HTTP (str, opt_https);

		case HTTPS_NONE:
		case HTTPS__N:
			break;
	}
	return 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			address porting			///////////////
///////////////////////////////////////////////////////////////////////////////

const ccp addr_size_flags_info[] = {
	"verified", // ASM_VERIFIED
	"extension from verified", // ASM_EXPANDED
	"not sure", // ASM_UNSURE
	"suspect to be wrong", // ASM_SUSPECT
};

///////////////////////////////////////////////////////////////////////////////

ccp GetAddressSizeColor (const ColorSet_t *cs, addr_size_mode_t asmode)
{
	if (cs)
	{
		switch (asmode & ASM_M_MODE)
		{
			case 0:
				return cs->green;
			case 1:
				return cs->cyan;
			case 2:
				return cs->yellow;
			case 3:
				return cs->red;
		}
	}
	return EmptyString;
}

///////////////////////////////////////////////////////////////////////////////

const addr_port_t *GetPortingRecordPAL (u32 addr)
{
	SetupAddrPortDB ();

	int beg = 0;
	int end = addr_port_records - 1;
	const int region = STR_M_PAL - STR_ZBI_FIRST;

	while (beg <= end)
	{
		const uint idx = (beg + end) / 2;
		const addr_port_t *p = addr_port + idx;
		if (addr < p->addr[region])
			end = idx - 1;
		else if (addr >= p->addr[region] + (p->size2 & ASM_M_SIZE))
			beg = idx + 1;
		else
			return p;
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

static int sort_addr_port_usa (const addr_port_t **a, const addr_port_t **b)
{
	return (int)(*a)->addr[STR_M_USA - STR_ZBI_FIRST] - (int)(*b)->addr[STR_M_USA - STR_ZBI_FIRST];
}

static int sort_addr_port_jap (const addr_port_t **a, const addr_port_t **b)
{
	return (int)(*a)->addr[STR_M_JAP - STR_ZBI_FIRST] - (int)(*b)->addr[STR_M_JAP - STR_ZBI_FIRST];
}

static int sort_addr_port_kor (const addr_port_t **a, const addr_port_t **b)
{
	return (int)(*a)->addr[STR_M_KOR - STR_ZBI_FIRST] - (int)(*b)->addr[STR_M_KOR - STR_ZBI_FIRST];
}

///////////////////////////////////////////////////////////////////////////////

const addr_port_t *GetPortingRecord (str_mode_t mode, u32 addr)
{
	SetupAddrPortDB ();

	static const addr_port_t **usa = 0;
	static const addr_port_t **jap = 0;
	static const addr_port_t **kor = 0;

	const addr_port_t ***list_ptr = 0;
	qsort_func func = 0;

	switch (mode)
	{
		case STR_M_PAL:
			return GetPortingRecordPAL (addr);
		case STR_M_USA:
			list_ptr = &usa;
			func = (qsort_func)sort_addr_port_usa;
			break;
		case STR_M_JAP:
			list_ptr = &jap;
			func = (qsort_func)sort_addr_port_jap;
			break;
		case STR_M_KOR:
			list_ptr = &kor;
			func = (qsort_func)sort_addr_port_kor;
			break;
		default:
			return 0;
	}

	if (!*list_ptr)
	{
		u_nsec_t start = GetTimerNSec ();
		*list_ptr = MALLOC (sizeof (**list_ptr) * addr_port_records);
		const addr_port_t *src = addr_port;
		const addr_port_t **dest = *list_ptr;
		for (int i = 0; i < addr_port_records; i++)
			*dest++ = src++;
		qsort (*list_ptr, addr_port_records, sizeof (**list_ptr), func);
		if (logging >= 3)
			fprintf (stdlog, "# Porting list for %s in %.1f µsec calculated.\n",
				GetStrModeName (mode), (GetTimerNSec () - start) * 1e-3);

#if 0
	const addr_port_t **base = *list_ptr;
	for ( int i = 0; i < 4; i++ )
	{
	    const addr_port_t *p = base[i];
	    printf (">>> %8x %8x %8x %8x : %6x %6x\n",
		p->addr[0], p->addr[1], p->addr[2], p->addr[3], p->size1, p->size2 );
	}
#endif
	}

	int beg = 0;
	int end = addr_port_records - 1;
	const int region = mode - STR_ZBI_FIRST;
	const addr_port_t **base = *list_ptr;

#if 1
	while (beg <= end)
	{
		const uint idx = (beg + end) / 2;
		const addr_port_t *p = base[idx];
		const u32 raddr = p->addr[region];

		if (addr < raddr)
			end = idx - 1;
		else if (addr >= raddr + (p->size2 & ASM_M_SIZE))
			beg = idx + 1;
		else
			return p;
	}
#else
	while (beg <= end)
	{
		const uint idx = (beg + end) / 2;
		const addr_port_t *p = base[idx];
		const u32 raddr = p->addr[region];

		if (addr < raddr)
			end = idx - 1;
		else if (addr > raddr)
			beg = idx + 1;
		else
			break;
	}

	const addr_port_t *p = base[beg];
	PRINT0 ("%u/%u: %08x + %06x + %06x, %08x\n", beg, addr_port_records, p->addr[region], p->size1,
		p->size2, (p[1]).addr[region]);
	return p;
#endif
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

addr_type_t GetAddressType (str_mode_t mode, u32 addr)
{
	if (!addr)
		return ADDRT_NULL;

	if (IsMirroredAddress (addr))
		return GetAddressType (mode, addr - MIRRORED_ADDRESS_DELTA)
			+ (ADDRT_MIRROR_BEGIN - ADDRT_MEMORY_BEGIN);

	const dol_sect_info_t *dsi = GetInfoDOL (mode);
	if (dsi)
	{
		for (int sect = 0; sect <= DOL_IDX_BSS; sect++, dsi++)
			if (addr >= dsi->addr && addr < dsi->addr + dsi->size)
				return sect + ADDRT_DOL;
	}

	const rel_info_t *ri = GetInfoREL (mode);
	if (ri)
	{
		const dol_sect_info_t *dsi = ri->sect;
		for (int sect = 0; sect < REL_SECT__N; sect++, dsi++)
			if (addr >= dsi->addr && addr < dsi->addr + dsi->size)
				return sect + ADDRT_REL_SECT;
	}

	//----------------------------------------------------------------
	//      mountpoint  file size  fix size  end of rel  end of fixed
	//----------------------------------------------------------------
	// PAL	0x805102e0  0x4ad3c4   0x3cd104  0x809bd6a4  0x808dd3e4
	// USA	0x8050bf60  0x4acf94   0x3ccd2c  0x809b8ef4  0x808d8c8c
	// JAP	0x8050fc60  0x4acabc   0x3cc8d4  0x809bc71c  0x808dc534
	// KOR	0x804fe300  0x4ad9fc   0x3cd57c  0x809abcfc  0x808cb87c
	//----------------------------------------------------------------

	struct data_t
	{
		u32 rel1; // first REL address
		u32 rel2; // last REL address+1
	};

	static const struct data_t pal = { 0x805102e0, 0x808dd3e4 };
	static const struct data_t usa = { 0x8050bf60, 0x808d8c8c };
	static const struct data_t jap = { 0x8050fc60, 0x808dc534 };
	static const struct data_t kor = { 0x804fe300, 0x808cb87c };

	const struct data_t *data;
	switch (mode)
	{
		case STR_M_PAL:
			data = &pal;
			break;
		case STR_M_USA:
			data = &usa;
			break;
		case STR_M_JAP:
			data = &jap;
			break;
		case STR_M_KOR:
			data = &kor;
			break;
		default:
			return ADDRT_INVALID;
	}

	if (addr >= data->rel1 && addr < data->rel2)
		return ADDRT_REL;

	struct mem_layout_t
	{
		u32 begin;
		u32 end;
		u32 mode;
	};

	static const struct mem_layout_t tab[] = { { 0x80000000, 0x80004000, ADDRT_HEAD },
		{ 0x80000000, 0x81800000, ADDRT_MEM1 }, { 0x90000000, 0x94000000, ADDRT_MEM2 },
		{ 0xcd000000, 0xcd008000, ADDRT_GPU }, { 0, 0, 0 } };

	for (const struct mem_layout_t *ptr = tab; ptr->end; ptr++)
		if (addr >= ptr->begin && addr < ptr->end)
			return ptr->mode;

	return ADDRT_INVALID;
}

///////////////////////////////////////////////////////////////////////////////

ccp GetAddressTypeName (addr_type_t atype)
{
	if (IsMirroredAddressType (atype))
	{
		ccp res = GetAddressTypeName (atype - (ADDRT_MIRROR_BEGIN - ADDRT_MEMORY_BEGIN));
		const uint size = strlen (res) + 1;
		char *buf = GetCircBuf (size);
		StringLowerS (buf, size, res);
		return buf;
	}

	if (IsAddrTypeDOL (atype))
		return dol_section_name[atype - ADDRT_DOL];

	if (IsAddrTypeREL (atype))
		return rel_section_name[atype - ADDRT_REL_SECT];

	switch (atype)
	{
		case ADDRT_NULL:
			return "null";
		case ADDRT_INVALID:
			return "invalid";
		case ADDRT_HEAD:
			return "HEAD";
		case ADDRT_MEM1:
			return "MEM1";
		case ADDRT_MEM2:
			return "MEM2";
		case ADDRT_GPU:
			return "GPU";
		default:
			return PrintCircBuf ("?%02u?", atype);
	}
}

///////////////////////////////////////////////////////////////////////////////

ccp GetAddressTypeColor (const ColorSet_t *col, addr_type_t atype)
{
	if (!col || !col->colorize)
		return EmptyString;

	if (IsMirroredAddressType (atype))
		atype -= ADDRT_MIRROR_BEGIN - ADDRT_MEMORY_BEGIN;

	if (IsAddrTypeDOL (atype))
		return atype < ADDRT_DOL + DOL_N_TEXT_SECTIONS ? col->b_green : col->b_yellow;

	if (IsAddrTypeREL (atype))
		return col->b_cyan;

	switch (atype)
	{
		case ADDRT_BSS:
			return col->b_magenta_blue;
		case ADDRT_NULL:
			return EmptyString;
		case ADDRT_INVALID:
			return col->b_orange;

		case ADDRT_HEAD:
		case ADDRT_MEM1:
			return col->b_blue_cyan;
		case ADDRT_MEM2:
			return col->b_blue;
		case ADDRT_GPU:
			return col->yellow;

		default:
			return col->error;
	}
}

///////////////////////////////////////////////////////////////////////////////

const dol_sect_info_t *GetAddressTypeSection (str_mode_t mode, addr_type_t atype)
{
	if (IsMirroredAddressType (atype))
		atype -= ADDRT_MIRROR_BEGIN - ADDRT_MEMORY_BEGIN;

	if (IsAddrTypeDOL (atype))
	{
		const dol_sect_info_t *di = GetInfoDOL (mode);
		return di ? di + (atype - ADDRT_DOL) : 0;
	}

	if (IsAddrTypeRELSect (atype))
	{
		const rel_info_t *ri = GetInfoREL (mode);
		return ri ? ri->sect + (atype - ADDRT_REL_SECT) : 0;
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////////

int GetOffsetByAddrM (str_mode_t mode, addr_type_t atype, u32 addr)
{
	// if !atype: Calculate it by 'addr'
	// return -1 if invalid

	if (!atype)
		atype = GetAddressType (mode, addr);

	if (IsAddrTypeDOL (atype))
	{
		const u32 aoff = GetDolOffsetByAddrM (mode, addr, 0, 0);
		return aoff ? aoff : -1;
	}

	if (IsAddrTypeREL (atype))
		return GetRelOffsetByAddrM (mode, addr, 0, 0);

	return -1;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			main.dol support		///////////////
///////////////////////////////////////////////////////////////////////////////

void AnalyzeDOL (staticr_t *str, // valid pointer
	bool restore // true: restore patched data
)
{
	DASSERT (str);
	str->dol_status = DOL_S_ANALYZED;

	if (PatchHTTPS_HTTP (str, HTTPS_RESTORE))
		str->dol_status |= DOL_S_HTTPS;

	static str_mode_t tab_mode[] = { STR_M_PAL, STR_M_USA, STR_M_JAP, STR_M_KOR, STR_M_UNKNOWN };

	static sha1_hash_t tab_sha1_mode[] = {
		// STR_M_PAL
		{ 0x12, 0xb0, 0x2a, 0xf1, 0x9e, 0x07, 0xd3, 0x6d, 0x8a, 0x38, 0xc9, 0xa7, 0xfe, 0x89, 0x15,
			0x55, 0x2f, 0x2e, 0x58, 0x5f },

		// STR_M_USA
		{ 0x0a, 0xed, 0x61, 0x96, 0x28, 0x11, 0xa1, 0x64, 0xf5, 0x8c, 0xd7, 0xed, 0xfc, 0x71, 0x6e,
			0xeb, 0x05, 0xb8, 0x4d, 0xab },

		// STR_M_JAP
		{ 0x69, 0xed, 0xa1, 0xa4, 0x05, 0xde, 0x0e, 0xe1, 0x29, 0x09, 0x15, 0x4f, 0xd5, 0xdc, 0x2c,
			0x33, 0x62, 0x9f, 0x6f, 0x37 },

		// STR_M_KOR
		{ 0x86, 0x2a, 0x38, 0xf2, 0xd7, 0x56, 0xe5, 0x96, 0x2c, 0xa1, 0x9f, 0xd0, 0x62, 0xd0, 0xbd,
			0x55, 0x67, 0x10, 0xed, 0x7e },
	};

	if (str->data_size < 0x1100)
		return;

	sha1_hash_t hash;
	SHA1 (str->data + 0x100, 0x1000, hash);

	uint i;
	for (i = 0; tab_mode[i] != STR_M_UNKNOWN; i++)
	{
		if (!memcmp (tab_sha1_mode + i, hash, sizeof (hash)))
		{
			str->is_main = true;
			str->mode = tab_mode[i];
			str->dol_status |= DOL_S_KNOWN;

			SHA1 (str->data, str->data_size, hash);
			const DolSectionMap_t *dsm = FindSectionDOL (str->data_size, hash);
			if (dsm && dsm->mode == str->mode && dsm->dol_stat)
			{
				str->dol_status |= dsm->dol_stat;
				str->dol_info = dsm->info;
				if (str->dol_status & DOL_S_WIIMMFI)
					str->dol_status &= ~DOL_S_HTTPS;
				else if (str->dol_status & DOL_S_HTTPS)
					str->dol_status &= ~DOL_S_ORIG;
			}
			break;
		}
	}
	str->dol_info_flags = (str->dol_info_flags & ~DIF_MODE) | (str->mode - STR_M_PAL & DIF_MODE);

	//-- check VBI

	const u32 vbi_addr = GetVbiAddress (str);
	if (vbi_addr)
	{
		dol_header_t *dh = (dol_header_t *)str->data;
		const u32 offset = GetDolOffsetByAddr (dh, vbi_addr, 4, 0);
		if (offset)
		{
			const u32 cur_value = be32 (str->data + offset);
			if (cur_value != ORIG_VBI_VALUE)
			{
				if (restore)
					write_be32 (str->data + offset, ORIG_VBI_VALUE);
				str->dol_status = str->dol_status & ~DOL_S_ORIG | DOL_S_VBI;

				if ((cur_value & 0xfc000000) == 0x48000000)
				{
					str->vbi_address = cur_value & 0x03ffffff;
					if (str->vbi_address & 0x02000000)
						str->vbi_address |= 0xfc000000;
					str->vbi_address += vbi_addr;
				}
			}
		}
	}

	//-- check patched_by VERSION

	char *id_ptr = GetIdPointer (str);
	if (id_ptr)
	{
		char *id_end = id_ptr + PATCHED_BY_SIZE;
		char *end = id_ptr;
		while (end < id_end && (uchar)*end >= ' ')
			end++;
		while (end > id_ptr && (end[-1] == '*' || end[-1] == ' '))
			end--;

		memset (str->patched_by, 0, sizeof (str->patched_by));
		if (id_ptr < end)
			memcpy (str->patched_by, id_ptr, end - id_ptr);
		if (restore)
			memset (id_ptr, '*', PATCHED_BY_SIZE);
	}

	//-- check sdkver VERSION

	const u32 vers_addr = GetSdkverAddress (str->mode);
	if (vbi_addr)
	{
		dol_header_t *dh = (dol_header_t *)str->data;
		const u32 offset = GetDolOffsetByAddr (dh, vers_addr, 8, 0);
		if (offset && memcmp (str->data + offset, ORIG_SDK_VERS, 8))
		{
			memcpy (str->sdk_version, str->data + offset, 8);
			str->dol_status = str->dol_status & ~DOL_S_ORIG | DOL_S_SDKVER;
			if (restore)
				memcpy (str->data + offset, ORIG_SDK_VERS, 8);
		}
	}

	//-- check second host

	u32 h2_orig, *h2 = GetSecondHostPointer (str, &h2_orig);
	if (h2)
	{
		if (ntohl (*h2) == ASM_NOP)
		{
			str->dol_status = str->dol_status & ~DOL_S_ORIG | DOL_S_2HOST;
			if (restore)
				*h2 = htonl (h2_orig);
		}
	}

	//-- finish

	if (!str->dol_info_flags_set)
	{
		str->dol_info_flags_set = true;
		if (str->dol_status & DOL_S_ORIG)
			str->dol_info_flags |= DIF_ORIG;
	}

#if defined(TEST) && 0
	if (vbi_addr)
	{
		dol_header_t *dh = (dol_header_t *)str->data;
		const u32 offset = GetDolOffsetByAddr (dh, vbi_addr, 4, 0);
		if (offset)
			fprintf (stderr, "#VBI: %08x: %08x %08x %08x\n", vbi_addr - 0x18,
				be32 (str->data + offset - 24), be32 (str->data + offset - 4),
				be32 (str->data + offset));
	}
#endif
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void PrintStatusDOL (FILE *f, // destination file
	int indent, // indention
	staticr_t *str, // valid pointer
	int long_mode // >0: print more track details if changed
)
{
	indent = NormalizeIndent (indent);
	if (!(str->dol_status & DOL_S_ANALYZED))
		AnalyzeDOL (str, true);

#ifdef TEST
	if (opt_new > 0)
	{
		u32 off1 = GetVbiAddress (str);
		u32 off2 = FindDolAddressOfVBI (str->data, str->data_size);
		printf ("VBI: %08x %c= %08x : %s\n", off1, off1 == off2 ? '=' : '!', off2, str->fname);
	}
#endif

	switch (str->mode)
	{
		case STR_M_PAL:
			fprintf (f, "%*sPAL version of main.dol found.\n", indent, "");
			break;

		case STR_M_USA:
			fprintf (f, "%*sNTSC/USA version of main.dol found.\n", indent, "");
			break;

		case STR_M_JAP:
			fprintf (f, "%*sNTSC/JAP version of main.dol found.\n", indent, "");
			break;

		case STR_M_KOR:
			fprintf (f, "%*sNTSC/KOREA version of main.dol found.\n", indent, "");
			break;

		default:
			fprintf (f, "%*sUnknown main.dol found.\n", indent, "");
			return;
	}

	if (str->dol_cleaned)
		fprintf (f, "%*s- DOL cleaned before analysis.\n", indent, "");

	if (str->patched_by[0])
		fprintf (f, "%*s- Patched by: %s\n", indent, "", str->patched_by);

	if (str->dol_status & DOL_S_HTTPS)
	{
		fprintf (f, "%*s- Known 'https://' URLs modified.\n", indent, "");
	}
	else if (str->dol_info)
	{
		fprintf (f, "%*s- %s\n", indent, "", str->dol_info);
		return;
	}

	const dol_header_t *dh = (dol_header_t *)str->data;
	if (str->dol_status & DOL_S_VBI)
	{
		if (str->vbi_address)
			fprintf (f, "%*s- VBI hook to address 0x%08x%s found.\n", indent, "", str->vbi_address,
				GetDolSectionAddrInfo (dh, str->vbi_address, 4));
		else
			fprintf (f, "%*s- VBI hook found.\n", indent, "");
	}

#ifdef HAVE_WIIMM_EXT
	if (str->dol_status & DOL_S_SDKVER)
		fprintf (f, "%*s- Value of 'sdkver' changed from '001000' to '%s'.\n", indent, "",
			str->sdk_version);
#endif

	if (str->dol_status & DOL_S_2HOST)
		fprintf (f, "%*s- Suppress second 'Host:' header at login.\n", indent, "");

	const dol_sect_info_t *orig = GetInfoDOL (str->mode);
	if (orig)
	{
		uint sect;
		for (sect = 0; sect < DOL_NN_SECTIONS; sect++, orig++)
		{
			dol_sect_info_t info;
			GetSectionDOL (str, &info, sect, true);
			noPRINT ("%-4s: %d,%d : %8x %8x %8x\n", info.name, info.sect_valid, info.hash_valid,
				info.off, info.addr, info.size);

			if (info.sect_valid)
			{
				bool find_section = info.hash_valid;

				if (!orig->sect_valid)
					fprintf (f, "%*s- Additional section %s.\n", indent, "", orig->name);
				else if (orig->size != info.size)
					fprintf (f, "%*s- Size of section %s changed.\n", indent, "", orig->name);
				else if (orig->hash_valid && memcmp (orig->hash, info.hash, sizeof (info.hash)))
					fprintf (f, "%*s- Section %s modified.\n", indent, "", orig->name);
				else if (orig->addr != info.addr)
				{
					if (sect == DOL_IDX_ENTRY)
						fprintf (f, "%*s- Entry point moved from %08x%s to %08x%s.\n", indent, "",
							orig->addr, GetDolSectionAddrInfo (dh, orig->addr, 4), info.addr,
							GetDolSectionAddrInfo (dh, info.addr, 4));
					else
						fprintf (f, "%*s- Section %s moved.\n", indent, "", orig->name);
				}
				else
					find_section = false;

				if (find_section)
				{
					const DolSectionMap_t *dsm = FindSectionDOL (info.size, info.hash);
					if (dsm)
						printf ("%*s- %s\n", indent + 3, "", dsm->info);
					else if (info.data)
					{
						bool found = false;
						if (info.addr == 0x80001800 && info.size > 0x800)
						{
							if (be64 (info.data + 0x11c) == 0x3c6000d06063c0deull)
							{
								found = true;
								printf ("%*s- Gecko Code Handler (Only)\n", indent + 3, "");
							}
							else if (be64 (info.data + 0x770) == 0x3c6000d06063c0deull)
							{
								ccp attrib = "full";
								const u64 val = be64 (info.data + 0x56C);
								if (val == 0x92f8680090786810ull)
									attrib = "Slot A";
								else if (val == 0x92f8681490786824ull)
									attrib = "Slot B";
								printf ("%*s- Gecko Code Handler (%s).\n", indent + 3, "", attrib);
								found = true;
							}
						}

						if (!found && info.addr == 0x802c0000)
							printf ("%*s- Maybe code loader by wstrt.\n", indent + 3, "");
					}
				}
			}
			else if (orig->sect_valid)
				fprintf (f, "%*s- Section %s removed.\n", indent, "", orig->name);
		}
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			versus points			///////////////
///////////////////////////////////////////////////////////////////////////////

uint GetVersusPointsOffset (str_mode_t mode)
{
	switch (mode)
	{
		case STR_M_PAL:
			return 0x37fd50;
		case STR_M_USA:
			return 0x37fc60;
		case STR_M_JAP:
			return 0x37fa20;
		case STR_M_KOR:
			return 0x380130;
		default:
			return 0;
	}
}

///////////////////////////////////////////////////////////////////////////////
// [[VersusPointsInfo]]

const VersusPointsInfo_t VersusPointsInfo[STR_M__N] = {
	{ STR_M_UNKNOWN, '0', 0, 0 },
	{ STR_M_PAL, 'P', 0x37fd50, 0x06890030 },
	{ STR_M_USA, 'E', 0x37fc60, 0x0688bbc0 },
	{ STR_M_JAP, 'J', 0x37fa20, 0x0688f680 },
	{ STR_M_KOR, 'K', 0x380130, 0x0687e430 },
};

///////////////////////////////////////////////////////////////////////////////

str_mode_t opt_cheat_region = STR_M_UNKNOWN;

int ScanOptCheatRegion (ccp arg)
{
	if (!arg || !*arg)
	{
		opt_cheat_region = STR_M_UNKNOWN;
		return 0;
	}

	static const KeywordTab_t tab[] = { { STR_M_PAL, "EUROPE", "PAL", 0 },
		{ STR_M_PAL, "AUSTRALIA", "OCEANIA", 0 }, { STR_M_USA, "AMERICA", "NTSC", 0 },
		{ STR_M_JAP, "JAPAN", 0, 0 }, { STR_M_KOR, "KOREA", 0, 0 }, { 0, 0, 0, 0 } };

	const KeywordTab_t *cmd = ScanKeyword (0, arg, tab);
	if (cmd)
	{
		opt_cheat_region = cmd->id;
		return 0;
	}

	ERROR0 (ERR_SYNTAX, "Invalid cheat region (option --cheat): '%s'\n", arg);
	return 1;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			analyze helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static void AnalyzeTracks (staticr_t *str, // valid pointer
	u8 *data, // data to analyze and modify
	const u32 *offset_tab, // null terminated table with offsets
	const u32 *ref_tab, // reference table
	uint base_index, // base index of first track
	uint n, // number of values in 'ref_tab'
	int base_status // base status bit
)
{
	DASSERT (str);
	DASSERT (data);
	DASSERT (offset_tab);
	DASSERT (ref_tab);
	DASSERT (n > 0);
	DASSERT (n <= 32);

	const uint cmp_size = n * sizeof (u32);

	uint i;
	u32 be_ref[32];
	for (i = 0; i < n; i++)
		write_be32 (be_ref + i, base_index + ref_tab[i]);
	TRACE_HEXDUMP16 (0, 0, be_ref, cmp_size); // putchar('\n');

	uint ok_count = 0; // number of unmodified records
	uint mod_count = 0; // number of modified records

	u8 *first = 0;
	const u32 *off_ptr;
	for (off_ptr = offset_tab; *off_ptr; off_ptr++)
	{
		DASSERT (*off_ptr < str->data_size);
		u8 *d = data + *off_ptr;
		if (!memcmp (d, be_ref, cmp_size))
		{
			ok_count++;
			continue;
		}
		mod_count++;

		if (!first)
			first = d;
		else if (memcmp (first, d, cmp_size))
			str->str_status |= base_status << STR_S_SHIFT_ORDER_DIFF;

		uint fail_buf[32];
		memset (fail_buf, 0, sizeof (fail_buf));
		for (i = 0; i < n; i++)
		{
			const u32 idx = be32 (d + 4 * i) - base_index;
			if (idx >= n)
			{
				str->str_status |= base_status << STR_S_SHIFT_ORDER_DIFF;
				break;
			}
			if (fail_buf[idx])
			{
				str->str_status |= base_status << STR_S_SHIFT_ORDER_MULTI;
				break;
			}
			fail_buf[idx]++;
		}
		if (i == n)
			str->str_status |= base_status << STR_S_SHIFT_ORDER_ALT;

		TRACE_HEXDUMP16 (0, *off_ptr, d, cmp_size); // putchar('\n');
	}

	if (mod_count && ok_count)
		str->str_status |= base_status << STR_S_SHIFT_ORDER_DIFF;

	//--- normalize

	for (off_ptr = offset_tab; *off_ptr; off_ptr++)
	{
		DASSERT (*off_ptr < str->data_size);
		u8 *d = data + *off_ptr;
		memcpy (d, be_ref, cmp_size);
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static void AnalyzeRegion (staticr_t *str, // valid pointer
	u8 *data // data to analyze and modify
)
{
	DASSERT (str);
	DASSERT (data);

	bool is_battle;
	for (is_battle = false; is_battle <= true; is_battle++)
	{
		region_info_t ri;
		SetupRegionInfo (&ri, str, is_battle);

		uint ok_stat = ri.flag_known;

		const uint offset_idx = str->mode - STR_ZBI_FIRST;
		memset (ri.region, 0, MAX_REGION_PATCH * sizeof (*ri.region));
		uint idx, patch_count = 0, unknown_count = 0;

		for (idx = 0; idx < ri.max_patch; idx++)
		{
			ri.region[idx] = -1;
			const region_patch_t *rp = ri.patch_tab + idx;
			if (!rp->orig)
				break;

			u8 *focus = data + rp->offset[offset_idx];
			u32 val = be32 (focus);
			if (val != rp->orig)
			{
				patch_count++;
				if ((val & 0xffff0000) == rp->patch)
				{
					ri.region[idx] = (u16)val;
					if (ri.region[idx] != ri.region[0])
						ok_stat = ri.flag_differ;
				}
				else
					unknown_count++;
				write_be32 (focus, rp->orig);
			}
		}

		if (unknown_count)
			str->str_status |= ri.flag_unknown;
		else if (patch_count)
			str->str_status |= ok_stat;
	}
}

///////////////////////////////////////////////////////////////////////////////

static void AnalyzeAllRanks (staticr_t *str, // valid pointer
	u8 *data, // data to analyze and modify
	const u32 *offset_tab // null terminated table with offsets
)
{
	DASSERT (str);
	DASSERT (data);
	DASSERT (offset_tab);

	uint i, patch_count = 0, known_count = 0;
	for (i = 0; offset_tab[i]; i++)
	{
		u8 *d = data + offset_tab[i];
		if (memcmp (d, all_ranks_orig, all_ranks_size))
		{
			patch_count++;
			if (memcmp (d, all_ranks_patch1, all_ranks_size)
				|| memcmp (d, all_ranks_patch2, all_ranks_size))
			{
				known_count++;
			}
			memcpy (d, all_ranks_orig, all_ranks_size);
		}
	}

	if (i == known_count)
	{
		str->str_status |= STR_S_ALL_RANKS_KNOWN;
	}
	else if (patch_count)
		str->str_status |= STR_S_ALL_RANKS_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////

static void AnalyzeVsBt (staticr_t *str, // valid pointer
	u8 *data, // data to analyze and modify
	const u32 *offset_tab, // null terminated table with offsets
	ccp name, // 'vs' or 'bt'
	uint flag // flag to set
)
{
	DASSERT (str);
	DASSERT (data);
	DASSERT (offset_tab);

	uint i;
	for (i = 0; offset_tab[i]; i++)
	{
		u8 *d = data + offset_tab[i];
		if (strncmp ((ccp)d, name, 3))
		{
			str->str_status |= flag;
			memcpy (d, name, 2);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

static void AnalyzeVersusPoints (staticr_t *str, // valid pointer
	u8 *data // data to analyze and modify
)
{
	DASSERT (str);
	DASSERT (data);

	const u32 offset = GetVersusPointsOffset (str->mode);
	if (offset)
	{
		data += offset;
		if (memcmp (data, MkwPointsTab_NINTENDO, 12 * 12))
		{
			str->str_status |= STR_S_VERSUS_POINTS;
			memcpy (data, MkwPointsTab_NINTENDO, 12 * 12);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

static void AnalyzeCannon (staticr_t *str, // valid pointer
	u8 *data // data to analyze and modify
)
{
	DASSERT (str);
	DASSERT (data);

	const u32 off = GetCannonOffset (str);
	if (off && !(str->str_status & STR_S_CANNON))
	{
		memcpy (str->cannon_data, data + off, sizeof (str->cannon_data));
		if (memcmp (data + off, CannonDataLEX + 4, sizeof (CannonDataLEX) - 4))
		{
			str->str_status |= STR_S_CANNON;
			memcpy (data + off, CannonDataLEX + 4, sizeof (CannonDataLEX) - 4);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

static void AnalyzeMenO (staticr_t *str, // valid pointer
	u8 *data // data to analyze and modify
)
{
	DASSERT (str);
	DASSERT (data);

	const u32 *optr = GetMenuOffsetTab (str->mode);
	if (optr)
	{
		uint count = 0, total = 0;
		const int *idx;
		for (idx = meno_patch_idx; *idx >= 0; idx++)
		{
			total++;
			u8 *d = data + optr[*idx] + 13; // 13 = 'O' offset
			if (*d == 'O')
			{
				*d = 'u';
				count++;
			}
		}

		if (total == count)
			str->str_status |= STR_S_MENO;
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			analyze staticr			///////////////
///////////////////////////////////////////////////////////////////////////////

static void AnalyzePAL (staticr_t *str // valid pointer
)
{
	DASSERT (str);

	//--- first hash test

	static const u8 good_hash[20] = { 0x88, 0x7b, 0xcc, 0x07, 0x67, 0x81, 0xf5, 0xb0, 0x05, 0xcc,
		0x31, 0x7a, 0x6e, 0x3c, 0xc8, 0xfd, 0x5f, 0x91, 0x13, 0x00 };

	sha1_hash_t hash;
	SHA1 (str->data, str->data_size, hash);
	if (!memcmp (hash, good_hash, sizeof (hash)))
	{
		if (!(str->str_status & STR_S_HTTPS))
			str->str_status |= STR_S_ORIG;
#ifndef TEST
		return;
#endif
	}

	//--- create a copy

	u8 *data = MALLOC (str->data_size);
	memcpy (data, str->data, str->data_size);

	//--- ctgp 4.4 tests

	static const u32 ctgp44[] = { // addr	  orig value  ctgp value
		//-------------------------------
		0x3d22b8, 0x00000db4, 0xff750030, 0x3d22c8, 0x00000db4, 0xff750030, 0x40df78, 0x00012660,
		0xff7742e0, 0x40df88, 0x00012660, 0xff7742e0, 0, 0, 0
	};

	const u32 *ptr;
	uint orig_failed = 0, ctgp_failed = 0;
	for (ptr = ctgp44; *ptr; ptr += 3)
	{
		DASSERT (*ptr + 4 <= str->data_size);
		const u32 val = be32 (str->data + *ptr);
		if (val != ptr[1])
			orig_failed++;
		if (val != ptr[2])
			ctgp_failed++;
		write_be32 (data + *ptr, ptr[1]);
	}

	if (orig_failed)
	{
		str->str_status |= STR_S_CTGP44_LIKE;
		if (!ctgp_failed)
			str->str_status |= STR_S_CTGP44;
	}

	//--- analyse tracks and arenas

	AnalyzeTracks (str, data, pal_track_offset, track_pos_default, MKW_TRACK_BEG, MKW_N_TRACKS,
		STR_S_TRACK_ORDER);
	AnalyzeTracks (str, data, pal_arena_offset, arena_pos_default, MKW_ARENA_BEG, MKW_N_ARENAS,
		STR_S_ARENA_ORDER);

	//--- analyse misc

	AnalyzeRegion (str, data);
	AnalyzeAllRanks (str, data, pal_all_ranks_offset);
	AnalyzeVsBt (str, data, pal_vs_offset, "vs", STR_S_VS);
	AnalyzeVsBt (str, data, pal_bt_offset, "bt", STR_S_BT);
	AnalyzeVersusPoints (str, data);
	AnalyzeCannon (str, data);
	AnalyzeMenO (str, data);

	//--- last hash test

	SHA1 (data, str->data_size, hash);
	if (memcmp (hash, good_hash, sizeof (hash)))
		str->str_status |= STR_S_UNKNOWN_PATCH;

	//--- clean

	FREE (data);
}

///////////////////////////////////////////////////////////////////////////////

static void AnalyzeUSA (staticr_t *str // valid pointer
)
{
	DASSERT (str);

	//--- first hash test

	static const u8 good_hash[20] = { 0x07, 0x2b, 0xa4, 0x43, 0x82, 0xfd, 0xca, 0x9b, 0xf5, 0xc0,
		0xec, 0x9d, 0x1e, 0x9b, 0x92, 0x2c, 0xaf, 0xd9, 0x92, 0x60 };

	sha1_hash_t hash;
	SHA1 (str->data, str->data_size, hash);
	if (!memcmp (hash, good_hash, sizeof (hash)))
	{
		str->str_status |= STR_S_ORIG;
#ifndef TEST
		return;
#endif
	}

	//--- create a copy

	u8 *data = MALLOC (str->data_size);
	memcpy (data, str->data, str->data_size);

	//--- ctgp 4.4 tests

	static const u32 ctgp44[] = { // addr	  orig value  ctgp value
		//-------------------------------
		0x3d1c38, 0x00000c14, 0xff7546e0, 0x3d1c48, 0x00000c14, 0xff7546e0, 0x40cda0, 0x00012098,
		0xff778720, 0x40cdb0, 0x00012098, 0xff778720, 0, 0, 0
	};

	const u32 *ptr;
	uint orig_failed = 0, ctgp_failed = 0;
	for (ptr = ctgp44; *ptr; ptr += 3)
	{
		DASSERT (*ptr + 4 <= str->data_size);
		const u32 val = be32 (str->data + *ptr);
		if (val != ptr[1])
			orig_failed++;
		if (val != ptr[2])
			ctgp_failed++;
		write_be32 (data + *ptr, ptr[1]);
	}

	if (orig_failed)
	{
		str->str_status |= STR_S_CTGP44_LIKE;
		if (!ctgp_failed)
			str->str_status |= STR_S_CTGP44;
	}

	//--- anlyze tracks and arenas

	AnalyzeTracks (str, data, usa_track_offset, track_pos_default, MKW_TRACK_BEG, MKW_N_TRACKS,
		STR_S_TRACK_ORDER);
	AnalyzeTracks (str, data, usa_arena_offset, arena_pos_default, MKW_ARENA_BEG, MKW_N_ARENAS,
		STR_S_ARENA_ORDER);

	//--- analyse misc

	AnalyzeRegion (str, data);
	AnalyzeAllRanks (str, data, usa_all_ranks_offset);
	AnalyzeVsBt (str, data, usa_vs_offset, "vs", STR_S_VS);
	AnalyzeVsBt (str, data, usa_bt_offset, "bt", STR_S_BT);
	AnalyzeVersusPoints (str, data);
	AnalyzeMenO (str, data);

	//--- last hash test

	SHA1 (data, str->data_size, hash);
	if (memcmp (hash, good_hash, sizeof (hash)))
		str->str_status |= STR_S_UNKNOWN_PATCH;

	//--- clean

	FREE (data);
}

///////////////////////////////////////////////////////////////////////////////

static void AnalyzeJAP (staticr_t *str // valid pointer
)
{
	DASSERT (str);

	//--- first hash test

	static const u8 good_hash[20] = { 0x20, 0xb4, 0x16, 0x1d, 0x41, 0x5e, 0x40, 0x92, 0xa6, 0x29,
		0x9b, 0xb2, 0x85, 0xfa, 0xa6, 0x22, 0x83, 0x5b, 0x2b, 0x16 };

	sha1_hash_t hash;
	SHA1 (str->data, str->data_size, hash);
	if (!memcmp (hash, good_hash, sizeof (hash)))
	{
		str->str_status |= STR_S_ORIG;
#ifndef TEST
		return;
#endif
	}

	//--- create a copy

	u8 *data = MALLOC (str->data_size);
	memcpy (data, str->data, str->data_size);

	//--- anlyze tracks and arenas

	AnalyzeTracks (str, data, jap_track_offset, track_pos_default, MKW_TRACK_BEG, MKW_N_TRACKS,
		STR_S_TRACK_ORDER);
	AnalyzeTracks (str, data, jap_arena_offset, arena_pos_default, MKW_ARENA_BEG, MKW_N_ARENAS,
		STR_S_ARENA_ORDER);

	//--- analyse misc

	AnalyzeRegion (str, data);
	AnalyzeAllRanks (str, data, jap_all_ranks_offset);
	AnalyzeVsBt (str, data, jap_vs_offset, "vs", STR_S_VS);
	AnalyzeVsBt (str, data, jap_bt_offset, "bt", STR_S_BT);
	AnalyzeVersusPoints (str, data);
	AnalyzeMenO (str, data);

	//--- last hash test

	SHA1 (data, str->data_size, hash);
	if (memcmp (hash, good_hash, sizeof (hash)))
		str->str_status |= STR_S_UNKNOWN_PATCH;

	//--- clean

	FREE (data);
}

///////////////////////////////////////////////////////////////////////////////

static void AnalyzeKOR (staticr_t *str // valid pointer
)
{
	DASSERT (str);

	//--- first hash test

	static const u8 good_hash[20] = { 0xe0, 0x00, 0x62, 0x50, 0xb1, 0xe5, 0xc1, 0xbd, 0xee, 0x8b,
		0xdb, 0x0e, 0x78, 0x9e, 0xa2, 0x00, 0xb3, 0xa8, 0x46, 0xe4 };

	sha1_hash_t hash;
	SHA1 (str->data, str->data_size, hash);
	if (!memcmp (hash, good_hash, sizeof (hash)))
	{
		str->str_status |= STR_S_ORIG;
#ifndef TEST
		return;
#endif
	}

	//--- create a copy

	u8 *data = MALLOC (str->data_size);
	memcpy (data, str->data, str->data_size);

	//--- anlyze tracks and arenas

	AnalyzeTracks (str, data, kor_track_offset, track_pos_default, MKW_TRACK_BEG, MKW_N_TRACKS,
		STR_S_TRACK_ORDER);
	AnalyzeTracks (str, data, kor_arena_offset, arena_pos_default, MKW_ARENA_BEG, MKW_N_ARENAS,
		STR_S_ARENA_ORDER);

	//--- analyse misc

	AnalyzeRegion (str, data);
	AnalyzeAllRanks (str, data, kor_all_ranks_offset);
	AnalyzeVsBt (str, data, kor_vs_offset, "vs", STR_S_VS);
	AnalyzeVsBt (str, data, kor_bt_offset, "bt", STR_S_BT);
	AnalyzeVersusPoints (str, data);
	AnalyzeMenO (str, data);

	//--- last hash test

	SHA1 (data, str->data_size, hash);
	if (memcmp (hash, good_hash, sizeof (hash)))
		str->str_status |= STR_S_UNKNOWN_PATCH;

	//--- clean

	FREE (data);
}

///////////////////////////////////////////////////////////////////////////////

void AnalyzeSTR (staticr_t *str // valid pointer
)
{
	DASSERT (str);

	if (str->is_dol)
	{
		if (!(str->dol_status & DOL_S_ANALYZED))
			AnalyzeDOL (str, true);
		return;
	}

	if (!CheckSizeSTR (str))
		return;

	str->str_status = STR_S_ANALYZED;
	if (PatchHTTPS_HTTP (str, HTTPS_RESTORE))
		str->str_status |= STR_S_HTTPS;

	switch (str->mode)
	{
		case STR_M_PAL:
			AnalyzePAL (str);
			break;

		case STR_M_USA:
			AnalyzeUSA (str);
			break;

		case STR_M_JAP:
			AnalyzeJAP (str);
			break;

		case STR_M_KOR:
			AnalyzeKOR (str);
			break;

		default:
			break;
	}
}

///////////////////////////////////////////////////////////////////////////////

static void PrintStatusRegion (FILE *f, // destination file
	int indent, // indention
	staticr_t *str, // valid pointer
	bool is_battle, // true: is battle
	int long_mode // >0: print more track details if changed
)
{
	DASSERT (f);
	DASSERT (str);

	region_info_t ri;
	SetupRegionInfo (&ri, str, is_battle);

	if (long_mode > 1 || long_mode > 0 && str->str_status & (ri.flag_unknown | ri.flag_differ))
	{
		fprintf (
			f, "%*s- %c%s region settings:\n", indent, "", toupper ((int)ri.name[0]), ri.name + 1);

		const region_patch_t *rp;
		for (rp = ri.patch_tab; rp->orig; rp++)
		{
			const uint offset = rp->offset[str->mode - STR_ZBI_FIRST];
			const u8 *d = str->data + offset;
			fprintf (
				f, "%*s%9x: %02x %02x %02x %02x : ", indent, "", offset, d[0], d[1], d[2], d[3]);
			const u32 val = be32 (d);
			if (val == rp->orig)
				fprintf (f, "%-23s : %s\n", "not patched", rp->info);
			else if ((val & 0xffff0000) == rp->patch)
				fprintf (f, "region %4x/hex = %5u : %s\n", val & 0xffff, val & 0xffff, rp->info);
			else
				fprintf (f, "%-23s : %s\n", "unknown patch", rp->info);
		}
	}
	else if (str->str_status & ri.flag_differ)
	{
		fprintf (f, "%*s- Known %s region patch found, regions = ", indent, "", ri.name);
		ccp sep = "";
		uint i;
		for (i = 0; i < ri.max_patch; i++)
		{
			if (ri.region[i] < 0)
				fprintf (f, "%sunpatched", sep);
			else
				fprintf (f, "%s%#x=%u", sep, ri.region[i], ri.region[i]);
			sep = ", ";
		}
		fputc ('\n', f);
	}
	else if (str->str_status & ri.flag_known)
		fprintf (f, "%*s- Known %s region patch found, region = 0x%02x = %u\n", indent, "", ri.name,
			ri.region[0], ri.region[0]);
	else if (str->str_status & ri.flag_unknown)
		fprintf (f, "%*s- Unknown %s region patch found.\n", indent, "", ri.name);
}

///////////////////////////////////////////////////////////////////////////////

void PrintStatusSTR (FILE *f, // destination file
	int indent, // indention
	staticr_t *str, // valid pointer
	int long_mode // >0: print more track details if changed
)
{
	DASSERT (f);
	DASSERT (str);

	if (str->is_dol)
	{
		PrintStatusDOL (f, indent, str, long_mode);
		return;
	}

	indent = NormalizeIndent (indent);
	if (!(str->str_status & STR_S_ANALYZED))
		AnalyzeSTR (str);

	switch (str->mode)
	{
		case STR_M_PAL:
			fprintf (f, "%*sPAL version of StaticR found.\n", indent, "");
			break;

		case STR_M_USA:
			fprintf (f, "%*sNTSC/USA version of StaticR found.\n", indent, "");
			break;

		case STR_M_JAP:
			fprintf (f, "%*sNTSC/JAP version of StaticR found.\n", indent, "");
			break;

		case STR_M_KOR:
			fprintf (f, "%*sNTSC/KOREA version of StaticR found.\n", indent, "");
			break;

		default:
			fprintf (f, "%*sNo StaticR found.\n", indent, "");
			break;
	}

	const str_status_t st = str->str_status;

	if (st & STR_S_WRONG_SIZE)
		fprintf (f, "%*s- Wrong file size.\n", indent, "");

	if (st & STR_S_ORIG)
		fprintf (f, "%*s- Original file.\n", indent, "");

	if (st & STR_S_HTTPS)
		fprintf (f, "%*s- Known 'https://' URLs modified.\n", indent, "");

	if (st & STR_S_TRACK_ORDER_ALT)
		fprintf (f, "%*s- Valid alternative track order detected.\n", indent, "");
	if (st & STR_S_TRACK_ORDER_MULTI)
		fprintf (f, "%*s- Multiple usage of singe track detected.\n", indent, "");
	if (st & STR_S_TRACK_ORDER_BAD)
		fprintf (f, "%*s- Bad track index detected.\n", indent, "");
	if (st & STR_S_TRACK_ORDER_DIFF)
		fprintf (f, "%*s- Different track orders detected.\n", indent, "");

	if (long_mode > 1 || long_mode > 0 && st & STR_S_TRACK_ORDER__FAIL)
	{
		fprintf (f, "%*s- Track assigning tables:\n", indent, "");
		const u32 *tab = GetTrackOffsetTab (str->mode);
		DASSERT (tab);
		for (;;)
		{
			u32 offset = *tab++;
			if (!offset)
				break;
			fprintf (f, "%*s%9x:", indent, "", offset);
			const u32 *d = (u32 *)(str->data + offset);
			uint i;
			for (i = 0; i < MKW_N_TRACKS; i++)
			{
				if (!(i % 4))
					fputc (' ', f);
				fprintf (f, "%u%c", be32 (d++), i == MKW_N_TRACKS - 1 ? '\n' : ',');
			}
		}
	}

	if (st & STR_S_ARENA_ORDER_ALT)
		fprintf (f, "%*s- Valid alternative arena order detected.\n", indent, "");
	if (st & STR_S_ARENA_ORDER_MULTI)
		fprintf (f, "%*s- Multiple usage of singe arena detected.\n", indent, "");
	if (st & STR_S_ARENA_ORDER_BAD)
		fprintf (f, "%*s- Bad arena index detected.\n", indent, "");
	if (st & STR_S_ARENA_ORDER_DIFF)
		fprintf (f, "%*s- Different arena orders detected.\n", indent, "");

	if (long_mode > 1 || long_mode > 0 && st & STR_S_ARENA_ORDER__FAIL)
	{
		fprintf (f, "%*s- Arena assigning tables:\n", indent, "");
		const u32 *tab = GetArenaOffsetTab (str->mode);
		DASSERT (tab);
		for (;;)
		{
			u32 offset = *tab++;
			if (!offset)
				break;
			fprintf (f, "%*s%9x:", indent, "", offset);
			const u32 *d = (u32 *)(str->data + offset);
			uint i;
			for (i = 0; i < MKW_N_ARENAS; i++)
			{
				if (!(i % 5))
					fputc (' ', f);
				fprintf (f, "%u%c", be32 (d++), i == MKW_N_ARENAS - 1 ? '\n' : ',');
			}
		}
	}

	if (st & STR_S_CTGP44)
		fprintf (f, "%*s- CTGP 4.4 modification found.\n", indent, "");
	else if (st & STR_S_CTGP44_LIKE)
		fprintf (f, "%*s- CTGP 4.4 like modification found.\n", indent, "");

	PrintStatusRegion (f, indent, str, false, long_mode);
	PrintStatusRegion (f, indent, str, true, long_mode);

	if (st & STR_S_ALL_RANKS_KNOWN)
		fprintf (f, "%*s- Known 'all ranks' patch found.\n", indent, "");
	else if (st & STR_S_ALL_RANKS_UNKNOWN)
		fprintf (f, "%*s- Unknown 'all ranks' patch found.\n", indent, "");

	if (st & STR_S_VS)
	{
		fprintf (f, "%*s- Versus identification ('vs') patched:", indent, "");
		const u32 *ptr;
		for (ptr = GetVsBtOffsetTab (str, false); *ptr; ptr++)
			fprintf (f, " %.2s", str->data + *ptr);
		fputc ('\n', f);
	}

	if (st & STR_S_BT)
	{
		fprintf (f, "%*s- Battle identification ('bt') patched:", indent, "");
		const u32 *ptr;
		for (ptr = GetVsBtOffsetTab (str, true); *ptr; ptr++)
			fprintf (f, " %.2s", str->data + *ptr);
		fputc ('\n', f);
	}

	if (st & STR_S_VERSUS_POINTS)
	{
		const u32 offset = GetVersusPointsOffset (str->mode);
		fprintf (f, "%*s- Versus points modified to '%s'.\n", indent, "",
			GetMkwPointInfo (str->data + offset)->info);
	}

	if (st & STR_S_CANNON)
	{
		fprintf (f, "%*s- Cannon parameters modified.\n", indent, "");
		const u8 *orig = CannonDataLEX + 4;
		const u8 *data = str->cannon_data;
		int idx;
		for (idx = 0; orig < CannonDataLEX + sizeof (CannonDataLEX);
			idx++, orig += CANNON_SIZE, data += CANNON_SIZE)
		{
			if (memcmp (orig, data, CANNON_SIZE))
			{
				printf ("%*s   > Cannon %u orig: %12.4f %12.4f %12.4f %12.4f\n", indent, "", idx,
					bef4 (orig), bef4 (orig + 4), bef4 (orig + 8), bef4 (orig + 12));
				printf ("%*s     Cannon %u data: %12.4f %12.4f %12.4f %12.4f\n", indent, "", idx,
					bef4 (data), bef4 (data + 4), bef4 (data + 8), bef4 (data + 12));
			}
		}
	}

	if (st & STR_S_MENO)
		fprintf (f, "%*s- Known 'MenO' patches found.\n", indent, "");

	if (st & STR_S_UNKNOWN_PATCH)
		fprintf (f, "%*s- Unknown patches found.\n", indent, "");
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			 PatchSTR()			///////////////
///////////////////////////////////////////////////////////////////////////////

static uint PatchTracks (staticr_t *str, // valid pointer
	const u32 *offset_tab, // null terminated table with offsets
	const u32 *track_tab, // table with track indexes
	uint base_index, // base index of first track
	uint n // number of values in 'ref_tab'
)
{
	DASSERT (str);
	DASSERT (offset_tab);
	DASSERT (track_tab);
	DASSERT (n > 0);
	DASSERT (n <= 32);

	uint count = 0;
	const uint copy_size = n * sizeof (u32);

	uint i;
	u32 be_data[32];
	for (i = 0; i < n; i++)
		write_be32 (be_data + i, base_index + track_tab[i]);
	TRACE_HEXDUMP16 (0, 0, be_data, copy_size);

	for (; *offset_tab; offset_tab++)
	{
		DASSERT (*offset_tab < str->data_size);
		u8 *d = str->data + *offset_tab;
		if (memcmp (d, be_data, copy_size))
		{
			count++;
			memcpy (d, be_data, copy_size);
		}
	}

	return count;
}

///////////////////////////////////////////////////////////////////////////////

static uint PatchVsBt (staticr_t *str, // valid pointer
	bool is_bt, // false: VS, true: BT
	enum VsBtMode mode, // mode
	ccp patch // string to set
)
{
	DASSERT (str);
	DASSERT (str);

	uint count = 0;
	const u32 *tab = GetVsBtOffsetTab (str, is_bt);
	switch (mode)
	{
		case VSBT_OFF:
			break;

		case VSBT_PATCH:
			while (*tab)
			{
				u8 *d = str->data + *tab++;
				if (d[0] != patch[0] || d[1] != patch[1])
				{
					*d++ = patch[0];
					*d = patch[1];
					count++;
				}
			}
			break;

		case VSBT_TEST:
		{
			char c0 = is_bt ? 'b' : 'v';
			char c1 = '1';
			while (*tab)
			{
				u8 *d = str->data + *tab++;
				if (d[0] != c0 || d[1] != c1)
				{
					*d++ = c0;
					*d = c1;
					count++;
				}
				c1++;
			}
		}
		break;
	}

	return count;
}

///////////////////////////////////////////////////////////////////////////////
#if 0 // region specific patch functions not needed yet
///////////////////////////////////////////////////////////////////////////////

static uint PatchPAL
(
    // returns: number of patches

    staticr_t		* str		// valid pointer
)
{
    DASSERT(str);
    DASSERT(str->data);
    DASSERT(str->data_size);
    DASSERT( str->mode == STR_M_PAL );

    uint count = 0;

    return count;
}

///////////////////////////////////////////////////////////////////////////////

static uint PatchUSA
(
    // returns: number of patches

    staticr_t		* str		// valid pointer
)
{
    DASSERT(str);
    DASSERT(str->data);
    DASSERT(str->data_size);
    DASSERT( str->mode == STR_M_USA );

    uint count = 0;

    return count;
}

///////////////////////////////////////////////////////////////////////////////

static uint PatchJAP
(
    // returns: number of patches

    staticr_t		* str		// valid pointer
)
{
    DASSERT(str);
    DASSERT(str->data);
    DASSERT(str->data_size);
    DASSERT( str->mode == STR_M_JAP );

    uint count = 0;

    return count;
}

///////////////////////////////////////////////////////////////////////////////

static uint PatchKOR
(
    // returns: number of patches

    staticr_t		* str		// valid pointer
)
{
    DASSERT(str);
    DASSERT(str->data);
    DASSERT(str->data_size);
    DASSERT( str->mode == STR_M_KOR );

    uint count = 0;

    return count;
}

///////////////////////////////////////////////////////////////////////////////
#endif // region specific patch functions not needed yet
///////////////////////////////////////////////////////////////////////////////

static uint PatchRegion (staticr_t *str, // valid pointer
	bool is_battle // true: is battle
)
{
	DASSERT (str);
	region_info_t ri;
	SetupRegionInfo (&ri, str, is_battle);

	uint count = 0;
	u8 temp[20];

	DASSERT (ri.opt_region);
	if (*ri.opt_region == SREG_RESTORE)
	{
		const region_patch_t *rp;
		for (rp = ri.patch_tab; rp->orig; rp++)
		{
			if (rp->patch_mode & RPAT_TEST && !*ri.opt_x)
				continue;

			write_be32 (temp, rp->orig);
			u8 *d = str->data + rp->offset[str->mode - STR_ZBI_FIRST];
			if (memcmp (d, temp, 4))
			{
				memcpy (d, temp, 4);
				count++;
			}
		}
	}
	else if (*ri.opt_region >= 0)
	{
		uint val = *ri.opt_region & 0xffff;
		uint add = *ri.opt_t;

		const region_patch_t *rp;
		for (rp = ri.patch_tab; rp->orig; rp++)
		{
			if (rp->patch_mode & RPAT_TEST && !*ri.opt_x)
				continue;

			write_be32 (temp, rp->patch | (rp->patch_mode & RPAT_FF ? val & 0xff : val));
			val += add;
			u8 *d = str->data + rp->offset[str->mode - STR_ZBI_FIRST];
			if (memcmp (d, temp, 4))
			{
				memcpy (d, temp, 4);
				count++;
			}
		}
	}

	return count;
}

///////////////////////////////////////////////////////////////////////////////

uint PatchSTR (
	// returns: number of patches

	staticr_t *str // valid pointer
)
{
	DASSERT (str);
	if (!str->data_size)
		return 0;
	DASSERT (str->data);

	if (str->is_dol)
		return PatchDOL (str);

	if (!CheckSizeSTR (str))
		return 0;

	uint count = 0;

	//--- patch tracks and arenas

	if (opt_tracks)
	{
		const u32 *tab = GetTrackOffsetTab (str->mode);
		if (tab)
			count += PatchTracks (str, tab, track_pos, MKW_TRACK_BEG, MKW_N_TRACKS);
	}

	if (opt_arenas)
	{
		const u32 *tab = GetArenaOffsetTab (str->mode);
		if (tab)
			count += PatchTracks (str, tab, arena_pos, MKW_ARENA_BEG, MKW_N_ARENAS);
	}

	//--- patch region

	count += PatchRegion (str, false);
	count += PatchRegion (str, true);

	//--- patch all-ranks

	if (opt_all_ranks)
	{
		ccp p1, p2;
		if (opt_all_ranks < 0)
			p1 = p2 = all_ranks_orig;
		else
		{
			p1 = all_ranks_patch1;
			p2 = all_ranks_patch2;
		}

		const u32 *tab = GetAllRanksOffsetTab (str);
		if (tab)
		{
			while (*tab)
			{
				u8 *d = str->data + *tab++;
				if (memcmp (d, p1, all_ranks_size))
				{
					memcpy (d, p1, all_ranks_size);
					count++;
					ccp temp = p1;
					p1 = p2;
					p2 = temp;
				}
			}
		}
	}

	//--- patch vs & bt

	count += PatchVsBt (str, false, patch_vs_mode, patch_vs_str);
	count += PatchVsBt (str, true, patch_bt_mode, patch_bt_str);

	//--- patch versus points

	if (opt_points_used)
	{
		const u32 offset = GetVersusPointsOffset (str->mode);
		if (offset)
		{
			u8 *data = str->data + offset;
			if (memcmp (data, MkwPointsTab, 12 * 12))
			{
				count++;
				memcpy (data, MkwPointsTab, 12 * 12);
			}
		}
	}

	//--- patch cannon params

	u32 offset = GetCannonOffset (str);
	if (offset)
	{
		u8 *data = str->data + offset;
		uint i;
		for (i = 0; i < CANNON_N; i++)
		{
			const cannon_param_t *cp = cannon_param + i;
			if (cp->valid)
			{
				uint j;
				for (j = 0; j < CANNON_N_PARAM; j++, data += sizeof (float))
					write_bef4 (data, cp->param[j]);
			}
			else
				data += CANNON_SIZE;
		}
	}

	//-- patch 'Menu*' -> 'MenO*'

	if (opt_meno)
	{
		const u32 *optr = GetMenuOffsetTab (str->mode);
		if (optr)
		{
			const int *idx;
			for (idx = meno_patch_idx; *idx >= 0; idx++)
			{
				u8 *d = str->data + optr[*idx] + 13; // 13 = 'O' offset
				if (*d != 'O')
				{
					*d = 'O';
					count++;
				}
			}
		}
	}

	//--- patch https

	count += PatchHTTPS (str);

#if 0 // region specific patch functions not needed yet

    //--- patch version specific

    str->str_status = STR_S_ANALYZED;
    switch (str->mode)
    {
	case STR_M_PAL:
	    count += PatchPAL(str);
	    break;

	case STR_M_USA:
	    count += PatchUSA(str);
	    break;

	case STR_M_JAP:
	    count += PatchJAP(str);
	    break;

	case STR_M_KOR:
	    count += PatchKOR(str);
	    break;

	default:
	    break;
    }
#endif

	return count + PatchByListWPF (str);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			CleanDOL()			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool CleanDol (staticr_t *str) // return TRUE if dol data modified.
{
	AnalyzeDOL (str, true);
	if (str->dol_status & DOL_S_ORIG)
	{
		str->dol_status = 0;
		return 0;
	}

	uint sect_list;
	dol_header_t *dol_head = (dol_header_t *)str->data;

	uint new_size = RemoveDolSections (dol_head, 0x7f83, &sect_list, logging >= 3 ? stdlog : 0);
	bool stat = new_size > 0;
	noPRINT ("SIZE: %x -> %x / sect=%x\n", str->data_size, new_size, sect_list);
	if (stat)
		str->data_size = new_size;

	//--- fix entry point

	const dol_sect_info_t *orig = GetInfoDOL (str->mode);
	DASSERT (orig);
	if (orig[DOL_IDX_ENTRY].addr != ntohl (dol_head->entry_addr))
	{
		stat = true;
		dol_head->entry_addr = htonl (orig[DOL_IDX_ENTRY].addr);
	}

	//--- restore HTTPS links

	PatchHTTPS_HTTP (str, HTTPS_RESTORE);

	//--- terminate

	str->dol_status = 0;
	str->dol_cleaned = true;
	return stat;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			PatchDOL()			///////////////
///////////////////////////////////////////////////////////////////////////////
// GCH support

#define GCT_ADDRESS 0x80001800u
#define GCT_MAX 0x80003000u
#define GCT_END_CHECK 0x80004000u
#define GCT_MM_BASE (GCT_ADDRESS - sizeof (gch_header_t))

#define GCT_MOVE_ADDR 0x802c0000u
#define GCT_MOVE_END 0x80377100u

static MemMap_t gct_data_mem = { 0 }; // memory map of GCT section
static GrowBuffer_t gecko_data = { 0 };
static GrowBuffer_t wcode_job = { 0 };
static GrowBuffer_t wcode_data = { 0 };

static bool use_gct_loader = false;

///////////////////////////////////////////////////////////////////////////////
// patched by

#define STD_PATCHED_BY WSTRT_SHORT " v" VERSION " r" REVISION
static char opt_patched_by[PATCHED_BY_SIZE + 1] = STD_PATCHED_BY;

patched_by_t patched_by_mode = PBY_AUTO;

//-----------------------------------------------------------------------------

