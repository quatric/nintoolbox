	if (err)
	{
		ResetSZS (&szs);

		// Last-resort fallback, tried only once the real U8/SARC/etc. parser
		// above has declined the file outright: an otherwise-opaque blob may
		// still carry self-describing embedded resources worth pulling out
		// (see extract_embedded_nw4r_file()'s own comment). This must run
		// AFTER LoadCreateSZS(), not before -- a legitimate U8 archive (e.g.
		// a Wii layout .arc bundling a BRLYT/BRLAN pair as regular SARC
		// members) contains exactly the byte patterns this scanner looks
		// for, so running it first stole real archives -- including their
		// textures -- and left only the two raw layout blobs it knows how
		// to name, silently dropping everything else in the archive.
		enumError nw4r_err = extract_embedded_nw4r_file (arg, basedir, depth);
		if (nw4r_err != ERR_NOTHING_TO_DO)
			return nw4r_err;

		return depth > 0 ? ERR_OK : err;
	}
