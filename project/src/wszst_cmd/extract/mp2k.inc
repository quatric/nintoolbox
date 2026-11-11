#include "lib-mp2k.h"
// GBA MusicPlayer2000 ("Sappy"/m4a) ROMs (".gba"): the sound engine is located and every song of its table is converted to
// "song_NNN.mid" in a "<name>.d" folder beside the source.
static enumError extract_mp2k_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".gba") && !is_ext (arg, ".agb")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX], out[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	snprintf (out, sizeof (out), "%s/%s", dest, basedir ? basedir : "");
	enumError err;
	if (testmode)
		err = ERR_OK;
	else
	{
		uint n = 0;
		err = ConvertMP2kROM (raw, sz, out, &n);
		if (!err && (verbose >= 0))
			fprintf (stdlog, "EXTRACT MP2K:%s -> %u MIDI:%s\n", arg, n, out);
	}
	FREE (raw);
	return err;
}
