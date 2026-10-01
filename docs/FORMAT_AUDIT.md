# Retail format audit

Retail samples retrieved with rclone were checked against independent Python
record parsers and byte-for-byte extraction comparisons. No game assets are
included in this repository. These results establish support for the sampled
files, not every revision or title that might use a similar extension.

| Sample | Coverage | Result |
| --- | --- | --- |
| Jump Super Stars (Japan), Nintendo DS | 745 ALAR type-2 archives; 2,528 members | Every member name and payload matched |
| Jump Ultimate Stars (Japan), Nintendo DS | 283 ALAR type-2 and 32 type-3 archives; 6,787 members | Every member name and payload matched |
| Jump Ultimate Stars compressed members | 3,249 DSCP-wrapped streams | Native decompression matched an independent LZ10 decoder byte for byte |
| Pikmin (USA), GameCube | 20 ARC/DIR pairs; 1,169 members | Interleaved directory records, aligned names, and Shift-JIS names |
| Pikmin standalone textures | 11 TXE files | Decoded through wimgt, including textures with an omitted payload size |

The ALAR fix restores all members instead of extracting only the first, and
associates type-3 names with their own records. Both extractors validate the
complete directory before writing, preserve empty members, reject unsafe paths,
propagate write failures, and honor `--recurse=0`. Known DSCP and CX00 wrappers
are accepted by LZ10/LZ11 decoding; CX00 coverage is synthetic.

Pikmin's ARC/DIR reader and writer now use interleaved records. The writer is
available through the C API and tested with synthetic fixtures; CLI creation is
not wired up. TXE loading is extension-gated because the format has no magic.

## Remaining uncertainty

The sampled Jump Stars files use ALAR rather than establishing retail STPK
support. The Pikmin disc listing contains ARC/DIR pairs and no `.pvol` files;
PVOL was identified as the Pipeworks Software volume archive format (`.vol` / `.pvol`)
used in Pipeworks titles such as *Godzilla: Unleashed* and *Godzilla: Save the Earth*.
STPK retains synthetic round-trip coverage while its former Jump Stars attribution
was unverified. Extraction of proprietary Jump Stars member data
does not imply semantic decoding of every contained image, animation, or model
format. Recompression and ALAR creation were not established by this audit.

## Reproduce

Retrieve the matching images from your configured rclone remote, extract the
NitroFS trees with `ndstool`, and extract Pikmin's ARC/DIR and TXE files with a
disc tool capable of reading RVZ. The source object names used were:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Jump Super Stars (Japan).zip
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Jump Ultimate Stars (Japan).zip
Nintendo - GameCube/Redump/[RVZ]/Games/Pikmin (USA).rvz
```

Run from the repository root, substituting your extracted directories:

```sh
make -C project -j8 all test-alar test-pikarc
python3 -m unittest discover -s tests -p 'test_*cli.py'
python3 tests/audit_retail_corpus.py /path/to/jump-super/files --output /tmp/super.json
python3 tests/audit_retail_corpus.py /path/to/jump-ultimate/files --output /tmp/ultimate.json
python3 tests/audit_retail_corpus.py /path/to/pikmin --format pikmin --output /tmp/pikmin.json
wimgt DECODE /path/to/texture.txe -d /tmp/texture.png
```

The corpus checker writes a JSON report with source hashes, member counts,
diagnostics, and mismatches. It snapshots the executable and uses temporary
output directories, leaving the supplied corpus untouched. Regression fixtures
are generated independently and contain no retail assets. Native parser tests
also support AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
make -C project test-alar ALAR_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
make -C project test-pikarc PIKARC_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
```

SHA-256 of the extracted DS ROMs and the original Pikmin RVZ:

```text
Jump Super Stars.nds    819a2325536c3f5b5fdbf6d77d924e5d30b686ba8741f0a9893e9dbc4bb57877
Jump Ultimate Stars.nds a9c9bf89e6d99548b7c87e822b217c3fb74ef25186535b06193a6fb73d0d6d27
Pikmin.rvz             239946db4d46fb29b90245a7602f27f83e3b5c90dc98510b15a2fd191f70b76f
```

## Professor Layton audio and archives

Additional rclone samples:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Professor Layton and the Curious Village (USA, Australia).zip
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Professor Layton and the Diabolical Box (USA).zip
```

| Sample | Coverage | Result |
| --- | --- | --- |
| Curious Village | 59 SADL IMA streams: 42 mono, 17 stereo | Every PCM sample, channel count, sample rate and length matches vgmstream |
| Diabolical Box | 359 SADL Procyon streams: 312 mono, 47 stereo | Every PCM sample, channel count, sample rate and length matches vgmstream |
| Both games | 4 looped streams | Original loop bounds preserved in WAV `smpl` chunks |
| Diabolical Box | 74 LZ10-wrapped PCK2 archives; 8,160 members | All payloads match independent decompression and directory parsing |

SADL conversion now accepts retail lowercase magic, decodes 16-byte channel
blocks with independent predictor state, honors the stored initial IMA state,
and handles Procyon ADPCM instead of treating every payload as IMA. Both sample
rates are covered by retail samples. The Layton samples use 0x100 headers. The 0xC0 variant is also verified
in Luminous Arc below; uppercase compatibility has synthetic coverage.
Malformed channels, codec flags, offsets, block lengths, IMA indices, and loops
are rejected before writing output. Decoding emits one pass through the stream,
with loop metadata rather than repeated audio.

The audio reference is [vgmstream commit 7dc938fa](https://github.com/vgmstream/vgmstream/tree/7dc938fa2f210943b37c7b6511852b516ef432ab),
particularly `src/meta/sadl.c`, `src/coding/ima_decoder.c`, and
`src/coding/nds_procyon_decoder.c`. The upstream [copyright and permission
notice](licenses/vgmstream.txt) is retained. Native conversion does not require
vgmstream; only the optional corpus checker uses it.

PCK2 uses sequential records with separate header, record and payload sizes.
The extractor validates all records and names before writing and supports raw
PCK2, LZ10-wrapped PLZ, empty members, dry runs and `--recurse=0`. The sample
members contain 4,755 scripts, 1,963 data files, 1,380 text files, 20 models, and
42 motion/animation files. Extraction does not establish semantic decoding of
every member type. PCK2 creation is not implemented.

```sh
wszst DECOMPRESS stream.SAD -d stream.wav --no-passthrough
wszst EXTRACT archive.plz -d archive-out --recurse=0 --no-passthrough
python3 tests/audit_sadl_corpus.py /path/to/curious/files /path/to/diabolical/files \
  --reference /path/to/vgmstream-cli --output /tmp/sadl-report.json
python3 tests/audit_retail_corpus.py /path/to/diabolical/files \
  --format pck2 --output /tmp/pck2-report.json
make -C project test-sadl SADL_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
make -C project test-pck2 PCK2_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
```

Extracted ROM SHA-256:

```text
Curious Village.nds 0d6c06e010f4228605f08f934bf30d994e9986898ddc68a5260f6937c41bda57
Diabolical Box.nds  604e962ad3eda65a637c6cace2a521d99133501b59c9cac150d7face11cba3f7
```

A Shift-JIS filename in Curious Village blocked the local `ndstool` build on
macOS. The audit instead read NitroFS directory/FAT records independently and
decoded names as UTF-8 or Shift-JIS. That external extraction limitation remains.
No retail game assets are included in this repository.

## Layton backgrounds and additional SADL variants

Native `wimgt DECODE` now renders Layton background images (`.arc` and `.arb`)
as PNG. The layout contains a BGR555 palette, 8-bit 8×8 tiles and a 16-bit tile
map. Nonzero palette entries are opaque; palette entry zero retains its alpha
bit. The loader accepts raw images and the four-byte Layton type prefix before
LZ10, RLE, Huffman-4 or Huffman-8 data. A complete structural and index check
precedes allocation and rendering, and the probe is restricted to `.arc` and
`.arb` candidates. A palette count that resembles a compression prefix is
handled as raw data when the full raw layout is valid.

| Sample | Images | Verification |
| --- | ---: | --- |
| Curious Village, `data/bg` | 1,161 | Every rendered RGBA pixel matches the independent decoder |
| Diabolical Box, `data_lt2/bg` | 1,030 | Every rendered RGBA pixel matches the independent decoder |

The combined set contains 2,092 LZ10, 80 Huffman-8, 16 RLE and 3 raw images.
Huffman-4 is covered by synthetic fixtures. Sprite/animation `.arc` files and
`.bgx` resources have different layouts and are not covered by this decoder.
Background encoding is not implemented.

The layout was cross-checked against the public-domain
[LaytonEditor BGImage reader](https://github.com/C3RV1/LaytonEditor/blob/8c57f449798caaa4ffd2d711f9624854daa96915/formats/graphics/bg.py)
and its [compression wrapper definitions](https://github.com/C3RV1/LaytonEditor/blob/8c57f449798caaa4ffd2d711f9624854daa96915/formats/compression/__init__.py).
The corpus checker independently expands the wrapper, reconstructs the palette
and tile map, and compares every output pixel, including transparent pixels.

Two more rclone samples broadened the SADL audit:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Luminous Arc (USA).zip
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Soma Bringer (Japan).zip
```

* **Luminous Arc:** all 2,152 streams match vgmstream, verifying the 0xC0 payload
  offset and codec flag 0x00 on retail mono, 16,364 Hz IMA audio.
* **Soma Bringer:** all 3 streams match vgmstream, covering stereo 32,728 Hz IMA
  and Procyon ADPCM. The outer NitroFS contains `data/data.srl`; the actual game
  files are in this nested DS ROM, which was extracted separately.

These are compatibility confirmations of the SADL decoder, with no audio-codec
changes required. The total verified SADL corpus is now **2,573 streams**.

```sh
wimgt DECODE background.arc -d background.png
python3 tests/audit_layton_backgrounds.py /path/to/curious/data/bg \
  /path/to/diabolical/data_lt2/bg --output /tmp/background-report.json
python3 tests/audit_sadl_corpus.py /path/to/luminous/files /path/to/soma/inner-files \
  --reference /path/to/vgmstream-cli --output /tmp/additional-sadl.json
make -C project test-layton-bg LAYTON_BG_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
```

Extracted ROM SHA-256 (Soma Bringer is the outer image):

```text
Luminous Arc.nds 30c8c2bf9eced043c4a993f43076fd8532a2df038d65ae5bed62a8cba8c87bed
Soma Bringer.nds db97954685e09b3814a5d048c4460779ecfee58a0389e62f59abc22f55cd171f
```


## Luminous Arc IEAR resource archives

Native `wszst EXTRACT` recognizes the `MAIN`/`JTBL` signature independently of
filename extension. It validates the complete directory, chunk lengths and
`ENDT` footer before writing any files. Each 16-byte resource wrapper is removed;
payloads are emitted as `file_0000.nclr`, `file_0001.ncbr`, and so on, using the
lowercase chunk tag. Unsafe tags fall back to `.bin`, and indexed names prevent
duplicate tags from overwriting one another.

All **331 archives and 7,511 members** from the Luminous Arc (USA) sample above
match an independent Python directory reader byte for byte. Native regression
tests cover every truncation of a fixture, oversized counts and corrupt offsets
and lengths under AddressSanitizer and UndefinedBehaviorSanitizer. CLI tests also
cover dry runs, empty members, unsafe tags and write failures.

This is payload extraction support. Repacking, automatic palette association,
and semantic decoding of proprietary resource tags remain unsupported.

```sh
wszst EXTRACT resource.iear --recurse=0 -d extracted
python3 tests/audit_retail_corpus.py /path/to/luminous/files --format iear \
  --output /tmp/iear-report.json
make -C project test-iear IEAR_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
```

The additional rclone sample
`Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Luminous Arc 2 (USA).zip`
contains no `MAIN` archives. It uses different resource layouts, so this IEAR
audit does not establish support for the sequel's resources. Its extracted ROM
SHA-256 is `dd6fc8a1e8a9019f75ec6683bbbdb001ded9fab7328b2e3802260b7a870064aa`.
No retail assets are included in the repository.


## Luminous Arc 2 LZE compression

Native `wszst DECOMPRESS` and `EXTRACT` now recognize the six-byte `Le` header
(two magic bytes and a little-endian 32-bit decoded size). Four two-bit commands
per flag byte represent one literal, three literals, a short-distance match, or
a long-distance match. Overlapping matches are supported. The decoder validates
the whole token stream before allocating output, rejects references before the
output start and matches beyond the declared length, and applies the shared
output-size limit. Final literal triples can end at the declared output size.
Trailing alignment bytes are permitted.

All **1,275 streams**, totaling **17,197,224 decoded bytes**, from the Luminous
Arc 2 (USA) sample above match
[CUE's reference LZE decoder](https://github.com/WonderfulToolchain/wf-nnpack/blob/d65bf0bd9ebfb5642ec63ff40b7bb1dcb670eb7f/lze.c)
byte for byte with no reference warnings. The corpus includes 408 `.LZE`, 250
`.imb`, 250 `.scb`, and 367 `.bin` files. CUE's 2011 implementation documents the
token layout and is used as the external reference; the native decoder performs
its own bounded validation. No reference source or retail assets are bundled.

Nine CLI tests cover all token forms, flag rollover, maximum distance and run
length, output naming, empty streams, dry runs, extraction dispatch, and invalid
streams. Native tests run under AddressSanitizer and UndefinedBehaviorSanitizer.
The format-name registry also restores missing NTTF/SHDVAR labels, which had
shifted the names of later formats; native tests guard those labels and AFS.

```sh
wszst DECOMPRESS resource.LZE -d resource.bin
python3 tests/audit_lze_corpus.py /path/to/luminous2/files \
  --reference /path/to/wf-nnpack/lze --output /tmp/lze-report.json
make -C project test-lze LZE_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
```

This adds decompression, not LZE encoding or rendering of the game's raw image,
palette, and tile-map resources. The same codec is documented for Luminous Arc 3,
but that game's resources have not been validated here.


## Luminous Arc 2 screen backgrounds

`wimgt DECODE image.scb` now loads `image.imb` and `image.plb` from the same
directory and renders the assembled background as PNG. SCB maps and IMB tiles
may be raw or LZE-compressed; palettes are raw BGR555. The verified layouts use
16-color, 4-bit tiles (32-byte palette) or 256-color, 8-bit tiles (512-byte
palette). Palette index zero is transparent, matching DS background behavior.
Screen entries use the standard DS tile number and horizontal/vertical flip bits
as documented in [GBATEK](https://mgba-emu.github.io/gbatek/#lcd-vram-bg-screen-data-format-bg-map).

The four SCB header words are tile columns, tile rows, entry width (16 bits),
and row stride in bytes. All dimensions, resource lengths, tile references and
palette modes are checked before allocating the rendered image. Missing
companions and malformed inputs return an error without producing a PNG.
Palette-bank variants are rejected because none were present in the sample.

All **250 backgrounds** from the Luminous Arc 2 (USA) sample above pass an
independent tile-first renderer comparison, including every RGBA pixel. Of these,
143 use 256 colors and 107 use 16 colors. The reference reader uses CUE to expand
LZE streams separately. A rendered gameplay resource was also inspected visually.
Six CLI tests cover both color depths, four flip states, raw/compressed inputs,
missing companions, invalid dimensions/indices, truncations and dry runs. Native
bounds tests pass AddressSanitizer and UndefinedBehaviorSanitizer.

```sh
wimgt DECODE image.scb -d image.png
python3 tests/audit_luminous_backgrounds.py /path/to/luminous2/files \
  --reference /path/to/wf-nnpack/lze --output /tmp/luminous-bg-report.json
make -C project test-luminous-bg LUMINOUS_BG_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
```

Companion filenames use the same stem and lowercase `.imb`/`.plb` extensions.
This renders individual resources; it does not reproduce game compositing,
animation, or sprite resources, and no encoding is implemented. No retail assets
are included in the repository.

## Soma Bringer graphics, archives, and animation sequences

Retail sample:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Soma Bringer (Japan).zip
```

| Sample | Coverage | Result |
| --- | --- | --- |
| Soma Bringer (Japan) | 20 DAD LZSS compressed archives (`.dad`, `DAD\x01`) | 100% decompressed cleanly and verified against ARM9 disassembly |
| Soma Bringer (Japan) | 16 PCS animation / layout timelines (`.pcs`, `pcs\0`) | 100% decoded into structured components and FX32 keyframes |
| Soma Bringer (Japan) | 204 OBP sprite/object graphics (`OBP1`) | Validated dimensions, RGB555 palettes, and 4bpp/8bpp tile banks |
| Soma Bringer (Japan) | 23 BGP background graphics (`BGP1`) | Validated 256x192 screen dimensions and companion palettes |

*Soma Bringer* (developed by Monolith Soft with music by Procyon Studio) uses a dedicated graphics, animation, and compression stack:
1. `DAD\x01`: LZSS compressed stream with an 8-byte header (`DAD\x01` + little-endian uncompressed size). Uses 1-bit flags (1 for literal byte, 0 for 2-byte sliding window reference: `dist = b1 | ((b2 & 0xf0) << 4)`, `len = (b2 & 0x0f) + 3`). Decompresses to `OBP1` graphics, `DFN\0` fonts, and `PACK` databases (`database.dad`). Decompression is integrated into `wszst decompress`.
2. `pcs\0`: 2D animation sequence and layout container with `pcn\0` components containing Translation (`pos\0`), Rotation (`ang\0`), Scale (`sca\0`), and Color (`col\0`) keyframe tracks in Nintendo DS FX32 (20.12 fixed point) format. Supported via `wszst text` and `wszst dump`.
3. `OBP1` / `BGP1`: 2D sprite sheets and background graphics with header fields, dimensions, and embedded RGB555 palettes. Supported via `wszst text` and `wszst dump`.

## Castlevania DS sprite object containers

Retail samples:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Castlevania - Dawn of Sorrow (USA).zip
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Castlevania - Order of Ecclesia (USA) (En,Fr).zip
```

| Sample | Coverage | Result |
| --- | --- | --- |
| Castlevania: Dawn of Sorrow (USA) | 165 Sprite Object containers (`so/p_*.dat`, `0xBEEFF00D`) | 100% (165/165) parsed and disassembled cleanly |
| Castlevania: Order of Ecclesia (USA) | 347 Sprite Object containers (`so/p_*.dat`, `0xBEEFF00D`) | 100% (347/347) parsed and disassembled cleanly |

Konami's Nintendo DS *Castlevania* trilogy (*Dawn of Sorrow*, *Portrait of Ruin*, *Order of Ecclesia*) structures character, enemy, and boss animations through binary sprite definition containers with magic `0xBEEFF00D` (little-endian byte sequence `0x0D, 0xF0, 0xEF, 0xBE`). Each container indexes parts (16 bytes: position, source graphics coordinates, width, height, graphics bank page, horizontal/vertical flip flags, palette index), hitboxes (8 bytes: offset, width, height), frames (12 bytes: composite part and hitbox ranges), frame delays (8 bytes: frame index, duration, flags), and animation sequences (8 bytes: frame count, start delay index). Disassembly into structured TOML/text is available via `wszst text` and `wszst dump`.

## Capcom CPAC multi-section archive containers

Retail sample:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Ghost Trick - Phantom Detective (USA) (En,Fr,De,Es,It).zip
```

| Sample | Coverage | Result |
| --- | --- | --- |
| Ghost Trick: Phantom Detective (USA) | `cpac_3d.bin` (6.45 MB, 4 sections) | 1,481/1,481 member files streamed and extracted cleanly with 0 bounds overflows |
| Ghost Trick: Phantom Detective (USA) | `cpac_2d.bin` (81.83 MB, 5 sections) | 24,852/24,852 member files (including 2,904 palettes) streamed and extracted cleanly |

Capcom DS titles (such as *Ghost Trick: Phantom Detective* and *Resident Evil: Deadly Silence*) package large multi-section resource archives (`cpac_2d.bin`, `cpac_3d.bin`) under a tagged chunk architecture. An outer index table describes 4-6 sections. Each section begins with a 24-byte tag header specifying `BKEY` / `BDAT` (for 3D models, 2D animations, textures, scripts) or `PKEY` / `PDAT` (for color palettes). `BKEY` sections index member pairs via 16-byte records, with bit 31 of size flagging standard Nintendo LZ11 compression. `PKEY` sections index RGB555 palette banks (16 colors / 32 bytes or 256 colors / 512 bytes). Streaming extraction and identification are integrated into `wszst extract` and `wszst filetype`.

## Level-5 / Professor Layton compression containers (RL / LZ10 / HUFF8)

Retail sample:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Professor Layton and the Curious Village (USA, Australia).zip
```

| Sample | Coverage | Result |
| --- | --- | --- |
| Professor Layton and the Curious Village | 1,770 LZ10 files (`.arc`, `.arj` with prefix 2) | 100% (1,770/1,770) decompressed cleanly via `wszst decompress` |
| Professor Layton and the Curious Village | 58 RL files (`.arc`, `.arj` with prefix 1) | 100% (58/58) decompressed byte-for-byte exact via `wszst decompress` |
| Professor Layton and the Curious Village | 47 HUFF8 files (`.arc`, `.arj` with prefix 4) | 100% (47/47) decompressed cleanly via `wszst decompress` |

Level-5 titles on Nintendo DS prefix standard GBA/DS BIOS compression streams (Run Length `0x30`, LZ10 `0x10`, Huffman-4 `0x24`, Huffman-8 `0x28`) with a 4-byte little-endian method header (`0x00000001` for RL, `0x00000002` for LZ10, `0x00000003` for Huff4, `0x00000004` for Huff8). `DetectNintendoFormat` automatically unwraps these 4-byte headers and routes them to their native decompressors, matching uncompressed reference implementations byte-for-byte.




