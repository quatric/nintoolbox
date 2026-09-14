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
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include "lib-std.h"
#include "lib-brsar.h"
#include "lib-sdat.h"
#if !defined(NO_VGMTRANS) || !NO_VGMTRANS
#include "vgmtrans_bridge.h"
#else
#define VGMTRANS_FMT_SF2  0x01
#define VGMTRANS_FMT_DLS  0x02
#define VGMTRANS_FMT_BOTH (VGMTRANS_FMT_SF2 | VGMTRANS_FMT_DLS)
#endif

static const char *find_vgmtrans_tool (const char *explicit_path, const char *argv0)
{
	static char found[1024];
	if (explicit_path && *explicit_path)
	{
		if (strchr (explicit_path, '/') && access (explicit_path, X_OK) == 0)
			return explicit_path;
		const char *dirs = getenv ("PATH");
		while (dirs && *dirs)
		{
			const char *end = strchr (dirs, ':');
			const size_t len = end ? (size_t)(end - dirs) : strlen (dirs);
			if (len)
			{
				snprintf (found, sizeof (found), "%.*s/%s", (int)len, dirs, explicit_path);
				if (access (found, X_OK) == 0)
					return found;
			}
			dirs = end ? end + 1 : NULL;
		}
		if (access (explicit_path, X_OK) == 0)
			return explicit_path;
	}
	if (argv0 && *argv0)
	{
		const char *slash = strrchr (argv0, '/');
		if (slash)
		{
			snprintf (found, sizeof (found), "%.*s/vgmtrans", (int)(slash - argv0), argv0);
			if (access (found, X_OK) == 0)
				return found;
			snprintf (found, sizeof (found), "%.*s/vgmtrans-cli", (int)(slash - argv0), argv0);
			if (access (found, X_OK) == 0)
				return found;
		}
	}
	const char *dirs = getenv ("PATH");
	while (dirs && *dirs)
	{
		const char *end = strchr (dirs, ':');
		const size_t len = end ? (size_t)(end - dirs) : strlen (dirs);
		if (len)
		{
			snprintf (found, sizeof (found), "%.*s/vgmtrans", (int)len, dirs);
			if (access (found, X_OK) == 0)
				return found;
			snprintf (found, sizeof (found), "%.*s/vgmtrans-cli", (int)len, dirs);
			if (access (found, X_OK) == 0)
				return found;
		}
		dirs = end ? end + 1 : NULL;
	}
	return NULL;
}

static int run_external_vgmtrans (const char *tool, const char *in_file, const char *out_dir)
{
	char *const child_argv[] = { (char *)tool, (char *)in_file, (char *)out_dir, 0 };
	pid_t pid = fork ();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		execv (tool, child_argv);
		execvp (tool, child_argv);
		_Exit (127);
	}
	int status = 0;
	while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
		;
	if (WIFEXITED (status) && WEXITSTATUS (status) == 0)
		return 0;
	return -1;
}

static int cmd_pack (int argc, char *argv[])
{
	if (argc < 3)
	{
		fprintf (stderr,
			"wbrsar pack: missing input directory\n"
			"Usage: %s pack <input_dir> [output] [--bfsar|--bcsar|--sdat]\n",
			argv[0]);
		return ERR_SYNTAX;
	}

	ccp input_dir = argv[2];
	ccp output_path = 0;
	brsar_variant_t variant = BRSAR_VARIANT_RSAR;
	bool sdat = false;
	for (int i = 3; i < argc; i++)
	{
		if (!strcmp (argv[i], "--bfsar"))
			variant = BRSAR_VARIANT_FSAR;
		else if (!strcmp (argv[i], "--bcsar"))
			variant = BRSAR_VARIANT_CSAR;
		else if (!strcmp (argv[i], "--sdat"))
			sdat = true;
		else if (!output_path)
			output_path = argv[i];
	}

	char out_buf[1024];
	if (!output_path)
	{
		ccp ext = sdat						? ".sdat"
			: variant == BRSAR_VARIANT_FSAR ? ".bfsar"
			: variant == BRSAR_VARIANT_CSAR ? ".bcsar"
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
	enumError err = sdat ? PackSDATDir (&data, &size, input_dir)
						 : PackBRSARDir (&data, &size, input_dir, variant);
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
			"Usage: %s unpack <input.brsar|.bfsar|.bcsar|.sdat> [output_dir]\n",
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

	err = raw_size >= 4 && !memcmp (raw, "SDAT", 4) ? UnpackSDAT (raw, raw_size, out_dir)
													: UnpackBRSAR (raw, raw_size, out_dir);
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
	const char *opt_with_vgmtrans = NULL;
	bool force_external = false;

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
				"       %s pack   <input_dir> [output] [--bfsar|--bcsar|--sdat]\n"
				"       %s unpack <input.brsar|.bfsar|.bcsar|.sdat> [output_dir]\n\n"
				"Options:\n"
				"  --sf2              Export SoundFont 2 (.sf2) [default]\n"
				"  --dls              Export DLS (.dls)\n"
				"  --both             Export both .sf2 and .dls\n"
				"  --external         Force using external vgmtrans binary\n"
				"  --with-vgmtrans=P  Specify path to external vgmtrans tool\n"
				"  -d, --dest <dir>   Specify destination directory\n"
				"  -h, --help         Show this help\n\n"
				"pack: Build an archive from a directory of RSEQ (.txt MML source or\n"
				"      .rseq/.brseq binary) and RBNK/RWAR/RWSD asset files. Defaults to\n"
				"      BRSAR (Wii); --bfsar/--bcsar select the Wii U / 3DS container\n"
				"      instead (FSAR/CSAR layout is extrapolated, not independently\n"
				"      verified -- see lib-brsar.h).\n"
				"      --sdat builds a Nintendo DS archive from SSEQ/SBNK/SWAR files\n"
				"      (or .txt MML assembled as SSEQ).\n"
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
		else if (!strcmp (arg, "--external"))
			force_external = true;
		else if (!strncmp (arg, "--with-vgmtrans=", 16))
			opt_with_vgmtrans = arg + 16;
		else if (!strcmp (arg, "--with-vgmtrans"))
		{
			if (++i < argc)
				opt_with_vgmtrans = argv[i];
		}
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

	int err = -1;
#if !defined(NO_VGMTRANS) || !NO_VGMTRANS
	if (!force_external)
		err = VgmtransConvertFileExt (in_file, out_dir, format_flags);
#else
	(void)format_flags;
#endif
	if (err != 0 || force_external)
	{
		const char *ext_tool = find_vgmtrans_tool (opt_with_vgmtrans, argv[0]);
		if (ext_tool)
		{
			if (run_external_vgmtrans (ext_tool, in_file, out_dir) == 0)
			{
				printf ("wbrsar: converted %s -> %s (external %s)\n", in_file, out_dir, ext_tool);
				return 0;
			}
		}
		if (force_external)
		{
			fprintf (stderr, "wbrsar: external vgmtrans conversion failed for %s\n", in_file);
			return 1;
		}
	}
	if (err)
	{
		u8 *raw = 0;
		size_t raw_size = 0;
		enumError lerr = LoadFileAlloc (in_file, 0, 0, &raw, &raw_size, 0, 0, 0, false);
		if (!lerr && raw)
		{
			enumError uerr = raw_size >= 4 && !memcmp (raw, "SDAT", 4)
				? UnpackSDAT (raw, raw_size, out_dir)
				: UnpackBRSAR (raw, raw_size, out_dir);
			FREE (raw);
			if (!uerr)
			{
				printf ("wbrsar: unpacked raw sound assets %s -> %s\n", in_file, out_dir);
				return 0;
			}
		}
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
