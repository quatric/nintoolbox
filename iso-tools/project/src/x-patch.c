// x-patch.c - "wit PATCH" command: apply known, documented anti-piracy
// neutralization patches to a Wii disc image or extracted FST.
//
// This is a thin database + XML-synthesis layer on top of the existing,
// already-tested Riivolution memory-patch pipeline (x-riivolution.c/.h):
// for each matched patch we build a tiny in-memory Riivolution XML document
// containing plain <memory offset="..." value="..."/> pokes and hand it to
// RiivolutionCommand(), which already knows how to extract/patch/rebuild
// ISO, WBFS, WDF, FST, etc. No second patch-application mechanism is
// implemented here.

#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "x-patch.h"
#include "x-patch-data.h"
#include "x-riivolution.h"
#include "iso-interface.h"

//=============================================================================
// Patch database
//=============================================================================

typedef struct patch_def_t
{
	ccp id6; // exact 6-char game ID this entry applies to, or NULL
	ccp key; // short selector, e.g. "metafortress"
	ccp title; // human readable title
	ccp description; // one-line description of what it neutralizes
	ccp citation; // where the technical basis came from
	const patch_pair_t *pairs;
	uint n_pairs;
	bool available; // false => documented as TODO, not implemented
	ccp todo_reason; // explanation when available == false
} patch_def_t;

static const patch_def_t patch_db[] = {
	//--------------------------------------------------------------------
	// 3. Kirby's Return to Dream Land / Kirby Wii "MetaFortress" trap level
	//--------------------------------------------------------------------
	// The retail disc contains a hidden decoy stage ("Metafortress") that
	// unofficial copies/loaders are diverted into instead of the real final
	// level, softlocking the game. The fix is a large set of direct 32-bit
	// code pokes (NOPs / branch patches) inside main.dol that neutralize the
	// detection/redirect logic. Values below are taken verbatim from the
	// Dolphin Emulator project's per-game "Bypass Metafortress [crediar]"
	// patch, which is widely mirrored and has been in public use for years.
	{ "SUKE01", "metafortress", "Kirby's Return to Dream Land (USA) - Bypass Metafortress",
		"Neutralizes the MetaFortress anti-piracy decoy-level trap.",
		"Dolphin Emulator GameSettings/SUKE01.ini, patch \"Bypass Metafortress [crediar]\" -- "
		"https://github.com/dolphin-emu/dolphin/blob/master/Data/Sys/GameSettings/SUKE01.ini",
		metafortress_suke01, sizeof (metafortress_suke01) / sizeof (metafortress_suke01[0]), true,
		NULL },
	{ "SUKP01", "metafortress", "Kirby's Return to Dream Land (Europe) - Bypass Metafortress",
		"Neutralizes the MetaFortress anti-piracy decoy-level trap.",
		"Dolphin Emulator GameSettings/SUKP01.ini, patch \"Bypass Metafortress [crediar]\" -- "
		"https://github.com/dolphin-emu/dolphin/blob/master/Data/Sys/GameSettings/SUKP01.ini",
		metafortress_sukp01, sizeof (metafortress_sukp01) / sizeof (metafortress_sukp01[0]), true,
		NULL },
	{ "SUKJ01", "metafortress", "Kirby's Adventure Wii (Japan) - Bypass Metafortress",
		"Neutralizes the MetaFortress anti-piracy decoy-level trap.",
		"Dolphin Emulator GameSettings/SUKJ01.ini, patch \"Bypass Metafortress [crediar]\" -- "
		"https://github.com/dolphin-emu/dolphin/blob/master/Data/Sys/GameSettings/SUKJ01.ini",
		metafortress_sukj01, sizeof (metafortress_sukj01) / sizeof (metafortress_sukj01[0]), true,
		NULL },

	//--------------------------------------------------------------------
	// 4. New Super Mario Bros. Wii anti-piracy message screen
	//--------------------------------------------------------------------
	// NSMBW compares disc BCA (Burst Cutting Area) data at startup; on
	// mismatch it silently logs a marked error to dvderror.dat and halts the
	// game 3-10 minutes later with a generic "An error has occurred"
	// message (TCRF: "New Super Mario Bros. Wii"). Unlike the Kirby
	// MetaFortress trap this is not a simple fixed set of code pokes shared
	// as a public Gecko code / Dolphin GameSettings patch: BCA data is
	// physically unique per pressed disc and the check interacts with the
	// disc-drive/IOS read path rather than a single guarded branch in
	// main.dol, and no publicly documented address/opcode set for
	// neutralizing it could be located (Dolphin's own SMNE01.ini ships no
	// such patch, since BCA is not emulated the same way). Left unimplemented
	// rather than guessing an offset.
	{ NULL, "nsmbw-antipiracy", "New Super Mario Bros. Wii - anti-piracy message screen",
		"TODO: not implemented, see todo_reason.",
		"TCRF \"New Super Mario Bros. Wii\" (BCA check writeup) -- "
		"https://tcrf.net/New_Super_Mario_Bros._Wii ; "
		"Dolphin GameSettings/SMNE01.ini (no anti-piracy patch present)",
		NULL, 0, false,
		"No sourced, verified address/opcode set for the BCA-check neutralization could be found "
		"(it is not distributed as a standard Gecko code / Dolphin GameSettings patch, unlike the "
		"Kirby MetaFortress trap). Needs reverse engineering per game revision before it can be "
		"added here." },

	//--------------------------------------------------------------------
	// 1 & 2. "Error 001" / "Error 002" disc anti-piracy read-pattern errors
	//--------------------------------------------------------------------
	// These are not per-game code checks: Error 001 ("unauthorized device
	// detected") and Error 002 (title running under an unexpected/incorrect
	// IOS, or a disc-drive read-pattern mismatch) are enforced by the Wii's
	// IOS/DI (Drive Interface) and ES trust chain, not by a guarded branch
	// inside an individual game's main.dol. Community fixes for these are
	// therefore IOS-level (custom IOS / Trucha Bug fixes, or loading the
	// correct IOS for a title) rather than per-game disc-image byte patches,
	// so they do not fit the "patch a disc image at a known offset" model
	// used by wit PATCH / Riivolution memory patches. Documenting instead
	// of guessing at a fake per-game offset.
	{ NULL, "error-001", "Wii \"Error #001\" (unauthorized device detected)",
		"TODO: not implemented, see todo_reason.",
		"ConsoleMods Wiki: Wii:Error_Codes -- https://consolemods.org/wiki/Wii:Error_Codes", NULL,
		0, false,
		"Error 001 is raised by the IOS/DI trust-chain check, not by per-game code in main.dol, so "
		"there is no single disc-image byte patch that fixes it for a given game. The documented "
		"community fix is a correctly-signed cIOS / Trucha Bug fix at the system level, which is "
		"out of scope for a disc-image patch database." },
	{ NULL, "error-002", "Wii \"Error #002\" (wrong IOS / disc read anti-piracy check)",
		"TODO: not implemented, see todo_reason.",
		"GBAtemp: \"Error #002 - Genuine Game + Wii System Menu, Why?\" -- "
		"https://gbatemp.net/threads/error-002-genuine-game-wii-system-menu-why.582740/",
		NULL, 0, false,
		"Error 002 is triggered when a title is not running under its expected IOS (an IOS-version "
		"compare enforced per game, but resolved system-side by loading the correct/patched IOS), "
		"not by a single fixed offset shared across games. Without a verified per-game address for "
		"a specific title this cannot be added without guessing an offset, which was avoided." },
};

#define N_PATCH_DB (sizeof (patch_db) / sizeof (patch_db[0]))

PatchOptions_t patch_options;

//=============================================================================

void InitPatchOptions (PatchOptions_t *opt)
{
	if (!opt)
		return;
	memset (opt, 0, sizeof (*opt));
	InitializeStringField (&opt->select);
	opt->output_oft = OFT_UNKNOWN;
}

//-----------------------------------------------------------------------------

static void print_patch_list (void)
{
	printf ("\nKnown anti-piracy patches (wit PATCH database):\n\n");
	for (uint i = 0; i < N_PATCH_DB; i++)
	{
		const patch_def_t *p = &patch_db[i];
		printf ("  %-20s %-8s %s\n", p->key, p->id6 ? p->id6 : "(any)", p->title);
		printf ("      %s\n", p->description);
		if (!p->available)
			printf ("      STATUS: NOT IMPLEMENTED - %s\n", p->todo_reason);
		else
			printf ("      %u code poke(s). Source: %s\n", p->n_pairs, p->citation);
		printf ("\n");
	}
}

//-----------------------------------------------------------------------------

static enumError detect_id6 (ccp source, char id6[7])
{
	id6[0] = 0;
	if (IsDirectory (source, false))
	{
		static const char *rel[] = { "disc/header.bin", "DATA/sys/boot.bin", "sys/boot.bin", 0 };
		for (int i = 0; rel[i]; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", source, rel[i]);
			FILE *f = fopen (path, "rb");
			if (f)
			{
				size_t n = fread (id6, 1, 6, f);
				fclose (f);
				if (n == 6)
				{
					id6[6] = 0;
					return ERR_OK;
				}
			}
		}
		return ERROR0 (ERR_CANT_OPEN, "Cannot determine game ID of FST directory: %s\n", source);
	}

	SuperFile_t sf;
	InitializeSF (&sf);
	SetupIOD (&sf, OFT_UNKNOWN, OFT_UNKNOWN);
	enumError err = OpenSF (&sf, source, true, false);
	if (err)
	{
		ResetSF (&sf, 0);
		return ERROR0 (err, "Cannot open source disc image: %s\n", source);
	}
	wd_disc_t *disc = OpenDiscSF (&sf, true, true);
	if (!disc)
	{
		ResetSF (&sf, 0);
		return ERROR0 (ERR_CANT_OPEN, "Cannot read disc header from image: %s\n", source);
	}
	memcpy (id6, disc->dhead.id6.id6, 6);
	id6[6] = 0;
	ResetSF (&sf, 0);
	return ERR_OK;
}

//-----------------------------------------------------------------------------

static bool key_selected (const PatchOptions_t *opt, ccp key)
{
	if (!opt->select.used)
		return true;
	for (uint i = 0; i < opt->select.used; i++)
		if (!strcasecmp (opt->select.field[i], key))
			return true;
	return false;
}

//-----------------------------------------------------------------------------

enumError PatchCommand (PatchOptions_t *opt)
{
	if (!opt)
		return ERROR0 (ERR_MISSING_PARAM, "Missing PATCH options.\n");

	if (opt->list_only || !opt->source_image)
	{
		print_patch_list ();
		return ERR_OK;
	}

	char id6[7];
	enumError err = detect_id6 (opt->source_image, id6);
	if (err)
		return err;

	if (opt->verbose >= 0)
		printf ("PATCH: source game ID = %s\n", id6);

	// Collect matching, available entries
	const patch_def_t *matched[N_PATCH_DB];
	uint n_matched = 0;
	bool any_key_matched_unavailable = false;

	for (uint i = 0; i < N_PATCH_DB; i++)
	{
		const patch_def_t *p = &patch_db[i];
		const bool id_ok = !p->id6 || !strcasecmp (p->id6, id6);
		if (!id_ok)
			continue;
		if (!key_selected (opt, p->key))
			continue;
		if (!p->available)
		{
			any_key_matched_unavailable = true;
			printf ("  ! Skipping \"%s\" (%s): not implemented - %s\n", p->key, p->title,
				p->todo_reason);
			continue;
		}
		matched[n_matched++] = p;
	}

	if (!n_matched)
	{
		if (any_key_matched_unavailable)
			return ERROR0 (ERR_WARNING,
				"No implemented anti-piracy patches could be applied to %s "
				"(matching entries exist but are marked TODO, see above).\n",
				id6);
		return ERROR0 (ERR_WARNING,
			"No known anti-piracy patches for game ID %s.\n"
			"Use 'wit PATCH --patch-list' to see the known patch database.\n",
			id6);
	}

	if (opt->verbose >= 0)
	{
		printf ("  Applying %u patch(es):\n", n_matched);
		for (uint i = 0; i < n_matched; i++)
			printf ("    - %s (%u pokes)\n", matched[i]->title, matched[i]->n_pairs);
	}

	// Synthesize a minimal Riivolution XML document with direct memory pokes
	// and hand off to the existing, tested Riivolution build pipeline.
	char tmp_xml[PATH_MAX];
	{
		const char *tmp_root = getenv ("TMPDIR");
		if (!tmp_root)
			tmp_root = getenv ("TEMP");
		if (!tmp_root)
			tmp_root = "/tmp";
		snprintf (tmp_xml, sizeof (tmp_xml), "%s/wit-patch-XXXXXX.xml", tmp_root);
		int fd = mkstemps (tmp_xml, 4);
		if (fd < 0)
			return ERROR0 (ERR_CANT_CREATE, "Failed to create temporary XML file.\n");

		FILE *f = fdopen (fd, "w");
		if (!f)
		{
			close (fd);
			return ERROR0 (ERR_CANT_CREATE, "Failed to open temporary XML file.\n");
		}

		fprintf (f, "<wiidisc version=\"1\">\n");
		fprintf (f, " <id game=\"%s\"/>\n", id6);
		for (uint i = 0; i < n_matched; i++)
		{
			const patch_def_t *p = matched[i];
			fprintf (f, " <patch id=\"p%u\">\n", i);
			for (uint j = 0; j < p->n_pairs; j++)
				fprintf (f, "  <memory offset=\"0x%08X\" value=\"%08X\"/>\n", p->pairs[j].addr,
					p->pairs[j].value);
			fprintf (f, " </patch>\n");
		}
		fprintf (f, "</wiidisc>\n");
		fclose (f);
	}

	RiivolutionOptions_t riiv_opt;
	InitRiivolutionOptions (&riiv_opt);
	riiv_opt.source_image = opt->source_image;
	riiv_opt.xml_file = tmp_xml;
	riiv_opt.dest_path = opt->dest_path;
	riiv_opt.overwrite = opt->overwrite;
	riiv_opt.testmode = opt->testmode;
	riiv_opt.verbose = opt->verbose;
	riiv_opt.custom_id = opt->custom_id;
	riiv_opt.custom_name = opt->custom_name;
	riiv_opt.output_oft = opt->output_oft;

	err = RiivolutionCommand (&riiv_opt);

	unlink (tmp_xml);
	return err;
}
