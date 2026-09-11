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

# 6. Convert a whole sound archive to a playable SoundFont + MIDI set
#    (BRSAR / BFSAR / BCSAR / SDAT -> one .sf2 plus every sequence as .mid)
wbrsar Sound.brsar --dest Sound.d
wbrsar Sound.sdat --dls --dest Sound.d      # DLS instead of SF2
wbrsar Sound.sdat --both --dest Sound.d
```

---

## Supported Formats by Category

### Archives & Containers

| Format | Extensions | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- |
| **ABE BigFile** | `.bf` | ✅ | — | — | ✅ | Ubisoft *Rabbids Go Home* BigFile archive (ABE\0 with segmented LZO1X chunks). Verified against the retail RGH.BF (~1 GiB, 8710 members): the magic check, the 200-byte entry stride, and the LZO1X M2/M3 distance decode were all wrong before this pass and are now byte-exact against liblzo2 |
| **ALAR** | `.alar` | ✅ | — | — | ✅ | Nintendo DS Nitro ALAR archive (*Jump Ultimate Stars*). `FF_ALAR` carries `FFT_ARCHIVE` but `GetIteratorFunction` has no entry for it, so the generic member iterator silently walked zero files for every real `.aar` on the cart, both sub-formats (a correction of an earlier pass in this same file, which mistook a whole-cart file-extension count for a successful per-archive extraction). Reverse-engineered and implemented a dedicated reader for both: type 2 (one raw blob, offset/size as plain LE32) and type 3 (a name/offset table -- see the extractor's own comment for the layout). Verified against the retail cart: `info.aar` (type 3) recovers 3 of 4 named members intact -- including one that itself cascades into a nested ALAR -- and `chr/*.aar` (type 2, the majority of the game's character data) now recovers its one member instead of nothing |
| **And-Kensaku** | `.rz` | ✅ | — | — | — | CyberConnect2 "Pres" archive, whole-stream Nintendo LZ77 (type 0x10/0x11) wrapped (Nintendo DS *Kanji Sonomama Rakubiki Jiten* / *And-Kensaku* family). Ported from Luigi Auriemma's `and_kensaku.bms`; verified byte-exact on synthetic LZ10 and LZ11 fixtures |
| **APAK** | `.apak` | ✅ | ✅ | ✅ | — | Nintendo / Pokémon APAK archive format (Wii U / Switch) |
| **ARC0** | `.fa` | 🟡 | — | — | 🟡 | Level-5 flat archive (*Yo-Kai Watch*, 3DS). The outer `ARC0` index (header offsets 22360/28244, neither a clean stride through anything found so far) isn't understood; what's implemented instead scans for concatenated XPCK sub-archives by their own magic and measures each one from its own file table. Verified against the retail `yw1_a.fa` (386 MB): recovers 4087 real XPCK archives / 182,649 files intact. Marked partial because this only recovers XPCK members -- anything else the outer index might also hold is still unreachable |
| **ARC / U8** | `.arc`, `.szs` | ✅ | ✅ | ✅ | — | Nintendo standard U8 archive (Wii / GameCube NintendoWare & EAD) |
| **ARCV** | `.arc` | ✅ | ✅ | ✅ | — | Namco / Tose Wii archive format |
| **Arika Archive** | `INFO.DAT`, `GAME.DAT`, `.arika` | ✅ | ✅ | ✅ | — | Arika DS / DSi / Wii archive system |
| **AT7** | `.at7` | ✅ | ✅ | ✅ | — | Koei Tecmo container format (Wii / PS2) |
| **BG4** | `.bg4` | ✅ | ✅ | ✅ | — | AlphaDream 3DS flat archive with BLZ member compression |
| **BIGF** | `.big` | ✅ | ✅ | ✅ | — | Electronic Arts Wii asset archive |
| **BNS Archive** | `.bns` | ✅ | — | — | ✅ | Koei Tecmo *Samurai Warriors 3* multi-file asset archive (`LINKDATA*.BNS`). Verified against the retail LINKDATA.BNS (~1.5 GiB): all 6022 members extract intact |
| **CA01 / SA01** | `.ca01`, `.sa01` | ✅ | ✅ | ✅ | — | Nintendo Network Mii & amiibo system archive (3DS / Wii U) |
| **CCF** | `.ccf` | ✅ | ✅ | ✅ | — | Nintendo Virtual Console container (Wii / Switch) |
| **CNUT** | `.cnut` | ✅ | ✅ | ✅ | — | *Wii Party* compiled Squirrel script & message container (`SQIR`) |
| **COD PAK0** | `.pak` | 🟡 | — | — | 🟡 | *Call of Duty: Black Ops* / *MW3* (Wii) sound archive (`PAK0`). Checked against every retail `.pak` in MW3 (including `sound.pak`, ~1.1 GiB): the CRC/offset/size table splits every member out at the right bytes, but the assumption that each member is a headerless raw DSP stream is wrong -- every member actually opens with a `43 21 00 02` tag, a nibble count, and a big-endian offset table before the real audio data starts, so the current pass-through can't turn any of them into playable audio yet |
| **CRAM** | `.arc`, `.cram` | ✅ | ✅ | ✅ | — | Monolith Soft 3DS flat archive container |
| **DARC** | `.darc` | ✅ | ✅ | ✅ | — | NintendoWare NW4C differential archive (3DS) |
| **DTLS** | `dt00`, `ls00`, `.ls` | ✅ | ✅ | ✅ | ✅ | Bandai Namco composite package & lookup archive (*Super Smash Bros. 4* Wii U / 3DS). The retail Wii U `content/ls` uses a fourth layout next to the three 24-byte-entry forms: 4-byte tag `"of\x02\x00"` + LE `u32` count + 6541 16-byte LE entries (hash, dt-file offset, span size, a two-u16 trailer whose low bit tracks with compressed sizes and whose high word is 0/2/4 — semantics unconfirmed without `dt00`). `ScanDTLS()` decodes it (entries without a stored decompressed size pass through as raw `dt00` slices; some are genuine zlib streams inflating to real `NUS3`/`VAT\0`/`SQB\0`/`NTP3` content) and `wszst xx` accepts the extensionless retail `content/ls` name, resolving `content/dt00` beside it (6541 members even with `dt00` absent). |
| **F9RES** | `.res` | ✅ | ✅ | ✅ | — | GameCube resource archive container |
| **FSYS** | `.fsys` | ✅ | ✅ | ✅ | — | Genius Sonority archive system (GameCube / Wii) |
| **GAR / ZAR** | `.zar`, `.gar` | ✅ | ✅ | ✅ | — | Grezzo Zelda & Luigi's Mansion archive (*OoT3D*, *MM3D*, *LM3DS*) |
| **GFA** | `.gfa` | ✅ | ✅ | ✅ | ✅ | Good-Feel GFAC container (Wii / 3DS / Wii U). Also verified against the retail *Yoshi's Woolly World* (Wii U): `content/message_image/msgbox008_00k.gfa` (148,877 bytes) extracts to a 2-member SARC that cascades into a real BFLIM texture and BFLYT layout. Committed as `tests/fixtures/gfa_wiiu_yoshis_woolly_world_msgbox008_00k.gfa` |
| **Hyrule Warriors** | `.idx`, `.bin` | ✅ | ✅ | ✅ | — | Koei Tecmo / Omega Force split index archive (3DS) |
| **IQIPACK** | `.pak` | ✅ | — | — | — | NVIDIA Shield iQiyi PAK archive with XXTEA encryption |
| **JARC** | `.jarc` | ✅ | ✅ | ✅ | — | Level-5 DS archive container (DS) |
| **LSPK** | `.pk`, `.pkh`, `.lspk` | ✅ | ✅ | ✅ | ✅ | Level-5 / Mistwalker flat package (*The Last Story*). Verified against the retail `levels.pk`/`.pkh` pair (2626 members). Unrelated to the DS *Inazuma Eleven*'s own `.pkh`/`.pkb` files, which open with a `PackNum` text header this format doesn't expect and are still unrecognized (see the G4PKM row) |
| **MDR** | `.mdr` | ✅ | ✅ | ✅ | — | *Dance Dance Revolution Mario Mix* chunk archive with per-chunk zlib streams |
| **MKGPDX PAC** | `.pac`, `.mkgpdx` | ✅ | ✅ | ✅ | — | *Mario Kart Arcade GP DX* layout archive (`pack`) |
| **MPBIN** | `.bin` | ✅ | ✅ | ✅ | ✅ | Hudson Soft Mario Party archive container (GameCube / Wii) |
| **MPR PACK** | `.pak` | ✅ | — | — | ✅ | Retro Studios asset container (*Metroid Prime Remastered*, Switch). LE RFRM form (`PACK` v1) with a `TOCC` v3 directory: 52-byte ADIR entries (FourCC type, LE uuid, absolute offset, declared + stored sizes), META blobs and STRG names. Members are raw RFRM resources, stored or LZSS-compressed (u32 LE mode 0-3). Verified against the retail romfs: `Preload/MPR1/GameplayOverrides.pak` (2 stored members) and `MiscData.pak` (11 members incl. LZSS-compressed SHNT/FONT and a TXTR) extract byte-identical to retrotool's own `pak extract`, as `<STRG name>.<TYPE>` with the FOOT form appended. `wszst xx` recurses into members; repack (`pak package` direction) is not implemented |
| **MSR** | `.bin` | 🟡 | — | — | 🟡 | *Metroid: Samus Returns* (3DS) flat archive container. Verified against the retail romfs: `.bctex` textures (MTXT) decode cleanly with no errors, but no dedicated top-level flat-archive decode was exercised -- the retail cart's romfs already ships as loose named files under a `files.toc` index, not a single packed blob |
| **MTXT** | `.mtxt` | ✅ | ✅ | ✅ | — | Nintendo Switch MTXT texture archive (gzip-wrapped XTX) |
| **NARC** | `.narc` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro standard archive (DS / DSi) |
| **NCCARC** | `.nccarc` | ✅ | ✅ | ✅ | — | Nintendo DS flat blob container |
| **NDS / SRL / DSI** | `.nds`, `.srl`, `.dsi` | ✅ | — | — | — | Nintendo DS & DSi ROM images and executables |
| **NXARC** | `.nxarc` | ✅ | ✅ | ✅ | — | Nintendo Switch NX archive (`RAXN`) |
| **PAC (Nd Cube)** | `.bin` | ✅ | — | — | ✅ | Nd Cube Wii U flat container (`PAC\0`, *Mario Party 10* / *Animal Crossing: amiibo Festival*). Every model/texture on the amiibo Festival retail disc ships packed inside this container. Members are ordinary zlib-compressed data (0x78 0xda header) -- not encrypted, and not even raw/headerless deflate, contrary to a prior pass's conclusion. Verified against the retail disc's `content/common/bin/ch_base/chara/cat/cat00.bin` (20/20 members decode to their exact declared size, including real GX2 `Gfx2` textures and the `cat00.bnfm` model -- see the BNFM row above). Unrelated to the "PAC / MRG" row below, which is a different format sharing only the `.pac` extension, not this one's `PAC\0` magic |
| **PAC / MRG** | `.pac`, `.mrg` | ✅ | ✅ | ✅ | — | HAL Laboratory / Game Arts Wii archive container |
| **PKG / GPKG / GPAK** | `.pkg`, `.pak`, `.gpak` | ✅ | ✅ | ✅ | — | Sonic Team Storybook series archive (*Secret Rings* / *Black Knight*), Gorilla Games *Bonsai Barber* PKG, and 2D Boy *World of Goo* GPAK |
| **PKZ** | `.pkz` | ✅ | ✅ | ✅ | — | PlatinumGames archive format (*Bayonetta*, *Astral Chain*) |
| **PRC** | `.prc` | ✅ | — | — | — | *Super Smash Bros. 4* parameter binary (`para`) |
| **PVOL** | `.pvol` | 🟡 | ✅ | ✅ | 🟡 | *Pikmin 1 & 2* model & resource container archive. The retail carts ship this content as `dataDir/archives/*.arc` (e.g. `water.arc`), not `.pvol` -- the extractor's `is_ext_match(arg,".pvol")` gate never matches, and the real header (BE32 count, then named length-prefixed entries: `objects/water/motion`, `wait.dca`, ...) doesn't match the LE32 offset-table layout `ExtractPVOLArchive` assumes either, so no sample in this corpus decodes |
| **RARC** | `.rarc`, `.arc` | ✅ | ✅ | ✅ | — | Nintendo standard resource archive (GameCube / Wii) |
| **RFL_Res** | `RFL_Res.dat`, `.dat` | ✅ | ✅ | ✅ | — | Revolution Face Library Mii resource database (Wii / 3DS / Wii U) |
| **RPAK** | `.rpak`, `.pak` | ✅ | — | — | ✅ | Retro Studios asset container (*Donkey Kong Country Returns*, Wii). Verified against a retail level pak (3672 entries); the original GameCube *Metroid Prime* (2002) uses an older, unrelated PAK layout without this format's STRG/RSHD header, so it doesn't apply here despite the similar extension |
| **RST / TOC** | `.rst`, `.toc` | ✅ | ✅ | ✅ | ✅ | Monster Games archive & table of contents (*Excite Truck* / *Excitebots*). Verified against the retail `race.res`/`race.toc` pair on both discs (the archive itself carries the `0TSR` magic; `.toc` is the paired index) -- cascades correctly into MOD/GLB, TEX/TM0/PNG, CAN and SFX exports |
| **SARC** | `.sarc`, `.szs` | ✅ | ✅ | ✅ | — | NintendoWare NW4F & NintendoSDK sorted archive (Wii U / Switch / 3DS) |
| **SFZDAT** | `.dat` | ✅ | 🟡 | — | ❌ | *Star Fox Zero* (Wii U) flat archive (`DAT\0`). Verification against the retail *Star Fox Zero* WUX is blocked: the disc's `wszst XX` extraction (6,709 files, 4.9 GiB) contains zero bare `.dat` files and zero `DAT\0` magic hits anywhere in the reachable tree. Every real `.dat` member -- `face_fox.dat`, `shader.dat`, `ba0001.dat`, `r200.dat`, ~650+ others, confirmed by name via the CPK directory strings -- instead lives inside four CRIWARE `content/data00{0..3}.cpk` archives (up to 864 MB each), which `wszst FILETYPE` reports as unrecognised (`?`) and this project has no CPK/CRILAYLA decoder for; recovering the actual bytes is out of scope for a verification-only pass |
| **SIR0** | `.sir0` | ✅ | ✅ | ✅ | — | Pokémon Mystery Dungeon resource container (DS / 3DS) |
| **STPK** | `.srd`, `.stpk` | ✅ | ✅ | ✅ | 🟡 | *Jump Super Stars* & *Jump Ultimate Stars* DS resource archive. Still no `STPK`-tagged file found anywhere in the retail *Jump Ultimate Stars* cart even after fixing ALAR extraction (see that row) and recovering ~3200 more files from it; either this cart doesn't use STPK at all, or it's nested deeper than this pass's recursion reached |
| **Storybook ONE** | `.one` | ✅ | — | — | ✅ | Sonic Team *Sonic and the Secret Rings* / *Black Knight* PRS-compressed container. The size-limit check used `out_size > opt_max_file_size` where 0 means "no limit" everywhere else in this codebase, so every real member (any nonzero size) was rejected unless `--max-file-size` happened to be set on the command line -- fixed and verified against a retail `.one` (33 members) |
| **SZE** | `.sze` | ✅ | ✅ | ✅ | — | Nintendo Switch AES-encrypted container (NST / Switch) |
| **TMPK** | `.pack`, `.tmpk` | ✅ | ✅ | ✅ | ✅ | *The Legend of Zelda: Twilight Princess HD* archive (`TMPK`). Verified against the retail cart: `content/Shaders.pack.gz` (a plain gzip stream, unrelated to this project's own compression formats) unwraps to a 10,402,512-byte TMPK archive that extracts cleanly to exactly 1568 non-empty members via `wszst EXTRACT`. Committed gzipped as `tests/fixtures/tmpk_wiiu_twilight_princess_hd_shaders.pack.gz` |
| **VCRA** | `.bin`, `.vcra` | ✅ | ✅ | ✅ | ✅ | Bandai Namco Museum Remix archive format (Wii). Verified against the retail `resident.arc`: all 65 members extract and cascade correctly into nested BRRES/TPL/BRFNT decoding |
| **VIBS** | `.vibs` | ✅ | ✅ | ✅ | — | Nintendo Switch Joy-Con vibration archive |
| **WARC** | `.warc` | ✅ | ✅ | ✅ | ✅ | Nintendo / Intelligent Systems flat archive (Wii U). Verified against the retail *Game & Wario* cart: `content/Puzzle/Bmp/Bmp.warc` (106,880 bytes) extracts cleanly to 93 non-empty `.bmp` members via `wszst EXTRACT`, and a second sample, `content/Common/Script.warc.fzip` (see FZIP below), decompresses to a 771,575-byte WARC payload that in turn extracts to 241 non-empty `.sttxt` members. Committed as `tests/fixtures/warc_wiiu_game_and_wario_bmp.warc` |
| **WUD / WUX** | `.wud`, `.wux` | ✅ | — | — | — | Nintendo Wii U optical disc images (raw & compressed) |
| **XPCK** | `.xc`, `.xpck` | ✅ | ✅ | ✅ | ✅ | Level-5 container archive (*Inazuma Eleven*, *Professor Layton*, *Yo-kai Watch*). No standalone `.xc`/`.xpck` file exists in the retail *Yo-Kai Watch* cart -- every one is concatenated, back to back with no directory of its own, inside a single 386 MB `ARC0`-tagged flat archive (`yw1_a.fa`). Added a scanner that finds each XPCK by its own magic and measures its length from its own file table (`ARC0`'s own index wasn't reverse-engineered -- its two table sizes, 22360 and 28244, don't divide evenly into any byte range this pass could find); recovered 4087 real XPCK archives and 182,649 files (1.3 GB) from that one file, spot-checked with recognizable Level-5 tags (`ATRC01`, `XMPR`) surviving intact |
| **VFF** | `.vff` | ✅ | — | — | — | Nintendo VFF virtual FAT volume (PrFILE2 / eSOL), used by Wii channels and save data. A 0x20-byte big-endian wrapper over an ordinary little-endian FAT12/FAT16 image with the boot sector omitted, so a normal FAT tool cannot open one: two cluster-aligned FAT copies, a fixed 0x1000-byte root directory, then the clusters. Verified against volumes whose filesystems were built by mtools rather than by this project, subdirectories and multi-cluster files included |
| **ZDAT** | `.zdat` | ✅ | — | — | ✅ | Animal Crossing: Pocket Camp asset container (DeNA/Nintendo, mobile). Header, entry array, names, then payloads; each stored file is a Unity `UnityFS` bundle masked with a single repeated byte, recovered from the bundle's own signature rather than from any key. Verified against 31 containers taken from the game's CDN, 1 to 45 entries and 168 files: the entry table closes exactly on the file and every unmasked bundle agrees with the length it records for itself. Extraction stops at the bundle — nothing here reads Unity assets |
| **ZLARC** | `.zlarc` | ✅ | ✅ | ✅ | ✅ | indieszero compressed package archive (*NES Remix*, *NES Remix 2*, *NES Remix Pack*). Verified against the retail *NES Remix Pack* (Wii U) -- 4 `.zlarc` files found (`content/Heri{1,2}/cmn/miiverse/HankoTga*.zlarc`, `content/Heri{1,2}/emu/vew/AllVewKey*.zlarc`), all plain zlib streams (`78 da`). The smallest, `HankoTga.zlarc` (37,882 bytes), decompresses cleanly to 4,968,395 bytes and is committed as `tests/fixtures/zlarc_wiiu_nes_remix_pack_hankotga.zlarc`. The decompressed payload is a flat offset/data blob, not a recognized container: `wszst FILETYPE` reports it as `U8`, but that's a false positive from the generic heuristic scorer -- the payload lacks `U8_MAGIC_NUM` (`55 AA 38 2D`) and `wszst EXTRACT` on it yields no members at all. So for this title "ZLARC" is just zlib-compressed data with no further sub-structure to decode |
| **ZTAB** | `.ztab`, `.tab` | ✅ | ✅ | ✅ | ✅ | Camelot archive table (*Mario Golf: Toadstool Tour*, *Mario Power Tennis*) |

`Byte-Exact Roundtrip` = build → extract → rebuild reproduces the archive's bytes
identically, so the writer's canonical layout is a fixed point of its own reader.
Exercised by `t_container_roundtrip()` in `tests/regress.sh`.

---

### 3D Models & Geometry

| Format | Extensions | Target Output | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **BCH** | `.bch` | **GLB** | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C H3D binary character model (3DS) |
| **BCMDL / CGFX** | `.bcmdl`, `.cgfx` | **GLB** | ✅ | — | — | ✅ | NintendoWare NW4C CGFX 3D model resource (3DS) |
| **BCRES** | `.bcres` | **GLB** | ✅ | — | — | ✅ | NintendoWare NW4C CGFX 3D graphics and model resource container (3DS) |
| **BFRES** | `.bfres` | **GLB** | ✅ | — | — | ✅ | Nintendo GX2 / NintendoSDK 3D model & surface resource archive (Wii U / Switch). Wii U geometry + FSKA skeletal clips export to GLB; Switch v8/v9/v10 geometry decodes via the version-gated BufferInfo pool (verified on Male.bfres: human-scale bboxes, index max = vcount-1). Skeletons follow BfresLibrary's FSKL/Bone layouts incl. v10 0x58-stride bones and quaternion rotations; skinning reads `_i0` indices (verified on 2284 Tomodachi Life v10 files: 2043/2043 geometries, 383 skins). All six Wii U animation entry bodies (FSKA/FSHU/FTXP/FVIS/FSHA/FSCN) structurally decode with per-curve validation (`wszst xx` prints an `ANIM:` summary line); entry layouts per the NintendoWare G3D SDK headers |
| **BIKE (MKAGPDX)** | `.bin` | **GLB** | ❌ | — | — | ❌ | Mario Kart Arcade GP DX 3D vehicle model (`BIKE`). The `BIKE` magic is registered for file-type detection, but no decoder exists anywhere in the codebase to back the "Decode Tested" this row claimed -- `wszst xx` on a real retail `BIKE` file (confirmed present and correctly identified as `MKAGPDX-MDL`) writes zero output |
| **BMD** | `.bmd`, `.bdhc` | **GLB** | ✅ | ✅ | ✅ | — | Early Nintendo DS 3D model format (DS) |
| **BNFM** | `.bnfm` | **GLB** | ✅ | ✅ | ✅ | ✅ | Nd Cube Wii U 3D model format (*Mario Party 10*, *Animal Crossing: amiibo Festival*). A prior pass here found every model on the amiibo Festival disc packed inside an undocumented `PAC\0` container and, after only trying a header-framed zlib/LZMA decompress, wrongly called the whole thing encrypted. A public QuickBMS script for this exact format (RandomTBush/RTB-QuickBMS-Scripts "MP10-Unpacker.bms") surfaced the real answer: every member is just ordinary zlib-compressed data (0x78 0xda header) -- verified directly against the retail disc's `content/common/bin/ch_base/chara/cat/cat00.bin` (20 members: sixteen real GX2 `Gfx2` textures, a `.mcf`, a `.gmo`, and `cat00.bnfm` itself), all decompressing via plain zlib inflate to exactly their header's declared size. See the new **PAC** row below for the container decoder this added (`ExtractPACArchive`, `file-type.c`'s `FF_PAC`) |
| **G1M** | `.g1m` | **GLB** | ✅ | — | — | ❌ | Koei Tecmo 3D model format (*Hyrule Warriors Legends*, 3DS; *Fire Emblem Warriors*). Positions, normals and UVs. Vertex colour is parsed but not exported; all 600 models checked carry a single bone and no blend attributes, so there is no skinning in this corpus to export. The Wii U original *Hyrule Warriors* was checked separately and ships zero bare `.g1m` files anywhere on the disc -- its model data is packed inside an undocumented, non-gzip chunked container (`*.bin.gz`/`*.g1t.gz`, real gzip magic `1f 8b` absent from all 5,720 samples) this project can't decode, so this attribution and its "Decode Tested" pass are 3DS-only |
| **G4PKM** | `.g4pkm` | — | — | — | — | — | Unidentified. Recognised by extension only, and the type table no longer claims a decoder or a cutter for it. The extension occurs in none of the games checked here, including a Level-5 3DS title (*Inazuma Eleven 3*, which uses `.pkh`/`.pkb`), and `pkm` in a Pokémon context names individual save data rather than a model, so the "3D model" this once claimed is unsupported |
| **GLG / RLG** | `.glg`, `.rlg` | **GLB** | ✅ | ✅ | ✅ | ✅ | Next Level Games 3D model format (*Super Mario Strikers*, *Mario Strikers Charged*) |
| **HSD** | `.dat` | **GLB** | ✅ | ✅ | ✅ | ✅ | HAL Laboratory `sysdolphin` object graph (GameCube) |
| **HSF** | `.hsf` | **GLB** | ✅ | ✅ | ✅ | ✅ | Hudson Soft 3D model format (GameCube / Wii) |
| **LMD** | `.lmd` | — | — | — | — | — | Unidentified. Recognised by extension only, with no magic, decoder or cutter. `.lmd` is not a single format — several unrelated programs use it — and no sample backs the *Pokémon Masters* attribution once claimed here |
| **MDL0 / BRRES** | `.mdl0`, `.brres` | **GLB** | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R binary resource model (Wii). Encoding injects into a parent, and anything that did not change is preserved from that parent byte for byte -- an MDL0 quantizes each vertex array to its own type and divisor, shares arrays between objects and draws them from a display list, none of which a GLB can carry, so only what actually changed is rebuilt. Textures travel inside the GLB and are written back on repack: an edited one is re-encoded in the parent's own format, indexed textures keeping their C4/C8 pixels and their sibling PLT0 palette |
| **MOD** | `.mod` | **GLB** | ✅ | ✅ | ✅ | ✅ | Monster Games NDL3/NDL2 display list model (Wii) |
| **MSH (PMsh)** | `.msh` | **GLB** | ✅ | ✅ | ✅ | ✅ | Monster Games collision mesh format (Wii) |
| **NSBMD** | `.nsbmd`, `.bmd` | **GLB** | ✅ | ✅ | ✅ | — | Nintendo DS Nitro 3D model format (DS) |
| **NUD** | `.nud` | **GLB** | ✅ | ✅ | ✅ | — | Bandai Namco 3D model format (*Super Smash Bros. 4* Wii U / 3DS) |
| **NUMSHB** | `.numshb` | **GLB** | ✅ | — | — | ✅ | Bandai Namco SSBH 3D mesh model (*Super Smash Bros. Ultimate* Switch). MESH v1.10: positions, normals, UVs, and skinning when the sibling `.nusktb` skeleton is present. v1.8 shares those field offsets but its attribute array points elsewhere, so positions are recovered from the vertex buffer and kept only when they fall inside the object's own bounding box, giving geometry without normals or UVs; tangents and colour sets are parsed but not exported |
| **MPR CMDL** | `.cmdl` | **GLB** | ✅ | — | — | ✅ | Retro Studios static model (*Metroid Prime Remastered*, Switch): LE RFRM form (`CMDL` v114/125) with HEAD/MTRL/MESH/VBUF/IBUF/GPU chunks plus a trailing FOOT/META buffer table (LZSS mode 0-3 GPU buffers, same dispatch as MPR PACK). Verified against all 687 retail CMDL members (every one yields a valid glTF, incl. a 51-mesh 2.2 MB scene); meshes carry positions + normals/UVs/colours where the vertex components provide them, positions required finite and inside the HEAD AABB. Skinned SMDL (127/133) and material/texture resolution are phase 2 |
| **PERS** | `.pers` | *(raw payload)* | ✅ | — | — | ✅ | Pokémon Stadium (N64) PERS-SZP container: a 24-byte header around a Yay0 stream. Verified against all 410 instances in the retail cart. The payload is not yet parsed, and the sibling `FRAGMENT` signature is a MIPS code overlay, not a model |

`Byte-Exact Roundtrip` = decode → GLB → re-encode reproduces the original file's bytes identically (canonical fixed-point verified), not just a successful encode.

---

### Textures & 2D Graphics

| Format | Extensions | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- |
| **AJPG / ODH** | `.ajpg` | ✅ | ✅ | — | — | ActImagine baseline-JPEG-derived still image format (GBA / Wii Message Board) |
| **ART / IMG** | `.art`, `.img` | ✅ | ✅ | ✅ | ✅ | Monster Games GUI image format (Wii) |
| **BCFNT / BFFNT / BRFNT** | `.bcfnt`, `.bffnt`, `.brfnt` | ✅ | ✅ | ✅ | ✅ | NintendoWare font resource (3DS / Wii U / Wii) |
| **BCLIM** | `.bclim` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C texture container (3DS) |
| **BFLIM** | `.bflim` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4F texture format (Wii U) |
| **BNR** | `.bnr` | ✅ | — | — | — | Nintendo GameCube & Wii game opening banner icon (RGB5A3) |
| **BNTX** | `.bntx` | ✅ | ✅ | ✅ | ✅ | NintendoSDK Tegra block-linear texture container (Switch) |
| **BREFT** | `.breft`, `.bt-img` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R particle effect texture (Wii) |
| **BTI / TPL** | `.bti`, `.tpl` | ✅ | ✅ | ✅ | ✅ | Nintendo standard texture palette library (GameCube / Wii) |
| **Camelot GX bank** | *(none)*, `.stpl`, `.sbn` | ✅ | — | — | ✅ | Camelot GX texture bank, standalone or inline in a model module (*Mario Golf: Toadstool Tour*, *Mario Power Tennis* GC & Wii, *We Love Golf!*) |
| **CTPK** | `.ctpk` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C texture package (3DS) |
| **CTXB** | `.ctxb` | ✅ | ✅ | ✅ | ✅ | Grezzo 3DS texture container (*Ocarina of Time 3D*, *Majora's Mask 3D*). A standalone `.ctxb` -- the common case, since these ship loose in `romfs/` rather than inside a GAR/ZAR -- never reached the decoder at all before this pass (wrong dispatch entirely, not a decode bug); fixed and verified against the retail romfs (1666 files). The decoder walks every texture entry in each `tex ` chunk, not just entry 0 (real romfs files pack several per chunk and entry 0 is not always decodable). Note: the decoder itself still only recovers the first texture of the first `tex ` chunk, and a GAR/ZAR's own extraction doesn't self-cascade into its output the way ABE/BNS/RST/RPAK do, so an embedded `.ctxb` only decodes on a second pass over that output directory |
| **DSB / TXTR** | `.bin` | 🟡 | — | — | — | Animal Crossing: Wild World DS menu texture (RGB555 + A3I5). No standalone file carrying the `TXTR` magic turned up anywhere across the retail cart's ~18,100 extracted files, so this pass couldn't confirm it against real data; it may only ever appear embedded in ARM9/overlay code rather than as its own file |
| **Retro TXTR** | `.txtr` | ✅ | ✅ | ✅ | — | Retro Studios texture, old revision (*Metroid Prime 1-3*, *Donkey Kong Country Returns*, Wii): BE header (GX format 0-0xA, dimensions, mip count), optional palette header, GX-tiled mip chain. Non-indexed formats decode via the shared GX tile codec and re-encode byte-exact (single mip); C4/C8 indexed decode via the sibling palette, C14X2 is rejected (unused by official textures). `wimgt DECODE/ENCODE` + `wszst xx` |
| **Tropical TXTR** | `.txtr` | ✅ | — | — | — | Retro Studios texture, new revision (*Donkey Kong Country: Tropical Freeze*, Wii U): RFRM form (`TXTR` id) with HEAD parameters and LZSS-compressed (modes 0-3, zlib fallback) GX2 surface data. Decodes 2D depth-1 surfaces via the GX2 detiler (base mip); cubemaps/arrays and Tropical re-encode are not implemented. `wimgt DECODE` + `wszst xx` |
| **MPR TXTR** | `.txtr` | ✅ | — | — | ✅ | Retro Studios texture, Remastered revision (*Metroid Prime Remastered*, Switch): LE RFRM form (`TXTR` v47/51) + HEAD + GPU buffers assembled from the FOOT META table, detiled with the Tegra block-linear path and pixel-decoded incl. BC1-7/ASTC. Verified against the retail `MiscData.pak` TXTR (232x232 R8Unorm) plus real ASTC/BC7 game textures. `wimgt DECODE` + `wszst xx` (PACK extract leaves a cascade `.TXTR.png`). An `RFRM` form nothing in the family claims reports UNKNOWN rather than falling through to the LZ10/LZ11 single-byte guesses (real TXTRs used to misreport as LZ streams, aborting image decode) |
| **G1T** | `.g1t` | ✅ | — | — | ❌ | Koei Tecmo texture container (*Hyrule Warriors Legends*, 3DS; *Fire Emblem Warriors*). 3DS ETC1/ETC1A4/RGBA8 — 2602 of the 2603 textures on the *Hyrule Warriors Legends* cart; the one holdout uses an 8bpp encoding no other file exercises. The Wii U original *Hyrule Warriors* was checked separately: almost all of its textures are wrapped in an undocumented, non-gzip chunked container (`*.g1t.gz`, no real gzip magic on any of 5,720 samples) this project can't decode, but 3 real unwrapped `.g1t` files do exist (`content/data/{deferred/rain,gallery/GalleryEnvMap,posteffect/BlurWeight}.g1t`) and `wszst FILETYPE` correctly tags them `G1T` -- yet `wszst X`/`XX` extracts none of them, because `ExtractG1TArchive()` (`lib-nintendo-archives.c:3350`) only accepts the little-endian 3DS magic/field order (`GT1G0600...`); all three Wii U files carry the fully byte-reversed big-endian variant (`G1TG0060...`) that this decoder has no path for. Not fixed here (out of scope for a verification-only pass); see PLAN.md #16 |
| **GTX** | `.gtx` | ✅ | ✅ | ✅ | ✅ | Nintendo Wii U GX2 surface container (Wii U) |
| **GVR** | `.gvr` | ✅ | — | — | — | Sega GameCube & Wii texture container (GCIX / GVRT) |
| **NDS banner** | `banner.bin` | ✅ | — | — | ✅ | Nintendo DS ROM banner: 32x32 icon (plus DSi animated icon frames) and per-language titles (DS / DSi). A DSi-animated banner's classic static icon (0x20/0x220 -- what a plain DS shows) and its 8-frame animated icon (0x1240/0x2240 -- the DSi HOME Menu tile) are independent images; `ScanNDSBanner()` used to silently overwrite the static one with animation frame 0 whenever a banner was animated, so the static icon export was wrong for every DSi title. Fixed by giving the static icon dedicated fields |
| **Wii banner** | `opening.bnr`, `IMET`, `IMD5` | ✅ | — | — | ✅ | Wii channel/disc banner: IMET header (per-language titles, MD5 verified) plus the inner U8 whose IMD5 (and optional `LZ77`) wrapped members expand to BRLYT/BRLAN/TPL/BNS |
| **WIBN** | `banner.bin`, `.bnr` | ✅ | — | — | — | Wii *save game* banner: a 192x64 RGB5A3 banner image plus up to 8 48x48 icon animation frames, with the title/subtitle pair; the frame count follows from the file size, and trailing all-zero frames are padding |
| **NCER / NANR** | `.ncer`, `.nanr` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro cell & animation resources (DS) |
| **NCGR / NCLR** | `.ncgr`, `.nclr` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro 2D graphics & palette (DS) |
| **NSCR** | `.nscr` | ✅ | — | — | ✅ | Nintendo DS Nitro screen/tilemap resource, rendered against its NCGR tiles and NCLR palette (DS) |
| **NSBCA / NSBTA / NSBTP / NSBVA / NSBMA** | `.nsbca`, `.nsbta`, `.nsbtp`, `.nsbva`, `.nsbma` | **GLB** *(NSBCA, via NSBMD siblings)* | ✅ | — | — | ✅ | Nintendo DS Nitro animation family (joint, texture SRT, texture pattern, visibility, material colour). NSBCA joints decode to a GLB animation when the sibling NSBMD is exported, keyframes verified against the Nitro SDK animation engine; encoding a bare NSBCA is a byte-exact pass-through, and a GLB animation re-encode rebuilds a fresh SDK-conformant NSBCA (pivot/ROT5 FX12 fixed-point) whose decode round-trips frame-for-frame; the other four are validated structurally and passed through unchanged |
| **NSBTX** | `.nsbtx` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro 3D texture container (DS) |
| **NUT** | `.nut` | ✅ | ✅ | ✅ | ✅ | Bandai Namco texture package (*Super Smash Bros. 4* Wii U / 3DS). Verified against the retail Wii U disc: an `NTP3` texture carved out of `content/dt00` (addressed via `content/ls`, zlib-inflated) decodes to a real DDS (`tests/fixtures/nut_wiiu_smash4_texture.nut`) |
| **NUTEXB** | `.nutexb` | ✅ | ✅ | ✅ | — | Bandai Namco / Nintendo Switch texture wrapper (Switch) |
| **PTLG** | `.glt`, `.rlt` | ✅ | ✅ | ✅ | — | Next Level Games texture container, extracted as TPL (*Super Mario Strikers*, *Mario Strikers Charged*) |
| **SMDH** | `.smdh` | ✅ | — | — | ✅ | Nintendo 3DS application icon, publisher info & title metadata |
| **TEX** | `.tex` | ✅ | ✅ | ✅ | ✅ | Monster Games GX texture format (Wii) |
| **TM0** | `.tm0` | ✅ | — | — | ✅ | Monster Games high-resolution texture (*Excite Truck*, Wii): an explicit header at 0x80 followed by a CMPR colour mip chain and, for renderer code 0x44, an I4 stencil chain that supplies the alpha. Both chains are 4bpp over 8x8 tiles and so identical in length, and nothing in the header names them apart. ExciteBots ships a headerless variant of the same container, which is not decoded yet |
| **CAN** | `.can` | ✅ | — | — | ✅ | Monster Games skeletal animation (*Excite Truck* / *ExciteBots*, Wii), converted to a GLB with the node hierarchy and one rotation/translation/scale channel per node. No magic: a little-endian header, 0x64-byte node records with a column-major rest matrix, and 36-byte keys of quaternion + translation + uniform scale + time. Verified across all 973 nodes of the 29 non-empty retail animations: hierarchy, rest pose, key values and duration all reproduced |
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
| **BARS** | `.bars` | ✅ | — | — | Nintendo Binary Audio Resource Archive (Wii U / Switch) |
| **BCSAR / BCWAR / BCWAV** | `.bcsar`, `.bcwar`, `.bcwav` | ✅ | ✅ | ✅ | NintendoWare NW4C sound archive & wave format (3DS) |
| **BFSAR / BFWAR / BFWAV** | `.bfsar`, `.bfwar`, `.bfwav` | ✅ | ✅ | ✅ | NintendoWare NW4F & NintendoSDK sound archive & wave format (Wii U / Switch). SYMB-less archives unpack too: `wbfsar extract` carves every FILE-pool asset via the FileInfo image reference (verified on YWW pj023.bfsar: 821 assets incl. 811 FSEQ). Sound->File names use the verified 12-byte STRG records |
| **BRSAR / RBNK / RWAV** | `.brsar`, `.rbnk`, `.rwav` | ✅ | ✅ | ✅ | NintendoWare NW4R sound archive, instrument bank & wave format (Wii) |
| **BRSTM / BCSTM / BFSTM** | `.brstm`, `.bcstm`, `.bfstm` | ✅ | ✅ | ✅ | Nintendo multi-channel stream audio (Wii / 3DS / Wii U / Switch) |
| **NUS3AUDIO** | `.nus3audio`, `.nus3bank` | ✅ | ✅ | — | Bandai Namco NUS3 audio archive (*Super Smash Bros. Ultimate* Switch) |
| **RSEQ / CSEQ / FSEQ / SSEQ** | `.rseq`, `.cseq`, `.fseq`, `.sseq` | ✅ | ✅ | ✅ | Nintendo sequence music format (Wii / 3DS / Wii U / DS). Disassembler handles the full MML set incl. extended (F0) commands, IF/TIME/RANDOM/VARIABLE prefixes and real LABEL names; FSEQ uses the true block-table container. Verified: 811/811 retail Wii U sequences round-trip byte-exact. Wii RSEQ verified too (direct-offset container, compact labels, code-exact round-trips) |
| **SADL** | `.sad`, `.sadl` | ✅ | — | ✅ | Level-5 / *Professor Layton* audio stream container (DS) |
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
| **ASH0** | `ASH0` | ✅ | ✅ | Nintendo Huffman+LZSS stream (Wii System Menu, Animal Crossing, My Pokémon Ranch; 11/15-bit distance fallback) |
| **BLZ** | ARM9 overlay trailer | ✅ | ✅ | Nintendo DS Nitro backward LZ overlay compression |
| **BPE / GFCP** | `GFCP` (zip mode 1) | ✅ | ✅ | Good-Feel Byte Pair Encoding (Wii Kirby's Epic Yarn / Yoshi's Woolly World) |
| **Bzip2** | `BZh` | ✅ | ✅ | Standard high-compression block-sorting codec |
| **Camelot LZ** | `0x01` / `0x02` prefix | ✅ | ✅ | Camelot Software Planning LZ77 compression (*Mario Golf*, *Mario Tennis* GameCube / Wii) |
| **Deflate / Zlib** | `78 01`, `78 9C`, `78 DA` | ✅ | ✅ | Standard RFC 1950 / 1951 stream compression |
| **Diff8 / Diff16** | `0x81`, `0x82` | ✅ | ✅ | Nintendo DS differential delta filter encoding |
| **FZIP** | `FZIP` | ✅ | ✅ | *Game & Wario* Zlib stream container (Wii U). Verified against the retail cart: `content/Common/Script.warc.fzip` (76,945 bytes) decompresses via `wszst DECOMPRESS` to a 771,575-byte payload that is itself a valid WARC archive (see the WARC row above), extracting to 241 non-empty members. Committed as `tests/fixtures/fzip_wiiu_game_and_wario_script.warc.fzip` |
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
| **7-Zip / RAR / Tar / Gzip Archives** | `.7z`, `.rar`, `.cb7`, `.tar`, `.tgz`, `.tbz2`, `.txz`, `.gz` | **`7z`** / **`7zz`** / **`7za`** / **`unar`** (`--with-7z`) | General archive unpacking, including a lone gzip-wrapped file (e.g. a Wii U retail asset shipped as `name.pack.gz`) |
| **Custom Binary Containers** | Arbitrary formats | **`QuickBMS`** (`--bms=<script.bms>`) | Direct execution of QuickBMS extraction scripts |
| **DSP-ADPCM Audio Streams** | `.brstm`, `.bcstm`, `.bfstm`, `.bns`, `.btsnd`, `.ast`, `.dsp` | **`mobipeg`** | Bit-exact Nintendo THP ADPCM coefficient search & stream encoding |
| **Mobiclip Video & Cutscenes** | `.mo`, `.mods`, `.moflex`, `.MOC`, `.MOD` | **`mobipeg`** (`--with-mobipeg`) / **`ffmpeg`** | Nintendo DS / 3DS / Wii Mobiclip video decoding to MP4 |
| **Nintendo 3DS Containers** | `.3ds`, `.cci`, `.cxi`, `.cfa`, `.cia`, `.app` | **`ctrtool`** / **`makerom`** (`--with-ctrtool`) | NCCH/NCSD partition extraction, ExeFS/RomFS unpacking & CIA installation packages |
| **Nintendo DS / DSi ROMs** | `.nds`, `.srl`, `.dsi` | **`ndstool`** (`--with-ndstool`) | Nitro ROM header, banner, arm9/arm7 binary & NitroFS extraction/rebuild |
| **Nintendo Switch Packages** | `.nsp`, `.xci`, `.nca` | **`hactool`** / **`hacbrewpack`** (`--with-hactool`, `--with-hacbrewpack`) | PFS0 / HFS0 / NCA content extraction & homebrew NSP repacking |
| **THP & Media Video** | `.thp`, `.h4m`, `.vid`, `.dpg`, `.fv`, `.ppm`, `.kwz`, `.mmstr`, `.rvid`, `.vx` | **`mobipeg`** / **`ffmpeg`** | GameCube/Wii THP, HVQM4, DPG, FastVideo & Flipnote animation decoding. Factor 5 VID1 DivX (`.vid`, GameCube): `VID1`-magic files decode to a `.mp4` preview via an external VID1 decoder (`VID1DEC=/path/to/binary`, else `NeversoftMultitool` on PATH) — stock ffmpeg/mobipeg cannot read the proprietary VIDD macroblock stream (verified against retail *Carmen Sandiego* discs: a standards-conformant remux opens but decodes to noise); without the decoder they are skipped, and there is deliberately no native demuxer. Bare-`.vid` files with no VID1 magic still go to the media tools |
| **SFX** | `.sfx` | **`mobipeg`** / **`ffmpeg`** | Monster Games DSP-ADPCM audio (*Excite Truck*, *ExciteBots*, Wii). A 0x80 header over a plain Nintendo DSP-ADPCM stream: sizes and sample rate little-endian, nibble count and coefficients in the usual big-endian DSP form. No magic, so a file identifies itself by its payload size accounting for the rest of the file and a byte rate twice the sample rate. Wrapped in GENH and decoded through the audio pass-through rather than re-implementing adpcm_thp; verified bit-identical to that decoder across all 145 effects on the disc |
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
