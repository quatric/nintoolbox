// SPDX-License-Identifier: GPL-2.0+
// See lib-teszip.h for format notes.

#include <zlib.h>
#include <string.h>
#include "lib-std.h"
#include "lib-nintendo.h" // rd_le16() / rd_le32()
#include "lib-teszip.h"

//-----------------------------------------------------------------------------

int IsTEZip ( const u8 *data, size_t size )
{
    if ( !data || size < TE_ZIP_LOCAL_HEADER_SIZE )
	return 0;

    if ( rd_le32(data) != TE_ZIP_LOCAL_MAGIC )
	return 0;

    const u16 method = rd_le16(data + 8);
    if ( method != 0 && method != 8 )
	return 0;

    const u16 name_len = rd_le16(data + 26);
    if ( !name_len || name_len > 512 )
	return 0;

    if ( TE_ZIP_LOCAL_HEADER_SIZE + name_len > size )
	return 0;

    // Reject anything with the file name outside plain printable ASCII --
    // every sample on this title uses plain 7-bit names, and this keeps the
    // magic-less-neighbour risk (this is a real, documented magic, but stay
    // conservative) low.
    const u8 *name = data + TE_ZIP_LOCAL_HEADER_SIZE;
    for ( u16 i = 0; i < name_len; i++ )
	if ( name[i] < 0x20 || name[i] > 0x7e )
	    return 0;

    return 1;
}

//-----------------------------------------------------------------------------

static bool ListPush ( te_zip_list_t *list, const te_zip_entry_t *e )
{
    if ( list->n == list->n_alloc )
    {
	u32 new_alloc = list->n_alloc ? list->n_alloc * 2 : 16;
	te_zip_entry_t *new_entry = REALLOC( list->entry, new_alloc * sizeof(te_zip_entry_t) );
	if ( !new_entry )
	    return false;
	list->entry = new_entry;
	list->n_alloc = new_alloc;
    }
    list->entry[list->n++] = *e;
    return true;
}

//-----------------------------------------------------------------------------

enumError DecodeTEZip ( te_zip_list_t *list, const u8 *data, size_t size )
{
    if ( !list || !data )
	return ERR_INVALID_DATA;
    memset( list, 0, sizeof(*list) );

    size_t off = 0;
    while ( off + TE_ZIP_LOCAL_HEADER_SIZE <= size )
    {
	const u8 *lh = data + off;
	const u32 magic = rd_le32(lh);
	if ( magic != TE_ZIP_LOCAL_MAGIC )
	    break; // central directory / end-of-central-directory / trailing junk

	const u16 flags       = rd_le16(lh + 6);
	const u16 method      = rd_le16(lh + 8);
	const u32 crc32_val   = rd_le32(lh + 14);
	const u32 comp_size   = rd_le32(lh + 18);
	const u32 uncomp_size = rd_le32(lh + 22);
	const u16 name_len    = rd_le16(lh + 26);
	const u16 extra_len   = rd_le16(lh + 28);

	if ( flags & 8 )
	{
	    // Streamed entry (sizes in a trailing data descriptor instead of
	    // the local header): not observed on this title, and locating
	    // the descriptor without a central directory walk isn't safe.
	    // Stop rather than guess.
	    break;
	}

	if ( method != 0 && method != 8 )
	    break; // unsupported compression method

	if ( uncomp_size > 0x10000000 || comp_size > 0x10000000 )
	    break; // reject unreasonable allocation sizes (> 256MB)

	const u64 hdr_end64 = (u64)off + TE_ZIP_LOCAL_HEADER_SIZE + name_len + extra_len;
	if ( hdr_end64 + comp_size > size )
	    break; // truncated / malformed -- stop, keep what was already decoded
	const size_t hdr_end = (size_t)hdr_end64;

	te_zip_entry_t e;
	memset( &e, 0, sizeof(e) );
	const size_t copy_len = name_len < sizeof(e.name) - 1 ? name_len : sizeof(e.name) - 1;
	memcpy( e.name, data + off + TE_ZIP_LOCAL_HEADER_SIZE, copy_len );
	e.name[copy_len] = 0;
	e.method      = method;
	e.crc32       = crc32_val;
	e.comp_size   = comp_size;
	e.uncomp_size = uncomp_size;

	const u8 *payload = data + hdr_end;

	if ( method == 0 )
	{
	    if ( (u64)hdr_end + uncomp_size > size || uncomp_size != comp_size )
		break;
	    e.data = MALLOC( uncomp_size ? uncomp_size : 1 );
	    if ( !e.data )
		break;
	    memcpy( e.data, payload, uncomp_size );
	}
	else // method == 8, deflate (raw, no zlib/gzip wrapper)
	{
	    e.data = MALLOC( uncomp_size ? uncomp_size : 1 );
	    if ( !e.data )
		break;

	    z_stream zs;
	    memset( &zs, 0, sizeof(zs) );
	    zs.next_in   = (Bytef*)payload;
	    zs.avail_in  = comp_size;
	    zs.next_out  = (Bytef*)e.data;
	    zs.avail_out = uncomp_size;

	    if ( inflateInit2( &zs, -15 ) == Z_OK )
	    {
		inflate( &zs, Z_FINISH );
		inflateEnd( &zs );
	    }
	}

	if ( !ListPush( list, &e ) )
	{
	    FREE( e.data );
	    break;
	}
	off = hdr_end + comp_size;
    }

    return ERR_OK;
}

//-----------------------------------------------------------------------------

void FreeTEZipList ( te_zip_list_t *list )
{
    if ( !list )
	return;
    for ( uint i = 0; i < list->n; i++ )
	FREE( list->entry[i].data );
    FREE( list->entry );
    memset( list, 0, sizeof(*list) );
}

//-----------------------------------------------------------------------------

enumError DecodeTEZip_Text ( FILE *f, const u8 *data, size_t size )
{
    if ( !f || !data )
	return ERR_INVALID_DATA;

    te_zip_list_t list;
    enumError err = DecodeTEZip( &list, data, size );
    if ( err != ERR_OK )
	return err;

    fprintf( f, "# T&E Soft ZIP container: %u entr%s\n",
		list.n, list.n == 1 ? "y" : "ies" );
    for ( uint i = 0; i < list.n; i++ )
    {
	const te_zip_entry_t *e = list.entry + i;
	fprintf( f, "%3u. %-8s comp=%8u uncomp=%8u crc32=%08x  %s\n",
		i, e->method == 8 ? "deflate" : "store",
		e->comp_size, e->uncomp_size, e->crc32, e->name );
    }

    FreeTEZipList( &list );
    return ERR_OK;
}
