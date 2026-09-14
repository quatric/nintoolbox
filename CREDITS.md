# Credits & Attributions

`nintoolbox` builds upon, incorporates, and interfaces with various open-source projects, libraries, reverse-engineering tools, and community research. We gratefully acknowledge and credit all original authors, contributors, and reverse-engineering pioneers below.

---

## Core Upstream Project

* **Wiimms SZS Tools**
  * **Author:** Dirk Clemens (`wiimm@wiimm.de`)
  * **Website / Project:** <https://szs.wiimm.de/>
  * **License:** GNU General Public License v2.0 or later (GPL-2.0-or-later)
  * **Description:** The foundation, CLI framework, and core format handlers for Mario Kart Wii and Nintendo file formats.

---

## Reference Tools & Research Implementations

We acknowledge and credit the following tools and authors whose research, format specifications, and reference implementations were instrumental:

* **Garhoogin / NitroPaint** ([Garhoogin](https://github.com/Garhoogin))
  * Reference implementation and deep technical research for Nintendo DS graphics, palettes, cell/animation systems, and 3D formats (NCGR, NCLR, NCER, NANR, NSBMD, etc.).
* **Nintendo DS Decompressors** by **CUE**
  * Reference implementations and algorithm specifications for Nintendo compression formats (LZ77 0x10, LZ11 0x11, Huffman 0x24/0x28, RLE 0x30, Difference filter 0x80).
* **Switch Toolbox, BfresLibrary & BFRES-Viewer** by **KillzXGaming** ([Switch-Toolbox](https://github.com/KillzXGaming/Switch-Toolbox), [BfresLibrary](https://github.com/KillzXGaming/BfresLibrary), [BFRES-Viewer](https://github.com/KillzXGaming/BFRES-Viewer))
  * Technical reference, reverse engineering, and format specifications for Nintendo Switch, Wii U, and 3DS format structures (BFRES, BNTX, BCA, BMA, BNXP, SARC, BYML, and texture compression layouts). Reference models and layouts for Wii U and Switch NintendoWare BFRES models, FSKL skeletons, FMDL geometry, FVTX vertex buffers, version-gated BufferInfo memory pools, TexSrt texture matrix transformations, material structures, and FSKA skeletal animation curves across versions (v8, v9, v10+).
* **Smash-Forge** by **KillzXGaming, jam1garner, Ploaj, SMG, and contributors** ([Smash-Forge](https://github.com/KillzXGaming/Smash-Forge))
  * Super Smash Bros. 4 (Wii U / 3DS) and *Pokkén Tournament* 3D model formats (NDP3, NDWU, and NDWD little-endian NUD specifications, ObjectData and PolyData mesh and vertex parsing, vertex attribute configurations, and NUT texture formats).
* **PartyPatcher & MPLibrary** by **KillzXGaming** ([PartyPatcher](https://github.com/KillzXGaming/PartyPatcher), [MPLibrary](https://github.com/KillzXGaming/MPLibrary))
  * Technical reference and specifications for Hudson Soft GameCube and Wii *Mario Party* formats, including HSF 3D models and MPBIN archive containers.
* **BFRES-Shader-Maker & ShaderLibrary** by **KillzXGaming** ([BFRES-Shader-Maker](https://github.com/KillzXGaming/BFRES-Shader-Maker), [ShaderLibrary](https://github.com/KillzXGaming/ShaderLibrary))
  * Technical reference for NintendoWare shader archive architectures (BFSHA / FSHA, BNSH, and SHARC / SHARCFB source and bytecode binary structures).
* **Blender-GCN-Mario-Party-Plugin** by **KillzXGaming** ([Blender-GCN-Mario-Party-Plugin](https://github.com/KillzXGaming/Blender-GCN-Mario-Party-Plugin))
  * Mario Party GameCube HSF model and BIN asset import/export specifications.
* **LayoutLibrary** by **KillzXGaming** ([LayoutLibrary](https://github.com/KillzXGaming/LayoutLibrary))
  * Reference implementation and specifications for NintendoWare 2D layout and pane animation formats (BFLYT, BFLAN, BCLYT, BCLAN, BRLYT, BRLAN, and Super Mario 3D All-Stars reversed TYLR/NALR definitions).
* **ImageLibrary** by **KillzXGaming** ([ImageLibrary](https://github.com/KillzXGaming/ImageLibrary))
  * Reference implementations for cross-platform Nintendo texture formats, ASTC codecs, swizzling, and decoding routines.
* **LegacySwitchLibraries** by **KillzXGaming** ([LegacySwitchLibraries](https://github.com/KillzXGaming/LegacySwitchLibraries))
  * Foundational Switch format reverse engineering (Syroot.NintenTools.Bfres and Syroot.NintenTools.Bntx).
* **Metroid-Fed-Force-Dumper** by **KillzXGaming** ([Metroid-Fed-Force-Dumper](https://github.com/KillzXGaming/Metroid-Fed-Force-Dumper))
  * Technical reference and specifications for Next Level Games dictionary archives (*Metroid Prime: Federation Force*, *Luigi's Mansion: Dark Moon* / LM2HD, *Luigi's Mansion 3*, and *Mario Strikers: Battle League Football*).
* **MPR-Model-Dumper** by **KillzXGaming** ([MPR-Model-Dumper](https://github.com/KillzXGaming/MPR-Model-Dumper))
  * Technical reference for Retro Studios RFRM format specifications (*Metroid Prime Remastered* and *Donkey Kong Country: Tropical Freeze* CMDL, SMDL, WMDL world model geometry, and SKEL skeleton hierarchies).
* **EffectLibrary** by **KillzXGaming** ([EffectLibrary](https://github.com/KillzXGaming/EffectLibrary))
  * NintendoWare particle effect systems (EFT1, EFT2, and VFXB `.ptcl`/`.eset` archives).
* **Metanoia** by **Ploaj** ([Metanoia](https://github.com/Ploaj/Metanoia))
  * Multi-platform reverse engineering, format analysis, and 3D model exploration tool for Nintendo formats across GameCube, Wii, 3DS, DS, and Switch (including HSF, HSD, GLG, BNFM, and proprietary console asset containers).
* **Scarlet** by **xdanieldzd** ([Scarlet](https://github.com/xdanieldzd/Scarlet))
  * Reference implementation and format specifications for Nintendo 3DS, DS, and console image, container, and compression formats (including PICA200 texture containers BTGA, CTXB, DMPBM, STEX, and CMB texture chunks).
* **ASH0-tools** by **NinjaCheetah & Garhoogin** ([ASH0-tools](https://github.com/NinjaCheetah/ASH0-tools))
  * Reference implementation and format specifications for Nintendo Wii ASH0 Huffman/LZ compression and decompression algorithms.
* **Kuriimu / Kuriimu2** by **IcySon55, FanTranslatorsInternational** ([Kuriimu](https://github.com/FanTranslatorsInternational/Kuriimu))
  * Research and reference implementation for game translation tools, text archives (MSBT, BMG, MSBP, MSBF), and container formats across Nintendo platforms.
* **BrawlCrate & BrawlLib** by **soopercool101, BrawlCrate Team, Kryal, BlackJax96** ([BrawlCrate](https://github.com/soopercool101/BrawlCrate))
  * Essential reference specifications and implementations for Nintendo Wii NW4R binary formats (BRRES, MDL0, CHR0, CLR0, PAT0, SCN0, SHP0, SRT0, VIS0, BREFF, BREFT).
* **GotaSequenceCmd & Nitro Studio** by **Gota7** ([Gota7](https://github.com/Gota7))
  * Sequence, bank, and wave format research and conversion tools for Nintendo DS/3DS sound archives (SDAT, SSEQ, SBNK, SWAR, CSEQ, CWAV).
* **SPICA & Ohana3DS / Ohana3DS Rebirth** by **gdkchan** ([SPICA](https://github.com/gdkchan/SPICA), [Ohana3DS](https://github.com/gdkchan/Ohana3DS-Rebirth))
  * Research and reference implementation for Nintendo 3DS 3D model formats (CTR NW4C BCH, CTPK, and PICA200 texture processing).
* **benzin** by **Treeki, feartec, megazig, quickdraw**
  * Pioneer research and disassembler/assembler tools for Wii layout formats (BRLYT, BRLAN).
* **LayoutStudio & WiiLayoutEditor** by **NinjaCheetah, Treeki, GalaxySimulator, and contributors**
  * Reference implementations and documentation for Nintendo 2D layout formats (BRLYT, BFLYT, BCLYT, BRLAN, BFLAN, BCLAN).
* **Sharpii & libWiiSharp** by **Treeki & Leathl**
  * Reference tools for Wii container and system formats (U8, TPL, BMG, DOL, WAD, TMD, Ticket).
* **nfs2iso2nfs** ([sabykos/nfs2iso2nfs](https://github.com/sabykos/nfs2iso2nfs))
  * Reference implementation for the Wii U "Wii Virtual Console" NFS/EGGS container; `wit`'s `x-nfs.c` is a direct C port of its `nfs2iso` / `iso2nfs` logic.
* **QuickBMS** by **Luigi Auriemma** (<http://aluigi.altervista.org/quickbms.htm>)
  * Format documentation, decompression algorithms, and container specifications used for various flat archives.
* **EveryFileExplorer** by **Gericom** ([EveryFileExplorer](https://github.com/Gericom/EveryFileExplorer))
  * Multi-format reverse engineering, specifications, and reference implementations for Nintendo 3DS, DS, Wii, and GameCube file systems, 2D/3D graphics, audio, container formats, and Mobiclip / FastVideoDS video codecs.
* **VGMTrans (Magcius fork)** by **Jasper St. Pierre (Magcius)** ([magcius/vgmtrans](https://github.com/magcius/vgmtrans))
  * Cross-platform modernized VGMTrans engine, CMake build infrastructure, and CLI driver for video game music translation (Wii BRSAR, NDS SDAT, and sequence/instrument bank extraction).
* **retrotool** by **PrimeDecomp** ([PrimeDecomp/retrotool](https://github.com/PrimeDecomp/retrotool))
  * Reverse engineering, format specifications, and reference implementations for Retro Studios game formats, notably *Metroid Prime Remastered* (PAK package containers, RFRM chunk layouts, CMDL 3D model geometry and vertex/index buffer layouts, TXTR textures, and Retro LZSS compression modes).

---

## Embedded & Integrated Third-Party Libraries

### 1. LibYAML
* **Author / Project:** Kirill Simonov & the YAML project contributors
* **Website:** <https://github.com/yaml/libyaml>
* **License:** MIT License
* **Description:** C YAML parser and emitter library for BYML/YAML text transformations.

### 2. Mini-XML (`mxml`)
* **Author:** Michael R Sweet
* **Website:** <https://www.msweet.org/mxml/>
* **License:** Apache License 2.0 with Exceptions / LGPL 2.0
* **Description:** Lightweight XML parsing library used for layout/metadata processing and serialization.

### 3. cgltf & cgltf_write
* **Authors:** 
  * Johannes Kuhlmann (cgltf parser)
  * Philip Rideout (cgltf writer)
  * Serge A. Zaitsev (jsmn parser core)
* **Website:** <https://github.com/jkuhlmann/cgltf>
* **License:** MIT License
* **Description:** Single-file C glTF 2.0 and GLB parser/exporter.

### 4. Decaf / Latte ISA Disassembler & Assembler (`latte-decaf`)
* **Authors / Project:** Decaf-emu team (exzap & contributors)
* **Website:** <https://github.com/decaf-emu/decaf-emu>
* **License:** GNU General Public License v3.0 (GPL-3.0)
* **Description:** Wii U Latte GPU shader bytecode disassembler and assembler.
* **Bundled Dependencies:**
  * **gsl-lite:** Martin Moene, Moritz Beutel, Microsoft Corporation (MIT)
  * **{fmt}:** Victor Zverovich and {fmt} contributors (MIT)
  * **peglib:** yhirose (MIT)
  * **cnl:** John McFarlane (Boost Software License 1.0)

### 5. VGMTrans
* **Authors / Projects:** 
  * Mike and the VGMTrans Team (<https://github.com/vgmtrans/vgmtrans>)
  * Jasper St. Pierre / Magcius (<https://github.com/magcius/vgmtrans>)
* **License:** zlib/libpng License
* **Description:** Video game music translation engine and architecture used for Wii BRSAR and NDS SDAT sequence, instrument bank, sample collection, and soundfont extraction. Integrated via vendored core in `wbrsar` and supported via external `vgmtrans` CLI invocation.

### 6. bcn-decoder & bcn-support
* **Author:** K0lb3
* **Website:** <https://github.com/K0lb3>
* **License:** MIT License
* **Description:** BC1-BC7 / DXT texture block compression and decompression routines.

### 7. ARM ASTC Codec Core
* **Author / Project:** ARM Limited and Contributors
* **License:** Apache License 2.0
* **Description:** ASTC texture decompression routines.

### 8. bzip2 (`libbz2`)
* **Author:** Julian R Seward
* **Website:** <https://sourceware.org/bzip2/>
* **License:** BSD-style bzip2 license
* **Description:** Block-sorting data compression library.

### 9. 7-Zip LZMA SDK (`liblzma`)
* **Author:** Igor Pavlov
* **Website:** <https://www.7-zip.org/sdk.html>
* **License:** Public Domain / LGPL
* **Description:** LZMA compression and decompression algorithms.

### 10. QuickLZ
* **Author:** Lasse Mikkel Reinhold
* **Website:** <http://www.quicklz.com/>
* **License:** GNU General Public License (GPL) 1/2/3
* **Description:** Fast compression library used for RST/TOC and QuickLZ streams.

### 11. midilib
* **Description:** Standard MIDI File (SMF 0/1/2) stream reader, event tracker, and file synthesis for Nintendo sequence conversion.

### 12. ctrtool
* **Author:** jakcron / Project_CTR contributors
* **Website:** <https://github.com/OfficialPixelBrush/Project_CTR> (fork of <https://github.com/3DSGuy/Project_CTR>)
* **License:** No explicit license file in the upstream repository for ctrtool itself; its bundled dependency libraries below carry their own stated licenses.
* **Description:** Nintendo 3DS CIA/CCI/NCCH/ExeFS/RomFS reader and extractor. Vendored under `project/third_party/ctrtool` and built as a companion binary shipped alongside `wszst`, so its 3DS pass-through extraction (`lib-passthru.c`) works without a separately installed copy on `PATH`.
  * Bundles **libmbedtls** (Apache License 2.0, Mbed-TLS/ARM contributors), **{fmt}** (MIT License, Victor Zverovich), and Project_CTR's own **libtoolchain**, **libnintendo-n3ds**, and **libbroadon-es** (MIT License).

---

## Community & Research Credits

* **Custom Mario Kart Wii (Wiiki) Community:** Documentation, specifications, and research on KMP, KCL, BRRES, and associated formats (<http://wiki.tockdom.com/>).
* **Nintendo Reverse Engineering Community:** Documentation and research on 3DS (CTR/NW4C), Wii U (Cafe/NW4F), and Switch (NX/NW4N) layout, sound, and model formats.
