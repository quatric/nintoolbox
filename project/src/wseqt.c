// wseqt - NintendoWare sequence tool (GotaSequenceCmd port)
//
// Provides disassembly, assembly, MIDI conversion, and sequence manipulation
// for RSEQ (Wii), CSEQ (3DS), FSEQ (Wii U / Switch), and SSEQ (NDS).

#include <stdio.h>
#include <string.h>
#include "lib-std.h"
#include "lib-sequence.h"

static void print_usage (const char *prog)
{
	printf ("wseqt - NintendoWare sequence bytecode compiler and MIDI tool\n"
			"Supports: RSEQ (Wii), CSEQ (3DS), FSEQ (Wii U / Switch), SSEQ (NDS)\n\n"
			"Usage:\n"
			"  %s disasm    <input.rseq|cseq|fseq|sseq> [output.txt]\n"
			"  %s asm       <input.txt> [output.rseq|cseq|fseq|sseq] [--format <fmt>]\n"
			"  %s to_midi   <input.rseq|cseq|fseq|sseq> [output.mid]\n"
			"  %s from_midi <input.mid> [output.rseq|cseq|fseq|sseq] [--format <fmt>]\n"
			"  %s invert    <input.rseq|cseq|fseq|sseq> [output.rseq] [--center <note>]\n"
			"  %s info      <input.rseq|cseq|fseq|sseq>\n\n"
			"Formats for --format: RSEQ (default), CSEQ, FSEQ, FSEQ_LE, SSEQ\n",
		prog, prog, prog, prog, prog, prog);
}

int main (int argc, char **argv)
{
	if (argc < 3)
	{
		print_usage (argv[0]);
		return ERR_SYNTAX;
	}

	ccp cmd = argv[1];
	ccp input_path = argv[2];
	char out_buf[PATH_MAX] = "";
	ccp output_path = 0;
	seq_format_t target_fmt = SEQ_FMT_RSEQ;
	int center_note = 63;

	for (int i = 3; i < argc; i++)
	{
		if (!strcasecmp (argv[i], "--format") || !strcasecmp (argv[i], "-f"))
		{
			if (i + 1 < argc)
				target_fmt = ParseSequenceFormatName (argv[++i]);
		}
		else if (!strcasecmp (argv[i], "--center") || !strcasecmp (argv[i], "-c"))
		{
			if (i + 1 < argc)
				center_note = atoi (argv[++i]);
		}
		else if (!output_path && argv[i][0] != '-')
		{
			output_path = argv[i];
		}
	}

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (input_path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
	{
		fprintf (stderr, "Error: failed to load input file '%s'\n", input_path);
		return err;
	}

	if (!strcasecmp (cmd, "disasm") || !strcasecmp (cmd, "disassemble") || !strcasecmp (cmd, "d"))
	{
		if (!output_path)
		{
			snprintf (out_buf, sizeof (out_buf), "%s.txt", input_path);
			output_path = out_buf;
		}

		char *text = 0;
		size_t text_size = 0;
		err = DisassembleSequence (&text, &text_size, raw, raw_size);
		if (!err && text)
		{
			File_t F;
			err = CreateFileOpt (&F, true, output_path, false, input_path);
			if (F.f && fwrite (text, 1, text_size, F.f) != text_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", text_size, output_path);
			ResetFile (&F, 0);
			FREE (text);
			printf ("Disassembled %s -> %s (%zu bytes)\n", input_path, output_path, text_size);
		}
	}
	else if (!strcasecmp (cmd, "asm") || !strcasecmp (cmd, "assemble") || !strcasecmp (cmd, "a"))
	{
		if (!output_path)
		{
			const char *ext = (target_fmt == SEQ_FMT_CSEQ)						   ? ".cseq"
				: (target_fmt == SEQ_FMT_FSEQ_BE || target_fmt == SEQ_FMT_FSEQ_LE) ? ".fseq"
				: (target_fmt == SEQ_FMT_SSEQ)									   ? ".sseq"
																				   : ".rseq";
			snprintf (out_buf, sizeof (out_buf), "%.*s%s",
				(int)(strlen (input_path) > 4
							&& !strcasecmp (input_path + strlen (input_path) - 4, ".txt")
						? strlen (input_path) - 4
						: strlen (input_path)),
				input_path, ext);
			output_path = out_buf;
		}

		u8 *bin = 0;
		size_t bin_size = 0;
		err = AssembleSequence (&bin, &bin_size, (const char *)raw, target_fmt);
		if (!err && bin)
		{
			File_t F;
			err = CreateFileOpt (&F, true, output_path, false, input_path);
			if (F.f && fwrite (bin, 1, bin_size, F.f) != bin_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", bin_size, output_path);
			ResetFile (&F, 0);
			FREE (bin);
			printf ("Assembled %s -> %s (%zu bytes, format: %s)\n", input_path, output_path,
				bin_size, GetSequenceFormatName (target_fmt));
		}
	}
	else if (!strcasecmp (cmd, "to_midi") || !strcasecmp (cmd, "to-midi")
		|| !strcasecmp (cmd, "mid"))
	{
		if (!output_path)
		{
			snprintf (out_buf, sizeof (out_buf), "%s.mid", input_path);
			output_path = out_buf;
		}

		u8 *midi = 0;
		size_t midi_size = 0;
		err = SequenceToMIDI (&midi, &midi_size, raw, raw_size);
		if (!err && midi)
		{
			File_t F;
			err = CreateFileOpt (&F, true, output_path, false, input_path);
			if (F.f && fwrite (midi, 1, midi_size, F.f) != midi_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", midi_size, output_path);
			ResetFile (&F, 0);
			FREE (midi);
			printf ("Converted %s -> %s (%zu bytes MIDI)\n", input_path, output_path, midi_size);
		}
	}
	else if (!strcasecmp (cmd, "from_midi") || !strcasecmp (cmd, "from-midi"))
	{
		if (!output_path)
		{
			const char *ext = (target_fmt == SEQ_FMT_CSEQ)						   ? ".cseq"
				: (target_fmt == SEQ_FMT_FSEQ_BE || target_fmt == SEQ_FMT_FSEQ_LE) ? ".fseq"
				: (target_fmt == SEQ_FMT_SSEQ)									   ? ".sseq"
																				   : ".rseq";
			snprintf (out_buf, sizeof (out_buf), "%s%s", input_path, ext);
			output_path = out_buf;
		}

		u8 *seq = 0;
		size_t seq_size = 0;
		err = SequenceFromMIDI (&seq, &seq_size, raw, raw_size, target_fmt);
		if (!err && seq)
		{
			File_t F;
			err = CreateFileOpt (&F, true, output_path, false, input_path);
			if (F.f && fwrite (seq, 1, seq_size, F.f) != seq_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", seq_size, output_path);
			ResetFile (&F, 0);
			FREE (seq);
			printf ("Converted MIDI %s -> %s (%zu bytes, format: %s)\n", input_path, output_path,
				seq_size, GetSequenceFormatName (target_fmt));
		}
	}
	else if (!strcasecmp (cmd, "invert"))
	{
		if (!output_path)
		{
			snprintf (out_buf, sizeof (out_buf), "%s.inv.rseq", input_path);
			output_path = out_buf;
		}

		u8 *inv = 0;
		size_t inv_size = 0;
		err = InvertSequence (&inv, &inv_size, raw, raw_size, center_note);
		if (!err && inv)
		{
			File_t F;
			err = CreateFileOpt (&F, true, output_path, false, input_path);
			if (F.f && fwrite (inv, 1, inv_size, F.f) != inv_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", inv_size, output_path);
			ResetFile (&F, 0);
			FREE (inv);
			printf ("Inverted %s -> %s (center: %d)\n", input_path, output_path, center_note);
		}
	}
	else if (!strcasecmp (cmd, "info"))
	{
		seq_format_t fmt = DetectSequenceFormat (raw, raw_size);
		printf ("File: %s\nSize: %zu bytes\nDetected Format: %s\n", input_path, raw_size,
			GetSequenceFormatName (fmt));
	}
	else
	{
		fprintf (stderr, "Unknown command '%s'\n", cmd);
		print_usage (argv[0]);
		err = ERR_SYNTAX;
	}

	FREE (raw);
	return err;
}

bool DefineIntVar (VarMap_t *vm, ccp varname, int value)
{
	return false;
}
