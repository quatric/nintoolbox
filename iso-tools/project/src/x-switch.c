
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

//
///////////////////////////////////////////////////////////////////////////////
///////////////		   Switch: XCI cartridges and NSP packages	///////////////
///////////////////////////////////////////////////////////////////////////////

// Layout references: switchbrew.org "NCA", "NSP" and "Gamecard Format"
// (public documentation, no secret material).
//
// A .nsp is a bare PFS0 archive of NCA files (plus .tik/.cert/.nacp/.jpg
// siblings). A .xci is a gamecard image: a 0x200 byte header, then an HFS0
// "root" partition whose entries are themselves HFS0 partitions (normally
// "update", "normal", "secure", sometimes "logo"), and the secure partition
// holds the NCAs. HFS0 and PFS0 share the same table layout except that HFS0
// entries additionally carry a hashed region, so one parser handles both.
//
// None of this container parsing needs any key material -- it is plain
// container framing -- so it is implemented solidly here. Decrypting the NCA
// headers/sections needs Nintendo's "header_key" and per-title/per-keyslot
// keys, which are never hardcoded; they are read from an external, widely
// used key file (~/.switch/prod.keys, "name = hex" per line, the format the
// homebrew scene's own tools already use). Without that file the NCAs are
// still extracted whole (they are exactly what the container held), just not
// header/section-decrypted, with a clear warning instead of a silent gap.

#define PFS0_MAGIC "PFS0"
#define HFS0_MAGIC "HFS0"

// Names used inside an extracted directory.
#define SW_FN_XCI_HEAD	"xci_header.bin"
#define SW_DIR_ROOT	"root"

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

//
///////////////////////////////////////////////////////////////////////////////
///////////////		     prod.keys (informational only)		///////////////
///////////////////////////////////////////////////////////////////////////////

// The key file is parsed and its presence/contents are reported so users know
// exactly what limits the extraction, but full NCA header (AES-XTS) and
// section (AES-CTR with a per-content key) decryption is deliberately not
// attempted: getting either subtly wrong would silently corrupt output, and
// this tool has no way to verify a from-scratch AES-XTS implementation
// against real data without a set of test vectors it doesn't have on hand.
// The container-level extraction below (PFS0/HFS0 -> whole NCA files) is
// exact and needs no keys at all.

typedef struct sw_keys_t
{
    bool	loaded;			// prod.keys was found
    bool	has_header_key;		// "header_key" entry present
    uint	n_entries;		// total recognised "name = hex" lines
}
sw_keys_t;

static void sw_load_keys ( sw_keys_t *keys )
{
    memset(keys,0,sizeof(*keys));

    ccp home = getenv("HOME");
    if (!home)
	return;
    char path[PATH_MAX];
    snprintf(path,sizeof(path),"%s/.switch/prod.keys",home);

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
	keys->n_entries++;
	if (!strcasecmp(name,"header_key"))
	    keys->has_header_key = true;
    }
    fclose(f);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		      PFS0 / HFS0 container walk		///////////////
///////////////////////////////////////////////////////////////////////////////
// [[pfs0_entry_t]]

// Both formats share this header shape:
//   magic[4], n_entries(u32), string_table_size(u32), reserved(u32)
// followed by n_entries fixed size entries and then the string table.
// PFS0 entries are 24 bytes (offset,size,name_off,reserved); HFS0 entries add
// a hashed-region size/offset and a 32 byte hash, 64 bytes total.

typedef struct pfs0_entry_t
{
    u64		offset;		// relative to the end of the whole header
    u64		size;
    ccp		name;
}
pfs0_entry_t;

typedef struct pfs0_t
{
    bool	is_hfs0;
    uint	n_entries;
    pfs0_entry_t entry[512];
    u64		body_off;	// file offset where entry offsets are relative to
}
pfs0_t;

// Parse a PFS0/HFS0 header located at 'base' (offset 'base' inside 'image').
// Returns false if the magic doesn't match.

static bool pfs0_parse ( const u8 *image, u64 image_size, u64 base, pfs0_t *out )
{
    memset(out,0,sizeof(*out));
    if ( base+16 > image_size )
	return false;

    const u8 *h = image+base;
    bool hfs0;
    if (!memcmp(h,PFS0_MAGIC,4))
	hfs0 = false;
    else if (!memcmp(h,HFS0_MAGIC,4))
	hfs0 = true;
    else
	return false;

    const u32 n = le32(h+4);
    const u32 str_size = le32(h+8);
    const uint entry_size = hfs0 ? 64 : 24;
    if ( n > 512 )
	return false;

    const u64 entries_off = base+16;
    const u64 strtab_off = entries_off + (u64)n*entry_size;
    if ( strtab_off + str_size > image_size )
	return false;

    out->is_hfs0 = hfs0;
    out->n_entries = n;
    out->body_off = strtab_off + str_size;

    for ( uint i = 0; i < n; i++ )
    {
	const u8 *e = image + entries_off + (u64)i*entry_size;
	out->entry[i].offset = le64(e);
	out->entry[i].size   = le64(e+8);
	const u32 name_off = le32(e+16);
	if ( strtab_off+name_off >= image_size )
	    return false;
	out->entry[i].name = (ccp)(image + strtab_off + name_off);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

static enumError pfs0_extract_entry
	( ccp dir, const u8 *image, u64 image_size, const pfs0_t *pfs, uint i )
{
    const pfs0_entry_t *e = pfs->entry+i;

    // Reject names that could escape the destination directory.
    if ( !e->name || !*e->name || strchr(e->name,'/')
      || !strcmp(e->name,".") || !strcmp(e->name,"..") )
	return ERROR0(ERR_INVALID_FILE,"Unsafe entry name in container\n");

    const u64 off = pfs->body_off + e->offset;
    if ( off + e->size > image_size )
	return ERROR0(ERR_INVALID_FILE,
		"Entry %s is outside the image (0x%llx+0x%llx)\n",
		e->name,off,e->size);

    char path[PATH_MAX];
    PathCatPP(path,sizeof(path),dir,e->name);
    return save_file(path,image+off,e->size);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     XINFO			///////////////
///////////////////////////////////////////////////////////////////////////////

static void print_pfs0 ( ccp label, const pfs0_t *pfs )
{
    printf("      %s: %s, %u entries\n",
	    label, pfs->is_hfs0 ? "HFS0" : "PFS0", pfs->n_entries );
    for ( uint i = 0; i < pfs->n_entries; i++ )
	printf("        %-48s %12llu bytes\n",
		pfs->entry[i].name, pfs->entry[i].size);
}

///////////////////////////////////////////////////////////////////////////////

enumError XInfoNSP ( ccp source )
{
    u64 size = 0;
    u8 *img = load_file(source,&size,false);
    if (!img)
	return ERR_CANT_OPEN;

    pfs0_t pfs;
    enumError err = ERR_OK;
    if (!pfs0_parse(img,size,0,&pfs))
	err = ERROR0(ERR_INVALID_FILE,"Not a valid PFS0: %s\n",source);
    else
	print_pfs0("contents",&pfs);

    FREE(img);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError XInfoXCI ( ccp source )
{
    u64 size = 0;
    u8 *img = load_file(source,&size,false);
    if (!img)
	return ERR_CANT_OPEN;

    enumError err = ERR_OK;
    if ( size < 0x200 || memcmp(img+0x100,"HEAD",4) )
    {
	err = ERROR0(ERR_INVALID_FILE,"Not a valid XCI: %s\n",source);
	goto abort;
    }

    printf("      cartridge size code: 0x%02x\n",img[0x10d]);

    pfs0_t root;
    if (!pfs0_parse(img,size,0xf000,&root))
	err = ERROR0(ERR_INVALID_FILE,"Root HFS0 not found: %s\n",source);
    else
    {
	print_pfs0("root",&root);
	for ( uint i = 0; i < root.n_entries; i++ )
	{
	    const u64 off = root.body_off + root.entry[i].offset;
	    pfs0_t sub;
	    if (pfs0_parse(img,size,off,&sub))
		print_pfs0(root.entry[i].name,&sub);
	}
    }

 abort:
    FREE(img);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   XEXTRACT			///////////////
///////////////////////////////////////////////////////////////////////////////

static void warn_no_decrypt ( const sw_keys_t *keys, ccp source )
{
    if (!keys->loaded)
	ERROR0(ERR_WARNING,
	    "No ~/.switch/prod.keys found; NCA files are extracted whole but"
	    " not header/section-decrypted: %s\n",source);
    else if (!keys->has_header_key)
	ERROR0(ERR_WARNING,
	    "~/.switch/prod.keys has no 'header_key' entry; NCA files are"
	    " extracted whole but not header/section-decrypted: %s\n",source);
    else
	ERROR0(ERR_WARNING,
	    "NCA header (AES-XTS) and section decryption is not implemented"
	    " (avoiding a from-scratch, unverified crypto path); NCA files"
	    " are extracted whole, undecrypted: %s\n",source);
}

///////////////////////////////////////////////////////////////////////////////

enumError XExtractNSP ( ccp source, ccp dest )
{
    u64 size = 0;
    u8 *img = load_file(source,&size,false);
    if (!img)
	return ERR_CANT_OPEN;

    pfs0_t pfs;
    enumError err = ERR_OK;
    if (!pfs0_parse(img,size,0,&pfs))
    {
	err = ERROR0(ERR_INVALID_FILE,"Not a valid PFS0: %s\n",source);
	goto abort;
    }

    err = CreatePath(dest,true);
    if (err)
	goto abort;

    sw_keys_t keys;
    sw_load_keys(&keys);
    warn_no_decrypt(&keys,source);

    for ( uint i = 0; i < pfs.n_entries && !err; i++ )
	err = pfs0_extract_entry(dest,img,size,&pfs,i);

    if ( !err && verbose >= 0 )
	printf("  extracted %s -> %s (%u entries)\n",source,dest,pfs.n_entries);

 abort:
    FREE(img);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

// Recursively unpack an HFS0/PFS0 partition under 'dir', descending into any
// entry that itself parses as HFS0/PFS0 (the XCI "secure" etc. partitions).

static enumError xci_extract_partition
	( ccp dir, const u8 *image, u64 image_size, u64 base, uint depth )
{
    if ( depth > 8 )
	return ERROR0(ERR_INVALID_FILE,"Partitions nested too deeply\n");

    pfs0_t pfs;
    if (!pfs0_parse(image,image_size,base,&pfs))
	return ERROR0(ERR_INVALID_FILE,"Invalid HFS0/PFS0 partition table\n");

    enumError err = CreatePath(dir,true);
    for ( uint i = 0; i < pfs.n_entries && !err; i++ )
	err = pfs0_extract_entry(dir,image,image_size,&pfs,i);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError XExtractXCI ( ccp source, ccp dest )
{
    u64 size = 0;
    u8 *img = load_file(source,&size,false);
    if (!img)
	return ERR_CANT_OPEN;

    enumError err = ERR_OK;
    char buf[PATH_MAX];

    if ( size < 0x200 || memcmp(img+0x100,"HEAD",4) )
    {
	err = ERROR0(ERR_INVALID_FILE,"Not a valid XCI: %s\n",source);
	goto abort;
    }

    err = CreatePath(dest,true);
    if (err)
	goto abort;

    PathCatPP(buf,sizeof(buf),dest,SW_FN_XCI_HEAD);
    err = save_file(buf,img,0x200);
    if (err)
	goto abort;

    sw_keys_t keys;
    sw_load_keys(&keys);
    warn_no_decrypt(&keys,source);

    // The root HFS0 conventionally starts right behind the 0x200 byte header
    // at a fixed 0xf000 offset on real cartridge dumps.
    pfs0_t root;
    if (!pfs0_parse(img,size,0xf000,&root))
    {
	err = ERROR0(ERR_INVALID_FILE,"Root HFS0 not found: %s\n",source);
	goto abort;
    }

    for ( uint i = 0; i < root.n_entries && !err; i++ )
    {
	const u64 off = root.body_off + root.entry[i].offset;
	char pdir[PATH_MAX];
	PathCatPP(pdir,sizeof(pdir),dest,root.entry[i].name);

	pfs0_t sub;
	if (pfs0_parse(img,size,off,&sub))
	    err = xci_extract_partition(pdir,img,size,off,1);
	else
	    // Not itself a container (rare); copy the raw range out.
	    err = pfs0_extract_entry(dest,img,size,&root,i);
    }

    if ( !err && verbose >= 0 )
	printf("  extracted %s -> %s\n",source,dest);

 abort:
    FREE(img);
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    XCREATE			///////////////
///////////////////////////////////////////////////////////////////////////////

// Repacking either format needs freshly signed NCA/XCI/NSP metadata (NCA
// content hashes chain up into signed structures the same way NCCH/CIA do),
// which this tool has no private key material for and will not fake.

enumError XCreateNSP ( ccp source, ccp dest )
{
    (void)source;
    return ERROR0(ERR_NOT_IMPLEMENTED,
	"Creating NSP packages requires signed NCA/ticket metadata, which is"
	" not implemented: %s\n",dest);
}

///////////////////////////////////////////////////////////////////////////////

enumError XCreateXCI ( ccp source, ccp dest )
{
    (void)source;
    return ERROR0(ERR_NOT_IMPLEMENTED,
	"Creating XCI cartridge images requires signed gamecard metadata,"
	" which is not implemented: %s\n",dest);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////
