
/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2011-2024 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

#include "lib-std.h"
#include "lib-image.h"

///////////////////////////////////////////////////////////////////////////////
///////////////		Median Cut: data structures		///////////////
///////////////////////////////////////////////////////////////////////////////

#undef CHAN
#define CHAN 4

#if 0
#define mcPRINT PRINT
#else
#define mcPRINT noPRINT
#endif

//-----------------------------------------------------------------------------

typedef struct mc_elem_t
{
	u8 val[CHAN]; // channel tuple
	u32 index; // index of element

} mc_elem_t;

//-----------------------------------------------------------------------------

typedef struct mc_block_t
{
	u8 min[CHAN]; // minimum value of each channel
	u8 max[CHAN]; // maximum value of each channel
	uint max_len; // maximum length of all channels

	mc_elem_t *elem_beg; // pointer to first element
	mc_elem_t *elem_end; // pointer to end of element list

	struct mc_block_t *next; // pointer to next block

} mc_block_t;

//-----------------------------------------------------------------------------

typedef struct mc_t
{
	uint n_elem; // number of element
	mc_elem_t *elem; // list with all elements, alloced

	uint n_block; // number of used blocks
	uint max_block; // max number of blocks
	mc_block_t *block_head; // head of sorted list of blocks
	mc_block_t *block_data; // alloced blocked data
	mc_block_t last_block; // special last block

} mc_t;

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    Median Cut: Debugging		///////////////
///////////////////////////////////////////////////////////////////////////////
#if HAVE_PRINT
///////////////////////////////////////////////////////////////////////////////

static void DumpBlock (const mc_t *mc, const mc_block_t *blk, ccp title)
{
	DASSERT (mc);
	DASSERT (blk);
	DASSERT (CHAN == 4);

	mcPRINT ("BLOCK %u/%u/%u [%s]\n", (int)(blk - mc->block_data), mc->n_block, mc->max_block,
		title ? title : "");

	mcPRINT ("  ** range: %02x..%02x %02x..%02x %02x..%02x %02x..%02x"
			 " ** len: %3u=0x%03x ** elem: %u..%u %3u/%u **\n",
		blk->min[0], blk->max[0], blk->min[1], blk->max[1], blk->min[2], blk->max[2], blk->min[3],
		blk->max[3], blk->max_len, blk->max_len, (int)(blk->elem_beg - mc->elem),
		(int)(blk->elem_end - mc->elem), (int)(blk->elem_end - blk->elem_beg), mc->n_elem);
}

///////////////////////////////////////////////////////////////////////////////

static void DumpBlockList (const mc_t *mc, ccp title)
{
	DASSERT (mc);

	mcPRINT ("BLOCK-LIST %u/%u [%s]\n", mc->n_block, mc->max_block, title ? title : "");

	mc_block_t *blk = mc->block_head;
	uint cnt;
	for (cnt = 0; blk; cnt++, blk = blk->next)
	{
		mcPRINT ("%4u %s: len: %3u=0x%03x ** elem: %u..%u %3u/%u\n", cnt, info, blk->max_len,
			blk->max_len, (int)(blk->elem_beg - mc->elem), (int)(blk->elem_end - mc->elem),
			(int)(blk->elem_end - blk->elem_beg), mc->n_elem);
	}
}

///////////////////////////////////////////////////////////////////////////////

#define DUMPBLOCK(m, b, t) DumpBlock (m, b, t)
#define DUMPBLOCKLIST(m, t) DumpBlockList (m, t)

#else // !HAVE_PRINT
#define DUMPBLOCK(m, b, t)
#define DUMPBLOCKLIST(m, t)
#endif

#define noDUMPBLOCK(m, b, t)
#define noDUMPBLOCKLIST(m, t)

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    Median Cut: Helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static uint CalcMinMax (mc_block_t *blk)
{
	DASSERT (blk);
	memset (blk->min, ~0, sizeof (blk->min));
	memset (blk->max, 0, sizeof (blk->max));

	const mc_elem_t *elem;
	for (elem = blk->elem_beg; elem < blk->elem_end; elem++)
	{
		uint c;
		for (c = 0; c < CHAN; c++)
		{
			const u8 val = elem->val[c];
			noPRINT ("C: %02x -> %02x..%02x\n", val, blk->min[c], blk->max[c]);
			if (blk->min[c] > val)
				blk->min[c] = val;
			if (blk->max[c] < val)
				blk->max[c] = val;
		}
	}

	uint max_len = 0, c;
	for (c = 0; c < CHAN; c++)
	{
		const uint ml = blk->max[c] - blk->min[c];
		if (max_len < ml)
			max_len = ml;
	}

	return blk->max_len = max_len;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    Median Cut: Interface		///////////////
///////////////////////////////////////////////////////////////////////////////

uint MedianCut (const u32 *data, // input: 4-tuple aka RGBA
								 // data is read at the very beginning
								 // and may overlay with 'index' and/or 'pal'
	u16 *index, // not NULL: store index into palette,
				//           can be the space as 'data'

	// The follwing 3 parameters are used for 'data' and 'index'.
	// They allow to skip unused pixel at the end or each row.
	uint width, // used pixels per row
	uint xwidth, // >width: pixels per row
	uint height, // number or rows

	// pallette data
	u32 *pal, // not NULL: store calculated palette
	uint pal_elem // number of wanted palette entries

	// return value = number of calculated palette values
)
{
	DASSERT (data);
	DASSERT (width);
	DASSERT (height);
	DASSERT (pal);
	DASSERT (pal_elem);

	PRINT ("MedianCut(), %d,%dx%d, pal=%d, n_pal=%u\n", width, xwidth, height, pal != 0, pal_elem);

#if HAVE_PRINT || DEBUG
	u64 usec = GetTimerUSec ();
#endif

	static bool done = false;
	if (!done)
	{
		done = true;
		TRACE ("- median cut\n");
		TRACE_SIZEOF (mc_t);
		TRACE_SIZEOF (mc_block_t);
		TRACE_SIZEOF (mc_elem_t);
		TRACE ("-\n");
	}

	//--- setup data structure

	const uint n_elem = width * height;
	if (xwidth <= width)
	{
		// normalization and a little optimization
		width = xwidth = n_elem;
		height = 1;
	}

	if (pal_elem > 0x10000)
		pal_elem = 0x10000;

	mc_t mc;
	memset (&mc, 0, sizeof (mc));
	mc.n_elem = n_elem;
	mc.elem = CALLOC (sizeof (*mc.elem), n_elem);
	mc.n_block = 1;
	mc.max_block = pal_elem;
	mc.block_data = CALLOC (sizeof (*mc.block_data), pal_elem);
	mc.block_head = mc.block_data;
	mc.last_block.elem_beg = mc.elem;
	mc.last_block.elem_end = mc.elem;

	//--- setup first block

	{
		mc_block_t *block = mc.block_head;
		block->next = &mc.last_block;
		block->elem_beg = mc.elem;
		block->elem_end = mc.elem + n_elem;

		const uint delta = xwidth - width;
		mc_elem_t *elem = mc.elem;
		uint h = height, i = 0;
		while (h-- > 0)
		{
			uint w = width;
			while (w-- > 0)
			{
				elem->index = i++;
				memcpy (elem->val, data, sizeof (elem->val));
				elem++;
				data++;
			}
			data += delta;
			i += delta;
		}
		CalcMinMax (block);
		DUMPBLOCK (&mc, block, "startup");
	}

	//--- median cut

	while (mc.n_block < mc.max_block)
	{
		//--- pick first block

		mc_block_t *b1 = mc.block_head;
		DASSERT (b1);
		DASSERT (b1->next);
		DASSERT (b1->max_len >= b1->next->max_len);

		if (!b1->max_len)
			break;

		mc.block_head = b1->next;

		//--- find channel with max length

		uint c;
		for (c = 0; b1->max[c] - b1->min[c] != b1->max_len; c++)
			;
		DASSERT (c < CHAN);

		//--- split block

		DASSERT (mc.n_block < mc.max_block);
		mc_block_t *b2 = mc.block_data + mc.n_block++;
		memcpy (b2, b1, sizeof (*b2));

		const u8 split_val = (b1->min[c] + b1->max[c] + 1) / 2;
		mcPRINT ("\n----- SPLIT b=%zu,%zu, c#%u at %02x, len=%02x -----\n", b1 - mc.block_data,
			b2 - mc.block_data, c, split_val, b1->max_len);
		// b1->max[c] = b1->min[c];
		// b2->min[c] = b2->max[c];

		mc_elem_t *e1 = b1->elem_beg;
		mc_elem_t *e2 = b1->elem_end - 1;
		for (;;)
		{
			//--- find elements to swap

			while (e1 < e2 && e1->val[c] < split_val)
				e1++;

			while (e1 < e2 && e2->val[c] >= split_val)
				e2--;

			if (e1 >= e2)
				break;

			//--- swap elements

			noPRINT ("SWAP: %zu,%zu : #%u %02x >= %02x > %02x\n", e1 - mc.elem, e2 - mc.elem, c,
				e1->val[c], split_val, e2->val[c]);
			mc_elem_t temp;
			memcpy (&temp, e1, sizeof (temp));
			memcpy (e1, e2, sizeof (temp));
			memcpy (e2, &temp, sizeof (temp));
		}
		mcPRINT (" %zu,%zu,%zu,%zu : #%u %02x < %02x <= %02x\n", b1->elem_beg - mc.elem,
			e1 - mc.elem, e2 - mc.elem, b1->elem_end - mc.elem, c, e1[-1].val[c], split_val,
			e1[0].val[c]);
		DASSERT (e1 > b1->elem_beg);
		DASSERT (e1 < b1->elem_end);
		DASSERT (e1[-1].val[c] < split_val);
		DASSERT (e1[0].val[c] >= split_val);

		b1->elem_end = b2->elem_beg = e1;
		CalcMinMax (b1);
		CalcMinMax (b2);

		DUMPBLOCK (&mc, b1, "split 1");
		DUMPBLOCK (&mc, b2, "split 2");

		//--- insert both blocks into list

		noDUMPBLOCKLIST (&mc, "0");

		if (b1->max_len < b2->max_len)
		{
			mc_block_t *temp = b1;
			b1 = b2;
			b2 = temp;
		}

		mc_block_t **ptr = &mc.block_head;
		while ((*ptr)->max_len > b1->max_len)
			ptr = &(*ptr)->next;
		b1->next = *ptr;
		*ptr = b1;
		noDUMPBLOCKLIST (&mc, "b1");

		while ((*ptr)->max_len > b2->max_len)
			ptr = &(*ptr)->next;
		b2->next = *ptr;
		*ptr = b2;
		noDUMPBLOCKLIST (&mc, "b2");
	}
	DUMPBLOCKLIST (&mc, "END");

	//--- fill index table

	if (index)
	{
		const uint max_index = xwidth * height;
		memset (index, 0, max_index * sizeof (*index));
		uint blk_idx = 0;
		const mc_block_t *blk;
		for (blk = mc.block_head; blk != &mc.last_block; blk = blk->next, blk_idx++)
		{
			const mc_elem_t *elem;
			for (elem = blk->elem_beg; elem < blk->elem_end; elem++)
			{
				DASSERT (elem->index < max_index);
				index[elem->index] = blk_idx;
			}
		}
	}

	//--- fill palette table

	if (pal)
	{
		u8 *dest = (u8 *)pal;
		const mc_block_t *blk;
		for (blk = mc.block_head; blk != &mc.last_block; blk = blk->next)
		{
			uint val[CHAN];
			memset (val, 0, sizeof (val));

			const mc_elem_t *elem;
			for (elem = blk->elem_beg; elem < blk->elem_end; elem++)
			{
				uint c;
				for (c = 0; c < CHAN; c++)
					val[c] += elem->val[c];
			}

			const uint n = blk->elem_end - blk->elem_beg;
			DASSERT (n);
			uint c;
			for (c = 0; c < CHAN; c++)
				*dest++ = (val[c] + n / 2) / n;
		}
	}

	//--- clean & return

	FREE (mc.elem);
	FREE (mc.block_data);

#if HAVE_PRINT
	usec = GetTimerUSec () - usec;
	PRINT ("MEDIAN-CUT: elem=%u, pal=%u/%u, %llu µsec\n", n_elem, mc.n_block, pal_elem, usec);
#elif DEBUG
	usec = GetTimerUSec () - usec;
	TRACE ("MEDIAN-CUT: elem=%u, pal=%u/%u, %llu µsec\n", n_elem, mc.n_block, pal_elem, usec);
#else
	PRINT ("MEDIAN-CUT: elem=%u, pal=%u/%u\n", n_elem, mc.n_block, pal_elem);
#endif

	return mc.n_block;
}

//
///////////////////////////////////////////////////////////////////////////////
