// Text manifests for Wii formats whose lib-*.c decoders were only reachable through FILETYPE: the Planet-G engine (.MWG/.MSP/.PKI/.pgf; .MWT textures and
// two-colour .pgf font atlases decode to PNG beside the source;
// Bermuda Triangle, Rock Blast, Veggy World, ...), the Eko Software engine (.rck/.spa/.mb2/.vis/.lit/.mat; Aqua Panic!, SPRay, Cyberbike, ...),
// Mercury Meltdown Revolution, Octomania, I Spy Spooky Mansion, How to Train Your Dragon, Tenchu (.b/.hd), Havok .hkx and
// Battle of the Bands .bag. The extension only gates the probe; the matching Is*() detector validates the content. Output goes to
// "<file>.d/<basename>.txt". Dispatched last in the chain so magic-validated handlers (EA .ast, Capcom .col, Tenchu .hd ...) win first;
// table order matters for shared extensions (Eko MATF .mat before the Mercury fallback).
#include "lib-bermudatriangle.h"
#include "lib-aquapanic.h"
#include "lib-mercurymeltdown.h"
#include "lib-octomania.h"
#include "lib-ispyspookymansion.h"
#include "lib-httyd.h"
#include "lib-t4res.h"
#include "lib-hdvoice.h"
#include "lib-hkx.h"
#include "lib-botbbag.h"

typedef struct wii_text_def_t
{
	ccp ext;
	int (*is) (const u8 *data, size_t size, size_t file_size);
	enumError (*dec) (FILE *f, const u8 *data, size_t size, size_t file_size);
} wii_text_def_t;

static const wii_text_def_t wii_text_defs[] =
{
	{ ".mwg", IsBermudaPlanetG, DecodeBermudaPlanetG_Text }, { ".msp", IsBermudaPlanetG, DecodeBermudaPlanetG_Text },
	{ ".pki", IsBermudaPki, DecodeBermudaPki_Text }, { ".pgf", IsBermudaPgf, DecodeBermudaPgf_Text },
	{ ".rck", IsAquaPanicRket, DecodeAquaPanicRket_Text }, { ".spa", IsAquaPanicRket, DecodeAquaPanicRket_Text }, { ".mb2", IsAquaPanicMb2, DecodeAquaPanicMb2_Text },
	{ ".vis", IsAquaPanicVis, DecodeAquaPanicVis_Text }, { ".lit", IsAquaPanicLit, DecodeAquaPanicLit_Text }, { ".mat", IsAquaPanicMat, DecodeAquaPanicMat_Text },
	{ ".zen", IsMercuryZen, DecodeMercuryZen_Text }, { ".col", IsMercuryCol, DecodeMercuryCol_Text }, { ".pst", IsMercuryPst, DecodeMercuryPst_Text },
	{ ".mat", IsMercuryMat, DecodeMercuryMat_Text }, { ".nav", IsMercuryNav, DecodeMercuryNav_Text },
	{ ".sec", IsOctomaniaSec, DecodeOctomaniaSec_Text }, { ".wt", IsOctomaniaWt, DecodeOctomaniaWt_Text },
	{ ".eid", IsSpookyEid, DecodeSpookyEid_Text }, { ".ast", IsSpookyAst, DecodeSpookyAst_Text }, { ".sdf", IsSpookySdf, DecodeSpookySdf_Text }, { ".ges", IsSpookyGes, DecodeSpookyGes_Text },
	{ ".rws", IsHTTYDRws, DecodeHTTYDRws_Text }, { ".krv", IsHTTYDKrv, DecodeHTTYDKrv_Text },
	{ ".b", IsT4Res, DecodeT4Res_Text }, { ".hd", IsHdVoice, DecodeHdVoice_Text }, { ".hkx", IsHKX, DecodeHKX_Text }, { ".bag", IsBotbBag, DecodeBotbBag_Text },
};

static enumError decode_image_if_possible (ccp arg); // formats/sprites.inc

static enumError extract_wii_text_manifest_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (export_count == 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	uint n_ext = 0;
	for (uint k = 0; k < sizeof (wii_text_defs) / sizeof (*wii_text_defs); k++)
		if (is_ext (arg, wii_text_defs[k].ext))
			n_ext++;
	if (!n_ext)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const wii_text_def_t *def = 0;
	for (uint k = 0; k < sizeof (wii_text_defs) / sizeof (*wii_text_defs) && !def; k++)
		if (is_ext (arg, wii_text_defs[k].ext) && wii_text_defs[k].is (raw, sz, sz))
			def = wii_text_defs + k;
	if (!def)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char *buf = 0;
	size_t blen = 0;
#ifdef _WIN32
	// mingw has no open_memstream(): decode into a temporary file and read it back.
	FILE *f = tmpfile ();
	if (!f)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	enumError err = def->dec (f, raw, sz, sz);
	fflush (f);
	long flen = fseek (f, 0, SEEK_END) ? -1 : ftell (f);
	if (flen > 0 && !fseek (f, 0, SEEK_SET) && (buf = (char *)MALLOC ((size_t)flen)) != 0)
		blen = fread (buf, 1, (size_t)flen, f);
	fclose (f);
#else
	FILE *f = open_memstream (&buf, &blen);
	if (!f)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	enumError err = def->dec (f, raw, sz, sz);
	fclose (f);
#endif
	FREE (raw);
	if (!err && blen)
	{
		ccp base = strrchr (arg, '/');
		base = base ? base + 1 : arg;
		char fn[PATH_MAX], dest[PATH_MAX];
		snprintf (fn, sizeof (fn), "%s.txt", base);
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WII TEXT:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, fn);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, fn, (const u8 *)buf, (uint)blen);
	}
	else
		err = ERR_NOTHING_TO_DO;
	FREE (buf);
	// A two-colour .pgf atlas also decodes to "<file>.png" (the manifest above keeps the glyph metrics).
	if (err == ERR_OK && def->is == IsBermudaPgf)
	{
		const enumError ierr = decode_image_if_possible (arg);
		if (ierr != ERR_NOTHING_TO_DO && ierr > err)
			err = ierr;
	}
	return err;
}
