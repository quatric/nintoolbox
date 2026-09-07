
/***************************************************************************
 *                    __            __ _ ___________                       *
 *                    \ \          / /| |____   ____|                      *
 *                     \ \        / / | |    | |                           *
 *                      \ \  /\  / /  | |    | |                           *
 *                       \ \/  \/ /   | |    | |                           *
 *                        \  /\  /    | |    | |                           *
 *                         \/  \/     |_|    |_|                           *
 *                                                                         *
 *                           Wiimms ISO Tools                              *
 *                         https://wit.wiimm.de/                           *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the WIT project.                                 *
 *   Visit https://wit.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2009-2021 by Dirk Clemens <wiimm@wiimm.de>              *
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


#include <string.h>

#include "lib-lfg.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    internal			///////////////
///////////////////////////////////////////////////////////////////////////////

// Advance the state by one full buffer length (LFG_K words).

static void regenerate ( lfg_t * lfg )
{
    DASSERT(lfg);

    uint i;
    for ( i = 0; i < LFG_J; i++ )
	lfg->buf[i] ^= lfg->buf[i+LFG_K-LFG_J];

    for ( i = LFG_J; i < LFG_K; i++ )
	lfg->buf[i] ^= lfg->buf[i-LFG_J];

    lfg->pos = 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    interface			///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeLFG ( lfg_t * lfg, const void * seed )
{
    DASSERT(lfg);
    DASSERT(seed);

    const u8 * src = seed;
    uint i;
    for ( i = 0; i < LFG_SEED_WORDS; i++, src += 4 )
	lfg->buf[i] = be32(src);

    for ( i = LFG_SEED_WORDS; i < LFG_K; i++ )
	lfg->buf[i] = lfg->buf[i-17] << 23
		    ^ lfg->buf[i-16] >>  9
		    ^ lfg->buf[i- 1];

    // The generator must be advanced 4 times before the first byte is valid.
    for ( i = 0; i < 4; i++ )
	regenerate(lfg);
}

///////////////////////////////////////////////////////////////////////////////

void ForwardLFG ( lfg_t * lfg, u32 count )
{
    DASSERT(lfg);

    while ( count )
    {
	if ( lfg->pos >= LFG_STATE_SIZE )
	    regenerate(lfg);

	const u32 avail = LFG_STATE_SIZE - lfg->pos;
	const u32 step  = count < avail ? count : avail;
	lfg->pos += step;
	count    -= step;
    }
}

///////////////////////////////////////////////////////////////////////////////

void GetBytesLFG ( lfg_t * lfg, void * dest, u32 size )
{
    DASSERT(lfg);
    DASSERT( dest || !size );

    // The 4 bytes of a state word are *not* a plain big endian store: the
    // second byte is taken from bits 18..25, not 16..23.  This overlap is a
    // property of Nintendo's generator, not a typo.
    static const u8 shift_tab[4] = { 24, 18, 8, 0 };

    u8 * out = dest;
    while ( size )
    {
	if ( lfg->pos >= LFG_STATE_SIZE )
	    regenerate(lfg);

	const u32 word = lfg->buf[ lfg->pos/4 ];
	*out++ = word >> shift_tab[ lfg->pos & 3 ];
	lfg->pos++;
	size--;
    }
}
