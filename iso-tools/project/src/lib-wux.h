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

#ifndef WIT_LIB_WUX_H
#define WIT_LIB_WUX_H 1

#include "dclib/dclib-types.h"
#include "lib-std.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////			WUX file layout			///////////////
///////////////////////////////////////////////////////////////////////////////

// WUX is the sparse container for Wii U disc images (WUD).  It is *not* a
// compression in the usual sense: the payload sectors are stored verbatim,
// but identical sectors are stored only once and referenced repeatedly by
// the index table.  A retail 25 GB WUD is mostly encrypted data plus huge
// runs of identical padding, so this alone is what makes the format useful.
//
//	+-----------------------+
//	| wux_header_t		|  0x20 bytes, little endian
//	+-----------------------+
//	| u32 index[n_sector]	|  one entry per image sector:
//	|			|  the index of the stored sector holding it
//	+-----------------------+  (padded up to a sector boundary)
//	| sector data		|  each unique sector, once, in first-use order
//	| ...			|
//	+-----------------------+

//
///////////////////////////////////////////////////////////////////////////////
///////////////			WUX definitions			///////////////
///////////////////////////////////////////////////////////////////////////////

#define WUX_MAGIC0		0x30585557	// 'WUX0', little endian
#define WUX_MAGIC1		0x1099d02e
#define WUX_HEAD_SIZE		0x20
#define WUX_DEF_SECTOR_SIZE	0x8000		// what retail images use

// A Wii U disc is 25 GB; refuse anything absurd rather than allocating it.
#define WUX_MAX_IMAGE_SIZE	(64ull*GiB)
#define WUX_MIN_SECTOR_SIZE	0x800
#define WUX_MAX_SECTOR_SIZE	0x100000

///////////////////////////////////////////////////////////////////////////////
// [[wux_header_t]]

typedef struct wux_header_t // little endian
{
  /* 0x00 */	u32 magic0;		// WUX_MAGIC0
  /* 0x04 */	u32 magic1;		// WUX_MAGIC1
  /* 0x08 */	u32 sector_size;	// size of one sector
  /* 0x0c */	u32 flags;		// unused, always 0
  /* 0x10 */	u64 image_size;		// size of the uncompressed image
  /* 0x18 */	u32 unknown[2];		// unused, always 0
  /* 0x20 */
}
__attribute__ ((packed)) wux_header_t;

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    WUX_t			///////////////
///////////////////////////////////////////////////////////////////////////////
// [[wux_hash_t]]

// Hash bucket used by the writer to find an already stored sector with the
// same content.  A match on 'sum' is only a candidate: the stored sector is
// read back and compared before it is reused, so a collision costs time but
// can never corrupt the image.

typedef struct wux_hash_t
{
    u32		sum;			// checksum of the sector
    u32		sector;			// index of the stored sector
    int		next;			// next entry of the bucket, -1 = end
}
wux_hash_t;

///////////////////////////////////////////////////////////////////////////////
// [[WUX_t]]

typedef struct WUX_t
{
    FILE	*f;			// open file, not owned by WUX_t
    ccp		fname;			// file name, for error messages only

    //--- geometry

    u32		sector_size;		// size of one sector
    u64		image_size;		// size of the uncompressed image
    u32		n_index;		// number of image sectors
    u32		*index;			// index table, alloced, little endian
    u64		data_offset;		// file offset of the first stored sector

    //--- write support

    bool	writing;		// true: opened for writing
    u32		n_written;		// number of image sectors consumed so far
    u32		n_stored;		// number of unique sectors stored in the file

    int		*hash_tab;		// alloced hash table, -1 = empty bucket
    uint	hash_size;		// number of buckets
    wux_hash_t	*hash_list;		// alloced, 'n_stored' used entries
    uint	hash_used;		// used entries of 'hash_list'
    uint	hash_alloced;		// alloced entries of 'hash_list'

    u8		*buf;			// alloced sector assembly buffer
    u32		buf_fill;		// bytes already collected in 'buf'
    u8		*cmp_buf;		// alloced scratch sector for compares
}
WUX_t;

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    Interface			///////////////
///////////////////////////////////////////////////////////////////////////////

// Return true if DATA looks like the start of a WUX file.  'data_size' must
// be at least WUX_HEAD_SIZE, otherwise the result is always false.

bool IsValidWUX
(
    const void		*data,		// valid pointer to data
    uint		data_size,	// size of data to analyze
    wux_header_t	*head		// not NULL: store header (local endian) here
);

///////////////////////////////////////////////////////////////////////////////

void InitializeWUX ( WUX_t *wux );
void ResetWUX ( WUX_t *wux );

///////////////////////////////////////////////////////////////////////////////
// reading

enumError OpenReadWUX
(
    WUX_t		*wux,		// data structure, will be initialized
    FILE		*f,		// open file, positioned anywhere
    ccp			fname		// file name for error messages
);

//-----------------------------------------------------------------------------

enumError ReadWUX
(
    WUX_t		*wux,		// initialized by OpenReadWUX()
    u64			off,		// offset into the uncompressed image
    void		*buf,		// destination buffer
    size_t		count		// number of bytes to read
);

///////////////////////////////////////////////////////////////////////////////
// writing
//
// The index table is written at the very start of the file, so the total
// image size must be known up front; a WUX can not grow.  Sectors are then
// handed over strictly in order by WriteWUX(), and TermWriteWUX() rewrites
// the header and index table.

enumError OpenWriteWUX
(
    WUX_t		*wux,		// data structure, will be initialized
    FILE		*f,		// open file, must be readable too ("w+b")
    ccp			fname,		// file name for error messages
    u64			image_size,	// size of the uncompressed image
    u32			sector_size	// 0: use WUX_DEF_SECTOR_SIZE
);

//-----------------------------------------------------------------------------

// Append the next 'count' bytes of the image.  Data must arrive in order.

enumError WriteWUX
(
    WUX_t		*wux,		// initialized by OpenWriteWUX()
    const void		*buf,		// source buffer
    size_t		count		// number of bytes to write
);

//-----------------------------------------------------------------------------

enumError TermWriteWUX
(
    WUX_t		*wux		// initialized by OpenWriteWUX()
);

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////

#endif // WIT_LIB_WUX_H
