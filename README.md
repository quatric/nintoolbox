# Wiimms SZS Tools Plus

A fast, unified command-line toolkit to extract, modify, convert, and rebuild game archives, textures, 3D models, audio, and layouts across **GameCube, Wii, Nintendo DS, 3DS, Wii U, and Nintendo Switch**.

---

## Quick Start

### Installation & Building

```bash
git clone https://github.com/quatric/wiimms-szs-tools-plus.git
cd wiimms-szs-tools-plus/project
make all -j$(nproc)
```

Compiled binaries (`wszst`, `wimgt`, `wmdlt`, `wbrsar`, `wbmgt`, `wlayt`, `wctct`, `wkclt`, `wkmpt`) will be placed in `project/bin/`.

---

## Common Commands

```bash
# 1. Extract any archive or ROM (SZS, U8, RARC, SARC, NARC, DARC, NDS, etc.)
wszst xx Track.szs
wszst xx Game.nds

# 2. Rebuild an extracted directory back into an archive
wszst CREATE Track.d --dest Track.szs

# 3. Convert 3D models to standard GLB (.glb)
wmdlt DECODE Mario.mdl0 --dest Mario.glb
wmdlt DECODE Course.bfres --dest Course.glb
wmdlt ENCODE Mario.glb --dest Mario.hsf

# 4. Convert Nintendo textures to PNG
wimgt DECODE texture.tpl --dest texture.png
wimgt DECODE texture.bntx --dest texture.png
wimgt ENCODE texture.png --dest texture.tpl

# 5. Extract sound archives and convert audio streams
wbrsar unpack Sound.brsar --dest Sound.d
wbrstm DECODE music.brstm --dest music.wav
wseqt DECODE sequence.sseq --dest sequence.mid
```

---

## Supported Formats by Category

### Archives & Containers

| Format | Extensions | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- |
| **APAK** | `.apak` | ✅ | ✅ | ✅ | — | Nintendo / Pokémon APAK archive format (Wii U / Switch) |
| **ARC / U8** | `.arc`, `.szs` | ✅ | ✅ | ✅ | — | Nintendo standard U8 archive (Wii / GameCube NintendoWare & EAD) |
| **ARCV** | `.arc` | ✅ | ✅ | ✅ | — | Namco / Tose Wii archive format |
| **Arika Archive** | `INFO.DAT`, `GAME.DAT`, `.arika` | ✅ | ✅ | ✅ | — | Arika DS / DSi / Wii archive system |
| **AT7** | `.at7` | ✅ | ✅ | ✅ | — | Koei Tecmo container format (Wii / PS2) |
| **BG4** | `.bg4` | ✅ | ✅ | ✅ | — | AlphaDream 3DS flat archive with BLZ member compression |
| **BIGF** | `.big` | ✅ | ✅ | ✅ | — | Electronic Arts Wii asset archive |
| **CA01 / SA01** | `.ca01`, `.sa01` | ✅ | ✅ | ✅ | — | Nintendo Network Mii & amiibo system archive (3DS / Wii U) |
| **CCF** | `.ccf` | ✅ | ✅ | ✅ | — | Nintendo Virtual Console container (Wii / Switch) |
| **CRAM** | `.arc`, `.cram` | ✅ | ✅ | ✅ | — | Monolith Soft 3DS flat archive container |
| **DARC** | `.darc` | ✅ | ✅ | ✅ | — | NintendoWare NW4C differential archive (3DS) |
| **DTLS** | `dt00`, `ls00`, `.ls` | ✅ | ✅ | ✅ | — | Bandai Namco composite package & lookup archive (*Super Smash Bros. 4* Wii U / 3DS) |
| **F9RES** | `.res` | ✅ | ✅ | ✅ | — | GameCube resource archive container |
| **FSYS** | `.fsys` | ✅ | ✅ | ✅ | — | Genius Sonority archive system (GameCube / Wii) |
| **GAR / ZAR** | `.zar`, `.gar` | ✅ | ✅ | ✅ | — | Grezzo Zelda & Luigi's Mansion archive (*OoT3D*, *MM3D*, *LM3DS*) |
| **GFA** | `.gfa` | ✅ | ✅ | ✅ | ✅ | Good-Feel GFAC container (Wii / 3DS) |
| **Hyrule Warriors** | `.idx`, `.bin` | ✅ | ✅ | ✅ | — | Koei Tecmo / Omega Force split index archive (3DS) |
| **JARC** | `.jarc` | ✅ | ✅ | ✅ | — | Level-5 DS archive container (DS) |
| **LSPK** | `.pk`, `.pkh`, `.lspk` | ✅ | ✅ | ✅ | — | Level-5 / Mistwalker flat package (*The Last Story*) |
| **MDR** | `.mdr` | ✅ | ✅ | ✅ | — | *Dance Dance Revolution Mario Mix* chunk archive with per-chunk zlib streams |
| **MKGPDX PAC** | `.pac`, `.mkgpdx` | ✅ | ✅ | ✅ | — | *Mario Kart Arcade GP DX* layout archive (`pack`) |
| **MPBIN** | `.bin` | ✅ | ✅ | ✅ | — | Hudson Soft Mario Party archive container (GameCube / Wii) |
| **MTXT** | `.mtxt` | ✅ | ✅ | ✅ | — | Nintendo Switch MTXT texture archive (gzip-wrapped XTX) |
| **NARC** | `.narc` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro standard archive (DS / DSi) |
| **NCCARC** | `.nccarc` | ✅ | ✅ | ✅ | — | Nintendo DS flat blob container |
| **NDS / SRL / DSI** | `.nds`, `.srl`, `.dsi` | ✅ | — | — | — | Nintendo DS & DSi ROM images and executables |
| **NXARC** | `.nxarc` | ✅ | ✅ | ✅ | — | Nintendo Switch NX archive (`RAXN`) |
| **PAC / MRG** | `.pac`, `.mrg` | ✅ | ✅ | ✅ | — | HAL Laboratory / Game Arts Wii archive container |
| **PKG / GPKG / GPAK** | `.pkg`, `.pak`, `.gpak` | ✅ | ✅ | ✅ | — | Sonic Team Storybook series archive (*Secret Rings* / *Black Knight*) |
| **PKZ** | `.pkz` | ✅ | ✅ | ✅ | — | PlatinumGames archive format (*Bayonetta*, *Astral Chain*) |
| **PVOL** | `.pvol` | ✅ | ✅ | ✅ | — | *Pikmin 1 & 2* model & resource container archive |
| **RARC** | `.rarc`, `.arc` | ✅ | ✅ | ✅ | — | Nintendo standard resource archive (GameCube / Wii) |
| **RFL_Res** | `RFL_Res.dat`, `.dat` | ✅ | ✅ | ✅ | — | Revolution Face Library Mii resource database (Wii / 3DS / Wii U) |
| **RPAK** | `.rpak`, `.pak` | ✅ | — | — | — | Retro Studios asset container (*Metroid Prime* / *Donkey Kong Country Returns*) |
| **RST / TOC** | `.rst`, `.toc` | ✅ | ✅ | ✅ | — | Monster Games archive & table of contents (*Excite Truck* / *Excitebots*) |
| **SARC** | `.sarc`, `.szs` | ✅ | ✅ | ✅ | — | NintendoWare NW4F & NintendoSDK sorted archive (Wii U / Switch / 3DS) |
| **SIR0** | `.sir0` | ✅ | ✅ | ✅ | — | Pokémon Mystery Dungeon resource container (DS / 3DS) |
| **STPK** | `.srd`, `.stpk` | ✅ | ✅ | ✅ | — | *Jump Super Stars* & *Jump Ultimate Stars* DS resource archive |
| **SZE** | `.sze` | ✅ | ✅ | ✅ | — | Nintendo Switch AES-encrypted container (NST / Switch) |
| **TMPK** | `.pack`, `.tmpk` | ✅ | ✅ | ✅ | — | *The Legend of Zelda: Twilight Princess HD* archive (`TMPK`) |
| **VCRA** | `.bin`, `.vcra` | ✅ | ✅ | ✅ | — | Bandai Namco Museum Remix archive format (Wii) |
| **VIBS** | `.vibs` | ✅ | ✅ | ✅ | — | Nintendo Switch Joy-Con vibration archive |
| **WARC** | `.warc` | ✅ | ✅ | ✅ | — | Nintendo / Intelligent Systems flat archive (Wii U) |
| **WUD / WUX** | `.wud`, `.wux` | ✅ | — | — | — | Nintendo Wii U optical disc images (raw & compressed) |
| **XPCK** | `.xc`, `.xpck` | ✅ | ✅ | ✅ | — | Level-5 container archive (*Inazuma Eleven*, *Professor Layton*, *Yo-kai Watch*) |
| **ZLARC** | `.zlarc` | ✅ | ✅ | ✅ | — | indieszero compressed package archive (*NES Remix*, *NES Remix 2*, *NES Remix Pack*) |
| **ZTAB** | `.ztab`, `.tab` | ✅ | ✅ | ✅ | ✅ | Camelot archive table (*Mario Golf: Toadstool Tour*, *Mario Power Tennis*) |

`Byte-Exact Roundtrip` = build → extract → rebuild reproduces the archive's bytes
identically, so the writer's canonical layout is a fixed point of its own reader.
Exercised by `t_container_roundtrip()` in `tests/regress.sh`.

---

### 3D Models & Geometry

| Format | Extensions | Target Output | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **BCH** | `.bch` | **GLB** | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C H3D binary character model (3DS) |
| **BMD** | `.bmd`, `.bdhc` | **GLB** | ✅ | ✅ | ✅ | — | Early Nintendo DS 3D model format (DS) |
| **BNFM** | `.bnfm` | **GLB** | ✅ | ✅ | ✅ | — | Nd Cube Wii U 3D model format (*Animal Crossing: Amiibo Festival*, *Mario Party 10*) |
| **G1M** | `.g1m` | **GLB** | ✅ | — | — | ✅ | Koei Tecmo 3D model format (*Hyrule Warriors*, *Fire Emblem Warriors*). Positions, normals and UVs. Vertex colour is parsed but not exported; all 600 models checked carry a single bone and no blend attributes, so there is no skinning in this corpus to export |
| **G4PKM** | `.g4pkm` | — | — | — | — | — | Identified by filename only: the type table carries no magic for it and nothing decodes it. The extension occurs in none of the games checked here, including a Level-5 3DS title (*Inazuma Eleven 3*, which uses `.pkh`/`.pkb`), so the format this names is unconfirmed |
| **GLG / RLG** | `.glg`, `.rlg` | **GLB** | ✅ | ✅ | ✅ | ✅ | Next Level Games 3D model format (*Super Mario Strikers*, *Mario Strikers Charged*) |
| **HGO** | `.hgo` | **GLB** | — | — | — | — | Camelot 3D model format. The `0OGH` magic this row claims occurs in none of *Mario Golf: Toadstool Tour* (GC), *Mario Power Tennis* (GC & Wii), *We Love Golf!* (Wii) or *Mario Golf: World Tour* (3DS) — neither as a file magic nor embedded anywhere. Camelot ships models as PPC relocatable modules (`elfbin/xcmdl_*.sbn`) instead, so the source of this magic is unidentified |
| **HSD** | `.dat` | **GLB** | ✅ | ✅ | ✅ | ✅ | HAL Laboratory `sysdolphin` object graph (GameCube) |
| **HSF** | `.hsf` | **GLB** | ✅ | ✅ | ✅ | ✅ | Hudson Soft 3D model format (GameCube / Wii) |
| **LMD** | `.lmd` | — | — | — | — | — | Identified by filename only: no magic in the type table and no decoder. No source game was available to check the claim against |
| **MDL0 / BRRES** | `.mdl0`, `.brres` | **GLB** | ✅ | ✅ | — | ✅ | NintendoWare NW4R binary resource model (Wii) |
| **MOD** | `.mod` | **GLB** | ✅ | ✅ | ✅ | ✅ | Monster Games NDL3/NDL2 display list model (Wii) |
| **MSH (PMsh)** | `.msh` | **GLB** | ✅ | ✅ | ✅ | ✅ | Monster Games collision mesh format (Wii) |
| **NSBMD** | `.nsbmd`, `.bmd` | **GLB** | ✅ | ✅ | ✅ | — | Nintendo DS Nitro 3D model format (DS) |
| **NUD** | `.nud` | **GLB** | ✅ | ✅ | ✅ | — | Bandai Namco 3D model format (*Super Smash Bros. 4* Wii U / 3DS) |
| **NUMSHB** | `.numshb` | **GLB** | ✅ | — | — | ✅ | Bandai Namco SSBH 3D mesh model (*Super Smash Bros. Ultimate* Switch). MESH v1.10 only: positions, normals and UVs. v1.8 lays its object record out differently and is declined; tangents and colour sets are parsed but not exported |
| **PERS** | `.pers` | *(raw payload)* | ✅ | — | — | ✅ | Pokémon Stadium (N64) PERS-SZP container: a 24-byte header around a Yay0 stream. Verified against all 410 instances in the retail cart. The payload is not yet parsed, and the sibling `FRAGMENT` signature is a MIPS code overlay, not a model |

`Byte-Exact Roundtrip` = decode → GLB → re-encode reproduces the original file's bytes identically (canonical fixed-point verified), not just a successful encode.

---

### Textures & 2D Graphics

| Format | Extensions | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- |
| **ART / IMG** | `.art`, `.img` | ✅ | ✅ | ✅ | ✅ | Monster Games GUI image format (Wii) |
| **BCFNT / BFFNT / BRFNT** | `.bcfnt`, `.bffnt`, `.brfnt` | ✅ | ✅ | ✅ | ✅ | NintendoWare font resource (3DS / Wii U / Wii) |
| **BCLIM** | `.bclim` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C texture container (3DS) |
| **BFLIM** | `.bflim` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4F texture format (Wii U) |
| **BNTX** | `.bntx` | ✅ | ✅ | ✅ | ✅ | NintendoSDK Tegra block-linear texture container (Switch) |
| **BREFT** | `.breft`, `.bt-img` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R particle effect texture (Wii) |
| **BTI / TPL** | `.bti`, `.tpl` | ✅ | ✅ | ✅ | ✅ | Nintendo standard texture palette library (GameCube / Wii) |
| **Camelot GX bank** | *(none)*, `.stpl`, `.sbn` | ✅ | — | — | ✅ | Camelot GX texture bank, standalone or inline in a model module (*Mario Golf: Toadstool Tour*, *Mario Power Tennis* GC & Wii, *We Love Golf!*) |
| **CTPK** | `.ctpk` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C texture package (3DS) |
| **CTXB** | `.ctxb` | ✅ | ✅ | ✅ | — | Grezzo 3DS texture container (*Ocarina of Time 3D*, *Majora's Mask 3D*) |
| **G1T** | `.g1t` | ✅ | — | — | ✅ | Koei Tecmo texture container (*Hyrule Warriors*, *Fire Emblem Warriors*). 3DS ETC1/ETC1A4/RGBA8 — 2602 of the 2603 textures on the *Hyrule Warriors Legends* cart; the one holdout uses an 8bpp encoding no other file exercises |
| **GTX** | `.gtx` | ✅ | ✅ | ✅ | ✅ | Nintendo Wii U GX2 surface container (Wii U) |
| **NCER / NANR** | `.ncer`, `.nanr` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro cell & animation resources (DS) |
| **NCGR / NCLR** | `.ncgr`, `.nclr` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro 2D graphics & palette (DS) |
| **NSBTX** | `.nsbtx` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro 3D texture container (DS) |
| **NUT** | `.nut` | ✅ | ✅ | ✅ | — | Bandai Namco texture package (*Super Smash Bros. 4* Wii U / 3DS) |
| **NUTEXB** | `.nutexb` | ✅ | ✅ | ✅ | — | Bandai Namco / Nintendo Switch texture wrapper (Switch) |
| **PTLG** | `.glt`, `.rlt` | ✅ | ✅ | ✅ | — | Next Level Games texture container, extracted as TPL (*Super Mario Strikers*, *Mario Strikers Charged*) |
| **TEX** | `.tex` | ✅ | ✅ | ✅ | ✅ | Monster Games GX texture format (Wii) |
| **TEX0** | `.tex0` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R texture resource (Wii) |
| **TEX3DS** | `.tex` | — | — | — | — | Nintendo 3DS proprietary texture (identification only) |
| **XIMG** | `.xi` | — | — | — | — | Level-5 3DS/Switch image & texture container |

`Byte-Exact Roundtrip` = encode → decode → re-encode to the same destination name
reproduces the file's bytes. Exercised by `t_byte_fixed_points()` in `tests/regress.sh`.
BRRES sub-file formats (TEX0, TEX) embed their own name, so the name has to match.

---

### Audio, Sound & Music

| Format | Extensions | Decode Tested | Encode Tested | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- |
| **BCSAR / BCWAR / BCWAV** | `.bcsar`, `.bcwar`, `.bcwav` | ✅ | ✅ | ✅ | NintendoWare NW4C sound archive & wave format (3DS) |
| **BFSAR / BFWAR / BFWAV** | `.bfsar`, `.bfwar`, `.bfwav` | ✅ | ✅ | ✅ | NintendoWare NW4F & NintendoSDK sound archive & wave format (Wii U / Switch) |
| **BRSAR / RBNK / RWAV** | `.brsar`, `.rbnk`, `.rwav` | ✅ | ✅ | ✅ | NintendoWare NW4R sound archive, instrument bank & wave format (Wii) |
| **BRSTM / BCSTM / BFSTM** | `.brstm`, `.bcstm`, `.bfstm` | ✅ | ✅ | ✅ | Nintendo multi-channel stream audio (Wii / 3DS / Wii U / Switch) |
| **NUS3AUDIO** | `.nus3audio`, `.nus3bank` | ✅ | ✅ | — | Bandai Namco NUS3 audio archive (*Super Smash Bros. Ultimate* Switch) |
| **RSEQ / CSEQ / FSEQ / SSEQ** | `.rseq`, `.cseq`, `.fseq`, `.sseq` | ✅ | ✅ | ✅ | Nintendo sequence music format (Wii / 3DS / Wii U / DS) |
| **SDAT** | `.sdat` | ✅ | ✅ | ✅ | Nintendo DS Nitro sound archive (DS) |

---

### Layouts, Text & Game Data

| Format | Extensions | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- |
| **BCLYT / BCLAN** | `.bclyt`, `.bclan` | ✅ | ✅ | ✅ | — | NintendoWare NW4C 2D layout & animation (3DS) |
| **BFLYT / BFLAN** | `.bflyt`, `.bflan` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4F 2D layout & animation (Wii U) |
| **BMG** | `.bmg` | ✅ | ✅ | ✅ | ✅ | Nintendo standard binary message format (GameCube / Wii) |
| **BRLYT / BRLAN** | `.brlyt`, `.brlan` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R 2D layout & animation (Wii) |
| **BYAML / BYML** | `.byaml`, `.byml` | ✅ | ✅ | ✅ | ✅ | Nintendo binary YAML data format (Wii / Wii U / Switch) |
| **MIO** | `.mio` | ✅ | — | — | ✅ | *WarioWare: D.I.Y.* / *Made in Ore* Game, Comic & Record data (DS / Wii) |
| **MSBT / MSBP / MSBF** | `.msbt`, `.msbp`, `.msbf` | ✅ | ✅ | ✅ | ✅ | Nintendo Message Studio binary text, project & flow (3DS / Wii U / Switch) |

`Byte-Exact Roundtrip` = encode → semantic text → re-encode reproduces the file's
bytes. Exercised by `t_byte_fixed_points()` in `tests/regress.sh`. BRLYT/BRLAN
canonical fixed point and semantic roundtrips are validated against retail Wii layouts.

---

### Compression & Encoding Formats

| Algorithm / Codec | Identifiers / Headers | Decode Tested | Encode Tested | Platform / Engine Context |
| --- | --- | --- | --- | --- |
| **ALZ1** | `ALZ1` | ✅ | ✅ | Hudson Soft Mario Party / Bomberman LZ77 (GameCube / Wii) |
| **BLZ** | ARM9 overlay trailer | ✅ | ✅ | Nintendo DS Nitro backward LZ overlay compression |
| **BPE / GFCP** | `GFCP` (zip mode 1) | ✅ | ✅ | Good-Feel Byte Pair Encoding (Wii Kirby's Epic Yarn / Yoshi's Woolly World) |
| **Bzip2** | `BZh` | ✅ | ✅ | Standard high-compression block-sorting codec |
| **Deflate / Zlib** | `78 01`, `78 9C`, `78 DA` | ✅ | ✅ | Standard RFC 1950 / 1951 stream compression |
| **Diff8 / Diff16** | `0x81`, `0x82` | ✅ | ✅ | Nintendo DS differential delta filter encoding |
| **Huffman (4-bit / 8-bit)** | `0x24`, `0x28` | ✅ | ✅ | Nintendo DS Huffman stream compression |
| **LZ10** | `0x10` (LZSS) | ✅ | ✅ | Nintendo standard LZ77 (GameCube / Wii / DS / GBA) |
| **LZ11** | `0x11` (Extended LZSS) | ✅ | ✅ | Nintendo extended LZSS with 4-byte match lengths (DS / 3DS) |
| **LZO / LZOvl** | Overlay trailer | ✅ | ✅ | Nintendo DS reverse LZO overlay compression |
| **LZX** | `LZX` | ✅ | ✅ | Capcom Ace Attorney / Ghost Trick LZSS (DS) |
| **MVDK** | `MVDK` | ✅ | ✅ | Nintendo Mario vs. Donkey Kong LZSS (DS) |
| **PSDK** | `PSDK` / `AT4PX` | ✅ | ✅ | Chunsoft Pokémon Mystery Dungeon Explorers LZSS (DS) |
| **PuCrunch** | `0x50 0x75` (`Pu`) | ✅ | ✅ | Retro / Nitro hybrid LZ + RLE stream compression |
| **QuickLZ** | `QLZ` | ✅ | ✅ | Fast byte-oriented block compression (Level 1 / 3) |
| **RLE** | `0x30` | ✅ | ✅ | Nintendo DS run-length encoding |
| **RNC1 / RNC2** | `RNC\1`, `RNC\2` | ✅ | ✅ | Rob Northen Computing ProPack Method 1 / Method 2 |
| **SSZL** | `SSZL` | ✅ | ✅ | Bandai Namco Museum Remix LZSS0 stream compression (Wii) |
| **VLX** | `VLX` | ✅ | ✅ | Level-5 Professor Layton / Inazuma Eleven LZSS (DS) |
| **Yay0 (SZP)** | `Yay0` | ✅ | ✅ | Nintendo early LZSS container (Nintendo 64 / GameCube) |
| **Yaz0 (SZS)** | `Yaz0` | ✅ | ✅ | Nintendo standard byte-aligned LZSS (GameCube / Wii / Switch) |
| **Zstandard (Zstd)** | `28 B5 2F FD` | ✅ | ✅ | Modern high-ratio dictionary compression (Switch / F-Zero 99) |

---

### Passthrough & External Tool Delegation

When extracting or repacking game trees with `wszst xx` / `wszst create`, unsupported container formats, optical disc images, and proprietary media are transparently delegated to external tools (configurable via `--with-<tool>=...` or `--no-passthrough`):

| Category / Format | Extensions & Types | Delegated Tool | Description & Integration |
|---|---|---|---|
| **7-Zip / RAR / Tar Archives** | `.7z`, `.rar`, `.cb7`, `.tar`, `.tgz`, `.tbz2`, `.txz` | **`7z`** / **`7zz`** / **`7za`** / **`unar`** (`--with-7z`) | General archive unpacking |
| **Custom Binary Containers** | Arbitrary formats | **`QuickBMS`** (`--bms=<script.bms>`) | Direct execution of QuickBMS extraction scripts |
| **DSP-ADPCM Audio Streams** | `.brstm`, `.bcstm`, `.bfstm`, `.bns`, `.btsnd`, `.ast`, `.dsp` | **`mobipeg`** | Bit-exact Nintendo THP ADPCM coefficient search & stream encoding |
| **Mobiclip Video & Cutscenes** | `.mo`, `.mods`, `.moflex`, `.MOC`, `.MOD` | **`mobipeg`** (`--with-mobipeg`) / **`ffmpeg`** | Nintendo DS / 3DS / Wii Mobiclip video decoding to MP4 |
| **Nintendo 3DS Containers** | `.3ds`, `.cci`, `.cxi`, `.cfa`, `.cia`, `.app` | **`ctrtool`** / **`makerom`** (`--with-ctrtool`) | NCCH/NCSD partition extraction, ExeFS/RomFS unpacking & CIA installation packages |
| **Nintendo DS / DSi ROMs** | `.nds`, `.srl`, `.dsi` | **`ndstool`** (`--with-ndstool`) | Nitro ROM header, banner, arm9/arm7 binary & NitroFS extraction/rebuild |
| **Nintendo Switch Packages** | `.nsp`, `.xci`, `.nca` | **`hactool`** / **`hacbrewpack`** (`--with-hactool`, `--with-hacbrewpack`) | PFS0 / HFS0 / NCA content extraction & homebrew NSP repacking |
| **THP & Media Video** | `.thp`, `.h4m`, `.dpg`, `.fv`, `.ppm`, `.kwz`, `.mmstr`, `.rvid`, `.vx` | **`mobipeg`** / **`ffmpeg`** | GameCube/Wii THP, HVQM4, DPG, FastVideo & Flipnote animation decoding |
| **Wii / GameCube Disc Images** | `.iso`, `.wbfs`, `.wdf`, `.ciso`, `.wia` | **`wit`** (`--with-wit`) | Disc partition extraction & scrubbed disc creation |
| **Wii U Optical Discs** | `.wud`, `.wux` | **`wud2app`** + **`cdecrypt`** | Automated compressed WUX disc decompression, partition dump & decryption |
| **Wii WAD Packages** | `.wad`, `.app` | **`sharpii`** (`--with-sharpii`) | Wii title & IOS WAD archive unpacking and repacking |

---

## Documentation & Guides

- **[Command Reference & New Tools Guide](docs/COMMANDS.md)**: Complete guide to all new standalone tools, wszst subcommands, and extended CLI workflows.
- **[Workflow & Modding Guide](docs/WORKFLOWS.md)**: Recursive game directory tree traversal, asset modification, and incremental repacking.
- **[Format Specifications & Technical Reference](docs/FORMATS.md)**: Deep technical index of all supported formats.
- **[Official Wiimms SZS Tools Documentation](https://szs.wiimm.de/)**: Original command reference, parameters, and documentation.

---

## License & Credits

- Based on **Wiimms SZS Tools** by Dirk Clemens (*Wiimm*).
- Licensed under the **GNU General Public License v2** (see `project/gpl-2.0.txt`).
- See **[CREDITS.md](CREDITS.md)** for full attributions of incorporated libraries and research projects.
