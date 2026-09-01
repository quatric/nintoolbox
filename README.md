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

| Format | Extensions | Middleware / Engine / Platform Context |
|---|---|---|
| **ARC / U8** | `.arc`, `.szs` | Nintendo standard U8 archive (Wii / GameCube NintendoWare & EAD) |
| **Arika Archive** | `INFO.DAT`, `GAME.DAT` | Arika DS / DSi / Wii archive system |
| **ARCV** | `.arc` | Namco / Tose Wii archive format |
| **AT7** | `.at7` | Koei Tecmo container format (Wii / PS2) |
| **BG4** | `.bg4` | AlphaDream 3DS flat archive with BLZ member compression |
| **BIGF** | `.big` | Electronic Arts Wii asset archive |
| **CA01 / SA01** | `.ca01`, `.sa01` | Nintendo Network Mii & amiibo system archive (3DS / Wii U) |
| **CCF** | `.ccf` | Nintendo Virtual Console container (Wii / Switch) |
| **CRAM** | `.arc`, `.cram` | Monolith Soft 3DS flat archive container |
| **DARC** | `.darc` | NintendoWare NW4C differential archive (3DS) |
| **FSYS** | `.fsys` | Genius Sonority archive system (GameCube / Wii) |
| **GFA** | `.gfa` | Game Freak GFAC container (3DS) |
| **Hyrule Warriors** | `.idx`, `.bin` | Koei Tecmo / Omega Force split index archive (3DS) |
| **MPBIN** | `.bin` | Hudson Soft Mario Party archive container (GameCube / Wii) |
| **NARC** | `.narc` | Nintendo DS Nitro standard archive (DS / DSi) |
| **NCCARC** | `.nccarc` | Nintendo DS flat blob container |
| **NDS / SRL / DSI** | `.nds`, `.srl`, `.dsi` | Nintendo DS & DSi ROM images and executables |
| **PAC / MRG** | `.pac`, `.mrg` | HAL Laboratory / Game Arts Wii archive container |
| **RARC** | `.rarc`, `.arc` | Nintendo standard resource archive (GameCube / Wii) |
| **RFL_Res** | `RFL_Res.dat`, `.dat` | Revolution Face Library Mii resource database (Wii / 3DS / Wii U) |
| **SARC** | `.sarc`, `.szs` | NintendoWare NW4F & NintendoSDK sorted archive (Wii U / Switch / 3DS) |
| **SZE** | `.sze` | Nintendo Switch AES-encrypted container (NST / Switch) |
| **WARC** | `.warc` | Nintendo / Intelligent Systems flat archive (Wii U) |
| **WUD / WUX** | `.wud`, `.wux` | Nintendo Wii U optical disc images (raw & compressed) |

---

### 3D Models & Geometry

| Format | Extensions | Target Output | Middleware / Engine / Platform Context |
|---|---|---|---|
| **BMD** | `.bmd`, `.bdhc` | **GLB** | Early Nintendo DS 3D model format (DS) |
| **HSD** | `.dat` | **GLB** | HAL Laboratory `sysdolphin` object graph (GameCube) |
| **HSF** | `.hsf` | **GLB** | Hudson Soft 3D model format (GameCube / Wii) |
| **MDL0 / BRRES** | `.mdl0`, `.brres` | **GLB** | NintendoWare NW4R binary resource model (Wii) |
| **MOD** | `.mod` | **GLB** | Monster Games NDL3/NDL2 display list model (Wii) |
| **MSH (PMsh)** | `.msh` | **GLB** | Monster Games collision mesh format (Wii) |
| **NSBMD** | `.nsbmd`, `.bmd` | **GLB** | Nintendo DS Nitro 3D model format (DS) |

---

### Textures & 2D Graphics

| Format | Extensions | Middleware / Engine / Platform Context |
|---|---|---|
| **ART / IMG** | `.art`, `.img` | Monster Games GUI image format (Wii) |
| **BCFNT / BFFNT / BRFNT** | `.bcfnt`, `.bffnt`, `.brfnt` | NintendoWare font resource (3DS / Wii U / Wii) |
| **BCLIM / CTPK** | `.bclim`, `.ctpk` | NintendoWare NW4C texture container (3DS) |
| **BFLIM** | `.bflim` | NintendoWare NW4F texture format (Wii U) |
| **BNTX** | `.bntx` | NintendoSDK Tegra block-linear texture container (Switch) |
| **BREFT** | `.breft` | NintendoWare NW4R particle effect texture (Wii) |
| **BTI / TPL** | `.bti`, `.tpl` | Nintendo standard texture palette library (GameCube / Wii) |
| **GTX** | `.gtx` | Nintendo Wii U GX2 surface container (Wii U) |
| **NCGR / NCLR / NCER / NANR** | `.ncgr`, `.nclr`, `.ncer`, `.nanr` | Nintendo DS Nitro 2D graphics, palette, cell & animation (DS) |
| **NSBTX** | `.nsbtx` | Nintendo DS Nitro 3D texture container (DS) |
| **NUTEXB** | `.nutexb` | Bandai Namco / Nintendo Switch texture wrapper (Switch) |
| **TEX** | `.tex` | Monster Games GX texture format (Wii) |
| **TEX0** | `.tex0` | NintendoWare NW4R texture resource (Wii) |

---

### Audio, Sound & Music

| Format | Extensions | Middleware / Engine / Platform Context |
|---|---|---|
| **BCSAR / BCWAR / BCWAV** | `.bcsar`, `.bcwar`, `.bcwav` | NintendoWare NW4C sound archive & wave format (3DS) |
| **BFSAR / BFWAR / BFWAV** | `.bfsar`, `.bfwar`, `.bfwav` | NintendoWare NW4F & NintendoSDK sound archive & wave format (Wii U / Switch) |
| **BRSAR / RBNK / RWAV** | `.brsar`, `.rbnk`, `.rwav` | NintendoWare NW4R sound archive, instrument bank & wave format (Wii) |
| **BRSTM / BCSTM / BFSTM** | `.brstm`, `.bcstm`, `.bfstm` | Nintendo multi-channel stream audio (Wii / 3DS / Wii U / Switch) |
| **RSEQ / CSEQ / FSEQ / SSEQ** | `.rseq`, `.cseq`, `.fseq`, `.sseq` | Nintendo sequence music format (Wii / 3DS / Wii U / DS) |
| **SDAT** | `.sdat` | Nintendo DS Nitro sound archive (DS) |

---

### Layouts, Text & Game Data

| Format | Extensions | Middleware / Engine / Platform Context |
|---|---|---|
| **BCLYT / BCLAN** | `.bclyt`, `.bclan` | NintendoWare NW4C 2D layout & animation (3DS) |
| **BFLYT / BFLAN** | `.bflyt`, `.bflan` | NintendoWare NW4F 2D layout & animation (Wii U) |
| **BMG** | `.bmg` | Nintendo standard binary message format (GameCube / Wii) |
| **BRLYT / BRLAN** | `.brlyt`, `.brlan` | NintendoWare NW4R 2D layout & animation (Wii) |
| **BYAML / BYML** | `.byaml`, `.byml` | Nintendo binary YAML data format (Wii / Wii U / Switch) |
| **MSBT / MSBP / MSBF** | `.msbt`, `.msbp`, `.msbf` | Nintendo Message Studio binary text, project & flow (3DS / Wii U / Switch) |

---

### Compression & Encoding Formats

| Algorithm / Codec | Identifiers / Headers | Platform / Engine Context |
|---|---|---|
| **ALZ1** | `ALZ1` | Hudson Soft Mario Party / Bomberman LZ77 (GameCube / Wii) |
| **BLZ** | ARM9 overlay trailer | Nintendo DS Nitro backward LZ overlay compression |
| **BPE / GFCP** | `GFCP` (zip mode 1) | Good-Feel Byte Pair Encoding (Wii Kirby's Epic Yarn / Yoshi's Woolly World) |
| **Bzip2** | `BZh` | Standard high-compression block-sorting codec |
| **Deflate / Zlib** | `78 01`, `78 9C`, `78 DA` | Standard RFC 1950 / 1951 stream compression |
| **Diff8 / Diff16** | `0x81`, `0x82` | Nintendo DS differential delta filter encoding |
| **Huffman (4-bit / 8-bit)** | `0x24`, `0x28` | Nintendo DS Huffman stream compression |
| **LZ10** | `0x10` (LZSS) | Nintendo standard LZ77 (GameCube / Wii / DS / GBA) |
| **LZ11** | `0x11` (Extended LZSS) | Nintendo extended LZSS with 4-byte match lengths (DS / 3DS) |
| **LZO / LZOvl** | Overlay trailer | Nintendo DS reverse LZO overlay compression |
| **LZX** | `LZX` | Capcom Ace Attorney / Ghost Trick LZSS (DS) |
| **MVDK** | `MVDK` | Nintendo Mario vs. Donkey Kong LZSS (DS) |
| **PSDK** | `PSDK` / `AT4PX` | Chunsoft Pokémon Mystery Dungeon Explorers LZSS (DS) |
| **PuCrunch** | `0x50 0x75` (`Pu`) | Retro / Nitro hybrid LZ + RLE stream compression |
| **QuickLZ** | `QLZ` | Fast byte-oriented block compression (Level 1 / 3) |
| **RLE** | `0x30` | Nintendo DS run-length encoding |
| **RNC1 / RNC2** | `RNC\1`, `RNC\2` | Rob Northen Computing ProPack Method 1 / Method 2 |
| **SSZL** | `SSZL` | Bandai Namco Museum Remix LZSS0 stream compression (Wii) |
| **VLX** | `VLX` | Level-5 Professor Layton / Inazuma Eleven LZSS (DS) |
| **Yay0 (SZP)** | `Yay0` | Nintendo early LZSS container (Nintendo 64 / GameCube) |
| **Yaz0 (SZS)** | `Yaz0` | Nintendo standard byte-aligned LZSS (GameCube / Wii / Switch) |
| **Zstandard (Zstd)** | `28 B5 2F FD` | Modern high-ratio dictionary compression (Switch / F-Zero 99) |

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

- **[Workflow & Modding Guide](docs/WORKFLOWS.md)**: Recursive game directory tree traversal, asset modification, and incremental repacking.
- **[Format Specifications & Technical Reference](docs/FORMATS.md)**: Deep technical index of all supported formats.
- **[Official Wiimms SZS Tools Documentation](https://szs.wiimm.de/)**: Original command reference, parameters, and documentation.

---

## License & Credits

- Based on **Wiimms SZS Tools** by Dirk Clemens (*Wiimm*).
- Licensed under the **GNU General Public License v2** (see `project/gpl-2.0.txt`).
- See **[CREDITS.md](CREDITS.md)** for full attributions of incorporated libraries and research projects.
