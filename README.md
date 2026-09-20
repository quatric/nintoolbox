# nintoolbox

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

**This program lets you unpack a whole game recursively (with wszst xx) and then pack it back up recursively (with wszst CREATE). Note that the latter functionality is quite experimental**

```bash
# 1. Extract any archive or ROM (SZS, U8, RARC, SARC, NARC, DARC, NDS, etc.)
wszst xx Track.szs
wszst xx Game.nds

# 2. Rebuild an extracted directory back into an archive
wszst CREATE Track.d --dest Track.szs

# 3. Convert 3D models to standard GLB (.glb)
wmdlt DECODE Mario.mdl0 --dest Mario.glb
wmdlt DECODE Course.bfres --dest Course.glb
wmdlt DECODE Model.bmd --dest Model.glb        # GameCube/Wii J3D (SuperBMD-compatible)
wmdlt ENCODE Mario.glb --dest Mario.hsf
wmdlt ENCODE custom.glb --dest custom.bmd      # GameCube/Wii J3D BMD (or .bdl)

# 4. Convert Nintendo textures to PNG
wimgt DECODE texture.tpl --dest texture.png
wimgt DECODE texture.bntx --dest texture.png
wimgt DECODE animation.cmab --dest texture.png # emits one PNG per embedded texture
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
| **ABE BigFile** | `.bf` | ✅ | — | — | ✅ | Ubisoft *Rabbids Go Home* BigFile archive (ABE\0 with segmented LZO1X chunks). |
| **ALAR** | `.alar` | ✅ | — | — | ✅ | Nintendo DS Nitro ALAR archive (*Jump Ultimate Stars*). |
| **And-Kensaku** | `.rz` | ✅ | — | — | — | CyberConnect2 "Pres" archive with LZ77 wrapping (Nintendo DS, *And-Kensaku* family). |
| **APAK** | `.apak` | ✅ | ✅ | ✅ | — | Nintendo / Pokémon APAK archive format (Wii U / Switch) |
| **ARC / U8** | `.arc`, `.szs` | ✅ | ✅ | ✅ | — | Nintendo standard U8 archive (Wii / GameCube NintendoWare & EAD) |
| **ARC0** | `.fa` | 🟡 | — | — | 🟡 | Level-5 flat archive (*Yo-Kai Watch*, 3DS). |
| **ARCV** | `.arc` | ✅ | ✅ | ✅ | — | Namco / Tose Wii archive format |
| **Arika Archive** | `INFO.DAT`, `GAME.DAT`, `.arika` | ✅ | ✅ | ✅ | — | Arika DS / DSi / Wii archive system |
| **Asobo BigFile** | `.drv` | ✅ | — | — | ✅ | Asobo Studio volume (*Ratatouille*, Wii): textures to PNG, sounds to WAV, meshes to GLB. |
| **AT7** | `.at7` | ✅ | ✅ | ✅ | — | Koei Tecmo container format (Wii / PS2) |
| **ATB** | `.atb` | ✅ | — | — | — | Hudson Soft Animation Texture Bank (*Mario Party 4-8*). |
| **Atomic Planet PUB** | `.pub` | ✅ | — | — | ✅ | *AMF Bowling: Pinbusters!* (Wii): textures to PNG, meshes to GLB. |
| **Avalanche THB/TBB** | `.thb` + `.tbb` | ✅ | — | — | ✅ | Avalanche Software texture pair (*Cars 2*, Wii); single textures and atlases to PNG |
| **BCGRP** | `.bcgrp` | ✅ | ✅ | — | — | Nintendo 3DS Sound Group (`CGRP`). |
| **BEA** | `.bea`, `.nx.bea` | ✅ | — | — | — | Nintendo EAD Bezel Engine Archive (`SCNE`, *WarioWare* / *Mario Party*). |
| **BFGRP** | `.bfgrp` | ✅ | ✅ | — | — | Nintendo Wii U / Switch Sound Group (`FGRP`). |
| **BFMA** | `.bfma` | ✅ | ✅ | — | — | Nintendo Wii U manual archive (SARC-based). |
| **BFSHA / BNSH** | `.bfsha`, `.bnsh` | ✅ | — | — | ✅ | NintendoWare shader archive (Wii U / Switch). |
| **BG4** | `.bg4` | ✅ | ✅ | ✅ | — | AlphaDream 3DS flat archive with BLZ member compression |
| **BIGF** | `.big` | ✅ | ✅ | ✅ | — | Electronic Arts Wii asset archive |
| **Bj engine** | `.tx1`/`.tx2`, `.mtm`, `.bsi`, `.bsm` | ✅ | — | — | ✅ | *Super Karts* (Wii): textures to PNG, meshes to GLB, sounds and music to WAV. |
| **Blue Tongue TRB** | `.trb` | ✅ | — | — | ✅ | *de Blob 2* (Wii): `ttex` textures (I4 to CMPR, paletted) to PNG, embedded XUR UI scenes, `tcmd` level and prop geometry to GLB (positions, normals, UVs; no materials; some batch variants skipped). |
| **BNS Archive** | `.bns` | ✅ | — | — | ✅ | Koei Tecmo *Samurai Warriors 3* multi-file asset archive (`LINKDATA*.BNS`). |
| **BombShell data pack** | `.xwi`, `.xdx9` | ✅ | — | — | ✅ | Smart Bomb Interactive engine (*Bee Movie Game*, Wii / PC): textures to PNG, sounds to FSB / WAV, Wii models to textured GLB. |
| **CA01 / SA01** | `.ca01`, `.sa01` | ✅ | ✅ | ✅ | — | Nintendo Network Mii & amiibo system archive (3DS / Wii U) |
| **CCF** | `.ccf` | ✅ | ✅ | ✅ | — | Nintendo Virtual Console container (Wii / Switch) |
| **CDGaCube CAR** | `.car` | ✅ | — | — | ✅ | Cat Daddy Games archive (*Birthday Party Bash*, Wii); stored and zlib members |
| **CNUT** | `.cnut` | ✅ | ✅ | ✅ | ✅ | *Wii Party* compiled Squirrel script & message container (`SQIR`). |
| **COD PAK0** | `.pak` | ✅ | ✅ | ✅ | ✅ | *Call of Duty: Black Ops* / *MW3* (Wii) sound archive (`PAK0`). |
| **CPK** | `.cpk` | ✅ | — | — | ✅ | CRIWARE CPK archive (*Star Fox Zero*, Wii U) |
| **CRAM** | `.arc`, `.cram` | ✅ | ✅ | ✅ | — | Monolith Soft 3DS flat archive container |
| **DARC / BCMA** | `.darc`, `.bcma`, `.arc` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C directory archive & 3DS electronic manual archive (3DS). |
| **DC2 DCX / DCT** | `.dcx`, `.dct` | ✅ | — | — | — | *Jakers! Kart Racing* (Wii): directory archives; GX CMPR / RGBA8 textures → PNG |
| **DTLS** | `dt00`, `ls00`, `.ls` | ✅ | ✅ | ✅ | ✅ | Bandai Namco composite package & lookup archive (*Super Smash Bros. 4*, Wii U / 3DS) |
| **EFFN** | `.eff`, `.effn` | ✅ | ✅ | ✅ | ✅ | Bandai Namco Super Smash Bros. 4 / Ultimate particle effect container archive (`EFFN`). |
| **F9RES** | `.res` | ✅ | ✅ | ✅ | — | GameCube resource archive container |
| **FMOD FSB** | `.fsb` | ✅ | — | — | ✅ | FMOD sound bank (FSB3 / FSB4 / FSB5, Wii); DSP-ADPCM and PCM16 samples decoded to WAV |
| **FSYS** | `.fsys` | ✅ | ✅ | ✅ | — | Genius Sonority archive system (GameCube / Wii) |
| **GAR / ZAR** | `.zar`, `.gar` | ✅ | ✅ | ✅ | ✅ | Grezzo Zelda & Luigi's Mansion archive (*OoT3D*, *MM3D*, *LM3DS*). |
| **GFA** | `.gfa` | ✅ | ✅ | ✅ | ✅ | Good-Feel GFAC container (Wii / 3DS / Wii U). |
| **GFLX** | `.gflxpack` | ✅ | — | — | — | Game Freak Switch `GFLXPACK` archive (SPICA `GFLXPack`): raw-LZ4 members (BNTX/BNSH/`gfbmdl`/raw). |
| **GFMPACK** | `.gfpack` | ✅ | — | — | — | Game Freak 3DS model / texture / shader pack (*Pokémon X/Y/ORAS*). |
| **GFPAK** | `.gfpak` | ✅ | — | — | — | Game Freak Pokémon archive (`GFLXPACK`). |
| **GFPKG** | `.gfpkg`, `.bin`, `.pak` | ✅ | — | — | — | Game Freak Gen6/Gen7 package (SPICA `GFPackage`; 2-letter magic + offset table). |
| **Grip RES** | `.res` | ✅ | — | — | — | *Sesame Street: Elmo's Musical Monsterpiece* (Wii): resource packages, `surf` textures to PNG. |
| **h.a.n.d. FBC** | `.fbc` | ✅ | — | — | ✅ | *Oyako de Asobo: Miffy no Omochabako* (Wii): nested file bundles unpacked by name. |
| **HBDF** | `.hbdf`, `.hsdf` | ✅ | — | — | — | Hudson Soft Nitro 3D model container (*Mario Party DS*), decoded to GLB. |
| **Heavy Iron HO** | `.ho` | ✅ | — | — | ✅ | Good Engine package (*WALL-E*, *Up*, Wii); assets with debug names, GX textures to PNG |
| **Humongous Resource.rez** | `.rez` | ✅ | — | — | ✅ | *Backyard Football '10* (Wii): textures to PNG, sounds to WAV, models and animations to GLB, THP videos. |
| **Hyrule Warriors** | `.idx`, `.bin` | ✅ | ✅ | ✅ | — | Koei Tecmo / Omega Force split index archive (3DS) |
| **IPK** | `.ipk` | ✅ | — | — | — | Ubisoft UbiArt archive (*Just Dance*, *Rayman Origins/Legends*; Wii / Wii U / Switch / PC). |
| **IQIPACK** | `.pak` | ✅ | — | — | — | NVIDIA Shield iQiyi PAK archive with XXTEA encryption |
| **JARC** | `.jarc` | ✅ | ✅ | ✅ | — | Level-5 DS archive container (DS) |
| **KPBIN** | `.kpbin` | ✅ | ✅ | — | — | Koopatlas binary world map (`KP_m`, *New Super Mario Bros. Wii* level-editor community format). |
| **LSPK** | `.pk`, `.pkh`, `.lspk` | ✅ | ✅ | ✅ | ✅ | Level-5 / Mistwalker flat package (*The Last Story*). |
| **LZBIN** | `.bin`, `.lzbin` | ✅ | — | — | — | Hudson Soft Nitro compressed archive (*Mario Party DS*). |
| **MDR** | `.mdr` | ✅ | ✅ | ✅ | — | *Dance Dance Revolution Mario Mix* chunk archive with per-chunk zlib streams |
| **MKGPDX PAC** | `.pac`, `.mkgpdx` | ✅ | ✅ | ✅ | ✅ | *Mario Kart Arcade GP DX* layout archive (`pack`). |
| **MPBIN** | `.bin` | ✅ | ✅ | ✅ | ✅ | Hudson Soft Mario Party archive container (GameCube / Wii) |
| **MPR PACK** | `.pak` | ✅ | — | — | ✅ | Retro Studios asset container (*Metroid Prime Remastered*, Switch). |
| **MSR** | `.pkg`, `.bin` | ✅ | — | — | ✅ | *Metroid: Samus Returns* (3DS) flat archive container (`.pkg`). |
| **MTXT** | `.mtxt` | ✅ | ✅ | ✅ | — | Nintendo Switch MTXT texture archive (gzip-wrapped XTX). |
| **NARC** | `.narc` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro standard archive (DS / DSi) |
| **NCCARC** | `.nccarc` | ✅ | ✅ | ✅ | — | Nintendo DS flat blob container |
| **NDS / SRL / DSI** | `.nds`, `.srl`, `.dsi` | ✅ | — | — | — | Nintendo DS & DSi ROM images and executables |
| **NIF (Gamebryo 20.6, Wii)** | `.nif` | ✅ | — | — | — | *Pocoyo Racing* (Wii): textures to PNG, `NiMesh` scenes to textured GLB. |
| **NLG DICT** | `.dict`, `.data` | ✅ | — | — | ✅ | Next Level Games dictionary archive (*Federation Force*, *Luigi's Mansion 2/3*, *Strikers*). |
| **NXARC** | `.nxarc` | ✅ | ✅ | ✅ | — | Nintendo Switch NX archive (`RAXN`) |
| **PAC (Nd Cube)** | `.bin` | ✅ | ✅ | ✅ | ✅ | Nd Cube Wii U flat container (`PAC\0`, *Mario Party 10* / *Animal Crossing: amiibo Festival*). |
| **PAC / MRG** | `.pac`, `.mrg` | ✅ | ✅ | ✅ | — | HAL Laboratory / Game Arts Wii archive container |
| **PKG / GPKG / GPAK** | `.pkg`, `.pak`, `.gpak` | ✅ | ✅ | ✅ | ✅ | Gorilla Games *Bonsai Barber*, 2D Boy *World of Goo* and Sonic Team Storybook archives. |
| **PKZ** | `.pkz` | ✅ | ✅ | ✅ | — | PlatinumGames archive format (*Bayonetta*, *Astral Chain*) |
| **PRC** | `.prc`, `.param` | ✅ | — | — | — | Smash parameter binary (Ultimate `paracobn` decoded to ParamXML; Smash 4 variants recognised). |
| **PTD** | `.ptd`, `.pdt` | ✅ | ✅ | ✅ | — | Hudson Soft DSP-ADPCM audio archive (*Mario Party 4-8*); streams extract to `.dsp`. |
| **PVOL** | `.pvol` | 🟡 | ✅ | ✅ | 🟡 | *Pikmin 1 & 2* model & resource container archive. |
| **RARC** | `.rarc`, `.arc` | ✅ | ✅ | ✅ | — | Nintendo standard resource archive (GameCube / Wii) |
| **RFL_Res** | `RFL_Res.dat`, `.dat` | ✅ | ✅ | ✅ | ✅ | Revolution Face Library Mii resource database (Wii / 3DS / Wii U). |
| **RPAK** | `.rpak`, `.pak` | ✅ | ✅ | — | ✅ | Retro Studios asset container (*Donkey Kong Country Returns*, Wii). |
| **RST / TOC** | `.rst`, `.toc` | ✅ | ✅ | ✅ | ✅ | Monster Games archive & table of contents (*Excite Truck* / *Excitebots*). |
| **RZPK** | `.rzpk` | ✅ | ✅ | ✅ | — | Mario Party 3DS compressed archive (zlib members). |
| **SARC** | `.sarc`, `.szs` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4F & NintendoSDK sorted archive (Wii U / Switch / 3DS). |
| **SFZDAT** | `.dat` | ✅ | 🟡 | — | ✅ | *Star Fox Zero* (Wii U) flat archive (`DAT\0`). |
| **SHARC / SHARCFB** | `.sharc`, `.sharcfb` | ✅ | ✅ | — | ✅ | NintendoWare shader source and binary archive (Wii U / Switch). |
| **SIR0** | `.sir0` | ✅ | ✅ | ✅ | ✅ | Pokémon Mystery Dungeon resource container (DS / 3DS). |
| **SMASH-ARC** | `.arc` | ✅ | ✅ | — | — | *Super Smash Bros. Ultimate* `data.arc` (Switch). |
| **Storybook ONE** | `.one` | ✅ | ✅ | — | ✅ | Sonic Team *Sonic and the Secret Rings* / *Black Knight* PRS-compressed container. |
| **STPK** | `.srd`, `.stpk` | ✅ | ✅ | ✅ | 🟡 | *Jump Super Stars* & *Jump Ultimate Stars* DS resource archive. |
| **Terminal Reality POD** | `.pod` | ✅ | — | — | ✅ | POD3/4/5 archive (*Nickelodeon Dance*, Wii); stored members with a directory tree |
| **TMPK** | `.pack`, `.tmpk` | ✅ | ✅ | ✅ | ✅ | *The Legend of Zelda: Twilight Princess HD* archive (`TMPK`). |
| **Torus Games hunkfile** | `.hnk` | ✅ | — | — | ✅ | *Barbie & Her Sisters: Puppy Rescue* (Wii): textures to PNG, DSP-ADPCM streams to WAV. |
| **Toshi TSFB** | `.ttl` (`.trb`, `.tkl`) | ✅ | — | — | ✅ | *Nickelodeon Barnyard* (Wii): compressed containers, `.ttl` texture libraries to PNG. |
| **TRPAK** | `.trpak` | ✅ | — | — | — | Nintendo Switch "tr Package" FlatBuffers archive (no magic; recognized by extension). |
| **UE4 PAK** | `.pak` | ✅ | ✅ | — | — | Unreal Engine 4 archive (*Mario & Luigi: Brothership*, Switch). |
| **Vblank BFP / BAP** | `.bfp`, `.bap` | ✅ | — | — | ✅ | Vblank Entertainment Wii packages (*Retro City Rampage DX*, *Shakedown: Hawaii*): game data, audio, chiptune modules. |
| **VCRA** | `.bin`, `.vcra` | ✅ | ✅ | ✅ | ✅ | Bandai Namco Museum Remix archive format (Wii). |
| **VFF** | `.vff` | ✅ | — | — | — | Nintendo VFF virtual FAT volume (PrFILE2 / eSOL), used by Wii channels and save data. |
| **VFXB / PTCL** | `.ptcl`, `.eset`, `.vfxb` | ✅ | ✅ | ✅ | ✅ | NintendoWare particle effect binary archive (Wii U / Switch). |
| **VIBS** | `.vibs` | ✅ | ✅ | ✅ | — | Nintendo Switch Joy-Con vibration archive |
| **WADH** | `.wad` (DataWII.wad) | ✅ | — | — | — | Data Design Interactive archive (*Ninjabread Man*, Wii); stored members with a directory tree |
| **WARC** | `.warc` | ✅ | ✅ | ✅ | ✅ | Nintendo / Intelligent Systems flat archive (Wii U). |
| **WTA / WTP** | `.wta` + `.wtp` | ✅ | — | — | ✅ | PlatinumGames texture bundle (*Star Fox Zero*, Wii U) |
| **WUD / WUX** | `.wud`, `.wux` | ✅ | — | — | — | Nintendo Wii U optical disc images (raw & compressed) |
| **XMSG** | `.bin` | ✅ | ✅ | — | — | *Wii Party* message / text archive (`mess.bin`, `XMSG`). |
| **XPCK** | `.xc`, `.xpck` | ✅ | ✅ | ✅ | ✅ | Level-5 container archive (*Inazuma Eleven*, *Professor Layton*, *Yo-kai Watch*). |
| **ZDAT** | `.zdat` | ✅ | ✅ | — | ✅ | Animal Crossing: Pocket Camp asset container (DeNA/Nintendo, mobile). |
| **ZLARC** | `.zlarc` | ✅ | ✅ | ✅ | ✅ | indieszero compressed package archive (*NES Remix*, *NES Remix 2*, *NES Remix Pack*). |
| **ZTAB** | `.ztab`, `.tab` | ✅ | ✅ | ✅ | ✅ | Camelot archive table (*Mario Golf: Toadstool Tour*, *Mario Power Tennis*) |

`Byte-Exact Roundtrip` = build → extract → rebuild reproduces the archive's bytes
identically, so the writer's canonical layout is a fixed point of its own reader.
Exercised by `t_container_roundtrip()` in `tests/regress.sh`.

---

### 3D Models & Geometry

| Format | Extensions | Target Output | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **ADJB** | `.adjb` | **TXT manifest** | ✅ | — | — | — | Bandai Namco mesh adjacency sidecar (*Super Smash Bros. Ultimate*). |
| **BCH** | `.bch` | **GLB** | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C H3D binary character model (3DS) |
| **BCMDL / CGFX** | `.bcmdl`, `.cgfx` | **GLB** | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C CGFX 3D model (3DS): skinned GLB, textures to PNG. |
| **BCRES** | `.bcres` | **GLB** | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C CGFX graphics and model container (3DS). |
| **BFRES** | `.bfres` | **GLB** | ✅ | — | — | ✅ | Nintendo GX2 / NintendoSDK 3D model & surface resource archive (Wii U / Switch). |
| **BMD** | `.bmd`, `.bdhc` | **GLB** | ✅ | ✅ | ✅ | — | Early Nintendo DS 3D model format (DS) |
| **BNFM** | `.bnfm` | **GLB** | ✅ | ✅ | ✅ | ✅ | Nd Cube Wii U 3D model (*Mario Party 10*, *Animal Crossing: amiibo Festival*) with `.bnfmsa` animations. |
| **CSB** | `.csb` | **GLB** | ✅ | ✅ | — | ✅ | Paper Mario collision scene (TTYD, Origami King, Color Splash): meshes and trigger volumes. |
| **CTB** | `.ctb` | *(text dump)* | ✅ | — | — | ✅ | Paper Mario collision search table, regenerated beside every CSB. |
| **FEDMODEL** | `.fedmodel` | **GLB** | ✅ | — | — | — | Next Level Games model container (Federation Force / LM2 / LM3), decoded to GLB. |
| **FEDSKEL** | `.fedskel` | **GLB** | ✅ | — | — | — | Next Level Games skeleton container (Federation Force / LM2 / LM3), joints exported to GLB. |
| **G1M** | `.g1m` | **GLB** | ✅ | — | — | ❌ | Koei Tecmo 3D model format (*Hyrule Warriors Legends*, 3DS; *Fire Emblem Warriors*). |
| **G4PKM** | `.g4pkm` | — | — | — | — | — | Unidentified. |
| **GF1MOT** | `.gf1mot` | *(text dump)* | ✅ | — | — | — | Game Freak XY/ORAS bone-motion pack. |
| **GFBANM** | `.gfbanm` | *(identification only)* | — | — | — | — | Game Freak FlatBuffer animation (recognized for extraction boundaries; not decoded). |
| **GFBMDL** | `.gfbmdl` | *(identification only)* | — | — | — | — | Game Freak FlatBuffer model (recognized for extraction boundaries; not decoded). |
| **GFMODEL** | `.gfmodel` | **GLB** | ✅ | — | — | — | Game Freak 3DS model (*Pokémon X/Y/ORAS*): skeleton, materials, skinned geometry. |
| **GFMOT** | `.gfmot` | *(text dump)* | ✅ | — | — | — | Game Freak 3DS skeletal / material / visibility motion. |
| **GLG / RLG** | `.glg`, `.rlg` | **GLB** | ✅ | ✅ | ✅ | ✅ | Next Level Games 3D model format (*Super Mario Strikers*, *Mario Strikers Charged*) |
| **HSD** | `.dat` | **GLB** | ✅ | ✅ | ✅ | ✅ | HAL Laboratory `sysdolphin` object graph (GameCube), exported to GLB. |
| **HSF** | `.hsf` | **GLB** | ✅ | ✅ | ✅ | ✅ | Hudson Soft 3D model format (GameCube / Wii) |
| **J3D BDL** | `.bdl` | **GLB** | ✅ | ✅ | — | — | Nintendo GameCube / Wii binary display-list model (`J3D2bdl4`); same coverage as BMD. |
| **J3D BMD** | `.bmd` | **GLB** | ✅ | ✅ | — | — | Nintendo GameCube / Wii binary model (`J3D2bmd3`, `bmd2`): geometry, skinning, materials, GX textures. |
| **LMD** | `.lmd` | — | — | — | — | — | Unidentified. |
| **MBN** | `.mbn` | **GLB** | ✅ | — | — | — | ModelBinary companion buffers applied onto the sibling `.bch` scene. |
| **MDL0 / BRRES** | `.mdl0`, `.brres` | **GLB** | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R binary resource model (Wii). |
| **MOD** | `.mod` | **GLB** | ✅ | ✅ | ✅ | ✅ | Monster Games NDL3/NDL2 display list model (Wii) |
| **MPR CMDL / SMDL / WMDL** | `.cmdl`, `.smdl`, `.wmdl` | **GLB** | ✅ | — | — | ✅ | Retro Studios static, skinned & world models (*Metroid Prime Remastered*, Switch; *DKCTF*). |
| **MPR SKEL** | `.skel` | **GLB** | ✅ | — | — | ✅ | Retro Studios skeletal hierarchy (*Metroid Prime Remastered*, Switch; *DKCTF*). |
| **MSH (PMsh)** | `.msh` | **GLB** | ✅ | ✅ | ✅ | ✅ | Monster Games collision mesh format (Wii) |
| **MTMOD** | `.mod` | **GLB** | ✅ | — | — | — | Capcom MT Framework Mobile model (3DS *Resident Evil / Monster Hunter* era). |
| **NSBMD** | `.nsbmd`, `.bmd` | **GLB** | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro 3D model format (DS). |
| **NUANMB** | `.nuanmb` | *(text dump)* | ✅ | — | — | 🟡 | Bandai Namco SSBH animation (*Super Smash Bros. Ultimate*); keyframe data not unpacked. |
| **NUD** | `.nud` | **GLB** | ✅ | ✅ | ✅ | ✅ | Bandai Namco 3D model (*Super Smash Bros. 4*, *Pokkén Tournament*). |
| **NUFXLB** | `.nufxlb` | *(text dump)* | ✅ | — | — | 🟡 | Bandai Namco SSBH shader-effects library: shader programs, render passes and vertex attributes. |
| **NUHLPB** | `.nuhlpb` | *(text dump)* | ✅ | — | — | 🟡 | Bandai Namco SSBH helper-bone aim/orient constraints. |
| **NULSTB** | `.nulstb` | *(text dump)* | ✅ | — | — | 🟡 | Bandai Namco SSBH file-name list. |
| **NUMATB** | `.numatb` | *(text dump)* | ✅ | — | — | 🟡 | Bandai Namco SSBH material container: materials, shader labels and named parameters. |
| **NUMDLB** | `.numdlb` | *(text dump)* | ✅ | — | — | 🟡 | Bandai Namco SSBH model descriptor (*Super Smash Bros. Ultimate*). |
| **NUMSHB** | `.numshb` | **GLB** | ✅ | — | — | ✅ | Bandai Namco SSBH 3D mesh model (*Super Smash Bros. Ultimate*, Switch) |
| **NURPDB** | `.nurpdb` | *(text dump)* | ✅ | — | — | 🟡 | Bandai Namco SSBH render-pass data (framebuffers, state objects, passes; partially understood upstream). |
| **NUSHDB** | `.nushdb` | *(text dump)* | ✅ | — | — | ✅ | Bandai Namco SSBH compiled-shader container (entries listed; NVN GPU binaries are not disassembled). |
| **NUSKTB** | `.nusktb` | *(text dump)* | ✅ | — | — | ✅ | Bandai Namco SSBH skeleton (*Super Smash Bros. Ultimate*). |
| **PERS** | `.pers` | *(raw payload)* | ✅ | — | — | ✅ | Pokémon Stadium (N64) PERS-SZP container |
| **SANIM** | `.sanim` | *(text dump)* | ✅ | — | — | — | Mario Strikers skeleton animation stream. |
| **TTMODEL** | `.model` | **GLB** | ✅ | — | — | — | TT Games NTT engine model (*LEGO Star Wars: The Skywalker Saga*). |
| **WMB** | `.wmb` | **GLB** | ✅ | — | — | ✅ | PlatinumGames model (*Star Fox Zero*, Wii U) |

`Byte-Exact Roundtrip` = decode → GLB → re-encode reproduces the original file's bytes identically (canonical fixed-point verified), not just a successful encode.

---

### Textures & 2D Graphics

| Format | Extensions | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- |
| **AJPG / ODH** | `.ajpg` | ✅ | ✅ | — | — | ActImagine baseline-JPEG-derived still image format (GBA / Wii Message Board) |
| **ART / IMG** | `.art`, `.img` | ✅ | ✅ | ✅ | ✅ | Monster Games GUI image format (Wii) |
| **ASTC** | `.astc` | ✅ | ✅ | — | — | Adaptive Scalable Texture Compression (ARM ASTC) files. |
| **BCFNT / BFFNT / BRFNT** | `.bcfnt`, `.bffnt`, `.brfnt` | ✅ | ✅ | ✅ | ✅ | NintendoWare font resource (3DS / Wii U / Wii). |
| **BCLIM** | `.bclim` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C texture container (3DS) |
| **BFLIM** | `.bflim` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4F texture format (Wii U) |
| **BNR** | `.bnr` | ✅ | — | — | — | Nintendo GameCube & Wii game opening banner icon (RGB5A3) |
| **BNSTX** | `.bnstx` | ✅ | — | — | — | Nintendo Switch texture package. |
| **BNTX** | `.bntx` | ✅ | ✅ | ✅ | ✅ | NintendoSDK Tegra block-linear texture container (Switch); DDS / ASTC export and DDS encode. |
| **BREFT** | `.breft`, `.bt-img` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R particle effect texture (Wii) |
| **BTGA / LGA** | `.btga`, `.lga` | ✅ | ✅ | — | — | Nintendo 3DS PICA texture wrapper (Lego titles) |
| **BTI / TPL** | `.bti`, `.tpl` | ✅ | ✅ | ✅ | ✅ | Nintendo standard texture palette library (GameCube / Wii) |
| **Camelot GX bank** | *(none)*, `.stpl`, `.sbn` | ✅ | — | — | ✅ | Camelot GX texture bank (*Mario Golf: Toadstool Tour*, *Mario Power Tennis*, *We Love Golf!*). |
| **CAN** | `.can` | ✅ | — | — | ✅ | Monster Games skeletal animation (*Excite Truck* / *ExciteBots*, Wii) |
| **CMAB** | `.cmab` | ✅ | — | — | — | Grezzo Nintendo 3DS material animation (*Ocarina of Time 3D*, *Majora's Mask 3D*). |
| **CMB** | `.cmb` | ✅ | ✅ | — | — | Grezzo Nintendo 3DS model container (*Ocarina of Time 3D*, *Majora's Mask 3D*, *Ever Oasis*). |
| **CTPK** | `.ctpk` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4C texture package (3DS) |
| **CTXB** | `.ctxb` | ✅ | ✅ | ✅ | ✅ | Grezzo 3DS texture container (*Ocarina of Time 3D*, *Majora's Mask 3D*). |
| **DDS** | `.dds` | ✅ | ✅ | — | — | Microsoft DirectDraw Surface texture. |
| **DMPBM** | `.dmpbm` | ✅ | ✅ | — | — | Atlus Nintendo 3DS tiled bitmap (*Shin Megami Tensei: Devil Survivor Overclocked*). |
| **DSB / TXTR** | `.dsb` | ✅ | ✅ | ✅ | — | Animal Crossing: Wild World DS menu texture: 32-entry RGB555 palette + A3I5 texels. |
| **FEDTEX** | `.fedtex` | ✅ | — | — | — | Next Level Games texture container (Federation Force / LM2 / LM3), decoded to PNG. |
| **G1T** | `.g1t` | ✅ | — | — | ✅ | Koei Tecmo texture container (*Hyrule Warriors Legends*, 3DS; *Fire Emblem Warriors*) |
| **GFTEX** | `.gftex` | ✅ | — | — | — | Game Freak 3DS texture (SPICA `GFTexture`; *Pokémon X/Y/ORAS*): PICA200 payloads incl. ETC1/ETC1A4. |
| **GTX** | `.gtx` | ✅ | ✅ | ✅ | ✅ | Nintendo Wii U GX2 surface container (Wii U) |
| **GVR** | `.gvr` | ✅ | — | — | ✅ | Sega GameCube & Wii texture container (GCIX / GVRT). |
| **MPR TXTR** | `.txtr` / `.mpr.txtr` | ✅ | ✅ | ✅ | ✅ | Retro Studios texture, Remastered revision (*Metroid Prime Remastered*, Switch) |
| **MTTEX** | `.tex` | ✅ | — | — | — | Capcom MT Framework Mobile texture (3DS): PICA200 payloads including ETC1 / ETC1A4. |
| **NCER / NANR** | `.ncer`, `.nanr` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro cell & animation resources (DS) |
| **NCGR / NCLR** | `.ncgr`, `.nclr` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro 2D graphics & palette (DS) |
| **NDS banner** | `banner.bin` | ✅ | — | — | ✅ | Nintendo DS ROM banner (DS / DSi) |
| **NSBCA / NSBTA / NSBTP / NSBVA / NSBMA** | `.nsbca`, `.nsbta`, `.nsbtp`, `.nsbva`, `.nsbma` | ✅ | — | — | ✅ | Nintendo DS Nitro animation family (joint, texture SRT, texture pattern, visibility, material colour). |
| **NSBTX** | `.nsbtx` | ✅ | ✅ | ✅ | ✅ | Nintendo DS Nitro 3D texture container (DS) |
| **NSCR** | `.nscr` | ✅ | — | — | ✅ | Nintendo DS Nitro screen/tilemap resource, rendered against its NCGR tiles and NCLR palette (DS) |
| **NTTF** | `.nttf`, `.bnttf` | ✅ | — | — | — | Nintendo DS / DSi manual texture. |
| **NUT** | `.nut` | ✅ | ✅ | ✅ | ✅ | Bandai Namco texture package (*Super Smash Bros. 4*, Wii U / 3DS) |
| **NUTEXB** | `.nutexb` | ✅ | ✅ | ✅ | — | Bandai Namco / Nintendo Switch texture wrapper (Switch) |
| **PLT0** | `.plt0` | — | — | — | — | Nintendo DS / Wii palette file (identification only). |
| **PTLG** | `.glt`, `.rlt` | ✅ | ✅ | ✅ | ✅ | Next Level Games texture container, extracted as TPL (*Super Mario Strikers*, *Mario Strikers Charged*). |
| **Retro TXTR** | `.txtr` | ✅ | ✅ | ✅ | ✅ | Retro Studios texture, old revision (*Metroid Prime 1-3*, *Donkey Kong Country Returns*, Wii) |
| **SMDH** | `.smdh` | ✅ | ✅ | — | ✅ | Nintendo 3DS application icon, publisher info & title metadata. |
| **STEX** | `.stex` | ✅ | ✅ | — | — | Atlus Nintendo 3DS PICA texture (*Etrian Odyssey IV*, *Shin Megami Tensei IV*). |
| **TEX** | `.tex` | ✅ | ✅ | ✅ | ✅ | Monster Games GX texture format (Wii) |
| **TEX0** | `.tex0` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R texture resource (Wii) |
| **TEX3DS** | `.tex` | — | — | — | — | Nintendo 3DS proprietary texture (identification only) |
| **TM0** | `.tm0` | ✅ | — | — | ✅ | Monster Games high-resolution texture (*Excite Truck*, Wii) |
| **Tropical TXTR** | `.txtr` | ✅ | — | — | — | Retro Studios texture, new revision (*Donkey Kong Country: Tropical Freeze*, Wii U) |
| **WIBN** | `banner.bin`, `.bnr` | ✅ | — | — | — | Wii save game banner (Wii) |
| **Wii banner** | `opening.bnr`, `IMET`, `IMD5` | ✅ | — | — | ✅ | Wii channel/disc banner (Wii) |
| **XIMG** | `.xi` | — | — | — | — | Level-5 3DS/Switch image & texture container |
| **XTX** | `.xtx` | ✅ | — | — | — | Nintendo Switch intermediate texture container (Tegra block-linear RGBA8 / BCn / ASTC). |

`Byte-Exact Roundtrip` = encode → decode → re-encode to the same destination name
reproduces the file's bytes. Exercised by `t_byte_fixed_points()` in `tests/regress.sh`
| **TVOL** | `.tvol` | ✅ | — | — | — | Koei Tecmo / Gust texture volume archive. |
| **TXE** | `.txe` | ✅ | — | — | — | *Pikmin 1* texture (decodes to PNG). |
| **TXTG** | `.txtg` | ✅ | — | — | — | Next Level Games Texture To Go (`6PK0`). |
| **WTB** | `.wta`, `.wtb` | ✅ | — | — | — | Nintendo Switch texture archive (texture headers + image data). |

---

### Audio, Sound & Music

| Format | Extensions | Decode Tested | Encode Tested | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- |
| **BARS** | `.bars` | ✅ | — | — | Nintendo Binary Audio Resource Archive (Wii U / Switch) |
| **BCSAR / BCWAR / BCWAV** | `.bcsar`, `.bcwar`, `.bcwav` | ✅ | ✅ | ✅ | NintendoWare NW4C sound archive & wave format (3DS). |
| **BFSAR / BFWAR / BFWAV** | `.bfsar`, `.bfwar`, `.bfwav` | ✅ | ✅ | ✅ | NintendoWare NW4F & NintendoSDK sound archive & wave format (Wii U / Switch). |
| **BRSAR / RBNK / RWAV** | `.brsar`, `.rbnk`, `.rwav` | ✅ | ✅ | ✅ | NintendoWare NW4R sound archive, instrument bank & wave format (Wii) |
| **BRSTM / BCSTM / BFSTM** | `.brstm`, `.bcstm`, `.bfstm` | ✅ | ✅ | ✅ | Nintendo multi-channel stream audio (Wii / 3DS / Wii U / Switch) |
| **NUS3AUDIO** | `.nus3audio`, `.nus3bank` | ✅ | ✅ | — | Bandai Namco NUS3 audio archive (*Super Smash Bros. Ultimate*, Switch) |
| **RSEQ / CSEQ / FSEQ / SSEQ** | `.rseq`, `.cseq`, `.fseq`, `.sseq` | ✅ | ✅ | ✅ | Nintendo sequence music format (Wii / 3DS / Wii U / DS). |
| **SADL** | `.sad`, `.sadl` | ✅ | — | ✅ | Level-5 / *Professor Layton* audio stream container (DS) |
| **SDAT** | `.sdat` | ✅ | ✅ | ✅ | Nintendo DS Nitro sound archive (DS) |

---

### Layouts, Text & Game Data

| Format | Extensions | Decode Tested | Encode Tested | Byte-Exact Roundtrip | Retail Source Tested | Middleware / Engine / Platform Context |
| --- | --- | --- | --- | --- | --- | --- |
| **AAMP** | `.aamp` | ✅ | ✅ | ✅ | — | Nintendo binary parameter archive (Wii U / Switch); YAML/JSON text conversion. |
| **BCLYT / BCLAN** | `.bclyt`, `.bclan` | ✅ | ✅ | ✅ | — | NintendoWare NW4C 2D layout & animation (3DS) |
| **BFLYT / BFLAN** | `.bflyt`, `.bflan` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4F 2D layout & animation (Wii U) |
| **BGLPBD** | `.bglpbd` | ✅ | ✅ | ✅ | — | AGL light-probe data (Wii U / Switch). |
| **BMG** | `.bmg` | ✅ | ✅ | ✅ | ✅ | Nintendo standard binary message format (GameCube / Wii) |
| **BRLYT / BRLAN** | `.brlyt`, `.brlan` | ✅ | ✅ | ✅ | ✅ | NintendoWare NW4R 2D layout & animation (Wii; *Super Mario 3D All-Stars* TYLR/NALR reversed definitions) |
| **BYAML / BYML** | `.byaml`, `.byml`, `.sbyml`, `.smubin` + other BotW `S`-prefixed Yaz0 containers | ✅ | ✅ | ✅ | ✅ | Nintendo binary YAML (Wii / Wii U / Switch); Yaz0-wrapped inputs decode transparently. |
| **KPMAP** | `.kpmap` | ✅ | ✅ | — | — | Koopatlas map project (JSON; *New Super Mario Bros. Wii* level-editor community format). |
| **MIO** | `.mio` | ✅ | — | — | ✅ | *WarioWare: D.I.Y.* / *Made in Ore* Game, Comic & Record data (DS / Wii) |
| **MPBOARD** | `.bin`, `.csv`, `.xml` | ✅ | ✅ | ✅ | — | Mario Party board data (GameCube / Wii binary, *Super Mario Party* CSV, *Mario Party 10* XML). |
| **MPMESS** | `.dat` | ✅ | ✅ | ✅ | — | Mario Party 4-7 GameCube message files, extracted to text / JSON. |
| **MSBT / MSBP / MSBF** | `.msbt`, `.msbp`, `.msbf` | ✅ | ✅ | ✅ | ✅ | Nintendo Message Studio binary text, project & flow (3DS / Wii U / Switch) |
| **MTMFX** | `.mfx`, `.lfx` | ✅ | — | — | — | Capcom MT Framework Mobile shader effects (3DS): input-layout tables. |
| **MTMRL** | `.mrl` | ✅ | — | — | — | Capcom MT Framework Mobile materials (SPICA `MTMaterials`; 3DS): CRC32-keyed texture bindings as text. |
| **XB** | `.xml` (binary) | ✅ | ✅ | ✅ | — | Nd Cube Binary XML (*Mario Party 10*), decoded to XML text. |
| **XMB** | `.xmb` | ✅ | — | — | — | Smash XMB material / LOD metadata (Smash 4 / Ultimate). |

`Byte-Exact Roundtrip` = encode → semantic text → re-encode reproduces the file's bytes. Exercised by `t_byte_fixed_points()` in `tests/regress.sh`. BRLYT/BRLAN canonical fixed point and semantic roundtrips are validated against retail Wii layouts.

---

### Compression & Encoding Formats

| Algorithm / Codec | Identifiers / Headers | Decode Tested | Encode Tested | Platform / Engine Context |
| --- | --- | --- | --- | --- |
| **ALZ1** | `ALZ1` | ✅ | ✅ | Hudson Soft Mario Party / Bomberman LZ77 (GameCube / Wii) |
| **ASH0** | `ASH0` | ✅ | ✅ | Nintendo Huffman+LZSS stream (Wii System Menu, Animal Crossing, My Pokémon Ranch) |
| **BLZ** | ARM9 overlay trailer | ✅ | ✅ | Nintendo DS Nitro backward LZ overlay compression |
| **BPE / GFCP** | `GFCP` (zip mode 1) | ✅ | ✅ | Good-Feel Byte Pair Encoding (Wii Kirby's Epic Yarn / Yoshi's Woolly World) |
| **Bzip2** | `BZh` | ✅ | ✅ | Standard high-compression block-sorting codec |
| **Camelot LZ** | `0x01` / `0x02` prefix | ✅ | ✅ | Camelot Software Planning LZ77 compression (*Mario Golf*, *Mario Tennis* GameCube / Wii) |
| **CMP** | `.cmp` (`0x11` prefix) | ✅ | ✅ | HAL Laboratory LZ11-compressed file wrapper |
| **Deflate / Zlib** | `78 01`, `78 9C`, `78 DA` | ✅ | ✅ | Standard RFC 1950 / 1951 stream compression |
| **Diff8 / Diff16** | `0x81`, `0x82` | ✅ | ✅ | Nintendo DS differential delta filter encoding |
| **FZIP** | `FZIP` | ✅ | ✅ | *Game & Wario* Zlib stream container (Wii U). |
| **Huffman (4-bit / 8-bit)** | `0x24`, `0x28` | ✅ | ✅ | Nintendo DS Huffman stream compression |
| **LZ10** | `0x10` (LZSS) | ✅ | ✅ | Nintendo standard LZ77 (GameCube / Wii / DS / GBA) |
| **LZ11** | `0x11` (Extended LZSS) | ✅ | ✅ | Nintendo extended LZSS with 4-byte match lengths (DS / 3DS) |
| **LZ4** | `04 22 4D 18` | ✅ | ✅ | Standard LZ4 frame compression |
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
| **7-Zip / RAR / Tar / Gzip Archives** | `.7z`, `.rar`, `.cb7`, `.tar`, `.tgz`, `.tbz2`, `.txz`, `.gz` | **`7z`** / **`7zz`** / **`7za`** / **`unar`** (`--with-7z`) | General archive unpacking |
| **Custom Binary Containers** | Arbitrary formats | **`QuickBMS`** (`--bms=<script.bms>`) | Direct execution of QuickBMS extraction scripts |
| **DSP-ADPCM Audio Streams** | `.brstm`, `.bcstm`, `.bfstm`, `.bns`, `.btsnd`, `.ast`, `.dsp` | **`mobipeg`** | Bit-exact Nintendo THP ADPCM coefficient search & stream encoding |
| **Mobiclip Video & Cutscenes** | `.mo`, `.mods`, `.moflex`, `.MOC`, `.MOD` | **`mobipeg`** (`--with-mobipeg`) / **`ffmpeg`** | Nintendo DS / 3DS / Wii Mobiclip video decoding to MP4 |
| **Nintendo 3DS Containers** | `.3ds`, `.cci`, `.cxi`, `.cfa`, `.cia`, `.app` | **`ctrtool`** (bundled) / **`makerom`** (`--with-ctrtool`) | NCCH/NCSD partition extraction, ExeFS/RomFS unpacking & CIA installation packages |
| **Nintendo DS / DSi ROMs** | `.nds`, `.srl`, `.dsi` | **`ndstool`** (`--with-ndstool`) | Nitro ROM header, banner, arm9/arm7 binary & NitroFS extraction/rebuild |
| **Nintendo Switch Packages** | `.nsp`, `.xci`, `.nca`, `.nsz`, `.xcz` | **`hactool`** / **`hacbrewpack`** / **`nsz`** (`--with-hactool`, `--with-hacbrewpack`, `--with-nsz`) | PFS0 / HFS0 / NCA content extraction, NSZ/XCZ decompression & homebrew NSP repacking |
| **SFX** | `.sfx` | **`mobipeg`** / **`ffmpeg`** | Monster Games DSP-ADPCM audio (*Excite Truck*, *ExciteBots*, Wii). |
| **Sound Archives** | `.brsar`, `.sdat`, `.bfsar`, `.bcsar` | **`wbrsar`** / **`vgmtrans`** (bundled; `--with-vgmtrans`) | Nintendo sound archive translation to MIDI + SoundFont, asset pack/unpack |
| **THP & Media Video** | `.thp`, `.h4m`, `.vid`, `.dpg`, `.fv`, `.ppm`, `.kwz`, `.mmstr`, `.rvid`, `.vx`, `.bik` | **`mobipeg`** / **`ffmpeg`** | GameCube/Wii THP, HVQM4, DPG, Bink, FastVideo & Flipnote animation decoding. |
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
