
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

#ifndef WIT_X_FORMATS_H
#define WIT_X_FORMATS_H 1

#include "dclib/dclib-types.h"
#include "lib-std.h"
#include "lib-xfile.h"

//
///////////////////////////////////////////////////////////////////////////////
///////////////	    per format back ends of the X* commands	///////////////
///////////////////////////////////////////////////////////////////////////////

// Each foreign container implements the subset of these that makes sense for
// it; lib-xfile.c decides which one to call and never calls a back end whose
// xformat_info_t says it is unavailable.

//--- Wii U (x-wiiu.c)

enumError XInfoWiiU    ( ccp source, xformat_t format );
enumError XConvertWiiU ( ccp source, xformat_t src_format,
			 ccp dest,   xformat_t dest_format );
enumError XExtractWiiU ( ccp source, xformat_t format, ccp dest );

//--- Nintendo DS (x-nds.c)

enumError XInfoNDS     ( ccp source );
enumError XExtractNDS  ( ccp source, ccp dest );
enumError XCreateNDS   ( ccp source, ccp dest );

//--- Wii WAD (x-wad.c)

enumError XInfoWAD     ( ccp source );
enumError XExtractWAD  ( ccp source, ccp dest );
enumError XCreateWAD   ( ccp source, ccp dest );

//--- NKit restore: GameCube (x-nkit.c) and Wii (x-nkit-wii.c)

enumError XExtractNKitGC  ( ccp source, ccp dest );
enumError XExtractNKitWii ( ccp source, ccp dest );

//--- 3DS: CCI cartridges and CIA titles (x-3ds.c)

enumError XInfoCCI     ( ccp source );
enumError XExtractCCI  ( ccp source, ccp dest );
enumError XCreateCCI   ( ccp source, ccp dest );

enumError XInfoCIA     ( ccp source );
enumError XExtractCIA  ( ccp source, ccp dest );
enumError XCreateCIA   ( ccp source, ccp dest );

//--- Switch: XCI cartridges and NSP packages (x-switch.c)

enumError XInfoXCI     ( ccp source );
enumError XExtractXCI  ( ccp source, ccp dest );
enumError XCreateXCI   ( ccp source, ccp dest );

enumError XInfoNSP     ( ccp source );
enumError XExtractNSP  ( ccp source, ccp dest );
enumError XCreateNSP   ( ccp source, ccp dest );

//
///////////////////////////////////////////////////////////////////////////////
///////////////                          END                    ///////////////
///////////////////////////////////////////////////////////////////////////////

#endif // WIT_X_FORMATS_H
