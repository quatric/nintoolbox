static enumError extract_sequence_file (ccp arg, ccp basedir, uint depth)
{
	ccp ext = strrchr (arg, '.');
	if (!ext)
		return ERR_NOTHING_TO_DO;

	if (strcasecmp (ext, ".rseq") && strcasecmp (ext, ".brseq") && strcasecmp (ext, ".cseq")
		&& strcasecmp (ext, ".bcseq") && strcasecmp (ext, ".fseq") && strcasecmp (ext, ".bfseq")
		&& strcasecmp (ext, ".sseq") && strcasecmp (ext, ".bms") && strcasecmp (ext, ".bmc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 4)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	seq_format_t fmt = DetectSequenceFormat (raw, raw_size);
	if (fmt == SEQ_FMT_UNKNOWN)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest_txt[PATH_MAX], dest_mid[PATH_MAX];
	if (opt_dest)
	{
		bool is_dir = (opt_dest[strlen (opt_dest) - 1] == '/') || IsDirectory (opt_dest, 0);
		if (is_dir)
		{
			SubstDest (dest_txt, sizeof (dest_txt), arg, opt_dest, "%N.txt", ".txt", false);
			SubstDest (dest_mid, sizeof (dest_mid), arg, opt_dest, "%N.mid", ".mid", false);
		}
		else
		{
			SubstDest (dest_txt, sizeof (dest_txt), arg, opt_dest, 0, ".txt", false);
			size_t tlen = strlen (dest_txt);
			if (tlen > 4 && !strcasecmp (dest_txt + tlen - 4, ".txt"))
				snprintf (dest_mid, sizeof (dest_mid), "%.*s.mid", (int)(tlen - 4), dest_txt);
			else if (tlen > 4 && !strcasecmp (dest_txt + tlen - 4, ".mid"))
			{
				snprintf (dest_mid, sizeof (dest_mid), "%s", dest_txt);
				snprintf (dest_txt, sizeof (dest_txt), "%.*s.txt", (int)(tlen - 4), dest_mid);
			}
			else
				snprintf (dest_mid, sizeof (dest_mid), "%s.mid", dest_txt);
		}
	}
	else
	{
		snprintf (dest_txt, sizeof (dest_txt), "%s.txt", arg);
		snprintf (dest_mid, sizeof (dest_mid), "%s.mid", arg);
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s -> %s / %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", GetSequenceFormatName (fmt), arg, dest_txt, dest_mid);

	if (!testmode)
	{
		char *text = 0;
		size_t text_size = 0;
		if (DisassembleSequence (&text, &text_size, raw, raw_size) == ERR_OK && text)
		{
			File_t F;
			CreateFileOpt (&F, true, dest_txt, false, arg);
			if (F.f && fwrite (text, 1, text_size, F.f) != text_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", text_size, dest_txt);
			ResetFile (&F, opt_preserve);
			FREE (text);
		}

		u8 *midi = 0;
		size_t midi_size = 0;
		if (SequenceToMIDI (&midi, &midi_size, raw, raw_size) == ERR_OK && midi)
		{
			File_t F;
			CreateFileOpt (&F, true, dest_mid, false, arg);
			if (F.f && fwrite (midi, 1, midi_size, F.f) != midi_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", midi_size, dest_mid);
			ResetFile (&F, opt_preserve);
			FREE (midi);
		}

		// a sequence file holding several named sounds (SE banks): one MIDI per label, "<file>.<label>.mid"
		u32 lab_off[256];
		char lab_name[256][32];
		const uint nlab = SequenceLabelList (raw, raw_size, lab_off, lab_name, 256);
		for (uint li = 0; nlab > 1 && li < nlab; li++)
		{
			u8 *lm = 0;
			size_t lm_size = 0;
			if (SequenceToMIDIFrom (&lm, &lm_size, raw, raw_size, lab_off[li]) != ERR_OK || !lm)
				continue;
			char clean[32], lpath[PATH_MAX];
			snprintf (clean, sizeof (clean), "%s", lab_name[li]);
			for (char *c = clean; *c; c++)
				if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
					*c = '_';
			const size_t dl = strlen (dest_mid);
			snprintf (lpath, sizeof (lpath), "%.*s.%s.mid", (int)(dl > 4 && !strcasecmp (dest_mid + dl - 4, ".mid") ? dl - 4 : dl), dest_mid, clean);
			File_t LF;
			CreateFileOpt (&LF, true, lpath, false, arg);
			if (LF.f && fwrite (lm, 1, lm_size, LF.f) != lm_size)
				err = FILEERROR1 (&LF, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", lm_size, lpath);
			ResetFile (&LF, opt_preserve);
			FREE (lm);
		}
	}

	FREE (raw);
	return err;
}
