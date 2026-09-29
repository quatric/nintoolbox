// The Voice (Wii karaoke) plain-text asset scripts: all five share this
// shape, differing only in extension, detector and decoder -- see
// lib-thevoice.h.
#define DEFINE_EXTRACT_VOICE(name, ext, is_fn, decode_fn)                                          \
	static enumError extract_voice_##name##_manifest (ccp arg)                                     \
	{                                                                                              \
		if (!is_ext (arg, ext))                                                                    \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		u8 *raw = 0;                                                                               \
		size_t raw_size = 0;                                                                       \
		enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);                \
		if (err)                                                                                   \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		if (!is_fn (raw, raw_size, raw_size))                                                      \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_NOTHING_TO_DO;                                                              \
		}                                                                                          \
                                                                                                   \
		char dest[PATH_MAX];                                                                       \
		if (opt_dest)                                                                              \
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);                      \
		else                                                                                       \
			snprintf (dest, sizeof (dest), "%s.txt", arg);                                         \
                                                                                                   \
		if (verbose >= 0 || testmode)                                                              \
			fprintf (stdlog, "%s%sDECODE VOICE-" #name ":%s -> %s\n", verbose > 0 ? "\n" : "",     \
				testmode ? "WOULD " : "", arg, dest);                                              \
                                                                                                   \
		if (testmode)                                                                              \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_OK;                                                                         \
		}                                                                                          \
                                                                                                   \
		File_t F;                                                                                  \
		err = CreateFileOpt (&F, true, dest, false, arg);                                          \
		if (F.f)                                                                                   \
			err = decode_fn (F.f, raw, raw_size, raw_size);                                        \
		ResetFile (&F, opt_preserve);                                                              \
		FREE (raw);                                                                                \
		return err;                                                                                \
	}

DEFINE_EXTRACT_VOICE (song, ".song", IsVoiceSong, DecodeVoiceSong_Text)
DEFINE_EXTRACT_VOICE (amc, ".amc", IsVoiceAmc, DecodeVoiceAmc_Text)
DEFINE_EXTRACT_VOICE (ams, ".ams", IsVoiceAms, DecodeVoiceAms_Text)
DEFINE_EXTRACT_VOICE (palcat, ".palcat", IsVoicePalcat, DecodeVoicePalcat_Text)
DEFINE_EXTRACT_VOICE (palseq, ".palseq", IsVoicePalseq, DecodeVoicePalseq_Text)

#undef DEFINE_EXTRACT_VOICE

// The Dog Island (Wii) formats -- differ only in extension-match
// expression, detector and decoder; see lib-dogisland.h. 'ext_expr' is a
// full boolean expression (built from is_ext()) so a decoder can be
// shared across multiple on-disc extensions (".mtt"/".ypc"/".pac" and
// ".efi"/".sci"/".qci" both reuse the same on-disc container/probe).
#define DEFINE_EXTRACT_DOGISLAND(name, ext_expr, is_fn, decode_fn)                                 \
	static enumError extract_dogisland_##name##_manifest (ccp arg)                                 \
	{                                                                                              \
		if (!(ext_expr))                                                                           \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		u8 *raw = 0;                                                                               \
		size_t raw_size = 0;                                                                       \
		enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);                \
		if (err)                                                                                   \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		if (!is_fn (raw, raw_size, raw_size))                                                      \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_NOTHING_TO_DO;                                                              \
		}                                                                                          \
                                                                                                   \
		char dest[PATH_MAX];                                                                       \
		if (opt_dest)                                                                              \
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);                      \
		else                                                                                       \
			snprintf (dest, sizeof (dest), "%s.txt", arg);                                         \
                                                                                                   \
		if (verbose >= 0 || testmode)                                                              \
			fprintf (stdlog, "%s%sDECODE DOGISLAND-" #name ":%s -> %s\n", verbose > 0 ? "\n" : "", \
				testmode ? "WOULD " : "", arg, dest);                                              \
                                                                                                   \
		if (testmode)                                                                              \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_OK;                                                                         \
		}                                                                                          \
                                                                                                   \
		File_t F;                                                                                  \
		err = CreateFileOpt (&F, true, dest, false, arg);                                          \
		if (F.f)                                                                                   \
			err = decode_fn (F.f, raw, raw_size, raw_size);                                        \
		ResetFile (&F, opt_preserve);                                                              \
		FREE (raw);                                                                                \
		return err;                                                                                \
	}

DEFINE_EXTRACT_DOGISLAND (wds, is_ext (arg, ".wds"), IsDogIslandWds, DecodeDogIslandWds_Text)
DEFINE_EXTRACT_DOGISLAND (wdb, is_ext (arg, ".wdb"), IsDogIslandWdb, DecodeDogIslandWdb_Text)
DEFINE_EXTRACT_DOGISLAND (
	yobj, is_ext (arg, ".ymg") || is_ext (arg, ".ymm"), IsDogIslandYobj, DecodeDogIslandYobj_Text)
DEFINE_EXTRACT_DOGISLAND (pms, is_ext (arg, ".pms"), IsDogIslandPms, DecodeDogIslandPms_Text)
DEFINE_EXTRACT_DOGISLAND (mtt, is_ext (arg, ".mtt") || is_ext (arg, ".ypc") || is_ext (arg, ".pac"),
	IsDogIslandMtt, DecodeDogIslandMtt_Text)
DEFINE_EXTRACT_DOGISLAND (cprm, is_ext (arg, ".cprm"), IsDogIslandCprm, DecodeDogIslandCprm_Text)
DEFINE_EXTRACT_DOGISLAND (script,
	is_ext (arg, ".efi") || is_ext (arg, ".sci") || is_ext (arg, ".qci"), IsDogIslandScript,
	DecodeDogIslandScript_Text)
DEFINE_EXTRACT_DOGISLAND (mpq, is_ext (arg, ".mpq"), IsDogIslandMpq, DecodeDogIslandMpq_Text)

#undef DEFINE_EXTRACT_DOGISLAND

// Muramasa - The Demon Blade (Wii) formats -- see lib-muramasa.h. The
// "FCMP" container decoder is shared across all six on-disc extensions it
// wraps, since detection/decode only depend on the outer header + inner
// sub-blob tag, not the extension itself.
#define DEFINE_EXTRACT_MURAMASA(name, ext_expr, is_fn, decode_fn)                                  \
	static enumError extract_muramasa_##name##_manifest (ccp arg)                                  \
	{                                                                                              \
		if (!(ext_expr))                                                                           \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		u8 *raw = 0;                                                                               \
		size_t raw_size = 0;                                                                       \
		enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);                \
		if (err)                                                                                   \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		if (!is_fn (raw, raw_size, raw_size))                                                      \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_NOTHING_TO_DO;                                                              \
		}                                                                                          \
                                                                                                   \
		char dest[PATH_MAX];                                                                       \
		if (opt_dest)                                                                              \
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);                      \
		else                                                                                       \
			snprintf (dest, sizeof (dest), "%s.txt", arg);                                         \
                                                                                                   \
		if (verbose >= 0 || testmode)                                                              \
			fprintf (stdlog, "%s%sDECODE MURAMASA-" #name ":%s -> %s\n", verbose > 0 ? "\n" : "",  \
				testmode ? "WOULD " : "", arg, dest);                                              \
                                                                                                   \
		if (testmode)                                                                              \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_OK;                                                                         \
		}                                                                                          \
                                                                                                   \
		File_t F;                                                                                  \
		err = CreateFileOpt (&F, true, dest, false, arg);                                          \
		if (F.f)                                                                                   \
			err = decode_fn (F.f, raw, raw_size, raw_size);                                        \
		ResetFile (&F, opt_preserve);                                                              \
		FREE (raw);                                                                                \
		return err;                                                                                \
	}

DEFINE_EXTRACT_MURAMASA (fcmp,
	is_ext (arg, ".mbs") || is_ext (arg, ".ftx") || is_ext (arg, ".esb") || is_ext (arg, ".nsb")
		|| is_ext (arg, ".abf") || is_ext (arg, ".nms") || is_ext (arg, ".wbf"),
	IsMuramasaFcmp, DecodeMuramasaFcmp_Text)
DEFINE_EXTRACT_MURAMASA (otb, is_ext (arg, ".otb"), IsMuramasaOtb, DecodeMuramasaOtb_Text)
DEFINE_EXTRACT_MURAMASA (nsi, is_ext (arg, ".nsi"), IsMuramasaNsi, DecodeMuramasaNsi_Text)

#undef DEFINE_EXTRACT_MURAMASA

// Red Steel 2 (Wii) formats -- see lib-redsteel2.h. Same manifest-emitting
// shape as the Muramasa formats above.
#define DEFINE_EXTRACT_REDSTEEL2(name, ext_expr, is_fn, decode_fn)                                 \
	static enumError extract_redsteel2_##name##_manifest (ccp arg)                                 \
	{                                                                                              \
		if (!(ext_expr))                                                                           \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		u8 *raw = 0;                                                                               \
		size_t raw_size = 0;                                                                       \
		enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);                \
		if (err)                                                                                   \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		if (!is_fn (raw, raw_size, raw_size))                                                      \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_NOTHING_TO_DO;                                                              \
		}                                                                                          \
                                                                                                   \
		char dest[PATH_MAX];                                                                       \
		if (opt_dest)                                                                              \
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);                      \
		else                                                                                       \
			snprintf (dest, sizeof (dest), "%s.txt", arg);                                         \
                                                                                                   \
		if (verbose >= 0 || testmode)                                                              \
			fprintf (stdlog, "%s%sDECODE REDSTEEL2-" #name ":%s -> %s\n", verbose > 0 ? "\n" : "", \
				testmode ? "WOULD " : "", arg, dest);                                              \
                                                                                                   \
		if (testmode)                                                                              \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_OK;                                                                         \
		}                                                                                          \
                                                                                                   \
		File_t F;                                                                                  \
		err = CreateFileOpt (&F, true, dest, false, arg);                                          \
		if (F.f)                                                                                   \
			err = decode_fn (F.f, raw, raw_size, raw_size);                                        \
		ResetFile (&F, opt_preserve);                                                              \
		FREE (raw);                                                                                \
		return err;                                                                                \
	}

DEFINE_EXTRACT_REDSTEEL2 (
	abe, is_ext (arg, ".bbf") || is_ext (arg, ".bf"), IsRedSteel2Abe, DecodeRedSteel2Abe_Text)
DEFINE_EXTRACT_REDSTEEL2 (rel, is_ext (arg, ".rel"), IsRedSteel2Rel, DecodeRedSteel2Rel_Text)

#undef DEFINE_EXTRACT_REDSTEEL2

// Zack & Wiki: Quest for Barbaros' Treasure (Wii) formats -- see
// lib-zackwiki.h. Same manifest-emitting shape as the formats above.
#define DEFINE_EXTRACT_ZACKWIKI(name, ext_expr, is_fn, decode_fn)                                  \
	static enumError extract_zackwiki_##name##_manifest (ccp arg)                                  \
	{                                                                                              \
		if (!(ext_expr))                                                                           \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		u8 *raw = 0;                                                                               \
		size_t raw_size = 0;                                                                       \
		enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);                \
		if (err)                                                                                   \
			return ERR_NOTHING_TO_DO;                                                              \
                                                                                                   \
		if (!is_fn (raw, raw_size, raw_size))                                                      \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_NOTHING_TO_DO;                                                              \
		}                                                                                          \
                                                                                                   \
		char dest[PATH_MAX];                                                                       \
		if (opt_dest)                                                                              \
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);                      \
		else                                                                                       \
			snprintf (dest, sizeof (dest), "%s.txt", arg);                                         \
                                                                                                   \
		if (verbose >= 0 || testmode)                                                              \
			fprintf (stdlog, "%s%sDECODE ZACKWIKI-" #name ":%s -> %s\n", verbose > 0 ? "\n" : "",  \
				testmode ? "WOULD " : "", arg, dest);                                              \
                                                                                                   \
		if (testmode)                                                                              \
		{                                                                                          \
			FREE (raw);                                                                            \
			return ERR_OK;                                                                         \
		}                                                                                          \
                                                                                                   \
		File_t F;                                                                                  \
		err = CreateFileOpt (&F, true, dest, false, arg);                                          \
		if (F.f)                                                                                   \
			err = decode_fn (F.f, raw, raw_size, raw_size);                                        \
		ResetFile (&F, opt_preserve);                                                              \
		FREE (raw);                                                                                \
		return err;                                                                                \
	}

DEFINE_EXTRACT_ZACKWIKI (tm2, is_ext (arg, ".tm2"), IsZackWikiTm2, DecodeZackWikiTm2_Text)
DEFINE_EXTRACT_ZACKWIKI (ppg, is_ext (arg, ".ppg"), IsZackWikiPpg, DecodeZackWikiPpg_Text)
DEFINE_EXTRACT_ZACKWIKI (tsb, is_ext (arg, ".tsb"), IsZackWikiTsb, DecodeZackWikiTsb_Text)
DEFINE_EXTRACT_ZACKWIKI (whd, is_ext (arg, ".whd"), IsZackWikiWhd, DecodeZackWikiWhd_Text)
DEFINE_EXTRACT_ZACKWIKI (mds, is_ext (arg, ".mds"), IsZackWikiMds, DecodeZackWikiMds_Text)
DEFINE_EXTRACT_ZACKWIKI (ssd, is_ext (arg, ".ssd"), IsZackWikiSsd, DecodeZackWikiSsd_Text)

#undef DEFINE_EXTRACT_ZACKWIKI

static enumError extract_mtmrl_manifest (ccp arg)
{
	if (!is_ext (arg, ".mrl"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsMTMRL (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE MTMRL:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f)
		err = DecodeMTMRL_Text (F.f, raw, raw_size);
	ResetFile (&F, opt_preserve);
	FREE (raw);
	return err;
}
