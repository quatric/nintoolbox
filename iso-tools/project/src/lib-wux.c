
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

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "lib-wux.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static u32 wux_checksum ( const u8 *data, uint size )
{
    // FNV-1a; only used to group candidate sectors, never to decide equality
    u32 sum = 0x811c9dc5;
    while ( size-- )
	sum = ( sum ^ *data++ ) * 0x01000193;
    return sum;
}

///////////////////////////////////////////////////////////////////////////////

static enumError wux_read_at ( WUX_t *wux, u64 off, void *buf, size_t count )
{
    if ( fseeko(wux->f,(off_t)off,SEEK_SET) )
	return ERROR1(ERR_READ_FAILED,"Can't seek to %llu: %s\n",off,wux->fname);
    if ( fread(buf,1,count,wux->f) != count )
	return ERROR1(ERR_READ_FAILED,"Can't read %zu bytes at %llu: %s\n",
			count,off,wux->fname);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    Interface			///////////////
///////////////////////////////////////////////////////////////////////////////

bool IsValidWUX
(
    const void		*data,		// valid pointer to data
    uint		data_size,	// size of data to analyze
    wux_header_t	*head		// not NULL: store header (local endian) here
)
{
    if ( !data || data_size < WUX_HEAD_SIZE )
	return false;

    const u8 *d = data;
    wux_header_t h;
    memset(&h,0,sizeof(h));
    h.magic0	  = le32(d+0x00);
    h.magic1	  = le32(d+0x04);
    h.sector_size = le32(d+0x08);
    h.flags	  = le32(d+0x0c);
    h.image_size  = le64(d+0x10);

    const bool ok = h.magic0 == WUX_MAGIC0
		 && h.magic1 == WUX_MAGIC1
		 && h.sector_size >= WUX_MIN_SECTOR_SIZE
		 && h.sector_size <= WUX_MAX_SECTOR_SIZE
		 && !( h.sector_size & h.sector_size-1 )	// power of 2
		 && h.image_size > 0
		 && h.image_size <= WUX_MAX_IMAGE_SIZE;

    if ( ok && head )
	memcpy(head,&h,sizeof(*head));
    return ok;
}

///////////////////////////////////////////////////////////////////////////////

void InitializeWUX ( WUX_t *wux )
{
    DASSERT(wux);
    memset(wux,0,sizeof(*wux));
}

///////////////////////////////////////////////////////////////////////////////

void ResetWUX ( WUX_t *wux )
{
    DASSERT(wux);
    FREE(wux->index);
    FREE(wux->hash_tab);
    FREE(wux->hash_list);
    FREE(wux->buf);
    FREE(wux->cmp_buf);
    InitializeWUX(wux);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    reading			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError OpenReadWUX
(
    WUX_t		*wux,		// data structure, will be initialized
    FILE		*f,		// open file, positioned anywhere
    ccp			fname		// file name for error messages
)
{
    DASSERT(wux);
    DASSERT(f);

    InitializeWUX(wux);
    wux->f	= f;
    wux->fname	= fname ? fname : "?";

    u8 head[WUX_HEAD_SIZE];
    enumError err = wux_read_at(wux,0,head,sizeof(head));
    if (err)
	return err;

    wux_header_t h;
    if (!IsValidWUX(head,sizeof(head),&h))
	return ERROR0(ERR_WRONG_FILE_TYPE,"Not a WUX file: %s\n",wux->fname);

    wux->sector_size = h.sector_size;
    wux->image_size  = h.image_size;

    const u64 n_index = ( h.image_size + h.sector_size - 1 ) / h.sector_size;
    if ( n_index > 0x8000000 )
	return ERROR0(ERR_WRONG_FILE_TYPE,
		"WUX index table too large (%llu entries): %s\n",n_index,wux->fname);
    wux->n_index = (u32)n_index;

    wux->index = MALLOC(wux->n_index*sizeof(*wux->index));
    err = wux_read_at(wux,WUX_HEAD_SIZE,wux->index,
			wux->n_index*sizeof(*wux->index));
    if (err)
	return err;

    const u64 tab_end = WUX_HEAD_SIZE + (u64)wux->n_index * sizeof(*wux->index);
    wux->data_offset = ( tab_end + h.sector_size - 1 ) / h.sector_size * h.sector_size;

    // The stored sector count is not in the header; the index table is the
    // only place it can be recovered from.
    for ( u32 i = 0; i < wux->n_index; i++ )
    {
	const u32 sector = le32(wux->index+i);
	if ( sector >= wux->n_stored )
	    wux->n_stored = sector + 1;
    }

    wux->buf = MALLOC(wux->sector_size);
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError ReadWUX
(
    WUX_t		*wux,		// initialized by OpenReadWUX()
    u64			off,		// offset into the uncompressed image
    void		*buf,		// destination buffer
    size_t		count		// number of bytes to read
)
{
    DASSERT(wux);
    DASSERT(wux->index);

    if ( off + count > wux->image_size )
	return ERROR0(ERR_READ_FAILED,
		"Read beyond end of image (%llu+%zu > %llu): %s\n",
		off, count, wux->image_size, wux->fname );

    u8 *dest = buf;
    while ( count > 0 )
    {
	const u32 idx	 = (u32)( off / wux->sector_size );
	const u32 skip	 = (u32)( off % wux->sector_size );
	const u32 avail	 = wux->sector_size - skip;
	const size_t now = count < avail ? count : avail;

	const u64 src = wux->data_offset
		      + (u64)le32(wux->index+idx) * wux->sector_size + skip;

	const enumError err = wux_read_at(wux,src,dest,now);
	if (err)
	    return err;

	off   += now;
	dest  += now;
	count -= now;
    }
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    writing			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError OpenWriteWUX
(
    WUX_t		*wux,		// data structure, will be initialized
    FILE		*f,		// open file, will be written from offset 0
    ccp			fname,		// file name for error messages
    u64			image_size,	// size of the uncompressed image
    u32			sector_size	// 0: use WUX_DEF_SECTOR_SIZE
)
{
    DASSERT(wux);
    DASSERT(f);

    InitializeWUX(wux);
    wux->f	 = f;
    wux->fname	 = fname ? fname : "?";
    wux->writing = true;

    if (!sector_size)
	sector_size = WUX_DEF_SECTOR_SIZE;
    if ( sector_size < WUX_MIN_SECTOR_SIZE
      || sector_size > WUX_MAX_SECTOR_SIZE
      || sector_size & sector_size-1 )
	return ERROR0(ERR_INVALID_FILE,
		"Invalid WUX sector size: 0x%x\n",sector_size);
    if ( !image_size || image_size > WUX_MAX_IMAGE_SIZE )
	return ERROR0(ERR_INVALID_FILE,
		"Invalid WUX image size: %llu\n",image_size);

    wux->sector_size = sector_size;
    wux->image_size  = image_size;
    wux->n_index     = (u32)(( image_size + sector_size - 1 ) / sector_size );

    const u64 tab_end = WUX_HEAD_SIZE + (u64)wux->n_index * sizeof(*wux->index);
    wux->data_offset = ( tab_end + sector_size - 1 ) / sector_size * sector_size;

    wux->index	  = CALLOC(wux->n_index,sizeof(*wux->index));
    wux->buf	  = MALLOC(sector_size);
    wux->cmp_buf  = MALLOC(sector_size);

    // One bucket per sector keeps the chains short; round up to a power of 2
    // so the modulo is a mask.
    wux->hash_size = 1024;
    while ( wux->hash_size < wux->n_index && wux->hash_size < 0x400000 )
	wux->hash_size *= 2;
    wux->hash_tab = MALLOC(wux->hash_size*sizeof(*wux->hash_tab));
    memset(wux->hash_tab,-1,wux->hash_size*sizeof(*wux->hash_tab));

    wux->hash_alloced = 1024;
    wux->hash_list = MALLOC(wux->hash_alloced*sizeof(*wux->hash_list));

    if ( fseeko(f,(off_t)wux->data_offset,SEEK_SET) )
	return ERROR1(ERR_WRITE_FAILED,"Can't seek to %llu: %s\n",
			wux->data_offset,wux->fname);
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

// Store one complete sector and return its stored index.  Identical sectors
// are stored only once; this is the entire point of the format.

static enumError store_sector ( WUX_t *wux, u32 image_index )
{
    const u32 sum = wux_checksum(wux->buf,wux->sector_size);
    const uint bucket = sum & wux->hash_size-1;

    for ( int i = wux->hash_tab[bucket]; i >= 0; i = wux->hash_list[i].next )
    {
	if ( wux->hash_list[i].sum != sum )
	    continue;

	// Candidate only: compare the real bytes before reusing it.
	const u64 off = wux->data_offset
		      + (u64)wux->hash_list[i].sector * wux->sector_size;
	const off_t save = ftello(wux->f);
	const enumError err
		= wux_read_at(wux,off,wux->cmp_buf,wux->sector_size);
	if (err)
	    return err;
	if ( fseeko(wux->f,save,SEEK_SET) )
	    return ERROR1(ERR_WRITE_FAILED,"Can't seek to %llu: %s\n",
			(u64)save,wux->fname);

	if (!memcmp(wux->cmp_buf,wux->buf,wux->sector_size))
	{
	    write_le32(wux->index+image_index,wux->hash_list[i].sector);
	    return ERR_OK;
	}
    }

    // Not seen before: append it.
    if ( fwrite(wux->buf,1,wux->sector_size,wux->f) != wux->sector_size )
	return ERROR1(ERR_WRITE_FAILED,"Can't write sector: %s\n",wux->fname);

    const u32 sector = wux->n_stored++;
    write_le32(wux->index+image_index,sector);

    if ( wux->hash_used == wux->hash_alloced )
    {
	wux->hash_alloced *= 2;
	wux->hash_list = REALLOC(wux->hash_list,
			wux->hash_alloced*sizeof(*wux->hash_list));
    }
    wux_hash_t *h = wux->hash_list + wux->hash_used;
    h->sum    = sum;
    h->sector = sector;
    h->next   = wux->hash_tab[bucket];
    wux->hash_tab[bucket] = wux->hash_used++;

    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError WriteWUX
(
    WUX_t		*wux,		// initialized by OpenWriteWUX()
    const void		*buf,		// source buffer
    size_t		count		// number of bytes to write
)
{
    DASSERT(wux);
    DASSERT(wux->writing);

    const u8 *src = buf;
    while ( count > 0 )
    {
	const u32 space = wux->sector_size - wux->buf_fill;
	const size_t now = count < space ? count : space;
	memcpy(wux->buf+wux->buf_fill,src,now);
	wux->buf_fill += now;
	src   += now;
	count -= now;

	if ( wux->buf_fill == wux->sector_size )
	{
	    const u32 idx = wux->n_written++;
	    if ( idx >= wux->n_index )
		return ERROR0(ERR_WRITE_FAILED,
			"More data written than announced: %s\n",wux->fname);
	    const enumError err = store_sector(wux,idx);
	    if (err)
		return err;
	    wux->buf_fill = 0;
	}
    }
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError TermWriteWUX
(
    WUX_t		*wux		// initialized by OpenWriteWUX()
)
{
    DASSERT(wux);
    DASSERT(wux->writing);

    // The image size need not be a multiple of the sector size, but a stored
    // sector always is: zero fill the tail before storing it.
    if ( wux->buf_fill )
    {
	memset(wux->buf+wux->buf_fill,0,wux->sector_size-wux->buf_fill);
	const u32 idx = wux->n_written++;
	if ( idx >= wux->n_index )
	    return ERROR0(ERR_WRITE_FAILED,
			"More data written than announced: %s\n",wux->fname);
	const enumError err = store_sector(wux,idx);
	if (err)
	    return err;
	wux->buf_fill = 0;
    }

    if ( wux->n_written != wux->n_index )
	return ERROR0(ERR_WRITE_FAILED,
		"Less data written than announced (%u of %u sectors): %s\n",
		wux->n_written, wux->n_index, wux->fname );

    u8 head[WUX_HEAD_SIZE];
    memset(head,0,sizeof(head));
    write_le32(head+0x00,WUX_MAGIC0);
    write_le32(head+0x04,WUX_MAGIC1);
    write_le32(head+0x08,wux->sector_size);
    write_le64(head+0x10,wux->image_size);

    if ( fseeko(wux->f,0,SEEK_SET)
      || fwrite(head,1,sizeof(head),wux->f) != sizeof(head)
      || fwrite(wux->index,sizeof(*wux->index),wux->n_index,wux->f) != wux->n_index )
	return ERROR1(ERR_WRITE_FAILED,"Can't write header: %s\n",wux->fname);

    return fflush(wux->f) ? ERROR1(ERR_WRITE_FAILED,"Flush failed: %s\n",wux->fname)
			  : ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
