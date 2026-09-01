# Wiimms SZS Tools Plus

A fast, unified command-line toolkit to extract, modify, convert, and rebuild game archives, textures, 3D models, audio, and layouts across **GameCube, Wii, Nintendo DS, 3DS, Wii U, and Nintendo Switch**.

---

## Highlights

- **Universal Extraction & Repacking (`wszst`)**: Extract almost any Nintendo archive or disc image with `wszst xx <file>`, edit the contents, and rebuild cleanly with `wszst CREATE <folder>`.
- **Modern 3D Model Pipelines (`wmdlt`)**: Convert Wii (MDL0), GameCube (HSF, HSD), DS (BMD, NSBMD), 3DS (BCH, BCRES), and Wii U / Switch (BFRES) models directly to and from **GLB** and **COLLADA (DAE)**.
- **Full Texture & Image Suite (`wimgt`)**: Decode and encode Wii GX textures (TPL, BTI, TEX0), 3DS (BCLIM, CTPK), Wii U (BFLIM, GTX), Switch (BNTX, NUTEXB), and DS sprites (NCGR, NCLR, NCER, NANR) to standard **PNG**.
- **Audio & Sound Archives (`wbrsar` / `wbrstm`)**: Unpack, convert, and repack BRSAR, BCSAR, BFSAR, and DS SDAT archives; convert stream audio (BRSTM, BCSTM, BFSTM) and sequence music (RSEQ, CSEQ, FSEQ, SSEQ) to WAV and MIDI.
- **Layout & Message Editing (`wlayt` / `wbmgt`)**: Lossless text/XML disassembly and compilation for BRLYT, BCLYT, BFLYT layouts and MSBT / BMG game text.
- **Transparent Compression & Crypto**: Native support for Yaz0, Yay0, LZ10, LZ11, QuickLZ, BLZ, Deflate, Zlib, RNC, and AES-encrypted containers.

---

## Quick Start

### Building from Source

Build the complete suite using `make`:

```bash
git clone https://github.com/quatric/wiimms-szs-tools-plus.git
cd wiimms-szs-tools-plus/project
make all -j$(nproc)
```

The compiled binaries (`wszst`, `wimgt`, `wmdlt`, `wbrsar`, `wbmgt`, `wlayt`, `wctct`, `wkclt`, `wkmpt`) will be placed in `project/bin/`.

---

## Common Commands

### 1. Extracting & Repacking Archives
```bash
# Extract any archive or ROM (SZS, U8, RARC, SARC, NARC, DARC, GFA, PAC, NDS, etc.)
wszst xx Track.szs

# Rebuild an extracted directory back into an archive
wszst CREATE Track.d --dest Track.szs
```

### 2. Converting 3D Models
```bash
# Convert a Nintendo 3D model to GLB (or .dae)
wmdlt DECODE Mario.mdl0 --dest Mario.glb
wmdlt DECODE Course.bfres --dest Course.glb
wmdlt DECODE Model.hsf --dest Model.glb

# Convert a GLB/DAE back into a native Nintendo model
wmdlt ENCODE Mario.glb --dest Mario.hsf
```

### 3. Converting Textures & Images
```bash
# Decode Nintendo textures to PNG
wimgt DECODE texture.tpl --dest texture.png
wimgt DECODE texture.bntx --dest texture.png

# Encode PNG images to Nintendo texture formats
wimgt ENCODE texture.png --dest texture.tpl
```

### 4. Audio & Sound Archives
```bash
# Unpack a sound archive (BRSAR / BCSAR / BFSAR / SDAT)
wbrsar unpack Sound.brsar --dest Sound.d

# Convert multi-channel streams to WAV
wbrstm DECODE music.brstm --dest music.wav

# Convert sequence music to MIDI
wseqt DECODE sequence.sseq --dest sequence.mid
```

---

## Supported Formats Summary

| Category | Supported Formats |
|---|---|
| **Archives & Containers** | `.szs`, `.u8`, `.arc`, `.rarc`, `.sarc`, `.narc`, `.darc`, `.gfa`, `.pac`, `.fsys`, `.bg4`, `.ccf`, `.cram`, `.sze`, `.rflres`, `.idx`/`.bin`, `.wud`, `.wux`, `.nds`, `.srl`, `.3ds`, `.cia`, `.nsp`, `.xci` |
| **3D Models** | `.mdl0`, `.bmd`, `.nsbmd`, `.bch`, `.bcres`, `.bfres`, `.hsf`, `.hsd` (`.dat`), `.mod`, `.msh` $\leftrightarrow$ **GLB** / **COLLADA (DAE)** |
| **Textures & Sprites** | `.tpl`, `.bti`, `.tex0`, `.bclim`, `.ctpk`, `.bflim`, `.gtx`, `.bntx`, `.nutexb`, `.ncgr`, `.nclr`, `.ncer`, `.nanr`, `.art`, `.img` $\leftrightarrow$ **PNG** |
| **Audio & Sound** | `.brsar`, `.bcsar`, `.bfsar`, `.sdat`, `.brstm`, `.bcstm`, `.bfstm`, `.bcwav`, `.bfwav`, `.rwav`, `.rseq`, `.cseq`, `.fseq`, `.sseq`, `.rbnk` $\leftrightarrow$ **WAV** / **MIDI** / **SF2** |
| **Layouts & Text** | `.brlyt`, `.brlan`, `.bclyt`, `.bclan`, `.bflyt`, `.bflan`, `.msbt`, `.msbp`, `.msbf`, `.bmg`, `.byaml`, `.byml` $\leftrightarrow$ **XML** / **TXT** |
| **Compression** | Yaz0, Yay0, LZ10, LZ11, LZ77, QuickLZ, BLZ, ALZ1, PSDK, SSZL, MVDK, Deflate, Zlib, RNC1, RNC2, Diff8/16, LZX, PuCrunch |

For detailed technical specifications, exact struct layouts, and reverse-engineering findings, see **[docs/FORMATS.md](docs/FORMATS.md)** and **[docs/WORKFLOWS.md](docs/WORKFLOWS.md)**.

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
