// wrbnk - Wiimms RBNK Tool
// Dumps a Wii RBNK instrument bank (program -> note-range -> InstParam
// lookup tree, plus embedded WaveInfo metadata for version < 2 banks) as a
// lossless-structure XML. Container/tree-walk logic is a documented port
// of BrawlLib's real RBNK parser (soopercool101/BrawlCrate) -- see
// lib-rbnk.h for provenance and scope.

#include <stdio.h>
#include <string.h>
#include "lib-std.h"
#include "lib-rbnk.h"

static void print_usage (ccp prog)
{
	printf ("wrbnk - Wiimms RBNK Tool\n"
			"Dumps or compiles a Wii RBNK instrument bank as lossless-structure XML.\n\n"
			"Usage:\n"
			"  %s dump <input.rbnk> [output.xml]\n"
			"  %s compile|encode <input.xml> [output.rbnk]\n",
		prog, prog);
}

int main (int argc, char **argv)
{
	stdlog = stderr; // unset otherwise (this tool skips wszst.c's usual startup init)

	if (argc < 3)
	{
		print_usage (argv[0]);
		return ERR_SYNTAX;
	}

	const bool is_dump = !strcasecmp (argv[1], "dump") || !strcasecmp (argv[1], "d");
	const bool is_compile = !strcasecmp (argv[1], "compile") || !strcasecmp (argv[1], "encode")
		|| !strcasecmp (argv[1], "c") || !strcasecmp (argv[1], "e");

	if (!is_dump && !is_compile)
	{
		print_usage (argv[0]);
		return ERR_SYNTAX;
	}

	ccp input_path = argv[2];
	ccp output_path = argc > 3 ? argv[3] : 0;
	char out_buf[PATH_MAX];

	if (is_dump)
	{
		if (!output_path)
		{
			snprintf (out_buf, sizeof (out_buf), "%s.xml", input_path);
			output_path = out_buf;
		}

		u8 *raw = 0;
		size_t raw_size = 0;
		enumError err = LoadFileAlloc (input_path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
		if (err)
		{
			fprintf (stderr, "Error: failed to load input file '%s'\n", input_path);
			return err;
		}

		rbnk_t rbnk;
		err = ScanRBNK (&rbnk, raw, (uint)raw_size);
		FREE (raw);
		if (err)
			return err;

		File_t F;
		err = CreateFileOpt (&F, true, output_path, false, input_path);
		if (!err && F.f)
		{
			DumpRBNK_XML (&rbnk, F.f, input_path);
			printf ("wrbnk: dumped %s -> %s (v%u.%u, %u programs, %u waves)\n", input_path,
				output_path, rbnk.version_major, rbnk.version_minor, rbnk.n_program, rbnk.n_wave);
		}
		ResetFile (&F, 0);
		ResetRBNK (&rbnk);
		return err;
	}
	else
	{
		if (!output_path)
		{
			size_t in_len = strlen (input_path);
			if (in_len > 4 && !strcasecmp (input_path + in_len - 4, ".xml"))
				snprintf (out_buf, sizeof (out_buf), "%.*s", (int)(in_len - 4), input_path);
			else
				snprintf (out_buf, sizeof (out_buf), "%s.rbnk", input_path);
			output_path = out_buf;
		}

		u8 *raw = 0;
		size_t raw_size = 0;
		enumError err = LoadFileAlloc (input_path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
		if (err)
		{
			fprintf (stderr, "Error: failed to load input XML '%s'\n", input_path);
			return err;
		}

		rbnk_t rbnk;
		err = ParseRBNK_XML (&rbnk, (const char *)raw, raw_size);
		FREE (raw);
		if (err)
			return err;

		u8 *bin_data = 0;
		uint bin_size = 0;
		err = EncodeRBNK (&rbnk, &bin_data, &bin_size);
		if (err)
		{
			ResetRBNK (&rbnk);
			return err;
		}

		File_t F;
		err = CreateFileOpt (&F, true, output_path, false, input_path);
		if (!err && F.f)
		{
			if (fwrite (bin_data, 1, bin_size, F.f) != bin_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", bin_size, output_path);
			else
				printf ("wrbnk: compiled %s -> %s (v%u.%u, %u programs, %u waves, %u bytes)\n",
					input_path, output_path, rbnk.version_major, rbnk.version_minor, rbnk.n_program,
					rbnk.n_wave, bin_size);
		}
		ResetFile (&F, 0);
		FREE (bin_data);
		ResetRBNK (&rbnk);
		return err;
	}
}

bool DefineIntVar (VarMap_t *vm, ccp varname, int value)
{
	return false;
}
