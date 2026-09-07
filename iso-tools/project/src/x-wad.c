
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
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "x-formats.h"
#include "libwbfs/file-formats.h"
#include "libwbfs/wiidisc.h"
#include "libwbfs/rijndael.h"
#include "crypto/wiimm-sha.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////		     installable Wii titles		///////////////
///////////////////////////////////////////////////////////////////////////////

// A WAD is a certificate chain, a ticket, a TMD and the title's contents,
// concatenated in that order and each padded to 64 bytes.  The contents are
// AES-CBC encrypted with the title key, which the ticket carries encrypted
// with the console's common key -- the same two step scheme the partitions
// of a Wii disc use, so wiidisc's helpers apply unchanged.

#define WAD_ALIGN		0x40
#define WAD_HEAD_SIZE		0x20
#define WAD_TYPE_IS		0x49730000	// 'Is\0\0', a normal title
#define WAD_TYPE_IB		0x69620000	// 'ib\0\0', a boot2 image
#define WAD_TICKET_SIZE		sizeof(wd_ticket_t)

// Names used inside an extracted directory.
#define WAD_FN_CERT		"cert.bin"
#define WAD_FN_CRL		"crl.bin"
#define WAD_FN_TICKET		"tik.bin"
#define WAD_FN_TMD		"tmd.bin"
#define WAD_FN_FOOTER		"footer.bin"
#define WAD_FN_TYPE		"wad-type.txt"

///////////////////////////////////////////////////////////////////////////////
// [[wad_header_t]]

typedef struct wad_header_t // big endian
{
  /* 0x00 */	u32 header_size;	// always WAD_HEAD_SIZE
  /* 0x04 */	u32 type;		// WAD_TYPE_IS or WAD_TYPE_IB
  /* 0x08 */	u32 cert_size;
  /* 0x0c */	u32 crl_size;
  /* 0x10 */	u32 tik_size;
  /* 0x14 */	u32 tmd_size;
  /* 0x18 */	u32 data_size;		// all contents together, each padded
  /* 0x1c */	u32 footer_size;
  /* 0x20 */
}
__attribute__ ((packed)) wad_header_t;

//
///////////////////////////////////////////////////////////////////////////////
///////////////			  small helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static u32 wad_align ( u32 size )
{
    return ( size + WAD_ALIGN - 1 ) / WAD_ALIGN * WAD_ALIGN;
}

///////////////////////////////////////////////////////////////////////////////

static u8 * wad_load ( ccp fname, u64 *size, bool silent )
{
    FILE *f = fopen(fname,"rb");
    if (!f)
    {
	if (!silent)
	    ERROR1(ERR_CANT_OPEN,"Can't open file: %s\n",fname);
	return 0;
    }

    struct stat st;
    if (fstat(fileno(f),&st))
    {
	fclose(f);
	if (!silent)
	    ERROR1(ERR_READ_FAILED,"Can't stat file: %s\n",fname);
	return 0;
    }

    u8 *data = MALLOC((size_t)st.st_size+1);
    if ( st.st_size && fread(data,1,st.st_size,f) != (size_t)st.st_size )
    {
	FREE(data);
	fclose(f);
	if (!silent)
	    ERROR1(ERR_READ_FAILED,"Can't read file: %s\n",fname);
	return 0;
    }
    fclose(f);
    data[st.st_size] = 0;
    if (size)
	*size = st.st_size;
    return data;
}

///////////////////////////////////////////////////////////////////////////////

static enumError wad_save ( ccp fname, const void *data, size_t size )
{
    enumError err = CreatePath(fname,false);
    if (err)
	return err;

    FILE *f = fopen(fname,"wb");
    if (!f)
	return ERROR1(ERR_CANT_CREATE,"Can't create file: %s\n",fname);
    const bool ok = !size || fwrite(data,1,size,f) == size;
    fclose(f);
    return ok ? ERR_OK : ERROR1(ERR_WRITE_FAILED,"Can't write file: %s\n",fname);
}

///////////////////////////////////////////////////////////////////////////////

// Set up the AES key and IV for one content.  The IV is the content index in
// the top two bytes and zero everywhere else.

static void content_iv ( u8 *iv, u16 index )
{
    memset(iv,0,WII_KEY_SIZE);
    iv[0] = index >> 8;
    iv[1] = index & 0xff;
}

///////////////////////////////////////////////////////////////////////////////

// Validate the header and locate the sections.  Returns the header with the
// sizes converted to host order, or an error.

static enumError wad_split
(
    const u8	*wad,		// the whole file
    u64		wad_size,	// its size
    ccp		fname,		// for error messages
    wad_header_t *head,		// filled with host order sizes
    u32		*offset		// filled with 6 section offsets, see below
)
{
    if ( wad_size < WAD_HEAD_SIZE )
	return ERROR0(ERR_INVALID_FILE,"File is too short: %s\n",fname);

    head->header_size = be32(wad+0x00);
    head->type	      = be32(wad+0x04);
    head->cert_size   = be32(wad+0x08);
    head->crl_size    = be32(wad+0x0c);
    head->tik_size    = be32(wad+0x10);
    head->tmd_size    = be32(wad+0x14);
    head->data_size   = be32(wad+0x18);
    head->footer_size = be32(wad+0x1c);

    if ( head->header_size != WAD_HEAD_SIZE )
	return ERROR0(ERR_INVALID_FILE,
		"Unexpected WAD header size 0x%x: %s\n",head->header_size,fname);
    if ( head->type != WAD_TYPE_IS && head->type != WAD_TYPE_IB )
	return ERROR0(ERR_INVALID_FILE,
		"Unknown WAD type 0x%08x: %s\n",head->type,fname);

    // Sections follow each other, each starting on a 64 byte boundary.
    u64 off = wad_align(head->header_size);
    const u32 size[6] =
    {
	head->cert_size, head->crl_size, head->tik_size,
	head->tmd_size,  head->data_size, head->footer_size
    };
    for ( int i = 0; i < 6; i++ )
    {
	offset[i] = (u32)off;
	off += wad_align(size[i]);
	if ( off > wad_size )
	    return ERROR0(ERR_INVALID_FILE,
		"WAD section %u ends beyond the file (%llu > %llu): %s\n",
		i, off, wad_size, fname );
    }
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     XINFO			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XInfoWAD ( ccp source )
{
    u64 size = 0;
    u8 *wad = wad_load(source,&size,false);
    if (!wad)
	return ERR_CANT_OPEN;

    wad_header_t head;
    u32 offset[6];
    enumError err = wad_split(wad,size,source,&head,offset);
    if (err)
	goto abort;

    {
	char type[5];
	write_be32(type,head.type);
	type[2] = type[3] = 0;
	printf("      type:     %s\n",type);
	printf("      sections: cert 0x%x, crl 0x%x, tik 0x%x,"
		" tmd 0x%x, data 0x%x, footer 0x%x\n",
		head.cert_size, head.crl_size, head.tik_size,
		head.tmd_size, head.data_size, head.footer_size );
    }

    if ( head.tmd_size >= sizeof(wd_tmd_t) )
    {
	const wd_tmd_t *tmd = (wd_tmd_t*)( wad + offset[3] );
	const uint n = ntohs(tmd->n_content);

	printf("      title id: %02x%02x%02x%02x-%.4s\n",
		tmd->title_id[0], tmd->title_id[1],
		tmd->title_id[2], tmd->title_id[3], tmd->title_id+4 );
	printf("      version:  %u, ios %u, %u content%s\n",
		ntohs(tmd->title_version),
		(uint)( ntoh64(tmd->sys_version) & 0xffffffff ),
		n, n == 1 ? "" : "s" );

	if ( head.tmd_size >= sizeof(wd_tmd_t) + n*sizeof(wd_tmd_content_t) )
	    for ( uint i = 0; i < n; i++ )
	    {
		const wd_tmd_content_t *c = tmd->content + i;
		printf("        %2u: id %08x  type 0x%04x  %llu bytes\n",
			ntohs(c->index), ntohl(c->content_id),
			ntohs(c->type), ntoh64(c->size) );
	    }
    }

 abort:
    FREE(wad);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   XEXTRACT			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XExtractWAD ( ccp source, ccp dest )
{
    u64 size = 0;
    u8 *wad = wad_load(source,&size,false);
    if (!wad)
	return ERR_CANT_OPEN;

    char buf[PATH_MAX];
    u8 *plain = 0;
    wad_header_t head;
    u32 offset[6];
    enumError err = wad_split(wad,size,source,&head,offset);
    if (err)
	goto abort;

    err = CreatePath(dest,true);
    if (err)
	goto abort;

    //--- the fixed sections are copied out verbatim

    PathCatPP(buf,sizeof(buf),dest,WAD_FN_CERT);
    err = wad_save(buf,wad+offset[0],head.cert_size);
    if ( !err && head.crl_size )
    {
	PathCatPP(buf,sizeof(buf),dest,WAD_FN_CRL);
	err = wad_save(buf,wad+offset[1],head.crl_size);
    }
    if (!err)
    {
	PathCatPP(buf,sizeof(buf),dest,WAD_FN_TICKET);
	err = wad_save(buf,wad+offset[2],head.tik_size);
    }
    if (!err)
    {
	PathCatPP(buf,sizeof(buf),dest,WAD_FN_TMD);
	err = wad_save(buf,wad+offset[3],head.tmd_size);
    }
    if ( !err && head.footer_size )
    {
	PathCatPP(buf,sizeof(buf),dest,WAD_FN_FOOTER);
	err = wad_save(buf,wad+offset[5],head.footer_size);
    }
    if (err)
	goto abort;

    {
	// The type is the one header field that is not implied by the files,
	// so it is written out as text to survive the round trip.
	char type[8];
	snprintf(type,sizeof(type),"%c%c\n",head.type>>24,head.type>>16&0xff);
	PathCatPP(buf,sizeof(buf),dest,WAD_FN_TYPE);
	err = wad_save(buf,type,strlen(type));
	if (err)
	    goto abort;
    }

    //--- the contents, decrypted

    if ( head.tik_size < WAD_TICKET_SIZE || head.tmd_size < sizeof(wd_tmd_t) )
    {
	err = ERROR0(ERR_INVALID_FILE,
		"Ticket or TMD is too short: %s\n",source);
	goto abort;
    }

    {
	const wd_ticket_t *tik = (wd_ticket_t*)( wad + offset[2] );
	const wd_tmd_t *tmd = (wd_tmd_t*)( wad + offset[3] );
	const uint n = ntohs(tmd->n_content);

	if ( head.tmd_size < sizeof(wd_tmd_t) + n*sizeof(wd_tmd_content_t) )
	{
	    err = ERROR0(ERR_INVALID_FILE,
		"TMD is too short for %u contents: %s\n",n,source);
	    goto abort;
	}

	u8 title_key[WII_KEY_SIZE];
	wd_decrypt_title_key(tik,title_key);
	aes_key_t akey;
	wd_aes_set_key(&akey,title_key);

	u32 pos = offset[4];
	for ( uint i = 0; i < n && !err; i++ )
	{
	    const wd_tmd_content_t *c = tmd->content + i;
	    const u64 csize = ntoh64(c->size);
	    // Only whole AES blocks are stored, so the encrypted content is
	    // the declared size rounded up to 16 bytes.
	    const u64 esize = ( csize + WII_KEY_SIZE - 1 )
			    / WII_KEY_SIZE * WII_KEY_SIZE;

	    if ( pos + esize > size )
	    {
		err = ERROR0(ERR_INVALID_FILE,
			"Content %u ends beyond the file: %s\n",i,source);
		break;
	    }

	    plain = MALLOC(esize?esize:1);
	    u8 iv[WII_KEY_SIZE];
	    content_iv(iv,ntohs(c->index));
	    wd_aes_decrypt(&akey,iv,wad+pos,plain,esize);

	    // The TMD hash is over the decrypted content and is the only
	    // check that the title key was right, so report it.
	    u8 hash[WII_HASH_SIZE];
	    SHA1(plain,csize,hash);
	    if (memcmp(hash,c->hash,sizeof(hash)))
		ERROR0(ERR_WARNING,
		    "Content %u (id %08x) does not match its TMD hash;"
		    " the common key may be wrong.\n",i,ntohl(c->content_id));

	    char name[32];
	    snprintf(name,sizeof(name),"%08x.app",ntohl(c->content_id));
	    PathCatPP(buf,sizeof(buf),dest,name);
	    err = wad_save(buf,plain,csize);

	    FREE(plain);
	    plain = 0;
	    pos += wad_align((u32)esize);
	}
    }

    if ( !err && verbose >= 0 )
	printf("  extracted %s -> %s\n",source,dest);

 abort:
    FREE(plain);
    FREE(wad);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    XCREATE			///////////////
///////////////////////////////////////////////////////////////////////////////

// Append 'size' bytes and pad the result to the 64 byte section grid.

static void wad_put ( FILE *f, const void *data, u32 size, u32 *total )
{
    static const u8 zero[WAD_ALIGN] = {0};
    if (size)
	fwrite(data,1,size,f);
    const u32 pad = wad_align(size) - size;
    if (pad)
	fwrite(zero,1,pad,f);
    if (total)
	*total += wad_align(size);
}

///////////////////////////////////////////////////////////////////////////////

enumError XCreateWAD ( ccp source, ccp dest )
{
    char buf[PATH_MAX];
    enumError err = ERR_OK;

    u8 *cert = 0, *crl = 0, *tik_data = 0, *tmd_data = 0, *footer = 0;
    u8 *tmd_orig = 0;
    u64 cert_size = 0, crl_size = 0, tik_size = 0, tmd_size = 0, footer_size = 0;
    FILE *out = 0;

    PathCatPP(buf,sizeof(buf),source,WAD_FN_CERT);
    cert = wad_load(buf,&cert_size,false);
    PathCatPP(buf,sizeof(buf),source,WAD_FN_TICKET);
    tik_data = wad_load(buf,&tik_size,false);
    PathCatPP(buf,sizeof(buf),source,WAD_FN_TMD);
    tmd_data = wad_load(buf,&tmd_size,false);
    if ( !cert || !tik_data || !tmd_data )
    {
	err = ERR_CANT_OPEN;
	goto abort;
    }

    PathCatPP(buf,sizeof(buf),source,WAD_FN_CRL);
    crl = wad_load(buf,&crl_size,true);
    PathCatPP(buf,sizeof(buf),source,WAD_FN_FOOTER);
    footer = wad_load(buf,&footer_size,true);

    if ( tik_size < WAD_TICKET_SIZE || tmd_size < sizeof(wd_tmd_t) )
    {
	err = ERROR0(ERR_INVALID_FILE,"Ticket or TMD is too short: %s\n",source);
	goto abort;
    }

    u32 type = WAD_TYPE_IS;
    {
	u64 tsize = 0;
	PathCatPP(buf,sizeof(buf),source,WAD_FN_TYPE);
	u8 *tdata = wad_load(buf,&tsize,true);
	if ( tdata && tsize >= 2 )
	    type = tdata[0] << 24 | tdata[1] << 16;
	FREE(tdata);
    }

    const wd_ticket_t *tik = (wd_ticket_t*)tik_data;
    wd_tmd_t *tmd = (wd_tmd_t*)tmd_data;
    const uint n = ntohs(tmd->n_content);
    if ( tmd_size < sizeof(wd_tmd_t) + n*sizeof(wd_tmd_content_t) )
    {
	err = ERROR0(ERR_INVALID_FILE,
		"TMD is too short for %u contents: %s\n",n,source);
	goto abort;
    }

    // Keep the TMD as it was: if nothing about the contents changed it must
    // be written back untouched, or a genuinely signed title would lose its
    // signature to a rebuild that changed nothing.
    tmd_orig = MALLOC(tmd_size);
    memcpy(tmd_orig,tmd_data,tmd_size);

    u8 title_key[WII_KEY_SIZE];
    wd_decrypt_title_key(tik,title_key);
    aes_key_t akey;
    wd_aes_set_key(&akey,title_key);

    out = fopen(dest,"wb");
    if (!out)
    {
	err = ERROR1(ERR_CANT_CREATE,"Can't create file: %s\n",dest);
	goto abort;
    }

    // The header holds the data size, which is only known after all contents
    // are written, so start behind it and come back at the end.
    fseeko(out,wad_align(WAD_HEAD_SIZE),SEEK_SET);
    wad_put(out,cert,cert_size,0);
    wad_put(out,crl,crl_size,0);
    wad_put(out,tik_data,tik_size,0);
    wad_put(out,tmd_data,tmd_size,0);

    u32 data_size = 0;
    for ( uint i = 0; i < n && !err; i++ )
    {
	wd_tmd_content_t *c = tmd->content + i;

	char name[32];
	snprintf(name,sizeof(name),"%08x.app",ntohl(c->content_id));
	PathCatPP(buf,sizeof(buf),source,name);

	u64 csize = 0;
	u8 *plain = wad_load(buf,&csize,false);
	if (!plain)
	{
	    err = ERR_CANT_OPEN;
	    break;
	}

	// The TMD describes the content, so it has to follow it: a rebuilt
	// content of a different size or with different bytes would
	// otherwise fail the console's own check.
	if ( csize != ntoh64(c->size) )
	{
	    c->size = hton64(csize);
	    if ( verbose >= 0 )
		printf("  content %u: size updated to %llu\n",i,csize);
	}
	SHA1(plain,csize,c->hash);

	const u64 esize = ( csize + WII_KEY_SIZE - 1 )
			/ WII_KEY_SIZE * WII_KEY_SIZE;
	u8 *enc = CALLOC(esize?esize:1,1);
	memcpy(enc,plain,csize);
	u8 iv[WII_KEY_SIZE];
	content_iv(iv,ntohs(c->index));
	wd_aes_encrypt(&akey,iv,enc,enc,esize);

	wad_put(out,enc,(u32)esize,&data_size);
	FREE(enc);
	FREE(plain);
    }
    if (err)
	goto abort;

    wad_put(out,footer,footer_size,0);

    // Only a TMD that actually changed needs rewriting, and a changed TMD no
    // longer matches its signature, so it has to be fake signed to stay
    // installable.
    if (memcmp(tmd_orig,tmd_data,tmd_size))
    {
	tmd_fake_sign(tmd,tmd_size);
	if ( verbose >= 0 )
	    printf("  TMD updated and fake signed\n");
	fseeko(out,wad_align(WAD_HEAD_SIZE)
		  + wad_align(cert_size) + wad_align(crl_size)
		  + wad_align(tik_size), SEEK_SET );
	wad_put(out,tmd_data,tmd_size,0);
    }

    {
	u8 head[WAD_HEAD_SIZE];
	memset(head,0,sizeof(head));
	write_be32(head+0x00,WAD_HEAD_SIZE);
	write_be32(head+0x04,type);
	write_be32(head+0x08,cert_size);
	write_be32(head+0x0c,crl_size);
	write_be32(head+0x10,tik_size);
	write_be32(head+0x14,tmd_size);
	write_be32(head+0x18,data_size);
	write_be32(head+0x1c,footer_size);

	fseeko(out,0,SEEK_SET);
	wad_put(out,head,sizeof(head),0);
    }

    if ( ferror(out) )
	err = ERROR1(ERR_WRITE_FAILED,"Write failed: %s\n",dest);
    else if ( verbose >= 0 )
	printf("  created %s: %u content%s\n",dest,n,n==1?"":"s");

 abort:
    if (out)
	fclose(out);
    if ( err && out )
	unlink(dest);
    FREE(footer);
    FREE(tmd_orig);
    FREE(tmd_data);
    FREE(tik_data);
    FREE(crl);
    FREE(cert);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
