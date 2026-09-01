# Wiimms SZS Tools

    *********************************
    *    _______ _______ _______    *
    *   |  ___  |____   |  ___  |   *
    *   | |   |_|    / /| |   |_|   *
    *   | |_____    / / | |_____    *
    *   |_____  |  / /  |_____  |   *
    *    _    | | / /    _    | |   *
    *   | |___| |/ /____| |___| |   *
    *   |_______|_______|_______|   *
    *                               *
    *       Wiimms SZS Tools        *
    *     https://szs.wiimm.de/     *
    *                               *
    *********************************
 
»Wiimms SZS Tools« is a set of command line tools to extract, modify and create
different files of game *Mario Kart Wii* and of other Nintendo games.
Development started in 2011. See https://szs.wiimm.de/ for more details,
documentation and downloads.

<dl>
<dt>Note:</dt>
<dd>
This is only a copy of Wiimms private SVN repository.
Only official releases are exported to <i>GitHub</i>.
Therefor merge requests can not imported directly and must be included manually.
</dd>

<dt>License:</dt>
<dd>
This program is free software;
you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation;
either version 2 of the License, or (at your option) any later version.

See file project/gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt for details.
</dd>
</dl>

*Wiimm, 2020-08-22*

---

## Fork: native Nintendo/Brawl format support

This fork adds native (no external binaries, except where noted) support for
a range of Wii/DS/3DS/Wii U/Switch container, texture, model and crypto
formats beyond what upstream Wiimm's SZS Tools ships, plus a few tools
(`wajpg`, `wlzh8`, `wbmsx`, `wwc24crypt`, `wlayt`, `wmdlt`, `wbrsar`) folded
into `wszst`/`wimgt`. Builds with the normal `cd project && make all -j4` and
is exercised by `tests/regress.sh`.

For the normal unpack, edit, and selective recursive rebuild workflow, see
**[docs/WORKFLOWS.md](docs/WORKFLOWS.md)**. That guide is the canonical home
for cache behavior, companion-tool setup, and source-preset-preserving mobipeg
re-encoding. The table below remains the canonical format capability index.

Regression results have distinct meanings. Ordinary `PASS` entries may be
decoded-data, semantic, structural, or explicitly byte-exact checks as stated
in their labels. The separate `BYTE`/`BFAIL` totals are stricter canonical
encoder-determinism checks: identical logical input (including the same
resource basename) is encoded twice and the complete output files must match.
The separate `FIXED`/`FFAIL` totals are stricter again: a canonical file is
encoded, decoded through its public interchange representation, and re-encoded,
and both complete binary generations must match. This currently covers 176
image, Message Studio, layout, model, audio bank, archive, compression, and disc-image
paths. These checks do not imply that rebuilding an arbitrary retail file keeps
its original padding, ordering, compression choices, or unknown fields.
The deterministic fixture run currently exercises 184 byte-equality checks
covering compression streams; flat and hierarchical archives; Nintendo
textures, fonts, layouts, messages, instrument banks (RBNK) and sequences; BRSAR/BCSAR/BFSAR/BRSTM/BFSTM/BCSTM;
HSF, HSD, MOD, MSH, MDL0, BCH, NSBMD and both Wii U/Switch BFRES paths; KMP course data; KCL collision
meshes; the complete GX/GTX format/tile/mip/array/MSAA encoder matrices; and GSH program assembly. Retail
decode→encode identity is separately asserted where the textual form is
designed to retain every source field (currently conditional NCER, NANR,
BRLAN and BRLYT tests, plus GSH program bytes). Normalizing/lossy conversions
such as arbitrary retail HSF→DAE→HSF, palette normalization, image codec
transcoding, and MIDI conversion are deliberately tested semantically rather
than mislabeled byte-preserving.

Full verification detail — what each format was checked against, which real
samples were used, and what remains unverified — is in a
[companion gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
rather than duplicated here.

### New features

- External pass-through extraction for containers with no native decoder —
  Wii/GC disc images, DS ROMs, CIA/3DS, Wii WADs, and Switch NSP/XCI/NCA are
  handed to `wit`/`ndstool`/`ctrtool`/`sharpii`/`hactool` and the unpacked
  tree is recursed into (`--no-passthrough` disables it).
- Wii U disc images (`.wud`/`.wux`) pass through to `wud2app`+`cdecrypt`
  (both required on PATH, plus a sibling `<name>.key` 16-byte title key next
  to the source). WUX is decompressed natively first (no external tool for
  that step); the disc is never read into RAM, so a 20+GB image extracts at
  the same fixed low memory cost as any other pass-through container.
- Recursive directory traversal and bottom-up incremental tree repacking:
  `wszst CREATE <wit-extracted-dir>` automatically detects modified assets (`.dae`, `.png`, `.yaml`, `.xml`, `.txt`) and child `.d` folders across the entire game hierarchy. It injects changed `.dae` models back into parent containers (`.brres`, `.bmd`, `.bch`, `.bcres`, `.bfres`, `.mdl0`), re-encodes textures and data, and rebuilds only the modified sub-archives (`.szs`, `.arc`, `.sarc`, `.narc`, `.darc`, `.pac`, `.gfa`, `.rarc`, `.bcsar`, `.bfsar`, `.rst`). Untouched sub-archives and files are skipped and preserved bit-for-bit.
- QuickBMS script chaining: `wszst xx --bms=<script.bms>` chains a QuickBMS
  script into `wszst xx` for unsupported containers, auto-staging and
  recursing into inner Nintendo assets.

  Build the pinned runtime with `make -C project quickbms`. It is selected
  automatically; set `WBMSX_QUICKBMS` to use a separately built runtime.
- `wajpg`/`wlzh8`/`wbmsx`/`wwc24crypt`/`wlayt`/`wmdlt`/`wbrsar` folded into
  `wimgt`/`wszst` as first-class commands, no standalone binaries needed.
- `wszst xx` recurses into pass-through and SARC/PAC/GFA/DARC extraction
  output, auto-decoding textures, layouts, fonts, and models found inside.
- `CREATE`/`ENCODE` cache a SHA1 per member in a hidden `.wszst-cache.txt`
  next to `wszst-setup.txt`. A member whose current content still matches
  the cache is neither re-encoded (e.g. an untouched `.tpl.png`) nor does it
  force the archive to be reassembled and recompressed — only a real,
  content-level change pays that cost, and a fully unchanged directory
  leaves the destination file untouched instead of rewriting it byte-for-byte.

- Fixed Nintendo-format auto-detection (`DetectNintendoFormat`) and image
  loading (`wimgt DECODE`) for **WarioWare: D.I.Y. Showcase / "WarioWare
  Snapped!"** (DSiWare, NTR-KUWE). Every LZ11-compressed Nitro graphics
  resource it stores (NCGR/NCLR/NCER/NANR) is wrapped in an extra 4-byte
  little-endian size-prefix record *inside* the LZ11 stream, before the
  resource's own RGCN/RLCN/RECN/RNAN magic: byte 0 is always `0x00`, bytes
  1-2 are a `u16` LE holding `payload_size`, byte 3 is always `0x00`, and
  the real resource — magic and all — starts at offset 4. Confirmed against
  5 real assets pulled from the retail USA Rev1 ROM (`Style/StyleO.NCLR.bin`,
  `Style/Style_Head.NCGR.bin`, `Style/Style_Head.NCER.bin`,
  `Style/Style_2P_01.NANR.bin`, `Game/WarningB.NCLR.bin`): in every case
  bytes 1-2 as LE16 equalled `(decompressed size - 4)` exactly. Previously
  every one of these files reported as unrecognized (`?`) because detection
  only ever looked for the magic at offset 0. `DetectNintendoFormat` now
  recognizes the wrapped form directly; `LoadIMG`/`AssignIMG` auto-
  decompress LZ10/LZ11 Nitro graphics and peel off the wrapper before
  handing the payload to the existing NCGR/NCLR decoders (this also fixed a
  separate, unrelated pre-existing gap where LZ10/LZ11-compressed Nitro
  graphics were never auto-decompressed before decode at all). Verified
  end-to-end: a real `WarningO.NCGR.bin` from that ROM now decodes to a
  valid 128x160 PNG via `wimgt DECODE` where it previously hard-failed.
  One separate, still-open limitation found along the way and left
  untouched (out of scope for this fix): `DecodeNCLR_RGBA` rejects any
  `depth==3` (4bpp) palette with more than 16 total colour entries, but
  some real DSiWare NCLR files (e.g. `StyleO.NCLR.bin`, a 4-bank/64-colour
  character-customization palette) legitimately pack multiple 16-colour
  banks into one `depth==3` section — multi-bank palette support is a
  separate decoder enhancement, not part of this detection/wrapper fix.
- Fixed and validated **Monster Games RST/TOC + QuickLZ** container support
  (used by Excite Truck, ExciteBots: Trick Racing, NASCAR Heat) against a real
  retail disc. TOC entry offsets are relative to the uncompressed payload,
  not the outer RST header; creation and extraction now use that retail
  convention instead of a mutually compatible 128-byte absolute-offset bug.
- Added native decode of the Monster Games (Excite Truck / ExciteBots)
  asset formats found inside those RST/TOC archives: `.tex` GX textures and
  `.art`/`.img` GUI images (both -> PNG, see the ART/IMG and TEX table rows
  for how the pixel format is recovered without a stored format field),
  `.msh` structured PMsh collision resources (-> COLLADA DAE), and `.mod`
  (NDL3/NDL2 GX display-list models, geometry format read straight out of
  the embedded display list's VAT register writes and a brute-forced index byte width) -> GLB, plus
  encoding back to geometry-only NDL3 `.mod` from DAE/GLB (see the
  MOD row), validated
  on 135/135 real Excite Truck and 193/203 real ExciteBots samples (see the
  MOD row). `.tex`/`.art`/`.msh`/`.mod` all decode real retail samples
  correctly and are safe to use today.

See the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for the full history of what was fixed, how each format was verified, and
against which real samples — not duplicated here.

### Format & compression support

3D model export defaults to **GLB** (binary glTF, textures embedded in the
file) rather than COLLADA/DAE — `wszst XX`/`XEXPORT`/`wmdlt ENCODE` write
`.glb` unless `--dest` names a file that explicitly ends in `.dae`, in which
case DAE (with loose sibling PNGs, still supported for injection workflows)
is written instead. Notes below that say "encode via DAE `--parent`
injection" describe the injection path specifically, which still consumes a
`.dae`; the model-export *default* is GLB.

| Format | Category | Decode | Encode | Notes |
|---|---|---|---|---|
| AJJPG / AJPG | Still image | ✅ | ✅ | GBA-era still image container |
| ALZ1 | Compression | ✅ | ✅ | Arika's DS/DSi archive compression -- classic 4096-byte-window LZSS with inverted, LSB-first flag bits (per GBATEK's "DS Encrypted Arika Archives with ALZ1 compression"); see the Arika row |
| Arika (INFO.DAT/GAME.DAT) | Archive | ✅ | ✅ | Arika's DS/DSi archive pair (Dr. Mario Online Rx, Dr. Mario Express, the original DS Endless Ocean; Endless Ocean: Blue World's "RF2" sub-grouping is handled by the same decoder). INFO.DAT's obfuscated directory (rotate/xor/subtract, or unencrypted when its 16-byte title key starts with 00h) is decrypted and each GAME.DAT member is either copied raw or ALZ1-decompressed; `wszst EXTRACT`/`xx` triggers on an "INFO.DAT" file and locates the sibling GAME.DAT automatically. No retail sample of either title was available while porting this from GBATEK's page plus aluigi's arika.bms/endless_ocean.bms QuickBMS scripts, so coverage is a from-spec decoder plus create→extract round trips (`tests/test-arika.c`), not yet retail-verified |
| ARCV | Archive | ✅ | ❌ | Namco/Tose's unnamed little-endian archive used by Pac-Man Party (Wii). `wszst EXTRACT`/`xx` validates the offset/size table bounds, extracts ordinally named members, recognises common embedded Wii asset types, and continues recursive extraction. Creation is unavailable because this ARCV variant stores no filenames and its CRC/rebuild rules have not been recovered. |
| ASH0 | Compression | ✅ | ✅ | |
| AT7 | Archive/compression | ✅ | ✅ | Another Century's Episode / Koei Tecmo |
| BCFNT | Font | ✅ | ✅ | 3DS bitmap font; full pixel decode to PNG via `wimgt`/`wszst XX` (RGBA8 linear exact; I4/I8/IA4/IA8/RGB565 via GX tile path, correct for linear data), encode via `wimgt` |
| BCH | Model | ✅ | ✅ | 3DS CTR H3D, incl. geometry; encode via DAE `--parent` injection |
| BCLAN | Layout | ✅ | ✅ | 3DS layout animation; shares BCLYT's parser/encoder, same status |
| BCLIM | Texture | ✅ | ✅ | 3DS textures |
| BCLYT | Layout | ✅ | ✅ | 3DS layout; 1980/1980 real files decode AND byte-exact round-trip (decode→encode→decode) against a real cartridge dump |
| BCRES | Model | ✅ | ✅ | 3DS graphics container, incl. geometry; encode via DAE `--parent` injection |
| BCSAR | Audio archive | ✅ | ✅ | 3DS Sound Archive (CSAR); recursive member & wave archive extraction (`wszst xx`) and creation (`wszst CREATE`). See the BFSAR row -- `wbfsar` reads both |
| BCWAV | Audio track | ✅ | ✅ | 3DS Sound Wave; DSP-ADPCM, IMA-ADPCM, PCM16, PCM8 decoding to WAV via `wszst DECOMPRESS`; encoding/transcoding supported in sibling `mobipeg` (`cwav`/`bcwav`) |
| BCWAR | Audio archive | ✅ | ✅ | 3DS Sound Wave Archive (CWAR); unpacks member BCWAV audio tracks and repacks (`wszst CREATE`) |
| ART / IMG | Texture | ✅ | ✅ | Excite Truck / ExciteBots (Wii) GUI images; both the older raw-GX/zero-footer layout (dimensions and format recovered via tile-seam continuity) and ExciteBots' explicit 128-byte header layout, including I4/IA4 resources with auxiliary renderer tails and multi-level images. Colour+stencil pairs are recombined into proper RGBA. Encode (`wimgt CONVERT` → `.art`/`.img`) writes the older layout and self-verifies through the real classifier; `.ebart`/`.ebimg` writes the explicit ExciteBots header (rename after conversion). The public encoder also supports an exact mip count, renderer code, every decoded GX format, and the mandatory I4/IA4 tail. Together with TEX, all 3,374 ART/IMG/TEX files in the available ExciteBots retail corpus decode successfully |
| BCGRP | Audio archive | ✅ | ✅ | 3DS Sound Group Archive (CGRP); unpacks embedded audio files and repacks (`wszst CREATE`) |
| BFFNT | Font | ✅ | ✅ | Wii U bitmap font; full pixel decode to PNG via `wimgt`/`wszst XX` (RGBA8 linear exact; I4/I8/IA4/IA8/RGB565 via GX tile path, correct for linear data), encode via `wimgt` |
| BG4 | Archive | ✅ | ✅ | Mario & Luigi: Paper Jam / Paper Mario MIX (3DS) flat archive (`"BG4\0"`, little-endian): a 16-byte header, a 14-byte-per-file table of {offset, size, CRC, u16 name offset}, then a name blob. A member whose table offset has bit 31 set is BLZ ("backward LZSS") compressed and is decompressed through this fork's existing BLZ decoder; offset 0 marks an unused slot. `wszst EXTRACT`/`xx` triggers on the magic and recurses into the extracted members; `wszst CREATE <folder> --dest <file.bg4>` repacks members with valid CRC calculations and strided metadata |
| BIGF | Archive | ✅ | ⛔ | EA BIGF archive; streamed extraction of named members, including the Littlest Pet Shop (Wii) game assets. Its variable-size table uses big-endian member offsets/sizes with a little-endian total-size field. |
| BFLAN | Layout | ✅ | ✅ | Wii U layout animation; lossless semantic text roundtrip via `wlayt`, verified on 1,148/1,148 retail files |
| BFLIM | Texture | ✅ | ✅ | Wii U textures, incl. BC1/BC2/BC3/BC4/BC5 block-compressed formats (fmt 14-17, 21-23) |
| BFLYT | Layout | ✅ | ✅ | Wii U layout; platform-specific material, pane, text, parts, group and container structures; lossless semantic text roundtrip via `wlayt`, verified on 251/251 retail files |
| BFRES | Model | ✅ | ✅ | Switch (v8+v9); geometry decode to GLB/DAE; encode via DAE `--parent` injection (`wmdlt ENCODE` / `wszst`), verified on Super Mario Odyssey models |
| BFRES | Model | ✅ | ✅ | Wii U; encode via DAE `--parent` injection; FMAT materials bound to their first FTEX texture ref, decoded+PNG'd during extraction (98.8% of a real disc's models resolve a diffuse texture) |
| BFSAR | Audio archive | ✅ | ✅ | Wii U / Switch Sound Archive (FSAR); recursive member & wave archive extraction (`wszst xx`) and creation (`wszst CREATE`). Separately, `wbfsar` reads the archive's own real internal directory (STRG name lookup + INFO's Sound/Bank/Player/WaveArchive/SoundGroup/Group/File tables) and dumps every entry's index/Id/name as XML -- verified against a real retail 48 MB Wii U file (2323 real sound names resolved correctly); per-entry internal fields (a sound's player/volume/stream-vs-sequence-vs-wave detail, a bank's instrument tree) aren't decoded yet, see lib-bfsar.h. Same container generation covers 3DS BCSAR (CSAR magic, `wbfsar` handles both) |
| BFWAV | Audio track | ✅ | ✅ | Wii U / Switch Sound Wave; DSP-ADPCM, IMA-ADPCM, PCM16, PCM8 decoding to WAV via `wszst DECOMPRESS`; encoding/transcoding supported in sibling `mobipeg` (`fwav`/`bfwav`) |
| BFWAR | Audio archive | ✅ | ✅ | Wii U / Switch Sound Wave Archive (FWAR); unpacks member BFWAV audio tracks and repacks (`wszst CREATE`) |
| RWAV | Audio track | ✅ | ✅ | Wii Sound Wave (the individual sample RWAR wave archives and RBNK instrument banks reference); DSP-ADPCM and PCM16/PCM8 decoding via `wszst DECOMPRESS`; encoding/transcoding supported in sibling `mobipeg` (`rwav`/`brwav`) |
| RBNK | Instrument bank | ✅ | ✅ | Wii instrument bank (what an RSEQ program-change selects); full program → note-range (RangeTable/IndexTable) → InstParam (wave index, ADSR, pitch/volume/pan) lookup tree, plus embedded WaveInfo metadata, dumped as lossless-structure XML via `wrbnk dump` and compiled to binary via `wrbnk compile`, with deterministic byte output and exact canonical fixed points (`FIXED_PASS`); verified against 9 real retail banks. Version ≥ 2 banks (waves referenced via an embedded RWAR instead of a direct WaveInfo list) parse the program tree but skip wave metadata |
| BFGRP | Audio archive | ✅ | ✅ | Wii U / Switch Sound Group Archive (FGRP); unpacks embedded audio files and repacks (`wszst CREATE`) |
| BRSTM / BFSTM / BCSTM | Audio stream | ✅ | ✅ | Wii/Wii U/3DS sound stream (`wbrstm`); DSP-ADPCM (`adpcm_thp`) and PCM16 to/from WAV. Encoding (`from_wav`) prefers passing straight through to the sibling 'mobipeg' repo's real `adpcm_thp` encoder over this project's own port, falling back to the port when mobipeg isn't installed or predates the brstm/dsp/bns muxers |
| BLZ | Compression | ✅ | ✅ | DS ARM9/ARM7/overlay compression |
| BMD (Early DS) | Model | ✅ | ✅ | Super Mario 64 DS / early Nitro 3D models; geometry decode to DAE & encode via DAE `--parent` injection |
| BMS | Interpreter | ✅ | ✅ | Bundled QuickBMS runtime (`wbmsx` + `wszst xx --bms`); supports the upstream command set when `third_party/quickbms/quickbms` is built. |
| BNTX | Texture | ✅ | ✅ | Switch textures; RGBA8/565/5551/4 + BC1-7 (incl. BC6H) + every standard 2D ASTC block footprint decode (validated against K0lb3/texture2ddecoder conformance vectors), RGBA8 encode |
| BREFT | Texture | ✅ | ✅ | Brawl effect texture, palette-indexed; encode via `wszst CREATE --breft`, `wimgt --btimg` |
| BRFNA | Font | ✅ | ✅ | Wii font archive, RFNA; decode to a PNG atlas plus character-placement XML; encode via `wimgt ENCODE .brfna` |
| BRFNT | Font | ✅ | ✅ | Wii bitmap font; `wimgt DECODE` joins multi-sheet fonts into one PNG atlas and writes character placements/metrics to a sibling XML file; encode via `wimgt ENCODE .brfnt` |
| BRLAN | Layout | ✅ | ✅ | Wii layout animation; lossless text roundtrip via `wlayt` |
| BRLYT | Layout | ✅ | ✅ | Wii layout; lossless text roundtrip via `wlayt` |
| BRRES MDL0 | Model | ✅ | ✅ | Wii models → COLLADA; encode via DAE `--parent` injection |
| BRRES TEX0 | Texture | ✅ | ✅ | Wii textures; encode via `wimgt`; palette-indexed decode pairs sibling PLT0 data |
| BRSAR | Audio | ✅ | ✅ | → MIDI+SF2, plus `wbrsar pack` (dir of RSEQ/RBNK/RWAR/RWSD → .brsar/.bfsar/.bcsar) and `wbrsar unpack` (any variant back to individual assets) |
| BYAML | Data | ✅ | ✅ | binary YAML; encode via `wszst CREATE .byml` |
| BYML | Data | ✅ | ✅ | binary YAML; encode via `wszst CREATE .byml` |
| CCF | Archive | ✅ | ✅ | Wii/Switch Virtual Console archive, optional zlib compression; create via `wszst CREATE .ccf` |
| CGFX | Model | ✅ | ✅ | 3DS graphics container, incl. geometry; encode via DAE `--parent` injection |
| CA01 / SA01 | Archive | ✅ | ✅ | Mii Maker (Wii U, `"SA01"`) and amiibo Settings (3DS, `"CA01"`) archives. Both wrap a zlib payload -- Mii Maker behind a bare big-endian u32 uncompressed size, amiibo behind a `"ZCMP"` header with the stream at 0x80 -- around a flat inner archive of three parallel arrays: member offsets (relative to a base offset), member sizes, and, for SA01 only, one 0x80-byte fixed-width name per file (CA01 is nameless and its members get ordinal `.sar` names). Inner word order is recovered from the file count rather than assumed, mirroring the scripts' `endian guess`. `wszst CREATE <folder> --dest <file.sa01>` or `.ca01` builds compliant inner images and wraps them in their container headers with zlib compression |
| cram (`.arc`) | Archive | ✅ | ✅ | Xenoblade Chronicles 3D (3DS) `"cram"` archive: flat, named, uncompressed -- a 16-byte header, a 16-byte-per-file table of {name CRC, 4-char type, offset, size}, then one u32 name offset per file into a trailing string blob. `wszst EXTRACT`/`xx` triggers on the magic and recurses into the members; `wszst CREATE <folder> --dest <file.cram>` builds compliant binary archives with name offsets, type tags and CRC32 table hashes |
| CTPK | Texture / Archive | ✅ | ✅ | 3DS multi-texture container; encode via `wimgt` or folder create via `wszst CREATE .ctpk` |
| DARC | Archive | ✅ | ✅ | 3DS "differential archive" container |
| DAT (Star Fox Zero) | Archive | ✅ | 🟡 | Star Fox Zero (Wii U) `"DAT\0"` big-endian flat archive: a 32-byte header naming five section offsets, then five parallel per-file arrays (member offset, 4-char type string, name, size, and a fifth table). Members are extracted as `<type>/<name>`. `wszst EXTRACT`/`xx` triggers on the magic and recurses into the members; `wszst CREATE` rebuilds one from an extracted directory. Layout ported from aluigi's `star_fox_zero_dat.bms`. Decode is retail-verified: the game ships its entire payload inside CRIWARE CPK archives (`content/data00*.cpk`, a CRIWARE container this fork does not yet read), so the sample was carved directly out of `data000.cpk` at offset 0xbec0, where a 47,072-byte DAT is stored uncompressed. All 4 members extract with their real names (`ba0001.wmb`, `ba0001.wta`, `ba0001.wtp`, `ba0001_0000.mot`) at sizes matching the stored size table exactly. Encode is 🟡: a rebuilt archive is semantically identical (every member name and payload preserved through extract→create→extract) but not byte-identical, because retail aligns member data to 0x100/0x2000 boundaries and this encoder packs members tightly, yielding a smaller file (40,336 vs 47,072 bytes on the test sample). The alignment rule the original exporter used has not been recovered |
| Deflate | Compression | ✅ | ✅ | via BMS & wszst; encode via `wszst COMPRESS --dest .deflate` |
| FSYS | Archive / Compression | ✅ | ✅ | Genius Sonority archive used by Pokémon Colosseum, XD and Battle Revolution; `wszst XX` extracts big-endian v1/v2 member tables, decodes bounded LZSS members, assigns stable ordinal names when retail entries are `(null)`, and recursively dispatches extracted children. `wszst CREATE <folder> --dest <file.fsys>` builds compliant FSYS archives with LZSS member compression and 32-byte alignment |
| FZIP | Compression | ✅ | ✅ | Game & Wario (Wii U) ZLIB-based container compression; decode & encode via `wszst COMPRESS`/`DECOMPRESS --dest .fzip` and transparent `wszst xx` extraction |
| GFA | Archive | ✅ | ✅ | "GFAC" archive; create via `wszst CREATE .gfa` |
| GTX | Texture | ✅ | ✅ | Wii U GX2 "Gfx2" texture container. Lossless bounds-checked parsing and raw-element encoding cover the complete public `GX2SurfaceFormat` storage matrix, explicit mip levels, array/cube/3D slices, MSAA samples, component selectors, depth/stencil surfaces, tile modes 0-15, thick tiles, bank swaps, and pipe/bank/slice/sample rotation. RGBA visualization covers UNORM/UINT/SNORM/SINT/FLOAT/SRGB channel formats, packed RGB/RGBA and depth formats, plus BC1-5 (including signed BC4/BC5); ordinary RGBA input and caller-supplied compressed/raw elements can be tiled back into GTX. Dedicated regressions exercise all public formats, every tile mode, mipmaps, arrays and 4x MSAA. |
| GSH | Shader | ✅ | 🟡 | Shader-only Wii U Gfx2 container; lossless bounds-checked blocks, relocation/string-table metadata, vertex/pixel/geometry/compute header↔program associations, and unknown blocks retained. `wszst EXTRACT file.gsh` emits named Latte listings only after reassembly proves complete program-byte equality; 12 programs across five real Nintendo Land containers pass, plus a deterministic semantic assembler test. Known instructions receive Decaf semantic ALU/TEX/VTX/CF output and unsupported encodings retain lossless RAW words. Rebuilding/editing a complete GSH container and high-level GLSL compilation remain outside the current encoder; CafeGLSL can be used for GLSL compilation |
| Hyrule Warriors Legends (`.idx`/`.bin`) | Archive | 🟡 | ⛔ | Hyrule Warriors Legends (3DS) split archive pair. The `.idx` half is nothing but an array of {u32 size, u32 offset} little-endian records addressing the sibling `.bin`, with size==0 marking a hole; there is no magic, header or name table, so members are extracted under ordinal names. `wszst EXTRACT`/`xx` triggers on a `.idx` file and locates the sibling `.bin` automatically, the same sibling-lookup convention the Arika INFO.DAT/GAME.DAT row uses; because `.idx` is a generic extension the scanner additionally rejects the pair outright unless every table record addresses a real, in-bounds region of that `.bin`. Layout ported from aluigi's `hyrule_warriors_legends.bms`. 🟡 because no retail cartridge dump was reachable while porting -- from-spec decoder plus a synthetic fixture (`tests/mk-bms-fixtures.py`), not retail-verified. Creation is unavailable: the format stores no names and nothing constrains how the game expects a rebuilt index laid out |
| HSF (HSFV037) | Model | ✅ | ✅ | Hudson model (Mario Party 4-8 `.hsf`, extracted from MPBIN; a different, unrelated format from HAL's own "sysdolphin" HSD below despite the similar nickname); exports geometry (including vertex colors, float/packed normals, UVs, triangles/quads/indexed strips), hierarchy, CENV skinning/inverse binds, per-primitive materials, GX textures/palettes, recursively nested replica mesh subtrees (shared geometry, cycle guards, material splits, and composed transforms), shape and partial-vertex cluster morph targets, exact type-2 successive-lerp cluster coefficients, native perspective cameras, `KHR_lights_punctual` lights, and native 60 Hz glTF TRS/morph animations (step/linear/Bézier/constant HSF curves) to GLB. Lossless sidecars retain motions, replicas, camera/light parameters, fog scenes, map attributes, matrix tables, parts, clusters, and shapes, including metadata-only HSF files. `wmdlt ENCODE model.glb --dest model.hsf` (or `.dae`) writes big-endian HSFV037 geometry, normals, UVs, vertex colors, triangle material assignments, materials, joint/object hierarchy, material attribute/symbol chains, and embedded textures converted to native GX RGBA8 textures. The model API additionally emits shape/cluster morphs, replicas, cameras/lights, whole/single/multi-weight CENV envelopes, and sampled TRS/morph motion tracks. Validated on MP4 Mario and 7,284 MP7 retail HSFs |
| HSD (sysdolphin `.dat`) | Model/Texture | ✅ | ✅ | HAL Laboratory's serialized-object-graph format (Super Smash Bros. Melee, Kirby Air Ride, Wii's "TV no Tomo" channel), identified structurally because it has no magic. Exports `HSD_Image`/`HSD_Tlut` textures and walks JOBJ/DOBJ/POBJ trees, decoding each GX display list's real per-attribute DIRECT/INDEX8/INDEX16 layout to GLB/DAE. Two-pass skeleton discovery resolves SingleBoundJOBJ and envelope-weighted meshes; MOBJ material colours/textures are bound, and playable-character root indirection is supported (fixture coverage includes Mario, Pikachu, Nana, Falco, Kirby, Luigi, Samus and Young Link). In the Melee item/object corpus, 346/352 `Ty*.dat` files produce glTF-validated geometry (9,564 meshes). `wmdlt ENCODE model.dae --dest model.dat` / `wmdlt DECODE model.dat --dest model.dae` provides complete model encoding and decoding, serializing compliant sysdolphin `.dat` graphs (`HSD_JOBJ`, `HSD_DOBJ`, `HSD_MOBJ`, `HSD_POBJ`, batched GX display lists, and relocation table) with deterministic byte output and exact canonical fixed points. |
| Huffman 0x24 | Compression | ✅ | ✅ | 4-bit nibble |
| Huffman 0x28 | Compression | ✅ | ✅ | 8-bit byte |
| Mario Party `.bin` | Archive | ✅ | ✅ | MPBIN container, games 4-8; unpacks & repacks via `wszst CREATE .bin` |
| MOD (NDL3/NDL2) | Model | ✅ | ✅ | Monster Games Excite Truck / ExciteBots (Wii) `.mod` 3D model; geometry format (position/UV element count, numeric format, fixed-point shift, index width) is read directly out of the embedded GX display list's vertex-attribute-table register writes rather than hardcoded, so it decodes the general case, not one fixed shape -> GLB (or COLLADA DAE with `--dest *.dae`); validated on 135/135 real Excite Truck and 193/203 real ExciteBots samples (the gap is exactly the separate .msh collision-mesh files, not .mod failures). Encoder: DAE/GLB -> `.mod` via `wmdlt ENCODE --dest *.mod` (plus sibling-DAE repack), emitting geometry-only NDL3 (`3LDN`) files with f32 big-endian positions, s16 shift-13 texcoords and GX TRIANGLES display lists (index width auto-sized; caps at 255 unique positions/texcoords since the tuple indices are bytes); round-trips all bundled fixtures through encode+decode twice with geometry preserved up to s16 UV quantization |
| ONE (Sonic Storybook) | Archive | ✅ | ⛔ | Sonic and the Secret Rings / Sonic and the Black Knight general asset archive. `wszst XX` validates the structural big-endian table, PRS-decompresses every member, and continues recursively into the extracted GNO/GVR/GNA/GNM/etc. assets. Wii WBFS input first passes through `wit`, so `wszst XX game.wbfs` reaches `.one` files automatically. |
| MSH (PMsh) | Model | ✅ | ✅ | Monster Games Excite Truck / ExciteBots (Wii) collision resource: little-endian bucket, indexed-position, triangle-normal and collision-plane records -> COLLADA DAE; layout recovered from the retail game loader/raycast code and validated on all 7 real ExciteBots samples, including `gpmesh.msh` and `rail2bp.msh`. Encoder: DAE/GLB -> `.msh` via `wmdlt ENCODE --dest *.msh` (plus sibling-DAE repack); recomputes face plane + inward edge normals with the formulas reverse-engineered from the retail records (matches stored floats to float32 precision) and emits <=16-triangle buckets with exact bbox spheres -- retail bucket spheres vary between exporter runs, so buckets are rebuilt rather than byte-copied |
| MSBF | Text/flow | ✅ | ✅ | Nintendo Message Studio Binary Flow; decode & encode via `wbmgt` & `wszst` |
| MSBP | Text/flow | ✅ | ✅ | Nintendo Message Studio Binary Project; decode & encode via `wbmgt` & `wszst` |
| MSBT | Text/flow | ✅ | ✅ | Nintendo Message Studio Binary Text; decode & encode via `wbmgt` & `wszst` |
| MSR (Metroid: Samus Returns) | Archive | 🟡 | ⛔ | Metroid: Samus Returns (3DS) archive: completely headerless -- `u32 info_size, u32 data_size, u32 files` followed by one {u32 CRC, u32 offset, u32 end offset} record per file, no magic and no names, so members are extracted under ordinal names. Because there is nothing to key detection off, the scanner enforces a deliberately strict set of self-consistency constraints -- the declared info section must be exactly the header plus the table, info+data must account for the entire file, members must begin immediately after the table, run in non-decreasing order, and stay in bounds -- and it is wired in *last*, after every signature-bearing format has declined the file. Layout ported from aluigi's `metroid_sr_3ds.bms`. 🟡 because no retail Samus Returns dump was reachable while porting -- from-spec decoder plus a synthetic fixture (`tests/mk-bms-fixtures.py`), not retail-verified; the detection heuristic in particular has not been exercised against a large real corpus. Creation is unavailable: the per-entry CRC's algorithm has not been recovered |
| MVDK | Compression | ✅ | ✅ | Mario vs. Donkey Kong custom Deflate/Huffman/LZSS variant (ported from Garhoogin's NitroPaint reverse-engineering); auto-detected and extracted transparently via `wszst EXTRACT`/`xx`, and encoded via `EncodeMVDK` |
| NANR | Sprite | ✅ | ✅ | DS sprite; XML via `wszst CREATE` |
| NARC | Archive | ✅ | ✅ | Nitro Archive, DS/3DS container; create via `wszst CREATE .narc` |
| NCER | Sprite | ✅ | ✅ | DS sprite; XML via `wszst CREATE` |
| NCGR | Sprite | ✅ | ✅ | DS sprite; via `wimgt` |
| NCLR | Sprite | ✅ | ✅ | DS sprite; via `wimgt` |
| NCCARC | Archive | ✅ | ✅ | WarioWare: Touched! (DS) flat blob container; unpacks member chunks and repacks via `wszst CREATE .nccarc` |
| NSBMD | Model | ✅ | ✅ | DS models, incl. bone hierarchy; encode via DAE `--parent` injection |
| NUTEXB | Texture | ✅ | ✅ | Switch texture wrapper used by Super Smash Bros. Ultimate and other titles; a distinct container from BNTX despite both being Tegra/NX formats. Struct layout verified field-by-field against Switch-Toolbox's NUTEXB.cs (KillzXGaming/Switch-Toolbox); decode reuses lib-bntx.c's already-verified Tegra deswizzle and BC1-BC7 block decoders, and EncodeNUTEXB_RGBA performs block-linear swizzling and trailer generation |
| ODH | Still image | ✅ | ✅ | GBA-era still image codec |
| PAC | Archive | ✅ | ✅ | Brawl "ARC\0" archive; create via `wszst CREATE .pac` |
| PLT0 | Animation | ✅ | ✅ | Brawl G3D palette-swap animation; IA8, RGB565, RGB5A3 encode via `wimgt` |
| PSDK | Compression | ✅ | ✅ | Prosonic SDK LZSS compression container; decompression and compression integrated into `lib-nintendo.c` and `wszst DECOMPRESS` |
| QuickLZ | Compression | ✅ | ✅ | both stream versions (1.20, 1.4.0) |
| RARC | Archive | ✅ | ✅ | GameCube / Wii object archive; create via `wszst CREATE .rarc` |
| romc | Compression | ✅ | ✅ | N64 Virtual Console ROM compression; type-1 LZ77 decode verified on Kirby 64 and deterministic literal-stream encode with 4 MiB-unit validation and round-trip coverage. Not every N64 VC title uses it (Yoshi's Story stores its ROM raw); the distinct type-2 `romchu` Huffman variant remains unavailable for lack of a retail sample |
| RL | Compression | ✅ | ✅ | |
| SARC | Archive | ✅ | ✅ | Nintendo "SARC" (Sorted ARChive); extract, inject, create via `wszst`; widely used in Switch titles |
| SSZL / VCRA | Compression / archive | ✅ | ✅ | Namco Museum Remix / Megamix (Wii): `wszst xx` recognizes extensionless `SSZL` LZSS0 streams (4096-byte zero-initialized window) and extracts `VCRA` archives; `EncodeSSZL`/`DecodeSSZL` fully supported |
| RNC1 | Compression | ✅ | ✅ | deterministic ProPack-compatible method-1 encoder; independently decoded by `propack` regression vectors |
| RNC2 | Compression | ✅ | ✅ | encode via `wszst COMPRESS --dest .rnc` |
| RSEQ | Sequence | ✅ | ✅ | Wii Revolution Sequence (.rseq/.brseq); MML disassembly, assembly, MIDI conversion (`wseqt` & `wszst`) |
| CSEQ | Sequence | ✅ | ✅ | 3DS CTR Sequence (.cseq/.bcseq); MML disassembly, assembly, MIDI conversion (`wseqt` & `wszst`) |
| FSEQ | Sequence | ✅ | ✅ | Wii U & Switch Format Sequence (.fseq/.bfseq); MML disassembly, assembly, MIDI conversion (`wseqt` & `wszst`) |
| SSEQ | Sequence | ✅ | ✅ | Nintendo DS Nitro Sequence (.sseq); MML disassembly, assembly, MIDI conversion (`wseqt` & `wszst`) |
| SDAT | Audio archive | ✅ | ✅ | Nintendo DS Sound Archive; MIDI + SoundFont SF2 conversion, raw SSEQ/SBNK/SWAR unpacking, and deterministic repacking via `wbrsar pack --sdat` |
| SMDH | Icon/metadata | ✅ | ✅ | 3DS application icon + title metadata block (icon.bin, and embedded in every CIA/3DSX/CCI title). `wimgt DECODE` exports icons to PNG, `ScanSMDH` decodes UTF-16LE titles and flags, and `EncodeSMDH` compiles binary SMDH containers |
| TEX | Texture | ✅ | ✅ | Excite Truck / ExciteBots (Wii) GX texture; supports both the older footer/header-less layout (format recovered from mip consistency) and ExciteBots' explicit 128-byte header layout, including I4/IA4 auxiliary-tail resources that can otherwise collide with OBFLOW detection. Encode (`wimgt CONVERT` → `.etex`, rename to `.tex`) writes the older layout and self-verifies through the decoder; `.ebtex` writes the explicit ExciteBots header and a full mip chain (rename to `.tex`). The public header encoder accepts an exact mip count and renderer code and emits the mandatory I4/IA4 tail. Together with ART/IMG, all 3,374 files in the available ExciteBots retail corpus decode successfully |
| WARC | Archive | ✅ | ✅ | Game & Wario (Wii U) flat archive; big-endian, uncompressed, unrelated to Excite's TOC/RES despite the naming coincidence; extraction layout ported from aluigi's `game_wario.bms`, with native folder creation and create→extract regression coverage |
| WC24 crypto | Crypto | ✅ | ✅ | `wwc24crypt` |
| WUD | Disc image | ✅ | ✅ | Wii U disc image; pass-through via `wud2app`+`cdecrypt` |
| WUX | Disc image | ✅ | ✅ | Wii U disc image, compressed; native WUX compress & decompress |
| Yay0 | Compression | ✅ | ✅ | |
| Zlib | Compression | ✅ | ✅ | via BMS & wszst; encode via `wszst COMPRESS --dest .zlib` |

✅ supported · 🟡 partial · 🔍 detected, not decoded · ⛔ not implemented — see
the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for verification details and real samples used for each of these.

### Formats WSZST supports officially (stock, upstream)

The table above covers this fork's own additions. Everything below is the
format list stock/upstream `wszst` already ships (`wszst FILETYPE`), listed
here for comparison. Status is read directly from each format's
`ff_attrib_t` flags in `src/file-type.c` (`FFT_DECODE`/`FFT_EXTRACT`/
`FFT_COMPRESS` → Decode, `FFT_ENCODE`/`FFT_CREATE`/`FFT_COMPRESS` → Encode).

| Format | Decode | Encode |
|---|---|---|
| ARC (RARC container) | ✅ | ✅ |
| BMG | ✅ | ✅ |
| BMG-TXT | ✅ | ✅ |
| BRASD | ⛔ | ⛔ |
| BREFF | ✅ | ✅ |
| BREFT | ✅ | ✅ |
| BREFT-IMG | ✅ | ✅ |
| BRES (BRRES) | ✅ | ✅ |
| BTI | ✅ | ✅ |
| BZ | ✅ | ✅ |
| BZ2 | ✅ | ✅ |
| C0CODE | ⛔ | ⛔ |
| C0DATA | ⛔ | ⛔ |
| C1CODE | ⛔ | ⛔ |
| C1DATA (CTCODE) | ✅ | ⛔ |
| CHR (CHR0) | ✅ | ✅ |
| CLR (CLR0) | ✅ | ✅ |
| CRS1 | ⛔ | ⛔ |
| CT-DEF | ✅ | ✅ |
| CT-SHA1 | ⛔ | ⛔ |
| CUP1 | ⛔ | ⛔ |
| CUPICON (cup-icon TPL) | ⛔ | ✅ |
| DOL | ✅ | ⛔ |
| DRIVER | ⛔ | ⛔ |
| GCH | ⛔ | ⛔ |
| GCT | ⛔ | ⛔ |
| GCT-TXT | ⛔ | ⛔ |
| GH-IOBJ | ✅ | ✅ |
| GH-IOBJ-TXT | ✅ | ✅ |
| GH-ITEM | ✅ | ✅ |
| GH-ITEM-TXT | ✅ | ✅ |
| GH-KART | ✅ | ✅ |
| GH-KART-TXT | ✅ | ✅ |
| GH-KOBJ | ✅ | ✅ |
| GH-KOBJ-TXT | ✅ | ✅ |
| ITEMSLT | ⛔ | ⛔ |
| ITEMSLT-TXT | ⛔ | ⛔ |
| KCL | ✅ | ✅ |
| KCL-TXT | ✅ | ✅ |
| KMG | ✅ | ✅ |
| KMG-TXT | ✅ | ✅ |
| KMP | ✅ | ✅ |
| KMP-TXT | ✅ | ✅ |
| KRM | ⛔ | ⛔ |
| KRM-TXT | ⛔ | ⛔ |
| KRT | ⛔ | ⛔ |
| KRT-TXT | ⛔ | ⛔ |
| LE-BIN | ✅ | ⛔ |
| LE-DEF | ✅ | ✅ |
| LE-DIS | ✅ | ✅ |
| LE-REF | ✅ | ✅ |
| LE-STR | ✅ | ✅ |
| LEX | ✅ | ✅ |
| LEX-TXT | ✅ | ✅ |
| LFL | ✅ | ✅ |
| LPAR | ✅ | ✅ |
| LTA | ✅ | ✅ |
| LZ | ✅ | ✅ |
| LZMA | ✅ | ✅ |
| MDL (MDL0) | ✅ | ⛔ |
| MDL-TXT | ⛔ | ⛔ |
| MOD1 | ⛔ | ⛔ |
| MOD2 | ⛔ | ⛔ |
| MTCAT | ✅ | ✅ |
| OBFLOW (OBJFLOW) | ✅ | ✅ |
| OBJFLOW-TXT | ✅ | ✅ |
| OVR1 | ⛔ | ⛔ |
| PACK | ✅ | ✅ |
| PAT (PAT0) | ✅ | ✅ |
| PAT-TXT | ✅ | ✅ |
| PNG | ✅ | ✅ |
| PREFIX | ✅ | ✅ |
| REL (STATICR) | ⛔ | ⛔ |
| RKC | ✅ | ✅ |
| RKCO | ⛔ | ⛔ |
| RKG | ⛔ | ⛔ |
| SCN (SCN0) | ✅ | ✅ |
| SHA1ID | ⛔ | ✅ |
| SHA1REF | ⛔ | ✅ |
| SHP (SHP0) | ✅ | ✅ |
| SKP-OBJ | ✅ | ✅ |
| SRT (SRT0) | ✅ | ✅ |
| TEX (TEX0) | ✅ | ✅ |
| VIS (VIS0) | ✅ | ✅ |
| TEX+CT | ✅ | ✅ |
| TPL | ✅ | ✅ |
| U8 | ✅ | ✅ |
| VEH (VEHICLE) | ⛔ | ⛔ |
| WAV-OBJ | ✅ | ✅ |
| WCH | ⛔ | ⛔ |
| WPF | ⛔ | ⛔ |
| WU8 | ✅ | ✅ |
| XPF | ⛔ | ⛔ |
| XYZ | ✅ | ✅ |
| XZ | ✅ | ✅ |
| YAZ (YAZ0) | ✅ | ✅ |
| YAZ1 | ✅ | ✅ |
| YBZ | ✅ | ✅ |
| YLZ | ✅ | ✅ |

✅ supported · 🟡 partial · ⛔ not implemented/registered — many of these `⛔`
rows (e.g. `CLR0`/`SCN0`/`SHP0`, the `RKG`/`GCT`/`WCH` family) are
recognized/detected file types with no dedicated decode or encode path
wired up in stock `wszst` itself.

Notes on the BRRES animation siblings added by this fork:

- **CHR0** 🟡 — bone animation. Full binary decode and re-encode of the
  container, resource group, per-bone code word, all nine channels and the
  I4/I6/I12/L1/L2/L4 track encodings, plus a lossless line-based text form
  (`src/lib-chr.{c,h}`, sharing `src/lib-brres-anim.{c,h}`). Verified against
  the 24 version-5 CHR0 files reachable from a retail Mario Kart Wii (USA)
  disc image: all 24 decode, and decode→encode→decode is semantically
  identical for all 24, byte-identical for 3. The remaining differences are
  confined to name-pool ordering and the order in which shared track blobs
  are emitted; the animation data itself matches. **Version 3 is deliberately
  refused** (`ERR_NOT_IMPLEMENTED`): its I6 tracks use an 8-byte header that
  carries no quantization base/step pair, and no scaling we tested reproduced
  the stored Hermite tangents across the 62 retail v3 tracks available, so the
  decoder declines rather than emitting plausible-looking wrong numbers. This
  is why the row is 🟡 and not ✅.
- **SRT0** 🟡 — material texture-SRT animation. Full decode and re-encode of
  the container, resource group, per-material layer masks, the per-layer code
  word and all five channels, plus a text form (`src/lib-srt.{c,h}`). Verified
  against the 10 retail Mario Kart Wii SRT0 files: all 10 decode, all 10 round
  trip semantically identically, 1 byte-identically, with the same name-pool
  and track-ordering caveat as CHR0.
- Both writers reproduce the retail layout: the NW4R resource-group lookup
  tree (reusing this repo's `CalcEntryBRRES`), the trailing length-prefixed
  string pool that sits outside the sub-file's declared size, and the sharing
  of byte-identical track blobs between channels.
- Both are now reachable from the CLI. `wszst TEXT <file.chr0>` decodes to the
  text form and `wszst BINARY <file.txt>` re-encodes it, matching how PAT0 is
  wired: new `FF_CHR_TXT`/`FF_SRT_TXT` file types with `#CHR`/`#SRT` text
  magics, so both directions are auto-detected and no explicit format flag is
  needed. Retested end to end through the CLI on the 6 retail Animal Crossing:
  City Folk animations in `tests/fixtures` (4 CHR0, 2 SRT0): all 6 decode and
  re-encode, the re-encoded binary decodes to a byte-identical text form, and
  the encoder is deterministic. None of the 6 is byte-exact against retail —
  the payload values are all present and the file size is often unchanged, but
  the deduplicated track blobs are laid out in a different order. Covered by
  `t_chr_srt_cli` in `tests/regress.sh`.
- The rows stay 🟡 rather than ✅: CHR0 still refuses version 3, and neither
  encoder reproduces retail's byte layout.
- **VIS0** 🟡 — node visibility animation. A `src/lib-vis0.{c,h}` had been
  sitting in this tree wired into the Makefile but reachable from nothing:
  there was no `FF_VIS`, so `wszst` reported these files as `?` and no code
  path ever called the library. It now has a file type (`FF_VIS`/`FF_VIS_TXT`,
  `VIS0`/`#VIS` magics), a BRSUB version record for v3 and v4, and TEXT/BINARY
  dispatch, which is also the first time any of it has ever executed.
  Running it found two real bugs, both fixed: the writer laid the string pool
  out *before* the resource group, which made every group-relative entry name
  offset negative and wrapped it to a huge u32, and a header offset slip wrote
  `n-frames` over the orig-path field, so re-encoded files lost their frame
  count and loop flag. The writer now uses the retail order (header, group,
  entry data, pool last) and the real NW4R lookup tree via `CalcEntryBRRES`.
  Verified on the 5 retail Animal Crossing: City Folk VIS0 files in
  `tests/fixtures`: all 5 decode and re-encode, the re-encoded file decodes to
  an identical text form, and re-encoding *that* is byte-identical, so the
  writer is deterministic. Against retail the group is reproduced exactly —
  same group size, entry count, dummy root and entry-data offset — but the
  files are not byte-identical, for a reason specific to VIS0: unlike CHR0 and
  SRT0, retail VIS0 entries do not carry their own names. Their name offsets
  point into the **shared BRRES string pool**, because the names duplicate the
  sibling MDL0's bone names, so a VIS0 extracted on its own has no bytes to
  resolve them from. The decoder substitutes explicit `"?<offset>"` markers and
  warns, rather than silently emitting empty names; since the NW4R tree ids are
  derived from the names, they differ too. Resolving these needs the VIS0 to be
  decoded in its container, which is not yet wired — hence 🟡.
- **CLR0** ✅ — material colour animation. Implemented from scratch in
  `src/lib-clr.{c,h}` (layout taken from BrawlLib's `CLR0.cs`: `CLR0v3`/
  `CLR0v4`, `CLR0Material`, `CLR0MaterialEntry`, `CLR0EntryFlags`). CLR0
  animates up to 11 GX colour targets per named material — the two light
  channel material colours, the two ambient colours, the three TEV colour
  registers and the four TEV konstants — each either absent, one constant
  RGBA, or one RGBA per frame. Registered as `FF_CLR`/`FF_CLR_TXT`
  (`CLR0`/`#CLR`), with BRSUB version records for v3 and v4 and TEXT/BINARY
  dispatch in `wszst`.
  Unlike VIS0, CLR0 carries its material names in its own trailing
  length-prefixed pool, so a standalone file is fully self-describing and the
  encoder can be held to byte equality — and is: both retail Animal Crossing:
  City Folk CLR0 files in `tests/fixtures` re-encode byte-identically to the
  originals. That required one non-obvious detail: retail lays the string pool
  out in **ordinal name order**, not in material-record order (BrawlLib's
  shared string table sorts before writing), so the writer sorts the pool and
  scatters the resulting offsets back onto the logical slots. The encoder also
  deduplicates identical colour arrays, as retail does. Covered by
  `t_clr0_cli` in `tests/regress.sh`.
- **SHP0** ✅ — vertex morph animation. Implemented from scratch in
  `src/lib-shp.{c,h}` (layout from BrawlLib's `SHP0.cs`: `SHP0v3`/`SHP0v4`,
  `SHP0Entry`, `SHP0KeyframeEntries`, plus `SHP0Node.OnInitialize` for how the
  string list resolves). SHP0 blends a polygon between named vertex sets over
  time: each entry names one polygon and carries one track per morph target,
  each track either a fixed value or a list of `(frame, value, tangent)`
  keyframes. The non-obvious part of the layout is `_indiciesOffset`, which
  points at the `u16` target-index array while the parallel `s32` keyframe-set
  offset array sits *immediately before* it — BrawlLib reaches it as
  `_indiciesOffset - 4 * _numIndices`.
  Registered as `FF_SHP`/`FF_SHP_TXT` (`SHP0`/`#SHP`), with BRSUB version
  records for v3 and v4 and TEXT/BINARY dispatch in `wszst`.
  Like VIS0, retail SHP0 names its morph targets through the **shared BRRES
  string pool**, so a standalone file cannot resolve them — but unlike VIS0
  those offsets feed nothing else (the NW4R tree is built from the polygon
  names, which *are* local), so the decoder keeps each raw offset and the
  encoder writes it back verbatim. That makes byte equality reachable anyway,
  and it holds: **all 31 retail SHP0 animations** found across the Mario Kart
  Wii sample corpus re-encode byte-identically to the originals. Two of the
  source archives are checked in as `tests/fixtures/mkw_r_parasol.brres` and
  `mkw_wanwan.brres`; covered by `t_shp0_cli` in `tests/regress.sh`.
- **SCN0** is the scene animation: light sets, ambient lights, lights, fog and
  cameras. It is the only NW4R animation built on a *nested* resource group --
  one outer group naming the sections, each holding its own group of nodes.
  Two layout rules had to be recovered from the retail files because BrawlLib
  does not describe them: a node's animated slots are written in flag-**bit**
  numeric order rather than in struct field order (for a camera that puts
  `perspFovY`, bit 0x80, physically ahead of `rotX`, bit 0x2000), and the
  trailing string pool is in ordinal name order, with version 4 declaring a
  size that *includes* that pool while version 5 stops at the end of the data
  section. As with SHP0, a node's own name offset points into the shared BRRES
  string pool and cannot be resolved standalone, so it is preserved verbatim.
  **All 19 retail SCN0 animations** found across the Mario Kart Wii sample
  corpus re-encode byte-identically to the originals, and every byte below the
  declared size is accounted for by the model. Fixtures are checked in as
  `tests/fixtures/mkw_123dai.brres`, `mkw_scn0_v4_course.scn0` and
  `mkw_scn0_v5_course.scn0`; covered by `t_scn0_cli` in `tests/regress.sh`.
  Fog is the one gap: **no sample in the corpus contains a fog node**, so the
  fog path is implemented from the BrawlLib layout but is *unverified* against
  retail data.


---

## Credits & Attributions

This project builds upon, incorporates, and references various open-source libraries, reverse-engineering projects, and format specifications:

### Core & Reference Implementations
- **[Wiimms SZS Tools](https://szs.wiimm.de/)** by Dirk Clemens (Core engine & framework)
- **[Garhoogin / NitroPaint](https://github.com/Garhoogin)** by Garhoogin (Nintendo DS graphics, palettes, cell/animations, and NSBMD 3D formats)
- **Nintendo DS Decompressors** by CUE (Nintendo compression algorithms: LZ77 0x10, LZ11 0x11, Huffman 0x24/0x28, RLE 0x30, Diff 0x80)
- **[Switch Toolbox](https://github.com/KillzXGaming/Switch-Toolbox)** by KillzXGaming (Switch, Wii U, and 3DS formats: BFRES, BNTX, BCA, BMA, BNXP, SARC, BYML)
- **[Kuriimu & Kuriimu2](https://github.com/FanTranslatorsInternational/Kuriimu)** by IcySon55 & FanTranslatorsInternational (Game translation tools, MSBT, BMG, containers)
- **[BrawlCrate & BrawlLib](https://github.com/soopercool101/BrawlCrate)** by soopercool101, BrawlCrate Team, Kryal, BlackJax96 (Wii NW4R formats: BRRES, MDL0, CHR0, CLR0, PAT0, SCN0, SHP0, SRT0, VIS0, REFF, REFT)
- **[GotaSequenceCmd & Nitro Studio](https://github.com/Gota7)** by Gota7 (Nintendo DS/3DS sound formats: SDAT, SSEQ, SBNK, SWAR, CSEQ, CWAV)
- **[SPICA](https://github.com/gdkchan/SPICA) & [Ohana3DS](https://github.com/gdkchan/Ohana3DS-Rebirth)** by gdkchan (3DS CTR NW4C BCH, CTPK, PICA200)
- **[benzin](https://github.com/Treeki)** by Treeki, feartec, megazig, quickdraw (Wii BRLYT / BRLAN layout tools)
- **[LayoutStudio & WiiLayoutEditor](https://github.com/NinjaCheetah/LayoutStudio)** (Nintendo layout format tools)
- **[Sharpii & libWiiSharp](https://github.com/Treeki/Sharpii)** by Treeki & Leathl (Wii container & system formats)
- **[QuickBMS](http://aluigi.altervista.org/quickbms.htm)** by Luigi Auriemma (Container extraction & compression specifications)

### Embedded Libraries
- **[LibYAML](https://github.com/yaml/libyaml)** by Kirill Simonov & contributors (YAML parser/emitter)
- **[Mini-XML (mxml)](https://www.msweet.org/mxml/)** by Michael R Sweet (XML parser/writer)
- **[cgltf](https://github.com/jkuhlmann/cgltf)** by Johannes Kuhlmann & Philip Rideout (glTF 2.0 / GLB)
- **[Decaf](https://github.com/decaf-emu/decaf-emu)** by exzap & contributors (Latte GPU ISA tools)
- **[VGMTrans](https://github.com/vgmtrans/vgmtrans)** by Mike & VGMTrans Team (Audio & SDAT translation)
- **[bcn-decoder](https://github.com/K0lb3)** by K0lb3 (BCn texture codecs)
- **[bzip2](https://sourceware.org/bzip2/)** by Julian R Seward
- **[LZMA SDK](https://www.7-zip.org/sdk.html)** by Igor Pavlov
- **[QuickLZ](http://www.quicklz.com/)** by Lasse Mikkel Reinhold

See [CREDITS.md](CREDITS.md) for full licensing and attribution details.
