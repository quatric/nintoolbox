// Process a single file with the full XX pipeline: external pass-through
// (strong, then weak) first, then the native Nintendo codecs, and finally
// the regular SZS/U8/BRRES extractor.  When a pass-through tool unpacks a
// container into a staging directory, the staged tree is walked recursively
// so every leaf is peeled in turn.  Returns the strongest error seen, or
// ERR_NOTHING_TO_DO when nothing claimed the file.
static enumError extract_one_file_inner (ccp arg, ccp basedir, uint depth);

// Monster Games .sfx: a 0x80 header over a plain Nintendo DSP-ADPCM stream.
// The payload is wrapped in a GENH so mobipeg (or a stock ffmpeg) can decode
// it with its own adpcm_thp, which is deliberately not reimplemented here.
static enumError extract_sfx_file (ccp arg, ccp basedir, uint depth)
{
	// This extractor emits a decoded WAV preview with no reciprocal packer.
	// Keep it out of normal staging trees; XEXPORT is the explicit preview mode.
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	sfx_info_t info;
	if (raw_size > UINT_MAX || !GetSFXInfo (raw, (uint)raw_size, &info))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *genh = 0;
	uint genh_size = 0;
	enumError err = BuildGENHFromSFX (raw, (uint)raw_size, &genh, &genh_size);
	FREE (raw);
	if (err)
		return err;

	char dest[PATH_MAX];
	if (opt_dest && *opt_dest)
		snprintf (dest, sizeof (dest), "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, sizeof (dest), "%s", basedir);
	else
		snprintf (dest, sizeof (dest), "%s.d", arg);
	CreatePath (dest, true);

	ccp stem = strrchr (arg, '/');
	stem = stem ? stem + 1 : arg;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SFX:%s (%u Hz, %u bytes) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, info.sample_rate, info.data_size, dest);
	if (testmode)
	{
		FREE (genh);
		return ERR_OK;
	}

	char genh_path[PATH_MAX], wav_path[PATH_MAX];
	snprintf (genh_path, sizeof (genh_path), "%s/%s.genh", dest, stem);
	snprintf (wav_path, sizeof (wav_path), "%s/%s.wav", dest, stem);

	err = SaveFile (genh_path, 0, 0, genh, genh_size, 0);
	FREE (genh);
	if (err)
		return err;

	err = PassthruDecodeAudio (genh_path, wav_path);
	unlink (genh_path); // scratch: it only existed to feed the decoder

	if (err == ERR_NOTHING_TO_DO)
		return ERROR0 (ERR_WARNING,
			"%s is DSP-ADPCM audio, but no mobipeg or ffmpeg was found to decode it"
			" (see --with-mobipeg)",
			arg);
	return err;
}

static enumError extract_one_file (ccp arg, ccp basedir, uint depth)
{
	enumError err = extract_one_file_inner (arg, basedir, depth);
	if (depth > 0 && err > ERR_WARNING)
		return ERR_WARNING;
	return err;
}

// Heavy Iron .ho assets are written as "name.<8 hex digits of the type hash>";
// their payloads are opaque engine data.
static bool is_ho_asset_name (ccp arg)
{
	ccp dot = strrchr (arg, '.');
	if (!dot || dot == arg || strlen (dot) != 9)
		return false;
	for (uint i = 1; i < 9; i++)
		if (!isxdigit ((u8)dot[i]))
			return false;
	return true;
}

static enumError extract_one_file_inner (ccp arg, ccp basedir, uint depth)
{
#include "dispatch/ho_texture.inc"
	// Asobo BigFile members ("hash.Class_Z" plus ".lnk" link headers) are
	// opaque engine data apart from bitmaps.
	if ((is_ext (arg, ".lnk") || (strlen (arg) > 2 && !strcasecmp (arg + strlen (arg) - 2, "_Z")))
		&& !is_ext (arg, ".bitmap_z") && !is_ext (arg, ".sound_z") && !is_ext (arg, ".mesh_z"))
		return ERR_OK;
	// Grip .res sections other than textures are opaque object data; keep the
	// magic-less sniffers from unpacking them into junk ".d" folders.
	{
		ccp slash = strrchr (arg, '/');
		if (slash && slash - arg > 6 && !memcmp (slash - 6, ".res.d", 6) && !is_ext (arg, ".surf"))
			return ERR_OK;
	}
#include "dispatch/muramasa_otb.inc"
#include "dispatch/muramasa_nsi.inc"
#include "dispatch/muramasa_fcmp.inc"
#include "dispatch/redsteel2_abe.inc"
#include "dispatch/redsteel2_rel.inc"
#include "dispatch/zackwiki_tm2.inc"
#include "dispatch/zackwiki_ppg.inc"
#include "dispatch/zackwiki_tsb.inc"
#include "dispatch/zackwiki_whd.inc"
#include "dispatch/zackwiki_mds.inc"
#include "dispatch/zackwiki_ssd.inc"
#include "dispatch/hgpk_pak.inc"
	char pbase_buf[PATH_MAX] = "";
	ccp pbase = opt_dest && *opt_dest ? opt_dest : basedir;
	if (pbase)
	{
		snprintf (pbase_buf, sizeof (pbase_buf), "%s", pbase);
		const uint n = strlen (pbase_buf);
		if (n && pbase_buf[n - 1] != '/' && n + 1 < sizeof (pbase_buf))
		{
			pbase_buf[n] = '/';
			pbase_buf[n + 1] = 0;
		}
		pbase = pbase_buf;
	}

	// This manifest is emitted by every native archive extraction. It is an
	// input to CREATE, not another container to peel during recursive XX.
	ccp arg_name = strrchr (arg, '/');
	arg_name = arg_name ? arg_name + 1 : arg;
	if (!strcmp (arg_name, "wszst-setup.txt"))
		return ERR_OK;
	if (depth && (!strncmp (arg_name, "image.", 6) || !strncmp (arg_name, "mipmap-", 7)))
		return ERR_OK;

#include "dispatch/tex.inc"
#include "dispatch/bmg.inc"
#include "dispatch/ctxb.inc"
#include "dispatch/bntx.inc"
	err = decode_wimg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
#include "dispatch/pfctex.inc"
#include "dispatch/bfttf.inc"
#include "dispatch/bnvib.inc"
#include "dispatch/nacp.inc"
#include "dispatch/pfs0.inc"
#include "dispatch/nro.inc"
#include "dispatch/nso.inc"
#include "dispatch/cnmt.inc"
#include "dispatch/npdm.inc"
#include "dispatch/xci.inc"
#include "dispatch/art.inc"
#include "dispatch/zip.inc"
#include "dispatch/tree_complete.inc"
	err = ExtractSmashARC (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractIQIPack (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

#include "dispatch/sarc.inc"
#include "dispatch/storybook_one.inc"
#include "dispatch/bigf.inc"
	if (is_littlest_pet_shop_rpk (arg))
		return ERR_OK;

#include "dispatch/abe.inc"
	// Stream conversion must precede generic archive sniffers.
	if (is_ext (arg, ".sad") || is_ext (arg, ".sadl"))
	{
		err = decompress_nintendo_file (arg);
		if (err != ERR_NOTHING_TO_DO)
			return err;
	}
#include "dispatch/iear.inc"
#include "dispatch/pck2.inc"
#include "dispatch/pikarc.inc"
#include "dispatch/alar.inc"
#include "dispatch/arc0.inc"
#include "dispatch/gfa.inc"
#include "dispatch/rz.inc"
#include "dispatch/res.inc"
#include "dispatch/rst.inc"
#include "dispatch/arika.inc"
#include "dispatch/thp.inc"
#include "dispatch/namco_sszl.inc"
#include "dispatch/namco_vcra.inc"
#include "dispatch/arcv.inc"
	// Bully: Scholarship Edition's collision.pak shares the ".pak" extension
	// with several weaker, extension-driven probes below (extract_pac_file
	// and friends), which would otherwise claim it first and silently
	// produce nothing. Its own gate is strictly content-based (declared
	// size, a fully chained offset/size table, and every member starting
	// with a real GTA-family COL FourCC), so it is safe to try ahead of
	// those weaker heuristics.
	err = ExtractCollPakArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

#include "dispatch/pac.inc"
#include "dispatch/gpkg.inc"
#include "dispatch/cod_pak0.inc"
#include "dispatch/gpak.inc"
#include "dispatch/bns.inc"
	// Retro Studios RPAK has a decoder but no lossless encoder yet.  Leaving
	// it opaque is essential: expanding it into a .d directory makes an
	// unchanged retail game appear editable even though CREATE cannot put its
	// entry table, compression flags, and alignment back byte-for-byte.
	// Keep the original .pak in the staged tree until the paired encoder exists.
	if (is_rpak_file (arg))
		return ERR_OK;
#include "dispatch/mpr_pack.inc"
#include "dispatch/jarc.inc"
#include "dispatch/lspk.inc"
#include "dispatch/fsys.inc"
#include "dispatch/warc.inc"
#include "dispatch/sfzdat.inc"
#include "dispatch/cpk.inc"
#include "dispatch/ipk.inc"
#include "dispatch/dig.inc"
#include "dispatch/agi.inc"
#include "dispatch/blpkg.inc"
#include "dispatch/goliath.inc"
#include "dispatch/rso.inc"
#include "dispatch/wadh.inc"
#include "dispatch/catcar.inc"
#include "dispatch/termpod.inc"
#include "dispatch/ho.inc"
#include "dispatch/asobo.inc"
#include "dispatch/vblank.inc"
#include "dispatch/fsb.inc"
#include "dispatch/kraw.inc"
#include "dispatch/dcx.inc"
#include "dispatch/dct.inc"
#include "dispatch/dsp.inc"
#include "dispatch/pk2.inc"
#include "dispatch/xtd.inc"
#include "dispatch/xmd.inc"
#include "dispatch/was.inc"
#include "dispatch/dksoundbank.inc"
#include "dispatch/dkzf.inc"
#include "dispatch/afs.inc"
#include "dispatch/rtdp.inc"
#include "dispatch/cxd.inc"
#include "dispatch/fpk.inc"
	err = ExtractCtrPack (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCtrResource (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

#include "dispatch/thor.inc"
#include "dispatch/ttarch.inc"
#include "dispatch/nif.inc"
#include "dispatch/gripres.inc"
#include "dispatch/gripsurf.inc"
#include "dispatch/rev.inc"
#include "dispatch/bltex.inc"
#include "dispatch/asobo_mesh.inc"
#include "dispatch/asobo_sound.inc"
#include "dispatch/asobo_bitmap.inc"
#include "dispatch/avalanche_tex.inc"
#include "dispatch/bj_tx.inc"
#include "dispatch/bj_mtm.inc"
#include "dispatch/bj_audio.inc"
#include "dispatch/torus_hnk.inc"
#include "dispatch/pub.inc"
#include "dispatch/rez.inc"
#include "dispatch/bombshell.inc"
#include "dispatch/bttrb.inc"
#include "dispatch/toshi_ttl.inc"
#include "dispatch/toshi_tkl.inc"
#include "dispatch/fbc.inc"
#include "dispatch/gct.inc"
#include "dispatch/gcg.inc"
#include "dispatch/blact.inc"
#include "dispatch/wta.inc"
#include "dispatch/bg4.inc"
#include "dispatch/cram.inc"
#include "dispatch/sumostz.inc"
#include "dispatch/sa01.inc"
#include "dispatch/hwlegends.inc"
#include "dispatch/rflres.inc"
#include "dispatch/bea.inc"
	// Check the complete MDR chunk headers and wrapped G1T signature before
	// the less distinctive MPBIN and ATB structures.
	err = ExtractMDRArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractG1TArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

#include "dispatch/mpbin.inc"
#include "dispatch/zmb.inc"
#include "dispatch/atb.inc"
#include "dispatch/ptd.inc"
#include "dispatch/lzbin.inc"
#include "dispatch/hbdf.inc"
#include "dispatch/effn.inc"
#include "dispatch/pctl.inc"
#include "dispatch/mii.inc"
#include "dispatch/msh.inc"
#include "dispatch/csb.inc"
#include "dispatch/hsf.inc"
#include "dispatch/g1m.inc"
#include "dispatch/glg.inc"
#include "dispatch/can.inc"
#include "dispatch/mod.inc"
#include "dispatch/nud.inc"
#include "dispatch/numshb.inc"
#include "dispatch/mpr_cmdl.inc"
#include "dispatch/wmb.inc"
#include "dispatch/ttmodel.inc"
#include "dispatch/nut.inc"
#include "dispatch/dtls.inc"
	err = ExtractXPCKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractZTABArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPVOLArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSTPKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractF9ResArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractZLARCArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGARArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMKGPDXPacArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTMPKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractNXARCArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractAPAKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPACArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPKZArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractVIBSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPGDATArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractWTAArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGFLXPackArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGFModelPackArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGFPackageArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGFPAKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractBARSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractFSBArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCSPakArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSBTArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGDSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCHXArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMapBadArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTXDArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractIdeImgArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractAGRArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractBamArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCSFontArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCSCsiArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractNLGDictArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSANIMArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTXTGArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractROMFSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractXTXArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTVOLArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMTXTArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractNUS3AudioArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSIR0Archive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPERSFile (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractZDATArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractRZPKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

#include "dispatch/sfx.inc"
	err = ExtractVFFArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractCamelotTexBank (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPTLGArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMIOArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

#include "dispatch/nccarc.inc"
#include "dispatch/ccf.inc"
#include "dispatch/sequence.inc"
#include "dispatch/romc.inc"
#include "dispatch/darc.inc"
#include "dispatch/sar.inc"
#include "dispatch/narc.inc"
#include "dispatch/at7.inc"
#include "dispatch/ctpk.inc"
#include "dispatch/gsh_shaders.inc"
#include "dispatch/imet.inc"
#include "dispatch/nds_banner.inc"
#include "dispatch/wibn.inc"
#include "dispatch/smdh.inc"
#include "dispatch/nitro_sprite.inc"
#include "dispatch/cfnt.inc"
#include "dispatch/brfnt.inc"
#include "dispatch/bflyt.inc"
#include "dispatch/byml.inc"
	err = export_kcl_glb_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

#include "dispatch/msbt.inc"
#include "dispatch/ue4_pak.inc"
#include "dispatch/prc.inc"
#include "dispatch/rstb.inc"
#include "dispatch/aamp.inc"
#include "dispatch/sharc.inc"
#include "dispatch/bnsh.inc"
#include "dispatch/nushdb.inc"
#include "dispatch/numatb.inc"
#include "dispatch/nusktb.inc"
#include "dispatch/nufxlb.inc"
#include "dispatch/numdlb.inc"
#include "dispatch/nuhlpb.inc"
#include "dispatch/nulstb.inc"
#include "dispatch/nurpdb.inc"
#include "dispatch/nuanmb.inc"
#include "dispatch/gfmotion.inc"
#include "dispatch/gf1motion.inc"
#include "dispatch/mtmrl.inc"
#include "dispatch/chnk.inc"
#include "dispatch/magma_fat.inc"
#include "dispatch/voice_song.inc"
#include "dispatch/voice_amc.inc"
#include "dispatch/voice_ams.inc"
#include "dispatch/voice_palcat.inc"
#include "dispatch/voice_palseq.inc"
#include "dispatch/dogisland_wds.inc"
#include "dispatch/dogisland_wdb.inc"
#include "dispatch/dogisland_yobj.inc"
#include "dispatch/dogisland_pms.inc"
#include "dispatch/dogisland_mtt.inc"
#include "dispatch/dogisland_cprm.inc"
#include "dispatch/dogisland_script.inc"
#include "dispatch/dogisland_mpq.inc"
#include "dispatch/mtmfx.inc"
#include "dispatch/mtmod.inc"
#include "dispatch/mbn.inc"
#include "dispatch/xmb.inc"
#include "dispatch/adjb.inc"
#include "dispatch/bfsha.inc"
#include "dispatch/effn2.inc"
#include "dispatch/pctl2.inc"
#include "dispatch/wtb.inc"
#include "dispatch/trpak.inc"
#include "dispatch/cnut.inc"
#include "dispatch/xmsg.inc"
#include "dispatch/nwr_levelinfo.inc"
#include "dispatch/nwr_animtiles.inc"
#include "dispatch/nsmbw_chk.inc"
#include "dispatch/nsmbw_tex.inc"
#include "dispatch/kpbin.inc"
#include "dispatch/chans.inc"
#include "dispatch/metroidsr.inc"
#include "dispatch/image.inc"
#include "dispatch/bfres.inc"
#include "dispatch/bcres.inc"
#include "dispatch/hsd.inc"
#include "dispatch/halbank.inc"
	err = export_model_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

#include "dispatch/bfres_switch.inc"
#include "dispatch/brsar.inc"
	// A native archive's extracted BRSUB members (TEX0/PLT0/CHR0/etc., and
	// MDL0 while the outer two-pass export is deferred) are already the leaf
	// resources we want. Do not cut those detached leaves again when walking
	// an archive's freshly completed output directory.
	{
		// GetByMagicFF deliberately requires at least 0x20 bytes before it
		// classifies BRSUB resources; shorter probes avoid mistaking the small
		// extraction metadata fragments for real TEX0/MDL0/etc. records.
		u8 head[0x40];
		FILE *f = fopen (arg, "rb");
		const size_t got = f ? fread (head, 1, sizeof (head), f) : 0;
		if (f)
			fclose (f);
		const file_format_t leaf_ff = GetByMagicFF (head, (uint)got, 0);
		if ((got >= 0x20 && IsBRSUB (leaf_ff)) || leaf_ff == FF_PLT0 || leaf_ff == FF_PNG
			|| leaf_ff == FF_BREFT_IMG || (GetAttribFF (leaf_ff) & FFT_TEXT))
			return ERR_OK;
	}

	// Raw Nintendo codecs are not U8/SZS archives, but users expect the
	// regular extraction front end to handle them as a single extracted file.
	// This also makes a compressed asset found inside an extracted tree usable
	// without switching to a separate command.
	char decompressed_path[PATH_MAX];
	bool decompressed_is_final = false;
	err = decompress_nintendo_file3 (
		arg, decompressed_path, sizeof (decompressed_path), &decompressed_is_final, false);
#include "dispatch/darc2.inc"
#include "dispatch/tree_complete2.inc"
	// Do not use a shallow magic or filename filter to decide whether a
	// nested member deserves the generic parser. Every format-specific probe
	// above has already had first refusal; this final path lets LoadCreateSZS()
	// run its complete structural validation for every remaining member.
	// A FourCC, ID string, or codec byte is only a candidate marker -- those
	// markers collide in real game data, while valid formats can keep their
	// identifying data away from offset zero. At recursive depth an unclaimed
	// or invalid member is quietly ignored below, so this is both exhaustive
	// and safe.

	szs_file_t szs;
	InitializeSZS (&szs);
	err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, false);
#include "dispatch/embedded_nw4r.inc"
#include "dispatch/sarc2.inc"
	ResetSZS (&szs);
	return depth && err ? ERR_OK : err;
}
