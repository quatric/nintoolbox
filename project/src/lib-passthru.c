// External pass-through for container formats added by the Nintendo fork.
// When XX/EXTRACT/XCOMMON encounter a file that is neither a native archive
// nor a raw Nintendo codec stream (see lib-nintendo), it is handed to one of
// the external unpackers (wit, ndstool, ctrtool, sharpii).  The external tool
// unpacks the container into a staging directory; the caller then recurses
// into that directory like any other extracted archive.
#include "lib-passthru.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>

#include "dclib-basics.h"
#include "dclib-color.h"
#include "dclib-debug.h"
#include "dclib-file.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-bms.h"
#include "lib-aes.h"

// option state, bound in tab-wszst.inc / CheckOptions() of wszst.c
bool opt_no_passthrough = false; // --no-passthrough: disable pass-through
ccp opt_with_wit = 0; // --with-wit=path|name
ccp opt_with_ndstool = 0; // --with-ndstool=path|name
ccp opt_with_ctrtool = 0; // --with-ctrtool=path|name
ccp opt_with_sharpii = 0; // --with-sharpii=path|name
ccp opt_with_hactool = 0; // --with-hactool=path|name
ccp opt_with_hacbrewpack = 0; // --with-hacbrewpack=path|name
ccp opt_with_bms = 0; // --with-bms=path|--bms=path
ccp opt_with_mobipeg = 0; // --with-mobipeg=path|name
ccp opt_with_7z = 0; // --with-7z=path|name

// Curried static result buffer, only valid until the next call.  Reasonable
// here since these helpers are used from single-threaded option parsing.
static char prog_buf[PATH_MAX];

static enumError make_stage_dir (ccp stage, bool tool_missing);

static enumError passthru_claim (
	bool strong_only, ccp src, ccp basedir, char *staged_dir, uint staged_dir_size);

static enumError passthru_archive (ccp src, ccp basedir, ccp stage, char *staged_dir,
	uint staged_dir_size, bool is_ds, bool is_ctr, bool is_wad, bool is_disc, bool is_switch);

static enumError passthru_archive_or_bms (ccp src, ccp basedir, ccp stage, char *staged_dir,
	uint staged_dir_size, bool is_ds, bool is_ctr, bool is_wad, bool is_disc, bool is_switch);

static int run_program_capture (char *const argv[], ccp capture_path);
static void dump_capture (ccp capture_path);
static const char *find_program (ccp name);

static ccp resolve_mobipeg (void);

static ccp resolve_ffprobe_for_mobipeg (ccp mobipeg)
{
	ccp slash = strrchr (mobipeg, '/');
	if (slash)
	{
		snprintf (prog_buf, sizeof (prog_buf), "%.*s/ffprobe", (int)(slash - mobipeg), mobipeg);
		if (!access (prog_buf, X_OK))
			return prog_buf;
	}
	return find_program ("ffprobe");
}

static enumError passthru_media (
	ccp src, ccp basedir, ccp stage, char *staged_dir, uint staged_dir_size, bool is_audio);

// Turn a possibly relative tool name/path into an absolute one by scanning
// PATH.  Returns the resolved name or NULL when not found.
static const char *find_program (ccp name)
{
	if (!name)
		return 0;

	if (strchr (name, '/'))
		return (ccp)strcpy (prog_buf, name);

	const char *dirs = getenv ("PATH");
	if (!dirs)
		return 0;

	while (dirs && *dirs)
	{
		ccp end = strchr (dirs, ':');
		const uint len = end ? (uint)(end - dirs) : (uint)strlen (dirs);
		if (len)
		{
			snprintf (prog_buf, sizeof (prog_buf), "%.*s/%s", (int)len, dirs, name);
			if (!access (prog_buf, X_OK))
				return prog_buf;
		}
		dirs = end ? end + 1 : 0;
	}
	return 0;
}

// Look up the tool the user requested.  WITH_VAL is the --with-<tool> value;
// DEFAULT is the bare name used for a plain PATH search.
static ccp resolve_tool (ccp with_val, ccp deflt)
{
	if (with_val && *with_val)
		return find_program (with_val);
	return find_program (deflt);
}

static ccp resolve_mobipeg (void)
{
	if (opt_with_mobipeg && *opt_with_mobipeg)
		return find_program (opt_with_mobipeg);

	ccp found = find_program ("mobipeg");
	if (found)
		return found;

	const char *home = getenv ("HOME");
	if (home)
	{
		snprintf (prog_buf, sizeof (prog_buf), "%s/bin/mobipeg", home);
		if (!access (prog_buf, X_OK))
			return prog_buf;

		snprintf (prog_buf, sizeof (prog_buf), "%s/mobipeg/ffmpeg", home);
		if (!access (prog_buf, X_OK))
			return prog_buf;

		snprintf (prog_buf, sizeof (prog_buf), "%s/mobipeg-src/ffmpeg", home);
		if (!access (prog_buf, X_OK))
			return prog_buf;
	}
	return 0;
}

static ccp resolve_7z (void)
{
	if (opt_with_7z && *opt_with_7z)
		return find_program (opt_with_7z);
	ccp found = find_program ("7z");
	if (found)
		return found;
	found = find_program ("7zz");
	if (found)
		return found;
	found = find_program ("7za");
	if (found)
		return found;
	found = find_program ("unar");
	if (found)
		return found;
	return 0;
}

// Spawn a program with ARGV (NULL-terminated).  ARGV[0] is used as path.
// STDOUT/STDERR are inherited so the user sees the tool's own messages.
// Returns the exit code or 127 on exec failure (like a shell).
static int run_program (char *const argv[])
{
	const pid_t pid = fork ();
	if (pid < 0)
		return -errno;
	if (pid == 0)
	{
		execv (argv[0], argv);
		_Exit (127); // execv failed
	}

	int status = 0;
	while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
		;
	if (WIFEXITED (status))
		return WEXITSTATUS (status);
	return -1;
}

static enumError passthru_media (
	ccp src, ccp basedir, ccp stage, char *staged_dir, uint staged_dir_size, bool is_audio)
{
	ccp tool = resolve_mobipeg ();
	if (!tool || !*tool)
		return ERR_NOTHING_TO_DO;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT media passthrough: %s -> %s (%s)\n", testmode ? "WOULD " : "",
			verbose > 0 ? "\n" : "", src, stage, tool);

	if (testmode)
	{
		snprintf (staged_dir, staged_dir_size, "%s", stage);
		return ERR_OK;
	}

	char base_leaf[PATH_MAX];
	ccp slash = strrchr (src, '/');
	ccp fn = slash ? slash + 1 : src;
	snprintf (base_leaf, sizeof (base_leaf), "%s", fn);
	char *dot = strrchr (base_leaf, '.');
	if (dot)
		*dot = 0;

	char out_file[PATH_MAX];
	snprintf (out_file, sizeof (out_file), "%s/%s.%s", stage, base_leaf, is_audio ? "wav" : "mp4");

	if (CreatePath (stage, true))
		return ERROR0 (ERR_CANT_CREATE_DIR, "Cannot create dest dir: %s", stage);

	char *argv[] = { (char *)tool, "-i", (char *)src, "-y", out_file, 0 };

	const int rc = run_program (argv);
	if (rc != 0)
		return ERROR0 (ERR_SUBJOB_FAILED, "pass-through mobipeg failed for %s (exit %d)", src, rc);

	// Extraction itself can take minutes. Give the preview the source's time so
	// a later CREATE can distinguish that generated file from a user edit.
	struct stat src_stat;
	if (!stat (src, &src_stat))
	{
		struct timespec times[2] = { src_stat.st_atim, src_stat.st_mtim };
		utimensat (AT_FDCWD, out_file, times, 0);
	}

	snprintf (staged_dir, staged_dir_size, "%s", stage);
	return ERR_OK;
}

static enumError passthru_7z (
	ccp src, ccp basedir, ccp stage, char *staged_dir, uint staged_dir_size)
{
	ccp tool = resolve_7z ();
	if (!tool || !*tool)
	{
		*staged_dir = 0;
		return make_stage_dir (stage, true);
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT 7zip passthrough: %s -> %s (%s)\n", testmode ? "WOULD " : "",
			verbose > 0 ? "\n" : "", src, stage, tool);

	if (testmode)
	{
		snprintf (staged_dir, staged_dir_size, "%s", stage);
		return ERR_OK;
	}

	if (CreatePath (stage, false))
		return ERROR0 (ERR_CANT_CREATE_DIR, "Cannot create dest dir: %s", stage);

	char out_arg[PATH_MAX + 8];
	char *argv[10];
	int argc = 0;
	argv[argc++] = (char *)tool;
	if (strstr (tool, "unar"))
	{
		argv[argc++] = "-o";
		argv[argc++] = (char *)stage;
		argv[argc++] = "-f";
		argv[argc++] = (char *)src;
		argv[argc] = 0;
	}
	else
	{
		argv[argc++] = "x";
		argv[argc++] = "-y";
		snprintf (out_arg, sizeof (out_arg), "-o%s", stage);
		argv[argc++] = out_arg;
		argv[argc++] = (char *)src;
		argv[argc] = 0;
	}

	const int rc = run_program (argv);
	if (rc != 0)
		return ERROR0 (ERR_SUBJOB_FAILED, "pass-through 7z failed for %s (exit %d)", src, rc);

	snprintf (staged_dir, staged_dir_size, "%s", stage);
	return ERR_OK;
}

// Encode WAV_PATH to DEST_PATH via mobipeg's real adpcm_thp encoder
// (libavformat/{dsp,brstm,bns}enc.c in the sibling 'mobipeg' repo) instead
// of this project's own EncodeBRSTM()/DspAdpcmEncodeBlock() port -- mobipeg
// carries Nintendo's actual coefficient-search algorithm (thp_*_merge, see
// its adpcmenc.c), which the port only approximates. FORMAT is mobipeg's
// muxer short name ("brstm", "bfstm", "bcstm", "dsp", or "bns"); passed
// explicitly via -f rather than relying on DEST_PATH's extension.
// LOOP_START < 0 means "not looping".
//
// Returns ERR_NOTHING_TO_DO -- the caller should fall back to the native
// encoder, not treat this as a hard failure -- both when mobipeg isn't on
// PATH at all, and when a *found* mobipeg build predates these muxers/the
// adpcm_thp encoder (confirmed against a real prebuilt binary: an older
// mobipeg exits with "Requested output format '<x>' is not known" or
// "Unknown encoder 'adpcm_thp'" rather than not existing). Any other
// failure is a real encode problem and is reported as such.
enumError PassthruEncodeAudio (ccp wav_path, ccp dest_path, ccp format, s64 loop_start)
{
	ccp tool = resolve_mobipeg ();
	if (!tool || !*tool)
		return ERR_NOTHING_TO_DO;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sENCODE audio passthrough: %s -> %s (%s, %s)\n",
			testmode ? "WOULD " : "", verbose > 0 ? "\n" : "", wav_path, dest_path, format, tool);

	if (testmode)
		return ERR_OK;

	char loop_start_buf[32];
	char *argv[16];
	uint n = 0;
	argv[n++] = (char *)tool;
	argv[n++] = "-i";
	argv[n++] = (char *)wav_path;
	argv[n++] = "-c:a";
	argv[n++] = "adpcm_thp";
	if (loop_start >= 0)
	{
		snprintf (loop_start_buf, sizeof (loop_start_buf), "%lld", (long long)loop_start);
		argv[n++] = "-loop";
		argv[n++] = "1";
		argv[n++] = "-loop_start";
		argv[n++] = loop_start_buf;
	}
	argv[n++] = "-f";
	argv[n++] = (char *)format;
	argv[n++] = "-y";
	argv[n++] = (char *)dest_path;
	argv[n++] = 0;
	assert (n <= sizeof (argv) / sizeof (*argv));

	char capture_path[PATH_MAX];
	snprintf (
		capture_path, sizeof (capture_path), "/tmp/wszst-mobipeg-encode-%d.log", (int)getpid ());

	const int rc = run_program_capture (argv, capture_path);
	enumError err = ERR_OK;
	if (rc != 0)
	{
		FILE *f = fopen (capture_path, "r");
		bool too_old = false;
		if (f)
		{
			char line[512];
			while (fgets (line, sizeof (line), f))
				if (strstr (line, "is not known") || strstr (line, "Unknown encoder"))
				{
					too_old = true;
					break;
				}
			fclose (f);
		}

		if (too_old)
			err = ERR_NOTHING_TO_DO;
		else
		{
			dump_capture (capture_path);
			err = ERROR0 (ERR_SUBJOB_FAILED, "pass-through mobipeg encode failed for %s (exit %d)",
				dest_path, rc);
		}
	}
	unlink (capture_path);
	return err;
}

enumError PassthruReencodeMedia (ccp preview_path, ccp source_path)
{
	ccp tool = resolve_mobipeg ();
	if (!tool || !*tool)
		return ERR_NOTHING_TO_DO;
	char tool_path[PATH_MAX];
	snprintf (tool_path, sizeof (tool_path), "%s", tool);

	ccp ext = strrchr (source_path, '.');
	ccp codec = 0, muxer = 0, mobi_generation = 0;
	if (ext && !strcasecmp (ext, ".thp"))
		codec = "thp", muxer = "thp";
	else if (ext && !strcasecmp (ext, ".mo"))
		codec = "mobiclip", muxer = "mobiclip_mo", mobi_generation = "0";
	else if (ext && !strcasecmp (ext, ".moflex"))
		codec = "mobiclip", muxer = "moflex", mobi_generation = "1";
	else if (ext && !strcasecmp (ext, ".mods"))
		codec = "mobiclip", muxer = "mods", mobi_generation = "2";
	else
		return ERR_NOTHING_TO_DO;

	// ffprobe is shipped beside mobipeg. Its compact output gives us the two
	// source-controlled video settings that can be recovered from a finished
	// bitstream. Encoder-only knobs (motion search, multipass, etc.) are not
	// present in any media file and therefore cannot truthfully be inferred.
	char fps[64] = {0}, bitrate[64] = {0};
	ccp probe = resolve_ffprobe_for_mobipeg (tool_path);
	if (probe)
	{
		char probe_path[PATH_MAX];
		snprintf (probe_path, sizeof (probe_path), "%s", probe);
		char capture[PATH_MAX];
		snprintf (capture, sizeof (capture), "/tmp/wszst-mobipeg-probe-%d.log", (int)getpid ());
		char *pargv[] = { probe_path, "-v", "error", "-select_streams", "v:0", "-show_entries",
			"stream=avg_frame_rate,bit_rate", "-of", "default=nw=1:nk=1", (char *)source_path, 0 };
		if (!run_program_capture (pargv, capture))
		{
			FILE *f = fopen (capture, "r");
			if (f)
			{
				if (fgets (fps, sizeof (fps), f)) fps[strcspn (fps, "\r\n")] = 0;
				if (fgets (bitrate, sizeof (bitrate), f)) bitrate[strcspn (bitrate, "\r\n")] = 0;
				fclose (f);
			}
		}
		unlink (capture);
	}

	char temp[PATH_MAX];
	snprintf (temp, sizeof (temp), "%s.wszst-new", source_path);
	char *argv[32]; uint n = 0;
	argv[n++] = tool_path; argv[n++] = "-i"; argv[n++] = (char *)preview_path;
	argv[n++] = "-i"; argv[n++] = (char *)source_path;
	argv[n++] = "-map"; argv[n++] = "0:v:0";
	argv[n++] = "-map"; argv[n++] = "1:a?";
	argv[n++] = "-c:v"; argv[n++] = (char *)codec;
	if (mobi_generation) { argv[n++] = "-mobiclip"; argv[n++] = (char *)mobi_generation; }
	if (*fps && strcmp (fps, "0/0") && strcmp (fps, "N/A")) { argv[n++] = "-r"; argv[n++] = fps; }
	if (*bitrate && strcmp (bitrate, "N/A") && strcmp (bitrate, "0"))
		{ argv[n++] = "-b:v"; argv[n++] = bitrate; }
	argv[n++] = "-c:a"; argv[n++] = "copy";
	argv[n++] = "-f"; argv[n++] = (char *)muxer;
	argv[n++] = "-y"; argv[n++] = temp; argv[n++] = 0;
	assert (n <= sizeof (argv) / sizeof (*argv));

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%sREPACK media passthrough: %s -> %s (%s; source fps/bitrate/audio)\n",
			testmode ? "WOULD " : "", preview_path, source_path, tool_path);
	if (testmode)
		return ERR_OK;
	const int rc = run_program (argv);
	if (rc || rename (temp, source_path))
	{
		unlink (temp);
		return ERROR0 (ERR_SUBJOB_FAILED, "pass-through mobipeg re-encode failed for %s (exit %d)",
			source_path, rc);
	}
	return ERR_OK;
}

// Same as run_program(), but redirects the child's stdout+stderr into
// CAPTURE_PATH instead of inheriting them. hactool exits 0 even when a
// section fails its hash check -- it prints "Error: section N is
// corrupted!" and writes zero bytes for that section instead of failing --
// so the exit code alone can never detect a bad titlekey. Capturing the
// text lets the caller grep for that message and retry with an alternate
// key. Falls back to plain run_program() (inherited stdio, no retry
// possible) if the capture file can't be opened.
static int run_program_capture (char *const argv[], ccp capture_path)
{
	const int fd = open (capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return run_program (argv);

	const pid_t pid = fork ();
	if (pid < 0)
	{
		close (fd);
		return -errno;
	}
	if (pid == 0)
	{
		dup2 (fd, 1);
		dup2 (fd, 2);
		close (fd);
		execv (argv[0], argv);
		_Exit (127); // execv failed
	}
	close (fd);

	int status = 0;
	while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
		;
	if (WIFEXITED (status))
		return WEXITSTATUS (status);
	return -1;
}

// True if a run_program_capture() log contains hactool's hash-verification
// failure message for at least one section.
static bool capture_shows_corruption (ccp capture_path)
{
	FILE *f = fopen (capture_path, "r");
	if (!f)
		return false;
	char line[512];
	bool found = false;
	while (fgets (line, sizeof (line), f))
		if (strstr (line, "is corrupted"))
		{
			found = true;
			break;
		}
	fclose (f);
	return found;
}

// Echo a run_program_capture() log to stdlog, same destination the tool's
// output would have gone to under plain run_program().
static void dump_capture (ccp capture_path)
{
	FILE *f = fopen (capture_path, "r");
	if (!f)
		return;
	char buf[4096];
	size_t n;
	while ((n = fread (buf, 1, sizeof (buf), f)) > 0)
		fwrite (buf, 1, n, stdlog);
	fclose (f);
}

// Return true when TOOL is the Nintendo fork of wit that implements the
// non-disc XEXTRACT/XCREATE commands. Upstream wit accepts `HELP XEXTRACT`
// with exit status 0 but prints no command help, so the exit code alone is
// not a feature probe; require the fork's command-specific Syntax line.
static bool wit_supports_xcontainers (ccp tool)
{
	if (!tool || !*tool)
		return false;

	char capture_path[PATH_MAX];
	snprintf (capture_path, sizeof (capture_path), "/tmp/wszst-wit-probe-%d.log", (int)getpid ());
	char *argv[] = { (char *)tool, "HELP", "XEXTRACT", 0 };
	const int rc = run_program_capture (argv, capture_path);

	bool supported = false;
	if (rc == 0)
	{
		FILE *f = fopen (capture_path, "r");
		if (f)
		{
			char line[512];
			while (fgets (line, sizeof (line), f))
				if (strstr (line, "Syntax: wit XEXTRACT"))
				{
					supported = true;
					break;
				}
			fclose (f);
		}
	}
	unlink (capture_path);
	return supported;
}

// Read the first N bytes of a file. Zeroes buffer first for partial reads.
static bool read_head (ccp src, u8 *buf, uint n)
{
	FILE *f = fopen (src, "rb");
	if (!f)
		return false;
	memset (buf, 0, n);
	const size_t got = fread (buf, 1, n, f);
	const bool ok = (got > 0 || feof (f)) && !ferror (f);
	fclose (f);
	return ok;
}

static bool is_ext (ccp src, ccp ext)
{
	const uint n = strlen (src);
	const uint m = strlen (ext);
	if (n < m)
		return false;
	return strcasecmp (src + n - m, ext) == 0;
}

// True for the file extensions used by disc image tools (wit).
static bool is_disc_ext (ccp src)
{
	return is_ext (src, ".wbfs") || is_ext (src, ".wdf") || is_ext (src, ".ciso")
		|| is_ext (src, ".iso") || is_ext (src, ".gcm") || is_ext (src, ".gca")
		|| is_ext (src, ".wia") || is_ext (src, ".raw") || is_ext (src, ".img");
}

// True for the file extensions a real, not-yet-extracted DS ROM image is
// stored under. Some DS ROM titles literally carry the ASCII string
// "NINTENDO" in their internal game-title header field (verified: real DS
// WFC/wifi-connection utility ROMs bundled inside Wii games), which is also
// this codebase's own claim signature for a DS ROM below -- so a bare
// "NINTENDO"-at-offset-0 check without this extension guard wrongly
// re-claims wit/ndstool's own already-extracted "header.bin" output (an
// exact byte-for-byte copy of that same header, including the title field)
// as if it were a fresh DS ROM to extract, sending it back through
// wit/ndstool where it fails (it isn't a real disc image). Same class of
// bug as the WBFS/disc claim just above, which already learned to pair its
// header signature with is_disc_ext() for exactly this reason.
static bool is_ds_ext (ccp src)
{
	return is_ext (src, ".nds") || is_ext (src, ".srl") || is_ext (src, ".dsi");
}

// Build the staging directory for SRC: BASEDIR (may be NULL or "" for none)
// plus the file name without extension plus ".d".  Returns false when SRC has
// no basename at all.
//
// Without an explicit BASEDIR (no --dest / no --dest-base), every other
// extractor in this codebase stages beside the source via the "\1P/\1N.d"
// SubstDest pattern -- i.e. the source's own directory, not the process's
// current working directory. stage_dir_of() used to fall back to a bare
// "<stem>.d" in that case, so `wszst XX /some/other/dir/foo.nds` from an
// unrelated cwd staged into "./foo.d" instead of "/some/other/dir/foo.d"
// (found by testing a real .nds from a scratch dir while cwd was elsewhere).
static bool stage_dir_of (ccp src, ccp basedir, char *buf, uint bufsize)
{
	ccp basename = strrchr (src, '/');
	basename = basename ? basename + 1 : src;

	char stem[PATH_MAX];
	strncpy (stem, basename, sizeof (stem) - 1);
	stem[sizeof (stem) - 1] = 0;
	char *dot = strrchr (stem, '.');
	if (dot && dot > stem)
		*dot = 0;

	if (basedir && *basedir)
	{
		const uint blen = strlen (basedir);
		if (basedir[blen - 1] == '/')
			snprintf (buf, bufsize, "%s%s.d", basedir, stem);
		else
			snprintf (buf, bufsize, "%s/%s.d", basedir, stem);
	}
	else if (basename != src)
		snprintf (buf, bufsize, "%.*s%s.d", (int)(basename - src), src, stem);
	else
		snprintf (buf, bufsize, "%s.d", stem);
	return *buf != 0;
}

// Report a recognized-but-unsupported pass-through container: the external
// tool for it is not available and the caller should skip the file.
static enumError make_stage_dir (ccp stage, bool tool_missing)
{
	if (tool_missing)
		return ERROR0 (ERR_WARNING,
			"Pass-through tool not found; install it "
			"or select it with --with-<tool>=<path>: %s",
			stage);

	return ERR_OK;
}

// Run the user's --bms=<script> against SRC, staging into STAGE. Returns
// ERR_NOTHING_TO_DO if no BMS script is configured (or during the strong,
// header-only pass, which BMS never participates in) so callers can chain
// it after any other claim attempt.
static enumError run_bms_fallback (
	bool strong_only, ccp src, ccp stage, char *staged_dir, uint staged_dir_size)
{
	if (strong_only || !opt_with_bms || !*opt_with_bms)
		return ERR_NOTHING_TO_DO;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BMS: %s -> %s (%s)\n", testmode ? "WOULD " : "",
			verbose > 0 ? "\n" : "", src, stage, opt_with_bms);

	if (testmode)
	{
		snprintf (staged_dir, staged_dir_size, "%s", stage);
		return ERR_OK;
	}

	if (CreatePath (stage, false))
		return ERROR0 (ERR_CANT_CREATE_DIR, "Cannot create dest dir: %s", stage);

	const enumError bms_err = RunBmsScript (opt_with_bms, src, stage);
	if (bms_err == ERR_OK)
	{
		snprintf (staged_dir, staged_dir_size, "%s", stage);
		return ERR_OK;
	}
	return bms_err;
}

// Call passthru_archive(); when the external tool for this container type
// is missing, fall through to the user's --bms=<script> instead of just
// failing -- this is the "game image extractor" (wit/ndstool/ctrtool/
// hactool/sharpii) path, and a BMS script is a legitimate substitute for
// any of them (e.g. a custom or newer container hactool doesn't know yet).
// Without this, a claim like the Switch NCA/NSP/XCI one below returned
// immediately on a missing hactool and BMS was never reached, even though
// the final fallback later in passthru_claim() would have tried it for an
// otherwise-unclaimed file.
//
// Deliberately ignores STRONG_ONLY here (unlike the final catch-all
// fallback in passthru_claim(), which still respects it): a container
// already claimed by header signature or extension has no native decoder
// to conflict with once its own tool turns out to be missing, so there is
// nothing left for a strong-pass-only restriction to protect -- most real
// containers (NSP/XCI/NCA, NCCH/NCSD, WBFS/ISO/etc.) are claimed in the
// strong pass, which is exactly where this fallback needs to fire.
static enumError passthru_archive_or_bms (ccp src, ccp basedir, ccp stage, char *staged_dir,
	uint staged_dir_size, bool is_ds, bool is_ctr, bool is_wad, bool is_disc, bool is_switch)
{
	const enumError err = passthru_archive (src, basedir, stage, staged_dir, staged_dir_size, is_ds,
		is_ctr, is_wad, is_disc, is_switch);
	if (err == ERR_WARNING)
	{
		const enumError bms_err = run_bms_fallback (false, src, stage, staged_dir, staged_dir_size);
		if (bms_err != ERR_NOTHING_TO_DO)
			return bms_err;
	}
	return err;
}

// If PATH decodes as valid BLZ, overwrite it with the decompressed bytes.
// Silent no-op (not an error) if it doesn't -- most arm7.bin/overlay files
// in particular are often already plain, and ndstool gives no signal
// either way.
static void try_decompress_blz_inplace (ccp path)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (path, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw || raw_size > UINT_MAX)
	{
		if (raw)
			FREE (raw);
		return;
	}

	u8 *dest = 0;
	uint dest_size = 0;
	const enumError err = DecodeBLZ (&dest, &dest_size, raw, (uint)raw_size);
	FREE (raw);
	if (err || !dest)
		return;

	File_t F;
	if (!CreateFileOpt (&F, true, path, false, path) && F.f)
		fwrite (dest, 1, dest_size, F.f);
	ResetFile (&F, false);
	FREE (dest);
}

// Sweep every regular file directly inside DIR through
// try_decompress_blz_inplace() -- used for ndstool's overlay/ directory,
// where overlay count and filenames vary per game.
static void blz_decompress_dir (ccp dir)
{
	DIR *d = opendir (dir);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (e->d_name[0] == '.')
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", dir, e->d_name);
		struct stat st;
		if (!stat (path, &st) && S_ISREG (st.st_mode))
			try_decompress_blz_inplace (path);
	}
	closedir (d);
}

// Read the Rights ID from an NCA header at its fixed offset. Returns true
// and fills RIGHTS_ID/RIGHTS_HEX iff the NCA actually uses titlekey crypto
// (a standard-crypto NCA has an all-zero Rights ID field, which is not an
// error -- most NCAs in a title aren't titlekey-encrypted).
static bool read_nca_rights_id (ccp nca_path, u8 rights_id[16], char rights_hex[33])
{
	memset (rights_id, 0, 16);
	rights_hex[0] = '\0';

	FILE *f = fopen (nca_path, "rb");
	if (!f)
		return false;
	u8 hdr[0x400];
	size_t got = fread (hdr, 1, sizeof (hdr), f);
	fclose (f);
	if (got < 0x220)
		return false;

	memcpy (rights_id, hdr + 0x204, 16);
	bool has_rights = false;
	for (int i = 0; i < 16; i++)
		if (rights_id[i] != 0)
		{
			has_rights = true;
			break;
		}
	if (!has_rights)
		return false;

	for (int i = 0; i < 16; i++)
		snprintf (rights_hex + i * 2, 3, "%02x", rights_id[i]);
	return true;
}

// Look up RIGHTS_HEX in ~/.switch/title.keys (Lockpick_RCM/hactool-database
// convention: "rights_id = titlekek-encrypted_titlekey", the same raw form
// hactool's --titlekey option expects -- see the comment on
// find_nca_titlekey() below about why that raw form matters). Returns true
// and fills OUT_TITLEKEY on a match.
static bool lookup_titlekeys_file (ccp rights_hex, char *out_titlekey, size_t out_size)
{
	out_titlekey[0] = '\0';
	if (!rights_hex || !*rights_hex)
		return false;

	const char *home = getenv ("HOME");
	if (!home)
		return false;

	char tkeys_path[PATH_MAX];
	snprintf (tkeys_path, sizeof (tkeys_path), "%s/.switch/title.keys", home);
	FILE *tkf = fopen (tkeys_path, "r");
	if (!tkf)
		return false;

	char line[256];
	bool found = false;
	while (fgets (line, sizeof (line), tkf))
	{
		char *eq = strchr (line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *k = line;
		while (*k == ' ' || *k == '\t')
			k++;
		char *kend = eq - 1;
		while (kend > k && (*kend == ' ' || *kend == '\t' || *kend == '\r' || *kend == '\n'))
			*kend-- = '\0';

		char *v = eq + 1;
		while (*v == ' ' || *v == '\t')
			v++;
		char *vend = v + strlen (v) - 1;
		while (vend > v && (*vend == ' ' || *vend == '\t' || *vend == '\r' || *vend == '\n'))
			*vend-- = '\0';

		if (!strcasecmp (k, rights_hex) && strlen (v) >= 32)
		{
			snprintf (out_titlekey, out_size, "%.32s", v);
			found = true;
			break;
		}
	}
	fclose (tkf);
	return found;
}

// hactool's own --titlekey option expects the RAW titlekey exactly as it is
// stored in the ticket (still titlekek-encrypted) -- it decrypts it itself
// internally using the NCA's own key generation. This function must hand
// that raw key straight through, never pre-decrypt it: doing so silently
// double-decrypts (hactool decrypts an already-decrypted key with the
// titlekek a second time), producing garbage content-section keys. hactool
// reports no error for this -- it prints "Error: section N is corrupted!"
// per hash-mismatched section and writes zero bytes for it, with a normal
// (0) exit code, so the pass-through looks like it "worked" while silently
// producing an empty tree.
//
// CORRECTION (Super Mario Odyssey NSP, 2026-08-15): the assumption above --
// that the sibling .tik always stores the still-titlekek-encrypted key --
// does not hold for every NSP source. Some repacking tools normalize a
// ticket's stored key to the *already-decrypted* titlekey instead of the
// standard titlekek-encrypted form. There is no header flag that tells you
// which kind a given .tik is; the only way to know is to try decrypting
// with it and see whether the result hash-verifies. So this function still
// prefers the .tik (matches the common case and every previously-verified
// sample), but the caller (passthru_archive's is_switch branch) now detects
// a "section is corrupted" result and retries with the ~/.switch/title.keys
// entry for the same Rights ID via lookup_titlekeys_file() above -- a
// separately curated database that isn't subject to a given NSP's own
// (possibly nonstandard) ticket encoding. Confirmed live on both of this
// title's Rights-ID-crypto NCAs: the .tik's raw stored key decrypts to a
// corrupted RomFS/ExeFS on this particular dump, while the title.keys entry
// for the same Rights ID extracts cleanly (thousands of real .szs files).
static void find_nca_titlekey (ccp nca_path, char *out_titlekey, size_t out_size)
{
	out_titlekey[0] = '\0';
	u8 rights_id[16];
	char rights_hex[33];
	const bool has_rights = read_nca_rights_id (nca_path, rights_id, rights_hex);

	// 1. Check sibling directory for .tik files
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", nca_path);
	char *slash = strrchr (dir, '/');
	if (slash)
		*slash = '\0';
	else
		snprintf (dir, sizeof (dir), ".");

	DIR *d = opendir (dir);
	if (d)
	{
		struct dirent *de;
		u8 fallback_tkey[16] = { 0 };
		bool has_fallback = false;

		while ((de = readdir (d)))
		{
			if (strstr (de->d_name, ".tik"))
			{
				char tik_path[PATH_MAX];
				snprintf (tik_path, sizeof (tik_path), "%s/%s", dir, de->d_name);
				FILE *tf = fopen (tik_path, "rb");
				if (tf)
				{
					u8 tdata[0x300];
					size_t tgot = fread (tdata, 1, sizeof (tdata), tf);
					fclose (tf);
					if (tgot >= 0x190)
					{
						bool nonzero = false;
						for (int i = 0; i < 16; i++)
						{
							if (tdata[0x180 + i] != 0)
							{
								nonzero = true;
								break;
							}
						}
						if (nonzero)
						{
							if (has_rights && tgot >= 0x2B0
								&& !memcmp (tdata + 0x2A0, rights_id, 16))
							{
								for (int i = 0; i < 16; i++)
									snprintf (out_titlekey + i * 2, out_size - i * 2, "%02x",
										tdata[0x180 + i]);
								closedir (d);
								return;
							}
							if (!has_fallback)
							{
								memcpy (fallback_tkey, tdata + 0x180, 16);
								has_fallback = true;
							}
						}
					}
				}
			}
		}
		closedir (d);

		if (has_fallback)
		{
			for (int i = 0; i < 16; i++)
				snprintf (out_titlekey + i * 2, out_size - i * 2, "%02x", fallback_tkey[i]);
			return;
		}
	}

	// 2. Check ~/.switch/title.keys
	if (has_rights)
		lookup_titlekeys_file (rights_hex, out_titlekey, out_size);
}

// Retry-path lookup used by passthru_archive's is_switch branch when the
// .tik-derived key from find_nca_titlekey() above produced a "section is
// corrupted" result. Consults ~/.switch/title.keys only, skipping the
// sibling .tik entirely -- see the CORRECTION comment on find_nca_titlekey()
// for why the two can legitimately disagree for a given NSP. Returns true
// and fills OUT_TITLEKEY on a match.
static bool find_nca_titlekey_from_titlekeys_file (
	ccp nca_path, char *out_titlekey, size_t out_size)
{
	out_titlekey[0] = '\0';
	u8 rights_id[16];
	char rights_hex[33];
	if (!read_nca_rights_id (nca_path, rights_id, rights_hex))
		return false;
	return lookup_titlekeys_file (rights_hex, out_titlekey, out_size);
}

// hactool's romfs extractor does not recursively create parent directory
// paths before attempting fopen(..., "wb"), causing "Failed to open ...!"
// write failures on nested files. Query the romfs directory listing first
// and pre-create all destination directories so hactool's writes succeed.
static void precreate_romfs_dirs (ccp tool, ccp prod_keys, ccp titlekey, ccp src, ccp romfs_dir)
{
	char cmd[PATH_MAX * 3];
	char k_opt[PATH_MAX + 16] = "";
	char t_opt[128] = "";
	if (prod_keys && *prod_keys)
		snprintf (k_opt, sizeof (k_opt), "-k \"%s\"", prod_keys);
	if (titlekey && *titlekey)
		snprintf (t_opt, sizeof (t_opt), "--titlekey=%s", titlekey);

	snprintf (
		cmd, sizeof (cmd), "\"%s\" %s %s --listromfs \"%s\" 2>/dev/null", tool, k_opt, t_opt, src);

	FILE *p = popen (cmd, "r");
	if (!p)
		return;

	char line[PATH_MAX];
	while (fgets (line, sizeof (line), p))
	{
		char *nl = strchr (line, '\r');
		if (nl)
			*nl = 0;
		nl = strchr (line, '\n');
		if (nl)
			*nl = 0;

		ccp rel = 0;
		if (!memcmp (line, "romfs:/", 7))
			rel = line + 7;
		else if (!memcmp (line, "romfs:\\", 7))
			rel = line + 7;
		else if (strstr (line, "romfs:/"))
			rel = strstr (line, "romfs:/") + 7;

		if (rel && *rel)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", romfs_dir, rel);
			CreatePath (path, false);
		}
	}
	pclose (p);
}

// Content-type tag hactool prints in its "-i -t nca" info dump, e.g.
// "Content Type:                       Program". Confirmed against a real
// build (hactool by SciresM, Nov 2023) run over a retail NSP's NCAs.
typedef enum
{
	NCA_CONTENT_UNKNOWN,
	NCA_CONTENT_PROGRAM,
	NCA_CONTENT_CONTROL,
} nca_content_type;

// Ask hactool what an NCA's "Content Type:" is (Program/Control/Meta/...).
// Needed because passthru_archive()'s .nsp branch only gets a pile of raw
// *.nca files from hactool's --pfs0dir dump -- there is no filename
// convention that says which one is the Program NCA (has exefs/romfs/logo)
// vs. the Control NCA (has control.nacp, needed by hacbrewpack).
static nca_content_type identify_nca_content_type (ccp tool, ccp prod_keys, ccp nca_path)
{
	char *argv[8];
	int argc = 0;
	argv[argc++] = (char *)tool;
	argv[argc++] = "-i";
	argv[argc++] = "-t";
	argv[argc++] = "nca";
	if (prod_keys && *prod_keys)
	{
		argv[argc++] = "-k";
		argv[argc++] = (char *)prod_keys;
	}
	argv[argc++] = (char *)nca_path;
	argv[argc] = 0;

	char capture_path[PATH_MAX];
	snprintf (capture_path, sizeof (capture_path), "%s.hactool-info.%d", nca_path, (int)getpid ());

	nca_content_type result = NCA_CONTENT_UNKNOWN;
	if (run_program_capture (argv, capture_path) == 0)
	{
		FILE *f = fopen (capture_path, "rb");
		if (f)
		{
			char line[512];
			while (fgets (line, sizeof (line), f))
			{
				ccp p = strstr (line, "Content Type:");
				if (p)
				{
					p += 13;
					while (*p == ' ' || *p == '\t')
						p++;
					if (!strncmp (p, "Program", 7))
						result = NCA_CONTENT_PROGRAM;
					else if (!strncmp (p, "Control", 7))
						result = NCA_CONTENT_CONTROL;
					break;
				}
			}
			fclose (f);
		}
	}
	unlink (capture_path);
	return result;
}

// Run the external unpacker for STAGE.  Exactly one of the DS/CTR/WAD flags
// is set.  SRC and BASEDIR are only used for messages; STAGE was produced by
// stage_dir_of() already and is filled into STAGED_DIR on success.
static enumError passthru_archive (
	ccp src, ccp basedir, ccp stage, char *staged_dir, uint staged_dir_size,
	bool is_ds, // true: ndstool
	bool is_ctr, // true: ctrtool
	bool is_wad, // true: sharpii
	bool is_disc, // true: wit
	bool is_switch // true: hactool
)
{
	// The Nintendo wit fork handles DS/WAD through XEXTRACT, but an
	// upstream wit may also be first on PATH and does not implement that
	// command. Probe the capability, then fall back to the original
	// format-specific tools instead of invoking an unknown wit command.
	bool use_wit_x = false;
	ccp tool = 0;
	ccp toolname = 0;
	if (is_disc)
	{
		tool = resolve_tool (opt_with_wit, "wit");
		toolname = "wit";
	}
	else if (is_ds || is_wad)
	{
		tool = resolve_tool (opt_with_wit, "wit");
		use_wit_x = wit_supports_xcontainers (tool);
		if (use_wit_x)
			toolname = "wit";
		else if (is_ds)
		{
			tool = resolve_tool (opt_with_ndstool, "ndstool");
			toolname = "ndstool";
		}
		else
		{
			tool = resolve_tool (opt_with_sharpii, "sharpii");
			toolname = "sharpii";
		}
	}
	else if (is_ctr)
	{
		tool = resolve_tool (opt_with_ctrtool, "ctrtool");
		toolname = "ctrtool";
	}
	else if (is_switch)
	{
		tool = resolve_tool (opt_with_hactool, "hactool");
		toolname = "hactool";
	}
	if (!tool || !*tool)
	{
		*staged_dir = 0;
		return make_stage_dir (stage, true);
	}
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT passthrough: %s -> %s (%s)\n", testmode ? "WOULD " : "",
			verbose > 0 ? "\n" : "", src, stage, toolname);

	if (testmode)
	{
		snprintf (staged_dir, staged_dir_size, "%s", stage);
		return ERR_OK;
	}

	// the external tools require an existing destination directory ("-D" of
	// wit would create it too, but ndstool/sharpii/ctrtool need it upfront)
	if (CreatePath (stage, false))
		return ERROR0 (ERR_CANT_CREATE_DIR, "Cannot create dest dir: %s", stage);

	if (is_disc)
	{
		// -D: create the destination path automatically. -f: force, so a
		// stage dir left over from an earlier (partial/failed) run doesn't
		// make wit silently skip instead of extracting. -vv: wit's own
		// second verbosity level turns on its progress counter, useful for
		// diagnosing a stall/failure on a multi-GB disc image instead of
		// getting nothing but our own before/after log lines.
		char *argv[]
			= { (char *)tool, "EXTRACT", "-D", stage, "--overwrite", "-vv", (char *)src, 0 };
		const int rc = run_program (argv);
		if (rc != 0)
			return ERROR0 (
				ERR_SUBJOB_FAILED, "pass-through 'wit EXTRACT' failed for %s (exit %d)", src, rc);
	}
	else if (is_ds)
	{
		char arm9[PATH_MAX], arm7[PATH_MAX], overlay_dir[PATH_MAX];
		snprintf (arm9, sizeof (arm9), "%s/arm9.bin", stage);
		snprintf (arm7, sizeof (arm7), "%s/arm7.bin", stage);
		snprintf (overlay_dir, sizeof (overlay_dir), "%s/overlay", stage);

		if (use_wit_x)
		{
			// The Nintendo wit fork stages the file-system tree under data/
			// and writes its own overlay directory.
			char *argv[]
				= { (char *)tool, "XEXTRACT", (char *)src, (char *)stage, "--overwrite", 0 };
			const int rc = run_program (argv);
			if (rc != 0)
				return ERROR0 (ERR_SUBJOB_FAILED,
					"pass-through 'wit XEXTRACT' failed for %s (exit %d)", src, rc);
		}
		else
		{
			// Upstream wit has no XEXTRACT. ndstool's established staging
			// layout uses data/ and overlay/ plus the two CPU binaries.
			char data_dir[PATH_MAX];
			snprintf (data_dir, sizeof (data_dir), "%s/data", stage);
			(void)CreatePath (data_dir, false);
			(void)CreatePath (overlay_dir, false);
			char *argv[] = { (char *)tool, "-x", (char *)src, "-9", arm9, "-7", arm7, "-d",
				data_dir, "-y", overlay_dir, 0 };
			const int rc = run_program (argv);
			if (rc != 0)
				return ERROR0 (ERR_SUBJOB_FAILED,
					"pass-through 'ndstool -x' failed for %s (exit %d)", src, rc);
		}

		// Both tools preserve BLZ-compressed executables/overlays as stored.
		// DecodeBLZ() is the structural gate against modifying plain files.
		try_decompress_blz_inplace (arm9);
		try_decompress_blz_inplace (arm7);
		blz_decompress_dir (overlay_dir);
	}
	else if (is_ctr)
	{
		// ctrtool decrypts with its built-in retail common keys by default.
		// Point --exefsdir and --romfsdir at STAGE to produce recursible trees.
		char exefs_path[PATH_MAX], romfs_path[PATH_MAX];
		snprintf (exefs_path, sizeof (exefs_path), "%s/exefs", stage);
		snprintf (romfs_path, sizeof (romfs_path), "%s/romfs", stage);
		(void)CreatePath (exefs_path, false);
		(void)CreatePath (romfs_path, false);

		char exefsdir_arg[PATH_MAX], romfsdir_arg[PATH_MAX];
		snprintf (exefsdir_arg, sizeof (exefsdir_arg), "--exefsdir=%s", exefs_path);
		snprintf (romfsdir_arg, sizeof (romfsdir_arg), "--romfsdir=%s", romfs_path);

		char *argv[16];
		int argc = 0;
		argv[argc++] = (char *)tool;
		argv[argc++] = exefsdir_arg;
		argv[argc++] = romfsdir_arg;
		argv[argc++] = "--decompresscode";
		argv[argc++] = (char *)src;
		argv[argc] = 0;

		const int rc = run_program (argv);
		if (rc != 0)
			return ERROR0 (
				ERR_SUBJOB_FAILED, "pass-through 'ctrtool' failed for %s (exit %d)", src, rc);
	}
	else if (is_wad)
	{
		if (use_wit_x)
		{
			char *argv[]
				= { (char *)tool, "XEXTRACT", (char *)src, (char *)stage, "--overwrite", 0 };
			const int rc = run_program (argv);
			if (rc != 0)
				return ERROR0 (ERR_SUBJOB_FAILED,
					"pass-through 'wit XEXTRACT' failed for %s (exit %d)", src, rc);
		}
		else
		{
			char *argv[] = { (char *)tool, "WAD", "-u", (char *)src, (char *)stage, 0 };
			const int rc = run_program (argv);
			if (rc != 0)
				return ERROR0 (ERR_SUBJOB_FAILED,
					"pass-through 'sharpii WAD -u' failed for %s (exit %d)", src, rc);
		}
	}
	else if (is_switch)
	{
		const char *home = getenv ("HOME");
		char prod_keys[PATH_MAX] = "";
		if (home)
		{
			snprintf (prod_keys, sizeof (prod_keys), "%s/.switch/prod.keys", home);
			if (access (prod_keys, R_OK))
				prod_keys[0] = '\0';
		}

		char titlekey_opt[128] = "";
		char romfs_dir[PATH_MAX], exefs_dir[PATH_MAX], sec0_dir[PATH_MAX];
		char pfs0_dir[PATH_MAX], xci_dir[PATH_MAX];
		char romfs_path[PATH_MAX] = "";
		char tkey[64] = "";
		bool is_nca = false;
		bool is_pfs0_dump = false;
		char *argv[16];
		int argc = 0;
		argv[argc++] = (char *)tool;

		if (*prod_keys)
		{
			argv[argc++] = "-k";
			argv[argc++] = prod_keys;
		}

		if (is_ext (src, ".nca") || is_ext (src, ".cnmt.nca"))
		{
			is_nca = true;
			argv[argc++] = "-x";
			char exefs_path[PATH_MAX], sec0_path[PATH_MAX];
			snprintf (romfs_path, sizeof (romfs_path), "%s/romfs", stage);
			snprintf (exefs_path, sizeof (exefs_path), "%s/exefs", stage);
			snprintf (sec0_path, sizeof (sec0_path), "%s/section0", stage);
			(void)CreatePath (romfs_path, false);
			(void)CreatePath (exefs_path, false);
			(void)CreatePath (sec0_path, false);

			snprintf (romfs_dir, sizeof (romfs_dir), "--romfsdir=%s", romfs_path);
			snprintf (exefs_dir, sizeof (exefs_dir), "--exefsdir=%s", exefs_path);
			snprintf (sec0_dir, sizeof (sec0_dir), "--section0dir=%s", sec0_path);
			argv[argc++] = romfs_dir;
			argv[argc++] = exefs_dir;
			argv[argc++] = sec0_dir;

			find_nca_titlekey (src, tkey, sizeof (tkey));
			if (*tkey)
			{
				snprintf (titlekey_opt, sizeof (titlekey_opt), "--titlekey=%s", tkey);
				argv[argc++] = titlekey_opt;
			}
			precreate_romfs_dirs (tool, prod_keys, tkey, src, romfs_path);
		}
		else if (is_ext (src, ".xci"))
		{
			argv[argc++] = "-x";
			snprintf (xci_dir, sizeof (xci_dir), "--outdir=%s", stage);
			argv[argc++] = xci_dir;
		}
		else
		{
			is_pfs0_dump = true;
			argv[argc++] = "-x";
			argv[argc++] = "-t";
			argv[argc++] = "pfs0";
			snprintf (pfs0_dir, sizeof (pfs0_dir), "--pfs0dir=%s", stage);
			argv[argc++] = pfs0_dir;
		}

		argv[argc++] = (char *)src;
		argv[argc] = 0;

		// A titlekey-crypto NCA can fail its hash check with a *wrong but
		// well-formed-looking* key and hactool still exits 0 (see the
		// run_program_capture()/capture_shows_corruption() comments above),
		// so this path always captures the tool's output instead of trusting
		// the exit code alone. Non-NCA extractions (XCI/PFS0, no titlekey
		// involved) go through the same call but can never trigger the retry
		// since find_nca_titlekey_from_titlekeys_file() only matches when the
		// .tik-derived key actually differs from a title.keys entry.
		char capture_path[PATH_MAX];
		snprintf (capture_path, sizeof (capture_path), "%s.hactool-out.%d", stage, (int)getpid ());

		int rc = run_program_capture (argv, capture_path);
		if (rc == 0 && is_nca && *tkey && capture_shows_corruption (capture_path))
		{
			char alt_tkey[64] = "";
			if (find_nca_titlekey_from_titlekeys_file (src, alt_tkey, sizeof (alt_tkey))
				&& strcasecmp (alt_tkey, tkey) != 0)
			{
				fprintf (stdlog,
					"%s: .tik-derived titlekey produced a corrupted section;"
					" retrying with the ~/.switch/title.keys entry for this Rights ID\n",
					src);
				snprintf (titlekey_opt, sizeof (titlekey_opt), "--titlekey=%s", alt_tkey);
				precreate_romfs_dirs (tool, prod_keys, alt_tkey, src, romfs_path);
				unlink (capture_path);
				rc = run_program_capture (argv, capture_path);
			}
		}
		dump_capture (capture_path);
		unlink (capture_path);

		if (rc != 0)
			return ERROR0 (
				ERR_SUBJOB_FAILED, "pass-through 'hactool' failed for %s (exit %d)", src, rc);

		// An .nsp only got its raw *.nca files dumped into STAGE above --
		// unlike the standalone .nca branch, hactool's --pfs0dir doesn't know
		// which of those NCAs is the Program NCA (holds exefs/romfs/logo) or
		// the Control NCA (holds control.nacp+icons). Both are needed later
		// by PassthruPack()'s hacbrewpack repack step, so do a second pass:
		// probe each dumped *.nca's "Content Type:" via hactool -i and, for
		// the ones that matter, extract them again with the section dirs
		// hacbrewpack expects (same --exefsdir/--romfsdir/--section2dir=logo
		// pattern the standalone .nca branch above already uses; verified by
		// hand against a retail NSP: Program NCA section2 is the Logo PFS0).
		if (is_pfs0_dump)
		{
			DIR *d = opendir (stage);
			if (d)
			{
				struct dirent *de;
				while ((de = readdir (d)) != 0)
				{
					if (!is_ext (de->d_name, ".nca") || is_ext (de->d_name, ".cnmt.nca"))
						continue;

					char nca_path[PATH_MAX];
					snprintf (nca_path, sizeof (nca_path), "%s/%s", stage, de->d_name);

					nca_content_type ctype = identify_nca_content_type (tool, prod_keys, nca_path);
					if (ctype == NCA_CONTENT_UNKNOWN)
						continue;

					char sub_tkey[64] = "";
					find_nca_titlekey (nca_path, sub_tkey, sizeof (sub_tkey));

					if (ctype == NCA_CONTENT_PROGRAM)
					{
						char exefs_path[PATH_MAX], romfs2_path[PATH_MAX], logo_path[PATH_MAX];
						snprintf (exefs_path, sizeof (exefs_path), "%s/exefs", stage);
						snprintf (romfs2_path, sizeof (romfs2_path), "%s/romfs", stage);
						snprintf (logo_path, sizeof (logo_path), "%s/logo", stage);
						(void)CreatePath (exefs_path, false);
						(void)CreatePath (romfs2_path, false);
						(void)CreatePath (logo_path, false);
						precreate_romfs_dirs (tool, prod_keys, sub_tkey, nca_path, romfs2_path);

						char exefs_arg[PATH_MAX + 16], romfs_arg[PATH_MAX + 16],
							logo_arg[PATH_MAX + 16];
						snprintf (exefs_arg, sizeof (exefs_arg), "--exefsdir=%s", exefs_path);
						snprintf (romfs_arg, sizeof (romfs_arg), "--romfsdir=%s", romfs2_path);
						snprintf (logo_arg, sizeof (logo_arg), "--section2dir=%s", logo_path);

						char *sub_argv[10];
						int sub_argc = 0;
						sub_argv[sub_argc++] = (char *)tool;
						sub_argv[sub_argc++] = "-x";
						if (*prod_keys)
						{
							sub_argv[sub_argc++] = "-k";
							sub_argv[sub_argc++] = prod_keys;
						}
						sub_argv[sub_argc++] = exefs_arg;
						sub_argv[sub_argc++] = romfs_arg;
						sub_argv[sub_argc++] = logo_arg;
						char sub_tkey_opt[128] = "";
						if (*sub_tkey)
						{
							snprintf (
								sub_tkey_opt, sizeof (sub_tkey_opt), "--titlekey=%s", sub_tkey);
							sub_argv[sub_argc++] = sub_tkey_opt;
						}
						sub_argv[sub_argc++] = nca_path;
						sub_argv[sub_argc] = 0;

						char sub_capture[PATH_MAX];
						snprintf (sub_capture, sizeof (sub_capture), "%s.hactool-prog.%d", stage,
							(int)getpid ());
						int sub_rc = run_program_capture (sub_argv, sub_capture);
						dump_capture (sub_capture);
						unlink (sub_capture);
						if (sub_rc != 0)
							fprintf (stdlog,
								"%s: warning: failed to extract Program NCA exefs/romfs/logo (exit "
								"%d)\n",
								src, sub_rc);
					}
					else if (ctype == NCA_CONTENT_CONTROL)
					{
						char control_path[PATH_MAX];
						snprintf (control_path, sizeof (control_path), "%s/control", stage);
						(void)CreatePath (control_path, false);
						precreate_romfs_dirs (tool, prod_keys, sub_tkey, nca_path, control_path);

						char control_arg[PATH_MAX + 16];
						snprintf (control_arg, sizeof (control_arg), "--romfsdir=%s", control_path);

						char *sub_argv[10];
						int sub_argc = 0;
						sub_argv[sub_argc++] = (char *)tool;
						sub_argv[sub_argc++] = "-x";
						if (*prod_keys)
						{
							sub_argv[sub_argc++] = "-k";
							sub_argv[sub_argc++] = prod_keys;
						}
						sub_argv[sub_argc++] = control_arg;
						char sub_tkey_opt2[128] = "";
						if (*sub_tkey)
						{
							snprintf (
								sub_tkey_opt2, sizeof (sub_tkey_opt2), "--titlekey=%s", sub_tkey);
							sub_argv[sub_argc++] = sub_tkey_opt2;
						}
						sub_argv[sub_argc++] = nca_path;
						sub_argv[sub_argc] = 0;

						char sub_capture[PATH_MAX];
						snprintf (sub_capture, sizeof (sub_capture), "%s.hactool-ctrl.%d", stage,
							(int)getpid ());
						int sub_rc = run_program_capture (sub_argv, sub_capture);
						dump_capture (sub_capture);
						unlink (sub_capture);
						if (sub_rc != 0)
							fprintf (stdlog,
								"%s: warning: failed to extract Control NCA romfs (exit %d)\n", src,
								sub_rc);
					}
				}
				closedir (d);
			}
		}
	}

	snprintf (staged_dir, staged_dir_size, "%s", stage);
	return ERR_OK;
}

// Same as run_program(), but chdir()s the child into WORKDIR first. wud2app
// has no output-directory flag -- it always mkdir()s a 10-char title-id
// folder (read from the disc itself) relative to its own cwd -- so this is
// the only way to control where that folder lands.
static int run_program_in_dir (char *const argv[], ccp workdir)
{
	const pid_t pid = fork ();
	if (pid < 0)
		return -errno;
	if (pid == 0)
	{
		if (chdir (workdir) != 0)
			_Exit (126);
		execv (argv[0], argv);
		_Exit (127); // execv failed
	}

	int status = 0;
	while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
		;
	if (WIFEXITED (status))
		return WEXITSTATUS (status);
	return -1;
}

// Wii U retail disc common key, shared across every title (paired with a
// per-title key to decrypt that title's partition). Constant is public --
// this is the same value already vendored, independently, inside cdecrypt's
// own source; kept here too since wud2app is a separate process invoked by
// path, not linked in, and needs the key as a 16-byte file on disk.
static const u8 WiiUDiscCommonKey[16] = { 0xD7, 0xB0, 0x04, 0x02, 0x65, 0x9B, 0xA2, 0xAB, 0xD2,
	0xCB, 0x0D, 0xB2, 0x7F, 0xA2, 0xB6, 0x56 };

// Decompress a WUX (sparse-compressed Wii U disc image) to a plain WUD.
bool wux_decompress (ccp src, ccp dst)
{
	FILE *fin = fopen (src, "rb");
	if (!fin)
		return false;

	u8 hdr[32];
	bool ok = fread (hdr, 1, sizeof (hdr), fin) == sizeof (hdr);

	u32 magic0 = 0, magic1 = 0, sector_size = 0, flags = 0;
	u64 uncompressed_size = 0;
	if (ok)
	{
		memcpy (&magic0, hdr + 0, 4);
		memcpy (&magic1, hdr + 4, 4);
		memcpy (&sector_size, hdr + 8, 4);
		memcpy (&uncompressed_size, hdr + 16, 8);
		memcpy (&flags, hdr + 24, 4);
		(void)flags;
		ok = magic0 == 0x30585557 /*'WUX0'*/ && magic1 == 0x1099d02e && sector_size > 0;
	}

	u32 *index_table = 0;
	u64 entry_count = 0;
	FILE *fout = 0;
	u8 *buf = 0;

	if (ok)
	{
		entry_count = (uncompressed_size + sector_size - 1) / sector_size;
		index_table = MALLOC ((size_t)entry_count * sizeof (u32));
		ok = index_table != 0;
	}
	if (ok)
	{
		fseeko (fin, 32, SEEK_SET);
		ok = fread (index_table, sizeof (u32), entry_count, fin) == entry_count;
	}
	if (ok)
	{
		u64 offset_index_table = 32;
		u64 offset_sector_array = offset_index_table + entry_count * 4;
		offset_sector_array = (offset_sector_array + sector_size - 1) / sector_size * sector_size;

		fout = fopen (dst, "wb");
		buf = ok ? MALLOC (sector_size) : 0;
		ok = fout && buf;

		u64 remaining = uncompressed_size;
		for (u64 i = 0; ok && i < entry_count; i++)
		{
			const u64 off = offset_sector_array + (u64)index_table[i] * sector_size;
			const u64 n = remaining < sector_size ? remaining : sector_size;
			if (fseeko (fin, off, SEEK_SET) != 0 || fread (buf, 1, n, fin) != n
				|| fwrite (buf, 1, n, fout) != n)
				ok = false;
			remaining -= n;
		}
	}

	FREE (buf);
	FREE (index_table);
	if (fout)
		fclose (fout);
	fclose (fin);
	if (!ok)
		unlink (dst);
	return ok;
}

// Compress a plain WUD to sparse-compressed WUX.
bool wux_compress (ccp src, ccp dst)
{
	FILE *fin = fopen (src, "rb");
	if (!fin)
		return false;

	fseeko (fin, 0, SEEK_END);
	off_t file_size = ftello (fin);
	fseeko (fin, 0, SEEK_SET);

	if (file_size <= 0)
	{
		fclose (fin);
		return false;
	}

	const u32 sector_size = 0x00010000; // 64 KB
	const u64 uncompressed_size = (u64)file_size;
	const u64 entry_count = (uncompressed_size + sector_size - 1) / sector_size;

	u32 *index_table = CALLOC ((size_t)entry_count, sizeof (u32));
	if (!index_table)
	{
		fclose (fin);
		return false;
	}

	FILE *fout = fopen (dst, "wb");
	if (!fout)
	{
		FREE (index_table);
		fclose (fin);
		return false;
	}

	u64 offset_index_table = 32;
	u64 offset_sector_array = offset_index_table + entry_count * 4;
	offset_sector_array = (offset_sector_array + sector_size - 1) / sector_size * sector_size;

	u8 *buf = MALLOC (sector_size);
	u8 *zero_buf = CALLOC (1, sector_size);
	if (!buf || !zero_buf)
	{
		FREE (buf);
		FREE (zero_buf);
		FREE (index_table);
		fclose (fin);
		fclose (fout);
		unlink (dst);
		return false;
	}

	fseeko (fout, (off_t)offset_sector_array, SEEK_SET);

	u32 next_sector_idx = 0;
	int zero_sector_idx = -1;
	bool ok = true;
	u64 remaining = uncompressed_size;

	for (u64 i = 0; ok && i < entry_count; i++)
	{
		const u64 n = remaining < sector_size ? remaining : sector_size;
		memset (buf, 0, sector_size);
		if (fread (buf, 1, n, fin) != n)
		{
			ok = false;
			break;
		}
		remaining -= n;

		if (!memcmp (buf, zero_buf, sector_size))
		{
			if (zero_sector_idx < 0)
			{
				zero_sector_idx = (int)next_sector_idx++;
				if (fwrite (zero_buf, 1, sector_size, fout) != sector_size)
				{
					ok = false;
					break;
				}
			}
			index_table[i] = (u32)zero_sector_idx;
		}
		else
		{
			index_table[i] = next_sector_idx++;
			if (fwrite (buf, 1, sector_size, fout) != sector_size)
			{
				ok = false;
				break;
			}
		}
	}

	if (ok)
	{
		u8 hdr[32];
		memset (hdr, 0, sizeof (hdr));
		const u32 magic0 = 0x30585557; // 'WUX0'
		const u32 magic1 = 0x1099d02e;
		memcpy (hdr + 0, &magic0, 4);
		memcpy (hdr + 4, &magic1, 4);
		memcpy (hdr + 8, &sector_size, 4);
		memcpy (hdr + 16, &uncompressed_size, 8);
		fseeko (fout, 0, SEEK_SET);
		if (fwrite (hdr, 1, sizeof (hdr), fout) != sizeof (hdr))
			ok = false;
		if (ok && fwrite (index_table, sizeof (u32), entry_count, fout) != entry_count)
			ok = false;
	}

	FREE (buf);
	FREE (zero_buf);
	FREE (index_table);
	fclose (fin);
	fclose (fout);
	if (!ok)
		unlink (dst);
	return ok;
}

// Locate the sibling <basename>.key file next to SRC (the common Redump
// Wii U disc-key distribution convention: a raw 16-byte binary title key
// with the same basename as the .wud/.wux, ".key" extension). Returns
// false if not found or not exactly 16 bytes.
static bool find_sibling_title_key (ccp src, char *keypath, uint keypath_size)
{
	ccp dot = strrchr (src, '.');
	ccp end = dot && dot > src ? dot : src + strlen (src);
	snprintf (keypath, keypath_size, "%.*s.key", (int)(end - src), src);

	struct stat st;
	return !stat (keypath, &st) && S_ISREG (st.st_mode) && st.st_size == 16;
}

// Recursively delete DIR (files + subdirs). Used to drop intermediate
// directories once consumed during extraction / repacking.
void remove_dir_recursive (ccp dir)
{
	DIR *d = opendir (dir);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir (d)))
	{
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, ".."))
			continue;
		char full[PATH_MAX];
		snprintf (full, sizeof (full), "%s/%s", dir, e->d_name);
		struct stat st;
		if (!lstat (full, &st))
		{
			if (S_ISDIR (st.st_mode))
				remove_dir_recursive (full);
			else
				unlink (full);
		}
	}
	closedir (d);
	rmdir (dir);
}

bool is_dir_newer_than (ccp dirpath, time_t target_mtime)
{
	DIR *dir = opendir (dirpath);
	if (!dir)
		return false;
	struct dirent *de;
	bool newer = false;
	while (!newer && (de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", dirpath, de->d_name);
		struct stat st;
		if (lstat (path, &st))
			continue;
		if (S_ISDIR (st.st_mode))
		{
			if (is_dir_newer_than (path, target_mtime))
				newer = true;
		}
		else if (S_ISREG (st.st_mode))
		{
			// Skip files that are auto-generated by wszst and never edited by the user.
			// These are written AFTER wszst-setup.txt, so they would otherwise always
			// make the directory look "newer", causing untouched archives to be rebuilt.
			const size_t nlen = strlen (de->d_name);

			// Exact-name tool metadata files
			if (!strcmp (de->d_name, "wszst-setup.txt") || !strcmp (de->d_name, "setup.txt")
				|| !strcmp (de->d_name, "setup.bat") || !strcmp (de->d_name, "setup.sh")
				|| !strcmp (de->d_name, "align-files.txt") || !strcmp (de->d_name, ".DS_Store")
				|| !strcmp (de->d_name, SZS_HASH_CACHE_FILE) || strstr (de->d_name, "string-pool"))
				continue;

			// Suffix-based tool-generated companion files:
			//   *.tflyt      -- layout text companion (brlan/brlyt decode)
			//   *.byml.yaml  -- byml text companion
			//   *.ncer.xml   -- ncer text companion
			//   *.nanr.xml   -- nanr text companion
			//   *.kcl.obj    -- kcl mesh companion
			//   *.kcl.mtl    -- kcl material companion
			//   *.glb / .dae -- model mesh companion
			if (nlen > 6 && !strcasecmp (de->d_name + nlen - 6, ".tflyt"))
				continue;
			if (nlen > 10 && !strcasecmp (de->d_name + nlen - 10, ".byml.yaml"))
				continue;
			if (nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".ncer.xml"))
				continue;
			if (nlen > 9 && !strcasecmp (de->d_name + nlen - 9, ".nanr.xml"))
				continue;
			if (nlen > 8 && !strcasecmp (de->d_name + nlen - 8, ".kcl.obj"))
				continue;
			if (nlen > 8 && !strcasecmp (de->d_name + nlen - 8, ".kcl.mtl"))
				continue;
			if (nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".glb"))
				continue;
			if (nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".dae"))
				continue;

			// PNG companion files: if a sibling native texture exists and PNG is not newer than
			// sibling + 2
			if (nlen > 4 && !strcasecmp (de->d_name + nlen - 4, ".png"))
			{
				char stem[PATH_MAX];
				snprintf (stem, sizeof (stem), "%.*s", (int)(strlen (path) - 4), path);
				struct stat st_sib;
				if (!stat (stem, &st_sib) && S_ISREG (st_sib.st_mode))
				{
					if (st.st_mtime <= st_sib.st_mtime + 2)
						continue;
				}
			}

			if (st.st_mtime > target_mtime + 2)
				newer = true;
		}
	}
	closedir (dir);
	return newer;
}

// Wii U retail disc image (.wud raw, or .wux sparse-compressed). Unlike the
// other pass-through containers, this needs a two-stage external chain
// (wud2app to pull the encrypted partition content off the disc, cdecrypt
// to decrypt it) plus a per-title key that isn't bundled with either tool.
// Verified end-to-end on a real retail WUX (Super Mario Maker, USA): the
// decrypted output diffs byte-identical against an independently-installed
// reference copy of the same title. See [[wiiu_wud_decrypt_pipeline]].
static enumError passthru_wiiu_disc (
	ccp src, ccp stage, char *staged_dir, uint staged_dir_size, bool is_wux)
{
	// resolve_tool()/find_program() return a pointer into a single shared
	// static buffer -- copy each result out immediately, or the second
	// call here silently overwrites the first (both locals would alias the
	// same memory, so the "wud2app" invocation below would actually exec
	// whatever cdecrypt resolved to, with wud2app's argv).
	char wud2app[PATH_MAX] = "", cdecrypt[PATH_MAX] = "";
	ccp found = resolve_tool (0, "wud2app");
	if (found)
		snprintf (wud2app, sizeof (wud2app), "%s", found);
	found = resolve_tool (0, "cdecrypt");
	if (found)
		snprintf (cdecrypt, sizeof (cdecrypt), "%s", found);
	if (!*wud2app || !*cdecrypt)
	{
		*staged_dir = 0;
		return ERROR0 (ERR_WARNING,
			"Wii U disc pass-through needs both 'wud2app' and 'cdecrypt' on"
			" PATH; install them to extract: %s",
			src);
	}

	char keypath[PATH_MAX];
	if (!find_sibling_title_key (src, keypath, sizeof (keypath)))
	{
		*staged_dir = 0;
		return ERROR0 (ERR_WARNING,
			"Wii U disc needs its 16-byte title key next to it (%s); place"
			" it there (e.g. from a Redump key set) to extract: %s",
			keypath, src);
	}

	// is_pure_dir=true: unlike the other passthru_archive() branches, no
	// external tool creates STAGE itself here -- wud2app needs it to exist
	// as its cwd before it ever runs, since it has no output-dir flag.
	if (CreatePath (stage, true))
		return ERROR0 (ERR_CANT_CREATE_DIR, "Cannot create dest dir: %s", stage);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT passthrough: %s -> %s (wud2app+cdecrypt)\n",
			testmode ? "WOULD " : "", verbose > 0 ? "\n" : "", src, stage);
	if (testmode)
	{
		snprintf (staged_dir, staged_dir_size, "%s", stage);
		return ERR_OK;
	}

	char abs_stage[PATH_MAX], abs_key[PATH_MAX], abs_common[PATH_MAX], abs_wud[PATH_MAX];
	if (!realpath (stage, abs_stage) || !realpath (keypath, abs_key))
		return ERROR0 (ERR_ERROR, "Cannot resolve pass-through paths for %s", src);

	snprintf (abs_common, sizeof (abs_common), "%s/common.key", abs_stage);
	FILE *ck = fopen (abs_common, "wb");
	if (!ck || fwrite (WiiUDiscCommonKey, 1, 16, ck) != 16)
	{
		if (ck)
			fclose (ck);
		return ERROR0 (ERR_ERROR, "Cannot write common key for %s", src);
	}
	fclose (ck);

	char temp_wud[PATH_MAX] = "";
	if (is_ext (src, ".wux"))
	{
		snprintf (temp_wud, sizeof (temp_wud), "%s/temp_disc_%d.wud", abs_stage, (int)getpid ());
		if (!wux_decompress (src, temp_wud))
		{
			unlink (abs_common);
			return ERROR0 (ERR_ERROR, "Failed to decompress WUX: %s", src);
		}
		snprintf (abs_wud, sizeof (abs_wud), "%s", temp_wud);
	}
	else if (!realpath (src, abs_wud))
	{
		unlink (abs_common);
		return ERROR0 (ERR_ERROR, "Cannot resolve %s", src);
	}

	// snapshot stage's entries so the new title-id folder wud2app creates
	// (name comes from the disc itself, not something we choose) can be
	// told apart from common.key/the temp .wud/anything already there.
	StringField_t before = { 0 };
	DIR *d = opendir (abs_stage);
	if (d)
	{
		struct dirent *e;
		while ((e = readdir (d)))
			if (e->d_name[0] != '.')
				InsertStringField (&before, e->d_name, false);
		closedir (d);
	}

	char *argv[] = { (char *)wud2app, abs_common, abs_key, abs_wud, 0 };
	const int rc = run_program_in_dir (argv, abs_stage);

	unlink (abs_common);
	if (*temp_wud)
		unlink (temp_wud);

	if (rc != 0)
	{
		ResetStringField (&before);
		return ERROR0 (
			ERR_SUBJOB_FAILED, "pass-through 'wud2app' failed for %s (exit %d)", src, rc);
	}

	char app_dir[PATH_MAX] = "";
	d = opendir (abs_stage);
	if (d)
	{
		struct dirent *e;
		while ((e = readdir (d)))
		{
			if (e->d_name[0] == '.')
				continue;
			if (FindStringField (&before, e->d_name))
				continue;
			char full[PATH_MAX];
			snprintf (full, sizeof (full), "%s/%s", abs_stage, e->d_name);
			struct stat st;
			if (!stat (full, &st) && S_ISDIR (st.st_mode))
			{
				snprintf (app_dir, sizeof (app_dir), "%s", full);
				break;
			}
		}
		closedir (d);
	}
	ResetStringField (&before);

	if (!*app_dir)
		return ERROR0 (ERR_ERROR, "wud2app produced no title folder for %s", src);

	char *cd_argv[] = { (char *)cdecrypt, app_dir, 0 };
	const int cd_rc = run_program (cd_argv);

	if (cd_rc != 0)
	{
		remove_dir_recursive (app_dir);
		return ERROR0 (
			ERR_SUBJOB_FAILED, "pass-through 'cdecrypt' failed for %s (exit %d)", src, cd_rc);
	}

	DIR *ad = opendir (app_dir);
	if (ad)
	{
		struct dirent *de;
		while ((de = readdir (ad)))
		{
			if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
				continue;
			if (!strcmp (de->d_name, "code") || !strcmp (de->d_name, "content")
				|| !strcmp (de->d_name, "meta"))
			{
				char src_item[PATH_MAX], dst_item[PATH_MAX];
				snprintf (src_item, sizeof (src_item), "%s/%s", app_dir, de->d_name);
				snprintf (dst_item, sizeof (dst_item), "%s/%s", abs_stage, de->d_name);
				rename (src_item, dst_item);
			}
		}
		closedir (ad);
	}

	remove_dir_recursive (app_dir);

	snprintf (staged_dir, staged_dir_size, "%s", stage);
	return ERR_OK;
}

// Try to pass SRC through to an external unpacker.  Fills STAGED_DIR with the
// directory that received the payload (relative to BASEDIR) on success.
enumError PassthruExtract (ccp src, ccp basedir, char *staged_dir, uint staged_dir_size)
{
	return passthru_claim (false, src, basedir, staged_dir, staged_dir_size);
}

// Strong-signature variant: only files that are unambiguously external
// containers by their HEADER (disc image magics, the DS "NINTENDO" tag) are
// claimed.  Called BEFORE the native probes in cmd_extract() so that huge
// disc images never trip the --max-file-size limited LoadFileAlloc() of the
// archive extractors.  Extension-only claims (.nds/.cia/.3ds/.cci/.cxi/
// .wad/.app) stay in the weak path so native decoders get first refusal.
enumError PassthruExtractStrong (ccp src, ccp basedir, char *staged_dir, uint staged_dir_size)
{
	return passthru_claim (true, src, basedir, staged_dir, staged_dir_size);
}

static enumError passthru_claim (bool strong_only, // true: header-claimed containers only
	ccp src, ccp basedir, char *staged_dir, uint staged_dir_size)
{
	// containers are dispatched by their header signature; an extension
	// match is only tried when no signature is present.  0x420 covers the
	// XML-disc probe at 0x418 (0x400 prefix skip + 0x18 disc header).
	u8 head[0x420];
	if (!read_head (src, head, sizeof (head)))
		return ERR_NOTHING_TO_DO;

	char stage[PATH_MAX];
	if (!stage_dir_of (src, basedir, stage, sizeof (stage)))
		return ERR_NOTHING_TO_DO;

	// ----- claimed by header signature alone -----

	// Wii / GC disc images  (WBFS, WDF, CISO, raw ISO)
	//
	// The container headers are magic-keyed:
	//   WBFS  files start with the ASCII tag "WBFS".
	//   WDF   files start with the ASCII tag "WDF\0".
	//   CISO  files start with the ASCII tag "CISO".
	//   Raw   Wii/GC ISO images have no signature at offset 0; the disc
	//         header at offset 0x18 carries the wii_magic 0x5d1c9ea3 (GC
	//         uses 0xc2339f3d at 0x1c) right after the 0x18 byte title
	//         / game code area.  XML-disc images skip the 0x400 byte
	//         prefix, so probe 0x400 instead of 0 for those.
	bool is_wbfs = !memcmp (head, "WBFS", 4);
	bool is_wdf = !memcmp (head, "WDF\0", 4);
	bool is_ciso = !memcmp (head, "CISO", 4);
	bool is_iso = (head[0x18] == 0x5d && head[0x19] == 0x1c && head[0x1a] == 0x9e
					  && head[0x1b] == 0xa3) // Wii raw
		|| (head[0x1c] == 0xc2 && head[0x1d] == 0x33 && head[0x1e] == 0x9f
			&& head[0x1f] == 0x3d) // GC raw
		|| (head[0x418] == 0x5d && head[0x419] == 0x1c && head[0x41a] == 0x9e
			&& head[0x41b] == 0xa3); // XML disc
	// A disc claim requires the container header signature AND a disc file
	// extension.  Without the extension check, already-extracted disc
	// sub-files (e.g. the 1K "boot.bin" passport block, whose header carries
	// the same wii_magic at 0x18) would wrongly be re-dispatched to wit.
	bool is_disc = (is_wbfs || is_wdf || is_ciso || is_iso) && is_disc_ext (src);
	bool is_ds = !memcmp (head, "NINTENDO", 8) && is_ds_ext (src);
	if (is_disc || is_ds) // DS ROM header claim
	{
		return passthru_archive_or_bms (
			src, basedir, stage, staged_dir, staged_dir_size, is_ds, false, false, is_disc, false);
	}

	// Wii U disc image (strong pass, header-only -- this MUST run before any
	// native probe, same reasoning as the Wii/GC disc claim above: a raw WUD
	// is a full 20-25GB disc image, and even a WUX is multi-GB, so either
	// one reaching a native LoadFileAlloc()-style full-file read is an OOM.
	//   WUX  files start with the ASCII tag "WUX0" + a fixed magic1 word
	//        (verified against the reference WudCompress/wud.cpp reader).
	//   WUD  (raw disc) has no dedicated container magic; every retail
	//        Wii U disc's game code begins "WUP-" (the Wii U product
	//        prefix), so that + the .wud extension is the claim, mirroring
	//        how the Wii/GC raw-ISO claim above pairs a header signature
	//        with is_disc_ext() rather than trusting either alone.
	bool is_wux = !memcmp (head, "WUX0", 4) && head[4] == 0x2e && head[5] == 0xd0 && head[6] == 0x99
		&& head[7] == 0x10;
	bool is_wud = !memcmp (head, "WUP-", 4) && is_ext (src, ".wud");
	if (is_wux || is_wud)
		return passthru_wiiu_disc (src, stage, staged_dir, staged_dir_size, is_wux);

	// 3DS NCCH / NCSD header signatures (strong pass):
	// NCCH at offset 0x100 (.cxi, .cfa, .app)
	// NCSD at offset 0x100 (.3ds, .cci)
	bool is_ncch = !memcmp (head + 0x100, "NCCH", 4);
	bool is_ncsd = !memcmp (head + 0x100, "NCSD", 4);
	if (is_ncch || is_ncsd)
		return passthru_archive_or_bms (
			src, basedir, stage, staged_dir, staged_dir_size, false, true, false, false, false);

	// Switch NSP/XCI/NCA (strong pass):
	// PFS0 (offset 0), XCI "HEAD" tag (offset 0x100), and NCA magic (offset 0x200 or 0)
	// Real NCA magic is 4 bytes, "NCA2" or "NCA3" (the two versions ever
	// shipped) -- checking only the first 3 raw bytes ("NCA") throws away
	// the version digit that actually disambiguates a real container from
	// random data, and coincidentally matches ~1 in 16M byte positions.
	// Confirmed as a real false-positive: a Retro Studios .pak's raw CMPD
	// compressed entry (Metroid Prime 3) has an "NCA" 3-byte run at offset
	// 0x200 purely by chance, which wrongly routed it to hactool ("PFS0 is
	// corrupt") even though the file is not remotely an NCA/NSP.
	// A bare 4-byte "PFS0" match at offset 0 has the same false-positive
	// risk as the old 3-byte NCA check above: confirmed on a real Pangya!
	// Golf with Style archive, whose raw (non-Switch) sub-data happened to
	// start with those 4 bytes purely by chance, again routing it to
	// hactool for the same "PFS0 is corrupt!" failure. A real PFS0 header
	// is magic + u32 file_count + u32 string_table_size + u32 reserved
	// (reserved must be 0); require file_count/string_table_size to be
	// sane and reserved to actually be 0 before trusting the magic alone.
	u32 pfs0_file_count = head[4] | head[5] << 8 | head[6] << 16 | head[7] << 24;
	u32 pfs0_str_size = head[8] | head[9] << 8 | head[10] << 16 | head[11] << 24;
	u32 pfs0_reserved = head[12] | head[13] << 8 | head[14] << 16 | head[15] << 24;
	bool is_nsp = !memcmp (head, "PFS0", 4) && pfs0_reserved == 0 && pfs0_file_count > 0
		&& pfs0_file_count < 10000 && pfs0_str_size < 0x100000;
	// Same false-positive risk as PFS0 above: a bare 4-byte "HEAD" match at
	// offset 0x100 also hit a real Pangya! Golf with Style sound-effect
	// file (.gsp, "GSNDB" container, nothing Switch-related) purely by
	// chance. Cross-check against hactool's own xci_header_t (SciresM/
	// hactool xci.h): the cart_type byte at head+0x10D is a closed enum
	// with exactly 6 real values (1/2/4/8/16/32 GB cartridge sizes) --
	// require it to be one of those before trusting the magic.
	bool xci_cart_size_valid = head[0x10D] == 0xFA || head[0x10D] == 0xF8 || head[0x10D] == 0xF0
		|| head[0x10D] == 0xE0 || head[0x10D] == 0xE1 || head[0x10D] == 0xE2;
	bool is_xci = !memcmp (head + 0x100, "HEAD", 4) && xci_cart_size_valid;
	bool is_nca_sig = !memcmp (head + 0x200, "NCA2", 4) || !memcmp (head + 0x200, "NCA3", 4)
		|| !memcmp (head, "NCA2", 4) || !memcmp (head, "NCA3", 4);
	if (is_nsp || is_xci || is_nca_sig)
		return passthru_archive_or_bms (
			src, basedir, stage, staged_dir, staged_dir_size, false, false, false, false, true);

	// 7-Zip / RAR / Tar archives (strong pass)
	bool is_7z_magic = !memcmp (head, "7z\xBC\xAF\x27\x1C", 6);
	bool is_rar_magic = !memcmp (head, "Rar!\x1A\x07", 6);
	bool is_tar_magic = !memcmp (head + 257, "ustar", 5);
	if (is_7z_magic || is_rar_magic || is_tar_magic)
		return passthru_7z (src, basedir, stage, staged_dir, staged_dir_size);

	// ----- claimed by extension alone (weak path only) -----

	// Nintendo DS ROM  (by extension)
	if (!strong_only && is_ext (src, ".nds"))
		return passthru_archive_or_bms (
			src, basedir, stage, staged_dir, staged_dir_size, true, false, false, false, false);

	// CIA / 3DS containers (by extension)
	if (!strong_only
		&& (is_ext (src, ".cia") || is_ext (src, ".3ds") || is_ext (src, ".cci")
			|| is_ext (src, ".cxi") || is_ext (src, ".cfa")))
		return passthru_archive_or_bms (
			src, basedir, stage, staged_dir, staged_dir_size, false, true, false, false, false);

	// Switch NCA / NSP / XCI (by extension)
	if (!strong_only && (is_ext (src, ".nca") || is_ext (src, ".nsp") || is_ext (src, ".xci")))
		return passthru_archive_or_bms (
			src, basedir, stage, staged_dir, staged_dir_size, false, false, false, false, true);

	// Wii WAD (and the common 00000001.app content blob): both need a
	// header check, not just the extension. A ".app" is ambiguous -- it is
	// also the name sharpii itself gives to a WAD's *unpacked* IOS/title
	// content files, which are raw ELF/binary payloads with no WAD header
	// at all. Re-submitting those to sharpii (observed via a real WAD
	// round-trip: extracting a Photo Channel WAD produces 00000004.app/
	// 00000006.app/00000007.app, none of them WADs) fails loudly on every
	// one. A real WAD/boot2 header starts with a big-endian header size
	// (0x20 or 0x40) followed by the "Is\0\0"/"ib\0\0" type tag.
	bool is_wad_header = head[0] == 0 && head[1] == 0
		&& (head[2] == 0x00 && head[3] == 0x20 || head[2] == 0x00 && head[3] == 0x40)
		&& head[4] == 'I' && (head[5] == 's' || head[5] == 'b') && head[6] == 0 && head[7] == 0;
	if (!strong_only && is_wad_header && (is_ext (src, ".wad") || is_ext (src, ".app")))
		return passthru_archive_or_bms (
			src, basedir, stage, staged_dir, staged_dir_size, false, false, true, false, false);

	// 7-Zip / RAR / Tar archives (by extension)
	if (!strong_only
		&& (is_ext (src, ".7z") || is_ext (src, ".rar") || is_ext (src, ".cb7")
			|| is_ext (src, ".tar") || is_ext (src, ".tgz") || is_ext (src, ".tbz2")
			|| is_ext (src, ".txz")))
		return passthru_7z (src, basedir, stage, staged_dir, staged_dir_size);

	// Media files (THP, Mobiclip, BRSTM, BCSTM, BFSTM, BNS, BTSND, AST, DSP, HVQM4, etc.)
	bool is_thp = !memcmp (head, "THP\0", 4) || is_ext (src, ".thp");
	bool is_mobiclip = !memcmp (head, ".MOC", 4) || !memcmp (head, ".MOD", 4) || is_ext (src, ".mo")
		|| is_ext (src, ".mods") || is_ext (src, ".moflex");
	// NOTE: ".bns" is deliberately magic-only here, not extension-fallback
	// like the siblings above it -- Koei Tecmo's Samurai Warriors 3 also
	// ships a completely unrelated "LINKDATA*.BNS" *archive* format under
	// the same extension (no magic, native support in ScanBNS()/
	// extract_bns_file()), and extension-only claiming here would steal
	// those files from the native extractor before it ever runs. A real
	// stream-audio .bns always starts with the "BNS " magic.
	bool is_stream_audio = !memcmp (head, "RSTM", 4) || !memcmp (head, "CSTM", 4)
		|| !memcmp (head, "FSTM", 4) || !memcmp (head, "BNS ", 4) || is_ext (src, ".brstm")
		|| is_ext (src, ".bcstm") || is_ext (src, ".bfstm") || is_ext (src, ".btsnd")
		|| is_ext (src, ".ast") || is_ext (src, ".dsp");
	bool is_other_media = is_ext (src, ".h4m") || is_ext (src, ".dpg") || is_ext (src, ".fv")
		|| is_ext (src, ".ppm") || is_ext (src, ".kwz") || is_ext (src, ".mmstr")
		|| is_ext (src, ".rvid") || is_ext (src, ".vx");

	if (is_thp || is_mobiclip || is_stream_audio || is_other_media)
	{
		ccp mobipeg = resolve_mobipeg ();
		if (mobipeg)
			return passthru_media (
				src, basedir, stage, staged_dir, staged_dir_size, is_stream_audio);
	}

	// Final fallback: nothing above claimed this file at all (not even a
	// recognized-but-tool-missing container) -- give the user's --bms=
	// script a shot at it before giving up.
	{
		const enumError bms_err
			= run_bms_fallback (strong_only, src, stage, staged_dir, staged_dir_size);
		if (bms_err != ERR_NOTHING_TO_DO)
			return bms_err;
	}

	return ERR_NOTHING_TO_DO;
}

bool IsDiscExt (ccp path)
{
	return path ? is_disc_ext (path) : false;
}

enumError PassthruPack (ccp src_dir, ccp dest)
{
	if (!src_dir || !dest || !*src_dir || !*dest)
		return ERR_NOTHING_TO_DO;

	if (opt_no_passthrough)
		return ERR_NOTHING_TO_DO;

	const size_t src_len = strlen (src_dir);
	const bool is_dot_d = (src_len > 2 && !strcasecmp (src_dir + src_len - 2, ".d"));

	// Nintendo DS sound archives are built by wbrsar, which owns the Nitro
	// SYMB/INFO/FAT/FILE implementation and SSEQ assembler.
	if (is_ext (dest, ".sdat"))
	{
		ccp tool = resolve_tool (0, "wbrsar");
		if (!tool || !*tool)
			return ERROR0 (ERR_NOT_EXISTS, "wbrsar not found; cannot pack SDAT: %s", dest);
		if (testmode)
			return ERR_OK;
		char *argv[] = { (char *)tool, "pack", (char *)src_dir, (char *)dest, "--sdat", 0 };
		const int rc = run_program (argv);
		return rc ? ERROR0 (ERR_SUBJOB_FAILED, "wbrsar failed packing %s (exit %d)", dest, rc)
			: ERR_OK;
	}

	// 1. Wii / GameCube disc images (.wbfs, .iso, .ciso, .wdf, .wia, .gcz, .gcm, .gca, .raw, .img)
	if (is_disc_ext (dest))
	{
		ccp tool = resolve_tool (opt_with_wit, "wit");
		if (!tool || !*tool)
			return make_stage_dir (dest, true);

		struct stat st_dst;
		bool dst_exists = (stat (dest, &st_dst) == 0 && S_ISREG (st_dst.st_mode));
		if (dst_exists && !is_dir_newer_than (src_dir, st_dst.st_mtime))
		{
			if (verbose >= 0)
				fprintf (stdlog, "ALREADY UP TO DATE: %s/ -> %s\n", src_dir, dest);
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sREPACK disc passthrough: %s/ -> %s (wit)\n",
				testmode ? "WOULD " : "", verbose > 0 ? "\n" : "", src_dir, dest);

		if (testmode)
			return ERR_OK;

		char *argv[]
			= { (char *)tool, "COPY", "-D", (char *)dest, "-o", "-f", "-vv", (char *)src_dir, 0 };
		const int rc = run_program (argv);
		if (rc != 0)
			return ERROR0 (ERR_SUBJOB_FAILED,
				"pass-through 'wit COPY' failed for %s -> %s (exit %d)", src_dir, dest, rc);
		if (is_dot_d)
			remove_dir_recursive (src_dir);
		return ERR_OK;
	}

	// 2. Nintendo DS ROM (.nds)
	if (is_ext (dest, ".nds"))
	{
		ccp tool = resolve_tool (opt_with_wit, "wit");
		bool use_ndstool = !wit_supports_xcontainers (tool);
		if (use_ndstool)
			tool = resolve_tool (opt_with_ndstool, "ndstool");
		if (!tool || !*tool)
			return make_stage_dir (dest, true);

		char arm9[PATH_MAX], arm7[PATH_MAX];
		snprintf (arm9, sizeof (arm9), "%s/arm9.bin", src_dir);
		snprintf (arm7, sizeof (arm7), "%s/arm7.bin", src_dir);
		struct stat st;
		if (stat (arm9, &st) || !S_ISREG (st.st_mode) || stat (arm7, &st) || !S_ISREG (st.st_mode))
			return ERR_NOTHING_TO_DO;

		struct stat st_dst;
		bool dst_exists = stat (dest, &st_dst) == 0 && S_ISREG (st_dst.st_mode);
		if (dst_exists && !is_dir_newer_than (src_dir, st_dst.st_mtime))
		{
			if (verbose >= 0)
				fprintf (stdlog, "ALREADY UP TO DATE: %s/ -> %s\n", src_dir, dest);
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sREPACK nds passthrough: %s/ -> %s (%s)\n",
				testmode ? "WOULD " : "", verbose > 0 ? "\n" : "", src_dir, dest,
				use_ndstool ? "ndstool" : "wit");
		if (testmode)
			return ERR_OK;

		int rc;
		if (!use_ndstool)
		{
			char *argv[]
				= { (char *)tool, "XCREATE", (char *)src_dir, (char *)dest, "--overwrite", 0 };
			rc = run_program (argv);
		}
		else
		{
			char data_dir[PATH_MAX], overlay_dir[PATH_MAX];
			char header[PATH_MAX], banner[PATH_MAX], y9[PATH_MAX], y7[PATH_MAX];
			snprintf (data_dir, sizeof (data_dir), "%s/data", src_dir);
			snprintf (overlay_dir, sizeof (overlay_dir), "%s/overlay", src_dir);
			snprintf (header, sizeof (header), "%s/header.bin", src_dir);
			snprintf (banner, sizeof (banner), "%s/banner.bin", src_dir);
			snprintf (y9, sizeof (y9), "%s/y9.bin", src_dir);
			snprintf (y7, sizeof (y7), "%s/y7.bin", src_dir);

			char *argv[32];
			int argc = 0;
			argv[argc++] = (char *)tool;
			argv[argc++] = "-c";
			argv[argc++] = (char *)dest;
			argv[argc++] = "-9";
			argv[argc++] = arm9;
			argv[argc++] = "-7";
			argv[argc++] = arm7;
			if (stat (data_dir, &st) == 0 && S_ISDIR (st.st_mode))
			{
				argv[argc++] = "-d";
				argv[argc++] = data_dir;
			}
			if (stat (overlay_dir, &st) == 0 && S_ISDIR (st.st_mode))
			{
				argv[argc++] = "-y";
				argv[argc++] = overlay_dir;
			}
			if (stat (header, &st) == 0 && S_ISREG (st.st_mode))
			{
				argv[argc++] = "-h";
				argv[argc++] = header;
			}
			if (stat (banner, &st) == 0 && S_ISREG (st.st_mode))
			{
				argv[argc++] = "-t";
				argv[argc++] = banner;
			}
			if (stat (y9, &st) == 0 && S_ISREG (st.st_mode))
			{
				argv[argc++] = "-y9";
				argv[argc++] = y9;
			}
			if (stat (y7, &st) == 0 && S_ISREG (st.st_mode))
			{
				argv[argc++] = "-y7";
				argv[argc++] = y7;
			}
			argv[argc] = 0;
			rc = run_program (argv);
		}
		if (rc)
			return ERROR0 (ERR_SUBJOB_FAILED,
				"pass-through NDS repack failed for %s -> %s (exit %d)", src_dir, dest, rc);
		if (is_dot_d)
			remove_dir_recursive (src_dir);
		return ERR_OK;
	}

	// 3. Wii WAD (.wad)
	if (is_ext (dest, ".wad"))
	{
		// Prefer wit's own XCREATE (x-wad.c): it reads back the exact
		// cert.bin/tik.bin/tmd.bin/footer.bin/%08x.app layout that wit's
		// XEXTRACT now produces on the extraction side (see is_wad branch
		// above), so a WAD round-trip no longer needs sharpii installed at
		// all. Fall back to sharpii's "WAD -p" when wit is unavailable or
		// lacks the fork-only X commands; it reads the same staged contents.
		ccp tool = resolve_tool (opt_with_wit, "wit");
		bool use_sharpii = !wit_supports_xcontainers (tool);
		ccp tool_name = use_sharpii ? "sharpii" : "wit";
		if (use_sharpii)
			tool = resolve_tool (opt_with_sharpii, "sharpii");
		if (!tool || !*tool)
			return make_stage_dir (dest, true);

		struct stat st_dst;
		bool dst_exists = (stat (dest, &st_dst) == 0 && S_ISREG (st_dst.st_mode));
		if (dst_exists && !is_dir_newer_than (src_dir, st_dst.st_mtime))
		{
			if (verbose >= 0)
				fprintf (stdlog, "ALREADY UP TO DATE: %s/ -> %s\n", src_dir, dest);
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sREPACK wad passthrough: %s/ -> %s (%s)\n",
				testmode ? "WOULD " : "", verbose > 0 ? "\n" : "", src_dir, dest, tool_name);

		if (testmode)
			return ERR_OK;

		char *argv[8];
		int argc = 0;
		argv[argc++] = (char *)tool;
		if (use_sharpii)
		{
			argv[argc++] = "WAD";
			argv[argc++] = "-p";
			argv[argc++] = (char *)src_dir;
			argv[argc++] = (char *)dest;
			argv[argc++] = "-f";
		}
		else
		{
			argv[argc++] = "XCREATE";
			argv[argc++] = (char *)src_dir;
			argv[argc++] = (char *)dest;
			argv[argc++] = "--overwrite";
		}
		argv[argc] = 0;

		const int rc = run_program (argv);
		if (rc != 0)
			return ERROR0 (ERR_SUBJOB_FAILED, "pass-through '%s' failed for %s -> %s (exit %d)",
				use_sharpii ? "sharpii WAD -p" : "wit XCREATE", src_dir, dest, rc);
		if (is_dot_d)
			remove_dir_recursive (src_dir);
		return ERR_OK;
	}

	// 4. 3DS ROM / CIA / CXI (.3ds, .cia, .cxi, .ncch)
	if (is_ext (dest, ".cia") || is_ext (dest, ".3ds") || is_ext (dest, ".cxi")
		|| is_ext (dest, ".ncch"))
	{
		struct stat st_dst;
		bool dst_exists = (stat (dest, &st_dst) == 0 && S_ISREG (st_dst.st_mode));
		if (dst_exists && !is_dir_newer_than (src_dir, st_dst.st_mtime))
		{
			if (verbose >= 0)
				fprintf (stdlog, "ALREADY UP TO DATE: %s/ -> %s\n", src_dir, dest);
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}

		ccp makerom = resolve_tool (0, "makerom");
		if (makerom && *makerom)
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sREPACK 3ds passthrough: %s/ -> %s (makerom)\n",
					testmode ? "WOULD " : "", verbose > 0 ? "\n" : "", src_dir, dest);

			if (testmode)
				return ERR_OK;

			char romfs_dir[PATH_MAX], exefs_dir[PATH_MAX], icon_path[PATH_MAX],
				banner_path[PATH_MAX];
			snprintf (romfs_dir, sizeof (romfs_dir), "%s/romfs", src_dir);
			snprintf (exefs_dir, sizeof (exefs_dir), "%s/exefs", src_dir);
			snprintf (icon_path, sizeof (icon_path), "%s/exefs/icon.bin", src_dir);
			snprintf (banner_path, sizeof (banner_path), "%s/exefs/banner.bin", src_dir);

			char *argv[32];
			int argc = 0;
			argv[argc++] = (char *)makerom;
			argv[argc++] = "-f";
			argv[argc++] = is_ext (dest, ".cia") ? "cia" : is_ext (dest, ".cxi") ? "cxi" : "cci";
			argv[argc++] = "-o";
			argv[argc++] = (char *)dest;

			struct stat st;
			if (stat (romfs_dir, &st) == 0 && S_ISDIR (st.st_mode))
			{
				argv[argc++] = "-romfs";
				argv[argc++] = romfs_dir;
			}
			if (stat (exefs_dir, &st) == 0 && S_ISDIR (st.st_mode))
			{
				argv[argc++] = "-exefsdir";
				argv[argc++] = exefs_dir;
			}
			if (stat (icon_path, &st) == 0 && S_ISREG (st.st_mode))
			{
				argv[argc++] = "-icon";
				argv[argc++] = icon_path;
			}
			if (stat (banner_path, &st) == 0 && S_ISREG (st.st_mode))
			{
				argv[argc++] = "-banner";
				argv[argc++] = banner_path;
			}
			argv[argc] = 0;

			const int rc = run_program (argv);
			if (rc == 0)
			{
				if (is_dot_d)
					remove_dir_recursive (src_dir);
				return ERR_OK;
			}
		}

		if (dst_exists)
		{
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}
		return ERR_NOTHING_TO_DO;
	}

	// 5. Nintendo Switch (.nsp only -- see below for .xci/.nca)
	//
	// Rebuilt via hacbrewpack (The-4n's tool, the standard homebrew/CFW NSP
	// builder). It can only produce an NSP a modded/Atmosphere Switch will
	// accept -- a genuinely Nintendo-signed retail package needs keys only
	// Nintendo has, which is an accepted, permanent limitation here, not a
	// bug. Flags below are confirmed against a real locally-built
	// hacbrewpack's own --help output, not guessed from docs:
	//   --exefsdir/--romfsdir/--logodir/--controldir  section source dirs
	//   --nspdir     output directory (hacbrewpack picks its own filename
	//                inside it, based on title id -- not an exact path we
	//                can hand it), so the produced .nsp is moved to DEST
	//                afterward.
	// There is no --titleid flag at all in this hacbrewpack build (checked
	// its real --help output). It gets the title id itself, unconditionally,
	// by reading 8 bytes at offset 0x3038 of <--controldir>/control.nacp
	// (confirmed by reading hacBrewPack's own nacp_process() source, not
	// guessed) and exits with an error if that file is missing -- so unlike
	// --logodir, --controldir/control.nacp is NOT optional here, and this
	// branch fails cleanly up front if passthru_archive()'s Control-NCA
	// extraction didn't produce one instead of letting hacbrewpack fail
	// deeper into the run.
	// Needs the same prod.keys as the extract side (header_key +
	// key_area_key_application_xx); hacbrewpack looks for
	// ~/.switch/prod.keys itself by default, same convention already used
	// for hactool elsewhere in this file.
	// 5a-xci. Switch .xci: hacbrewpack has no XCI output mode at all (see
	// its --help; only --nspdir exists) and there's no keyless way here to
	// synthesize a signed gamecard header, so a byte-real .xci is out of
	// reach with the tools this pipeline has. What a modded Switch actually
	// installs from is an NSP (Tinfoil/DBI/etc. -- raw XCI is for physical
	// cartridges or emulators, never a NAND install), so an .xci repack
	// request is honored by building that same installable NSP via
	// hacbrewpack and dropping it next to DEST as "<dest-without-ext>.nsp"
	// instead of silently no-op'ing. DEST itself (the .xci path) is
	// deliberately left unwritten -- writing NSP bytes under a .xci name
	// would just be a mislabeled file, not a real XCI.
	bool xci_via_nsp = false;
	char xci_nsp_dest[PATH_MAX];
	if (is_ext (dest, ".xci"))
	{
		xci_via_nsp = true;
		const uint stem_len = (uint)(strlen (dest) - 4); // ".xci"
		snprintf (xci_nsp_dest, sizeof (xci_nsp_dest), "%.*s.nsp", stem_len, dest);
		dest = xci_nsp_dest;
	}

	if (is_ext (dest, ".nsp"))
	{
		ccp tool = resolve_tool (opt_with_hacbrewpack, "hacbrewpack");
		if (!tool || !*tool)
			return make_stage_dir (dest, true);

		char exefs_dir[PATH_MAX], romfs_dir[PATH_MAX], logo_dir[PATH_MAX], control_dir[PATH_MAX];
		snprintf (exefs_dir, sizeof (exefs_dir), "%s/exefs", src_dir);
		snprintf (romfs_dir, sizeof (romfs_dir), "%s/romfs", src_dir);
		snprintf (logo_dir, sizeof (logo_dir), "%s/logo", src_dir);
		snprintf (control_dir, sizeof (control_dir), "%s/control", src_dir);

		struct stat st;
		if (stat (exefs_dir, &st) || !S_ISDIR (st.st_mode))
			return ERR_NOTHING_TO_DO;

		// control.nacp is where hacbrewpack (unconditionally, no --titleid
		// override exists) reads the title id from -- fail clearly here
		// rather than let hacbrewpack die deeper into the run with a less
		// obvious "Failed to open .../control.nacp!" message.
		char control_nacp[PATH_MAX];
		snprintf (control_nacp, sizeof (control_nacp), "%s/control.nacp", control_dir);
		if (stat (control_nacp, &st) || !S_ISREG (st.st_mode))
			return ERROR0 (ERR_SUBJOB_FAILED,
				"pass-through 'hacbrewpack' needs %s (Control NCA's control.nacp,"
				" for the title id) but it wasn't extracted for %s -> %s",
				control_nacp, src_dir, dest);

		struct stat st_dst;
		bool dst_exists = (stat (dest, &st_dst) == 0 && S_ISREG (st_dst.st_mode));
		if (dst_exists && !is_dir_newer_than (src_dir, st_dst.st_mtime))
		{
			if (verbose >= 0)
				fprintf (stdlog, "ALREADY UP TO DATE: %s/ -> %s\n", src_dir, dest);
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}

		if (verbose >= 0 || testmode)
		{
			if (xci_via_nsp)
				fprintf (stdlog,
					"%s%sREPACK xci-as-nsp passthrough: %s/ -> %s "
					"(hacbrewpack; no XCI output mode exists, NSP is what a "
					"modded Switch actually installs)\n",
					testmode ? "WOULD " : "", verbose > 0 ? "\n" : "", src_dir, dest);
			else
				fprintf (stdlog, "%s%sREPACK nsp passthrough: %s/ -> %s (hacbrewpack)\n",
					testmode ? "WOULD " : "", verbose > 0 ? "\n" : "", src_dir, dest);
		}

		if (testmode)
			return ERR_OK;

		char nspdir[PATH_MAX];
		snprintf (nspdir, sizeof (nspdir), "%s.hacbrewpack_nsp.%d", src_dir, (int)getpid ());
		(void)CreatePath (nspdir, false);

		const char *home = getenv ("HOME");
		char prod_keys[PATH_MAX] = "";
		if (home)
		{
			snprintf (prod_keys, sizeof (prod_keys), "%s/.switch/prod.keys", home);
			if (access (prod_keys, R_OK))
				prod_keys[0] = '\0';
		}

		char exefs_arg[PATH_MAX + 16], romfs_arg[PATH_MAX + 16], nspdir_arg[PATH_MAX + 16];
		char logo_arg[PATH_MAX + 16] = "", control_arg[PATH_MAX + 16];
		snprintf (exefs_arg, sizeof (exefs_arg), "--exefsdir=%s", exefs_dir);
		snprintf (romfs_arg, sizeof (romfs_arg), "--romfsdir=%s", romfs_dir);
		snprintf (nspdir_arg, sizeof (nspdir_arg), "--nspdir=%s", nspdir);
		snprintf (control_arg, sizeof (control_arg), "--controldir=%s", control_dir);

		bool have_logo = !stat (logo_dir, &st) && S_ISDIR (st.st_mode);
		if (have_logo)
			snprintf (logo_arg, sizeof (logo_arg), "--logodir=%s", logo_dir);

		char *argv[16];
		int argc = 0;
		argv[argc++] = (char *)tool;
		if (*prod_keys)
		{
			argv[argc++] = "-k";
			argv[argc++] = prod_keys;
		}
		argv[argc++] = exefs_arg;
		argv[argc++] = romfs_arg;
		argv[argc++] = control_arg;
		if (have_logo)
			argv[argc++] = logo_arg;
		else
			argv[argc++] = "--nologo";
		argv[argc++] = nspdir_arg;
		argv[argc] = 0;

		const int rc = run_program (argv);
		if (rc != 0)
		{
			remove_dir_recursive (nspdir);
			return ERROR0 (ERR_SUBJOB_FAILED,
				"pass-through 'hacbrewpack' failed for %s -> %s (exit %d)", src_dir, dest, rc);
		}

		// hacbrewpack names its own output (by title id), not DEST -- find
		// whatever single .nsp it dropped into nspdir/ and move it into place.
		DIR *d = opendir (nspdir);
		char produced[PATH_MAX] = "";
		if (d)
		{
			struct dirent *de;
			while ((de = readdir (d)) != 0)
			{
				if (is_ext (de->d_name, ".nsp"))
				{
					snprintf (produced, sizeof (produced), "%s/%s", nspdir, de->d_name);
					break;
				}
			}
			closedir (d);
		}

		enumError err = ERR_OK;
		if (!*produced)
			err = ERROR0 (ERR_SUBJOB_FAILED,
				"'hacbrewpack' reported success but produced no .nsp for %s -> %s", src_dir, dest);
		else if (rename (produced, dest))
			err = ERROR1 (ERR_CANT_CREATE, "Can't move %s -> %s\n", produced, dest);

		remove_dir_recursive (nspdir);
		if (err)
			return err;

		if (is_dot_d)
			remove_dir_recursive (src_dir);
		return ERR_OK;
	}

	// 5b. Switch .nca repacking: not implemented. A lone .nca output has no
	// established tool in this pipeline (hacbrewpack only assembles a full
	// NSP from a set of NCAs it builds itself). Leave it a clean no-op
	// rather than a half-correct guess. (.xci is handled above, as an NSP.)
	if (is_ext (dest, ".nca"))
	{
		struct stat st_dst;
		bool dst_exists = (stat (dest, &st_dst) == 0 && S_ISREG (st_dst.st_mode));
		if (dst_exists && !is_dir_newer_than (src_dir, st_dst.st_mtime))
		{
			if (verbose >= 0)
				fprintf (stdlog, "ALREADY UP TO DATE: %s/ -> %s\n", src_dir, dest);
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}

		if (dst_exists)
		{
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}
		return ERR_NOTHING_TO_DO;
	}

	// 6. Wii U disc / sparse disc (.wud, .wux, .rpx)
	if (is_ext (dest, ".wux") || is_ext (dest, ".wud") || is_ext (dest, ".rpx"))
	{
		struct stat st_dst;
		bool dst_exists = (stat (dest, &st_dst) == 0 && S_ISREG (st_dst.st_mode));
		if (dst_exists && !is_dir_newer_than (src_dir, st_dst.st_mtime))
		{
			if (verbose >= 0)
				fprintf (stdlog, "ALREADY UP TO DATE: %s/ -> %s\n", src_dir, dest);
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}

		if (is_ext (dest, ".wux"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sREPACK wux: %s -> %s\n", testmode ? "WOULD " : "",
					verbose > 0 ? "\n" : "", src_dir, dest);

			if (testmode)
				return ERR_OK;

			if (!wux_compress (src_dir, dest))
				return ERROR0 (
					ERR_SUBJOB_FAILED, "WUX compression failed for %s -> %s", src_dir, dest);
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}

		if (dst_exists)
		{
			if (is_dot_d)
				remove_dir_recursive (src_dir);
			return ERR_OK;
		}
		return ERR_NOTHING_TO_DO;
	}

	return ERR_NOTHING_TO_DO;
}
