//
///////////////////////////////////////////////////////////////////////////////
///////////////		    command BCH				///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_bch (void)
{
	if (!n_param)
		return ERROR0 (ERR_SYNTAX, "Command BCH needs a file\n");

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		u8 *data = 0;
		size_t fsize = 0;
		enumError err = LoadFileAlloc (arg, 0, 0, &data, &fsize, 0, 0, 0, false);
		if (err)
		{
			if (max_err < err)
				max_err = err;
			continue;
		}

		// CGFX/BCRES is the other 3DS graphics container; list it here too so
		// one command covers both rather than making the user pick.
		if (fsize >= 4 && !memcmp (data, "CGFX", 4))
		{
			cgfx_t cgfx;
			if (!ScanCGFX (&cgfx, data, (size_t)fsize))
			{
				FREE (data);
				ERROR0 (ERR_INVALID_DATA, "Invalid CGFX file: %s\n", arg);
				if (max_err < ERR_INVALID_DATA)
					max_err = ERR_INVALID_DATA;
				continue;
			}
			printf ("\n%s%s%s\n", colout->heading, arg, colout->reset);
			printf ("  CGFX/BCRES, revision 0x%08x\n", cgfx.revision);
			uint tot = 0;
			for (int i = 0; i < CGFX_N_DICTS; i++)
			{
				if (!cgfx.dict[i].n)
					continue;
				tot += cgfx.dict[i].n;
				printf ("  %-21s %u\n", GetCGFXDictName (i), cgfx.dict[i].n);
				for (uint k = 0; k < cgfx.dict[i].n; k++)
					printf ("      @%08x  %s\n", cgfx.dict[i].entries[k].address,
						cgfx.dict[i].entries[k].name);
			}
			if (!tot)
				printf ("  (no named content)\n");
			ResetCGFX (&cgfx);
			FREE (data);
			continue;
		}

		bch_t bch;
		err = ScanBCH (&bch, data, (uint)fsize);
		FREE (data);
		if (err)
		{
			ERROR0 (ERR_INVALID_DATA, "Not a BCH or CGFX file: %s\n", arg);
			if (max_err < ERR_INVALID_DATA)
				max_err = ERR_INVALID_DATA;
			continue;
		}

		printf ("\n%s%s%s\n", colout->heading, arg, colout->reset);
		printf ("  version: backward 0x%02x, forward 0x%02x\n", bch.bc, bch.fc);
		printf ("  sections: contents 0x%x+0x%x, strings 0x%x+0x%x,"
				" commands 0x%x+0x%x, raw 0x%x+0x%x, reloc 0x%x+0x%x\n",
			bch.contents_addr, bch.contents_len, bch.strings_addr, bch.strings_len,
			bch.commands_addr, bch.commands_len, bch.raw_data_addr, bch.raw_data_len,
			bch.reloc_addr, bch.reloc_len);

		uint total = 0;
		for (int i = 0; i < BCH_N_DICTS; i++)
		{
			const bch_dict_t *d = bch.dict + i;
			if (!d->n)
				continue;
			total += d->n;
			printf ("  %-21s %u\n", GetBCHDictName (i), d->n);
			for (uint k = 0; k < d->n; k++)
				printf ("      @%08x  %s\n", d->entries[k].address, d->entries[k].name);
		}
		if (!total)
			printf ("  (no named content)\n");

		ResetBCH (&bch);
	}

	ResetStringField (&plist);
	return max_err;
}
