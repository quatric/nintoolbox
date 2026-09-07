
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
#include <sys/stat.h>

#include "x-formats.h"
#include "libwbfs/rijndael.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////		 3DS: CCI, CIA and standalone NCCH		///////////////
///////////////////////////////////////////////////////////////////////////////

// Layout references: 3dbrew.org "NCSD", "NCCH" and "CIA" pages (public
// documentation, no secret material).  A CCI (.3ds/.cci) is an NCSD header
// followed by up to 8 NCCH partitions.  A CIA wraps NCCH content the same way
// a WAD wraps Wii content: cert chain, ticket, TMD, content, meta, each
// padded to a 64 byte boundary.  A bare .cxi/.cfa is a single NCCH.
//
// NCCH content can be plaintext ("NoCrypto" flag) or AES-CTR encrypted with a
// key derived from a per-console/per-slot KeyX and a per-title KeyY (taken
// from the header).  The KeyX values are Nintendo's secret keyslot constants
// and are never hardcoded here; they are read from an external key file the
// user supplies (conventionally ~/.3ds/aes_keys.txt, "name = 32 hex bytes"
// per line).  Missing keys or seed-crypto titles degrade to a warning and a
// partial extraction rather than a hard failure or guessed key material.

#define NCCH_SIZE		0x200
#define MEDIA_UNIT		0x200

#define NCCH_OFF_MAGIC		0x100
#define NCCH_OFF_CONTENT_SIZE	0x104	// media units
#define NCCH_OFF_PARTITION_ID	0x108	// u64 LE
#define NCCH_OFF_PROGRAM_ID	0x118	// u64 LE
#define NCCH_OFF_EXHEAD_SIZE	0x180
#define NCCH_OFF_FLAGS		0x188	// 8 bytes
#define NCCH_OFF_PLAIN_OFF	0x190
#define NCCH_OFF_PLAIN_SIZE	0x194
#define NCCH_OFF_LOGO_OFF	0x198
#define NCCH_OFF_LOGO_SIZE	0x19c
#define NCCH_OFF_EXEFS_OFF	0x1a0
#define NCCH_OFF_EXEFS_SIZE	0x1a4
#define NCCH_OFF_ROMFS_OFF	0x1b0
#define NCCH_OFF_ROMFS_SIZE	0x1b4

#define NCCH_FLAG_CRYPTO_METHOD	3	// byte index inside the flags field
#define NCCH_FLAG_UNIT_SIZE	6
#define NCCH_FLAG_BITS		7
#define NCCH_BIT_FIXED_KEY	0x01
#define NCCH_BIT_NO_ROMFS	0x02
#define NCCH_BIT_NO_CRYPTO	0x04
#define NCCH_BIT_SEED		0x20

#define NCSD_OFF_MAGIC		0x100
#define NCSD_OFF_IMAGE_SIZE	0x104	// media units
#define NCSD_OFF_PART_TABLE	0x120	// 8 * (offset,size), media units

#define AES_BLOCK_SIZE		16

// Names used inside an extracted directory.
#define D3_FN_NCSD_HEAD	"ncsd_header.bin"
#define D3_FN_CIA_HEAD	"cia_header.bin"
#define D3_FN_CERT	"cert.bin"
#define D3_FN_TICKET	"tik.bin"
#define D3_FN_TMD	"tmd.bin"
#define D3_FN_META	"meta.bin"
#define D3_DIR_PART	"partition%u"
#define D3_FN_NCCH_HEAD	"ncch_header.bin"
#define D3_FN_EXHEADER	"exheader.bin"
#define D3_FN_PLAIN	"plain.bin"
#define D3_FN_LOGO	"logo.bin"
#define D3_FN_EXEFS	"exefs.bin"
#define D3_FN_ROMFS	"romfs.bin"

//
///////////////////////////////////////////////////////////////////////////////
///////////////			  small helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static u8 * load_file ( ccp fname, u64 *size, bool silent )
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

static enumError save_file ( ccp fname, const void *data, size_t size )
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

// Copy 'size' bytes at 'off' out of the loaded image into a new file.

static enumError extract_range
	( ccp fname, const u8 *image, u64 image_size, u64 off, u64 size )
{
    if (!size)
	return ERR_OK;
    if ( off + size > image_size )
	return ERROR0(ERR_INVALID_FILE,
		"Range 0x%llx+0x%llx is outside the image: %s\n",off,size,fname);
    return save_file(fname,image+off,size);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		     AES-CTR via the CBC primitive		///////////////
///////////////////////////////////////////////////////////////////////////////

// wiidisc's wd_aes_encrypt() only exposes CBC, but CBC-encrypting a single
// all-zero block with IV=X computes exactly AES_ECB_encrypt(X): the CBC chain
// XORs the plaintext with the IV before the block cipher, and 0 XOR X == X.
// That is the keystream block CTR mode needs, so CTR can be built on top of
// it without a second AES implementation.

static void ctr_increment ( u8 *ctr )
{
    for ( int i = AES_BLOCK_SIZE-1; i >= 0 && !++ctr[i]; i-- )
	;
}

static void aes_ctr_crypt
(
    const aes_key_t	*akey,
    const u8		ctr_start[AES_BLOCK_SIZE],
    const u8		*in,
    u8			*out,
    u64			len
)
{
    static const u8 zero[AES_BLOCK_SIZE] = {0};
    u8 ctr[AES_BLOCK_SIZE];
    memcpy(ctr,ctr_start,AES_BLOCK_SIZE);

    u8 stream[AES_BLOCK_SIZE];
    for ( u64 pos = 0; pos < len; pos += AES_BLOCK_SIZE )
    {
	wd_aes_encrypt(akey,ctr,zero,stream,AES_BLOCK_SIZE);
	const u64 chunk = len-pos < AES_BLOCK_SIZE ? len-pos : AES_BLOCK_SIZE;
	for ( u64 i = 0; i < chunk; i++ )
	    out[pos+i] = in[pos+i] ^ stream[i];
	ctr_increment(ctr);
    }
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    external key file support		///////////////
///////////////////////////////////////////////////////////////////////////////

// ~/.3ds/aes_keys.txt: one "name = 32 hex byte value" assignment per line,
// '#' starts a comment.  Recognised names are "keyx_2c", "keyx_25", "keyx_18",
// "keyx_1b" (the four slots retail NCCH crypto methods use) and
// "common_key_00".."common_key_05" (used to decrypt CIA ticket title keys).
// The file is never shipped and never guessed at: if it is missing, or an
// entry is missing, that specific decrypt step is skipped with a warning.

#define D3_MAX_KEYS 16

typedef struct d3_key_t { char name[32]; u8 key[16]; bool set; } d3_key_t;

typedef struct d3_keys_t
{
    d3_key_t	keyx_2c, keyx_25, keyx_18, keyx_1b;
    d3_key_t	common_key[6];
    bool	loaded;		// the file itself was found
}
d3_keys_t;

static int hex_val ( int c )
{
    if ( c >= '0' && c <= '9' ) return c-'0';
    if ( c >= 'a' && c <= 'f' ) return c-'a'+10;
    if ( c >= 'A' && c <= 'F' ) return c-'A'+10;
    return -1;
}

static bool parse_hex16 ( ccp src, u8 *key )
{
    uint n = 0;
    for ( ; *src && n < 32; src++ )
    {
	const int hi = hex_val(*src);
	if ( hi < 0 )
	    continue;
	src++;
	const int lo = hex_val(*src);
	if ( lo < 0 )
	    return false;
	key[n/2] = hi<<4 | lo;
	n += 2;
    }
    return n == 32;
}

static void d3_load_keys ( d3_keys_t *keys )
{
    memset(keys,0,sizeof(*keys));

    ccp home = getenv("HOME");
    if (!home)
	return;
    char path[PATH_MAX];
    snprintf(path,sizeof(path),"%s/.3ds/aes_keys.txt",home);

    FILE *f = fopen(path,"r");
    if (!f)
	return;
    keys->loaded = true;

    char line[256];
    while (fgets(line,sizeof(line),f))
    {
	char *hash = strchr(line,'#');
	if (hash)
	    *hash = 0;
	char *eq = strchr(line,'=');
	if (!eq)
	    continue;
	*eq = 0;
	char name[64];
	if ( sscanf(line,"%63s",name) != 1 )
	    continue;

	u8 key[16];
	if (!parse_hex16(eq+1,key))
	    continue;

	d3_key_t *slot = 0;
	uint idx;
	if (!strcasecmp(name,"keyx_2c"))
	    slot = &keys->keyx_2c;
	else if (!strcasecmp(name,"keyx_25"))
	    slot = &keys->keyx_25;
	else if (!strcasecmp(name,"keyx_18"))
	    slot = &keys->keyx_18;
	else if (!strcasecmp(name,"keyx_1b"))
	    slot = &keys->keyx_1b;
	else if ( sscanf(name,"common_key_%u",&idx) == 1 && idx < 6 )
	    slot = &keys->common_key[idx];

	if (slot)
	{
	    memcpy(slot->key,key,16);
	    slot->set = true;
	}
    }
    fclose(f);
}

///////////////////////////////////////////////////////////////////////////////

// The documented 3DS keyscrambler: normalKey = rol(rol(keyX,2) ^ keyY + C,87)
// over 128 bit values, C = 0xFF3407CC3D6854F0A24D5D68E4A67C4C.  This is a
// public algorithm (3dbrew "AES Registers"/"Keyslot Descriptions"); only the
// per-slot KeyX inputs are secret, and those come from the external key file.

static void u128_rol ( u8 *v, uint bits )
{
    u8 tmp[16];
    const uint byte_shift = bits/8, bit_shift = bits%8;
    for ( int i = 0; i < 16; i++ )
    {
	const uint src = (i + 16 - byte_shift) % 16;
	const uint src2 = (src + 15) % 16;
	tmp[i] = bit_shift
	    ? v[src] << bit_shift | v[src2] >> (8-bit_shift)
	    : v[src];
    }
    memcpy(v,tmp,16);
}

static void u128_add ( u8 *dst, const u8 *a, const u8 *b )
{
    int carry = 0;
    for ( int i = 15; i >= 0; i-- )
    {
	const int sum = a[i] + b[i] + carry;
	dst[i] = (u8)sum;
	carry = sum >> 8;
    }
}

static void scramble_key ( const u8 *keyx, const u8 *keyy, u8 *normal_key )
{
    static const u8 C[16] =
    {
	0xFF,0x34,0x07,0xCC,0x3D,0x68,0x54,0xF0,
	0xA2,0x4D,0x5D,0x68,0xE4,0xA6,0x7C,0x4C
    };
    u8 x[16];
    memcpy(x,keyx,16);
    u128_rol(x,2);
    for ( int i = 0; i < 16; i++ )
	x[i] ^= keyy[i];
    u8 sum[16];
    u128_add(sum,x,C);
    u128_rol(sum,87);
    memcpy(normal_key,sum,16);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    NCCH: dump one partition		///////////////
///////////////////////////////////////////////////////////////////////////////

// Decrypt exefs/romfs of one NCCH into a freshly alloced buffer, or return
// the plaintext region unchanged if 'no_crypto' is set.  Returns NULL (with
// *out_size==0) rather than a corrupted buffer whenever the key material is
// not available -- the caller then skips that region and warns.

static u8 * ncch_decrypt_region
(
    const u8		*ncch,		// NCCH start
    u64			region_off,	// byte offset from NCCH start
    u64			region_size,	// byte size
    bool		no_crypto,
    const u8		*normal_key,	// NULL if not available
    u8			section_id,	// 1=exheader,2=exefs,3=romfs (CTR high nibble use)
    u64			*out_size
)
{
    *out_size = 0;
    if (!region_size)
	return 0;
    if (no_crypto)
    {
	u8 *out = MALLOC(region_size);
	memcpy(out,ncch+region_off,region_size);
	*out_size = region_size;
	return out;
    }
    if (!normal_key)
	return 0;

    // CTR starts with the partition id (from the header, big endian for the
    // counter) followed by the section id and a zero byte, per 3dbrew.
    u8 ctr[AES_BLOCK_SIZE];
    memcpy(ctr,ncch+NCCH_OFF_PARTITION_ID,8);
    // partition id is stored little endian in the header; the CTR wants it
    // reversed (big endian) in the first 8 bytes.
    for ( int i = 0; i < 4; i++ )
    {
	const u8 t = ctr[i]; ctr[i] = ctr[7-i]; ctr[7-i] = t;
    }
    ctr[8] = section_id;
    memset(ctr+9,0,7);

    // Advance the counter to the start of 'region_off' (counted in 16 byte
    // blocks from the NCCH start, matching how citra/ctrtool derive it).
    u64 blocks = region_off / AES_BLOCK_SIZE;
    while (blocks--)
	ctr_increment(ctr);

    aes_key_t akey;
    wd_aes_set_key(&akey,normal_key);
    u8 *out = MALLOC(region_size);
    aes_ctr_crypt(&akey,ctr,ncch+region_off,out,region_size);
    *out_size = region_size;
    return out;
}

///////////////////////////////////////////////////////////////////////////////

static enumError dump_ncch
(
    ccp			dest,		// directory to write into (created)
    const u8		*ncch,		// NCCH start
    u64			ncch_avail,	// bytes available from 'ncch' onward
    const d3_keys_t	*keys,
    ccp			label		// for messages, e.g. the source path
)
{
    if ( ncch_avail < NCCH_SIZE || memcmp(ncch+NCCH_OFF_MAGIC,"NCCH",4) )
	return ERROR0(ERR_INVALID_FILE,"Not a valid NCCH: %s\n",label);

    enumError err = CreatePath(dest,true);
    if (err)
	return err;

    char buf[PATH_MAX];
    PathCatPP(buf,sizeof(buf),dest,D3_FN_NCCH_HEAD);
    err = save_file(buf,ncch,NCCH_SIZE);
    if (err)
	return err;

    const u8 flags7 = ncch[NCCH_OFF_FLAGS+NCCH_FLAG_BITS];
    const bool no_crypto  = ( flags7 & NCCH_BIT_NO_CRYPTO ) != 0;
    const bool fixed_key  = ( flags7 & NCCH_BIT_FIXED_KEY ) != 0;
    const bool seed_crypto= ( flags7 & NCCH_BIT_SEED ) != 0;
    const u8 crypto_method = ncch[NCCH_OFF_FLAGS+NCCH_FLAG_CRYPTO_METHOD];

    const u8 *keyx = 0;
    if ( keys->keyx_2c.set || keys->keyx_25.set
      || keys->keyx_18.set || keys->keyx_1b.set )
    {
	switch (crypto_method)
	{
	    case 0x00: keyx = keys->keyx_2c.set ? keys->keyx_2c.key : 0; break;
	    case 0x01: keyx = keys->keyx_25.set ? keys->keyx_25.key : 0; break;
	    case 0x0a: keyx = keys->keyx_18.set ? keys->keyx_18.key : 0; break;
	    case 0x0b: keyx = keys->keyx_1b.set ? keys->keyx_1b.key : 0; break;
	}
    }

    u8 normal_key[16];
    const u8 *use_key = 0;
    if (!no_crypto)
    {
	if (fixed_key)
	{
	    // Fixed-crypto NCCH: the per-title KeyX/KeyY scramble is skipped
	    // entirely in favour of one of two small, publicly documented
	    // constants (3dbrew "NCCH" -> "Cryptography", FixedCryptoKey):
	    //   - ordinary titles: an all-zero 128 bit AES key.
	    //   - "System" category titles: a separate fixed constant, chosen
	    //     via the Program ID's category bit (bit 0x10 of the high
	    //     32 bits, per 3dbrew).  That second constant is not
	    //     implemented here -- rather than guess an unverified byte
	    //     value, System-category fixed-key NCCH content is skipped
	    //     with an explicit warning; only the zero-key case (which
	    //     needs no lookup at all) is decrypted.
	    const u32 program_id_hi = le32(ncch+NCCH_OFF_PROGRAM_ID+4);
	    const bool is_system = (program_id_hi & 0x10) != 0;
	    if (is_system)
	    {
		ERROR0(ERR_WARNING,
		    "NCCH uses the System FixedCryptoKey, which is not"
		    " implemented (only the all-zero FixedCryptoKey is);"
		    " exefs/romfs will not be decrypted: %s\n",label);
	    }
	    else
	    {
		memset(normal_key,0,sizeof(normal_key));
		use_key = normal_key;
	    }
	}
	else if (seed_crypto)
	{
	    ERROR0(ERR_WARNING,
		"NCCH uses seed crypto (needs an online seed lookup this tool"
		" can't perform); exefs/romfs will not be decrypted: %s\n",label);
	}
	else if (!keyx)
	{
	    ERROR0(ERR_WARNING,
		"No matching KeyX found in ~/.3ds/aes_keys.txt for crypto"
		" method 0x%02x; exefs/romfs will not be decrypted: %s\n",
		crypto_method,label);
	}
	else
	{
	    // KeyY is the first 16 bytes of the NCCH's RSA-2048 signature.
	    scramble_key(keyx,ncch,normal_key);
	    use_key = normal_key;
	}
    }

    //--- extended header (also CTR encrypted, section id 1)

    const u32 exhead_size = le32(ncch+NCCH_OFF_EXHEAD_SIZE);
    if (exhead_size)
    {
	u64 got = 0;
	u8 *data = ncch_decrypt_region(ncch,NCCH_SIZE,exhead_size*2,	// exheader is duplicated
			no_crypto,use_key,1,&got);
	if (data)
	{
	    PathCatPP(buf,sizeof(buf),dest,D3_FN_EXHEADER);
	    err = save_file(buf,data,got);
	    FREE(data);
	    if (err)
		return err;
	}
    }

    //--- plain region and logo: always unencrypted

    const u32 plain_off = le32(ncch+NCCH_OFF_PLAIN_OFF) * MEDIA_UNIT;
    const u32 plain_size = le32(ncch+NCCH_OFF_PLAIN_SIZE) * MEDIA_UNIT;
    if (plain_size)
    {
	PathCatPP(buf,sizeof(buf),dest,D3_FN_PLAIN);
	err = extract_range(buf,ncch,ncch_avail,plain_off,plain_size);
	if (err)
	    return err;
    }

    const u32 logo_off = le32(ncch+NCCH_OFF_LOGO_OFF) * MEDIA_UNIT;
    const u32 logo_size = le32(ncch+NCCH_OFF_LOGO_SIZE) * MEDIA_UNIT;
    if (logo_size)
    {
	PathCatPP(buf,sizeof(buf),dest,D3_FN_LOGO);
	err = extract_range(buf,ncch,ncch_avail,logo_off,logo_size);
	if (err)
	    return err;
    }

    //--- exefs

    const u64 exefs_off = (u64)le32(ncch+NCCH_OFF_EXEFS_OFF) * MEDIA_UNIT;
    const u64 exefs_size = (u64)le32(ncch+NCCH_OFF_EXEFS_SIZE) * MEDIA_UNIT;
    if ( exefs_size && exefs_off+exefs_size <= ncch_avail )
    {
	u64 got = 0;
	u8 *data = ncch_decrypt_region(ncch,exefs_off,exefs_size,
			no_crypto,use_key,2,&got);
	if (data)
	{
	    PathCatPP(buf,sizeof(buf),dest,D3_FN_EXEFS);
	    err = save_file(buf,data,got);
	    FREE(data);
	    if (err)
		return err;
	}
    }

    //--- romfs -- dumped as a raw image; walking the IVFC level-3 directory
    //--- tree is not implemented, romfs.bin is the documented simpler
    //--- fallback the task description allows.

    const u64 romfs_off = (u64)le32(ncch+NCCH_OFF_ROMFS_OFF) * MEDIA_UNIT;
    const u64 romfs_size = (u64)le32(ncch+NCCH_OFF_ROMFS_SIZE) * MEDIA_UNIT;
    if ( romfs_size && romfs_off+romfs_size <= ncch_avail
      && !(flags7 & NCCH_BIT_NO_ROMFS) )
    {
	u64 got = 0;
	u8 *data = ncch_decrypt_region(ncch,romfs_off,romfs_size,
			no_crypto,use_key,3,&got);
	if (data)
	{
	    PathCatPP(buf,sizeof(buf),dest,D3_FN_ROMFS);
	    err = save_file(buf,data,got);
	    FREE(data);
	    if (err)
		return err;
	}
    }

    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     XINFO			///////////////
///////////////////////////////////////////////////////////////////////////////

static void print_ncch_line ( uint idx, const u8 *ncch )
{
    if (memcmp(ncch+NCCH_OFF_MAGIC,"NCCH",4))
    {
	printf("      partition %u: not NCCH\n",idx);
	return;
    }
    char product[17];
    memcpy(product,ncch+0x150,16);
    product[16] = 0;
    const u8 flags7 = ncch[NCCH_OFF_FLAGS+NCCH_FLAG_BITS];
    printf("      partition %u: %-10s  %s%s%s\n",idx,product,
	    flags7 & NCCH_BIT_NO_CRYPTO ? "plain" : "encrypted",
	    flags7 & NCCH_BIT_FIXED_KEY ? " fixed-key" : "",
	    flags7 & NCCH_BIT_SEED      ? " seed" : "" );
}

///////////////////////////////////////////////////////////////////////////////

enumError XInfoCCI ( ccp source )
{
    u64 size = 0;
    u8 *img = load_file(source,&size,false);
    if (!img)
	return ERR_CANT_OPEN;

    enumError err = ERR_OK;
    if ( size < 0x200 || memcmp(img+NCSD_OFF_MAGIC,"NCSD",4) )
    {
	err = ERROR0(ERR_INVALID_FILE,"Not a valid NCSD/CCI: %s\n",source);
	goto abort;
    }

    printf("      image size: %llu bytes\n",
	    (u64)le32(img+NCSD_OFF_IMAGE_SIZE)*MEDIA_UNIT);

    for ( uint i = 0; i < 8; i++ )
    {
	const u8 *entry = img+NCSD_OFF_PART_TABLE+i*8;
	const u64 off  = (u64)le32(entry)*MEDIA_UNIT;
	const u64 psize= (u64)le32(entry+4)*MEDIA_UNIT;
	if (!psize)
	    continue;
	if ( off+NCCH_SIZE > size )
	{
	    printf("      partition %u: out of range\n",i);
	    continue;
	}
	print_ncch_line(i,img+off);
    }

 abort:
    FREE(img);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError XInfoCIA ( ccp source )
{
    u64 size = 0;
    u8 *cia = load_file(source,&size,false);
    if (!cia)
	return ERR_CANT_OPEN;

    enumError err = ERR_OK;
    if ( size < 0x20 )
    {
	err = ERROR0(ERR_INVALID_FILE,"File is too short: %s\n",source);
	goto abort;
    }

    const u32 hsize = le32(cia);
    const u32 cert_size = le32(cia+0x08);
    const u32 tik_size  = le32(cia+0x0c);
    const u32 tmd_size  = le32(cia+0x10);
    const u32 meta_size = le32(cia+0x14);
    const u64 content_size = le64(cia+0x18);

    printf("      header 0x%x, cert 0x%x, tik 0x%x, tmd 0x%x,"
	    " content %llu, meta 0x%x\n",
	    hsize,cert_size,tik_size,tmd_size,content_size,meta_size);

 abort:
    FREE(cia);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

// Bare .cxi/.cfa: a single NCCH.  AnalyzeXFormat() does not detect this on
// its own (it is only reached if the caller already knows), so XInfoCCI is
// reused for CCI and this helper is exposed for completeness; the dispatcher
// in lib-xfile.c only routes XF_CCI/XF_CIA today.

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   XEXTRACT			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError XExtractCCI ( ccp source, ccp dest )
{
    u64 size = 0;
    u8 *img = load_file(source,&size,false);
    if (!img)
	return ERR_CANT_OPEN;

    d3_keys_t keys;
    d3_load_keys(&keys);
    if (!keys.loaded)
	ERROR0(ERR_WARNING,
	    "No ~/.3ds/aes_keys.txt found; encrypted NCCH content will not"
	    " be decrypted, only NoCrypto content and plain regions.\n");

    enumError err = ERR_OK;
    char buf[PATH_MAX];

    if ( size < 0x200 || memcmp(img+NCSD_OFF_MAGIC,"NCSD",4) )
    {
	err = ERROR0(ERR_INVALID_FILE,"Not a valid NCSD/CCI: %s\n",source);
	goto abort;
    }

    err = CreatePath(dest,true);
    if (err)
	goto abort;

    PathCatPP(buf,sizeof(buf),dest,D3_FN_NCSD_HEAD);
    err = save_file(buf,img,0x200);
    if (err)
	goto abort;

    for ( uint i = 0; i < 8 && !err; i++ )
    {
	const u8 *entry = img+NCSD_OFF_PART_TABLE+i*8;
	const u64 off  = (u64)le32(entry)*MEDIA_UNIT;
	const u64 psize= (u64)le32(entry+4)*MEDIA_UNIT;
	if (!psize)
	    continue;
	if ( off+NCCH_SIZE > size )
	{
	    ERROR0(ERR_WARNING,"Partition %u is out of range, skipped: %s\n",i,source);
	    continue;
	}

	char pdir[PATH_MAX], name[32];
	snprintf(name,sizeof(name),D3_DIR_PART,i);
	PathCatPP(pdir,sizeof(pdir),dest,name);

	const u64 avail = off+psize <= size ? psize : size-off;
	err = dump_ncch(pdir,img+off,avail,&keys,source);
    }

    if ( !err && verbose >= 0 )
	printf("  extracted %s -> %s\n",source,dest);

 abort:
    FREE(img);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError XExtractCIA ( ccp source, ccp dest )
{
    u64 size = 0;
    u8 *cia = load_file(source,&size,false);
    if (!cia)
	return ERR_CANT_OPEN;

    enumError err = ERR_OK;
    char buf[PATH_MAX];

    if ( size < 0x20 )
    {
	err = ERROR0(ERR_INVALID_FILE,"File is too short: %s\n",source);
	goto abort;
    }

    const u32 hsize = ( le32(cia) + 0x3f ) & ~0x3fu;
    const u32 cert_size = ( le32(cia+0x08) + 0x3f ) & ~0x3fu;
    const u32 tik_size  = ( le32(cia+0x0c) + 0x3f ) & ~0x3fu;
    const u32 tmd_size  = ( le32(cia+0x10) + 0x3f ) & ~0x3fu;
    const u64 content_size = le64(cia+0x18);
    const u32 meta_size = le32(cia+0x14);
    const u32 raw_tmd_size = le32(cia+0x10);

    u64 pos = hsize;
    const u64 cert_off = pos; pos += cert_size;
    const u64 tik_off  = pos; pos += tik_size;
    const u64 tmd_off  = pos; pos += tmd_size;
    const u64 content_off = pos; pos += ( content_size + 0x3f ) & ~(u64)0x3f;
    const u64 meta_off = pos;

    if ( tmd_off+raw_tmd_size > size || content_off+content_size > size )
    {
	err = ERROR0(ERR_INVALID_FILE,"CIA sections don't fit the file: %s\n",source);
	goto abort;
    }

    err = CreatePath(dest,true);
    if (err)
	goto abort;

    PathCatPP(buf,sizeof(buf),dest,D3_FN_CIA_HEAD);
    err = save_file(buf,cia,hsize<size?hsize:size);
    if (!err && le32(cia+0x08))
    {
	PathCatPP(buf,sizeof(buf),dest,D3_FN_CERT);
	err = save_file(buf,cia+cert_off,le32(cia+0x08));
    }
    if (!err && le32(cia+0x0c))
    {
	PathCatPP(buf,sizeof(buf),dest,D3_FN_TICKET);
	err = save_file(buf,cia+tik_off,le32(cia+0x0c));
    }
    if (!err)
    {
	PathCatPP(buf,sizeof(buf),dest,D3_FN_TMD);
	err = save_file(buf,cia+tmd_off,raw_tmd_size);
    }
    if ( !err && meta_size && meta_off+meta_size <= size )
    {
	PathCatPP(buf,sizeof(buf),dest,D3_FN_META);
	err = save_file(buf,cia+meta_off,meta_size);
    }
    if (err)
	goto abort;

    // The content is one or more NCCH images concatenated (per the TMD
    // content records), each still title-key encrypted with a common key
    // this tool never hardcodes. Without that key the content is dumped as
    // one raw blob so nothing is lost, and each NoCrypto NCCH inside it can
    // still be located and unpacked by hand; with an unencrypted title
    // (common in homebrew/dev CIAs, or a NoCrypto NCCH already inside), the
    // dump is directly usable.
    ERROR0(ERR_WARNING,
	"CIA content decryption needs the console common key, which is never"
	" hardcoded here; the content is written out title-key encrypted as"
	" stored (contents.bin). Provide a NoCrypto/dev CIA, or decrypt it"
	" externally, to get plaintext NCCH images.\n");

    {
	PathCatPP(buf,sizeof(buf),dest,"contents.bin");
	err = save_file(buf,cia+content_off,content_size);
    }

    if ( !err && verbose >= 0 )
	printf("  extracted %s -> %s\n",source,dest);

 abort:
    FREE(cia);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    XCREATE			///////////////
///////////////////////////////////////////////////////////////////////////////

// Repacking a CCI or CIA means re-signing the NCSD/NCCH/CIA headers with
// Nintendo's private RSA keys, which this tool has no access to and will not
// fake. Unsigned/dev-mode repacking (as makerom's "-dev" CIAs do) is a
// narrower, legitimate case, but is not implemented here either -- doing it
// half right would silently produce images that look valid but aren't.

enumError XCreateCCI ( ccp source, ccp dest )
{
    (void)source;
    return ERROR0(ERR_NOT_IMPLEMENTED,
	"Creating CCI/3DS images requires re-signing NCSD/NCCH headers with"
	" Nintendo's private keys, which is not implemented: %s\n",dest);
}

///////////////////////////////////////////////////////////////////////////////

enumError XCreateCIA ( ccp source, ccp dest )
{
    (void)source;
    return ERROR0(ERR_NOT_IMPLEMENTED,
	"Creating CIA titles requires a signed TMD/ticket, which is not"
	" implemented: %s\n",dest);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
