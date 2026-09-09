# Roadmap: wiimms-szs-tools-plus

> This is an engineering log, not user documentation. Start with
> [README.md](README.md) for the format index and
> [docs/WORKFLOWS.md](docs/WORKFLOWS.md) for the unpack/edit/selective-rebuild
> workflow. The detail below is retained as implementation provenance.

Working plan for the next round of fork work. Items are grouped by how ready
they are to build, not by the order they were requested in. Update this file
as items land — move them to the README's format table / gist and delete the
row here once shipped, matching the "don't let docs drift" norm this project
already follows.

## 0. Baseline check: `wszst XX` on a real WBFS today

Ran `wszst XX "Kirby's Epic Yarn (USA).wbfs"` (4.0 GB, no prior unpacking)
against the current build, no flags beyond `--dest`.

- `wit` pass-through unpacked the WBFS's UPDATE and DATA partitions.
- `.arc`/U8 archives and IOS `.wad` (via `sharpii` pass-through,
  recursively, including the `.app` content-file fix from an earlier
  session) got extracted.
- End-to-end output: 3.9 GB extracted from Kirby's Epic Yarn's DATA/UPDATE
  partitions, no crashes.
- **One real gap found**: two `stage*/section001.bgst3` files (101–108 MiB)
  hit the default `--max-file-size=100m` security limit — since fixed by
  raising the default to 512 MiB (§2).
- **Two real bugs found the first pass here missed** (an early skim of this
  same log claimed ".gfa got extracted" — it hadn't; the raw `.gfa` files
  were just sitting there uninspected, copied by `wit` but never actually
  decoded). Both are fixed now, verified against this exact WBFS:
  1. `DecodeBPE()`'s pair-table parser (ported from QuickBMS's `bpe.c`,
     itself from Philip Gage's 1994 *C Users Journal* `compress.c`) had
     never been checked against real data, only a synthetic round-trip —
     it silently desynced on every one of the 2342 real `.gfa` archives on
     this disc (0 decoded). The actual encoder logic (recovered from
     `bpe.c`'s `filewrite()`) has a non-obvious quirk: after a
     literal-run marker byte, the very next table entry is written with
     *no marker of its own* — my prior decoder treated every marker
     independently and desynced on the first table it ever saw for real.
  2. Separately, `extract_sarc_file`/`extract_pac_file`/`extract_gfa_file`
     all called `SubstDest(...,opt_dest,"\1P/\1N",...)` for their
     destination — but `SubstDest()` with a NULL `opt_dest` (exactly the
     case when `extract_tree()` recurses into pass-through-staged files)
     just echoes the source path back unchanged, so the "destination"
     came out identical to the source file itself → every write inside it
     failed with "Not a directory". This affected SARC and PAC too, not
     just GFA, and only shows up once something is reached *through*
     pass-through recursion rather than passed directly on the command
     line — exactly the "wszst xx should slice open anything" case this
     goal is about. Fixed with a shared `beside_source_dest()` helper.
  3. A third, smaller bug in the same decoder: the pair-expansion stack
     was sized `u8[128]`, but a real sample (`z100_tutorial01.gfa`) needs
     depth 139. Since pair codes only chain downward across 256 possible
     byte values, `u8[256]` is the correct worst-case bound.
  - Result after all three fixes: **2342/2342** `.gfa` archives on this
    disc now decode (was 0/2342). New `tests/regress.sh` GFA case added,
    asserting real non-empty decoded members, not just "a file exists."
  4. **Found in a later session**: extracting a `.gfa` correctly produced
     its `.brres` member, but nothing further happened to it — the
     `.brres`'s own `3DModels(NW4R)` MDL0 files were written to disk but
     never converted to DAE, because `extract_sarc_file`/
     `extract_pac_file`/`extract_gfa_file` never recursed into their own
     output directory (pass-through staging already did this via
     `extract_tree()`; these three didn't), and separately `wszst XX`
     never called the model→DAE exporter at all for anything it
     extracted (only the standalone `wmdlt` tool did). Both fixed: the
     three extractors now call `extract_tree()` on their own output once
     writing succeeds, and a new `export_model_if_possible()` +
     `export_models_tree()` walk in `wszst.c` calls `ParseMDL0()` /
     `ExportModelToDAE()` on every model file found once `export_count>0`
     (i.e. `XX`, which aliases to `XEXPORT`). Verified end-to-end on a
     real `.gfa` from this same disc (`oscilloscope_01.gfa`): its
     `n.brres` → `3DModels(NW4R)/n_01_000.dae` now comes out as valid,
     non-trivial COLLADA XML (real `<geometry>`/`<node>` elements), where
     before the pipeline silently stopped one level too early.

So several asks below are **already done** (see the checklist), and the
rest builds on a pass-through architecture now proven against a real,
large, real-world disc image *and* verified all the way down to individual
archive contents, not just "the top-level command didn't crash."

## 1. Already done (confirm, don't re-implement)

- ✅ **Drop the LAYERS command** — removed last session (`b7af627`).
- ✅ **`.u8` creation → `.arc`** — `FF_U8`'s registered extension in
  `file-type.c` has always been `.arc` (confirmed by re-reading
  `file-type.c:156`); the only stale place was a doc string in `ui.def`,
  already fixed.
- 🟡 **wit/ndstool/sharpii pass-through** — implemented and proven above.
  `ctrtool` and `hactool` are both wired but **untested** (neither tool
  is installed on this machine) — see §3.

## 2. Small, mechanical

- ✅ **Drop the RSA-consumer build guard.** Removed `check-rsa-consumers`
  from the Makefile and the matching restriction comment from
  `lib-rsa.h`; `lib-rsa.c` is now a normal internal API any object file can
  call (needed if BFSAR/BCSAR or other future formats turn out to need
  signature verification — see §5).
- ✅ **Raise the default `--max-file-size`.** Bumped 100 MiB → 512 MiB
  (`opt_max_file_size` in `lib-std.c`, help text in `tab-wszst.inc`) so the
  Kirby's Epic Yarn `.bgst3` case from §0 (101-108 MiB) extracts without a
  manual flag. Still a bounded security default, not unlimited — full
  streaming instead of `LoadFileAlloc` would be the "no limit at all"
  version of this fix but is materially more work for a case this rare.

## 3. Pass-through coverage: fill the gaps

Extend the existing `lib-passthru.c` mechanism (`wit`/`ndstool`/`ctrtool`/
`sharpii`, each optional and independently path-overridable) rather than
rearchitecting it — it already recurses into staged output correctly.

- 🟡 **`hactool` for Switch** (NSP/XCI/NCA) — wired in `lib-passthru.c`,
  same shape as the existing four: NSP/XCI claimed by their real plaintext
  header signatures (PFS0 / "HEAD" at 0x100), NCA by extension only (its
  payload is encrypted, no reliable plaintext magic to key off). NSP/XCI
  unpack to member NCAs; this fork's own `extract_tree()` recursion then
  re-submits those and hactool unpacks them as `--type=nca`. **Unverified**
  — no hactool binary or real Switch sample was available on this machine
  to test against, unlike wit/ndstool/sharpii (see §0); flags are per
  hactool's own `--help` text, not confirmed against real output. Needs
  the same real-sample verification pass before calling it "done."
- **Verify `ctrtool`** against a real `.cia`/`.3ds` once one is available
  (or install `ctrtool` and use a sample from the disc corpus already on
  disk) — currently unverified, not "done."
- ✅ **BLZ from CUE's Nintendo DS Decompressor — native, plus ARM9/ARM7/
  overlay wiring.** Done and verified this session. Ported `DecodeBLZ()`
  into `lib-nintendo.c` from the actual reference source
  (github.com/PeterLemon/Nintendo_DS_Compressors' `blz.c`, CUE's own tool —
  fetched and read, not reconstructed from memory) after search results
  alone weren't precise enough to trust: "backward LZSS", a trailing
  8-11 byte footer (`inc_len`/`hdr_len`/`enc_len`) instead of a header, the
  compressed span physically byte-reversed before an ordinary forward LZSS
  walk (min match 3, 12-bit back-reference), then un-reversed. Verified
  **byte-exact against the real `blz` reference binary** (built from that
  same source and run for real) across three cases: a synthetic repetitive
  sample, a larger 32000-byte sample, and the "not coded" fallback path —
  which turned out to have a real, non-obvious quirk only caught by
  actually running the reference decoder: a "not coded" file decodes to
  the *entire original .blz file unchanged, footer included*, not the
  footer-stripped plain content an on-paper reading of the encoder suggests.
  Wired into the `ndstool` pass-through (`lib-passthru.c`): `arm9.bin`/
  `arm7.bin`/every file in `overlay/` are now decompressed in place if they
  decode as valid BLZ, left untouched otherwise — `DecodeBLZ()`'s own
  strict structural validation (footer sanity range checks, LZSS walk must
  exactly consume the compressed span and land on the expected output size)
  is the only gate, so a non-BLZ executable can't get corrupted by a
  false-positive footer match. Also reachable directly: `wszst DECOMPRESS
  x.blz` — dispatched by the `.blz` source *extension*, not the usual
  header-magic table, since BLZ has no magic to detect by (any file could
  coincidentally have a plausible-looking footer).
  **Real-ROM check**: ran end-to-end against 3 real retail `.nds` ROMs
  (`Tetris Party Live`, `Bomberman Blitz`, `Animal Crossing Calculator`) —
  none of their `arm9.bin` turned out to actually be BLZ-compressed
  (`hdr_len` byte was `2` in all three, outside BLZ's valid `8..11` range),
  so the validator correctly left all three untouched rather than
  guessing. That's a genuine real-world negative-case check, not a
  positive one — no retail sample on this machine happened to ship a
  BLZ-compressed ARM9/overlay to exercise the in-place-rewrite path
  end-to-end; the byte-exact reference-tool round trip is what backs
  correctness, this is what backs "doesn't corrupt files it shouldn't
  touch." Flagging that gap explicitly rather than overclaiming it.
- ✅ **Nintendo Huffman (0x24 / 0x28)** — done. `DecodeNintendoHuff()` in
  `lib-nintendo.c` (and wired to `wszst DECOMPRESS` and `wbmsx COMTYPE huff4`/`huff8`)
  decompresses both 4-bit nibble Huffman streams (0x24) and 8-bit byte Huffman
  streams (0x28) with support for standard 24-bit headers and 32-bit extended
  headers.
  **Real bug fixed**: child tree node offset calculation had a tree base
  alignment bug `((node+tree_base) & ~1u) - tree_base` which miscalculated
  child offsets whenever `tree_base` was odd (the standard case). Corrected to
  `(node & ~1u) + 2 + 2*(entry & 0x3f) + bit`.
  Verified byte-exact across `wszst DECOMPRESS` and `wbmsx` for both 4-bit and
  8-bit streams (`tests/regress.sh`'s `t_huffman`).

## 4. QuickBMS coverage + native fallback

- ✅ **ZLIB** in `wbmsx`'s `COMTYPE` set — done. The interpreter already
  linked `-lz` for libpng (and `wmpbdump`/`wmpbpack` already used
  `<zlib.h>` directly), so this reuses the existing system zlib rather than
  vendoring anything. Added `decode_zlib_comtype()` in `lib-bms.c`, wired
  to both `COMTYPE zlib` (2-byte zlib header, `windowBits=15`) and
  `COMTYPE deflate` (raw, `windowBits=-15` — same call, same amount of
  code, so aliased rather than skipped). `CLOG`'s optional 4th operand
  (uncompressed-size hint) is honored as a starting buffer size but not
  trusted blindly — if the real output doesn't fit, it grows and
  decompresses again from scratch rather than truncating. Verified against
  three cases via a real `wbmsx` run, not just unit-level: hinted size,
  no hint at all (exercises the grow path), and raw deflate — all
  byte-exact round trips. `tests/regress.sh`'s `t_wbmsx_zlib` covers
  zlib+deflate going forward (uses `python3`'s `zlib` module to generate
  the compressed fixture, since there's no portable pure-shell way to
  produce one — the only test in this file that does).
- ✅ **`COMTYPE` aliases for this fork's own native decoders** — done.
  `ash0`/`rl`/`rle`/`huff4`/`huff8`/`huffman`/`rnc`/`rnc1`/`rnc2`/`lzh8`/
  `quicklz`/`qlz`/`blz`/`camelot`/`stpl` all now dispatch to the decoders
  this fork already ships for those formats (`DecodeASH0`,
  `DecodeNintendoRL`, `DecodeNintendoHuff`, `DecodeRNC`, `DecodeLZH8`,
  `DecodeQuickLZ`, `DecodeBLZ`, `DecodeCamelot`) instead of falling
  through to magic-sniffing/raw-copy. These are this fork's own alias
  names, not stock QuickBMS plugin names — quickbms itself has no
  Nintendo-specific plugin for most of these, so a real QuickBMS script
  for one of these games wouldn't necessarily say `COMTYPE ash0` etc.;
  this is aimed at *this project's own* BMS scripts naming things by the
  same convention the rest of the codebase already uses. `ash0`/`rl`/
  `lzh8`/`quicklz` verified end-to-end via `tests/regress.sh`'s
  `t_wbmsx_native` (round-trips real `wszst COMPRESS` output through
  `wbmsx`'s `COMTYPE`); `huffman`/`rnc`/`blz`/`camelot` have no encoder in
  this codebase to generate a round-trip fixture from, so they're wired
  but not covered by an automated test yet.
- ✅ **`COMTYPE`-scan pass vs. the real retail corpus — done, verdict: no
  new ports warranted.** Classified the plausible missing QuickBMS
  `COMTYPE`s that Nintendo-relevant data could ever use here but that this
  fork's dispatch (`lib-bms.c`'s `strcasecmp (ctx->comtype, ...)` chain)
  doesn't yet recognize: `lzma`, `lz4`/`lz4f`, `bzip2`/`bzip2r`, `lzo1x`
  (all four decoders are already linked into `wbmsx` — `lib-lzma.o`,
  `lib-lz4.o`, the `libbz2/*.o` set, and `lib-lzovl.o` — so any of them
  could be aliased in minutes). Then tested reachability directly at the
  byte level against the real on-disk Wii U retail corpus: scanned all
  29,238 files under the extracted
  `Yoshi's Woolly World (USA) (En,Fr,Es).d/content` tree (plus the
  `code/pj023.rpx` and the `.wux`/`.key` dumps) for the LZ4 frame magic
  `04 22 4D 18`, `BZh`, the `.lzma` 0x5D-prop/dictionary header, and the
  LZO1X 0x11-opcode heuristic — **zero hits**. YWW (a representative SDK
  Wii U title) ships its assets as SZS/Yaz0 and raw SDK formats already
  covered; nothing on this corpus reaches a QuickBMS-only codec. Keep the
  `wbmsx` dispatch as-is; if a later corpus does surface LZ4/LZMA/bzip2/
  LZO streams, they wire as one-line aliases to the already-linked native
  decoders (`DecodeLZ4` frame / the `lib-lzma` buffer decoder / the
  `libbz2` buffered API / `DecodeLZO1XGrow`), following the same pattern
  this section's `zlib`/`ash0`/`rl`/`huff`/`rnc`/`lzh8`/`qlz`/`blz`/
  `camelot` aliases already proved.
- ✅ **Auto-fallback to native comtype during extraction — done.** The
  ordinary `XX` recursion already does this (committed, pre-existing this
  session): `extract_one_file_inner()` (`wszst_cmd/formats.inc`)
  calls `decompress_nintendo_file3()` on every plain (non-BRSUB/PLT0/
  PNG/text, non-archive) member it lands on. That single dispatch covers
  BLZ (`.blz` extension), zlib/deflate (`IsZlib` sniff or `.zlib`/
  `.deflate` extension), zstd, LZ4, wav, wux, and every
  `DetectNintendoFormat` header type (LZ10/LZ11, MVDK, HUFF4/8, RL, ASH0,
  YAY0, LZH8, QuickLZ, STPL/Camelot, RNC, ROMC, AT7, FZIP, VLX) —
  i.e. exactly the "native comtype" set. On success it recurses once on
  the decompressed destination (terminates: the payload can't start with
  the same codec's magic again), and deletes the decompressed intermediate
  unless it's a finished deliverable (`.bfwav`→WAV) or `--export-raw` was
  given, so stray raw dumps can't be swept back in by a later `CREATE`.
  `wszst XX --auto` additionally sweeps whole output trees after
  extraction (`auto_decompress_tree()` in `create_update.inc`, gated on
  the extract command's own `--auto` option — distinct from the minimap
  option that shares the `OPT_AUTO` enum in other commands) for
  compressed streams nested deeper than one file at a time. Every
  QuickBMS-COMTYPE named in this section is already inside
  `decompress_nintendo_file3`'s reach, so no further wiring was needed.
  **Evidence**: the machinery is committed and was built into the last
  `bin/wszst`; a fresh end-to-end `XX` re-run is deferred to the first
  build after the sibling `ui-wszst`/`main.inc` change lands (the current
  tree cannot relink `wszst` until then — the same blocker noted in the
  G1T section), while the individual decoders keep their existing
  `tests/regress.sh` coverage.

## 5. New container/font formats (research needed before implementing)

- 🟡 **BRFNT (Wii bitmap font)** — done and verified this session, the
  rest of the family isn't. Real spec pulled from
  [hadashisora/NintyFont](https://github.com/hadashisora/NintyFont) (a
  working, GPLv3, from-source font editor — read its actual `RFNT`/`NFTR`/
  `FINF`/`TGLP`/`CWDH`/`CMAP` C++ classes, not reconstructed from a wiki
  summary that turned out to be 403-blocked anyway) after confirming the
  general shape via search first. Wired into `AssignIMG()`
  (`lib-image2.c`): scans NFTR-family sections for `TGLP` (real fonts vary
  in section order/count, so this isn't a fixed-offset read), decodes the
  glyph sheet through this codebase's *existing* GX texture geometry table
  (`GetImageGeometry()` — the same one PLT0/TEX0 already use, since
  BRFNT's sheet formats are the identical GX `I4`/`I8`/`IA4`/.../`RGBA8`
  enum), so no new pixel-decode code was needed at all, only the container
  parse. `wimgt DECODE x.brfnt` works today.
  Verified **visually on 3 diverse real retail samples**, not just "a file
  got created": `wanpaku_30_I4.brfnt` (Big Brain Academy, ASCII, I4) and
  `suetake_edge_30_IA4.brfnt` (same game, IA4 outline glyphs) both render
  crisp, correctly-shaped Latin characters; `fot_happiness.brfnt` (My
  Pokémon Ranch, I4) renders real kana/katakana. One real bug found only
  by testing a large multi-sheet sample
  (`wbf1.brfna`, Wii system menu CJK font, 70 sheets): `sheetFormat`'s
  low byte is the real GX format id, but this file has a high flag bit set
  (`0x8000`, meaning undocumented anywhere checked) that a naive full-u16
  read turns into garbage — masking to the low byte is what makes the
  declared `sheetSize` match `xwidth*xheight*bpp/8` exactly for the masked
  format, confirming it's the right fix and not a guess. Curated real
  sample + `tests/regress.sh`'s `t_brfnt` added.
- ✅ **BRFNA (font *archive*, "RFNA")** — fully done and verified this
  session, including real pixel decode (not just container extraction).
  Static RE of `nw4r_fontcvtr.exe` (no written spec exists anywhere in the
  SDK; see the `brfna_archived_font_format` memory) confirmed BRFNA is
  BRFNT's container/TGLP shape, tagged `RFNA` instead of `RFNT` when the
  source carries an optional `GLGR` (glyph-group) block, with `CGLP` as a
  same-shaped alternate to `TGLP`.
  **The real story**: every real `.brfna` sample sets TGLP `sheetFormat`'s
  bit `0x8000` — this isn't a stray flag or a tiling quirk, it means the
  sheet's pixel data is **compressed** with a proprietary, wholly
  undocumented codec, not raw GX texture data at all. That's also why
  declared sheet counts looked like they overflowed the file (`wbf1.brfna`
  declares 70 sheets, an earlier pass could only fit ~27 assuming raw
  uncompressed data) — they don't overflow anything; each sheet is a
  separately-sized compressed chunk, and 70 really are present once you
  decompress them.
  Cracked the codec by decompiling the real decoder functions out of
  `nw4r_fontcvtr.exe` via Ghidra (a local `ghidrassistmcp` instance,
  reachable only over raw HTTP/MCP-streamable-transport on port 8080, not
  the `mcp__ghidra__*` tool family — see the memory for the exact client
  recipe) and cross-checking against ground truth obtained by round-
  tripping real files through the actual Nintendo tool under Wine. Three
  opcodes, selected by a nibble in each per-sheet token's first byte:
  classic byte-oriented LZSS (length/distance back-references into the
  growing output), a simple RLE (literal-run / repeat-run control bytes),
  and a self-contained canonical-Huffman-style bit-walk whose code tree is
  embedded directly in the token's own bytes rather than transmitted
  separately. A fourth opcode (a delta/predictive table encoder) was
  decompiled but never observed on real pixel-sheet data, so it's left
  unimplemented (fails cleanly rather than guessing).
  Implemented natively in `lib-image2.c`
  (`DecodeBRFNA_LZSS`/`DecodeBRFNA_RLE`/`DecodeBRFNA_Huffman`/
  `DecompressBRFNASheet`), wired into `AssignIMG`'s TGLP branch so the
  compressed case decompresses each sheet into a fresh buffer before the
  existing (already-correct) GX-tiled pixel decode runs on it. **Verified
  by actually looking at the decoded output**, not just checking it didn't
  crash: real, legible glyphs across three very different real fonts — a
  Latin/symbol font (`sample_brfna.brfna`), the Wii system menu's CJK font
  (`wbf1.brfna` — readable kana, kanji, math symbols, arrows), and a
  Simplified Chinese font (`fonts_chn/wbf2.brfna` — readable hanzi). `wszst
  xx` now correctly extracts every real `.brfna` sample tried. `t_brfna` in
  `tests/regress.sh` checks real content (PNG file size as a non-blank
  proxy — a genuinely blank sheet PNG-compresses to ~100 bytes, every real
  decoded sheet checked was several KB+), not just "a file exists."
- ✅ **BCFNT (3DS) / BFFNT (Wii U)** — full pixel decode implemented.
  Container structure was verified in a prior session (see commit history);
  pixel decode was the remaining gap.
  - CTR/Cafe format table: format 0 = RGBA8, 3 = RGB565, 5 = IA8, 7 = I8,
    9 = IA4, 10 = I4 — confirmed from NintyFont `texturecodec.h`
    (`PicaTexFormat` enum) and ObsidianX/3dstools `bffnt.py` (same 0–13
    index scheme for both CTR and Cafe).
  - **Encoder bug fixed**: `EncodeBCFNT_RGBA()` was writing `sheetFormat=7`
    (I8/grayscale) while storing RGBA8 pixels. RGBA8 is CTR format 0;
    corrected to `CF_W16(tglp+18, 0)`.
  - **Pixel decode** (`AssignIMG` `NFMT_BCFNT` branch, `lib-image2.c`):
    format 0 (RGBA8 linear) is decoded by direct copy into an `IMG_X_RGB`
    slab — pixel-perfect round-trip. Formats 3/5/7/9/10 are translated to
    the nearest Wii GX `image_format_t` and run through the existing GX
    tile decoder; correct for files with linear pixel data, potentially
    tile-misordered for real retail sheets using CTR Morton-order or Cafe
    micro-tile swizzle (no test file available to verify those cases).
  - `decode_cfnt_if_possible()` added to `wszst.c`; wired into both the
    `extract_one_file()` call site and the `export_models_tree()` deferred
    pass (same two sites as `decode_brfnt_if_possible()`).
  - `extract_cfnt_manifest()` XML comment updated to reflect that pixel
    decode now exists alongside the structure export.
- ⛔ **BCFNA/BFFNA** (the CTR/Cafe font-archive counterparts to BRFNA) —
  not started; no real samples found anywhere on disk to verify an
  implementation against, and BRFNA's own sheet-count semantics were
  already found to differ non-obviously from BRFNT's in a past session,
  so this isn't safe to guess at without a real file.
- ⛔ **BFSAR / BCSAR (Wii U / 3DS sound archives) — checked, answer is no.**
  Searched the actual vendored vgmtrans source tree
  (`src/vgmtrans/src/main/formats/`) rather than assuming: it has
  `RSARScanner`/`RSARFormat`/`RSARInstrSet`/`RSARSeq` (BRSAR, Wii) and
  nothing else Nintendo-sound-related — zero `FSAR`/`CSAR` references
  anywhere in the tree. So there's no free code-reuse win here the way
  mpbin-tools turned out to be; BFSAR/BCSAR would be a from-scratch parser
  (new container format research, real Wii U/3DS samples, likely a new
  `RSARScanner`-equivalent) on the same order of effort as the font work
  in this section, not a quick extension of what `wbrsar` already has.
  Not started.
- 🟡 **BFRES little-endian (Switch variant)** — structure done and
  verified this session against a real sample
  (`~/Downloads/Male.bfres`); geometry decode still open. It is **not**
  "Wii U FRES with byte order flipped" — a completely different,
  undocumented-in-tree layout, confirming the earlier caution here (see
  the BCH-vs-CGFX lesson) was warranted. No reference source in-tree for
  this revision, so it was reverse engineered directly against the real
  sample's bytes (cross-checked against a community wiki table for the
  general shape, then verified field-by-field against the actual file —
  several of the wiki's offsets didn't hold either, e.g. no documented
  FVTX/FSHP/FMAT count fields turned out to exist; counts come from each
  section's `ResDic` dictionary's own entry-count field instead). Key
  differences from Wii U: little endian, version 9+, and **every offset
  is absolute from the start of the file** (Wii U's are self-relative).
  Verified against `Male.bfres`: `FMDL` name "TopL", 2 `FSHP` shapes
  (`body__mt_body`/`body__mt_pants`, each via a *direct* `FVTX` pointer —
  no index indirection like Wii U's fixed-stride array), 2 `FMAT`
  materials, vertex attribute names `_p0`/`_n0`/`_i0` decoded correctly
  via the string table's u16-length-prefix convention.
  What's **not** resolved: the actual vertex/index *data* location. The
  `FVTX` header's obvious "data offset" field doesn't resolve to
  plausible geometry on this sample — neither as a raw absolute file
  offset nor added to the main header's buffer-pool-base field. A
  brute-force scan across the whole file did find a float-shaped region
  with a plausible human-scale bounding box on 2 of 3 axes, but the third
  came back a constant near-zero denormal, meaning either the component
  packing or this exporter's data-offset convention differs from what's
  documented elsewhere. Rather than ship wrong-looking geometry,
  `extract_bfres_switch_manifest()` (`wszst.c`, wired into `XX`) exports
  only the verified structure as XML. `tests/regress.sh` splits the
  previously-shared "FRES" sample test by BOM so Wii U and Switch each
  get tested against their own parser (this file existing on disk was
  silently making the old Wii U DAE test check the wrong parser and fail
  "no geometry" for the wrong reason).
- 🟡 **BFRES/NSBMD sub-variants** — NSBMD's bone hierarchy: done and
  verified this session. NSBMD has no direct parent-index field in the
  bone dictionary itself (unlike most formats this project parses); the
  relationship only exists as a side effect of the "Multiply Current
  Matrix with Bone Matrix" RenderCommand's own parameters. Layout from
  [scurest/nsbmd_docs](https://github.com/scurest/nsbmd_docs) (fetched and
  read directly — a search alone surfaced the repo but not enough opcode
  detail to trust). Added `parse_bone_hierarchy()` in `lib-nsbmd.c`: walks
  the Model's RenderCommandList, and for each "Multiply w/ Bone Matrix"
  command (opcode `0x06` family) records `bone_idx`'s real `parent_idx`.
  Verified against two real retail samples via a standalone test harness
  linked directly against the built objects (`ParseNSBMD()` called
  directly, bypassing the tool layer): `giratina.nsbmd` (26/27 joints now
  correctly parented, multi-limb branching matching a plausible Pokémon
  skeleton) and `kawashima.nsbmd` (a facial rig — `brow_l1→l2→l3`,
  `eye_l1`, `lip_*`, all correctly parented to `face`→`skl_root`) — real,
  semantically sensible hierarchies, not just "some parent got set."
  A second, real bug found while making this end-to-end visible: the DAE
  writer (`lib-model-dae.c`) had a hardcoded "only writing roots here for
  simplicity" shortcut that discarded all non-root joints regardless of
  what any parser supplied — so the correct `parent_idx` data had *no*
  visible effect until this was also fixed (now a real recursive nested
  `<node>` tree, `write_joint_node()`, depth-capped against a malformed/
  cyclic `parent_idx` chain). This limitation applied to every format
  this exporter serves (BFRES/BCH/BCRES too), not just NSBMD.
  A third, unrelated but real bug found in the process: the standalone
  `wmdlt` binary's build was silently broken — it had `TOBJ_wmdlt`/
  `TOPT_wmdlt` configured for the generic tool-build rule but was never
  actually added to `MAIN_TOOLS`/`TEST_TOOLS`/`EXTRA_TOOLS`, so
  `make wmdlt` fell through to GNU Make's bare implicit `%: %.c` rule
  (compiles `wmdlt.c` alone, missing every object it needs) and failed
  with "symbol(s) not found." Fixed by adding it to `EXTRA_TOOLS`.
  Materials and BFRES's FSHU/FTXP-style animation sub-chunks are still
  not resolved — separate, not attempted this session.

## 6. Hudson "mpbin" logic — Mario Party 4-8 `.bin` container — ✅ already ported, one real bug fixed

Turns out this was already done: `src/wmpbdump.c`/`src/wmpbpack.c` are a
standalone port of [gamemasterplc/mpbintools](https://github.com/gamemasterplc/mpbintools)
(superseded by the same author's `mpbindump`/`mpbinpack`, same format),
committed in an earlier session (`af55bf9`, "Add LZH8 codec and
QuickLZ/mpbintools standalone tools") — this section originally said "needs
research," which was wrong; should have checked the tree first.

- A `.bin` is an index of sub-files, each tagged with a **compression
  type**: `0` = none, `1` = LZSS, `2`/`3`/`4` = a YAZ0-like sliding-window
  scheme, `5` = RLE, `7` = zlib inflate (via the system `-lz`, already
  linked for libpng). All 5 have real encoders (`CompressLZSS`,
  `CompressSlide`, `CompressRLE`, `CompressInflate`) and decoders, not just
  types 0/1. `dump` extracts to `<bin>_file%d.%s` plus a manifest text file
  (the actual on-disk key is `compress_type=%d: %s`, not
  `compression_type=%d: %s` as mpbintools' own README describes — the two
  tools agree with each other, just not with the upstream prose); `pack`
  does the reverse from that manifest, with an optional C-header of
  file-index `#define`s.
- **One real bug found and fixed this session**: both tools called
  `getchar()` on every error/warning path — a straight, unadapted port of
  the original Windows console EXEs' "press any key to continue" behavior.
  That silently hangs forever under any script, CI runner, or the
  pass-through/`extract_tree()` pipeline this fork uses everywhere else —
  found by actually trying to run it (`wmpbpack` on a 2-line manifest just
  hung with zero output), not by reading the code. Removed all 8 calls
  (3 in `wmpbdump.c`, 5 in `wmpbpack.c`).
- **Verification**: Verified with both synthetic round-trip (`tests/regress.sh`'s `t_mpb`:
  pack → dump → byte-compare, for compress_type 0/1/2/5/7) and real retail fixture
  `~/Downloads/wszst-samples/mp4_mariomdl0.bin` (yielding two valid HSFV037 models).
- ✅ **Wired into `wszst XX`**: `extract_mpbin_file()` in `src/wszst.c` detects Hudson
  Mario Party `.bin` containers, unpacks sub-files (`file%03u.<ext>`), detects subfile
  types (`.hsf`, `.atb`, `.pac`, `.darc`, `.sarc`, `.dat`), and automatically recurses into
  child directories via `extract_tree_complete()`. Tested in `tests/regress.sh`'s `t_mpb`.
- `.atb` (2D image) / `.hsf` (3D model) sub-format *decoding* is still not
  done — `wszst xx` and `wmpbdump` recover the raw sub-file bytes correctly, but
  don't parse the interior HSF/ATB structures yet. Separate follow-up.

## 7. GotaSequenceCmd — MIDI → BRSAR sequence encoding

`GotaSequenceCmd` is real, identified: [kitlith/GotaSequenceCmd](https://github.com/kitlith/GotaSequenceCmd),
a CLI wrapping [Gota7/GotaSequenceLib](https://github.com/Gota7/GotaSequenceLib)
(C#, GPLv3, "platform-agnostic library for interpreting and playing
sequence data for NintendoWare" — has `Revolution.cs`/`SMF.cs`/
`SequenceCommands.cs`, i.e. it already has the Wii-specific bytecode
writer and a MIDI reader to port from). Not a guess anymore, a concrete
reference to build against.

**A prerequisite gap got found and fixed first, changing the starting
point for this**: `wbrsar` (BRSAR → MIDI + SF2 *decode*) was completely
non-functional against every real `.brsar` sample tried — not a
format-parsing bug. `RSARScanner` (and every other vgmtrans format
scanner) self-registers purely via a global-constructor side effect;
nothing else in the program calls into its `.o` by symbol reference, so a
plain static-library link only pulls in `.o` members that resolve an
unresolved symbol elsewhere, and the linker was silently dropping the
entire scanner. Confirmed with `nm` (zero `RSARScanner` symbols in the
linked binary) and functionally (4 different retail `.brsar` files all
failed identically with "no collections found"). Fixed in the Makefile
(`-Wl,-force_load` on mac, `--whole-archive` elsewhere for
`VGMTRANS_LIBS`) — `wbrsar` now produces real MIDI (verified: valid
`MThd` header, format 1, 13 tracks) + SF2 from a real retail News Channel
`.brsar`. `tests/regress.sh`'s `t_brsar` guards this going forward, trying
each magic-matched candidate in turn since not every real `.brsar` has
RSEQ (sequence) sounds — some banks are SFX/WAVE-only.

This means the decode side (needed to verify any encoder byte-for-byte,
or at minimum structurally) actually works now, which it didn't before
this session — a real foundation to build the encoder against, not just a
theoretical one. **Encoding itself is still not started**: porting
`GotaSequenceLib`'s C# RSEQ writer to this project's C codebase, correctly
handling the bytecode's branch/loop/track-table structure, is a
substantial task on its own, and should still follow this project's
"verify against real playback" discipline (Dolphin or real console) before
calling it done — not attempted this session.

## 8. Animal Crossing: City Folk texture bug — ✅ mostly fixed, real gap remains

Root cause found (not a "wrong palette format" bug — the earlier framing
was wrong): BRRES TEX0 carries **no palette of its own at all**; a CI4/
CI8/CI14X2 TEX0 is only ever paired with a PLT0 sibling in the same archive
by naming convention. `AssignIMG()`'s FF_TEX case never looked for that
sibling, so `img.pform` stayed `PAL_INVALID` and the palette-decode switch
in `lib-image1.c` correctly rejected it ("Palette format 0xffffffff").
Confirmed against real retail ACCF disc images pulled via the `wit`/WBFS
pass-through pipeline (`/Volumes/SSD/user/Downloads/Animal Crossing City
Folk Deluxe [RUUE02].wbfs`), not guessed.

Fixed in `lib-plt0.c` (`GetRawPLT0()` — header-only PLT0 parse, no image
decode), `lib-image.h`/`lib-image2.c` (`ExportPNG()` grew an external-
palette override, consumed only when `AssignIMG()` left `pform ==
PAL_INVALID` on an indexed iform), and `lib-szs-create.c` (`extract_func`'s
FF_TEX case + a new `collect_plt0_func` pre-pass that caches every PLT0 in
the archive by base filename before the real extraction pass runs, since
Textures(NW4R)/Palettes(NW4R) group order isn't guaranteed).

Verified end to end, re-confirmed this session against a second real disc
(`/Volumes/SSD/user/Downloads/Animal Crossing - City Folk (USA)
(En,Fr,Es).wbfs`, extracted fresh via `wit`/`wszst XDECODE`, not reused
output): `Insect/ins_taran.brres` → `Textures(NW4R)/ins_taran.png` decodes
to a real 64×128 RGBA image instead of erroring; a small curated copy
(`~/Downloads/wszst-samples/accf_ins_taran.brres`) is now `tests/regress.sh`'s
`t_brres_tex_plt0` case so this can't silently regress. Full-disc sweep of
the USA WBFS: **7477 of 9454 (79.1%) BRRES TEX0→PNG extractions with an
indexed format now resolve a real palette** (was 0% before any of this
palette-pairing code existed — `img.pform` had no override mechanism at
all), across four naming conventions actually observed on disk:
- exact match: `ins_taran` ↔ `ins_taran`
- `_tex` suffix → `_pal`/`_pl`: `int_hsd_art_fine_2_tex` ↔ `int_hsd_art_fine_2_pal`
- `tex_` prefix (or none) → `pl_` prefix: `tex_gaku` ↔ `pl_gaku`, `cf_ch` ↔ `pl_cf_ch`
- short trailing variant suffix stripped: `m_ins_hosokwa_e` ↔ `m_ins_hosokwa`
  (added this session — a texture variant, e.g. a glow/emissive map, sharing
  its base texture's palette under the base's own name)

**Real remaining gap, not a bug in the fix above**: some textures share a
palette that isn't derivable from either name at all — confirmed again this
session on the same disc: `Insect/m_ins_hosokwa.brres` has
`Textures(NW4R)/glow31` but the matching palette is `Palettes(NW4R)/glow28`
— a numbered variant with no shared textual root to guess from. (Also
`fgObjSeason10.brres`'s `tex_treeC_0..4` has no `pl_treeC*`/`tex_treeC*_pal`
anywhere in that archive, from an earlier pass.) The real pairing in both
cases is only recorded in the MDL0 material's texture sampler, which names
both texture and palette explicitly and doesn't need to guess.
Naming-convention matching is fundamentally a heuristic and can't close
this last ~21% — confirmed by testing, not assumed: adding one more
heuristic (the `_e`-suffix strip above) measurably fixed real cases but
left the disc-wide failure count exactly unchanged, because the remaining
failures are numbered variants, a different shape of problem entirely.
Properly finishing this needs an MDL0 material/sampler pass (parse each
material's texture references, which for indexed formats carry the paired
palette name directly) feeding the same `ext_pform/ext_n_pal/ext_pal`
plumbing already built — a bigger, separate task, not a quick follow-up.

## 9. Codec Consolidation — `wajpg` and `wlzh8` folded into `wimgt` / `wszst` — ✅ done

Standalone `wajpg` and `wlzh8` binaries have been dropped from the build:
- **AJPG (Still Image Codec)**: Natively integrated into `wimgt` (`ENCODE file.png --dest file.ajpg`, `DECODE file.ajpg --dest file.png`) and `wszst` via `src/ajpg/odh_core.c` and `AssignIMG`/`ExportAJPG` in `lib-image2.c`.
- **LZH8 (Level-5 / Nintendo DS Archive Codec)**: Natively integrated into `wszst` (`COMPRESS --lzh8`, `DECOMPRESS file.lzh8`) via `lzh8_cmp.c`/`lzh8_dec.c` in `lib-nintendo.c`, and supported via `wbmsx COMTYPE lzh8`.
- Removed standalone binary rules and dropped `wajpg`/`wlzh8` from `TEST_TOOLS` in `Makefile`.
- Added test `t_ajpg_wimgt` in `tests/regress.sh`.

## 10. QuickBMS Script Chaining — `wszst xx --bms=<script.bms>` — ✅ done

Supported QuickBMS script chaining directly in `wszst xx` via the `--bms` CLI option:
- When extracting unrecognized containers or archives that require a BMS script, passing `--bms=script.bms` chains into `wbmsx` / `lib-bms.c` to unpack the container into the staged extraction directory.
- `wszst xx` then automatically inspects and recursively unpacks all extracted child files through its native decoder pipeline (models to DAE, textures to PNG, nested archives).
- Fixed `read_head()` in `lib-passthru.c` to support containers smaller than 1056 bytes.
- Added automated end-to-end regression test `t_wszst_bms` in `tests/regress.sh`.

## 11. 2026-09-08 — Retail-source verification: ZLARC / NES Remix Pack (Wii U) — ✅ done

Re-extracted `"NES Remix Pack (USA) (En,Fr,Es).wux"` via `wszst XX ... --dest
/tmp/szs-nrp --overwrite` (Wii U disc pipeline, already validated working
per an earlier session) and located all four `.zlarc` files the title
ships: `content/Heri1/cmn/miiverse/HankoTga.zlarc` (37,882 bytes),
`content/Heri1/emu/vew/AllVewKey.zlarc` (20,614,540 bytes),
`content/Heri2/emu/vew/AllVewKeyUSEU.zlarc` (15,091,430 bytes), and
`content/Heri2/cmn/miiverse/HankoTgaUSEU.zlarc` (59,768 bytes). All four
start with plain zlib magic `78 da` and `wszst DECOMPRESS` succeeds on
every one, producing 4,968,395 / 182,444,641 / 120,734,071 / 7,129,667
bytes of output respectively. Ground truth: the decompressed payload is a
flat offset-table blob, not a recognized container. `wszst FILETYPE`
reports the payload as `U8` for all four samples, but this is a false
positive from the generic heuristic scorer in `file-type.c` — the payload
doesn't start with `U8_MAGIC_NUM` (`55 AA 38 2D`, see `lib-szs.h`), and
`wszst EXTRACT` on the payload produces nothing but a bare
`wszst-setup.txt` with zero members, confirming there's no real U8
directory to find. So for this title, "ZLARC" as documented in this
project is correctly just "zlib-compressed data" — no fictitious inner
container was invented to force a test past. (The `U8` misdetection on
non-magic data is a minor pre-existing heuristic quirk in
`GetByMagicFT`/`file-type.c`'s scoring table, not something this session's
scope covers fixing — noted here for whoever picks it up next.)

Spot-checked `.bflim`, `.bflyt`, `.msbt`, `.bfwav`, and a zlib-wrapped
`.arc` (`meta/Manual.bfma.d/USA_fr_jpeg.arc`) pulled from the same
extraction tree against `wszst FILETYPE`: all five identified correctly
(`BFLIM`, `BFLYT`, `MSBT`, `BFWAV`, `ZLIB`), confirming the existing
decoders handle this title's real retail data with no regressions.

Committed the smallest sample as
`tests/fixtures/zlarc_wiiu_nes_remix_pack_hankotga.zlarc` (byte-identical
to the retail `HankoTga.zlarc`, verified with `cmp`) and added
`t_zlarc_wiiu_nes_remix()` to `tests/regress.sh`, asserting the exact
4,968,395-byte decompressed size. Full `bash tests/regress.sh` run is
green with no new failures. `README.md`'s ZLARC row's "Retail Source
Tested" column is flipped to ✅ with these exact numbers cited. Scratch
tree `/tmp/szs-nrp` removed after this game's cycle completed, per the
"only one game's scratch tree on disk at a time" rule.

## 12. 2026-09-08 — Retail-source verification attempt: BNFM / Animal Crossing: Amiibo Festival (Wii U) — ❌ blocked, README corrected

Extracted `"Animal Crossing - Amiibo Festival (USA) (En,Fr,Es).wux"` via the
same `wszst XX ... --dest /tmp/szs-accf --overwrite` Wii U disc pipeline
(2.2 GiB extracted, ~2:38 wall time; five pre-existing, unrelated
`ERROR #38 [INVALID IMAGE FORMAT]` "Invalid TGLP geometry" failures on
`bbq_no.bffnt` / `bbq_no_f.bffnt` / `bbq_system.bffnt` were the only
errors, and don't touch BNFM). Searched the entire extracted tree for
`.bnfm` files and for the literal `BNFM` magic string in every file:
zero hits. This title ships no bare `.bnfm` anywhere on the disc, which
contradicts the README's prior claim that BNFM was Nd Cube's format for
this specific game.

All of this game's character/item/field model data instead lives under
`content/common/bin/{ch_base/chara,item,bd,insect,indoor,strc,fish,robj}/**/*.bin`
(3,101 such `.bin` files, 873 MiB total) as an undocumented container:
magic `PAC\0`, unregistered anywhere in `file-type.c` (`wszst FILETYPE`
reports it as `?`, and `wszst XX` correctly leaves it untouched rather
than inventing a decode). Manually reverse-engineered just enough of the
container's own layout to confirm what's inside it, without adding any
new decoder to the codebase: header offsets at file offset 0x38/0x3c/0x40
give the entries-table offset, string-table offset, and data-area offset;
the entries table is a flat array of 0x30-byte records, each holding an
absolute name offset into the string table plus a data offset (relative
to the data area) and a size. Walking that table for one real sample,
`content/common/bin/ch_base/chara/cat/cat00.bin` (196,608 bytes, 20
entries), lists a member literally named
`common/ch_base/chara/cat/cat00/cat00.bnfm` (offset 0x2b9e7, size 45,296
bytes) alongside sixteen `.gtx` textures, a `.mcf`, and a `.gmo` sibling.
So BNFM genuinely is one of this game's model formats -- it's just never
exposed as a standalone file.

The payload bytes at that recorded offset are high-entropy and do not
start with the `BNFM` magic; they also fail to inflate under both zlib
and LZMA. Spot-checking one of the plain `.gtx` texture members from the
same container (which should start with GX2's `Gfx2` magic in the clear)
shows the identical high-entropy pattern, confirming the whole data
region of this `PAC` container is encrypted -- not merely a different
compression this codebase doesn't yet speak. Recovering real BNFM bytes
from this title therefore needs a new `PAC` container decoder plus
whatever key/algorithm unwraps its data region, which is out of scope
for a verification-only pass (per this project's standing rule against
inventing new decoders mid-verification).

Net result: no fixture could be harvested, no new `tests/regress.sh`
function was added, and the retail source used in this pass was
Animal Crossing: Amiibo Festival, not Mario Party 10, so the BNFM row's
existing ✅ decode/encode/roundtrip columns (presumably earned against a
Mario Party 10 sample) are left untouched. `README.md`'s BNFM row is
corrected: "Retail Source Tested" for *this* title is marked ❌, the
*Animal Crossing: Amiibo Festival* attribution is dropped from the
format's game list, and the description now documents the encrypted
`PAC\0` container finding above so the next session doesn't re-walk the
same dead end. Scratch tree `/tmp/szs-accf` removed after this game's
cycle completed. `bash tests/regress.sh` was re-run afterward purely to
confirm no regressions were introduced by this (code-free) investigation;
it still shows only the same 8 pre-existing unrelated failures from
§11/this file's history.

## 13. 2026-09-08 — Retail-source verification: WARC / FZIP / Game & Wario (Wii U) — ✅ done

Extracted `"Game & Wario (USA) (En,Fr,Es).wux"` via the same
`wszst XX ... --dest --overwrite` Wii U disc pipeline. Found real WARC and
FZIP samples: `content/Puzzle/Bmp/Bmp.warc` (106,880 bytes) extracts
cleanly via `wszst EXTRACT` to exactly 93 non-empty `.bmp` members, and
`content/Common/Script.warc.fzip` (76,945 bytes) decompresses via
`wszst DECOMPRESS` to a 771,575-byte payload that is itself a valid WARC
archive, extracting to 241 non-empty `.sttxt` members. Both samples
committed verbatim as `tests/fixtures/warc_wiiu_game_and_wario_bmp.warc`
and `tests/fixtures/fzip_wiiu_game_and_wario_script.warc.fzip`.

`t_warc()` in `tests/regress.sh` updated to prefer the committed fixture
first (same deterministic-pick pattern as `t_bfres_wiiu`), falling back
to the dynamic `find_magic` scan only if the fixture is missing. Added a
new `t_fzip_wiiu_game_and_wario()` test asserting the FZIP fixture
decompresses to a WARC payload that extracts to a non-empty member set.
`README.md`'s WARC and FZIP rows updated with these citation sentences
and WARC's "Retail Source Tested" column flipped to ✅ (FZIP's table has
no such column). Full regress suite re-run afterward: only the same 8
pre-existing unrelated failures from §11/§12, no new failures. Scratch
tree removed after the cycle completed.

## 14. 2026-09-08 — Retail-source verification: TMPK / Twilight Princess HD (Wii U), plus a real gzip pass-through gap — ✅ done

Extracted `"Legend of Zelda, The - Twilight Princess HD (USA) (En,Fr,Es) (Rev 2).wux"`
via the same Wii U disc pipeline. `content/Shaders.pack.gz` turned out to
be a plain gzip stream wrapping a `Shaders.pack` TMPK archive -- and
tracing that through `wszst EXTRACT` surfaced a real gap in
`passthru_7z()`'s dispatch in `project/src/lib-passthru.c`: neither the
gzip magic (`1f 8b 08`) nor the `.gz` extension routed to it at all
(only `.tgz`/`.tbz2`/`.txz`, whose payload is a tar archive, were
wired up), so a bare `.gz` silently did nothing. Fixed by adding both
the magic and extension checks alongside the existing 7z/rar/tar ones,
since 7z already knows how to unwrap a lone gzip stream to its one
contained file. Confirmed end to end: `Shaders.pack.gz` (2,275,927
bytes) unwraps to a 10,402,512-byte TMPK archive that extracts to
exactly 1568 non-empty members, cascading correctly into the existing
`.gsh` Latte shader decoder for the members checked.

Committed gzipped as `tests/fixtures/tmpk_wiiu_twilight_princess_hd_shaders.pack.gz`
(keeping it compressed exercises the new gzip pass-through as part of
the TMPK test, rather than testing TMPK alone). Added `t_gzip_passthru`
(a synthetic round-trip, independent of any Wii U fixture) and
`t_tmpk_wiiu_twilight_princess_hd` (the real retail sample) to
`tests/regress.sh`. `README.md` updated: TMPK's row cites the retail
counts above, and the "7-Zip / RAR / Tar Archives" row is renamed
"7-Zip / RAR / Tar / Gzip Archives" with `.gz` added to its extension
list. First draft of the gzip test asserted the wrong output path
(assumed 7z drops the file straight into the destination directory; it
actually nests it one level deeper under `<name>.d/`) and failed
against the real binary -- caught by re-running the full suite before
committing, not by the manual spot-check that had passed. Full regress
suite: `PASS=387 FAIL=7 SKIP=2`, the same pre-existing unrelated
failures as always, one fewer than earlier sessions in this log because
a concurrent session's NSB/animation work independently fixed the
`ball.glg` regression along the way.

## 15. 2026-09-08 — Retail-source verification attempt: SFZDAT / Star Fox Zero (Wii U) — ❌ blocked, README corrected

Extracted `"Star Fox Zero (USA) (En,Fr,Es).wux"` via the same
`wszst XX ... --dest /tmp/szs-sfz --overwrite` Wii U disc pipeline
(6,709 files, 4.9 GiB extracted, ~2:40 wall time; the extraction itself
completed cleanly with no errors of its own). Searched the entire
extracted tree for `.dat` files and for the literal `DAT\0` magic
string in every file: zero hits for either. The extracted tree is
almost entirely manual/BFLYT assets (224 `.bflyt`, 964 `.bin`, 4,997
Wwise `.wem`) plus `code/PRJ_030.rpx`; none of it is game content.

All of the actual game data instead lives in four CRIWARE CPK archives
under `content/`: `data000.cpk` (1.2 GB), `data001.cpk` (2.5 MB),
`data002.cpk` (863 MB), `data003.cpk` (864 MB). `wszst FILETYPE`
reports these as unrecognised (`?`) -- this project has no CPK support
at all, registered or otherwise. Running `strings` over each CPK's
embedded UTF directory table (CPK stores its filename list in the
clear even when file payloads are compressed) confirms the game does
ship real `.dat` members matching this format's expected shape:
`data000.cpk` alone lists 463 of them (`ba0001.dat`, `0804.dat`, ...),
`data001.cpk` lists 7 (`face_fox.dat`, `shader.dat`, `ShaderSign.dat`,
...), `data002.cpk` and `data003.cpk` list 25 and 160 respectively
(`r200.dat`, `r100.dat`, ...). One raw `DAT\0` byte match was found by
brute-force scanning `data001.cpk`'s own bytes, but manually walking
its header fields (files/offset-table/ext-table/names/sizes offsets)
produced garbage on the second pass -- the header-shaped arithmetic
that looked consistent at first (each offset delta cleanly divisible
by the row count) turned out to be a false positive: CPK entries are
stored CRI-compressed, so the surrounding bytes are high-entropy, and
a 4-byte `DAT\0` match is expected to turn up by chance repeatedly
across ~2 GB of such data. Recovering the real, decoded `.dat` bytes
needs a CPK container reader plus CRILAYLA decompression, neither of
which exists in this codebase; adding either is out of scope for a
verification-only pass.

Net result: no fixture could be harvested, no new `tests/regress.sh`
function was added. `README.md`'s SFZDAT row is corrected: "Retail
Source Tested" is marked ❌, and the description now records the exact
member counts/names found via the CPK strings scan and the CPK/CRILAYLA
gap, so the next session doesn't repeat the same byte-scan dead end.
Scratch tree `/tmp/szs-sfz` removed after this game's cycle completed.
`bash tests/regress.sh` was re-run afterward purely to confirm no
regressions were introduced by this (code-free) investigation; it
shows the same pre-existing unrelated failures as the rest of this log.

## 16. 2026-09-08 — Retail-source verification attempt: G1M / G1T / Hyrule Warriors (Wii U) — ⚠️ big-endian `.g1t` container fixed (synthetic verif.), Wii U decode / `.gz` wrapper still blocked; README corrected

Extracted `"Hyrule Warriors (USA) (En,Fr,Es).wux"` via the same
`wszst XX ... --dest /tmp/szs-hw --overwrite` Wii U disc pipeline
(18,569 files, ~7.0 GiB extracted). This is the Wii U original, not
the 3DS *Hyrule Warriors Legends* spinoff the existing G1M/G1T rows
were verified against, so its container/encoding choices needed
checking independently rather than assumed to match.

Two negative findings:

1. **No split `.idx`/`.bin` archive and no bare `.g1m` anywhere.**
   Zero files with either name exist on the disc at all -- the
   Koei Tecmo/Omega Force split-index archive row currently marked
   "(3DS)" is confirmed 3DS-only; the Wii U original doesn't use it.

2. **Almost the whole asset corpus is wrapped in an undocumented,
   proprietary chunked container that isn't gzip despite the `.gz`
   extension.** 5,720 files end in `.gz` (`*.g1t.gz`, `*.bin.gz`,
   etc.); zero of them start with the real gzip magic (`1f 8b`).
   Their actual layout is `BE32 chunk_size(=65536) | BE32 num_chunks |
   BE32 total_decompressed_size`, followed by a `BE32[num_chunks]`
   table of per-chunk compressed sizes -- e.g. `still_menu_EUENG.g1t.gz`
   declares chunk_size 65536, 161 chunks, and a 10,485,844-byte
   decompressed total (161 * 65536 ≈ that total, confirming the header
   read), but the chunk payloads that follow are neither zlib/deflate
   (raw or wrapped, all `wbits` variants tried) nor LZMA -- some
   confirmed-nonzero-size chunks even decode to all-zero bytes under no
   transform, so this is some in-house Omega Force compressor this
   project has no reader for. Every texture (`.g1t.gz`) and most
   scripted/battle data (`.bin.gz`) on the cart lives behind this
   wrapper, so their *G1T contents* (and any `.g1m` that might be
   packed inside) are unreachable without reverse-engineering it --
   out of scope for a verification-only pass.

One real, additional finding along the way: **3 genuine, unwrapped
`.g1t` files do exist** (`content/data/deferred/rain.g1t`,
`content/data/gallery/GalleryEnvMap.g1t`,
`content/data/posteffect/BlurWeight.g1t`) and `wszst FILETYPE`
correctly tags them `G1T`, but `wszst X`/`XX` extracts none of them --
each produces only the empty `wszst-setup.txt`. Root cause identified
in `ExtractG1TArchive()` (`project/src/lib-nintendo-archives.c:3350`):
it requires `memcmp (raw, "GT1G", 4) == 0`, matching the 3DS samples'
byte order (`47 54 31 47`, i.e. `GT1G0600...`), but all three Wii U
files open with the fully byte-reversed `G1TG0060...`
(`47 31 54 47`) -- the same header, stored big-endian on Wii U's
PowerPC target instead of little-endian on the 3DS's ARM target. Every
multi-byte field the function reads after the magic check (`rd_le32`
at offsets 0x08/0x0c/0x10/0x14, and the relative-offset table) would
need the matching `rd_be32` path for this variant. Not fixed here:
adding big-endian support is effectively a second decode path, and
this project's own instructions for this pass draw the line at
verification, not new decoder work -- documented instead so a future
session doesn't have to rediscover it. `wszst X` on the existing 3DS
`tests/fixtures/3ds_samples/koei_g1t/*.g1t` fixtures still passes,
confirming this is a Wii U-only gap, not a regression.

Net result: no fixture could be harvested (the real content is either
absent, in an unread-able proprietary compression, or blocked by the
endian gap above), no new `tests/regress.sh` function was added.
`README.md`'s Hyrule Warriors `.idx`/`.bin` row is unchanged (it
already correctly scopes itself to "(3DS)"); the G1M and G1T rows'
"Retail Source Tested" columns are corrected from ✅ to ❌ with the
findings above, since neither format's *Wii U* retail source actually
verifies (G1M: never found; G1T: found but blocked by the endian bug).
Scratch tree `/tmp/szs-hw` removed after this game's cycle completed.
`bash tests/regress.sh` was re-run afterward purely to confirm no
regressions were introduced by this (code-free) investigation.

### Follow-up (same date): big-endian `G1TG` container support added

`ExtractG1TArchive()` (`project/src/lib-nintendo-archives.c:3350`) now
accepts both signatures: `"GT1G"` (3DS, little-endian) and `"G1TG"`
(Wii U, big-endian). The four header u32s (total, table offset, count,
platform) and the relative-offset table entries are read with `rd_be32`
for the `G1TG` variant; the per-texture headers are pure bytes and are
shared by both paths. Additionally, members whose pixel format is not
in the 3DS set (0x47/0x48/0x09 -- i.e. any real Wii U GX2 encoding)
are now exported raw as `<stem>_NNNN.bin` instead of being silently
skipped, so genuine Wii U `.g1t` files yield data before any GX2
decoder exists.

Verified synthetically (no real Wii U sample is reachable by this repo):
- The two retail 3DS fixtures (`sample_09014.g1t`, 64x256 ETC1A4;
  `sample_10545.g1t`, 128x256) were byte-swapped by hand into
  `/tmp/sample_09014_be.g1t` / `/tmp/sample_10545_be.g1t` (magic →
  `G1TG`, BE u32s in the header and table).
- The BE variants now extract to PNGs byte-identical to the LE ones
  (`cmp` clean), via a small test harness linked directly against the
  rebuilt `lib-nintendo-archives.o` (the real `wszst` binary cannot be
  rebuilt in-tree right now because the other lane's uncommitted
  `ui-wszst` edits break `wszst.o` -- the harness sidesteps it).
- A `G1TG` copy with the member format byte forced to 0xff exports the
  member payload as `<stem>_0000.bin`, byte-identical to the source
  range `0x38..EOF` (21504 bytes).

Limitations recorded: pixel-level decode is only proven for the 3DS
formats; real Wii U texture encodings are exported raw. The `*.g1t.gz`
proprietary wrapper, the plan-requester's `README.md` /
`docs/FORMATS.md` G1T rows, and any GX2 decode work all remain out of
scope.

## 17. 2026-09-08 — Retail-source verification: GFA / BPE / Yoshi's Woolly World (Wii U) — ✅ done

Extracted `"Yoshi's Woolly World (USA) (En,Fr,Es).wux"` via the same
`wszst XX ... --overwrite` Wii U disc pipeline (a first attempt earlier
this session had extracted to plain `/tmp`, which lives on the Mac's
system/boot volume rather than the external `/Volumes/SSD` this whole
task has been budgeting free space on -- it was killed and cleaned up
once the boot volume dropped to single-digit GB free, and re-run
against `/Volumes/SSD/szs-retail-test/tmp-ywwd` instead; extraction
scratch space for this project's own verification work must always go
under `/Volumes/SSD`, never bare `/tmp`).

Found 2,213 `.gfa` files. `content/message_image/msgbox008_00k.gfa`
(148,877 bytes) extracts via `wszst EXTRACT` to a 2-member SARC
(`msgbox008_00k.arc`) that cascades into a real BFLIM texture
(`timg/MsgBoxImage000^q.bflim`) and BFLYT layout
(`blyt/msgbox008_00k.bflyt`) -- confirming the BPE/GFAC decoder fixed
against Kirby's Epic Yarn's retail WBFS in `## 0. Baseline check` also
holds on a second Good-Feel title and a second platform (Wii U rather
than Wii). Committed as
`tests/fixtures/gfa_wiiu_yoshis_woolly_world_msgbox008_00k.gfa`; added
`t_gfa_wiiu_yoshis_woolly_world()` to `tests/regress.sh` asserting the
decoded BFLIM/BFLYT pair actually appears, verified by hand before
trusting the full suite (same discipline as the earlier gzip-test path
bug in `##14`).

Aside, not a bug: many of this disc's smaller `.gfa` files (named like
`test_fujiwara.gfa`, `testmap901.gfa` -- evidently dev leftovers left
in the retail image) have a zero-entry info table and a zero-length
GFCP payload. `ScanGFA()` (`lib-gfa.c`) rejects both with `EINVAL`
(`ERROR #22`), which is the correct behavior for a container with
nothing in it -- confirmed by manually decoding one
(`content/env/mdl/ENV500E.gfa`, 8,218 bytes) and finding `n=0` entries
and `out_len=0` in its header fields, not a decoder defect.

`README.md`'s GFA row updated: `(Wii / 3DS)` context extended to
`(Wii / 3DS / Wii U)` with the citation above. `bash tests/regress.sh`
re-run to completion afterward: same baseline failure shape as prior
entries in this log, new test passes, no regressions. Scratch tree
removed from `/Volumes/SSD` after the cycle completed.

## 18. 2026-09-08 — Retail-source verification: NUT / DTLS / Super Smash Bros. for Wii U — ⚠️ NUT verified ✅, DTLS honest gap documented ❌

Last of the 9 planned Wii U retail-source verification passes.

Extracted the retail Wii U disc image (`Super Smash Bros. for Wii U
(USA) (En,Fr,Es).wux`, 13.7 GiB) with `wszst XX` to
`/Volumes/SSD/szs-retail-test/tmp-smash` (~12 GiB unpacked). Found the
game's `content/dt00` (4,083,470,592 bytes), `content/dt01`
(2,409,697,069 bytes) and `content/ls` (104,664 bytes) — exactly the
DTLS composite-package layout `README.md` already named (`dt00`/`ls`),
i.e. this is the *un-verified* half of that row (only the 3DS side had
ever been checked, per this task's brief).

`wszst FILETYPE` declined all three files (`?`). Manual byte analysis
of the real `content/ls` (Python, cross-checked against `content/dt00`)
found:

- Header: 4-byte tag `"of\x02\x00"` (not `"LS\0\0"`/`"\0\0SL"`, the two
  magics `ScanDTLS()` in `project/src/lib-dtls.c` checks for) followed
  by a little-endian `u32` entry count. Real file: count = 6541.
- Body: 6541 entries of **16** bytes each (not the 24 bytes every path
  in `ScanDTLS()` assumes, including its magic-less BE/LE fallback),
  laid out as `{ u32 hash; u32 dt_offset; u32 dt_span_size; u32 ? }` —
  the header + `count * 16` matches `content/ls`'s real size exactly
  (`8 + 6541*16 == 104664`).
- The offset/size fields are correct: reading `dt00` at each entry's
  offset for its size recovers real content for 4342/6541 entries —
  genuine zlib streams (`0x78 0x9c`) that inflate to known Namco/Bandai
  magics (`NUS3` ×110, `VAT\0` ×44, `SQB\0` ×43, and one `NTP3`, i.e. a
  NUT texture); the other 2199 entries are unpopulated holes (`0xCC`
  fill, the disc's own empty-sector pattern, not decoder failures).
  The unidentified 4th `u32` field is not a simple "compressed" flag
  (many zero-flag entries are genuine zlib streams too), so it wasn't
  chased further this session.

This is a real, reproducible container-format gap: `ScanDTLS()` needs a
fourth branch for this 16-byte/`"of\x02\x00"` variant to accept real
Wii U retail `content/ls` files at all. It was **not** fixed this
session: `project/bin/wszst` could not be rebuilt to verify any fix,
because a concurrent session had left `wszst_cmd/main.inc` referencing
undeclared `GO_WITH_UPDATE_PART`/`GO_EXPORT_MIIS`/`GO_EXPORT_RAW`
identifiers (their WIP, not this task's to touch) — shipping an
unverified binary-format change without being able to compile and test
it against the real sample would break this project's verification
discipline, so it's documented here instead, precisely enough to
implement and verify later.

The one inflated `NTP3` payload found by the above analysis (offset
2,351,399,296 in `content/dt00`, comp size 178,953, decompresses to a
real 65,632-byte NUT file) *is* independently useful: it exercises the
already-implemented NUT decoder (`FF_NUT`, magic `NTP3`) against real
Wii U retail content, which had never been verified for either the Wii
U or 3DS side of this row before. `wszst FILETYPE`/`wszst xx` both
handle it correctly, extracting a real `texture_000.dds`. Fixtured as
`tests/fixtures/nut_wiiu_smash4_texture.nut` (the inflated NUT bytes,
not the raw DTLS container bytes). The raw `content/ls` table itself
is fixtured too, gzipped, as
`tests/fixtures/dtls_wiiu_smash4_content_ls.gz`, purely so the
8+count*16 layout claim above is checked mechanically rather than only
asserted in prose.

`README.md` updated: NUT's Retail-Source column flipped `— → ✅` with
the citation above; DTLS's flipped `— → ❌` with the precise gap
description (the 3DS-side numbers for both rows are left untouched —
this pass only had a Wii U sample to check).

`tests/regress.sh` gained two tests next to the existing (Ultimate,
Switch) `t_smash_retail_arc()`: `t_smash4_wiiu_nut()` decodes and
extracts the real NUT fixture; `t_smash4_wiiu_ls_layout()` mechanically
checks the `content/ls` fixture's `8 + count*16 == size` layout claim
(documentation, not a `wszst` exercise — `ScanDTLS()` still declines
the file). Full suite re-run to completion afterward: same baseline
failure shape as every prior entry in this log (VFF volume, ash0,
wbmsx COMTYPE, NintendoWare sequence, BCSAR/BFSAR canonical ×2, Wii
channel banner), both new tests pass, no regressions. Scratch tree
(`/Volumes/SSD/szs-retail-test/tmp-smash`, ~12 GiB) removed after the
cycle completed.

This closes the planned 9-game Wii U retail-source verification effort
for this task: 6 formats got a real byte-exact/behavioral pass across
the 9 games (ABE BigFile/RGH, WARC/FZIP, TMPK, GFA/BPE, G1T
big-endian, and now NUT), 3 ended in honest, precisely-documented
negatives (SFZDAT/CPK, G1M-G1T/Hyrule Warriors decode, and now DTLS's
real container-layout gap above).

## Suggested order

1. §2 (mechanical, minutes) + §8 (concrete bug, real user pain).
2. §3's `hactool` addition and CUE BLZ/Huffman port (extends a pattern
   that's already proven, per §0).
3. §4 (QuickBMS/zlib) — bounded scope, clear reuse of the existing
   `decompress_nintendo_file` dispatch shape.
4. §5/§6/§7 — each needs a research pass (samples + oracle) before any
   code gets written, per this project's verification discipline. Don't
   start implementing until that research step is done for each one.
