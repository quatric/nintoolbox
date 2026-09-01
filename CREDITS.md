# Credits & Third-Party Attributions

`wiimms-szs-tools-plus` builds upon, incorporates, and interfaces with many open-source projects, libraries, and reverse-engineering contributions. We gratefully acknowledge and credit all original authors and contributors below.

---

## Core Upstream Project

* **Wiimms SZS Tools**
  * **Author:** Dirk Clemens (`wiimm@wiimm.de`)
  * **Website / Project:** <https://szs.wiimm.de/>
  * **License:** GNU General Public License v2.0 or later (GPL-2.0-or-later)
  * **Description:** The foundation, CLI framework, and core format handlers for Mario Kart Wii and Nintendo file formats.

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

### 11. AJPG / ODH Codec (mobipeg / ActImagine)
* **Authors / Project:** FFmpeg project & mobipeg contributors (ActImagine GBA baseline JPEG core)
* **License:** GNU Lesser General Public License v2.1 or later (LGPL-2.1+)
* **Description:** GBA and Wii Message Board photo attachment (AJPG/ODH) codec.

### 12. midilib
* **Description:** Standard MIDI File (SMF 0/1/2) stream reader, event tracker, and file synthesis for Nintendo sequence conversion.

---

## Community & Research Credits

* **Custom Mario Kart Wii (Wiiki) Community:** Documentation, specifications, and research on KMP, KCL, BRRES, and associated formats (<http://wiki.tockdom.com/>).
* **Nintendo Reverse Engineering Community:** Documentation on 3DS (CTR/NW4C), Wii U (Cafe/NW4F), and Switch (NX/NW4N) layout, sound, and model formats.
