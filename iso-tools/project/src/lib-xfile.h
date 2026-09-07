
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

#ifndef WIT_LIB_XFILE_H
#define WIT_LIB_XFILE_H 1

#include "dclib/dclib-types.h"
#include "lib-std.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    foreign container support		///////////////
///////////////////////////////////////////////////////////////////////////////

// WIT proper only ever deals with GameCube and Wii disc images: every command
// funnels through SuperFile_t, and SourceIteratorHelper() rejects anything
// that is not such an image.  The containers handled here -- Wii U, DS, 3DS,
// Switch and installable Wii titles -- share none of that structure, so they
// are deliberately kept in their own small subsystem instead of being forced
// into enumOFT.  They are reached through the X* commands (XINFO, XEXTRACT,
// XCREATE, XCONVERT) and never through COPY, EXTRACT or VERIFY.

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    xformat_t			///////////////
///////////////////////////////////////////////////////////////////////////////
// [[xformat_t]]

typedef enum xformat_t
{
    XF_UNKNOWN = 0,	// not a known foreign container

    XF_WUD,		// Wii U disc image, plain
    XF_WUX,		// Wii U disc image, sparse (WUX0)
    XF_NDS,		// Nintendo DS / DSi cartridge dump
    XF_WAD,		// installable Wii title (WAD)
    XF_CCI,		// 3DS cartridge dump (NCSD, aka .3ds / .cci)
    XF_CIA,		// 3DS installable title
    XF_XCI,		// Switch cartridge dump
    XF_NSP,		// Switch package (PFS0)
    XF_NKIT_GC,		// NKit-compressed GameCube disc image (.nkit.iso); see x-nkit.c
    XF_NKIT_WII,	// NKit-compressed Wii disc image (.nkit.iso); see x-nkit-wii.c

    XF__N		// number of formats
}
xformat_t;

///////////////////////////////////////////////////////////////////////////////
// [[xformat_info_t]]

typedef struct xformat_info_t
{
    xformat_t	format;		// the format itself
    ccp		name;		// short upper case name, e.g. "WUX"
    ccp		ext;		// canonical file extension including the dot
    ccp		info;		// one line description
    bool	can_extract;	// XEXTRACT is implemented
    bool	can_create;	// XCREATE is implemented
    bool	can_convert;	// XCONVERT can read/write it
}
xformat_info_t;

// Indexed by xformat_t, XF__N entries.
extern const xformat_info_t xformat_info[XF__N];

//-----------------------------------------------------------------------------

// Scan a format name ("WUX", "NDS", ...).  Returns XF_UNKNOWN if unknown.
xformat_t ScanXFormat ( ccp name );

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   detection			///////////////
///////////////////////////////////////////////////////////////////////////////

// Number of leading bytes AnalyzeXFormat() wants to see.  Everything is
// identified by a magic in the first sector, so one sector is plenty.
#define XFILE_HEAD_SIZE 0x1000

// Identify a container by content.  'file_size' may be 0 if unknown; it is
// only used to reject headers that promise more data than the file holds.
xformat_t AnalyzeXFormat
(
    const void		*data,		// valid pointer to the file start
    uint		data_size,	// number of valid bytes in 'data'
    u64			file_size	// 0 or the real file size
);

//-----------------------------------------------------------------------------

// Open FNAME, read its head and identify it.  Prints an error and returns
// XF_UNKNOWN if the file can not be opened or is not a known container.
xformat_t AnalyzeXFile
(
    ccp			fname,		// file to analyze
    u64			*file_size	// not NULL: store the file size here
);

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   commands			///////////////
///////////////////////////////////////////////////////////////////////////////

// Print one or more lines describing SOURCE.
enumError XInfo
(
    ccp			source		// file to describe
);

//-----------------------------------------------------------------------------

// Unpack SOURCE into the directory DEST, which is created if needed.
enumError XExtract
(
    ccp			source,		// container to unpack
    ccp			dest		// destination directory
);

//-----------------------------------------------------------------------------

// Pack the directory SOURCE into the container DEST.  The output format is
// taken from 'format', or from the extension of DEST if 'format' is
// XF_UNKNOWN.
enumError XCreate
(
    ccp			source,		// source directory
    ccp			dest,		// container to create
    xformat_t		format		// XF_UNKNOWN: derive from 'dest'
);

//-----------------------------------------------------------------------------

// Rewrite SOURCE as DEST in a different container of the same family, e.g.
// WUX <-> WUD.  The output format is taken from 'format', or from the
// extension of DEST if 'format' is XF_UNKNOWN.
enumError XConvert
(
    ccp			source,		// source container
    ccp			dest,		// destination container
    xformat_t		format		// XF_UNKNOWN: derive from 'dest'
);

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////

#endif // WIT_LIB_XFILE_H
