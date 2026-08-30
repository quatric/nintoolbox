// External pass-through for container formats added by the Nintendo fork.
// When XX/EXTRACT/XCOMMON encounter a file that is not a native archive or
// raw Nintendo codec stream (see lib-nintendo), it is either decoded in
// process or handed to an external tool (wit/ndstool/ctrtool/sharpii) that
// unpacks it into a staging directory under the extraction base dir.  The
// staged tree is then processed like any other extracted archive.
#ifndef SZS_LIB_PASSTHRU_H
#define SZS_LIB_PASSTHRU_H 1

#include "types.h"

// option controls, bound in tab-wszst.inc and CheckOptions() of wszst.c
extern bool opt_no_passthrough; // --no-passthrough: disable pass-through
extern ccp opt_with_wit; // --with-wit=path|name
extern ccp opt_with_ndstool; // --with-ndstool=path|name
extern ccp opt_with_ctrtool; // --with-ctrtool=path|name
extern ccp opt_with_sharpii; // --with-sharpii=path|name
extern ccp opt_with_hactool; // --with-hactool=path|name
extern ccp opt_with_hacbrewpack; // --with-hacbrewpack=path|name
extern ccp opt_with_bms; // --with-bms=path|--bms=path

// Try to pass an unrecognized SRC through to an external unpacker or to an
// in-process decoder.  STAGED_DIR is filled with the directory (relative to
// the caller's base dir) that received the unpacked payload when the return
// value is <= ERR_WARNING, so the caller can recurse into it.
//
// Return:
//   ERR_NOTHING_TO_DO  SRC is not a known pass-through container
//   ERR_WARNING        container recognized but the external tool is missing
//   ERR_OK             container recognized and unpacked; *STAGED_DIR is valid
//   > ERR_ERROR        container recognized but unpacking failed
enumError PassthruExtract (ccp src, ccp basedir, char *staged_dir, uint staged_dir_size);

// Strong-signature variant of PassthruExtract(): claims only files whose
// HEADER identifies an external container (disc image magics, the DS
// "NINTENDO" tag).  Call this BEFORE the native archive/codec probes so a
// large disc image is dispatched to wit without tripping the file-size
// limited LoadFileAlloc() of the archive extractors; extension-only claims
// remain with PassthruExtract() so native decoders get first refusal.
enumError PassthruExtractStrong (ccp src, ccp basedir, char *staged_dir, uint staged_dir_size);

// Decompress / Compress Wii U sparse disc image (.wux <-> .wud)
bool wux_decompress (ccp src, ccp dst);
bool wux_compress (ccp src, ccp dst);

// Encode WAV_PATH to DEST_PATH (DSP-ADPCM/BRSTM/BFSTM/BCSTM/BNS) via
// mobipeg's real adpcm_thp encoder rather than this project's own port.
// FORMAT is mobipeg's muxer short name ("brstm"/"bfstm"/"bcstm"/"dsp"/"bns");
// LOOP_START < 0 means "not looping". See lib-passthru.c for why this exists
// alongside a native encoder instead of replacing it.
// Returns ERR_NOTHING_TO_DO if mobipeg isn't installed.
enumError PassthruEncodeAudio (ccp wav_path, ccp dest_path, ccp format, s64 loop_start);

// Repack an extracted container directory back into a container file using
// external tools (wit COPY, ndstool -c, sharpii WAD -p, wux_compress).
enumError PassthruPack (ccp src_dir, ccp dest);
bool IsDiscExt (ccp path);

// Helpers for incremental builds & directory tree cleanup
void remove_dir_recursive (ccp dir);
bool is_dir_newer_than (ccp dirpath, time_t target_mtime);

#endif