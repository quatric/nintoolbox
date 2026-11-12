// Sygnus ".soundstream" streams (SOL-AVES sound engine; Kirby Air Riders "Asset.Streaming/Sound"):
// AES-192-ECB encrypted files are decrypted first; the payload at 0x2c is the engine's 4-bit ADPCM
// (-> "<name>.wav"), Nintendo Switch Opus (-> "<name>.ogg" + ".wav") or a plain RIFF/WAVE.
// Sound effects are phx "PSND" records (".psnd" from a PARC catalog) embedding one such source.
static enumError extract_sygnus_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool is_psnd = is_ext (arg, ".psnd");
	if ((!is_ext (arg, ".soundstream") && !is_psnd) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	bool decrypted = false;
	uint po = SYGNUS_PAYLOAD;
	const sygnus_kind_t kind = is_psnd ? FindSygnusSource (raw, (uint)sz, &po)
									   : PrepareSygnusStream (raw, (uint)sz, &decrypted);

	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const char *sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot)
		strcpy (dot, ".wav");

	if (kind == SYGNUS_OPUS)
		err = switch_opus_decode_mem (arg, basedir, raw, sz, po);
	else if (kind == SYGNUS_RIFF)
	{
		const u8 *w = raw + po;
		size_t wn = (size_t)le32 (w + 4) + 8;
		if (wn > sz - po)
			wn = sz - po;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SYGNUS-RIFF:%s -> WAV\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, w, (uint)wn);
	}
	else if (kind == SYGNUS_ADPCM)
	{
		s16 *pcm = 0;
		uint ch = 0, rate = 0, ns = 0;
		if (!DecodeSygnusADPCM (&pcm, &ch, &rate, &ns, raw + po, (uint)(sz - po)))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE SYGNUS-ADPCM:%s (%u ch, %u Hz, %u samples%s) -> WAV\n",
					verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns,
					decrypted ? ", decrypted" : "");
			if (testmode)
				err = ERR_OK;
			else
			{
				size_t wn = 0;
				u8 *w = nds_strm_make_wav (pcm, ns, ch, rate, &wn);
				err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
				FREE (w);
			}
		}
		FREE (pcm);
	}
	FREE (raw);
	return err;
}
