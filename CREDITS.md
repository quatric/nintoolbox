# Credits & Attributions

`wiimms-szs-tools-plus` builds upon, incorporates, and interfaces with various open-source projects, libraries, reverse-engineering tools, and community research. We gratefully acknowledge and credit all original authors, contributors, and reverse-engineering pioneers below.

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
* **Switch Toolbox** by **KillzXGaming** ([Switch-Toolbox](https://github.com/KillzXGaming/Switch-Toolbox))
  * Technical reference for Switch, Wii U, and 3DS format structures (BFRES, BNTX, BCA, BMA, BNXP, SARC, BYML, and texture compression layouts).
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
* **QuickBMS** by **Luigi Auriemma** (<http://aluigi.altervista.org/quickbms.htm>)
  * Format documentation, decompression algorithms, and container specifications used for various flat archives.
* **LibMobiclip / FastVideoDS** by **Gericom**
  * Video playback, codec reverse engineering, and format specifications for Nintendo DS / Wii Mobiclip video streams.

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
* **Authors / Project:** Mike and the VGMTrans Team
* **Website:** <https://github.com/vgmtrans/vgmtrans>
* **License:** zlib/libpng License
* **Description:** Video game music translation engine used for NDS SDAT sequence, instrument bank, and soundfont extraction.

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

---

## Community & Research Credits

* **Custom Mario Kart Wii (Wiiki) Community:** Documentation, specifications, and research on KMP, KCL, BRRES, and associated formats (<http://wiki.tockdom.com/>).
* **Nintendo Reverse Engineering Community:** Documentation and research on 3DS (CTR/NW4C), Wii U (Cafe/NW4F), and Switch (NX/NW4N) layout, sound, and model formats.
