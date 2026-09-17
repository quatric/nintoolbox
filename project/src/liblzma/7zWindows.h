/* 7zWindows.h -- windows.h wrapper, as shipped by the upstream 7-Zip SDK.
   This vendored copy of the 7-Zip SDK source (src/liblzma) was missing this
   one small header (its _WIN32-guarded code paths were presumably never
   exercised before, since the project's only prior Windows build used
   Cygwin, whose gcc does not define _WIN32). Restored verbatim from the
   upstream 7-Zip SDK so the existing #ifdef _WIN32 / #include "7zWindows.h"
   lines in CpuArch.c / Threads.h resolve under MinGW (which does define
   _WIN32) the same way they would with the real SDK tree. */

#ifndef __7Z_WINDOWS_H
#define __7Z_WINDOWS_H

#ifdef _WIN32

#include <windows.h>

#endif

#endif
