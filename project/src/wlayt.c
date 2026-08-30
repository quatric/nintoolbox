// wlayt - Wiimms Layout Tool
// Native decoder/encoder for BRLYT/BRLAN (Wii), BFLYT/BFLAN (Wii U/Switch)
// and BCLYT/BCLAN (3DS) layout files. No external interpreters or scripts
// are used -- everything is handled by the native lib-bflyt implementation.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib-std.h"
#include "dclib-file.h"
#include "lib-bflyt.h"

static ccp GetDefaultDest (ccp src, bool to_text)
{
	static char buf[PATH_MAX];
	ccp dot = strrchr (src, '.');
	uint baselen = dot ? (uint)(dot - src) : strlen (src);
	if (baselen >= sizeof (buf) - 8)
		baselen = sizeof (buf) - 8;
	memcpy (buf, src, baselen);
	ccp ext = to_text ? ".txt" : ".bin";
	strcpy (buf + baselen, ext);
	return buf;
}

static int do_decode (ccp src, ccp dest)
{
	u8 *data = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (src, 0, 0, &data, &size, 0, 0, 0, false);
	if (err)
	{
		fprintf (stderr, "wlayt: can't read %s\n", src);
		return 1;
	}

	bflyt_t bflyt;
	InitializeBFLYT (&bflyt);
	err = ScanBFLYT (&bflyt, false, data, (uint)size);
	FREE (data);
	if (err)
	{
		fprintf (
			stderr, "wlayt: %s is not a valid BRLYT/BFLYT/BCLYT/BRLAN/BFLAN/BCLAN file\n", src);
		ResetBFLYT (&bflyt);
		return 1;
	}

	ccp out = dest ? dest : GetDefaultDest (src, true);
	err = SaveTextBFLYT (&bflyt, out, true);
	ResetBFLYT (&bflyt);
	if (err)
	{
		fprintf (stderr, "wlayt: failed to write %s\n", out);
		return 1;
	}
	printf ("wlayt: decoded %s -> %s\n", src, out);
	return 0;
}

static int do_encode (ccp src, ccp dest)
{
	u8 *data = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (src, 0, 0, &data, &size, 0, 0, 0, false);
	if (err)
	{
		fprintf (stderr, "wlayt: can't read %s\n", src);
		return 1;
	}

	bflyt_t bflyt;
	InitializeBFLYT (&bflyt);
	err = ScanBFLYT (&bflyt, false, data, (uint)size);
	FREE (data);
	if (err)
	{
		fprintf (stderr, "wlayt: %s is not a valid layout text file\n", src);
		ResetBFLYT (&bflyt);
		return 1;
	}

	ccp out = dest ? dest : GetDefaultDest (src, false);
	err = SaveRawBFLYT (&bflyt, out, true);
	ResetBFLYT (&bflyt);
	if (err)
	{
		fprintf (stderr, "wlayt: failed to write %s\n", out);
		return 1;
	}
	printf ("wlayt: encoded %s -> %s\n", src, out);
	return 0;
}

int main (int argc, char *argv[])
{
	if (argc < 3)
	{
		printf ("wlayt - Wiimms Layout Tool\n"
				"Native BRLYT/BFLYT/BCLYT + BRLAN/BFLAN/BCLAN <-> text converter.\n"
				"Usage: %s decode <input> [output]\n"
				"       %s encode <input> [output]\n",
			argv[0], argv[0]);
		return 1;
	}

	ccp dest = argc > 3 ? argv[3] : 0;

	if (!strcmp (argv[1], "decode"))
		return do_decode (argv[2], dest);
	if (!strcmp (argv[1], "encode"))
		return do_encode (argv[2], dest);

	fprintf (stderr, "wlayt: unknown command '%s' (expect decode|encode)\n", argv[1]);
	return 1;
}

bool DefineIntVar (VarMap_t *vm, ccp varname, int value)
{
	return false;
}
