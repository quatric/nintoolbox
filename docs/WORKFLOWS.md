# Editing and rebuilding games

This is the practical guide for the `wiimms-szs-tools-plus` fork. The root
README is the format reference; this file is the source of truth for the
extract/edit/rebuild workflow.

## Build

```sh
make -C project -j4
```

The tools are written to `project/`. Most archive and asset operations are
native. Whole-disc/ROM formats and unusual media use companion tools; pass an
explicit path such as `--with-wit=/path/to/wit` or
`--with-mobipeg=/path/to/mobipeg` when they are not on `PATH`.

## Unpack recursively

```sh
project/wszst XX game.wbfs --dest game.d --overwrite
```

`XX` extracts the outer game and walks into recognized child archives and
assets. Native formats stay in-process. Disc/ROM formats are staged through a
companion program and then enter the same recursive walk.

Editable companions are written beside their binary sources—for example PNG
for textures, GLB/DAE for models, XML/YAML for structured data, and MP4/WAV for
media. Keep the original binary and extraction metadata: they identify the
correct parent and preserve unchanged content.

## Rebuild only what changed

```sh
project/wszst CREATE game.d --dest rebuilt.wbfs --overwrite
```

Rebuilding is depth-first and bottom-up:

1. Changed editable assets are encoded into their immediate binary parents.
2. Only child archives affected by those changes are rebuilt.
3. Rebuilt children propagate upward until the game container is packed.
4. An unchanged branch is skipped, leaving its binary byte-for-byte intact.

Each extracted archive directory contains `.wszst-cache.txt`. It records SHA-1
content hashes, not merely timestamps. A matching member is not re-encoded and
does not make its archive rebuild. Missing or malformed cache data is safe: it
causes a conservative rebuild, never a false cache hit. Do not add the cache to
the packed archive or hand-edit it.

Generated preview files have their timestamps normalized to the source when
they are extracted. An untouched preview is therefore discarded during a
rebuild, while a subsequently edited preview is processed. Archive members use
the stronger SHA-1 cache described above.

## mobipeg media replacement

THP and MobiClip (`.mo`, `.moflex`, `.mods`) previews are decoded through
mobipeg. If the MP4 preview is edited, `CREATE` sends it back through mobipeg
and derives the recoverable preset from the original source file:

- the same container and MobiClip generation;
- the source average frame rate;
- the source average video bitrate, when recorded; and
- the original encoded audio stream, copied without transcoding.

This avoids replacing a retail clip with unrelated defaults. Some encoder
choices—motion search, multipass decisions, and similar encode-time knobs—are
not stored in a finished bitstream and cannot be reconstructed. mobipeg's codec
defaults fill only those unknowable values. `ffprobe` should be installed next
to mobipeg (as in its normal distribution) or available on `PATH`.

Audio-only previews are not treated as generic archives. BRSTM/BFSTM/BCSTM and
related audio encoding uses mobipeg's Nintendo ADPCM encoder where supported,
with the native encoder retained as a compatibility fallback.

## Selective-rebuild guarantees

- Editing one leaf does not rewrite its siblings.
- A fully unchanged `CREATE` does not rewrite the destination file.
- Removing the cache trades speed for safety; it does not lose source data.
- A changed leaf rebuilds each containing archive on the path to the requested
  output, while unrelated archive branches remain untouched.
- The original media binary is both the fallback and the source of its preset.

## Controls and verification

- `--no-passthrough` disables external companion extraction.
- `--with-mobipeg=PATH` selects a specific mobipeg build.
- `--with-wit=PATH`, `--with-ndstool=PATH`, `--with-ctrtool=PATH`, and the
  equivalent options select other companion programs.
- `--bms=SCRIPT` applies a QuickBMS script to an unsupported container and
  recursively processes its output.
- `--test` prints planned operations without writing output.

Run `tests/regress.sh` for the complete suite. Its incremental-cache regression
checks that an identical second build leaves the destination mtime unchanged,
then verifies that a real edit rebuilds the archive without altering a sibling.
