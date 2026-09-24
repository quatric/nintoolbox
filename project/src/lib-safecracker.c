// SPDX-License-Identifier: GPL-2.0+
// See lib-safecracker.h for format notes.

#include <string.h>
#include "lib-std.h"
#include "lib-nintendo.h" // rd_le16() / rd_le32()
#include "lib-safecracker.h"

//-----------------------------------------------------------------------------

int IsSafecrackerTOC ( const u8 *data, size_t size )
{
    if ( !data || size < SF_TOC_HEADER_SIZE )
	return 0;

    if ( data[SF_TOC_MAGIC_OFF] != 'L' || data[SF_TOC_MAGIC_OFF+1] != 'E' )
	return 0;

    const u32 slot_count = rd_le32(data);
    if ( !slot_count || slot_count > 0x10000 )
	return 0;

    if ( (u64)SF_TOC_HEADER_SIZE + (u64)slot_count * SF_TOC_RECORD_SIZE > size + SF_TOC_RECORD_SIZE )
	return 0; // wildly inconsistent with the actual file size

    if ( size >= SF_TOC_HEADER_SIZE + SF_TOC_RECORD_SIZE )
    {
	const u32 tag0 = rd_le32( data + SF_TOC_HEADER_SIZE + 4 );
	if ( tag0 != SF_TOC_TAG && tag0 != SF_TOC_SENTINEL_U32 )
	    return 0;
    }

    return 1;
}

//-----------------------------------------------------------------------------

static void ListPush ( sf_toc_t *toc, const sf_toc_entry_t *e )
{
    if ( toc->n == toc->n_alloc )
    {
	toc->n_alloc = toc->n_alloc ? toc->n_alloc * 2 : 16;
	toc->entry = REALLOC( toc->entry, toc->n_alloc * sizeof(sf_toc_entry_t) );
    }
    toc->entry[toc->n++] = *e;
}

//-----------------------------------------------------------------------------

enumError DecodeSafecrackerTOC ( sf_toc_t *toc, const u8 *data, size_t size )
{
    if ( !toc || !data )
	return ERR_INVALID_DATA;
    memset( toc, 0, sizeof(*toc) );

    if ( !IsSafecrackerTOC( data, size ) )
	return ERR_INVALID_DATA;

    toc->slot_count = rd_le32(data);
    toc->used_size  = rd_le32(data + 0x10);

    size_t off = SF_TOC_HEADER_SIZE;
    for ( u32 i = 0; i < toc->slot_count && off + SF_TOC_RECORD_SIZE <= size; i++, off += SF_TOC_RECORD_SIZE )
    {
	const u8 *rec = data + off;
	const u32 id     = rd_le32(rec);
	const u32 tag    = rd_le32(rec + 4);
	const u32 rsize  = rd_le32(rec + 8);
	const u32 roff   = rd_le32(rec + 12);

	if ( tag != SF_TOC_TAG )
	    break; // sentinel (0xAAAAAAAA fill) or malformed -- stop

	sf_toc_entry_t e;
	e.id     = id;
	e.size   = rsize;
	e.offset = roff;
	ListPush( toc, &e );
    }

    return ERR_OK;
}

//-----------------------------------------------------------------------------

void FreeSafecrackerTOC ( sf_toc_t *toc )
{
    if ( !toc )
	return;
    FREE( toc->entry );
    memset( toc, 0, sizeof(*toc) );
}

//-----------------------------------------------------------------------------

enumError DecodeSafecrackerTOC_Text ( FILE *f, const u8 *data, size_t size )
{
    if ( !f || !data )
	return ERR_INVALID_DATA;

    sf_toc_t toc;
    enumError err = DecodeSafecrackerTOC( &toc, data, size );
    if ( err != ERR_OK )
	return err;

    fprintf( f, "# Safecracker bigfile TOC: %u slot%s, %u real entr%s, used_size=%u\n",
		toc.slot_count, toc.slot_count == 1 ? "" : "s",
		toc.n, toc.n == 1 ? "y" : "ies", toc.used_size );
    for ( uint i = 0; i < toc.n; i++ )
    {
	const sf_toc_entry_t *e = toc.entry + i;
	fprintf( f, "%3u. id=%5u  offset=%9u  size=%8u  end=%9u\n",
		i, e->id, e->offset, e->size, e->offset + e->size );
    }

    FreeSafecrackerTOC( &toc );
    return ERR_OK;
}
