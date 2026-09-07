
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

///////////////////////////////////////////////////////////////////////////////
// Lagged Fibonacci generator as used by Nintendo to fill the unused areas of
// GameCube and Wii discs with "junk" data.  Parameters: f = xor, j = 32,
// k = 521.  The same generator is used by the RVZ container format to store
// that padding losslessly (see lib-wia.c) and by NKit to rebuild it.
//
// Reference: https://github.com/dolphin-emu/dolphin/blob/master/docs/WiaAndRvz.md
///////////////////////////////////////////////////////////////////////////////

#ifndef WIT_LIB_LFG_H
#define WIT_LIB_LFG_H 1

#include "dclib/dclib-types.h"
#include "lib-std.h"

//-----------------------------------------------------------------------------

#define LFG_K		521			// state size in 32 bit words
#define LFG_J		 32			// lag
#define LFG_SEED_WORDS	 17			// words of seed data
#define LFG_SEED_SIZE	 (LFG_SEED_WORDS*4)	// = 68 bytes of seed data
#define LFG_STATE_SIZE	 (LFG_K*4)		// = 2084 bytes per regeneration

// [[lfg_t]]

typedef struct lfg_t
{
    u32		buf[LFG_K];	// generator state, host endian
    u32		pos;		// byte position within 'buf', 0..LFG_STATE_SIZE

} lfg_t;

//-----------------------------------------------------------------------------

// Initialize LFG with 68 bytes of big endian seed data, as stored in RVZ.
void InitializeLFG ( lfg_t * lfg, const void * seed );

// Skip 'count' bytes of output.  Cheap for small counts, but still O(count)
// because the generator has no closed form; RVZ only ever needs < 32 KiB.
void ForwardLFG ( lfg_t * lfg, u32 count );

// Write 'size' bytes of generated data to 'dest'.
void GetBytesLFG ( lfg_t * lfg, void * dest, u32 size );

#endif // WIT_LIB_LFG_H
