///////////////////////////////////////////////////////////////////////////////
///////////////			global common			///////////////
///////////////////////////////////////////////////////////////////////////////

const VarMap_t *SetupVarsCOMMON ()
{
	static VarMap_t vm = { .force_case = LOUP_UPPER };
	if (!vm.used)
	{
		DefineObjectNameVars (&vm);
		DefineParserVars (&vm);

		//--- setup integer variables

		struct inttab_t
		{
			ccp name;
			int val;
		};
		static const struct inttab_t inttab[]
			= { { "NEWOBJ$OFF", NEWOBJ_OFF }, { "NEWOBJ$REPLACE", NEWOBJ_REPLACE },
				  { "NEWOBJ$SHRINK", NEWOBJ_SHRINK }, { "NEWOBJ$GROW", NEWOBJ_GROW },

				  { "NEW_OBJECT", NEWOBJ__DEFAULT }, { "OBJECT_SLOTS", OBJMGR_USUAL_SLOTS },
				  { "MAX_OBJECT_ID", OBJMGR_USUAL_MAX_OBJID },

				  { 0, 0 } };

		const struct inttab_t *ip;
		for (ip = inttab; ip->name; ip++)
			DefineIntVar (&vm, ip->name, ip->val);
	}

	return &vm;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool WriteObjMgrToSetupSection (FILE *f, // output file
	const object_mgr_t *om, // valid object manager
	bool always, // false: write only if not standard
	uint header, // 0: off
				 // 1: start with: [SECTION]\n @REVISION=
				 // 2: add 'section_sep' before step 1
	bool explain // true: insert extented explainations
)
{
	DASSERT (f);
	DASSERT (om);

	if (!always && IsStandardObjType (&om->otype))
		return false;

	if (header)
	{
		if (header > 1)
			fputs (section_sep, f);
		fprintf (f, "\r\n[SETUP]\r\nREVISION	= %u\r\n", REVISION_NUM);
	}

	if (explain)
		fputs (text_explain_objmgr_cr, f);

	fprintf (f,
		"\r\n"
		"NEW-OBJECT	= NEWOBJ$%s\r\n"
		"OBJECT-SLOTS	= 0x%03x\r\n"
		"MAX-OBJECT-ID	= 0x%03x\r\n",
		GetNewObjectName (om->otype.new_object), om->otype.object_slots, om->otype.max_object_id);

	return true;
}

///////////////////////////////////////////////////////////////////////////////

bool SetupObjMgrBySI (object_mgr_t *om, // valid data
	ScanInfo_t *si, // valid data
	ccp name // name of command
)
{
	DASSERT (om);
	DASSERT (si);
	DASSERT (name);

	if (*name == '@')
		name++;

	if (!strcmp (name, "NEW-OBJECT"))
	{
		u32 val;
		ScanU32SI (si, &val, 1, 0);
		if (val < NEWOBJ__N)
		{
			om->otype.new_object = val;
			DefineIntVar (&si->gvar, "NEW_OBJECT", val);
			PRINT ("NEW_OBJECT = %d", val);
		}
		return true;
	}

	if (!strcmp (name, "OBJECT-SLOTS"))
	{
		u32 val;
		ScanU32SI (si, &val, 1, 0);
		if (val > OBJMGR_MAX_SLOTS)
			val = OBJMGR_MAX_SLOTS;
		om->otype.object_slots = val;
		DefineIntVar (&si->gvar, "OBJECT_SLOTS", val);
		PRINT ("OBJECT_SLOTS = %d = 0x%x", val, val);
		return true;
	}

	if (!strcmp (name, "MAX-OBJECT-ID"))
	{
		u32 val;
		ScanU32SI (si, &val, 1, 0);
		if (val > OBJMGR_MAX_OBJID)
			val = OBJMGR_MAX_OBJID;
		om->otype.max_object_id = val;
		DefineIntVar (&si->gvar, "MAX_OBJECT_ID", val);
		PRINT ("MAX_OBJECT_ID = %d = 0x%x", val, val);
		return true;
	}

	return false;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			object_mgr_t			///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeObjMgr (object_mgr_t *om, file_format_t fform)
{
	DASSERT (om);
	memset (om, 0, sizeof (*om));
	InitializeObjType (&om->otype, fform);
#if USE_OBJECT_MANAGER
	if (om->otype.row_size)
		SetupObjMgr (om, om->otype.row_size);
#endif
}

//-----------------------------------------------------------------------------

void ResetObjMgr (object_mgr_t *om)
{
	if (om)
	{
		FREE (om->list);
		InitializeObjMgr (om, om->otype.fform);
	}
}

//-----------------------------------------------------------------------------

ccp LogStateObjMgr (const object_mgr_t *om)
{
	if (!om)
		return "-";

	if (!om->list)
		return LogObjType (&om->otype);

	const int PRIO_SIZE = OBJMGR_MAX_PRIO + 1;

	uint usage_count = 0;
	u16 count[PRIO_SIZE], max[PRIO_SIZE];
	memset (count, 0, sizeof (count));
	memset (max, 0, sizeof (max));

	int i;
	for (i = 0; i < OBJMGR_MAX_SLOTS; i++)
	{
		const u8 prio = om->prio[i];
		if (prio <= OBJMGR_MAX_PRIO)
		{
			count[prio]++;
			max[prio] = i;
			if (prio)
				usage_count++;
		}
	}

	char buf[200], *dest = buf;
	dest = snprintfE (dest, buf + sizeof (buf), "nr=%u/%u ds=%u %s", usage_count, OBJMGR_MAX_SLOTS,
		om->row_data_size, LogObjType (&om->otype));

	for (i = 0; i < PRIO_SIZE; i++)
		if (count[i])
			dest = snprintfE (dest, buf + sizeof (buf), " P%u=%u,%x", i, count[i], max[i]);

	MARK_USED (count, max);
	return CopyCircBuf0 (buf, dest - buf);
}

///////////////////////////////////////////////////////////////////////////////
#if USE_OBJECT_MANAGER
///////////////////////////////////////////////////////////////////////////////

bool SetupObjMgr (object_mgr_t *om, uint row_size)
{
	DASSERT (om);
	DASSERT (row_size <= OBJMGR_MAX_ROW_SIZE);

	if (row_size >= 2 && row_size <= OBJMGR_MAX_ROW_SIZE)
	{
		FREE (om->list);
		om->otype.row_size = row_size;
		om->row_data_size = row_size - 2;
		om->list = CALLOC (row_size, OBJMGR_MAX_SLOTS);

		uint obj_id;
		u8 *dest = om->list;
		for (obj_id = 0; obj_id < OBJMGR_MAX_SLOTS; obj_id++)
		{
			write_be16 (dest, obj_id);
			dest += row_size;
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------

bool SetupObjMgrByData (object_mgr_t *om, cvp data, uint data_size, u8 prio)
{
	bool stat = ScanObjType (&om->otype, data, data_size, data_size);
	if (stat)
	{
		SetupObjMgr (om, om->otype.row_size);

		uint i;
		const u8 *ptr = (u8 *)data + om->otype.slot_offset;
		for (i = 0; i < om->otype.object_slots; i++, ptr += om->otype.row_size)
			InsertObjMgr (om, be16 (ptr), ptr + 2, prio);
	}
	return stat;
}

///////////////////////////////////////////////////////////////////////////////

u8 *InsertObjMgr (object_mgr_t *om, uint obj_id, cvp data, u8 prio)
{
	DASSERT (om);
	if (!prio)
		return RemoveObjMgr (om, obj_id);

	if (om && om->list && obj_id < OBJMGR_MAX_SLOTS)
	{
		u8 *dest = om->list + obj_id * om->otype.row_size;
		if (om->prio[obj_id] <= prio)
		{
			om->prio[obj_id] = prio;
			memcpy (dest + 2, data, om->row_data_size);
			if (om->max_prio < prio)
				om->max_prio = prio;
		}
		return dest;
	}
	return 0;
}

//-----------------------------------------------------------------------------

u8 *ClearObjMgr (object_mgr_t *om, uint obj_id)
{
	DASSERT (om);
	if (om && om->list && obj_id < OBJMGR_MAX_SLOTS)
	{
		om->prio[obj_id] = 1;
		u8 *dest = om->list + obj_id * om->otype.row_size;
		memset (dest + 2, 0, om->row_data_size);
		return dest;
	}
	return 0;
}

//-----------------------------------------------------------------------------

u8 *RemoveObjMgr (object_mgr_t *om, uint obj_id)
{
	DASSERT (om);
	if (om && om->list && obj_id < OBJMGR_MAX_SLOTS)
	{
		om->prio[obj_id] = 0;
		u8 *dest = om->list + obj_id * om->otype.row_size;
		memset (dest + 2, 0, om->row_data_size);
		return dest;
	}
	return 0;
}

//-----------------------------------------------------------------------------

u8 *GetObjMgr (object_mgr_t *om, uint obj_id)
{
	DASSERT (om);
	if (om && om->list && obj_id < OBJMGR_MAX_SLOTS)
		return om->list + obj_id * om->otype.row_size;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

u8 *CreateBinObjMgr (
	// return alloced data
	uint *res_size, // not NULL: store size of alloced data

	const object_mgr_t *om, // valid object manager
	uint min_prio, // minimal prio
	uint max_elem, // >0: limit number of elements
	uint max_obj_id // >0: ignore objects with id >#
)
{
	DASSERT (om);
	PRINT ("CreateBinObjMgr( minp=%u, maxe=%u, maxo=%u )"
		   " list=%d, USE_OBJECT_MANAGER=%d\n",
		min_prio, max_elem, max_obj_id, !om ? -1 : om->list != 0, USE_OBJECT_MANAGER);
	PRINT (" -> %s\n", LogStateObjMgr (om));
	if (!om || !om->list || !om->otype.row_size)
		return 0;

	if (!max_elem || max_elem >= OBJMGR_MAX_SLOTS)
		max_elem = OBJMGR_MAX_SLOTS - 1;
	const uint limit_obj_id
		= max_obj_id && max_obj_id < OBJMGR_MAX_SLOTS ? max_obj_id : OBJMGR_MAX_SLOTS - 1;

	//--- statistical analysis

	const int PRIO_SIZE = OBJMGR_MAX_PRIO + 1;

	uint total_count = 0;
	u16 count[PRIO_SIZE], max[PRIO_SIZE];
	memset (count, 0, sizeof (count));
	memset (max, 0, sizeof (max));

	int i;
	for (i = 0; i < limit_obj_id; i++)
	{
		const u8 prio = om->prio[i];
		if (prio >= min_prio && prio <= OBJMGR_MAX_PRIO)
		{
			count[prio]++;
			max[prio] = i;
			total_count++;
		}
	}

	//--- find elements to insert

	uint total_use = 0;
	u16 use[OBJMGR_MAX_SLOTS];
	memset (use, 0, sizeof (use));

	int prio, max_obj_used = 0;
	for (prio = OBJMGR_MAX_PRIO; prio >= min_prio && total_use < max_elem; prio--)
	{
		if (count[prio])
		{
			uint i;
			for (i = 0; i < limit_obj_id; i++)
				if (prio == om->prio[i])
				{
					use[i]++;
					if (max_obj_used < i)
						max_obj_used = i;
					if (++total_use >= max_elem)
						break;
				}
		}
	}

	if (!max_obj_id)
		max_obj_id = max_obj_used;

	PRINT (" -> FOUND: %u/%u/%u, max_obj=%x/%x/%x\n", total_use, total_count, max_elem,
		max_obj_used, max_obj_id, limit_obj_id);

	//--- calculate data size

	const uint data_size
		= om->otype.slot_offset + total_use * om->otype.row_size + (max_obj_id + 1) * sizeof (u16);
	if (res_size)
		*res_size = data_size;
	u8 *data = MALLOC (data_size);

	//--- fill data

	write_be16 (data, total_use);
	write_be16 (data + 2, om->row_data_size / 2);

	u8 *bin = data + om->otype.slot_offset;
	u16 *ref = (u16 *)(bin + total_use * om->otype.row_size);
	DASSERT ((u8 *)ref + (max_obj_id + 1) * sizeof (u16) == data + data_size);
	memset (ref, ~0, (max_obj_id + 1) * sizeof (u16));

	uint idx = 0;

	for (i = 0; i <= max_obj_id; i++)
	{
		if (!use[i])
			continue;

		memcpy (bin, om->list + i * om->otype.row_size, om->otype.row_size);
		bin += om->otype.row_size;
		ref[i] = htons (idx++);
	}
	DASSERT (idx == total_use);

	return data;
}

///////////////////////////////////////////////////////////////////////////////

bool VerifyObjMgr (const object_mgr_t *om, // valid object manager
	cvp data, // data to compare
	uint data_size, // size of 'data'
	ccp identify // string to identify the caller
)
{
	uint om_size;
	u8 *om_data = CreateBinObjMgr (&om_size, om, 1, 0, 0);

	bool status = false;
	if (!om_data)
		fprintf (stderr, "VERIFY OBJ-MGR[%s]: no data\n", identify);
	else if (om_size != data_size)
		fprintf (
			stderr, "VERIFY OBJ-MGR[%s]: size differ: %u : %u\n", identify, data_size, om_size);
	else if (memcmp (om_data, data, data_size))
		fprintf (stderr, "VERIFY OBJ-MGR[%s]: data differ\n", identify);
	else
		status = true;

	if (!status)
		fprintf (stderr, "LOG: %s\n", LogStateObjMgr (om));

	if (!status && om_data)
	{
		XDump_t xd;
		InitializeXDump (&xd);
		xd.f = stderr;
		xd.indent = 6;
		xd.format = XDUMPF_INT_2;
		xd.endian = DC_BIG_ENDIAN;
		xd.assumed_size = data_size < om_size ? data_size : om_size;
		xd.min_width = om->otype.row_size;
		while (xd.min_width > 40 && !(xd.min_width & 1))
			xd.min_width /= 2;

#if 0
	const uint off = om->otype.slot_offset;
	if ( data_size <= off || om_size <= off || memcmp(data,om_data,off) )
	    XDiff( &xd, data,data_size,true, om_data,om_size,true, 5,false );
	else
	    XDiff( &xd, data+off,data_size-off,true,
			om_data+off,om_size-off,true, 5,false );
#else
		u64 break_list[] = { om->otype.slot_offset, om->otype.ref_offset, 0 };
		xd.addr_break = break_list;
		XDiff (&xd, data, data_size, true, om_data, om_size, true, 5, false);
#endif
		ResetXDump (&xd);
	}
	FREE (om_data);
	return status;
}

///////////////////////////////////////////////////////////////////////////////

bool VerifyBinObjMgr (cvp data, uint data_size, ccp identify)
{
	object_mgr_t om;
	InitializeObjMgr (&om, FF_UNKNOWN);
	SetupObjMgrByData (&om, data, data_size, 1);
	const bool stat = VerifyObjMgr (&om, data, data_size, identify);
	ResetObjMgr (&om);
	return stat;
}

///////////////////////////////////////////////////////////////////////////////
#endif //  USE_OBJECT_MANAGER
//
///////////////////////////////////////////////////////////////////////////////
///////////////			    END				///////////////
///////////////////////////////////////////////////////////////////////////////
