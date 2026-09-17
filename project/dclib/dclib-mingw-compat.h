/***************************************************************************
 *                                                                         *
 *   MinGW (native Windows, no Cygwin) compatibility shim.                 *
 *                                                                         *
 *   Several source files need a handful of POSIX/BSD socket + Windows     *
 *   declarations (sockaddr, htonl/ntohl, ...) that only come from         *
 *   winsock2.h/windows.h on this target.  Pulling windows.h in, though,   *
 *   declares its own CreateFile/CopyFile/DeleteFile/MoveFile/GetFileSize/ *
 *   GetCurrentTime, which collide with dclib/src functions of the same   *
 *   name.  Rename the WinAPI ones out of the way before including        *
 *   windows.h (via winsock2.h) and restore the names afterwards so our    *
 *   own functions can still be declared normally.  This header is the     *
 *   single place windows.h gets included from, so every translation unit  *
 *   that needs it (via dclib-basics.h / dclib-file.h / lib-std.h) gets    *
 *   the same renaming applied exactly once.                               *
 *                                                                         *
 ***************************************************************************/

#ifndef DCLIB_MINGW_COMPAT_H
#define DCLIB_MINGW_COMPAT_H 1

#ifdef __MINGW32__

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // keep windows.h from pulling in ole2.h/oleauto.h,
			     // which typedefs "DATE" and collides with
			     // version.h's "#define DATE <build date>"
#endif

#define CreateFile	CreateFile_Win32Unused
#define CopyFile	CopyFile_Win32Unused
#define DeleteFile	DeleteFile_Win32Unused
#define MoveFile	MoveFile_Win32Unused
#define GetFileSize	GetFileSize_Win32Unused
#define OpenFile	OpenFile_Win32Unused

#include <winsock2.h> // struct sockaddr, sockaddr_in, htonl/ntohl/..., fd_set
#include <ws2tcpip.h> // struct sockaddr_in6
#include <afunix.h>   // struct sockaddr_un (Windows 10+ AF_UNIX support)

#undef CreateFile
#undef CopyFile
#undef DeleteFile
#undef MoveFile
#undef GetFileSize
#undef OpenFile
#undef GetCurrentTime // windows.h's GetTickCount() alias shadows dclib's own
		       // GetCurrentTime(bool)

#endif // __MINGW32__

#endif // DCLIB_MINGW_COMPAT_H
