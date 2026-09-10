// wbfsar - Wiimms BFSAR/BCSAR Tool
// Dumps a Wii U/Switch (FSAR) or 3DS (CSAR) Sound Archive's real directory
// -- every Sound/Bank/Player/WaveArchive/SoundGroup/Group entry's index, Id
// and real name -- as a lossless-structure XML, and extracts every mapped
// FILE-pool asset (sequences, banks, wave archives, waves) to individual
// files. Container logic is a documented port of the real, written Citric
// Composer spec + source (Gota7/Citric-Composer), verified against real
// retail data -- see lib-bfsar.h for provenance and scope.

#include <stdio.h>
#include <string.h>
#include "lib-std.h"
#include "lib-bfsar.h"

static void print_usage (ccp prog)
{
	printf ("wbfsar - Wiimms BFSAR/BCSAR Tool\n"
		"Dumps a Wii U/Switch/3DS Sound Archive's directory as a lossless-structure XML,\n"
		"or extracts its FILE-pool assets to individual files.\n\n"
		"Usage:\n"
		"  %s dump <input.bfsar|.bcsar> [output.xml]\n"
		"  %s extract <input.bfsar|.bcsar> [output_dir]\n",
		prog, prog);
}

static int cmd_dump (ccp input_path, ccp output_path)
{
	char out_buf[PATH_MAX];
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

	bfsar_t bfsar;
	err = ScanBFSAR (&bfsar, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return err;

	File_t F;
	err = CreateFileOpt (&F, true, output_path, false, input_path);
	if (!err && F.f)
	{
		DumpBFSAR_XML (&bfsar, F.f, input_path);
		uint total = 0;
		for (uint t = 0; t < bfsar.n_table; t++)
			total += bfsar.table[t].n_entry;
		printf ("wbfsar: dumped %s -> %s (%s v%u.%u.%u, %u tables, %u entries)\n", input_path,
			output_path, bfsar.is_ctr ? "CSAR" : "FSAR", bfsar.version_major, bfsar.version_minor,
			bfsar.version_revision, bfsar.n_table, total);
	}
	ResetFile (&F, 0);
	ResetBFSAR (&bfsar);
	return err;
}

static int cmd_extract (ccp input_path, ccp output_path)
{
	char out_buf[PATH_MAX];
	if (!output_path)
	{
		snprintf (out_buf, sizeof (out_buf), "%s.d", input_path);
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

	bfsar_t bfsar;
	err = ScanBFSAR (&bfsar, raw, (uint)raw_size);
	if (!err)
	{
		uint n = 0;
		err = ExtractBFSARFiles (&bfsar, raw, (uint)raw_size, output_path, &n);
		if (!err)
			printf ("wbfsar: extracted %s -> %s (%u assets)\n", input_path, output_path, n);
	}
	FREE (raw);
	ResetBFSAR (&bfsar);
	return err;
}

int main (int argc, char **argv)
{
	stdlog = stderr; // unset otherwise (this tool skips wszst.c's usual startup init)

	if (argc < 3 || (strcasecmp (argv[1], "dump") && strcasecmp (argv[1], "extract")))
	{
		print_usage (argv[0]);
		return ERR_SYNTAX;
	}

	if (!strcasecmp (argv[1], "dump"))
		return cmd_dump (argv[2], argc > 3 ? argv[3] : 0);
	return cmd_extract (argv[2], argc > 3 ? argv[3] : 0);
}

bool DefineIntVar (VarMap_t *vm, ccp varname, int value)
{
	return false;
}
