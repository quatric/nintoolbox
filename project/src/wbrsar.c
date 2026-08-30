// wbrsar - Wiimms BRSAR/BFSAR/BCSAR Tool
// Converts Wii BRSAR sound archives (and other formats vgmtrans recognizes)
// to MIDI + SF2/DLS; packs a directory of RSEQ/RBNK/RWAR/RWSD assets into a
// .brsar/.bfsar/.bcsar; and unpacks any of those back to individual asset
// files (see lib-brsar.h for the pack/unpack implementation and its
// documented field-layout provenance -- RSAR is verified against vgmtrans'
// reader, FSAR/CSAR are an extrapolation with no independent reference).
// The vgmtrans BRSAR scanner/sequence/instrument logic used for the MIDI/
// SF2 conversion path is statically linked into this binary (see
// src/vgmtrans/src/ui/cli/vgmtrans_bridge.cpp) -- no external process is
// spawned at runtime.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lib-std.h"
#include "lib-brsar.h"
#include "vgmtrans_bridge.h"

static int cmd_pack (int argc, char *argv[])
{
	if (argc < 3)
	{
		fprintf (stderr,
			"wbrsar pack: missing input directory\n"
			"Usage: %s pack <input_dir> [output.brsar] [--bfsar|--bcsar]\n",
			argv[0]);
		return ERR_SYNTAX;
	}

	ccp input_dir = argv[2];
	ccp output_path = 0;
	brsar_variant_t variant = BRSAR_VARIANT_RSAR;
	for (int i = 3; i < argc; i++)
	{
		if (!strcmp (argv[i], "--bfsar"))
			variant = BRSAR_VARIANT_FSAR;
		else if (!strcmp (argv[i], "--bcsar"))
			variant = BRSAR_VARIANT_CSAR;
		else if (!output_path)
			output_path = argv[i];
	}

	char out_buf[1024];
	if (!output_path)
	{
		ccp ext = variant == BRSAR_VARIANT_FSAR ? ".bfsar"
			: variant == BRSAR_VARIANT_CSAR		? ".bcsar"
												: ".brsar";
		size_t len = strlen (input_dir);
		if (len > 2 && !strcmp (input_dir + len - 2, ".d"))
			snprintf (out_buf, sizeof (out_buf), "%.*s%s", (int)(len - 2), input_dir, ext);
		else
			snprintf (out_buf, sizeof (out_buf), "%s%s", input_dir, ext);
		output_path = out_buf;
	}

	u8 *data = 0;
	size_t size = 0;
	enumError err = PackBRSARDir (&data, &size, input_dir, variant);
	if (err)
	{
		fprintf (stderr, "wbrsar: pack failed for %s\n", input_dir);
		return err;
	}

	File_t F;
	err = CreateFileOpt (&F, true, output_path, false, input_dir);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (
			&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", size, output_path);
	ResetFile (&F, 0);
	FREE (data);

	if (!err)
		printf ("wbrsar: packed %s -> %s (%zu bytes)\n", input_dir, output_path, size);
	return err;
}

static int cmd_unpack (int argc, char *argv[])
{
	if (argc < 3)
	{
		fprintf (stderr,
			"wbrsar unpack: missing input archive\n"
			"Usage: %s unpack <input.brsar|.bfsar|.bcsar> [output_dir]\n",
			argv[0]);
		return ERR_SYNTAX;
	}

	ccp input_path = argv[2];
	char dest_buf[1024];
	ccp out_dir = argc > 3 ? argv[3] : 0;
	if (!out_dir)
	{
		snprintf (dest_buf, sizeof (dest_buf), "%s.d", input_path);
		out_dir = dest_buf;
	}

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (input_path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
	{
		fprintf (stderr, "wbrsar: failed to load %s\n", input_path);
		return err;
	}

	err = UnpackBRSAR (raw, raw_size, out_dir);
	FREE (raw);
	if (err)
	{
		fprintf (stderr, "wbrsar: unpack failed for %s\n", input_path);
		return err;
	}
	printf ("wbrsar: unpacked %s -> %s\n", input_path, out_dir);
	return 0;
}

int main (int argc, char *argv[])
{
	if (argc > 1 && (!strcmp (argv[1], "pack") || !strcmp (argv[1], "p")))
		return cmd_pack (argc, argv);
	if (argc > 1 && (!strcmp (argv[1], "unpack") || !strcmp (argv[1], "u")))
		return cmd_unpack (argc, argv);

	const char *in_file = NULL;
	const char *out_dir = NULL;
	int format_flags = VGMTRANS_FMT_SF2;

	for (int i = 1; i < argc; i++)
	{
		const char *arg = argv[i];
		if (!strcmp (arg, "-h") || !strcmp (arg, "--help"))
		{
			printf (
				"wbrsar - Wiimms BRSAR/BFSAR/BCSAR Tool\n"
				"Converts a BRSAR (or other vgmtrans-recognized) sound bank to MIDI + SF2/DLS.\n"
				"Extracts all MIDI sequences and exactly 1 copy of the soundfont for the "
				"archive.\n\n"
				"Usage: %s [options] <input.brsar> [output_dir]\n"
				"       %s pack   <input_dir> [output.brsar] [--bfsar|--bcsar]\n"
				"       %s unpack <input.brsar|.bfsar|.bcsar> [output_dir]\n\n"
				"Options:\n"
				"  --sf2            Export SoundFont 2 (.sf2) [default]\n"
				"  --dls            Export DLS (.dls)\n"
				"  --both           Export both .sf2 and .dls\n"
				"  -d, --dest <dir> Specify destination directory\n"
				"  -h, --help       Show this help\n\n"
				"pack: Build an archive from a directory of RSEQ (.txt MML source or\n"
				"      .rseq/.brseq binary) and RBNK/RWAR/RWSD asset files. Defaults to\n"
				"      BRSAR (Wii); --bfsar/--bcsar select the Wii U / 3DS container\n"
				"      instead (FSAR/CSAR layout is extrapolated, not independently\n"
				"      verified -- see lib-brsar.h).\n"
				"unpack: Extract an archive's RSEQ/RBNK/RWAR/RWSD assets to a directory\n"
				"      (raw asset dump, distinct from the MIDI/SF2 conversion above).\n",
				argv[0], argv[0], argv[0]);
			return 0;
		}
		else if (!strcmp (arg, "--sf2"))
			format_flags = VGMTRANS_FMT_SF2;
		else if (!strcmp (arg, "--dls"))
			format_flags = VGMTRANS_FMT_DLS;
		else if (!strcmp (arg, "--both"))
			format_flags = VGMTRANS_FMT_BOTH;
		else if (!strcmp (arg, "-d") || !strcmp (arg, "--dest"))
		{
			if (++i < argc)
				out_dir = argv[i];
		}
		else if (!strncmp (arg, "--dest=", 7))
			out_dir = arg + 7;
		else if (!strncmp (arg, "-d=", 3))
			out_dir = arg + 3;
		else if (*arg != '-')
		{
			if (!in_file)
				in_file = arg;
			else if (!out_dir)
				out_dir = arg;
		}
	}

	if (!in_file)
	{
		printf ("wbrsar - Wiimms BRSAR Tool\n"
				"Usage: %s [options] <input.brsar> [output_dir]\n"
				"Type '%s --help' for available options.\n",
			argv[0], argv[0]);
		return 1;
	}

	char dest_buf[1024];
	if (!out_dir)
	{
		snprintf (dest_buf, sizeof (dest_buf), "%s.d", in_file);
		out_dir = dest_buf;
	}

	struct stat st;
	if (stat (out_dir, &st) != 0)
		mkdir (out_dir, 0755);

	int err = VgmtransConvertFileExt (in_file, out_dir, format_flags);
	if (err)
	{
		fprintf (stderr, "wbrsar: conversion failed for %s\n", in_file);
		return err;
	}
	printf ("wbrsar: converted %s -> %s\n", in_file, out_dir);
	return 0;
}

bool DefineIntVar (VarMap_t *vm, ccp varname, int value)
{
	return false;
}
