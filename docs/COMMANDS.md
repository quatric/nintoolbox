# Command Reference & New Tools Guide

This document provides a comprehensive reference for all new commands, standalone tools, and command-line options introduced in **Wiimms SZS Tools Plus** beyond upstream Wiimms SZS Tools (v2.42a).

For format specifications, see **[FORMATS.md](FORMATS.md)**. For the recursive unpacking and incremental rebuild workflow, see **[WORKFLOWS.md](WORKFLOWS.md)**.

---

## Tool Overview Matrix

| Binary | Tool Name | Focus Area | Key Commands / Functions | Platforms & Context |
|---|---|---|---|---|
| **`wszst`** | Wiimms SZS Tool | Universal Game Archives & Containers | `xx`, `CREATE`, `EXTRACT`, `WC24DECRYPT`, `WC24ENCRYPT`, `BCH`, `SPRITES`, `BMS`, `COMPRESS`, `DECOMPRESS` | All Nintendo platforms (GC, Wii, DS, 3DS, Wii U, Switch) |
| **`wmdlt`** | Wiimms Model Tool | 3D Models & Geometry Conversion | `DECODE`, `ENCODE`, `CAT`, `STRINGS`, `GEOMETRY` | MDL0, HSD, HSF, NSBMD, CGFX, BCRES, BCH, BFRES, NUD, NUMSHB, MOD, MSH, etc. $\leftrightarrow$ GLB / DAE |
| **`wbrsar`** | Wiimms Sound Archive Tool | Sound Archives & Audio Extraction | *Default* (MIDI + SF2/DLS), `unpack`, `pack` | Wii BRSAR, Wii U BFSAR, 3DS BCSAR, NDS SDAT |
| **`wbfsar`** | Wiimms BFSAR/BCSAR Tool | Sound Archive Directory Listing | `dump` | Wii U / Switch (FSAR) and 3DS (CSAR) directory XML dump |
| **`wlayt`** | Wiimms Layout Tool | 2D Layouts & Animations | `decode`, `encode` | BRLYT/BRLAN (Wii), BFLYT/BFLAN (Wii U/Switch), BCLYT/BCLAN (3DS) |
| **`wbrstm`** | Wiimms Stream Audio Tool | Multi-Channel Stream Audio | `to_wav`, `from_wav`, `info` | BRSTM (Wii), BFSTM (Wii U/Switch), BCSTM (3DS) $\leftrightarrow$ WAV |
| **`wseqt`** | Wiimms Sequence Audio Tool | Sequence Bytecode & MIDI | `disasm`, `asm`, `to_midi`, `from_midi`, `invert`, `info` | RSEQ (Wii), CSEQ (3DS), FSEQ (Wii U/Switch), SSEQ (NDS), BMS $\leftrightarrow$ MIDI / Text |
| **`wrbnk`** | Wiimms RBNK Tool | Instrument Banks | `dump`, `compile` / `encode` | Wii RBNK instrument bank $\leftrightarrow$ XML |
| **`wwc24crypt`** | WiiConnect24 Crypto Utility | WC24 Decryption & Signing | `wc24-decrypt`, `wc24-encrypt`, `selftest` | WiiConnect24 AES-128-OFB + RSA-SHA1 content |
| **`wbmsx`** | QuickBMS Script Runner | Scripted Binary Extraction | `<script.bms> <input> <dest>` | Embedded QuickBMS interpreter for arbitrary formats |
| **`wmpbdump`** | Mario Party Archive Unpacker | Chunked Archive Extraction | `<input.bin> [output_dir]` | Hudson Soft GameCube/Wii Mario Party chunk archives |
| **`wmpbpack`** | Mario Party Archive Packer | Chunked Archive Repacking | `<input_dir> <output.bin>` | Hudson Soft GameCube/Wii Mario Party chunk archives |
| **`wimgt`** | Wiimms Image Tool | Textures & 2D Graphics | `DECODE`, `ENCODE`, `CONVERT` | BNTX, NUTEXB, BFLIM, GTX, BCLIM, CTPK, NCGR/NCLR, NSBTX, DSB, AJPG, NUT, XIMG, G1T, etc. |
| **`wbmgt`** | Wiimms Binary Message Tool | In-Game Text & Message Flow | `DECODE`, `ENCODE`, `LIST`, `CAT` | MSBT (with LBL1 labels), MSBP, MSBF, and extended BMG (FLI/FLW flow sections) |

---

## 1. New Commands in `wszst`

### `wszst WC24DECRYPT` / `wszst WC24D`

Decrypts WiiConnect24 content files (news, weather, mail, and title update payloads).

```bash
wszst WC24DECRYPT <source> <dest> <key> [--overwrite]
# Alias:
wszst WC24D <source> <dest> <key>
```

#### Parameters & Keys
- `<source>`: Encrypted WC24 file.
- `<dest>`: Decrypted output destination path.
- `<key>`: Encryption key supplied as one of:
  - 32-character hexadecimal string (`0123456789abcdef0123456789abcdef`).
  - 16-byte raw binary key file.
  - 544-byte `wc24pubk.mod` blob containing the AES key at byte offset 512.

#### Technical Details
WC24 files use AES-128-OFB cipher mode (not CBC). The initialization vector (IV) is read from byte offset 48, and the encrypted payload begins at byte offset 320.

#### Examples
```bash
# Decrypt using a 32-character hex key string:
wszst WC24DECRYPT forecast.enc forecast.bin 0123456789abcdef0123456789abcdef

# Decrypt using a wc24pubk.mod key blob:
wszst WC24DECRYPT nwc24msg.cb nwc24msg.xml /etc/wc24pubk.mod --overwrite
```

---

### `wszst WC24ENCRYPT` / `wszst WC24E`

Encrypts and cryptographically signs data into the WiiConnect24 container format.

```bash
wszst WC24ENCRYPT <source> <dest> <key> <rsa-key> [iv] [--overwrite]
# Alias:
wszst WC24E <source> <dest> <key> <rsa-key> [iv]
```

#### Parameters
- `<source>`: Plaintext payload to encrypt.
- `<dest>`: Encrypted and signed WC24 output file.
- `<key>`: AES key (32-character hex string, 16-byte file, or 544-byte `wc24pubk.mod` blob).
- `<rsa-key>`: Path to a PKCS#1 RSA private key in PEM format (`-----BEGIN RSA PRIVATE KEY-----`) used to generate the 256-byte RSA-SHA1 signature.
- `[iv]`: Optional 16-byte IV (hex string or file). If omitted, a cryptographically secure random IV is generated.

#### Examples
```bash
wszst WC24ENCRYPT news.xml news.enc wc24.key wc24_private.pem --overwrite
```

---

### `wszst BCH`

Inspects and lists the internal structure of Nintendo 3DS CTR H3D (`.bch`) files.

```bash
wszst BCH <file.bch>... [--ignore]
```

#### Description
Because BCH files store unrelocated memory addresses, `wszst BCH` parses the header, resolves and applies the internal PICA200 relocation tables, and dumps an address-mapped index of:
- 3D models and skeleton hierarchies
- Materials and texture references
- Shaders and shader programs
- Lookup tables (LUTs)
- Skeletal and camera animations

#### Examples
```bash
wszst BCH Character.bch
wszst BCH romfs/models/*.bch --ignore
```

---

### `wszst SPRITES` / `wszst SPR`

Composites and renders Nintendo DS 2D sprite sets to PNG.

```bash
wszst SPRITES <source>... [--dest <dir>] [--overwrite]
# Alias:
wszst SPR <source>...
```

#### Description
Takes a directory or one member of a Nintendo DS sprite set. `wszst` automatically discovers matching companion files sharing the same base name:
- `.ncgr`: Nitro Character Graphics (pixel tiles)
- `.nclr`: Nitro Color Palette
- `.ncer`: Nitro Cell definitions (sprite boundaries and part layout)
- `.nanr`: Nitro Animation Sequences (frame timings and transformations)

It composites individual NCER cells into transparent PNG images and renders full NANR animation sequences as sequentially numbered PNG frames.

#### Examples
```bash
# Render all sprites in a directory:
wszst SPRITES romfs/battle/sprites/ --dest extracted_sprites/

# Render from an individual NCER cell definition file:
wszst SPRITES player.ncer --dest player_frames/
```

---

### `wszst BMS`

Executes a QuickBMS extraction script directly against a binary archive.

```bash
wszst BMS <script.bms> <source> <dest-dir> [--overwrite]
```

#### Description
Executes QuickBMS scripts using a built-in interpreter. Supports core statements:
- Flow & Variables: `SET`, `MATH`, `FOR` / `NEXT`, `IF` / `ELSE` / `ENDIF`, `PRINT`
- Seeking & Reading: `GOTO`, `SAVEPOS`, `IDSTRING`, `GET`, `GETDSTRING`, `ENDIAN`
- Writing & Output: `LOG`, `CLOG`
- Codecs (`COMTYPE`): `copy`, `lz10`, `lz11`, `yay0`, `zlib`, `deflate`, `ash0`, `rl`, `lzh8`, `quicklz`, `at7`

#### Examples
```bash
wszst BMS unpack_custom.bms archive.bin output_dir/ --overwrite
```

---

## 2. Extended Features in Classic `wszst` Commands

### Universal Recursive Extraction (`wszst xx` / `wszst EXTRACT`)

`wszst xx` (or `wszst EXTRACT`) recursively traverses nested archives and decodes assets into editable companions (PNG for textures, GLB for models, XML/text for data).

```bash
wszst xx <source> [--dest <dir>] [--auto] [--overwrite] [delegation options]
```

#### New Options
- `--auto`: Recursively scans extracted trees and decompresses recognized raw Nintendo compression streams (LZ10, LZ11, Yay0, Yaz0, ASH0, RLE, etc.).
- `--parent=<file>`: Specifies a parent model file (`.brres` or `.mdl0`) to associate with extracted/decoded geometry.
- `--max-file-size=<size>`: Default file limit increased from 100 MiB to 512 MiB (accepts units `m`, `g`).
- `--bms=<script.bms>`: Chains a QuickBMS script to unpack unsupported container formats within the recursive walk.
- `--no-passthrough`: Disables external companion tool delegation, keeping all processing strictly native.
- External tool delegation paths:
  - `--with-wit=<path>`: Custom path to `wit` (Wii/GameCube disc extraction).
  - `--with-mobipeg=<path>`: Custom path to `mobipeg` (MobiClip & DSP-ADPCM processing).
  - `--with-ndstool=<path>`: Custom path to `ndstool` (Nintendo DS ROM extraction).
  - `--with-ctrtool=<path>`: Custom path to `ctrtool` (Nintendo 3DS CIA/NCCH/RomFS extraction).
  - `--with-hactool=<path>`: Custom path to `hactool` (Nintendo Switch NCA/PFS0 extraction).
  - `--with-hacbrewpack=<path>`: Custom path to `hacbrewpack`.
  - `--with-sharpii=<path>`: Custom path to `sharpii` (Wii WAD unpacking).
  - `--with-7z=<path>`: Custom path to `7z` / `7zz`.

#### Newly Supported Archive Formats
`wszst xx` and `wszst EXTRACT` natively recognize and extract:
- Nintendo standard: **SARC** (Big-Endian & Little-Endian), **DARC**, **PAC / MRG**, **RARC**
- GameCube / Wii: **FSYS** (Pokémon Colosseum/XD), **F9RES**, **MDR** (DDR Mario Mix), **PVOL** (Pikmin), **MPBIN** (Mario Party), **ZTAB** (Camelot), **ARCV** (Pac-Man Party), **AT7** (Koei Tecmo), **BIGF** (EA), **VCRA**
- Nintendo DS / 3DS / Wii U: **GFA** / **BPE** (Good-Feel), **XPCK** (Level-5), **STPK** (Jump Super Stars), **ZLARC** (NES Remix), **BG4** (Mario & Luigi), **CRAM** (Xenoblade 3D), **WARC** (Game & Wario), **CA01** / **SA01** (Mii Maker)

---

### Selective Incremental Rebuilding (`wszst CREATE`)

Rebuilds an extracted directory tree back into an archive container with bottom-up caching.

```bash
wszst CREATE <extracted.d> --dest <output.szs|output.sarc|output.wbfs> [--overwrite]
```

#### Key Capabilities
- **SHA-1 Cache (`.wszst-cache.txt`)**: Re-encodes only modified assets. Unchanged files are reused as-is, preventing generational recompression degradation.
- **Native Target Formats**: Can create and pack `.szs` (Yaz0 / U8), `.sarc` (BE / LE), `.darc`, `.pac`, `.gfa`, `.mdr`, `.pvol`, `.xpck`, `.stpk`, `.ztab`, `.f9res`, `.zlarc`, and `.big`.

---

### Codec Compression & Decompression (`wszst COMPRESS` / `DECOMPRESS`)

Direct stream compression and decompression for Nintendo algorithms:

```bash
# Decompress any recognized stream:
wszst DECOMPRESS stream.lz11 --dest stream.bin
wszst DECOMPRESS stream.ash0 --dest stream.bin

# Compress raw data:
wszst COMPRESS raw.bin --dest raw.lz10
wszst COMPRESS raw.bin --dest raw.lz11
wszst COMPRESS raw.bin --dest raw.yay0
wszst COMPRESS raw.bin --dest raw.ash0
wszst COMPRESS raw.bin --dest raw.lh      # LZH8 alias
```

---

## 3. Standalone 3D Model Tool: `wmdlt`

`wmdlt` handles decoding, encoding, inspection, and geometry injection for 3D model formats across GameCube, Wii, Nintendo DS, 3DS, Wii U, and Nintendo Switch.

### Supported Model Formats
- **Nintendo standard**: `MDL0` / `BRRES` (Wii NW4R), `BFRES` (Wii U / Switch), `CGFX` / `BCRES` (3DS NW4C), `BCH` (3DS CTR H3D), `NSBMD` / `BMD` (DS Nitro 3D)
- **GameCube engines**: HAL `HSD` (`.dat`), Hudson Soft `HSF` (`.hsf`), Camelot `HGO`, Next Level `GLG`, Pokémon `PERS`
- **Smash Bros**: Bandai Namco `NUD` (Smash 4 Wii U/3DS), `NUMSHB` / SSBH (Smash Ultimate Switch)
- **Other engines**: Monster Games `MOD` and `MSH` (Excite Truck/Bots), Nd Cube `BNFM` (Mario Party 10 / Amiibo Festival), Koei Tecmo `G1M`, Level-5 `G4PKM`, DeNA `LMD`

### `wmdlt DECODE` / `wmdlt DEC`

Converts Nintendo 3D models to standard **GLB** (`.glb`) or **COLLADA** (`.dae`).

```bash
wmdlt DECODE <model-file> [--dest <output.glb|output.dae>] [--overwrite]
# Short form:
wmdlt DEC Mario.mdl0 --dest Mario.glb
```

#### Examples
```bash
# Convert Wii NW4R model to modern GLB:
wmdlt DECODE Course.mdl0 --dest Course.glb

# Convert Wii U BFRES model to GLB:
wmdlt DECODE Player.bfres --dest Player.glb

# Convert GameCube Hudson HSF model:
wmdlt DECODE Character.hsf --dest Character.glb

# Convert Nintendo DS NSBMD model to COLLADA DAE:
wmdlt DECODE Map.nsbmd --dest Map.dae
```

---

### `wmdlt ENCODE` / `wmdlt ENC`

Encodes standard GLB or DAE models back into Nintendo formats, or injects geometry into existing binary parent models.

```bash
# 1. Direct model encoding (HSF, HSD, MSH, MOD, Switch BFRES):
wmdlt ENCODE <model.glb|model.dae> --dest <output.hsf|output.dat|output.bfres>

# 2. Geometry injection into an existing parent model:
wmdlt ENCODE <model.glb|model.dae> --parent=<parent.brres|parent.mdl0> --dest <output.brres|output.mdl0>
```

#### Geometry Injection Workflow
When creating custom tracks or character models for Wii games (such as *Mario Kart Wii* or *Super Smash Bros. Brawl*), `wmdlt` injects new vertices, polygons, UV coordinates, and normals from a `.glb` or `.dae` into an existing `BRRES` or `MDL0` template file. This preserves original bones, node structures, material settings, and shader stages.

#### Examples
```bash
# Encode a custom model to Hudson HSF:
wmdlt ENCODE custom.glb --dest mario.hsf

# Encode a custom model to HAL Sysdolphin HSD (.dat):
wmdlt ENCODE custom.glb --dest trophy.dat

# Inject custom geometry into a Mario Kart Wii track model:
wmdlt ENCODE track.dae --parent=course_model.brres --dest modified_course.brres --overwrite
```

---

### `wmdlt CAT`, `STRINGS` & `GEOMETRY`

Model diagnostics and metadata inspection.

```bash
# Print decoded model structure as text to stdout:
wmdlt CAT Mario.mdl0

# List all material names, bone names, and texture references:
wmdlt STRINGS Course.bfres
wmdlt STRINGS track.szs/course_model.brres

# Inspect and validate vertex and polygon geometry sections:
wmdlt GEOMETRY Course.mdl0
```

---

## 4. Standalone Audio Tools

### `wbrsar` (Sound Archives: BRSAR, BFSAR, BCSAR, SDAT)

Converts Nintendo sound archives to MIDI and SoundFont instruments, or unpacks/packs raw audio assets.

```bash
# 1. Default: Convert all sequenced music to MIDI + SoundFont (.sf2):
wbrsar <sound.brsar> [output_dir] [--sf2] [--dls] [--both]

# 2. Unpack raw asset files (RSEQ, RBNK, RWAR, RWSD, SSEQ, SBNK, SWAR):
wbrsar unpack <sound.brsar|.bfsar|.bcsar|.sdat> [output_dir]

# 3. Rebuild / Pack sound archives from asset directory:
wbrsar pack <input_dir> [output.brsar]
wbrsar pack <input_dir> [output.bfsar] --bfsar
wbrsar pack <input_dir> [output.bcsar] --bcsar
wbrsar pack <input_dir> [output.sdat]  --sdat
```

#### Examples
```bash
# Convert a Wii sound archive to MIDI sequences + SoundFont:
wbrsar sound/eulaSound.brsar extracted_music/ --sf2

# Unpack a Nintendo DS SDAT archive into constituent assets:
wbrsar unpack sound_data.sdat sdat_assets/

# Rebuild an SDAT archive after editing SSEQ sequences:
wbrsar pack sdat_assets/ sound_data_custom.sdat --sdat
```

---

### `wbfsar` (BFSAR / BCSAR Directory Examiner)

Dumps directory listings and symbol mappings from Wii U, Switch (`.bfsar`), and 3DS (`.bcsar`) sound archives.

```bash
wbfsar dump <input.bfsar|.bcsar> [output.xml]
```

#### Output Content
Generates structured XML containing every sound item, sound group, bank, wave archive, and player entry along with its ID and string identifier.

---

### `wbrstm` (Stream Audio: BRSTM, BFSTM, BCSTM $\leftrightarrow$ WAV)

Decodes and encodes multi-channel Nintendo stream audio files.

```bash
# 1. Decode stream to standard WAV:
wbrstm to_wav <input.brstm|.bfstm|.bcstm> [output.wav]

# 2. Encode standard WAV to Nintendo stream audio:
wbrstm from_wav <input.wav> [output.brstm] [--pcm]
wbrstm from_wav <input.wav> [output.bfstm] --bfstm
wbrstm from_wav <input.wav> [output.bcstm] --bcstm

# 3. Inspect stream properties and loop points:
wbrstm info <input.brstm|.bfstm|.bcstm>
```

#### Technical Notes
- By default, `from_wav` compresses audio using Nintendo DSP-ADPCM. If `mobipeg` is available, it transparently passes through to mobipeg's coefficient-search encoder for bit-exact retail parity.
- `--pcm` forces uncompressed 16-bit PCM encoding.

#### Examples
```bash
# Convert background music to WAV:
wbrstm to_wav bgm_01.brstm bgm_01.wav

# Encode custom music with loop points to Wii U BFSTM:
wbrstm from_wav custom_bgm.wav custom_bgm.bfstm --bfstm

# View sample rate, channel count, and loop sample points:
wbrstm info bgm_01.brstm
```

---

### `wseqt` (Sequence Bytecode & MIDI Tool)

Compiles, disassembles, and converts sequence music across Wii (RSEQ), 3DS (CSEQ), Wii U / Switch (FSEQ), and Nintendo DS (SSEQ).

```bash
# 1. Disassemble binary sequence to MML / text assembly:
wseqt disasm <input.rseq|cseq|fseq|sseq|bms> [output.txt]

# 2. Assemble text assembly to binary sequence:
wseqt asm <input.txt> [output] [--format <fmt>]

# 3. Convert sequence to standard MIDI:
wseqt to_midi <input.rseq|cseq|fseq|sseq|bms> [output.mid]

# 4. Compile standard MIDI into sequence bytecode:
wseqt from_midi <input.mid> [output] [--format <fmt>]

# 5. Musical note-inversion transform around a center pitch:
wseqt invert <input> [output] [--center <note>]

# 6. Display sequence track and header metadata:
wseqt info <input>
```

#### Supported Formats for `--format`
- `RSEQ`: Nintendo Wii (standard NW4R)
- `CSEQ`: Nintendo 3DS (NW4C)
- `FSEQ`: Nintendo Wii U (Big-Endian NW4F)
- `FSEQ_LE`: Nintendo Switch (Little-Endian NintendoSDK)
- `SSEQ`: Nintendo DS (Nitro)
- `BMS`: JAudio BMS sequence

#### Examples
```bash
# Convert a Wii RSEQ sequence to standard MIDI:
wseqt to_midi fan_fare.rseq fan_fare.mid

# Convert a MIDI file to a Nintendo Switch sequence:
wseqt from_midi theme.mid theme.fseq --format FSEQ_LE

# Disassemble a Nintendo DS sequence to editable assembly text:
wseqt disasm town.sseq town_assembly.txt
```

---

### `wrbnk` (RBNK Instrument Bank Tool)

Inspects and compiles Wii NW4R RBNK instrument sound bank files.

```bash
# 1. Dump instrument definitions to XML:
wrbnk dump <input.rbnk> [output.xml]

# 2. Compile XML instrument definitions back to binary RBNK:
wrbnk compile <input.xml> [output.rbnk]
# Alias:
wrbnk encode <input.xml> [output.rbnk]
```

---

## 5. Standalone Layout & Crypto Tools

### `wlayt` (Wiimms Layout Tool)

Decodes and compiles 2D binary layout (`.brlyt`, `.bflyt`, `.bclyt`) and animation (`.brlan`, `.bflan`, `.bclan`) files.

```bash
# Decode binary layout or animation to editable text:
wlayt decode <input.brlyt|input.bflyt|input.bclyt> [output.txt]
wlayt decode <input.brlan|input.bflan|input.bclan> [output.txt]

# Compile text back to binary layout or animation:
wlayt encode <input.txt> [output.brlyt|output.bflyt|output.bclyt]
wlayt encode <input.txt> [output.brlan|output.bflan|output.bclan]
```

#### Examples
```bash
# Decode a Wii U HUD layout to text:
wlayt decode hud.bflyt hud.txt

# Recompile the edited HUD text:
wlayt encode hud.txt hud.bflyt
```

---

### `wwc24crypt` (WiiConnect24 Crypto Utility)

Standalone cryptographic utility for WiiConnect24 encrypted network content.

```bash
# Decrypt WC24 content:
wwc24crypt wc24-decrypt <infile> <outfile> <key-hex32 | keyfile>

# Encrypt and sign WC24 content:
wwc24crypt wc24-encrypt <infile> <outfile> <key-hex32 | keyfile> <rsa-key.pem> [iv-hex32 | ivfile]

# Verify crypto engine implementation against test vectors:
wwc24crypt selftest
```

---

### `wbmsx` (QuickBMS Script Runner)

CLI utility for executing QuickBMS extraction scripts against unsupported container formats.

```bash
wbmsx <script.bms> <input_file> <output_dir>
```

#### Environment Variables
- `WBMSX_QUICKBMS`: Explicit path to an external QuickBMS executable if overriding the bundled native engine.

---

### `wmpbdump` & `wmpbpack` (Mario Party Archive Tools)

Dedicated tools for Hudson Soft GameCube/Wii `MPBIN` (`.bin`) chunked archives with LZSS, Slide, RLE, and Inflate compression.

```bash
# Unpack Mario Party chunked archive:
wmpbdump <input.bin> [output_dir]

# Repack directory back into Mario Party chunked archive:
wmpbpack <input_dir> <output.bin>
```

---

## 6. Enhancements to Image & Message Tools

### `wimgt` (New Texture Formats)

`wimgt DECODE` and `wimgt ENCODE` support expanded modern texture formats:
- **Nintendo Switch**: `BNTX` (Tegra block-linear surface container), `NUTEXB`
- **Nintendo Wii U**: `BFLIM` (GX2 formats: BC1, BC2, BC3, BC4, BC5, RGBA8), `GTX`
- **Nintendo 3DS**: `BCLIM` (CTR formats: L8, A8, LA4, LA8, RGB565, RGB8, RGBA8, ETC1, ETC1A4), `CTPK`
- **Nintendo DS**: `NCGR` (tile sheets with palette integration), `NCLR` (palettes), `NSBTX` (3D textures), `DSB` (Animal Crossing Wild World), `AJPG` / `AJJPG`
- **Bandai Namco**: `NUT` (Super Smash Bros. 4)
- **Monster Games**: `ART` / `IMG` GUI textures, `TEX` GX textures
- **Wii System**: `BNR1` (opening banner icons and sound wrappers), `BREFT`, `BRFNT` glyph sheets (TGLP)

```bash
# Decode Switch BNTX texture to PNG:
wimgt DECODE texture.bntx --dest texture.png

# Encode PNG to Wii U BFLIM texture:
wimgt ENCODE texture.png --dest texture.bflim

# Decode Nintendo DS tile sheet and palette:
wimgt DECODE tiles.ncgr --dest tiles.png
```

---

### `wbmgt` (Message Studio & Flow Messages)

`wbmgt DECODE`, `ENCODE`, and `LIST` support Nintendo Message Studio formats:
- **`MSBT` (Message Studio Binary Text)**: Decodes UTF-8 and UTF-16 strings, control escape codes, and `LBL1` label tables. Round-trips cleanly through `.tmsbt` text format.
- **`MSBP` & `MSBF`**: Message Studio project definitions and flow graph logic.
- **Enhanced BMG**: Preserves flow sections (`FLI`, `FLW`), `INF2`, `TBN`, and `WII` attributes with bit-exact round-trip preservation.

```bash
# Export all message labels from an MSBT file:
wbmgt LIST GameText.msbt

# Decode MSBT to human-readable text:
wbmgt DECODE GameText.msbt --dest GameText.tmsbt

# Compile edited text back to MSBT:
wbmgt ENCODE GameText.tmsbt --dest GameText.msbt
```
