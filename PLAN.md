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
- **Port more QuickBMS `COMTYPE`s** from aluigi's public QuickBMS source
  where a Nintendo-relevant format is missing and this fork has no native
  decoder to alias — needs a pass over `quickbms.c`'s `comtype_scan`
  table to see which are actually reachable from real Nintendo-game
  samples on disk versus generic/unrelated formats not worth the port.
- **Auto-fallback to native comtype during extraction.** When `wszst XX`
  recurses into an extracted tree and finds a file whose header matches a
  `COMTYPE` this project natively decodes (not just via the standalone
  `wbmsx` script path), decompress it automatically as part of the normal
  extraction chain — same shape as `decompress_nintendo_file` already does
  for LZ10/LZ11/RNC/etc, just extended to cover whatever QuickBMS-only
  comtypes get natively ported per the item above.

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

## Suggested order

1. §2 (mechanical, minutes) + §8 (concrete bug, real user pain).
2. §3's `hactool` addition and CUE BLZ/Huffman port (extends a pattern
   that's already proven, per §0).
3. §4 (QuickBMS/zlib) — bounded scope, clear reuse of the existing
   `decompress_nintendo_file` dispatch shape.
4. §5/§6/§7 — each needs a research pass (samples + oracle) before any
   code gets written, per this project's verification discipline. Don't
   start implementing until that research step is done for each one.
