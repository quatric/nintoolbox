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

// wingdi.h's LOGFONT face-family constants collide with src/file-type.h's
// FF_* file-format enum (FF_SCRIPT, FF_MODERN, ...); wszst has no use for
// the WinGDI font constants, so just drop them.
#undef FF_DONTCARE
#undef FF_ROMAN
#undef FF_SWISS
#undef FF_MODERN
#undef FF_SCRIPT
#undef FF_DECORATIVE

//-----------------------------------------------------------------------------
// small POSIX-function shims shared by dclib-basics.c / dclib-file.c / ...
// (kept here, rather than duplicated per file, since every file that needs
// one of them already includes this header transitively)

#include <direct.h>  // _mkdir()
#include <stdlib.h>  // _fullpath(), _putenv_s()
#include <string.h>  // memcmp() for the memmem() shim below
#include <ctype.h>   // tolower() for the strcasestr() shim below

// mkdir()'s mode argument doesn't exist on Windows -- this relies on the
// preprocessor's no-self-recursion rule: the expansion below still calls
// the real single-argument mkdir() declared in <io.h>, it just doesn't
// get macro-expanded again.
#ifndef mkdir
#define mkdir(path, mode) mkdir (path)
#endif

// realpath() isn't in the MinGW CRT; _fullpath() is the closest analog
// (no symlink resolution, but only used here for canonicalizing paths
// for comparison, not for resolving symlinks).
#ifndef realpath
#define realpath(path, resolved) _fullpath (resolved, path, PATH_MAX)
#endif

// setenv() isn't in the MinGW CRT; _putenv_s() is the closest analog.
#ifndef setenv
#define setenv(name, value, overwrite) _putenv_s (name, value)
#endif

// fcntl(F_SETFD, FD_CLOEXEC) hardens a FILE* against being inherited by a
// child process across exec(); Windows has no exec()-time fd inheritance
// model to match 1:1, so this is a no-op here.
#ifndef F_SETFD
#define F_SETFD 0
#define FD_CLOEXEC 0
static inline int fcntl (int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }
#endif

// memrchr() is a glibc/BSD extension MinGW doesn't ship.
static inline void *dclib_mingw_memrchr (const void *s, int c, size_t n)
{
	const unsigned char *p = (const unsigned char *)s + n;
	while (n--)
		if (*--p == (unsigned char)c)
			return (void *)p;
	return 0;
}
#define memrchr dclib_mingw_memrchr

// getrlimit()/setrlimit()/RLIMIT_NOFILE (open-file-count limits) and
// sysconf(_SC_CLK_TCK) have no Windows equivalent worth emulating -- wszst
// only uses these for informational stats and a /proc/cpuinfo-based CPU
// stat helper that already falls back to sane defaults when unavailable.
struct rlimit
{
	unsigned long rlim_cur;
	unsigned long rlim_max;
};
#define RLIMIT_NOFILE 0
static inline int getrlimit (int resource, struct rlimit *rlim)
{
	(void)resource;
	(void)rlim;
	return -1;
}
static inline int setrlimit (int resource, const struct rlimit *rlim)
{
	(void)resource;
	(void)rlim;
	return -1;
}
#define _SC_CLK_TCK 0
static inline long sysconf (int name) { (void)name; return -1; }

// getrusage()/struct rusage/RUSAGE_SELF (CPU-time accounting) aren't in
// the MinGW CRT; every caller already treats a nonzero return as "no data
// this round" and just skips the update, so failing here is enough.
struct rusage_timeval { long tv_sec; long tv_usec; };
struct rusage
{
	struct rusage_timeval ru_utime;
	struct rusage_timeval ru_stime;
};
#define RUSAGE_SELF 0
static inline int getrusage (int who, struct rusage *ru) { (void)who; (void)ru; return -1; }

//-----------------------------------------------------------------------------
// <time.h> gaps: gmtime_r()/localtime_r() are POSIX (MinGW/MSVCRT instead
// have gmtime_s()/localtime_s(), with the result/source arguments
// swapped), timegm() is a glibc/BSD extension (MSVCRT's _mkgmtime() is the
// equivalent), and strptime() isn't in the MinGW CRT at all -- only one
// call site (ScanDateTime() in dclib-numeric.c) uses it, always with the
// fixed format "%Y-%m-%d", so only that one format is implemented here.

#include <time.h>
#include <stdio.h>

static inline struct tm *dclib_mingw_gmtime_r (const time_t *timer, struct tm *result)
{
	return gmtime_s (result, timer) ? 0 : result;
}
#define gmtime_r dclib_mingw_gmtime_r

static inline struct tm *dclib_mingw_localtime_r (const time_t *timer, struct tm *result)
{
	return localtime_s (result, timer) ? 0 : result;
}
#define localtime_r dclib_mingw_localtime_r

#define timegm _mkgmtime

// in_addr_t is a POSIX/BSD sockets typedef winsock2.h doesn't provide;
// dclib-network.h/dclib-network-linux.h declare (but, since dclib-network.c
// itself isn't part of this build -- see dclib/Makefile.inc's DCLIB_NETWORK
// -- never define) a few functions using it.
#ifndef __in_addr_t_defined
#define __in_addr_t_defined
typedef u_long in_addr_t;
#endif

// O_NONBLOCK is meaningless here since /dev/urandom doesn't exist on
// Windows anyway (ReadFromUrandom() in dclib-numeric.c already treats a
// failed open() as "urandom not available" and falls back accordingly).
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

static inline char *dclib_mingw_strptime_ymd (const char *s, const char *format, struct tm *tm)
{
	(void)format; // only "%Y-%m-%d" is ever passed in this codebase
	int y, m, d, n;
	if (sscanf (s, "%d-%d-%d%n", &y, &m, &d, &n) != 3)
		return 0;
	tm->tm_year = y - 1900;
	tm->tm_mon = m - 1;
	tm->tm_mday = d;
	return (char *)(s + n);
}
#define strptime dclib_mingw_strptime_ymd

// lstat() (stat a symlink itself, not its target) -- Windows has no
// classic symlink model for stat() to distinguish, so this just forwards
// to stat().
#ifndef lstat
#define lstat stat
#endif

// getline() is a POSIX/glibc extension (allocate-as-needed line reading)
// not in the MinGW CRT.
static inline ssize_t dclib_mingw_getline (char **lineptr, size_t *n, FILE *stream)
{
	if (!lineptr || !n || !stream)
		return -1;

	if (!*lineptr || !*n)
	{
		*n = 256;
		*lineptr = (char *)malloc (*n);
		if (!*lineptr)
			return -1;
	}

	size_t len = 0;
	int c;
	while ((c = fgetc (stream)) != EOF)
	{
		if (len + 1 >= *n)
		{
			size_t new_size = *n * 2;
			char *new_ptr = (char *)realloc (*lineptr, new_size);
			if (!new_ptr)
				return -1;
			*lineptr = new_ptr;
			*n = new_size;
		}
		(*lineptr)[len++] = (char)c;
		if (c == '\n')
			break;
	}

	if (!len && c == EOF)
		return -1;

	(*lineptr)[len] = 0;
	return (ssize_t)len;
}
#define getline dclib_mingw_getline

// memmem() is a glibc/BSD extension MinGW doesn't ship.
static inline void *dclib_mingw_memmem (
	const void *haystack, size_t haystacklen, const void *needle, size_t needlelen)
{
	if (!needlelen)
		return (void *)haystack;
	if (needlelen > haystacklen)
		return 0;
	const unsigned char *h = (const unsigned char *)haystack;
	const unsigned char *end = h + haystacklen - needlelen;
	for (; h <= end; h++)
		if (!memcmp (h, needle, needlelen))
			return (void *)h;
	return 0;
}
#define memmem dclib_mingw_memmem

// strcasestr() is a glibc/BSD extension MinGW doesn't ship.
static inline char *dclib_mingw_strcasestr (const char *haystack, const char *needle)
{
	if (!*needle)
		return (char *)haystack;
	for (; *haystack; haystack++)
	{
		const char *h = haystack, *n = needle;
		while (*h && *n && tolower ((unsigned char)*h) == tolower ((unsigned char)*n))
		{
			h++;
			n++;
		}
		if (!*n)
			return (char *)haystack;
	}
	return 0;
}
#define strcasestr dclib_mingw_strcasestr

// No hardlinks via a single libc call on MinGW; CreateHardLinkA() is the
// Win32 equivalent (link() itself isn't declared by the MinGW CRT).
static inline int dclib_mingw_link (const char *oldpath, const char *newpath)
{
	return CreateHardLinkA (newpath, oldpath, 0) ? 0 : -1;
}
#define link dclib_mingw_link

// MinGW's <sys/stat.h> has no S_IFLNK/S_IFSOCK bits (no symlinks/sockets in
// the classic Windows stat() model) and its <dirent.h> has no d_type member
// at all -- callers use DIRENT_D_TYPE(dent) instead of dent->d_type, and
// always get DT_UNKNOWN (0) here, which every caller already handles by
// falling back to an explicit stat() call.
#ifndef S_IFLNK
#define S_IFLNK 0xA000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0xC000
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DIRENT_D_TYPE(dent) DT_UNKNOWN

#endif // __MINGW32__

#endif // DCLIB_MINGW_COMPAT_H
