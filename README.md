# nintoolbox

A fast, unified command-line toolkit to extract, modify, convert, and rebuild game archives, textures, 3D models, audio, and layouts across **GameCube, Wii, Nintendo DS, 3DS, Wii U, and Nintendo Switch**.

---

## Quick Start

### Installation & Building

```bash
git clone https://github.com/quatric/nintoolbox.git
cd nintoolbox/project
make all -j8
```

Compiled binaries (`wszst`, `wimgt`, `wmdlt`, `wbrsar`, `wbmgt`, `wlayt`, `wctct`, `wkclt`, `wkmpt`) will be placed in `project/bin/`.

---

## Common Commands

`wszst xx` unpacks a whole game recursively; `wszst CREATE` packs it back (experimental).

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
#    (-> Sound.d/raw: BrawlCrate-style .brseq/.brbnk/.brwar, Sound.d/midi: one .mid
#     per sound, Sound.d/soundfonts: one .sf2 per bank)
wbrsar Sound.brsar --dest Sound.d
wbrsar Sound.sdat --dest Sound.d
wbrsar pack Sound.d/raw Sound-new.brsar       # rebuild from the raw assets
```

---

## Supported Formats by Category

### Archives & Containers

| Format | Extensions | Games |
| --- | --- | --- |
| **ABE BigFile** | `.bf` | *Rabbids Go Home* |
| **AGI** | `.pak` | *Skylanders: Swap Force* |
| **ALAR** | `.aar`, `.alar` | *Jump Super Stars*, *Jump Ultimate Stars* |
| **And-Kensaku** | `.rz` | *And-Kensaku* |
| **APAK** | `.apak` | Wii U / Switch |
| **ARC / U8** | `.arc`, `.szs` | Wii / GameCube NintendoWare & EAD |
| **ARC0** | `.fa` | *Yo-Kai Watch* |
| **ARCV** | `.arc` | Namco / Tose Wii titles |
| **Arika Archive** | `INFO.DAT`, `GAME.DAT`, `.arika` | Arika DS / DSi / Wii titles |
| **Asobo BigFile** | `.drv` | *Ratatouille* |
| **AST** | `.ast` | *I Spy Spooky Mansion* |
| **AT7** | `.at7` | Wii / PS2 |
| **ATB** | `.atb` | *Mario Party 4-8* |
| **Atomic Planet PUB** | `.pub` | *AMF Bowling: Pinbusters!* |
| **Avalanche THB/TBB** | `.thb` + `.tbb` | *Cars 2* |
| **Battle of the Bands BAG** | `.bag` | *Battle of the Bands* |
| **Havok HKX (classic packfile)** | `.hkx`, `.HKX` | *Tenchu: Shadow Assassins* |
| **T4-RES (Tenchu: Shadow Assassins)** | `.b` | *Tenchu: Shadow Assassins*, *"` tagged resource family (`Common/Camera/* |
| **HDVOICE (Tenchu: Shadow Assassins)** | `.hd` | *Tenchu: Shadow Assassins* |
| **BCGRP** | `.bcgrp` | 3DS NintendoWare titles |
| **BEA** | `.bea`, `.nx.bea` | *WarioWare*, *Mario Party* |
| **BFGRP** | `.bfgrp` | Wii U / Switch NintendoWare titles |
| **BFMA** | `.bfma` | Wii U titles with a built-in manual |
| **BFSHA / BNSH** | `.bfsha`, `.bnsh` | Wii U / Switch |
| **BG4** | `.bg4` | *Mario & Luigi* (3DS) |
| **BIGF** | `.big` | Electronic Arts Wii titles |
| **Bj engine** | `.tx1`/`.tx2`, `.mtm`, `.bsi`, `.bsm` | *Super Karts* |
| **Blue Tongue TRB** | `.trb` | *de Blob 2* |
| **BNS Archive** | `.bns` | *Samurai Warriors 3* |
| **BombShell data pack** | `.xwi`, `.xdx9` | *Bee Movie Game* |
| **CA01 / SA01** | `.ca01`, `.sa01` | 3DS / Wii U |
| **CCF** | `.ccf` | Wii / Switch |
| **CDGaCube CAR** | `.car` | *Birthday Party Bash* |
| **CHDp / C3Dp** | `.chd`, `.c3d` + `.cbd` | *Donkey Konga*, *Donkey Konga 2*, *Donkey Konga 3* |
| **CNUT** | `.cnut` | *Wii Party* |
| **COD PAK0** | `.pak` | *Call of Duty: Black Ops*, *MW3* |
| **CPK** | `.cpk` | *Star Fox Zero* |
| **CRAM** | `.arc`, `.cram` | *Project X Zone* |
| **DARC / BCMA** | `.darc`, `.bcma`, `.arc` | 3DS |
| **DC2 DCX / DCT** | `.dcx`, `.dct` | *Jakers! Kart Racing* |
| **DIG** | `.dig` | *Bomberman Land* |
| **DKZF** | `.tpl.dkz` | *Donkey Konga*, *Donkey Konga 2*, *3* |
| **DTLS** | `dt00`, `ls00`, `.ls` | *Super Smash Bros. 4* |
| **EFFN** | `.eff`, `.effn` | *Super Smash Bros. for 3DS / Wii U*, *Super Smash Bros. Ultimate* |
| **F9RES** | `.res` | GameCube titles |
| **FBTI** | `.Mod`, `.Mot` | *Rune Factory: Frontier* |
| **FMOD FSB** | `.fsb` | FSB3 / FSB4 / FSB5, Wii |
| **FSYS** | `.fsys` | GameCube / Wii |
| **GAR / ZAR** | `.zar`, `.gar` | *OoT3D*, *MM3D*, *LM3DS* |
| **GFA** | `.gfa` | Wii / 3DS / Wii U |
| **GFLX** | `.gflxpack` | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFMPACK** | `.gfpack` | *Pokémon X/Y/ORAS* |
| **GFPAK** | `.gfpak` | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFPKG** | `.gfpkg`, `.bin`, `.pak` | *Pokémon X / Y*, *Omega Ruby / Alpha Sapphire*, *Sun / Moon* |
| **Goliath GS Package** | `.pkz` | *Skylanders: SuperChargers Racing*, *The Amazing Spider-Man* |
| **Grip RES** | `.res` | *Sesame Street: Elmo's Musical Monsterpiece* |
| **h.a.n.d. FBC** | `.fbc` | *Oyako de Asobo: Miffy no Omochabako* |
| **HBDF** | `.hbdf`, `.hsdf` | *Mario Party DS* |
| **Heavy Iron HO** | `.ho` | *WALL-E*, *Up* |
| **Humongous Resource.rez** | `.rez` | *Backyard Football '10* |
| **Hyrule Warriors** | `.idx`, `.bin` | 3DS |
| **IPK** | `.ipk` | *Just Dance*, *Rayman Origins/Legends* |
| **IQIPACK** | `.pak` | NVIDIA Shield iQiyi titles |
| **JARC** | `.jarc` | DS |
| **KPBIN** | `.kpbin` | *New Super Mario Bros. Wii* |
| **kRAW music** | `.kRAW` | *Geometry Wars: Galaxies* |
| **LSPK** | `.pk`, `.pkh`, `.lspk` | *The Last Story* |
| **LZBIN** | `.bin`, `.lzbin` | *Mario Party DS* |
| **MDR** | `.mdr` | *Dance Dance Revolution Mario Mix* |
| **MKGPDX PAC** | `.pac`, `.mkgpdx` | *Mario Kart Arcade GP DX* |
| **MPBIN** | `.bin` | GameCube / Wii |
| **MPR PACK** | `.pak` | *Metroid Prime Remastered* |
| **MSR** | `.pkg`, `.bin` | *Metroid: Samus Returns* |
| **MTXT** | `.mtxt` | Switch titles |
| **NARC** | `.narc` | DS / DSi |
| **NCCARC** | `.nccarc` | DS titles |
| **NDS / SRL / DSI** | `.nds`, `.srl`, `.dsi` | All DS / DSi games |
| **Radical RCF** | `.rcf` | *Crash of the Titans*, *Simpsons Hit & Run* |
| **NIBM audio** | `.aud` | *CSI: Hard Evidence*, *Destroy All Humans* |
| **Harmonix Ark** | `.hdr` + `_N.ark` | *AC/DC Live: Rock Band Track Pack*, *The Beatles: Rock Band* |
| **XPK** | `.pak` | *Angry Birds Star Wars* |
| **Artefacts MAP** | `.map`, `.gam` | *Diabolik: The Original Sin*, *Boot Camp Academy*, *Jillian Michaels Fitness Ultimatum*, *Dodge Racing*, *.gam` when that is present (all of* |
| **Blue Castle BIG** | `.big`, `.dspi` | *The Bigs*, *The Bigs 2* |
| **Natsume BIN** | `.bin` | *Harvest Moon* |
| **Town Factory PCKG** | `.pac`, `.pcha`, `.pac0`-`9`, `.bin`, `.dat` | *Little King's Story* |
| **Neversoft GH PAK/IMG** | `.pak.ngc`, `.img.ngc` | *Guitar Hero* |
| **WSI** | `.wsi` | *Alone in the Dark*, *Aladdin Magic Racer* |
| **DSi TAD** | `.tad` | the DSi's WAD, TwlSDK |
| **DSiWare export** | `.bin` (SD card) | DSiWare titles |
| **DSi modcrypt SRL** | `.nds`, `.srl`, `.dsi` | DSi-enhanced and DSiWare titles |
| **NIF (Gamebryo 20.6, Wii)** | `.nif` | *Pocoyo Racing* |
| **NLG DICT** | `.dict`, `.data` | *Federation Force*, *Luigi's Mansion 2/3*, *Strikers* |
| **NXARC** | `.nxarc` | Switch titles |
| **PAC (Nd Cube)** | `.bin` | *Mario Party 10*, *Animal Crossing: amiibo Festival* |
| **PAC / MRG** | `.pac`, `.mrg` | HAL Laboratory / Game Arts Wii titles |
| **PKG (Barking Lizards)** | `.pkg` | *Nickelodeon: The Naked Brothers Band - The Video Game* |
| **PKG / GPKG / GPAK** | `.pkg`, `.pak`, `.gpak` | *Bonsai Barber*, *World of Goo* |
| **PKI** | `.PKI` | *Bermuda Triangle: Saving the Coral* |
| **PKZ** | `.pkz` | *Bayonetta*, *Astral Chain* |
| **PRC** | `.prc`, `.param` | *Super Smash Bros. for 3DS / Wii U*, *Super Smash Bros. Ultimate* |
| **PTD** | `.ptd`, `.pdt` | *Mario Party 4-8* |
| **PCK2 / PLZ** | `.plz`, `.pck2` | *Professor Layton and the Diabolical Box* |
| **Pikmin ARC/DIR** | `.arc` + `.dir` | *Pikmin* |
| **PVOL** | `.pvol`, `.vol` | *Godzilla: Unleashed*, *Godzilla: Save the Earth* |
| **RARC** | `.rarc`, `.arc` | GameCube / Wii |
| **RFL_Res** | `RFL_Res.dat`, `.dat` | Wii / 3DS / Wii U |
| **RKET** | `.rck`, `.spa` | *Aqua Panic!* |
| **RPAK** | `.rpak`, `.pak` | *Donkey Kong Country Returns* |
| **RSO** | `.rso` | *Skylanders: SuperChargers Racing* |
| **RST / TOC** | `.rst`, `.toc` | *Excite Truck*, *Excitebots* |
| **RWS / MTD** | `.RWS`, `.mtd` | *DreamWorks How to Train Your Dragon* |
| **RZPK** | `.rzpk` | *Mario Party* (3DS) |
| **SARC** | `.sarc`, `.szs` | Wii U / Switch / 3DS |
| **SEC** | `.sec` | *Octomania* |
| **SFZDAT** | `.dat` | *Star Fox Zero* |
| **SHARC / SHARCFB** | `.sharc`, `.sharcfb` | Wii U / Switch |
| **SIR0** | `.sir0` | DS / 3DS |
| **SMASH-ARC** | `.arc` | *Super Smash Bros. Ultimate* |
| **Storybook ONE** | `.one` | *Sonic and the Secret Rings*, *Black Knight* |
| **STPK** | `.srd`, `.stpk` | None verified (*Jump Stars* use ALAR) |
| **Sumo STZ** | `.stz` | *Sonic & Sega All-Stars Racing* |
| **Sumo WAS** | `.was` | *Sonic & Sega All-Stars Racing*, */* |
| **Terminal Reality POD** | `.pod` | *Nickelodeon Dance* |
| **TMPK** | `.pack`, `.tmpk` | *The Legend of Zelda: Twilight Princess HD* |
| **Torus Games hunkfile** | `.hnk` | *Barbie & Her Sisters: Puppy Rescue* |
| **Toshi TSFB** | `.ttl`, `.tkl` (`.trb`) | *Nickelodeon Barnyard* |
| **TRPAK** | `.trpak` | *Pokémon Scarlet / Violet* |
| **UE4 PAK** | `.pak` | *Mario & Luigi: Brothership* |
| **Vblank BFP / BAP** | `.bfp`, `.bap` | *Retro City Rampage DX*, *Shakedown: Hawaii* |
| **VCRA** | `.bin`, `.vcra` | Wii |
| **VFF** | `.vff` | Wii channels and save data |
| **VFXB / PTCL** | `.ptcl`, `.eset`, `.vfxb` | Wii U / Switch |
| **VIBS** | `.vibs` | Switch first-party titles |
| **WADH** | `.wad` (DataWII.wad) | *Ninjabread Man* |
| **WARC** | `.warc` | Wii U |
| **WTA / WTP** | `.wta` + `.wtp` | *Star Fox Zero* |
| **WUD / WUX** | `.wud`, `.wux` | All Wii U games |
| **XMSG** | `.bin` | *Wii Party* |
| **XPCK** | `.xc`, `.xpck` | *Inazuma Eleven*, *Professor Layton*, *Yo-kai Watch* |
| **ZDAT** | `.zdat` | *Animal Crossing: Pocket Camp* |
| **ZLARC** | `.zlarc` | *NES Remix*, *NES Remix 2*, *NES Remix Pack* |
| **ZTAB** | `.ztab`, `.tab` | *Mario Golf: Toadstool Tour*, *Mario Power Tennis* |

`Byte-Exact Roundtrip` = build → extract → rebuild reproduces the archive's bytes
identically, so the writer's canonical layout is a fixed point of its own reader.
Exercised by `t_container_roundtrip()` in `tests/regress.sh`.

---

### 3D Models & Geometry

| Format | Extensions | Output | Games |
| --- | --- | --- | --- |
| **ADJB** | `.adjb` | **TXT manifest** | *Super Smash Bros. Ultimate* |
| **BCH** | `.bch` | **GLB** | 3DS |
| **BCMDL / CGFX** | `.bcmdl`, `.cgfx` | **GLB** | 3DS |
| **BCRES** | `.bcres` | **GLB** | 3DS |
| **BFRES** | `.bfres` | **GLB** | Wii U / Switch |
| **BMD** | `.bmd`, `.bdhc` | **GLB** | DS |
| **BNFM** | `.bnfm` | **GLB** | *Mario Party 10*, *Animal Crossing: amiibo Festival* |
| **CSB** | `.csb` | **GLB** | *Paper Mario: The Thousand-Year Door*, *The Origami King*, *Color Splash* |
| **CTB** | `.ctb` | *(text dump)* | *Paper Mario: The Thousand-Year Door*, *The Origami King*, *Color Splash* |
| **FEDMODEL** | `.fedmodel` | **GLB** | *Metroid Prime: Federation Force*, *Luigi's Mansion: Dark Moon*, *Luigi's Mansion 3* |
| **FEDSKEL** | `.fedskel` | **GLB** | *Metroid Prime: Federation Force*, *Luigi's Mansion: Dark Moon*, *Luigi's Mansion 3* |
| **G1M** | `.g1m` | **GLB** | *Hyrule Warriors Legends*, *Fire Emblem Warriors* |
| **G4PKM** | `.g4pkm` | — | Unidentified |
| **GF1MOT** | `.gf1mot` | *(text dump)* | *Pokémon X / Y*, *Omega Ruby / Alpha Sapphire* |
| **GFBANM** | `.gfbanm` | *(identification only)* | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFBMDL** | `.gfbmdl` | *(identification only)* | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFMODEL** | `.gfmodel` | **GLB** | *Pokémon X/Y/ORAS* |
| **GFMOT** | `.gfmot` | *(text dump)* | *Pokémon X / Y*, *Sun / Moon* |
| **GLG / RLG** | `.glg`, `.rlg` | **GLB** | *Super Mario Strikers*, *Mario Strikers Charged* |
| **HSD** | `.dat` | **GLB** | GameCube |
| **HSF** | `.hsf` | **GLB** | GameCube / Wii |
| **J3D BDL** | `.bdl` | **GLB** | *The Legend of Zelda: The Wind Waker*, *Twilight Princess*, *Super Mario Sunshine* |
| **J3D BMD** | `.bmd` | **GLB** | *The Legend of Zelda: The Wind Waker*, *Twilight Princess*, *Super Mario Sunshine* |
| **LMD** | `.lmd` | — | Unidentified |
| **MBN** | `.mbn` | **GLB** | 3DS titles with `.bch` scenes |
| **MDL0 / BRRES** | `.mdl0`, `.brres` | **GLB** | Wii |
| **MOD** | `.mod` | **GLB** | Wii |
| **MPR CMDL / SMDL / WMDL** | `.cmdl`, `.smdl`, `.wmdl` | **GLB** | *Metroid Prime Remastered*, *DKCTF* |
| **MPR SKEL** | `.skel` | **GLB** | *Metroid Prime Remastered*, *DKCTF* |
| **MSH (PMsh)** | `.msh` | **GLB** | Wii |
| **MTMOD** | `.mod` | **GLB** | *Resident Evil / Monster Hunter* |
| **NSBMD** | `.nsbmd`, `.bmd` | **GLB** | DS |
| **NUANMB** | `.nuanmb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **NUD** | `.nud` | **GLB** | *Super Smash Bros. 4*, *Pokkén Tournament* |
| **NUFXLB** | `.nufxlb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **NUHLPB** | `.nuhlpb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **NULSTB** | `.nulstb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **NUMATB** | `.numatb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **NUMDLB** | `.numdlb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **NUMSHB** | `.numshb` | **GLB** | *Super Smash Bros. Ultimate* |
| **NURPDB** | `.nurpdb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **NUSHDB** | `.nushdb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **NUSKTB** | `.nusktb` | *(text dump)* | *Super Smash Bros. Ultimate* |
| **PERS** | `.pers` | *(raw payload)* | N64 |
| **SANIM** | `.sanim` | *(text dump)* | *Super Mario Strikers* |
| **TTMODEL** | `.model` | **GLB** | *LEGO Star Wars: The Skywalker Saga* |
| **WMB** | `.wmb` | **GLB** | *Star Fox Zero* |

`Byte-Exact Roundtrip` = decode → GLB → re-encode reproduces the original file's bytes identically (canonical fixed-point verified), not just a successful encode.

---

### Textures & 2D Graphics

| Format | Extensions | Games |
| --- | --- | --- |
| **AJPG / ODH** | `.ajpg` | GBA / Wii Message Board |
| **ART / IMG** | `.art`, `.img` | Wii |
| **ASTC** | `.astc` | Switch and mobile titles |
| **BCFNT / BFFNT / BRFNT** | `.bcfnt`, `.bffnt`, `.brfnt` | 3DS / Wii U / Wii |
| **BCLIM** | `.bclim` | 3DS |
| **BFLIM** | `.bflim` | Wii U |
| **BNR** | `.bnr` | GameCube / Wii titles |
| **BNSTX** | `.bnstx` | Switch titles |
| **BNTX** | `.bntx` | Switch |
| **BREFT** | `.breft`, `.bt-img` | Wii |
| **BTGA / LGA** | `.btga`, `.lga` | LEGO 3DS titles |
| **BTI / TPL** | `.bti`, `.tpl` | GameCube / Wii |
| **Camelot GX bank** | *(none)*, `.stpl`, `.sbn` | *Mario Golf: Toadstool Tour*, *Mario Power Tennis*, *We Love Golf!* |
| **CAN** | `.can` | *Excite Truck*, *ExciteBots* |
| **CMAB** | `.cmab` | *Ocarina of Time 3D*, *Majora's Mask 3D* |
| **CMB** | `.cmb` | *Ocarina of Time 3D*, *Majora's Mask 3D*, *Ever Oasis* |
| **CTPK** | `.ctpk` | 3DS |
| **CTXB** | `.ctxb` | *Ocarina of Time 3D*, *Majora's Mask 3D* |
| **DDS** | `.dds` | Switch / Wii U / PC-origin titles |
| **DMPBM** | `.dmpbm` | *Shin Megami Tensei: Devil Survivor Overclocked* |
| **DSB / TXTR** | `.dsb` | *Animal Crossing: Wild World* |
| **FEDTEX** | `.fedtex` | *Metroid Prime: Federation Force*, *Luigi's Mansion: Dark Moon*, *Luigi's Mansion 3* |
| **G1T** | `.g1t` | *Hyrule Warriors Legends*, *Fire Emblem Warriors* |
| **GFTEX** | `.gftex` | *Pokémon X/Y/ORAS* |
| **GTX** | `.gtx` | Wii U |
| **GVR** | `.gvr` | Sega GameCube / Wii titles (e.g. *Sonic Riders*) |
| **MPR TXTR** | `.txtr` / `.mpr.txtr` | *Metroid Prime Remastered* |
| **MTTEX** | `.tex` | 3DS |
| **MWT** | `.MWT` | *Bermuda Triangle: Saving the Coral* |
| **Layton backgrounds** | `.arc`, `.arb` | *Professor Layton* |
| **NCER / NANR** | `.ncer`, `.nanr` | DS |
| **NCGR / NCLR** | `.ncgr`, `.nclr` | DS |
| **NDS banner** | `banner.bin` | DS / DSi |
| **NSBCA / NSBTA / NSBTP / NSBVA / NSBMA** | `.nsbca`, `.nsbta`, `.nsbtp`, `.nsbva`, `.nsbma` | DS titles using Nitro 3D |
| **NSBTX** | `.nsbtx` | DS |
| **NSCR** | `.nscr` | DS |
| **NTTF** | `.nttf`, `.bnttf` | DS / DSi titles with a built-in manual |
| **NUT** | `.nut` | *Super Smash Bros. 4* |
| **NUTEXB** | `.nutexb` | Switch |
| **PLT0** | `.plt0` | Wii titles |
| **PST** | `.pst` | *Mercury Meltdown Revolution* |
| **PTLG** | `.glt`, `.rlt` | *Super Mario Strikers*, *Mario Strikers Charged* |
| **Retro TXTR** | `.txtr` | *Metroid Prime 1-3*, *Donkey Kong Country Returns* |
| **SMDH** | `.smdh` | All 3DS applications |
| **STEX** | `.stex` | *Etrian Odyssey IV*, *Shin Megami Tensei IV* |
| **TEX** | `.tex` | Wii |
| **TEX0** | `.tex0` | Wii |
| **TEX3DS** | `.tex` | 3DS titles |
| **TM0** | `.tm0` | *Excite Truck* |
| **Tropical TXTR** | `.txtr` | *Donkey Kong Country: Tropical Freeze* |
| **TVOL** | `.tvol` | Koei Tecmo / Gust titles |
| **TXE** | `.txe` | *Pikmin* |
| **TXTG** | `.txtg` | Next Level Games titles |
| **WIBN** | `banner.bin`, `.bnr` | Wii |
| **Wii banner** | `opening.bnr`, `IMET`, `IMD5` | Wii |
| **WTB** | `.wta`, `.wtb` | PlatinumGames Switch / Wii U titles |
| **XIMG** | `.xi` | Level-5 3DS / Switch titles (e.g. *Yo-kai Watch*) |
| **XTX** | `.xtx` | Tegra block-linear RGBA8 / BCn / ASTC |

`Byte-Exact Roundtrip` = encode → decode → re-encode to the same destination name
reproduces the file's bytes. Exercised by `t_byte_fixed_points()` in `tests/regress.sh`

---

### Audio, Sound & Music

| Format | Extensions | Games |
| --- | --- | --- |
| **BARS** | `.bars` | Wii U / Switch |
| **BCSAR / BCWAR / BCWAV** | `.bcsar`, `.bcwar`, `.bcwav` | 3DS |
| **BFSAR / BFWAR / BFWAV** | `.bfsar`, `.bfwar`, `.bfwav` | Wii U / Switch |
| **BRSAR / RBNK / RWAV** | `.brsar`, `.rbnk`, `.rwav` | Wii |
| **BRSTM / BCSTM / BFSTM** | `.brstm`, `.bcstm`, `.bfstm` | Wii / 3DS / Wii U / Switch |
| **EID** | `.eid` | *I Spy Spooky Mansion* |
| **NUS3AUDIO** | `.nus3audio`, `.nus3bank` | *Super Smash Bros. Ultimate* |
| **RSEQ / CSEQ / FSEQ / SSEQ** | `.rseq`, `.cseq`, `.fseq`, `.sseq` | Wii / 3DS / Wii U / DS |
| **SADL** | `.sad`, `.sadl` | *Professor Layton* |
| **SDAT** | `.sdat` | DS |
| **WT** | `.wt` | *Octomania* |

---

### Layouts, Text & Game Data

| Format | Extensions | Games |
| --- | --- | --- |
| **AAMP** | `.aamp` | Wii U / Switch |
| **BCLYT / BCLAN** | `.bclyt`, `.bclan` | 3DS |
| **BFLYT / BFLAN** | `.bflyt`, `.bflan` | Wii U |
| **BGLPBD** | `.bglpbd` | Wii U / Switch |
| **BMG** | `.bmg` | GameCube / Wii |
| **BRLYT / BRLAN** | `.brlyt`, `.brlan` | *Super Mario 3D All-Stars* |
| **BYAML / BYML** | `.byaml`, `.byml`, `.sbyml`, `.smubin` + other BotW `S`-prefixed Yaz0 containers | Wii / Wii U / Switch |
| **COL / CAM** | `.col`, `.cam` | *Mercury Meltdown Revolution* |
| **GES** | `.ges` | *I Spy Spooky Mansion* |
| **HMT** | `.Hmt` | *Rune Factory: Frontier* |
| **HVB** | `.Hvb` | *Rune Factory: Frontier* |
| **HVC** | `.Hvc` | *Rune Factory: Frontier* |
| **HVG** | `.Hvg` | *Rune Factory: Frontier* |
| **HVH** | `.Hvh` | *Rune Factory: Frontier* |
| **HVM** | `.Hvm` | *Rune Factory: Frontier* |
| **HVT** | `.hvt`, `.Hvt` | *Rune Factory: Frontier* |
| **KPMAP** | `.kpmap` | *New Super Mario Bros. Wii* |
| **KRV** | `.KRV` | *DreamWorks How to Train Your Dragon* |
| **LIT** | `.lit` | *Aqua Panic!* |
| **MAT (Aqua Panic)** | `.mat` | *Aqua Panic!* |
| **MAT (Mercury Meltdown)** | `.mat` | *Mercury Meltdown Revolution* |
| **MB2** | `.mb2` | *Aqua Panic!* |
| **MIO** | `.mio` | *WarioWare: D.I.Y.*, *Made in Ore* |
| **MPBOARD** | `.bin`, `.csv`, `.xml` | *Super Mario Party*, *Mario Party 10* |
| **MPMESS** | `.dat` | *Mario Party 4–7* |
| **MSBT / MSBP / MSBF** | `.msbt`, `.msbp`, `.msbf` | 3DS / Wii U / Switch |
| **MTMFX** | `.mfx`, `.lfx` | 3DS |
| **MTMRL** | `.mrl` | SPICA |
| **MWG / MSP** | `.MWG`, `.MSP` | *Bermuda Triangle: Saving the Coral* |
| **NAV** | `.nav` | *Mercury Meltdown Revolution* |
| **PGF** | `.pgf` | *Bermuda Triangle: Saving the Coral* |
| **SDF** | `.sdf` | *I Spy Spooky Mansion* |
| **VIS** | `.vis` | *Aqua Panic!* |
| **XB** | `.xml` (binary) | *Mario Party 10* |
| **XMB** | `.xmb` | *Super Smash Bros. for 3DS / Wii U*, *Super Smash Bros. Ultimate* |
| **ZEN** | `.zen` | *Mercury Meltdown Revolution* |

`Byte-Exact Roundtrip` = encode → semantic text → re-encode reproduces the file's bytes. Exercised by `t_byte_fixed_points()` in `tests/regress.sh`. BRLYT/BRLAN canonical fixed point and semantic roundtrips are validated against retail Wii layouts.

---

### Compression & Encoding Formats

| Algorithm | Identifier | Used in |
| --- | --- | --- |
| **ALZ1** | `ALZ1` | GameCube / Wii |
| **ASH0** | `ASH0` | Wii System Menu, Animal Crossing, My Pokémon Ranch |
| **BLZ** | ARM9 overlay trailer | DS titles (ARM9 overlays) |
| **BPE / GFCP** | `GFCP` (zip mode 1) | Wii Kirby's Epic Yarn / Yoshi's Woolly World |
| **Bzip2** | `BZh` | Many cross-platform titles |
| **Camelot LZ** | `0x01` / `0x02` prefix | *Mario Golf*, *Mario Tennis* |
| **CMP** | `.cmp` (`0x11` prefix) | HAL Laboratory titles |
| **Deflate / Zlib** | `78 01`, `78 9C`, `78 DA` | Many cross-platform titles |
| **Diff8 / Diff16** | `0x81`, `0x82` | DS titles |
| **FZIP** | `FZIP` | *Game & Wario* |
| **Huffman (4-bit / 8-bit)** | `0x24`, `0x28` | DS / GBA titles |
| **LZ10** | `0x10` (LZSS) | GameCube / Wii / DS / GBA |
| **LZ11** | `0x11` (Extended LZSS) | DS / 3DS |
| **LZ4** | `04 22 4D 18` | Switch and many cross-platform titles |
| **LZO / LZOvl** | Overlay trailer | DS titles (reverse LZO overlays) |
| **LZX** | `LZX` | DS |
| **MVDK** | `MVDK` | DS |
| **PSDK** | `PSDK` / `AT4PX` | DS |
| **PuCrunch** | `0x50 0x75` (`Pu`) | DS titles |
| **QuickLZ** | `QLZ` | Various third-party titles |
| **RLE** | `0x30` | DS titles |
| **RNC1 / RNC2** | `RNC\1`, `RNC\2` | Rob Northen ProPack titles |
| **SSZL** | `SSZL` | Wii |
| **VLX** | `VLX` | DS |
| **Yay0 (SZP)** | `Yay0` | Nintendo 64 / GameCube |
| **Yaz0 (SZS)** | `Yaz0` | GameCube / Wii / Switch |
| **Zstandard (Zstd)** | `28 B5 2F FD` | Switch / F-Zero 99 |

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
| **THP & Media Video** | `.thp`, `.h4m`, `.vid`, `.dpg`, `.fv`, `.ppm`, `.kwz`, `.mmstr`, `.rvid`, `.vx`, `.bik`, `.xmv`, `.vp6`, `.usm`, `.sfd`, `.sfv` | **`mobipeg`** / **`ffmpeg`** | GameCube/Wii THP, HVQM4, DPG, Bink, EA VP6, CRI Sofdec/Sofdec2 and related video to MP4 |
| **Wii / GameCube Disc Images** | `.iso`, `.wbfs`, `.wdf`, `.ciso`, `.wia` | **`wit`** (`--with-wit`) | Disc partition extraction & scrubbed disc creation |
| **Wii U Optical Discs** | `.wud`, `.wux` | **`wud2app`** + **`cdecrypt`** | Automated compressed WUX disc decompression, partition dump & decryption |
| **Wii WAD Packages** | `.wad`, `.app` | **`sharpii`** (`--with-sharpii`) | Wii title & IOS WAD archive unpacking and repacking |
| **Flash SWF Files** | `.swf` | **`ffdec`** (bundled; `--with-ffdec`) | Decompile ActionScript 1/2/3 and extract shapes, images, sounds, fonts, and binary assets via JPEXS Free Flash Decompiler |

---

## Documentation

- [Commands](docs/COMMANDS.md) — all tools and wszst subcommands
- [Workflows](docs/WORKFLOWS.md) — recursive extract, edit, repack
- [Formats](docs/FORMATS.md) — technical format reference
- [Wiimms SZS Tools](https://szs.wiimm.de/) — original documentation

---

## License & Credits

Based on **Wiimms SZS Tools** by Dirk Clemens (*Wiimm*), licensed under GPL v2 (`project/gpl-2.0.txt`). See [CREDITS.md](CREDITS.md) for attributions.

