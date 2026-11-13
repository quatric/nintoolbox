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
| **2XL LPAK** | — | *SCORE International Baja 1000* |
| **3DS Camera PACK** | `.pack` | 3DS |
| **49Games PAK** | `.pak` | *Summer Athletics - The Ultimate Challenge*, *Winter Sports* (49Games / Deep Silver) |
| **ABE BigFile** | `.bf` | *Rabbids Go Home* |
| **AFS** | `.afs` | *Dragon Ball Z: Budokai Tenkaichi 3* |
| **AGI** | `.pak` | *Skylanders: Swap Force* |
| **ALAR** | `.aar`, `.alar` | *Jump Super Stars*, *Jump Ultimate Stars* |
| **And-Kensaku** | `.rz` | *And-Kensaku* |
| **APAK** | `.apak` | Wii U / Switch |
| **ARC / U8** | `.arc`, `.szs` | Wii / GameCube NintendoWare & EAD |
| **ARC0** | `.fa` | *Yo-Kai Watch* |
| **ARCV** | `.arc` | Namco / Tose Wii titles |
| **Arika Archive** | `INFO.DAT`, `GAME.DAT`, `.arika` | Arika DS / DSi / Wii titles |
| **Artefacts MAP** | `.map`, `.gam` | *Diabolik: The Original Sin*, *Boot Camp Academy*, *Jillian Michaels Fitness Ultimatum*, *Dodge Racing*, *.gam` when that is present (all of* |
| **Asobo BigFile** | `.drv` | *Ratatouille* |
| **AST** | `.ast` | *I Spy Spooky Mansion* |
| **AT7** | `.at7` | Wii / PS2 |
| **ATB** | `.atb` | *Mario Party 4-8* |
| **ATLUS-NDX** | `.ndx`, `.idx`, `.bin` | *Radiant Historia*, *Shin Megami Tensei: Strange Journey*, *Devil Survivor*, *Etrian Odyssey* |
| **Atlus PAK** | `.pak` | *Trauma Center: Second Opinion*, *Trauma Center: New Blood*, *Trauma Team* |
| **Atomic Planet PUB** | `.pub` | *AMF Bowling: Pinbusters!* |
| **Avalanche THB/TBB** | `.thb` + `.tbb` | *Cars 2* |
| **Battle of the Bands BAG** | `.bag` | *Battle of the Bands* |
| **BCGRP** | `.bcgrp` | 3DS NintendoWare titles |
| **BCPAK** | `.bcpak` | *Top Shot Arcade*, *Our House - Party!* |
| **BEA** | `.bea`, `.nx.bea` | *WarioWare*, *Mario Party* |
| **BFGRP** | `.bfgrp` | Wii U / Switch NintendoWare titles |
| **BFMA** | `.bfma` | Wii U titles with a built-in manual |
| **BFSHA / BNSH** | `.bfsha`, `.bnsh` | Wii U / Switch |
| **BG4** | `.bg4` | *Mario & Luigi* (3DS) |
| **BGIB** | `.gcs`, `.gcm`, `.pak` (matched by `BGIB` magic) | *Pheasants Forever*, *Tournament of Legends*, *Nickelodeon Dora the Explorer* / *Go Diego Go!* / *Ni Hao, Kai-Lan*, *Rudolph the Red-Nosed Reindeer*, *The Secret Saturdays*, *Pirates PlundArrr* |
| **BIGF** | `.big` | Electronic Arts Wii titles |
| **BIGFILE TOC/DAT (DKAP)** | `BIGFILEn.TOC` + `.DAT` | *Totally Spies! Totally Party* |
| **Bj engine** | `.tx1`/`.tx2`, `.mtm`, `.bsi`, `.bsm` | *Super Karts* |
| **Blitz REV** | `.rev`, `.gcp` | *The House of the Dead: Overkill* |
| **Blue Castle BIG** | `.big`, `.dspi` | *The Bigs*, *The Bigs 2* |
| **Blue Tongue TRB** | `.trb` | *de Blob 2* |
| **BNS Archive** | `.bns` | *Samurai Warriors 3* |
| **BombShell data pack** | `.xwi`, `.xdx9` | *Bee Movie Game* |
| **CA01 / SA01** | `.ca01`, `.sa01` | 3DS / Wii U |
| **Capcom MT Framework ARC** | `.arc` | *Sengoku Basara 3* |
| **CCF** | `.ccf` | Wii / Switch |
| **CDGaCube CAR** | `.car` | *Birthday Party Bash* |
| **CHDp / C3Dp** | `.chd`, `.c3d` + `.cbd` | *Donkey Konga*, *Donkey Konga 2*, *Donkey Konga 3* |
| **CHNK tree** | `.scene`, `.object`, `.phn`, `.var`, `.cha` | *Paws & Claws: Pet Resort* |
| **CING-WPF** | `.wpf`, `.tbl` | *Hotel Dusk: Room 215*, *Wish Room* |
| **Climax ARC** | `.arc` | *Silent Hill: Shattered Memories* |
| **CNUT** | `.cnut` | *Wii Party* |
| **COD PAK0** | `.pak` | *Call of Duty: Black Ops*, *MW3* |
| **Coresoft JPack + NWTX** | `.pak` | *Fantasy Aquarium World* |
| **CPK** | `.cpk` | *Star Fox Zero* |
| **CRAM** | `.arc`, `.cram` | *Project X Zone* |
| **CS_DCT** | `.dct` | *Chicken Shoot* |
| **CyberConnect2 fpack** | `.fpk` | *Naruto Shippuden: Clash of Ninja Revolution 3*, *Tatsunoko vs. Capcom* |
| **DARC / BCMA** | `.darc`, `.bcma`, `.arc` | 3DS |
| **DC2 DCX / DCT** | `.dcx`, `.dct` | *Jakers! Kart Racing* |
| **DIG** | `.dig` | *Bomberman Land* |
| **DKZF** | `.tpl.dkz` | *Donkey Konga*, *Donkey Konga 2*, *3* |
| **DSi modcrypt SRL** | `.nds`, `.srl`, `.dsi` | DSi-enhanced and DSiWare titles |
| **DSi TAD** | `.tad` | the DSi's WAD, TwlSDK |
| **DSiWare export** | `.bin` (SD card) | DSiWare titles |
| **DTLS** | `dt00`, `ls00`, `.ls` | *Super Smash Bros. 4* |
| **EA DBPF package** | `.package`, `.mstex` | *MySims*, *MySims Kingdom*, *MySims Agents* |
| **EFFN** | `.eff`, `.effn` | *Super Smash Bros. for 3DS / Wii U*, *Super Smash Bros. Ultimate* |
| **F9RES** | `.res` | GameCube titles |
| **FBND bundle** | `data.bin` | *Sega Bass Fishing*, *Uchi Tsuri! Sega Bass Fishing* |
| **FBTI** | `.Mod`, `.Mot` | *Rune Factory: Frontier* |
| **FCAT** | `.dat` | *Winning Post 7 / World* |
| **FMOD FSB** | `.fsb` | FSB3 / FSB4 / FSB5, Wii |
| **FSYS** | `.fsys` | GameCube / Wii |
| **Ganbarion ALAR** | `.aar` | *Pandora's Tower* |
| **GAR / ZAR** | `.zar`, `.gar` | *OoT3D*, *MM3D*, *LM3DS* |
| **GFA** | `.gfa` | Wii / 3DS / Wii U |
| **GFLX** | `.gflxpack` | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFMPACK** | `.gfpack` | *Pokémon X/Y/ORAS* |
| **GFPAK** | `.gfpak` | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFPKG** | `.gfpkg`, `.bin`, `.pak` | *Pokémon X / Y*, *Omega Ruby / Alpha Sapphire*, *Sun / Moon* |
| **GGXArchiver** | — | *Radirgy Noa* |
| **Goliath GS Package** | `.pkz` | *Skylanders: SuperChargers Racing*, *The Amazing Spider-Man* |
| **Grip RES** | `.res` | *Sesame Street: Elmo's Musical Monsterpiece* |
| **h.a.n.d. FBC** | `.fbc` | *Oyako de Asobo: Miffy no Omochabako* |
| **h.a.n.d. FBC bundle** | `.fbc`, `.brres`, `.tpl`, `.brasd`, `.ssb` | *Miffy no Omochabako* |
| **Harmonix Ark** | `.hdr` + `_N.ark` | *AC/DC Live: Rock Band Track Pack*, *The Beatles: Rock Band* |
| **Hash PAK (`!ZLS`)** | `PAK\0` magic | *Kid Adventures: Sky Captain* |
| **Havok HKX (classic packfile)** | `.hkx`, `.HKX` | *Tenchu: Shadow Assassins* |
| **HBDF** | `.hbdf`, `.hsdf` | *Mario Party DS* |
| **HDVOICE (Tenchu: Shadow Assassins)** | `.hd` | *Tenchu: Shadow Assassins* |
| **Heavy Iron HO** | `.ho` | *WALL-E*, *Up* |
| **HOG** | `.hog`, `.gcb` | *Ultimate Band*, *Tiger Woods PGA Tour* |
| **Humongous Resource.rez** | `.rez` | *Backyard Football '10* |
| **HVP** | `.hvp` | *Aladdin Magic Racer*, *X Factor*, *Knockout Party*, *Obscure: The Aftermath* |
| **Hyrule Warriors** | `.idx`, `.bin` | 3DS |
| **IEAR** | — | DS |
| **iPck index pack** | `.bin` | Wii |
| **IPK** | `.ipk` | *Just Dance*, *Rayman Origins/Legends* |
| **IQIPACK** | `.pak` | NVIDIA Shield iQiyi titles |
| **JARC** | `.jarc` | DS |
| **Junction Point PAK** | `.pak`, `.nif_wii`, `.hkx_wii`, `.bin` | *Epic Mickey* |
| **JUPITER-PCK** | `.pck` | *The World Ends with You*, *Kingdom Hearts: Re:coded* |
| **KP0 package** | `.pkt` | *Power Punch* |
| **KPBIN** | `.kpbin` | *New Super Mario Bros. Wii* |
| **kRAW music** | `.kRAW` | *Geometry Wars: Galaxies* |
| **Krome RKV2** | `.mus`, `.mdl`, `.mdg`, `.tex` | *Star Wars: The Force Unleashed*, *Happy Feet Two* |
| **Kylotonn FVRT archive** | `.vid`, `.snd`, `.anm`, `.mdl`, `.txc` | *My Body Coach*, *My Body Coach 2*, *Cocoto Festival* |
| **L5-PAC** | `.pac`, `.dat` | *Dragon Quest IX: Sentinels of the Starry Skies* |
| **Lost in Blue: Shipwrecked archive** | `.bin`, `.zms` | *Lost in Blue: Shipwrecked* |
| **LSPK** | `.pk`, `.pkh`, `.lspk` | *The Last Story* |
| **LZBIN** | `.bin`, `.lzbin` | *Mario Party DS* |
| **Marvelous pBin / FIPAFTEX** | — | *Harvest Moon: Animal Parade* |
| **MDR** | `.mdr` | *Dance Dance Revolution Mario Mix* |
| **Midway WAD** | `.WAD` | Wii |
| **MKGPDX PAC** | `.pac`, `.mkgpdx` | *Mario Kart Arcade GP DX* |
| **Momotarou Dentetsu `cmps` archive** | — | *Momotarou Dentetsu 16* |
| **MPBIN** | `.bin` | GameCube / Wii |
| **MPH SNDFILE archive** | `.arc` | *Metroid Prime Hunters* (DS) |
| **MPR PACK** | `.pak` | *Metroid Prime Remastered* |
| **MRQZ** | `.pak` | *My Fitness Coach* |
| **MSR** | `.pkg`, `.bin` | *Metroid: Samus Returns* |
| **MTXT** | `.mtxt` | Switch titles |
| **NARC** | `.narc` | DS / DSi |
| **Natsume BIN** | `.bin` | *Harvest Moon* |
| **NCCARC** | `.nccarc` | DS titles |
| **NDS / SRL / DSI** | `.nds`, `.srl`, `.dsi` | All DS / DSi games |
| **Neversoft GH PAK/IMG** | `.pak.ngc`, `.img.ngc` | *Guitar Hero* |
| **NIBM audio** | `.aud` | *CSI: Hard Evidence*, *Destroy All Humans* |
| **NIF (Gamebryo 20.6, Wii)** | `.nif` | *Pocoyo Racing* |
| **Nippon Ichi WIIIDARC** | `.dat` | *Phantom Brave: We Meet Again* |
| **NLG DICT** | `.dict`, `.data` | *Federation Force*, *Luigi's Mansion 2/3*, *Strikers* |
| **NUP** | `.nup` | Decrypted Switch / Switch 2 dumps (*Kirby and the Forgotten Land – Nintendo Switch 2 Edition*, *Kirby Air Riders*) |
| **NXARC** | `.nxarc` | Switch titles |
| **Okami DAT / T** | `.dat`, `.t` | *Okami* |
| **Opoona SL01 / OPKG** | `.btl`, `.bin`, `.dat` | *Opoona* |
| **PACK v2** | `.pak` | *Ski and Shoot* |
| **PAC (Nd Cube)** | `.bin` | *Mario Party 10*, *Animal Crossing: amiibo Festival* |
| **PAC / MRG** | `.pac`, `.mrg` | HAL Laboratory / Game Arts Wii titles |
| **PAK\3 archive** | `.pak` | *International Athletics* |
| **PAKb** | `.pak` | *Secret Files: Tunguska*, *Secret Files 2: Puritas Cordis* |
| **PCK2 / PLZ** | `.plz`, `.pck2` | *Professor Layton and the Diabolical Box* |
| **PDA BRA** | `.bra`, `.3df`, `.bin`, `.spr`, `.prt` | *Gummy Bears: Magical Medallion / MiniGolf*, *Pool Hall Pro*, *Build'n Race* |
| **Pet Vet packages DAT** | `.dat` | *Pet Vet: Marine Patrol* |
| **phx PARC / PDAT** | `assets.cat` + `assets_*.dat` | *Kirby Air Riders* |
| **Pikmin ARC/DIR** | `.arc` + `.dir` | *Pikmin* |
| **Pipeworks bundle** | `.bdg` | *Merv Griffin's Crosswords* |
| **PK03 hash package** | `PK03` magic | *Deer Drive Legends* |
| **PKG (Barking Lizards)** | `.pkg` | *Nickelodeon: The Naked Brothers Band - The Video Game* |
| **PKG / GPKG / GPAK** | `.pkg`, `.pak`, `.gpak` | *Bonsai Barber*, *World of Goo* |
| **PKI** | `.PKI` | *Bermuda Triangle: Saving the Coral*, *Ocean Commander* |
| **PKZ** | `.pkz` | *Bayonetta*, *Astral Chain* |
| **PlatinumGames DAT** | `.dat` | *Star Fox Zero*, *MadWorld* |
| **PRC** | `.prc`, `.param` | *Super Smash Bros. for 3DS / Wii U*, *Super Smash Bros. Ultimate* |
| **PTD** | `.ptd`, `.pdt` | *Mario Party 4-8* |
| **PVOL** | `.pvol`, `.vol` | *Godzilla: Unleashed*, *Godzilla: Save the Earth* |
| **Radical RCF** | `.rcf` | *Crash of the Titans*, *Simpsons Hit & Run* |
| **RARC** | `.rarc`, `.arc` | GameCube / Wii |
| **RCB resource bundle** | `.rcb` | *Bass Fishing Wii* |
| **Rebellion Asura resource archive** | `.wii`, `.asrBE`, `.guiBE` | Rebellion Asura Wii titles |
| **Rebellion Asura Wii ARC** | `.tNN`, `.t1` | *PDC World Championship Darts* |
| **resource.dat + resource.tbl (LZ11)** | `.dat`, `.tbl` | *Deer Drive* |
| **Retro PAK / CSMP** | `.csmp`, `.strm`, `.ras`, `.strg` | *Metroid Prime 3*, *Donkey Kong Country Returns* |
| **RFL_Res** | `RFL_Res.dat`, `.dat` | Wii / 3DS / Wii U |
| **RKET** | `.rck`, `.spa` | *Aqua Panic!*, *SPRay*, *Cyberbike*, other Eko Software games |
| **RKV** | `.rkv` | *Happy Feet Two*, *Legend of Spyro*, *Owls of Ga'Hoole* |
| **RMHG container** | `.rsl` | *No More Heroes*, *No More Heroes 2* |
| **RPAK** | `.rpak`, `.pak` | *Donkey Kong Country Returns* |
| **RSO** | `.rso` | *Skylanders: SuperChargers Racing* |
| **RST / TOC** | `.rst`, `.toc` | *Excite Truck*, *Excitebots* |
| **RWS / MTD** | `.RWS`, `.mtd` | *DreamWorks How to Train Your Dragon* |
| **RZPK** | `.rzpk` | *Mario Party* (3DS) |
| **SARC** | `.sarc`, `.szs` | Wii U / Switch / 3DS |
| **Scribblenauts index.bin / .p** | `.p` | *Scribblenauts Unlimited* |
| **SEC** | `.sec` | *Octomania* |
| **SEDB** | `.rps`, `.txb` | *Dragon Quest X* (Wii) |
| **Sega ARCB / AVLZ** | `.arc` | *Super Monkey Ball: Banana Blitz* |
| **Sega URES/AMPP/GDEN/SASN** | `.urs`, `.amf`, `.gdr`, `.gdn`, `.arm` | Wii |
| **SFZDAT** | `.dat` | *Star Fox Zero* |
| **Shade BIN / BLN** | — | *Inazuma Eleven Strikers* |
| **SHARC / SHARCFB** | `.sharc`, `.sharcfb`, `.sharcb` | Wii U / Switch / Switch 2 |
| **Shrek Forever After APK (APKF)** | `.apk` | *Shrek Forever After* |
| **Simple Wii Series DPK** | `.dpk` | *Simple Wii Series Vol. 1-4 and 6*, *Final Fantasy* |
| **SIR0** | `.sir0` | DS / 3DS |
| **DATA-ARC** | `.arc` | *Super Smash Bros. Ultimate* |
| **STME** | `.stm` | *Need for Speed: Hot Pursuit* |
| **Storybook ONE** | `.one` | *Sonic and the Secret Rings*, *Black Knight* |
| **STPK** | `.srd`, `.stpk` | None verified (*Jump Stars* use ALAR) |
| **Sumo STZ** | `.stz` | *Sonic & Sega All-Stars Racing* |
| **Sumo WAS** | `.was` | *Sonic & Sega All-Stars Racing*, */* |
| **T4-RES (Tenchu: Shadow Assassins)** | `.b` | *Tenchu: Shadow Assassins*, *"` tagged resource family (`Common/Camera/* |
| **Telltale TTARCH v3-6** | `.ttarch` | *Sam & Max: Beyond Time and Space* |
| **Terminal Reality POD** | `.pod` | *Nickelodeon Dance* |
| **THOR** | `.wii` | *Phineas and Ferb: Quest for Cool Stuff* |
| **THQ Australia pack** | `.pak`, `.rad` | *Avatar: The Last Airbender* |
| **TMPK** | `.pack`, `.tmpk` | *The Legend of Zelda: Twilight Princess HD* |
| **Torus Games hunkfile** | `.hnk` | *Barbie & Her Sisters: Puppy Rescue* |
| **Toshi TSFB** | `.ttl`, `.tkl` (`.trb`) | *Nickelodeon Barnyard* |
| **Town Factory PCKG** | `.pac`, `.pcha`, `.pac0`-`9`, `.bin`, `.dat` | *Little King's Story* |
| **TREASURE-MRG** | `.mrg`, `.bg4`, `.bg8` | *Bleach: Dark Souls*, *Bleach: The Blade of Fate*, *Bangai-O Spirits* |
| **TRPAK** | `.trpak` | *Pokémon Scarlet / Violet* |
| **Ubisoft Jade BigFile** | `.bf` | *Rayman Raving Rabbids 1/2/TV Party*, *Prince of Persia* |
| **UE4 PAK** | `.pak` | *Mario & Luigi: Brothership* |
| **Unity 2.x SerializedFile** | `.assets` | *My Animal Centre*, *JumpStart*, *El Chavo*, *Big Time Rush* |
| **Unity AssetBundle (UnityFS)** | `.unity3d`, `.bundle`, `.assetbundle`, `.ab` | Unity 5.3+ games (LZMA/LZ4/LZ4HC; extracts `CAB-*`, `.resS`, `.resource`) |
| **Vblank BFP / BAP** | `.bfp`, `.bap` | *Retro City Rampage DX*, *Shakedown: Hawaii* |
| **VCRA** | `.bin`, `.vcra` | Wii |
| **VFF** | `.vff` | Wii channels and save data |
| **VFXB / PTCL** | `.ptcl`, `.eset`, `.vfxb` | Wii U / Switch |
| **VIBS** | `.vibs` | Switch first-party titles |
| **WADH** | `.wad` (DataWII.wad) | *Ninjabread Man* |
| **WARC** | `.warc` | Wii U |
| **Wii resource ARC** | `wii\0` magic | *Petanque Master*, *Petanque Pro* |
| **WSI** | `.wsi` | *Alone in the Dark*, *Aladdin Magic Racer* |
| **WTA / WTP** | `.wta` + `.wtp` | *Star Fox Zero* |
| **WUD / WUX** | `.wud`, `.wux` | All Wii U games |
| **XMSG** | `.bin` | *Wii Party* |
| **XPCK** | `.xc`, `.xpck` | *Inazuma Eleven*, *Professor Layton*, *Yo-kai Watch* |
| **XPK** | `.pak` | *Angry Birds Star Wars* |
| **ZAP archive** | `.zap` | *Thrillville: Off the Rails* |
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
| **Alchemy IGZ** | `.igz` | **GLB** | *Madagascar: Escape 2 Africa* |
| **Athletics-engine ANM** | `.ANM` | *(text dump)* | *International Athletics* |
| **BCH** | `.bch` | **GLB** | 3DS |
| **BCMDL / CGFX** | `.bcmdl`, `.cgfx` | **GLB** | 3DS |
| **BCRES** | `.bcres` | **GLB** | 3DS |
| **BFRES** | `.bfres` | **GLB** | Wii U / Switch |
| **BMD** | `.bmd`, `.bdhc` | **GLB** | DS |
| **BNFM** | `.bnfm` | **GLB** | *Mario Party 10*, *Animal Crossing: amiibo Festival* |
| **CAMELOT-MDLR** | `.mdlr`, `.col` | *(text dump)* | *Golden Sun: Dark Dawn* |
| **CAPCOM-MODS** | `.mods` | *(text dump)* | *Ghost Trick: Phantom Detective* |
| **Capcom MT Framework Wii animation SRT table** | `.astl` | *(text dump)* | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii collision table** | `.col` | *(text dump)* | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii effect sprite animation** | `.ean` | *(text dump)* | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii model** | `.rmd` | **GLB** | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii motion bank** | `.lmt` | *(text dump)* | Capcom MT Framework Wii titles |
| **CSB** | `.csb` | **GLB** | *Paper Mario: The Thousand-Year Door*, *The Origami King*, *Color Splash* |
| **CTB** | `.ctb` | *(text dump)* | *Paper Mario: The Thousand-Year Door*, *The Origami King*, *Color Splash* |
| **DC2 model** | `.dcm` | **GLB** | *Speed 2* |
| **DC2 world** | `.dcw` | **GLB** | *Speed 2* |
| **FEDMODEL** | `.fedmodel` | **GLB** | *Metroid Prime: Federation Force*, *Luigi's Mansion: Dark Moon*, *Luigi's Mansion 3* |
| **FEDSKEL** | `.fedskel` | **GLB** | *Metroid Prime: Federation Force*, *Luigi's Mansion: Dark Moon*, *Luigi's Mansion 3* |
| **FLCG collision** | `.GCL` | *(text dump)* | *No More Heroes* |
| **G1M** | `.g1m` | **GLB** | *Hyrule Warriors Legends*, *Fire Emblem Warriors* |
| **G4PKM** | `.g4pkm` | — | Unidentified |
| **GAN2 animation** | `.GA2` | *(text dump)* | *No More Heroes* |
| **GF1MOT** | `.gf1mot` | *(text dump)* | *Pokémon X / Y*, *Omega Ruby / Alpha Sapphire* |
| **GFBANM** | `.gfbanm` | *(identification only)* | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFBMDL** | `.gfbmdl` | *(identification only)* | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFMODEL** | `.gfmodel` | **GLB** | *Pokémon X/Y/ORAS* |
| **GFMOT** | `.gfmot` | *(text dump)* | *Pokémon X / Y*, *Sun / Moon* |
| **GLG / RLG** | `.glg`, `.rlg` | **GLB** | *Super Mario Strikers*, *Mario Strikers Charged* |
| **GMF2 model** | `.gm2`, `.gmf2` | *(text dump)* | *No More Heroes* |
| **HSD** | `.dat` | **GLB** | GameCube |
| **HSF** | `.hsf` | **GLB** | GameCube / Wii |
| **Imagine camera path** | `.cam` | *(text dump)* | *Party Babyz* |
| **Imagine WA2 skeletal animation** | `.wa2` | *(text dump)* | *Beastly* |
| **Imagine WSN scene mesh** | `.wsn` | *(text dump)* | *Beastly*, *Party Babyz* |
| **J3D BCK / BTK** | `.bck`, `.btk` | *(text dump)* | GameCube / Wii |
| **J3D BDL** | `.bdl` | **GLB** | *The Legend of Zelda: The Wind Waker*, *Twilight Princess*, *Super Mario Sunshine* |
| **J3D BMD** | `.bmd` | **GLB** | *The Legend of Zelda: The Wind Waker*, *Twilight Princess*, *Super Mario Sunshine* |
| **J3D BPK / BTP** | `.bpk`, `.btp` | *(text dump)* | GameCube / Wii |
| **Konami ZMB** | `.zmb` | **OBJ** | *Dance Dance Revolution: Winx Club* |
| **LMD** | `.lmd` | — | Unidentified |
| **MBN** | `.mbn` | **GLB** | 3DS titles with `.bch` scenes |
| **MDL0 / BRRES** | `.mdl0`, `.brres` | **GLB** | Wii |
| **Midway PHA** | `.pha`, `.anm` | *(text dump)* | Wii |
| **MOD** | `.mod` | **GLB** | Wii |
| **MPH Model** | `_Model.bin` (+ `_Tex.bin`) | **GLB** | *Metroid Prime Hunters* (DS; textures written as PNG) |
| **MPR CMDL / SMDL / WMDL** | `.cmdl`, `.smdl`, `.wmdl` | **GLB** | *Metroid Prime Remastered*, *DKCTF* |
| **MPR SKEL** | `.skel` | **GLB** | *Metroid Prime Remastered*, *DKCTF* |
| **MSH (PMsh)** | `.msh` | **GLB** | Wii |
| **MTMOD** | `.mod` | **GLB** | *Resident Evil / Monster Hunter* |
| **Ninja motion** | `.njm` | *(text dump)* | *Radirgy Noa*, *Illvelo* |
| **Ninja NJCM** | — | **GLB** | *Chaos Field*, *Karous* |
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
| **OCN** | `.dat` | **GLB** | Wii |
| **Opoona ATB** | `.atb` | **OBJ** | *Opoona* |
| **Opoona MAP** | `.map` | **GLB** | *Opoona* |
| **Opoona MDL** | `.mdl` | **GLB** | *Opoona* |
| **Opoona MOT** | `.mot` | *(text dump)* | *Opoona* |
| **Pangya PET / MPET** | `.pet`, `.mpet` | *(text dump)* | *Super Swing Golf* |
| **PCS** | — | *(text dump)* | *Soma Bringer* |
| **PERS** | `.pers` | *(raw payload)* | N64 |
| **Petz DPGZ track** | — | *(text dump)* | *Petz Sports* |
| **Petz MACT table** | — | *(text dump)* | *Petz Sports* |
| **PMSH (phx)** | `.pmsh` + `.pmsh.gbuf` (from PARC) | **GLB** | *Kirby Air Riders* |
| **PANM (phx)** | `.panm` (from PARC) | **GLB** (skeleton + animation), property channels JSON | *Kirby Air Riders* |
| **engine_bin (phx)** | `startup.dat`, `Bin/config.bin` | **JSON** / Prism data | *Kirby Air Riders* |
| **Phive collision** | `.bphsh`, `.bphsc` | **OBJ** (+ material JSON; water grids as 16-bit PNG) | *Splatoon 3*, *Mario Kart World* |
| **RenderWare clump (DFF/RWS)** | `.dff`, `.rws` | **OBJ** | Wii titles |
| **SANIM** | `.sanim` | *(text dump)* | *Super Mario Strikers* |
| **Scribblenauts vector mesh** | — | *(text dump)* | *Scribblenauts Unlimited* |
| **Sega Ninja chunk model** | `.nj`, `.njm` | **OBJ** | *Radirgy Noa*, *Illvelo* |
| **TTMODEL** | `.model` | **GLB** | *LEGO Star Wars: The Skywalker Saga* |
| **Ubisoft BAN** | `.BAN` | *(text dump)* | Wii |
| **Ubisoft Imagine WIZ model** | `.wiz` | **OBJ** | *Beastly*, *Babyz* |
| **Ubisoft SERZ geometry** | `.GeoWii` | **OBJ** | Wii |
| **WMB** | `.wmb` | **GLB** | *Star Fox Zero* |

`Byte-Exact Roundtrip` = decode → GLB → re-encode reproduces the original file's bytes identically (canonical fixed-point verified), not just a successful encode.

---

### Textures & 2D Graphics

| Format | Extensions | Games |
| --- | --- | --- |
| **AJPG / ODH** | `.ajpg` | GBA / Wii Message Board |
| **Arc Rise Fantasia WTM** | `.wtm` | *Arc Rise Fantasia* |
| **ARTE-TEX** | `.tex` | *Dragon Quest IV: Chapters of the Chosen*, *Dragon Quest V: Hand of the Heavenly Bride*, *Dragon Quest VI: Realms of Revelation* |
| **ART / IMG** | `.art`, `.img` | Wii |
| **ASTC** | `.astc` | Switch and mobile titles |
| **Avalanche DBL** | `.dbl`, `.mdb` | *Disney's Bolt* |
| **BCFNT / BFFNT / BRFNT** | `.bcfnt`, `.bffnt`, `.brfnt` | 3DS / Wii U / Wii / Switch / Switch 2 |
| **BFTTF / BFOTF** | `.bfttf`, `.bfotf` | Wii U / Switch / Switch 2 |
| **BGC image table** | `.bgc` | *Happy Neuron Academy* |
| **BGP** | — | *Soma Bringer* |
| **BGST3** | `.bgst3` | *Kirby's Epic Yarn* |
| **DC2 font cache** | `.dcf` | *Speed 2* |
| **EA FntG** | `.gfn` | Wii |
| **EA SHPG** | `.gsh` | Wii |
| **EA TXG** | — | *Tiger Woods PGA Tour* |
| **Exient TWI** | `.twi` | *Need for Speed: Hot Pursuit* |
| **FCPX complex font** | `.bfcpx` | Switch / Switch 2 |
| **BCLIM** | `.bclim` | 3DS |
| **BFLIM** | `.bflim` | Wii U |
| **BNR** | `.bnr` | GameCube / Wii titles |
| **BNSTX** | `.bnstx` | Switch titles |
| **BNTX** | `.bntx` | Switch |
| **BREFT** | `.breft`, `.bt-img` | Wii |
| **BTGA / LGA** | `.btga`, `.lga` | LEGO 3DS titles |
| **BTI / TPL** | `.bti`, `.tpl` | GameCube / Wii |
| **GCT0** | `.BIN` | *No More Heroes* |
| **Havok-style HVT texture** | `.hvt` | *Aladdin Magic Racer* |
| **International Athletics TEX** | `.TEX` | *International Athletics* |
| **J3D BMT** | `.bmt` | GameCube / Wii |
| **JParticle JPC (textures)** | `.jpc` | *Twilight Princess*, *Super Mario Galaxy* |
| **JSystem BFN** | `.bfn` | *Twilight Princess*, *Super Mario Galaxy* |
| **Krome TEX** | `.tex` | *Star Wars: The Force Unleashed* |
| **Kylotonn VID (video frames)** | `.vid` | *Cocoto Festival* |
| **Midway FNT** | `.fnt`, `.tim` | Wii |
| **Midway SPR** | `.spr` | Wii |
| **Midway TIM** | `.tim` | Wii |
| **Momotarou Dentetsu 2010 font sheet** | — | *Momotarou Dentetsu 2010*, *Momotarou Dentetsu 16* |
| **Muramasa FTX / NMS** | `.ftx`, `.nms` | *Muramasa: The Demon Blade* |
| **Next Level texture banks** | `.rlt`, `.res`, `.dmn` | *Mario Strikers Charged* |
| **Nippon Ichi IMY** | `.imy` | *Phantom Brave: We Meet Again* |
| **NW4R intermediate texture** | `.rtex` | *Der Gesundheitscoach* |
| **OBP** | — | *Soma Bringer* |
| **OIBB object pack** | — | *Doctor Fizzwizzle's Animal Rescue* |
| **Opoona TPL** | `.tpl` | *Opoona* |
| **Pangya GSR** | `.gsr` | *Super Swing Golf* |
| **Paon .d4** | `.d4` | *Donkey Kong: Barrel Blast* |
| **Petz TEXL** | — | *Cranium Kabookii* |
| **Petz TTPL** | `.ozzy`, `...txl` | *Petz Sports*, *Petz Dogz/Horsez/Catz* |
| **PTEX (phx)** | `.ptex` + `.ptex.body` (from PARC) | *Kirby Air Riders* |
| **Qubic LTEX** | `.ltex` | *2 Fast 4 Gnomz* |
| **RenderWare TXD** | `.txd` | *GameCube/Wii* |
| **Rhythm Heaven Groove textures / sprite sets (TEX / SPR)** | `.tex`, `.spr` | *Rhythm Heaven Groove* |
| **Camelot GX bank** | *(none)*, `.stpl`, `.sbn` | *Mario Golf: Toadstool Tour*, *Mario Power Tennis*, *We Love Golf!* |
| **CAN** | `.can` | *Excite Truck*, *ExciteBots* |
| **CMAB** | `.cmab` | *Ocarina of Time 3D*, *Majora's Mask 3D* |
| **Sakura Wars HTX** | `.htx` | *Sakura Wars: So Long, My Love* |
| **Scaleform GFx / Flash CWS** | `.gfx` | *Epic Mickey* |
| **Sega GTS texture set** | — | *Ghost Squad* |
| **Sega-style PVR texture** | — | *Radirgy Noa*, *Illvelo* |
| **SERZ texture groups** | `.TgWii` | Wii |
| **Sonic TXAG TXD** | `.txd` | *Sonic and the Black Knight* |
| **Super Fruit Fall TDW / MEE** | `.tdw`, `.mee` | *Super Fruit Fall* |
| **TARB terrain tiles** | `.hght`, `.mate`, `.norm`, `.extm` | *Mario Kart World* |
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
| **Layton backgrounds** | `.arc`, `.arb` | *Professor Layton* |
| **MPR TXTR** | `.txtr` / `.mpr.txtr` | *Metroid Prime Remastered* |
| **MTTEX** | `.tex` | 3DS |
| **MWT** | `.MWT` | *Bermuda Triangle: Saving the Coral*, *Rock Blast*, *Veggy World*, *Ocean Commander*, *Arcade Shooting Gallery* |
| **PGF** (two-colour font atlases) | `.pgf` | *Bermuda Triangle: Saving the Coral*, *Rock Blast*, *Veggy World*, *Ocean Commander*, *Arcade Shooting Gallery*, *Offshore Tycoon* |
| **NCER / NANR** | `.ncer`, `.nanr` | DS |
| **NCGR / NCLR** | `.ncgr`, `.nclr` | DS |
| **NDS banner** | `banner.bin` | DS / DSi |
| **NGCBM (`_NGCBM01`)** | `.ngcbm` | *Totally Spies! Totally Party* (unpacked from BIGFILE DKAP members) |
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
| **Trauma Team TPX / BF / BM2** | `.tpx`, `.bf`, `.bm2` | *Trauma Team* |
| **Tropical TXTR** | `.txtr` | *Donkey Kong Country: Tropical Freeze* |
| **TVOL** | `.tvol` | Koei Tecmo / Gust titles |
| **TXE** | `.txe` | *Pikmin* |
| **TXTG** | `.txtg` | Next Level Games titles |
| **Ubisoft Imagine WSP sprite** | `.wsp` | *Babysitting Party*, *Babyz* |
| **WIBN** | `banner.bin`, `.bnr` | Wii |
| **Wii banner** | `opening.bnr`, `IMET`, `IMD5` | Wii |
| **Wii save banner / icon** | `savebanner.raw`, `saveicon<N>.raw` | Wii save data |
| **WII! textures** | `.WII` | *Star Trek: Conquest* |
| **WTB** | `.wta`, `.wtb` | PlatinumGames Switch / Wii U titles |
| **WTXID object image** | `.wtxg`, `.wtxa` | *Cooking Mama: Cook Off* |
| **XIMG** | `.xi` | Level-5 3DS / Switch titles (e.g. *Yo-kai Watch*) |
| **XTX** | `.xtx` | Tegra block-linear RGBA8 / BCn / ASTC; Switch 2 (Orin GOB) textures, e.g. sead's nvn2_font |

`Byte-Exact Roundtrip` = encode → decode → re-encode to the same destination name
reproduces the file's bytes. Exercised by `t_byte_fixed_points()` in `tests/regress.sh`

---

### Audio, Sound & Music

| Format | Extensions | Games |
| --- | --- | --- |
| **49Games WSBL** | — | *Summer Athletics* |
| **AAL audio data (AMTA / AATN / AROC / AACL / AGST / AADR)** | `.amta`, `.baatn`, `.baroc`, `.baacl`, `.bagst`, `.baadr` | Switch / Switch 2 |
| **Acoustic / light probe grids (DAPG / DLPG)** | `.dapg`, `.dlpg` | *Mario Kart World* |
| **AGSC** | `.agsc` | GameCube |
| **Alfa System STS** | `.sts` | *Castle of Shikigami III* |
| **Asobo VAI** | `.vai` | GameCube |
| **Asura SFX / RedSpark** | `.sfx`, `.rsd` | *Sniper Elite*, *MadWorld* |
| **BARS** | `.bars` | Wii U / Switch / Switch 2 |
| **BCBNK** | — | 3DS |
| **BCSAR / BCWAR / BCWAV** | `.bcsar`, `.bcwar`, `.bcwav` | 3DS |
| **BCWSD** | — | 3DS |
| **BFSAR / BFWAR / BFWAV** | `.bfsar`, `.bfwar`, `.bfwav` | Wii U / Switch |
| **BRSAR / RBNK / RWAV** | `.brsar`, `.rbnk`, `.rwav` | Wii |
| **BRSTM / BCSTM / BFSTM** | `.brstm`, `.bcstm`, `.bfstm` | Wii / 3DS / Wii U / Switch |
| **BWAV** | `.bwav` | Switch |
| **Capcom MT Framework Wii sound event request** | `.qers` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii sound tables** | `.qrts`, `.qers` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii surround sound** | `.srnd` | Capcom MT Framework Wii titles |
| **Capcom SRT + SSD** | `.srt`, `.ssd` | *Monster Hunter Tri* |
| **Cauldron STR** | `.str` | GameCube |
| **CRI AAX** | `.aax` | *Mario & Sonic at the London 2012 Olympic Games* |
| **CRI ADX** | `.adx` | *Ghost Squad* |
| **CRI AIX** | `.aix` | *Sonic Unleashed*, *Metroid: Other M* |
| **CS12 sound container** | — | *Emergency Heroes* |
| **DLS** | `.dls` | *Ninjabread Man*, *Monster Trux Arenas*, *Kidz Sports*, *Rig Racer 2* |
| **DSP container ASF** | `.asf` | *Brunswick Pro Bowling*, *Celebrity Sports Showdown* |
| **EA BNKb** | `.abk`, `.bnk` | *Tiger Woods PGA Tour*, *Madden NFL 10* |
| **EA PathFinder MUS** | `.mus` | *The Godfather: Blackhand Edition*, *EA Sports Active* |
| **EA SCHl** | `.ngc`, `.str` | *Tiger Woods PGA Tour 12* |
| **EA SCHl EALayer3** | `.ast` | *Celebrity Sports Showdown* |
| **EA SCHl EA-XA** | `.exa` | *The Godfather: Blackhand Edition* |
| **EA SCHl NGC** | `.ngc`, `.fcd`, `.ast` | *Tiger Woods PGA Tour*, *NBA Live 08*, *Madden NFL 10* |
| **EID** | `.eid` | *I Spy Spooky Mansion* |
| **Eurocom MUSX** | — | *Dead Space: Extraction*, *The Mummy: Tomb of the Dragon Emperor* |
| **Exient BNK** | `.bnk` | *Need for Speed: Hot Pursuit* |
| **Exient WIIADPCM** | `.adpcm` | *Need for Speed: Hot Pursuit* |
| **FMOD FEV** | `.fev` | *WALL-E*, *Kung Fu Panda 2*, *Marvel Super Hero Squad* |
| **FSAR 6.2 sound archive** | `.fsst` | *Switch 2 Welcome Tour* |
| **GBA MP2k (Sappy)** | `.gba` | Game Boy Advance |
| **GC LPS / KCEJE DSP / YMF** | `.lps`, `.ymf` | GameCube |
| **GCSP** | `.sounds` | GameCube |
| **GCSW** | `.gcw` | GameCube |
| **GCub** | `.gcub` | GameCube |
| **HAL HPS** | `.hps` | *Super Smash Bros.* series, *Kirby* |
| **Hal SSM** | `.ssm` | GameCube |
| **Hudson PDT** | `.pdt` | GameCube |
| **Inevitable IDSP** | `.idsp` | *Defender* |
| **Infernal ITL** | `.itl`, `.ds2` | *MX vs. ATV Untamed* |
| **IOI IOISNDSTREAM** | `.str` | Wii |
| **ISH + ISD** | `.ish`, `.isd` | GameCube |
| **JAudio AAF** | `.aaf`, `.aw` | GameCube |
| **Konami VAS** | `.vas` | *Jikkyou Powerful Pro Yakyuu*, *MLB Power Pros* |
| **Krome MUS** | `.mus` | *Star Wars: The Force Unleashed* |
| **KTSS** | `.kns`, `.kno`, `.ktss` | Switch |
| **KT WiiBGM / G1L** | `.g1l` | Wii / Wii U |
| **Kuju KDSP voices** | `.ksp` | Wii |
| **KWA** | `.kwa` | *The Lord of the Rings: Aragorn's Quest* |
| **Kylotonn SND** | `.snd` | *Cocoto Festival* |
| **Madagascar SNDS** | `.snds` | *Madagascar: Escape 2 Africa* |
| **Major Minor WAF** | `.waf` | *Major Minor's Majestic March* |
| **Metroid Prime RSF** | `.rsf` | *Metroid Prime* |
| **Midway ADS** | `.ads` | GameCube |
| **Midway ASF bank** | `.ASF` | Wii |
| **Midway SFX bank** | `.sfx` | Wii |
| **Monster Games SFX** | `.sfx` | *Excite Truck*, *ExciteBots* |
| **Monster Games SNG** | `.sng`, `.sn0` + `.sf0` | *ExciteBots* |
| **Namco IDSP** | — | Wii / Wii U / 3DS / Switch |
| **Namco NUB** | `.nub` | *Family Ski*, *Family Jockey* |
| **NDS HWAS** | `.hwas` | DS |
| **NDS RRDS** | `.rrds` | DS |
| **NDS STRM / SWAV** | `.strm`, `.swav` | DS |
| **NDS SWAR** | `.swar` | DS |
| **NDS ZSD** | `.zsd` | DS |
| **Neko MUSIC.GRP** | `MUSIC.GRP` | *Cocoto Kart Racer 2*, *Cocoto Magic Circus* |
| **Next Level wave bank** | `.resbun`, `.nlxwb` | *Mario Strikers Charged* |
| **NGCSD (`_NGCSD02`)** | `.ngcsd` | *Totally Spies! Totally Party* (unpacked from BIGFILE members) |
| **Nintendo AFC** | `.afc`, `.stx`, `.ast` | GC / Switch |
| **Nintendo AST** | `.ast` | GC / Wii / Switch |
| **Nintendo JAudio BAA** | `.baa` | *Zelda: Twilight Princess* |
| **NUS3AUDIO** | `.nus3audio`, `.nus3bank` | *Super Smash Bros. Ultimate* |
| **NW4R RWAR / RWAV** | `.brwar`, `.rwar`, `.brwav`, `.rwav` | Wii |
| **NXA1** | `.nxa` | Switch |
| **NXOF / NXMS** | `.nxopus`, `.nxms` | Switch |
| **Paon KNON** | `.str`, `.asr` | *Donkey Kong: Barrel Blast* |
| **Paon KNON wave bank** | `.WVD` | *Donkey Kong: Barrel Blast* |
| **PlatinumGames RSP sound pack** | `.rsp`, `.rsd` | *MadWorld* |
| **Procyon SADL** | `.sad` | DS |
| **PROCYON-SMD** | `.smd` | *Professor Layton and the Diabolical Box*, *Pokémon Mystery Dungeon: Explorers of Sky* |
| **PROCYON-SWD** | `.swd` | *Professor Layton and the Diabolical Box*, *Inazuma Eleven*, *Pokémon Mystery Dungeon: Explorers of Sky* |
| **Punchers Impact MYSPD** | `.myspd` | *U-Sing* |
| **Red Ball music** | `.mus` | *The Ultimate Red Ball Challenge* |
| **Reflections 04SW** | `.XA` | *Driver: Parallel Lines*, *Emergency Heroes* |
| **RenderWare ADPCM stream** | `.rws` | *The Legend of Spyro: Dawn of the Dragon*, *How to Train Your Dragon* |
| **Rhythm Heaven Groove sound / tempo map (CWV / WMB)** | `.cwv`, `.wmb` | *Rhythm Heaven Groove* |
| **RIFX WAVE** | `.wax`, `.rifx` | *2 in 1 Meine Tierpension* |
| **RSEQ / CSEQ / FSEQ / SSEQ** | `.rseq`, `.cseq`, `.fseq`, `.sseq` | Wii / 3DS / Wii U / DS |
| **RWSD (single wave)** | `.rwsd`, `.brwsd` | Wii |
| **SADL** | `.sad`, `.sadl` | *Professor Layton* |
| **SDAT** | `.sdat` | DS |
| **Sega gcax sound bank** | `.dat` | Wii |
| **Shin'en OGL** | `.ogl` | *Jett Rocket* |
| **Sony SCEI sound bank** | `.hd` | *Momotarou Dentetsu 2010* |
| **SPD / SPT audio bank** | `.snd`, `.spd`, `.spt` | *Worms: Battle Islands*, *Beastly*, *Babysitting Party*, *Burger Bot* |
| **Stormfront SEG** | `.seg` | *The Spiderwick Chronicles* |
| **Sumo ABC** | `.abc` | *Sonic & SEGA All-Stars Racing* |
| **Switch Opus** | `.opus`, `.lopus`, `.opusnx` | Switch |
| **Sygnus soundstream / PSND** | `.soundstream`, `.psnd` (from PARC) | *Kirby Air Riders* |
| **Tecmo GSND** | `.gsp`, `.gsb` | Wii |
| **Terminal Reality GCA** | `.gca` | *Metal Slug Anthology* |
| **Terminal Reality SMP** | `.smp` | *Guilty Party* |
| **Treyarch NSLB WBK** | `.wbk` | *Kung Fu Panda*, *Call of Duty 3* |
| **tri-Crescendo CAF** | `.caf`, `.cfn` | *Fragile Dreams* |
| **Ubisoft BAO package** | `.pk`, `.lpk`, `.spk` | *Rayman Raving Rabbids: TV Party*, *Just Dance*, *We Dare*, *Avatar* |
| **Ubisoft GWB+GWD** | `.gwb`, `.gwd` | *Monster 4x4: World Circuit* |
| **Ubisoft Imagine ADPCM** | `.adpcm` | *Beastly* |
| **Ubisoft SB** | `.sb7` | *Red Steel* |
| **waud PCM sound** | `.wtxs`, `.wtss` | *Cooking Mama: Cook Off* |
| **WAVEBATCH** | `.wavebatch` | 3DS |
| **WayForward WAVE** | `.wave` | 3DS / DS / Wii |
| **Wrapped interleaved DSP** | `.gcm`, `.idsp`, `.sad` | *Transformers*, *Mario Strikers Charged*, *Disaster: Day of Crisis* |
| **WT** | `.wt` | *Octomania* |
| **Wwise BNK** | `.wem` | *Epic Mickey* |
| **YDSP** | `.ydsp` | GameCube |
| **Z-Axis ZSND** | `.zss`, `.zsm` | *Marvel: Ultimate Alliance 2* |

---

### Layouts, Text & Game Data

| Format | Extensions | Games |
| --- | --- | --- |
| **AiLive LiveMove LMC** | `.lmc` | Wii |
| **AINB AI node graph** | `.ainb` | *Splatoon 3*, *Tears of the Kingdom*, *Mario Kart World* |
| **AAMP** | `.aamp` | Wii U / Switch |
| **AGL parameters (AAMP)** | `.baglenv`, `.baglsky`, `.bgsdw`, other `.bagl*` | Switch titles, *Mario Kart World* |
| **Avalanche DICT** | — | *Disney's Bolt* |
| **BCLYT / BCLAN** | `.bclyt`, `.bclan` | 3DS |
| **BETP dance trace** | — | Wii |
| **BFLYT / BFLAN** | `.bflyt`, `.bflan` | Wii U / Switch |
| **BGLPBD** | `.bglpbd` | Wii U / Switch |
| **Bj engine track data** | `.btd` | *Maximum Racing* |
| **BMG** | `.bmg` | GameCube / Wii |
| **BRLYT / BRLAN** | `.brlyt`, `.brlan` | *Super Mario 3D All-Stars* |
| **BYAML / BYML** | `.byaml`, `.byml`, `.sbyml`, `.smubin` + other BotW `S`-prefixed Yaz0 containers | Wii / Wii U / Switch |
| **Capcom CSVT tables** | — | *Resident Evil Umbrella / Darkside Chronicles* |
| **CAPCOM-GML1** | `.xml.bin` | *Ghost Trick: Phantom Detective* |
| **Capcom MT Framework Wii actor index** | `.idx` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii AI decision trees** | `.thk` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii attack table** | `.atk` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii effect bank** | `.efl` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii effect link** | `.eco` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii scripted resource** | `.scra` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii serialized object** | `.xfs` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii weapon parameters** | `.wpd` | Capcom MT Framework Wii titles |
| **Capcom TRES text** | — | *Resident Evil Umbrella / Darkside Chronicles* |
| **COL / CAM** | `.col`, `.cam` | *Mercury Meltdown Revolution* |
| **EA Attrib vault** | `.vlt`, `.bin` | *EA Playground*, *Celebrity Sports Showdown*, *Le Tournoi des Célébrités*, *FIFA 10* |
| **EA LOCH** | `.loc` | *Madden NFL* |
| **FUAX** | — | *Rebel Raiders: Operation Nighthawk* |
| **GES** | `.ges` | *I Spy Spooky Mansion* |
| **GFMC** | — | *Teolsil Kirby Iyagi* |
| **GSH shader** | `.gsh` | Wii U |
| **GVX0 event script** | `.pvx` | *Power Punch* |
| **HMT** | `.Hmt` | *Rune Factory: Frontier* |
| **HVB** | `.Hvb` | *Rune Factory: Frontier* |
| **HVC** | `.Hvc` | *Rune Factory: Frontier* |
| **HVG** | `.Hvg` | *Rune Factory: Frontier* |
| **HVH** | `.Hvh` | *Rune Factory: Frontier* |
| **HVM** | `.Hvm` | *Rune Factory: Frontier* |
| **HVT** | `.hvt`, `.Hvt` | *Rune Factory: Frontier* |
| **JSystem BCSV** | `.bcsv` | *Super Mario Galaxy*, *Twilight Princess* |
| **KPMAP** | `.kpmap` | *New Super Mario Bros. Wii* |
| **KRV** | `.KRV` | *DreamWorks How to Train Your Dragon* |
| **LIT** | `.lit` | *Aqua Panic!*, *SPRay*, *Cyberbike*, other Eko Software games |
| **Localisation string table** | `.str` | *Freddi Fish: Kelp Seed Mystery* |
| **Lua 5.1 bytecode** | `.compiledLuaWii`, `.luac` | Wii |
| **MATF** | `.mat` | *Aqua Panic!*, *SPRay*, *Cyberbike*, other Eko Software games |
| **MAT (Mercury Meltdown)** | `.mat` | *Mercury Meltdown Revolution* |
| **MB2** | `.mb2` | *Aqua Panic!*, *SPRay*, *Cyberbike*, other Eko Software games |
| **Midway text tables** | `.txt` | *TNA Impact!* |
| **MIO** | `.mio` | *WarioWare: D.I.Y.*, *Made in Ore* |
| **Momotarou Dentetsu 16 message.bin** | `message.bin` | *Momotarou Dentetsu 16* |
| **MPBOARD** | `.bin`, `.csv`, `.xml` | *Super Mario Party*, *Mario Party 10* |
| **MPMESS** | `.dat` | *Mario Party 4–7* |
| **MSBT / MSBP / MSBF** | `.msbt`, `.msbp`, `.msbf` | 3DS / Wii U / Switch |
| **MTMFX** | `.mfx`, `.lfx` | 3DS |
| **MTMRL** | `.mrl` | SPICA |
| **NRR0 (module list)** | `.nrr` | Switch 2 |
| **Opoona message bank** | `msg_*.bin` | *Opoona* |
| **Opoona parameter tables** | `*_param.bin`, `encounter.bin` | *Opoona* |
| **Phantom Brave battle map** | `.mpd` | *Phantom Brave: We Meet Again* |
| **PLANETG** | `.MWG`, `.MSP` | *Bermuda Triangle: Saving the Coral*, *Rock Blast*, *Veggy World*, *Ocean Commander*, *Arcade Shooting Gallery* |
| **NAV** | `.nav` | *Mercury Meltdown Revolution* |
| **PGF** | `.pgf` | *Bermuda Triangle: Saving the Coral*, *Rock Blast*, *Veggy World*, *Ocean Commander*, *Arcade Shooting Gallery*, *Offshore Tycoon* |
| **pop'n music SEN / AEPT** | `.sen`, `.aept`, `.aepc` | *pop'n music* |
| **RC chr fighter record** | `.chr` | *Power Punch* |
| **Rebellion Asura STR** | `.str` | *PDC World Championship Darts* |
| **Rockstar GXT text** | `.gxt` | Wii |
| **RSTB / RESTBL** | `.rstb`, `.rsizetable` | *Breath of the Wild*, *Tears of the Kingdom*, *Mario Kart World* |
| **SDF** | `.sdf` | *I Spy Spooky Mansion* |
| **Sengoku Basara 3 LSP / SDL** | `.lsp`, `.sdl` | *Sengoku Basara 3* |
| **Sims vault** | `.vlt`, `.bin` | *SimAnimals*, *SimAnimals Africa*, *MySims Kingdom*, *MySims Agents* |
| **SPHC TXF** | — | *Pool Hall Pro*, *Vertigo* |
| **Spyro: Dawn of the Dragon KCS** | `.KCS` | *The Legend of Spyro: Dawn of the Dragon* |
| **Spyro DotD KRV (language)** | `.KRV` | *The Legend of Spyro: Dawn of the Dragon* |
| **STGS** | — | *MySims Agents* |
| **STR module path list** | `.str` | Wii titles |
| **STRT** | — | *Unlimited Adventure/Cruise* |
| **TDNS sound names** | `.bin` | Wii |
| **Thrillville serialised objects** | `.prk`, `.tbp`, `.atr` | *Thrillville: Off the Rails* |
| **TSCB** | `.tscb` | *Mario Kart World* |
| **Ubisoft Imagine LOC** | `.LOCV1.0` | *Beastly*, *Babysitting Party*, *Babyz* |
| **Ubisoft LOC text** | `.loc` | Wii |
| **VISLIST** | `.vis` | *Aqua Panic!*, *SPRay*, *Cyberbike*, other Eko Software games |
| **WB-L10n** | — | *So Blonde: Back to the Island* |
| **XBIN (HAL XData: YAML / FDG)** | `.bin`, `.dat`, `.archive.bin` | *Kirby and the Forgotten Land* |
| **XLNK (XLink2 effect / sound links)** | `.belnk`, `.bslnk` | Switch (*Tears of the Kingdom*-era layout checked on *Tomodachi Life: Living the Dream*) |
| **BAEV (animation events)** | `.baev` | *Tomodachi Life: Living the Dream* |
| **ASB (animation sequence binary)** | `.asb` | *Tomodachi Life: Living the Dream* |
| **Phive cloth / Havok tag files** | `.bphcl` | *Tomodachi Life: Living the Dream* |
| **VoiceText database** | `.vtdb2` | *Tomodachi Life: Living the Dream* |
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
| **DAD** | — | *Soma Bringer* |
| **Deflate / Zlib** | `78 01`, `78 9C`, `78 DA` | Many cross-platform titles |
| **Diff8 / Diff16** | `0x81`, `0x82` | DS titles |
| **FZIP** | `FZIP` | *Game & Wario* |
| **HAL LZ4** | `.cmp`, `.cmpbin` (u32 size + `04 22 4D 18`) | *Kirby and the Forgotten Land* |
| **Huffman (4-bit / 8-bit)** | `0x24`, `0x28` | DS / GBA titles |
| **INIO-LZO** | — | *Osu! Tatakae! Ouendan*, *Moero! Nekketsu Rhythm Damashii*, *Elite Beat Agents* |
| **LZ10** | `0x10` (LZSS) | GameCube / Wii / DS / GBA |
| **LZ11** | `0x11` (Extended LZSS) | DS / 3DS |
| **LZ4** | `04 22 4D 18` | Switch and many cross-platform titles |
| **LZE** | — | DS |
| **LZO / LZOvl** | Overlay trailer | DS titles (reverse LZO overlays) |
| **LZX** | `LZX` | DS |
| **MVDK** | `MVDK` | DS |
| **Neko LZSS** | `.pc`, `.GCN` | *Cocoto Kart Racer 2*, *Cocoto Magic Circus* |
| **Next Level .zlib / NLOC** | `.zlib`, `.loc` | *Mario Strikers Charged* |
| **PSDK** | `PSDK` / `AT4PX` | DS |
| **PuCrunch** | `0x50 0x75` (`Pu`) | DS titles |
| **QuickLZ** | `QLZ` | Various third-party titles |
| **RLE** | `0x30` | DS titles |
| **RNC1 / RNC2** | `RNC\1`, `RNC\2` | Rob Northen ProPack titles |
| **SSZL** | `SSZL` | Wii |
| **Tornado Outbreak BGW** | `.bgw` | *Tornado Outbreak* |
| **Ubisoft Imagine LZSS** | `.wiz`, `.wsp`, `.wsn`, `.tan`, `.wan` | *Babysitting Party*, *Babyz*, *Beastly* |
| **VLX** | `VLX` | DS |
| **WIS block-LZSS** | `.wis` | *Cabela's* Wii titles |
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
| **Flash SWF Files** | `.swf` | **`ffdec`** (bundled; `--with-ffdec`) | Decompile ActionScript 1/2/3 and extract shapes, images, sounds, fonts, and binary assets via JPEXS Free Flash Decompiler |
| **Microsoft Cabinet (MSCF)** | `.cab`, `.bin`, `.dat` (matched by `MSCF` magic) | **`7z`** / **`7zz`** / **`7za`** (`--with-7z`) | CAB packs unpacked in place, e.g. *Tales of Symphonia: Dawn of the New World* item/character `.bin` files |
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

---

## Documentation

- [Commands](docs/COMMANDS.md) — all tools and wszst subcommands
- [Workflows](docs/WORKFLOWS.md) — recursive extract, edit, repack
- [Formats](docs/FORMATS.md) — technical format reference
- [Wiimms SZS Tools](https://szs.wiimm.de/) — original documentation

---

## License & Credits

Based on **Wiimms SZS Tools** by Dirk Clemens (*Wiimm*), licensed under GPL v2 (`project/gpl-2.0.txt`). See [CREDITS.md](CREDITS.md) for attributions.

