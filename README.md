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
| **`gamedata.fat` + `gamedata`** | `.bink` | *Battle of Giants: Dinosaurs Strike* |
| **ABE BigFile** | `.bf` | *Rabbids Go Home* |
| **AFS** | `.afs` | *Dragon Ball Z: Budokai Tenkaichi 3* |
| **Agatha Christie `.wii` bundle** | `.wii` | *Agatha Christie: And Then There Were None* |
| **AGI** | `.pak` | *Skylanders: Swap Force* |
| **ALAR** | `.aar`, `.alar` | *Jump Super Stars*, *Jump Ultimate Stars* |
| **And-Kensaku** | `.rz` | *And-Kensaku* |
| **Angry Birds Trilogy `.MTP` (MULA) archive** | `.MTP` | Wii |
| **APAK** | `.apak` | Wii U / Switch |
| **ARC / U8** | `.arc`, `.szs` | Wii / GameCube NintendoWare & EAD |
| **ARC0** | `.fa` | *Yo-Kai Watch* |
| **ARCV** | `.arc` | Namco / Tose Wii titles |
| **Arika (INFO.DAT/GAME.DAT)** | — | DS |
| **Arika Archive** | `INFO.DAT`, `GAME.DAT`, `.arika` | Arika DS / DSi / Wii titles |
| **Artefacts MAP** | `.map`, `.gam` | *Diabolik: The Original Sin*, *Boot Camp Academy*, *Jillian Michaels Fitness Ultimatum*, *Dodge Racing* |
| **Asobo BigFile** | `.drv` | *Ratatouille* |
| **AST** | `.ast` | *I Spy Spooky Mansion* |
| **AST (Proteus `SDASSETF`)** | `.ast` | *I Spy Spooky Mansion*, *Goosebumps HorrorLand*, *Spore Hero* |
| **AT7** | `.at7` | Wii / PS2 |
| **ATB** | `.atb` | *Mario Party 4-8* |
| **ATLUS-NDX** | `.ndx`, `.idx`, `.bin` | *Radiant Historia*, *Shin Megami Tensei: Strange Journey*, *Devil Survivor*, *Etrian Odyssey* |
| **Atlus PAK** | `.pak` | *Trauma Center: Second Opinion*, *Trauma Center: New Blood*, *Trauma Team* |
| **Atomic Planet PUB** | `.pub` | *AMF Bowling: Pinbusters!* |
| **Avalanche THB/TBB** | `.thb` + `.tbb` | *Cars 2* |
| **Batman `.vtb` buffers** | `.vtb` | *Batman: The Brave and the Bold*, A, A* |
| **Battle of the Bands BAG** | `.bag` | *Battle of the Bands* |
| **BCGRP** | `.bcgrp` | 3DS NintendoWare titles |
| **BCPAK** | `.bcpak` | *Top Shot Arcade*, *Our House - Party!* |
| **BEA** | `.bea`, `.nx.bea` | *WarioWare*, *Mario Party* |
| **BEA (Switch 2 "SCNE")** | — | *Switch 2 Welcome Tour* |
| **BFGRP** | `.bfgrp` | Wii U / Switch NintendoWare titles |
| **BFMA** | `.bfma` | Wii U titles with a built-in manual |
| **BFSHA / BNSH** | `.bfsha`, `.bnsh` | Wii U / Switch |
| **BG4** | `.bg4` | *Mario & Luigi* (3DS) |
| **BGIB** | `.gcs`, `.gcm`, `.pak` (matched by `BGIB` magic) | *Pheasants Forever*, *Tournament of Legends*, *Nickelodeon Dora the Explorer* / *Go Diego Go!* / *Ni Hao, Kai-Lan*, *Rudolph the Red-Nosed Reindeer*, *The Secret Saturdays*, *Pirates PlundArrr* |
| **BGIB level / sound pack** | `.gcs`, `.gcm`, `.dsp` | *Pheasants Forever*, *Tournament of Legends*, *Rudolph* |
| **BIGF** | `.big` | Electronic Arts Wii titles |
| **BIGFILE TOC/DAT (DKAP)** | `BIGFILEn.TOC` + `.DAT` | *Totally Spies! Totally Party* |
| **Bit.Trip Complete `.dar`** | `.dar` | Wii |
| **Bj engine** | `.tx1`/`.tx2`, `.mtm`, `.bsi`, `.bsm` | *Super Karts* |
| **Bleach `.sms` (`SMST`) containers** | `.sms` | *Bleach: Shattered Blade* |
| **Blitz REV** | `.rev`, `.gcp` | *The House of the Dead: Overkill*, *iCarly* |
| **Blue Castle BIG** | `.big`, `.dspi` | *The Bigs*, *The Bigs 2* |
| **Blue Tongue TRB** | `.trb` | *de Blob 2* |
| **BNS Archive** | `.bns` | *Samurai Warriors 3* |
| **BombShell data pack** | `.xwi`, `.xdx9` | *Bee Movie Game* |
| **Brawl fighter pack (`.pcs`)** | `.pcs` | *Super Smash Bros. Brawl*, */Fit* |
| **CA01 / SA01** | `.ca01`, `.sa01` | 3DS / Wii U |
| **Cabela's `.crs` (CPF) package** | `.crs`, `.wit`, `.wis` | Wii |
| **Call of Duty `.ff` fastfile (Wii)** | `.ff`, `.shock`, `.vision`, `.atr` | Wii |
| **Capcom @LPK / @FPK / ANA@** | `.brres`, `.bcmdl`, `.bsrt`, `.csl` | *Dead Rising: Chop Till You Drop* |
| **Capcom `.bgz` background packs** | `.bgz`, `.h4m` | *Resident Evil Archives: Resident Evil* |
| **Capcom MT Framework ARC** | `.arc` | *Sengoku Basara 3* |
| **CARC resource pack (`.carc`)** | `.carc`, `.arc`, `.csv` | *Haneru no Tobira Wii - Giri Girissu* |
| **CCF** | `.ccf` | Wii / Switch |
| **CDGaCube CAR** | `.car` | *Birthday Party Bash* |
| **Champion Jockey UnitedData `DATA.UNH` + `DATA.UNB`** | `.N` | Wii |
| **CHDp / C3Dp** | `.chd`, `.c3d` + `.cbd` | *Donkey Konga*, *Donkey Konga 2*, *Donkey Konga 3* |
| **CHNK tree** | `.scene`, `.object`, `.phn`, `.var`, `.cha` | *Paws & Claws: Pet Resort* |
| **CING-WPF** | `.wpf`, `.tbl` | *Hotel Dusk: Room 215*, *Wish Room* |
| **Climax ARC** | `.arc` | *Silent Hill: Shattered Memories* |
| **CNUT** | `.cnut` | *Wii Party* |
| **COD PAK0** | `.pak` | *Call of Duty: Black Ops*, *MW3* |
| **Coresoft JPack (.pak) + NWTX (.wtx)** | `.JAW`, `.JMW` | *Fantasy Aquarium World* |
| **Coresoft JPack + NWTX** | `.pak` | *Fantasy Aquarium World* |
| **CPK** | `.cpk` | *Star Fox Zero* |
| **CRAM** | `.arc`, `.cram` | *Project X Zone* |
| **CRI `.awb` streaming archive (CPK with ITOC only)** | `.awb`, `.adx`, `.bin`, `.hca` | *Power Rangers Samurai* |
| **CRI ROFS image (`.cvm`)** | `.cvm` | *Sonic Riders: Shooting Star Story* |
| **Crystal Dynamics BigFile (`.wii-w`)** | `.raw` | *Tomb Raider: Anniversary* |
| **CS_DCT** | `.dct` | *Chicken Shoot* |
| **Cursed Mountain `.dat`, `.timg`** | `.dat`, `.timg`, `.gr2`, `.grz` | Wii |
| **CyberConnect2 fpack** | `.fpk` | *Naruto Shippuden: Clash of Ninja Revolution 3*, *Tatsunoko vs. Capcom* |
| **DARC / BCMA** | `.darc`, `.bcma`, `.arc` | 3DS |
| **DC2 DCX / DCT** | `.dcx`, `.dct` | *Jakers! Kart Racing* |
| **Deadly Creatures `.wii.database`** | — | Wii |
| **DIG** | `.dig` | *Bomberman Land* |
| **DKZF** | `.tpl.dkz` | *Donkey Konga*, *Donkey Konga 2*, *3* |
| **Dog Island `.mpc` table pack** | `.mpc` | *Artlist Collection: The Dog Island* |
| **Dragon Quest Monster Battle Road Victory `.RSPA`** | `.RSPA`, `.brres`, `.rsm`, `.rst` | Wii |
| **DSi modcrypt SRL** | `.nds`, `.srl`, `.dsi` | DSi-enhanced and DSiWare titles |
| **DSi TAD** | `.tad` | the DSi's WAD, TwlSDK |
| **DSiWare export** | `.bin` (SD card) | DSiWare titles |
| **DTLS** | `dt00`, `ls00`, `.ls` | *Super Smash Bros. 4* |
| **EA DBPF package** | `.package`, `.mstex` | *MySims*, *MySims Kingdom*, *MySims Agents* |
| **EFFN** | `.eff`, `.effn` | *Super Smash Bros. for 3DS / Wii U*, *Super Smash Bros. Ultimate* |
| **ENV0 collision archive** | — | *Paws & Claws - Pet Resort* |
| **Epic Mickey 2 `.chf` pack** | `.chf` | *Disney Epic Mickey 2* |
| **Eurocom `Filelist` pack** | — | *GoldenEye 007* |
| **F9RES** | `.res` | GameCube titles |
| **Family GameShow `.piz` archive** | `.piz` | Wii |
| **FAST (.fst)** | `.fst` | *Crazy Machines* |
| **FBND bundle** | `data.bin` | *Sega Bass Fishing*, *Uchi Tsuri! Sega Bass Fishing* |
| **FBTI** | `.Mod`, `.Mot` | *Rune Factory: Frontier* |
| **FCAT** | `.dat` | *Winning Post 7 / World* |
| **Flash SWF media (`.swf`)** | `.swf`, `.wav`, `.flv` | *Spaghetti Western Shooter* |
| **FMOD FSB** | `.fsb` | FSB3 / FSB4 / FSB5, Wii |
| **Freestyle Games `FSAR` archive (`.FAR`)** | `.FAR`, `.gr2`, `.msb`, `.img` | Wii |
| **FSYS** | `.fsys` | GameCube / Wii |
| **G.I. Joe level pack (`.lvl_NNN`)** | `.lvl_NNN`, `.lvlrmp`, `.mmpa` | *G.I. Joe: The Rise of Cobra* |
| **Gamebryo NIF stream pack (`.gda`)** | `.gda` | *Sid Meier's Pirates!* |
| **Ganbarion ALAR** | `.aar` | *Pandora's Tower* |
| **Ganbarion ALAR (BE)** | `.aar` | *Pandora's Tower* |
| **GAR / ZAR** | `.zar`, `.gar` | *OoT3D*, *MM3D*, *LM3DS* |
| **GEEK archive (`.csa`)** | `.csa` | *Barbie Horse Adventures: Riding Camp* |
| **GFA** | `.gfa` | Wii / 3DS / Wii U |
| **GFLX** | `.gflxpack` | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFMPACK** | `.gfpack` | *Pokémon X/Y/ORAS* |
| **GFPAK** | `.gfpak` | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFPKG** | `.gfpkg`, `.bin`, `.pak` | *Pokémon X / Y*, *Omega Ruby / Alpha Sapphire*, *Sun / Moon* |
| **GGXArchiver** | — | *Radirgy Noa* |
| **GGXArchiver (`GGXArchiver1.00`)** | `.ini`, `.PVR`, `.png` | *Radirgy Noa* |
| **GLOB resource pack (`.glob` + `.name.glob`)** | `.glob`, `.gaftx`, `.gab`, `.gcb` | *Green Lantern: Rise of the Manhunters* |
| **Goliath GS Package** | `.pkz` | *Skylanders: SuperChargers Racing*, *The Amazing Spider-Man* |
| **Grip RES** | `.res` | *Sesame Street: Elmo's Musical Monsterpiece* |
| **h.a.n.d. FBC** | `.fbc` | *Oyako de Asobo: Miffy no Omochabako* |
| **h.a.n.d. FBC bundle** | `.fbc`, `.brres`, `.tpl`, `.brasd`, `.ssb` | *Miffy no Omochabako* |
| **Harmonix Ark** | `.hdr` + `_N.ark` | *AC/DC Live: Rock Band Track Pack*, *The Beatles: Rock Band* |
| **Hash `PAK\0` (`!ZLS`)** | — | *Kid Adventures: Sky Captain* |
| **Hash PAK (`!ZLS`)** | `PAK\0` magic | *Kid Adventures: Sky Captain* |
| **Havok HKX (classic packfile)** | `.hkx`, `.HKX` | *Tenchu: Shadow Assassins* |
| **HBDF** | `.hbdf`, `.hsdf` | *Mario Party DS* |
| **HDVOICE (Tenchu: Shadow Assassins)** | `.hd` | *Tenchu: Shadow Assassins* |
| **Heatseeker `.dt3`** | `.dt3` | *Heatseeker* |
| **Heavy Iron HO** | `.ho` | *WALL-E*, *Up* |
| **HOG** | `.hog`, `.gcb` | *Ultimate Band*, *Tiger Woods PGA Tour* |
| **Home Button Menu `.hbm`** | `.hbm` | Wii |
| **Humongous Resource.rez** | `.rez` | *Backyard Football '10* |
| **HVP** | `.hvp` | *Aladdin Magic Racer*, *X Factor*, *Knockout Party*, *Obscure: The Aftermath* |
| **Hyrule Warriors** | `.idx`, `.bin` | 3DS |
| **IARC (`.iaa`)** | `.iaa`, `.wax`, `.ogg` | *Benjamin Blumchen: Törööö im Zoo* |
| **IEAR** | — | DS |
| **iPck index pack** | `.bin` | Wii |
| **IPK** | `.ipk` | *Just Dance*, *Rayman Origins/Legends* |
| **IQIPACK** | `.pak` | NVIDIA Shield iQiyi titles |
| **JARC** | `.jarc` | DS |
| **Junction Point PAK** | `.pak`, `.nif_wii`, `.hkx_wii`, `.bin` | *Epic Mickey* |
| **JUPITER-PCK** | `.pck` | *The World Ends with You*, *Kingdom Hearts: Re:coded* |
| **Konami `.afs` container** | `.afs`, `.bin` | *Pro Evolution Soccer 2012* |
| **KP0 package** | `.pkt` | *Power Punch* |
| **KPBIN** | `.kpbin` | *New Super Mario Bros. Wii* |
| **kRAW music** | `.kRAW` | *Geometry Wars: Galaxies* |
| **Krome RKV2** | `.mus`, `.mdl`, `.mdg`, `.tex` | *Star Wars: The Force Unleashed*, *Happy Feet Two* |
| **Kylotonn FVRT archive** | `.vid`, `.snd`, `.anm`, `.mdl`, `.txc` | *My Body Coach*, *My Body Coach 2*, *Cocoto Festival* |
| **L5-PAC** | `.pac`, `.dat` | *Dragon Quest IX: Sentinels of the Starry Skies* |
| **Line Attack Heroes `.alb`** | `.alb` | Wii |
| **Line Rider ACP (`.acp`)** | `.acp` | *Line Rider Freestyle* |
| **Lost in Blue: Shipwrecked `.bin`, `.zms`** | `.bin`, `.zms`, `.TSB` | Wii |
| **Lost in Blue: Shipwrecked archive** | `.bin`, `.zms` | *Lost in Blue: Shipwrecked* |
| **LSPK** | `.pk`, `.pkh`, `.lspk` | *The Last Story* |
| **Luxor PPK (`.ppk`)** | `.ppk`, `.B1T` | *Luxor: Pharaoh's Challenge* |
| **LZBIN** | `.bin`, `.lzbin` | *Mario Party DS* |
| **Mama `.pkb` resource packs** | `.pkb` | *Cooking Mama / Babysitting Mama* |
| **Mario Sports Mix `stage/*.bin`** | — | *Mario Sports Mix* |
| **Marvelous `pBin` pack (`.dpt` …)** | `.dpt` | *Harvest Moon: Animal Parade* |
| **Marvelous pBin / FIPAFTEX** | — | *Harvest Moon: Animal Parade* |
| **MDR** | `.mdr` | *Dance Dance Revolution Mario Mix* |
| **Mercury Meltdown PAQ (`.paq`)** | `.paq`, `.arc` | *Mercury Meltdown Revolution* |
| **Midway `SEC ` containers** | `.bin` | *Mortal Kombat: Armageddon* |
| **Midway WAD** | `.WAD` | Wii |
| **MKGPDX PAC** | `.pac`, `.mkgpdx` | *Mario Kart Arcade GP DX* |
| **Moki Moki `add` pack** | `.pacz`, `.tgz`, `.tpl` | Wii |
| **Momotarou Dentetsu `cmps` archive** | — | *Momotarou Dentetsu 16* |
| **Monster Jam BigFile (`MJ.bft`/`.bff`/`.bfd`)** | `.bff`, `.bfd`, `.mti`, `.bin` | *Monster Jam: Path of Destruction* |
| **MPBIN** | `.bin` | GameCube / Wii |
| **MPH SNDFILE archive** | `.arc` | *Metroid Prime Hunters* (DS) |
| **MPR PACK** | `.pak` | *Metroid Prime Remastered* |
| **MRQZ** | `.pak` | *My Fitness Coach* |
| **MSR** | `.pkg`, `.bin` | *Metroid: Samus Returns* |
| **MTXT** | `.mtxt` | Switch titles |
| **Namco Tales LZSS (`.slz`)** | `.slz` | *Tales of Graces* |
| **NARC** | `.narc` | DS / DSi |
| **Natsume BIN** | `.bin` | *Harvest Moon* |
| **NCCARC** | `.nccarc` | DS titles |
| **NDS / SRL / DSI** | `.nds`, `.srl`, `.dsi` | All DS / DSi games |
| **Neighborhood Games bundle (`.dbreg`)** | `.dbreg` | *Neighborhood Games* |
| **Neversoft GH PAK/IMG** | `.pak.ngc`, `.img.ngc` | *Guitar Hero* |
| **NIBM audio** | `.aud` | *CSI: Hard Evidence*, *Destroy All Humans* |
| **NIF (Gamebryo 20.6, Wii)** | `.nif` | *Pocoyo Racing* |
| **Nippon Ichi WIIIDARC** | `.dat` | *Phantom Brave: We Meet Again* |
| **NLCM index** | `.dat` | *Rune Factory Frontier* |
| **NLG DICT** | `.dict`, `.data` | *Federation Force*, *Luigi's Mansion 2/3*, *Strikers* |
| **NUP** | `.nup` | Decrypted Switch / Switch 2 dumps (*Kirby and the Forgotten Land – Nintendo Switch 2 Edition*, *Kirby Air Riders*, *The Legend of Zelda: Ocarina of Time* (Switch 2)) |
| **NSO** (Switch executable) | `.nso`, `main` | Switch / Switch 2 titles, including *The Legend of Zelda: Ocarina of Time* (Switch 2) whose `main` uses ZBIC segments |
| **NXARC** | `.nxarc` | Switch titles |
| **Okami DAT / T** | `.dat`, `.t` | *Okami* |
| **OMOM quest archive** | — | *Monster Hunter 3 tri-* |
| **Opoona SL01 / OPKG** | `.btl`, `.bin`, `.dat` | *Opoona* |
| **PACK v2** | `.pak` | *Ski and Shoot* |
| **PAC (Nd Cube)** | `.bin` | *Mario Party 10*, *Animal Crossing: amiibo Festival* |
| **PAC / MRG** | `.pac`, `.mrg` | HAL Laboratory / Game Arts Wii titles |
| **PAK\3 archive** | `.pak` | *International Athletics* |
| **PAKb** | `.pak` | *Secret Files: Tunguska*, *Secret Files 2: Puritas Cordis* |
| **PCK2 / PLZ** | `.plz`, `.pck2` | *Professor Layton and the Diabolical Box* |
| **PDA BRA** | `.bra`, `.3df`, `.bin`, `.spr`, `.prt` | *Gummy Bears: Magical Medallion / MiniGolf*, *Pool Hall Pro*, *Build'n Race* |
| **Pet Vet `.dat` packages** | `.dat`, `.gr2`, `.glb` | *Pet Vet: Marine Patrol* |
| **Pet Vet packages DAT** | `.dat` | *Pet Vet: Marine Patrol* |
| **phx PARC / PDAT** | `assets.cat` + `assets_*.dat` | *Kirby Air Riders* |
| **Pikmin 1 `.anm`** | `.anm`, `.dca`, `.dck` | *Pikmin* |
| **Pikmin 1 `.bun`** | `.bun`, `.anm`, `.pcr` | *Pikmin* |
| **Pikmin 1 `pikiseq.arc`** | `.jam` | *Pikmin* |
| **Pikmin ARC/DIR** | `.arc` + `.dir` | *Pikmin* |
| **Pipeworks bundle** | `.bdg` | *Merv Griffin's Crosswords* |
| **PK03 hash package** | `PK03` magic | *Deer Drive Legends* |
| **PKG (Barking Lizards)** | `.pkg` | *Nickelodeon: The Naked Brothers Band - The Video Game* |
| **PKG / GPKG / GPAK** | `.pkg`, `.pak`, `.gpak` | *Bonsai Barber*, *World of Goo* |
| **PKI** | `.PKI` | *Bermuda Triangle: Saving the Coral*, *Ocean Commander* |
| **PKZ** | `.pkz` | *Bayonetta*, *Astral Chain* |
| **PlatinumGames DAT** | `.dat` | *Star Fox Zero*, *MadWorld* |
| **Pokémon Battle Revolution big-endian `CRAN` NARC** | — | Wii |
| **PRC** | `.prc`, `.param` | *Super Smash Bros. for 3DS / Wii U*, *Super Smash Bros. Ultimate* |
| **PTD** | `.ptd`, `.pdt` | *Mario Party 4-8* |
| **Purr Pals RSC0 (`.rsc`)** | `.rsc` | *Purr Pals* |
| **Puzzler Collection `.txz` / `.lpz`** | `.txz`, `.lpz` | Wii |
| **PVOL** | `.pvol`, `.vol` | *Godzilla: Unleashed*, *Godzilla: Save the Earth* |
| **Race-O-Rama `.gcn.res` resources** | — | *Cars: Race-O-Rama* |
| **Radical RCF** | `.rcf` | *Crash of the Titans*, *Simpsons Hit & Run* |
| **Radical RCF (ATG)** | `.p3d`, `.blua`, `.rsd`, `.bik` | *file-offset* |
| **RARC** | `.rarc`, `.arc` | GameCube / Wii |
| **RCB resource bundle** | `.rcb` | *Bass Fishing Wii* |
| **Rebellion Asura `.wii`, `.asrBE`, `.guiBE`** | `.wii`, `.asrBE`, `.guiBE` | Wii |
| **Rebellion Asura resource archive** | `.wii`, `.asrBE`, `.guiBE` | Rebellion Asura Wii titles |
| **Rebellion Asura Wii ARC** | `.tNN`, `.t1` | *PDC World Championship Darts* |
| **Reflections CHNK (`.d4c`)** | `.d4c` | Wii |
| **resource.dat + resource.tbl (LZ11)** | `.dat`, `.tbl` | *Deer Drive* |
| **Retro PAK / CSMP** | `.csmp`, `.strm`, `.ras`, `.strg` | *Metroid Prime 3*, *Donkey Kong Country Returns* |
| **RFL_Res** | `RFL_Res.dat`, `.dat` | Wii / 3DS / Wii U |
| **RKET** | `.rck`, `.spa` | *Aqua Panic!*, *SPRay*, *Cyberbike*, other Eko Software games |
| **RKV** | `.rkv` | *Happy Feet Two*, *Legend of Spyro*, *Owls of Ga'Hoole* |
| **RKV (RK33)** | `.rkv` | *Happy Feet Two*, *Legend of Spyro*, *Owls of Ga'Hoole* |
| **RMHG container** | `.rsl` | *No More Heroes*, *No More Heroes 2* |
| **Roogoo SCF (`.scf`)** | `.scf` | *Roogoo: Twisted Towers* |
| **RPAK** | `.rpak`, `.pak` | *Donkey Kong Country Returns* |
| **RSO** | `.rso` | *Skylanders: SuperChargers Racing* |
| **RST / TOC** | `.rst`, `.toc` | *Excite Truck*, *Excitebots* |
| **Rubik's Puzzle World `.ma` archive** | `.ma` | *Rubik's Puzzle World* |
| **RWS / MTD** | `.RWS`, `.mtd` | *DreamWorks How to Train Your Dragon* |
| **RZPK** | `.rzpk` | *Mario Party* (3DS) |
| **SARC** | `.sarc`, `.szs` | Wii U / Switch / 3DS |
| **Scribblenauts index.bin / .p** | `.p` | *Scribblenauts Unlimited* |
| **SEC** | `.sec` | *Octomania* |
| **SEDB** | `.rps`, `.txb` | *Dragon Quest X* (Wii) |
| **SEDB (.rps / txb / shd / lyb / PHB)** | `.txb`, `.shd`, `.phb`, `.mtb` | *Dragon Quest X* |
| **Sega `gos1` pack (`.gos`)** | `.gos`, `.gms` | *Ghost Squad* |
| **Sega ABRS pack (`.abr`)** | `.abr` | *Sakura Wars: So Long, My Love*, */E/* |
| **Sega ARCB / AVLZ** | `.arc` | *Super Monkey Ball: Banana Blitz* |
| **Sega URES/AMPP/GDEN/SASN** | `.urs`, `.amf`, `.gdr`, `.gdn`, `.arm` | Wii |
| **SFZDAT** | `.dat` | *Star Fox Zero* |
| **Shade BIN / BLN** | — | *Inazuma Eleven Strikers* |
| **SHARC / SHARCFB** | `.sharc`, `.sharcfb`, `.sharcb` | Wii U / Switch / Switch 2 |
| **Shark 3D `.s3d_pack` packs** | `.s3d_pack`, `.stk` | Wii |
| **Shark 3D `.stk` stacks** | `.stk` | Wii |
| **Shrek Forever After APK (APKF)** | `.apk` | *Shrek Forever After* |
| **Simple Series Vol. 3 `.pk3`** | `.pk3`, `.brres`, `.tpl`, `.bin` | Wii |
| **Simple Wii Series DPK** | `.dpk` | *Simple Wii Series Vol. 1-4 and 6*, *Final Fantasy* |
| **SIR0** | `.sir0` | DS / 3DS |
| **DATA-ARC** | `.arc` | *Super Smash Bros. Ultimate* |
| **Sonic `.gncp`** | `.gncp`, `.TXL` | *Sonic and the Secret Rings* |
| **Sonic `_Ex` packs** | `.gvr`, `.bin` | *Sonic and the Secret Rings* |
| **Sonic GVRs `.GVRs`** | `.GVRs` | *Sonic and the Secret Rings* |
| **Sonic Team `.one`** | `.one` | *Sonic and the Black Knight* |
| **Sonic Team ONE `.onz` / `.one`** | `.onz`, `.one` | *Sonic Unleashed* |
| **Spectrobes `.LXMF` bundle** | `.LXMF` | *Spectrobes: Origins* |
| **Spectrobes LXBD (`.lxbd`)** | `.lxbd`, `.txt` | Wii |
| **Sting `GAME.PAH` / `GAME.PAC`** | `.IGB`, `.PIM`, `.PIL`, `.TGA` | *Dokapon Kingdom* |
| **STME** | `.stm` | *Need for Speed: Hot Pursuit* |
| **Storybook ONE** | `.one` | *Sonic and the Secret Rings*, *Black Knight* |
| **STPK** | `.srd`, `.stpk` | None verified (*Jump Stars* use ALAR) |
| **Stunt Cars `.epk` pack** | `.epk`, `.NDS` | Wii |
| **Sumo STZ** | `.stz` | *Sonic & Sega All-Stars Racing* |
| **Sumo WAS** | `.was` | *Sonic & Sega All-Stars Racing*, */* |
| **Super PickUps MIX (`.mix`)** | `.mix` | *Super PickUps* |
| **T4-RES (Tenchu: Shadow Assassins)** | `.b` | *Tenchu: Shadow Assassins* |
| **Tecmo MAD/LAD/EFE/CLF pack** | `.mad`, `.lad`, `.efe`, `.clf` | *Rygar: The Battle of Argus* |
| **Telltale TTARCH v3-6** | `.ttarch` | *Sam & Max: Beyond Time and Space* |
| **Tenchu 4 offset packs (`.earc` / `.ires` / `.ui`)** | `.earc`, `.ires`, `.ui` | *Tenchu 4* |
| **Terminal Reality POD** | `.pod` | *Nickelodeon Dance* |
| **THOR** | `.wii` | *Phineas and Ferb: Quest for Cool Stuff* |
| **THQ Australia pack** | `.pak`, `.rad` | *Avatar: The Last Airbender* |
| **TMPK** | `.pack`, `.tmpk` | *The Legend of Zelda: Twilight Princess HD* |
| **Torus Games hunkfile** | `.hnk` | *Barbie & Her Sisters: Puppy Rescue* |
| **Toshi TSFB** | `.ttl`, `.tkl` (`.trb` models and terrain) | *Nickelodeon Barnyard* |
| **Twinkle Queen `.cg` (DHGC) image archive** | `.cg` | Wii |
| **Ubisoft ABE BigFile** | `.BF`, `.BBF` | *Rabbids Party: Time Travel*, *Just Dance 2*, *Rabbids Go Home* |
| **Ubisoft BIG bigfile v42 (`.bf`)** | `.bf` | *Prince of Persia: The Forgotten Sands* |
| **Ubisoft UbiArt IPK (`.ipk`)** | `.ipk`, `.ckd`, `.msm`, `.webm` | *Just Dance 2015* |
| **Unreal Engine 1/2 package (`.uml`/`.upk`/`.utx`/…)** | `.uml`, `.upk`, `.utx` | *Surf's Up* |
| **vaps page sets** | `.wii` | *Disney-Pixar Toy Story Mania!*, *Disney Princess: Enchanted Journey*, *George of the Jungle* |
| **VLIB variable library (`.lib`)** | `.lib`, `.var`, `.tbd`, `.rgd` | *Meet the Robinsons* |
| **War Drum CLMP** | `.dff` (models, untextured) | *Manhunt 2* |
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
| **Where's Wally `.GWVideo`** | `.GWVideo`, `.ppm` | Wii |
| **Wii resource ARC** | `wii\0` magic | *Petanque Master*, *Petanque Pro* |
| **Winning Post DATL (`.datl`)** | `.datl` | *Winning Post World* |
| **WSI** | `.wsi` | *Alone in the Dark*, *Aladdin Magic Racer* |
| **WTA / WTP** | `.wta` + `.wtp` | *Star Fox Zero* |
| **WUD / WUX** | `.wud`, `.wux` | All Wii U games |
| **X-Men: Destiny `.zzz` chunk streams** | `.zzz`, `.dds`, `.gfx`, `.bnk` | *X-Men: Destiny* |
| **xar (`.xar`)** | `.xar`, `.brcha`, `.N` | Wii |
| **XMSG** | `.bin` | *Wii Party* |
| **XPCK** | `.xc`, `.xpck` | *Inazuma Eleven*, *Professor Layton*, *Yo-kai Watch* |
| **XPK** | `.pak` | *Angry Birds Star Wars* |
| **Yard Sale Hidden Treasures `.fez` data pack** | `.fez`, `.gvf` | *Yard Sale Hidden Treasures: Sunnyville* |
| **ZAP archive** | `.zap` | *Thrillville: Off the Rails* |
| **ZDAT** | `.zdat` | *Animal Crossing: Pocket Camp* |
| **ZIP / TSZIP / SZIP / ZGS** | `.szip`, `.zgs` | *Super Swing Golf*, *Pangya! Golf with Style* |
| **ZLARC** | `.zlarc` | *NES Remix*, *NES Remix 2*, *NES Remix Pack* |
| **ZTAB** | `.ztab`, `.tab` | *Mario Golf: Toadstool Tour*, *Mario Power Tennis* |
| **Manhunt 2 MH2Z** | `.txd`, `.bsp`, `.dff`, `.ifp` (zlib wrapped) | *Manhunt 2* |

`Byte-Exact Roundtrip` = build → extract → rebuild reproduces the archive's bytes
identically, so the writer's canonical layout is a fixed point of its own reader.
Exercised by `t_container_roundtrip()` in `tests/regress.sh`.

---

### 3D Models & Geometry

| Format | Extensions | Output | Games |
| --- | --- | --- | --- |
| **`MOEG` geometry** | — | — | DS |
| **ADJB** | `.adjb` | **TXT manifest** | *Super Smash Bros. Ultimate* |
| **Alchemy IGZ** | `.igz` | **GLB** | *Madagascar: Escape 2 Africa* |
| **Alchemy IGZ (Madagascar 2)** | — | — | *Madagascar: Escape 2 Africa* |
| **Athletics-engine ANM** | `.ANM` | *(text dump)* | *International Athletics* |
| **Batman B&B `.fmsb` level objects** | `.fmsb` | — | *Batman: The Brave and the Bold* |
| **BCAB collision chunk list** | — | — | *Paws & Claws - Pet Resort* |
| **BCH** | `.bch` | **GLB** | 3DS |
| **BCMDL / CGFX** | `.bcmdl`, `.cgfx` | **GLB** | 3DS |
| **BCRES** | `.bcres` | **GLB** | 3DS |
| **BFRES** | `.bfres` | **GLB** | Wii U / Switch |
| **BFSKA (standalone FSKA)** | `.bfska` | **GLB** (clip bound to the first `.bfres` / `.bfres.zs` in the same folder) | Wii U / Switch (*BOTW*, *TOTK*); bare-file layout unverified |
| **BMD** | `.bmd`, `.bdhc` | **GLB** | DS |
| **BNFM** | `.bnfm` | **GLB** | *Mario Party 10*, *Animal Crossing: amiibo Festival* |
| **Call of Duty WaW `.xgm` / `.sgm` geometry stream** | `.xgm`, `.sgm`, `.ff` | — | *Call of Duty: World at War* |
| **CAMELOT-MDLR** | `.mdlr`, `.col` | *(text dump)* | *Golden Sun: Dark Dawn* |
| **CAPCOM-MODS** | `.mods` | *(text dump)* | *Ghost Trick: Phantom Detective* |
| **Capcom MT Framework Wii animation SRT table** | `.astl` | *(text dump)* | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii collision table** | `.col` | *(text dump)* | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii effect sprite animation** | `.ean` | *(text dump)* | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii model** | `.rmd` | **GLB** | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii motion bank** | `.lmt` | *(text dump)* | Capcom MT Framework Wii titles |
| **Cars GCT / GCG** | `.arc`, `.gct`, `.gcg`, `.gcm` | — | *Disney-Pixar Cars* |
| **CLMP model** | `.gpl`, `.tpl`, `.obj`, `.txd` | — | *Manhunt 2*, *Manhunt 2 world and model geometry* |
| **CSB** | `.csb` | **GLB** | *Paper Mario: The Thousand-Year Door*, *The Origami King*, *Color Splash* |
| **CTB** | `.ctb` | *(text dump)* | *Paper Mario: The Thousand-Year Door*, *The Origami King*, *Color Splash* |
| **DC2 model** | `.dcm` | **GLB** | *Speed 2* |
| **DC2 world** | `.dcw` | **GLB** | *Speed 2* |
| **FEDMODEL** | `.fedmodel` | **GLB** | *Metroid Prime: Federation Force*, *Luigi's Mansion: Dark Moon*, *Luigi's Mansion 3* |
| **FEDSKEL** | `.fedskel` | **GLB** | *Metroid Prime: Federation Force*, *Luigi's Mansion: Dark Moon*, *Luigi's Mansion 3* |
| **FLCG collision** | `.GCL` | *(text dump)* | *No More Heroes* |
| **G1M** | `.g1m` | **GLB** | *Hyrule Warriors Legends*, *Fire Emblem Warriors* |
| **G4PKM** | `.g4pkm` | — | Unidentified |
| **Gamebryo 20.3.0.9 `.nif` (classic geometry)** | `.nif`, `.tga`, `.kf`, `.kfm` | — | *Monster Mayhem: Build and Battle*, *Dance on Broadway* |
| **GAN2 animation** | `.GA2` | *(text dump)* | *No More Heroes* |
| **GF1MOT** | `.gf1mot` | *(text dump)* | *Pokémon X / Y*, *Omega Ruby / Alpha Sapphire* |
| **GFBANM** | `.gfbanm` | *(identification only)* | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFBMDL** | `.gfbmdl` | *(identification only)* | *Pokémon Sword / Shield*, *Legends: Arceus* |
| **GFMODEL** | `.gfmodel` | **GLB** | *Pokémon X/Y/ORAS* |
| **GFMOT** | `.gfmot` | *(text dump)* | *Pokémon X / Y*, *Sun / Moon* |
| **GLG / RLG** | `.glg`, `.rlg` | **GLB** | *Super Mario Strikers*, *Mario Strikers Charged* |
| **GMF2 model** | `.gm2`, `.gmf2` | *(text dump)* | *No More Heroes* |
| **Granny 2 `.gr2` (big-endian)** | `.gr2`, `.glb` | — | *Paws & Claws: Pet Vet* |
| **HSD** | `.dat` | **GLB** | GameCube |
| **HSF** | `.hsf` | **GLB** | GameCube / Wii |
| **HXCB** | `.Hvc` | — | *Rune Factory: Frontier* |
| **HXHB** | `.Hvh` | — | *Rune Factory: Frontier* |
| **HXTP** | `.Hmt` | — | *Rune Factory: Frontier* |
| **Igor `.ggm` model** | `.ggm`, `.xga` | — | Wii |
| **Igor `.nav` navigation mesh** | `.nav`, `.nad` | — | Wii |
| **Imagine camera path** | `.cam` | *(text dump)* | *Party Babyz* |
| **Imagine WA2 skeletal animation** | `.wa2` | *(text dump)* | *Beastly* |
| **Imagine WSN scene mesh** | `.wsn` | *(text dump)* | *Beastly*, *Party Babyz* |
| **J3D BCK / BTK** | `.bck`, `.btk` | *(text dump)* | GameCube / Wii |
| **J3D BDL** | `.bdl` | **GLB** | *The Legend of Zelda: The Wind Waker*, *Twilight Princess*, *Super Mario Sunshine* |
| **J3D BMD** | `.bmd` | **GLB** | *The Legend of Zelda: The Wind Waker*, *Twilight Princess*, *Super Mario Sunshine* |
| **J3D BPK / BTP** | `.bpk`, `.btp` | *(text dump)* | GameCube / Wii |
| **Konami ZMB** | `.zmb` | **OBJ** | *Dance Dance Revolution: Winx Club* |
| **Krome `.mdl` / `.mdg`** | `.mdl`, `.mdg`, `.tex` | — | *Star Wars: The Force Unleashed* |
| **LMD** | `.lmd` | — | Unidentified |
| **MBN** | `.mbn` | **GLB** | 3DS titles with `.bch` scenes |
| **MCPK (mesh codec package), container only** | `.baatarc` | — | *The Legend of Zelda: Ocarina of Time*, *The payload is not decoded:* |
| **MDL0 / BRRES** | `.mdl0`, `.brres` | **GLB** | Wii |
| **Midway PHA** | `.pha`, `.anm` | *(text dump)* | Wii |
| **Midway PHM** | `.phm` | — | Wii |
| **Mini Ninjas `.gms` scene geometry** | `.gms` | — | Wii |
| **Mini Ninjas `.MAT` material library** | `.MAT`, `.TEX` | — | Wii |
| **Mini Ninjas `.prp` property stream** | `.prp` | — | Wii |
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
| **Opoona collision mesh (.atb)** | — | — | Wii |
| **Opoona MAP** | `.map` | **GLB** | *Opoona* |
| **Opoona MDL** | `.mdl` | **GLB** | *Opoona* |
| **Opoona model (.mdl)** | — | — | Wii |
| **Opoona MOT** | `.mot` | *(text dump)* | *Opoona* |
| **Pangya PET / MPET** | `.pet`, `.mpet` | *(text dump)* | *Super Swing Golf* |
| **PCS** | — | *(text dump)* | *Soma Bringer* |
| **PERS** | `.pers` | *(raw payload)* | N64 |
| **Petz DPGZ track** | — | *(text dump)* | *Petz Sports* |
| **Petz MACT table** | — | *(text dump)* | *Petz Sports* |
| **Phive static compound (.bphsc)** | `.bphsh` | — | *Mario Kart World* |
| **Pikmin 1 `.mod`** | `.mod`, `.glb` | — | *Pikmin* |
| **PMSH (phx)** | `.pmsh` + `.pmsh.gbuf` (from PARC) | **GLB** | *Kirby Air Riders* |
| **PANM (phx)** | `.panm` (from PARC) | **GLB** (skeleton + animation), property channels JSON | *Kirby Air Riders* |
| **engine_bin (phx)** | `startup.dat`, `Bin/config.bin` | **JSON** / Prism data | *Kirby Air Riders* |
| **Phive collision** | `.bphsh`, `.bphsc` | **OBJ** (+ material JSON; water grids as 16-bit PNG) | *Splatoon 3*, *Mario Kart World* |
| **RenderWare clump (DFF/RWS)** | `.dff`, `.rws` | **OBJ** | Wii titles |
| **Rockstar Leeds COL** | `.obj` | — | *Manhunt 2* |
| **Rockstar Leeds INST** | `.inst` | — | *Manhunt 2* |
| **SANIM** | `.sanim` | *(text dump)* | *Super Mario Strikers* |
| **Scribblenauts vector mesh** | — | *(text dump)* | *Scribblenauts Unlimited* |
| **Sega Ninja chunk model** | `.nj`, `.njm` | **OBJ** | *Radirgy Noa*, *Illvelo* |
| **Sega Ninja chunk model (`MCJN`)** | `.nj`, `.png`, `.pvr`, `.njm` | — | *Radirgy Noa*, *Illvelo* |
| **Sega Ninja-GC model `.gno`** | `.gno` | — | *Super Monkey Ball: Banana Blitz*, *Ghost Squad* |
| **Spectrobes `.HIT` collision** | `.HIT` | — | *Spectrobes: Origins* |
| **TARB (tera terrain)** | `.hght`, `.mate`, `.norm` | — | *Mario Kart World*, *The Legend of Zelda: Ocarina of Time*, *Tears of the Kingdom* |
| **Toshi terrain** | — | — | *Nickelodeon Barnyard* |
| **TSCB (MKW)** | — | — | Switch 2 |
| **TTMODEL** | `.model` | **GLB** | *LEGO Star Wars: The Skywalker Saga* |
| **Ubisoft BAN** | `.BAN` | *(text dump)* | Wii |
| **Ubisoft Imagine WIZ model** | `.wiz` | **OBJ** | *Beastly*, *Babyz* |
| **Ubisoft SERZ geometry** | `.GeoWii` | **OBJ** | Wii |
| **Voxler `.mesh.bin`** | — | — | *Let's Sing*, *The Voice*, "UV00", u32 count, count * |
| **WMB** | `.wmb` | **GLB** | *Star Fox Zero* |
| **SEGA GRO3** | `.gro3` | **OBJ** | *Sakura Wars: So Long, My Love* |
| **SEGA HMOT** | `.hmot` | **JSON** | *Sakura Wars: So Long, My Love* |
| **SEGA ABST** | `.asd` | **JSON** | *Sakura Wars: So Long, My Love* |
| **SEGA GLIP** | `.cnk` | **JSON** | *Sakura Wars: So Long, My Love* |
| **SEGA MSCR** | `.msX` | **MSB** (unpacked script binary) | *Sakura Wars: So Long, My Love* |
| **SEGA MSB** | `.msb` | **JSON** | *Sakura Wars: So Long, My Love* |
| **SEGA GHSL** | `.bin` | **JSON** | *Sakura Wars: So Long, My Love* |
| **SEGA MPLN** | `.GRD` | **JSON** + `.htsf` + **PNG** (atlas textures) | *Sakura Wars: So Long, My Love* |
| **SEGA AFCE** | `.GRD` | **JSON** + `.htsf` + **PNG** (face textures) | *Sakura Wars: So Long, My Love* |
| **SEGA GTPA** | `.gtp`, `.arm` | **JSON** | *Sakura Wars: So Long, My Love* |
| **SEGA ABDA** | `.abd`, `.bfX`, `.bf1`, `.dia`, `.arm` | **JSON** | *Sakura Wars: So Long, My Love* |
| **SEGA HSPR** | `.hspr`, `.bfX`, `.arm` | **JSON** | *Sakura Wars: So Long, My Love* |
| **Sumo COG / GOG** | `.cog`, `.gog` | **OBJ** | *Sonic & Sega All-Stars Racing*, *Virtua Tennis 2009* |

`Byte-Exact Roundtrip` = decode → GLB → re-encode reproduces the original file's bytes identically (canonical fixed-point verified), not just a successful encode.

---

### Textures & 2D Graphics

| Format | Extensions | Games |
| --- | --- | --- |
| **`ALIG` `.aig` textures** | `.aig` | *One Piece: Unlimited Adventure* |
| **`DST1` sprite sheets** | `.dst` | DS |
| **`GRPH` graphics (Boogie)** | `.dat` | DS |
| **`JBOA` animation manifests** | `.aap`, `.atb` | *Battle of Giants: Fight for Survival* |
| **`NCCL` / `NCCG` / `NCSC` (.ncl / .ncg / .nsc)** | `.ncgr`, `.nscr`, `.nce` | DS |
| **`NSC` tile maps** | `.nsc` | * w * |
| **`RCMN` / `RAMN` (NITRO MultiCell)** | `.nmcr`, `.nmar` | DS |
| **`TKLT` tile sets** | `.atk`, `.apl` | *Fight for Survival* |
| **AJPG / ODH** | `.ajpg` | GBA / Wii Message Board |
| **Arc Rise Fantasia WTM** | `.wtm` | *Arc Rise Fantasia* |
| **Argos no Senshi `.tgs` texture** | `.tgs` | Wii |
| **Around the World `.PTX` / `.PTXH` textures** | `.PTX`, `.PTXH`, `.png` | Wii |
| **ARTE-TEX** | `.tex` | *Dragon Quest IV: Chapters of the Chosen*, *Dragon Quest V: Hand of the Heavenly Bride*, *Dragon Quest VI: Realms of Revelation* |
| **ART / IMG** | `.art`, `.img` | Wii |
| **ASTC** | `.astc` | Switch and mobile titles |
| **Avalanche DBL** | `.dbl`, `.mdb` | *Disney's Bolt* |
| **BCFNT / BFFNT / BRFNT** | `.bcfnt`, `.bffnt`, `.brfnt` | 3DS / Wii U / Wii / Switch / Switch 2 |
| **Bee Movie `.Wiitex` texture** | `.Wiitex` | Wii |
| **BFFNT (Switch 2)** | — | *Mario Kart World* |
| **BFTTF / BFOTF** | `.bfttf`, `.bfotf` | Wii U / Switch / Switch 2 |
| **BGC image table** | `.bgc` | *Happy Neuron Academy* |
| **BGP** | — | *Soma Bringer* |
| **BGST3** | `.bgst3` | *Kirby's Epic Yarn* |
| **Bionicle Heroes `.CTX` texture** | `.CTX`, `.ctx` | Wii |
| **Budokai Tenkaichi 2 `.dbt` texture bundle** | `.dbt` | Wii |
| **Capcom `.apx` images** | `.apx` | *Zack & Wiki* |
| **Capcom `MTHP` still backgrounds** | — | *Resident Evil Archives: Resident Evil Zero* |
| **DC2 font cache** | `.dcf` | *Speed 2* |
| **EA FntG** | `.gfn` | Wii |
| **EA SHPG** | `.gsh` | Wii |
| **EA TXG** | — | *Tiger Woods PGA Tour* |
| **Earth Seeker `.pix` texture wrapper** | `.pix`, `.rsam`, `.fon`, `.divc` | Wii |
| **Exient TWI** | `.twi` | *Need for Speed: Hot Pursuit* |
| **Famicom Wars VS `.btf` texture bundle** | `.btf` | Wii |
| **FCPX complex font** | `.bfcpx` | Switch / Switch 2 |
| **BCLIM** | `.bclim` | 3DS |
| **BFLIM** | `.bflim` | Wii U |
| **BNR** | `.bnr` | GameCube / Wii titles |
| **BNSTX** | `.bnstx` | Switch titles |
| **BNTX** | `.bntx` | Switch |
| **BREFT** | `.breft`, `.bt-img` | Wii |
| **BTGA / LGA** | `.btga`, `.lga` | LEGO 3DS titles |
| **BTI / TPL** (BTI header edit via `wszst BTI`; GameCube banners via `wszst BNR`) | `.bti`, `.tpl` | GameCube / Wii |
| **GCT0** | `.BIN` | *No More Heroes* |
| **Green Lantern `.gaftx` texture** | `.gaftx`, `.glob` | Wii |
| **Havok-style HVT texture** | `.hvt` | *Aladdin Magic Racer* |
| **Igor `.fbin` / `.sfbin` bitmap font** | `.fbin`, `.sfbin` | Wii |
| **Infinity Ward `.iwi` image (v7, v11)** | `.iwi` | Wii |
| **International Athletics TEX** | `.TEX` | *International Athletics* |
| **J3D BMT** (MAT3 list/edit via `wszst MAT3`) | `.bmt` | GameCube / Wii |
| **Jillian Michaels Fitness Ultimatum `.lmp` texture lump** | `.lmp`, `.lps` | Wii |
| **JParticle JPC** (textures; list/extract/import particles with `wszst JPC`) | `.jpc` | *Twilight Princess*, *Super Mario Galaxy* |
| **JSystem BFN** | `.bfn` | *Twilight Princess*, *Super Mario Galaxy* |
| **Jurassic: The Hunted `.wit` texture** | `.wit` | Wii |
| **K11 `.btx` dialog table / `.a2` texture** | `.btx`, `.a2` | Wii |
| **Krome TEX** | `.tex` | *Star Wars: The Force Unleashed* |
| **Kylotonn VID (video frames)** | `.vid` | *Cocoto Festival* |
| **Little League TPC (`.tpc`)** | `.tpc` | *Little League World Series Baseball 2009* |
| **LTEX (Qubic)** | `.ltex`, `.png` | *2 Fast 4 Gnomz* |
| **LZMA-wrapped BRRES (`.brtex`)** | `.brtex` | *TV Show King Party*, * and * |
| **Mahjong Fight Club `.kwtp` texture pack** | `.kwtp`, `.kwbf`, `.kmsg` | *Mahjong Fight Club Wii* |
| **Midway FNT** | `.fnt`, `.tim` | Wii |
| **Midway SPR** | `.spr` | Wii |
| **Midway TIM** | `.tim` | Wii |
| **Momotarou Dentetsu 2010 font sheet** | — | *Momotarou Dentetsu 2010*, *Momotarou Dentetsu 16* |
| **Momotarou Dentetsu 2010 font sheet (`font.bin`, `font2.bin`, `kantei.bin`)** | — | *Momotarou Dentetsu 2010*, *Momotarou Dentetsu 16* |
| **Muramasa FTX / NMS** | `.ftx`, `.nms` | *Muramasa: The Demon Blade* |
| **Game Freak NTBG** | `.5bg` | *Looksley's Line Up* |
| **Namco Tales texture set (`.txm` + `.txv`)** | `.txm`, `.txv` | *Tales of Graces* |
| **NDS-style bitmap font (`.NFTR`)** | `.NFTR` | *Hannah Montana: The Movie* |
| **Next Level texture banks** | `.rlt`, `.res`, `.dmn` | *Mario Strikers Charged* |
| **NFF bitmap font `.nff`** | `.nff` | *Monster Mayhem* |
| **Nicktoons MLB BCT (`.bct`)** | `.bct` | *Nickelodeon Nicktoons MLB* |
| **Nippon Ichi IMY** | `.imy` | *Phantom Brave: We Meet Again* |
| **NW4R intermediate texture** | `.rtex` | *Der Gesundheitscoach* |
| **OBP** | — | *Soma Bringer* |
| **OIBB object pack** | — | *Doctor Fizzwizzle's Animal Rescue* |
| **Opoona textures (.tpl)** | — | Wii |
| **Opoona TPL** | `.tpl` | *Opoona* |
| **Pangya GSR** | `.gsr` | *Super Swing Golf* |
| **Paon .d4** | `.d4` | *Donkey Kong: Barrel Blast* |
| **Pet Vet `.wii` textures** | `.wii`, `.wii1`, `.png`, `.gr2` | *Paws & Claws: Pet Vet* |
| **Petz TEXL** | — | *Cranium Kabookii* |
| **Petz TTPL** | `.ozzy`, `...txl` | *Petz Sports*, *Petz Dogz/Horsez/Catz* |
| **Pikmin 1 `.txe`** | `.txe`, `.bti` | *Pikmin* |
| **Pixel Ent. texture (`.pte`/`.pfi`)** | `.pte`, `.pfi`, `.pso`, `.pmt` | *Barbie Horse Adventures* |
| **Planet 51 S3T (`.s3t`)** | `.s3t` | *Planet 51: The Game* |
| **PlatinumGames IDD / EFF** | `.idd`, `.eff` | *MadWorld* |
| **PS2 TIM2 `.tm2` texture** | `.tm2` | Wii |
| **PTEX (phx)** | `.ptex` + `.ptex.body` (from PARC) | *Kirby Air Riders* |
| **Qubic LTEX** | `.ltex` | *2 Fast 4 Gnomz* |
| **RenderWare TXD** | `.txd` | *GameCube/Wii* |
| **Rhythm Heaven Groove textures / sprite sets (TEX / SPR)** | `.tex`, `.spr` | *Rhythm Heaven Groove* |
| **Camelot GX bank** | *(none)*, `.stpl`, `.sbn` | *Mario Golf: Toadstool Tour*, *Mario Power Tennis*, *We Love Golf!* |
| **CAN** | `.can` | *Excite Truck*, *ExciteBots* |
| **CMAB** | `.cmab` | *Ocarina of Time 3D*, *Majora's Mask 3D* |
| **Rubik's Puzzle World `.etx` texture** | `.etx` | *Rubik's Puzzle World* |
| **Sakura Wars HTX** | `.htx` | *Sakura Wars: So Long, My Love* |
| **Scaleform GFx / Flash CWS** | `.gfx` | *Epic Mickey* |
| **SCB / IMB / PLB** | — | DS |
| **Sega GTS texture set** | — | *Ghost Squad* |
| **Sega GVR** | `.gvr`, `.gvp` | *Sonic and the Secret Rings*, 259 files, * |
| **Sega-style PVR (`XIBG`/`TRVP`) texture** | `.pvr` | *Radirgy Noa*, *Illvelo* |
| **Sega-style PVR texture** | — | *Radirgy Noa*, *Illvelo* |
| **SERZ texture groups** | `.TgWii` | Wii |
| **Sonic TXAG TXD** | `.txd` | *Sonic and the Black Knight* |
| **Starbreeze MOS texture set (`.xtc`)** | `.xtc` | *Enclave: Shadows of Twilight* |
| **Super Fruit Fall TDW / MEE** | `.tdw`, `.mee` | *Super Fruit Fall* |
| **TARB terrain tiles** | `.hght`, `.mate`, `.norm`, `.extm` | *Mario Kart World*, *The Legend of Zelda: Ocarina of Time* (Switch 2) |
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
| **NMCR / NMAR** | `.nmcr`, `.nmar` | DS |
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
| **Telltale `.d3dtx` textures (Wii)** | `.d3dtx`, `.ttarch` | *Back to the Future*, *Sam & Max* |
| **TEX** | `.tex` | Wii |
| **TEX0** | `.tex0` | Wii |
| **TEX3DS** | `.tex` | 3DS titles |
| **TGA with a foreign extension** | `.tga` | *Cocoto Kart Racer* |
| **TM0** | `.tm0` | *Excite Truck* |
| **Tony Hawk: Ride `.txw`** | `.txw` | Wii |
| **TPLC / GFX** | `.tplc`, `.gfx`, `.xpf` | *10 Minute Solution* |
| **Trauma Team TPX / BF / BM2** | `.tpx`, `.bf`, `.bm2` | *Trauma Team* |
| **Tropical TXTR** | `.txtr` | *Donkey Kong Country: Tropical Freeze* |
| **TVOL** | `.tvol` | Koei Tecmo / Gust titles |
| **TXE** | `.txe` | *Pikmin* |
| **TXTG** | `.txtg` | Next Level Games titles |
| **Ubisoft Imagine MSK / TAN** | `.msk`, `.tan` | *Babysitting Party* |
| **Ubisoft Imagine WSP sprite** | `.wsp` | *Babysitting Party*, *Babyz* |
| **Where's Wally `.ppm` (FMPP) images** | `.ppm`, `.GWVideo` | Wii |
| **WIBN** | `banner.bin`, `.bnr` | Wii |
| **Wii banner** | `opening.bnr`, `IMET`, `IMD5` | Wii |
| **Wii Chess `.mbb` message table / `.5a3` banner** | `.mbb`, `.5a3` | Wii |
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
| **`.mus` DSP-ADPCM chunks (Build-A-Bear / Alien Syndrome / Despicable Me engine)** | `.mus` | Wii |
| **`sedl` / `smdl` (Procyon DSE sequences)** | `.sed`, `.smd` | DS |
| **`swdl` (Procyon DSE sound bank)** | `.swd`, `.sed` | DS |
| **AADR (AAL directivity)** | `.baadr` | Switch |
| **AAL audio data (AMTA / AATN / AROC / AACL / AGST / AADR)** | `.amta`, `.baatn`, `.baroc`, `.baacl`, `.bagst`, `.baadr` | Switch / Switch 2 |
| **Academy of Champions `.asb` sound package** | `.asb` | *Academy of Champions: Soccer* |
| **Acoustic / light probe grids (DAPG / DLPG)** | `.dapg`, `.dlpg` | *Mario Kart World* |
| **AGSC** | `.agsc` | GameCube |
| **AGST 10** | `.bagst` | Switch 2 |
| **AIFF / AIFF-C PCM (`.aif`)** | `.aif` | *All Star Cheerleader 2*, *Blob* |
| **Aladin Magic Racer `.wsi`** | `.wsi` | Wii |
| **Alfa System STS** | `.sts` | *Castle of Shikigami III* |
| **AMTA 5.0** | — | *Mario Kart World* |
| **Aqua Panic! `common/*.eng`/`.fre` (RKET) DSP sound bank** | `.fre` | Wii |
| **Are You Smarter than a 5th Grader `.bob`** | `.bob` | Wii |
| **Asobo VAI** | `.vai` | GameCube |
| **Asura SFX / RedSpark** | `.sfx`, `.rsd` | *Sniper Elite*, *MadWorld* |
| **Baroque `.snt` + `.snd`** | `.snt`, `.snd` | Wii |
| **BARS** | `.bars` | Wii U / Switch / Switch 2 |
| **BARS (Switch 2)** | — | Switch 2 |
| **Bass Fishing `.u8c` text / Backyard Sports `.bank` sound bank** | `.u8c`, `.bank`, `.wav` | Wii |
| **BCBNK** | — | 3DS |
| **BCSAR / BCWAR / BCWAV** | `.bcsar`, `.bcwar`, `.bcwav` | 3DS |
| **BCWSD** | — | 3DS |
| **BFSAR / BFWAR / BFWAV** | `.bfsar`, `.bfwar`, `.bfwav` | Wii U / Switch |
| **Blazing Angels `_*.bnk` stereo DSP** | — | *Blazing Angels: Squadrons of WWII* |
| **BRSAR / RBNK / RWAV** | `.brsar`, `.rbnk`, `.rwav` | Wii |
| **BRSTM / BCSTM / BFSTM** | `.brstm`, `.bcstm`, `.bfstm` | Wii / 3DS / Wii U / Switch |
| **Bully: Scholarship Edition `.SEA` sound bank** | `.SEA` | Wii |
| **BWAV** | `.bwav` | Switch / Switch 2 (PCM16, DSP-ADPCM, Switch Opus) |
| **BWAV (native)** | `.bwav`, `.wav`, `.opus` | *Ocarina of Time* |
| **Capcom MT Framework Wii sound event request** | `.qers` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii sound tables** | `.qrts`, `.qers` | Capcom MT Framework Wii titles |
| **Capcom MT Framework Wii surround sound** | `.srnd` | Capcom MT Framework Wii titles |
| **Capcom SRT + SSD** | `.srt`, `.ssd` | *Monster Hunter Tri* |
| **Cauldron STR** | `.str` | GameCube |
| **Codemasters `iSWS` stream (`.was`)** | `.was` | *Dirt 2* |
| **Compiled asset `ASSETS/<id>` (SND, BMP)** | — | *We Wish You a Merry Christmas* |
| **Cosmic Family `.snh` + `streams.sns`** | `.snh`, `.mrq`, `.ana`, `.anf` | *Cosmic Family*, */voices/<lang>/* |
| **CRI `.ahx` / ADX audio** | `.ahx`, `.wav` | DS |
| **CRI AAX** | `.aax` | *Mario & Sonic at the London 2012 Olympic Games* |
| **CRI ADX** | `.adx` | *Ghost Squad* |
| **CRI AIX** | `.aix` | *Sonic Unleashed*, *Metroid: Other M* |
| **CRI Atom cue-sheet bank `.csb`** | `.csb` | *Pandora's Tower*, *Ghost Squad* |
| **CRI HCA audio `.hca`** | `.hca`, `.awb`, `.cpk` | *Power Rangers Samurai* |
| **CS12 sound container** | — | *Emergency Heroes* |
| **Dance Sensation `.lcm`** | `.lcm`, `.AMF` | *inferred* |
| **Data East `.jpi` + `.jps`** | `.jpi`, `.jps` | *Data East Arcade Classics*, *assumed* |
| **Despicable Me `Sounds/*.str` stereo DSP** | — | *Despicable Me: The Game*, *the sample rate is not stored and 32 kHz is assumed* |
| **Destroy All Humans! `.vbk` + `.vbi` sound bank** | `.vbk`, `.vbi` | Wii |
| **Disney Think Fast `.MIB` music** | `.MIB` | Wii |
| **Disney Think Fast speech pack (`.spb`)** | `.spb`, `.wav` | Wii |
| **DLS** | `.dls` | *Ninjabread Man*, *Monster Trux Arenas*, *Kidz Sports*, *Rig Racer 2* |
| **DSP `.asf` container** | `.asf` | *Brunswick Pro Bowling*, *Celebrity Sports Showdown* |
| **DSP container ASF** | `.asf` | *Brunswick Pro Bowling*, *Celebrity Sports Showdown* |
| **EA AEMS (`S10S`) EA-XAS stream** | — | *FaceBreaker K.O. Party* |
| **EA BNKb** | `.abk`, `.bnk` | *Tiger Woods PGA Tour*, *Madden NFL 10* |
| **EA multi-track `.mus` (EA-XAS)** | `.mus`, `.aems` | *FaceBreaker K.O. Party* |
| **EA pathfinder `.mus` (Spore Hero, EA-XAS)** | `.mus` | *Spore Hero* |
| **EA PathFinder MUS** | `.mus` | *The Godfather: Blackhand Edition*, *EA Sports Active* |
| **EA pathfinder overlay `.mus` (Spore Hero EALayer3)** | `.mus` | *Spore Hero*, */ovl_* |
| **EA SBR/SBS sound banks (EAAC XAS)** | — | *Grand Slam Tennis* |
| **EA SCHl** | `.ngc`, `.str` | *Tiger Woods PGA Tour 12* |
| **EA SCHl EALayer3** | `.ast` | *Celebrity Sports Showdown* |
| **EA SCHl EA-XA** | `.exa` | *The Godfather: Blackhand Edition* |
| **EA SCHl NGC** | `.ngc`, `.fcd`, `.ast` | *Tiger Woods PGA Tour*, *NBA Live 08*, *Madden NFL 10* |
| **EA SNR EALayer3 stream (`.snr`)** | `.snr`, `.mp3`, `.mus` | *FaceBreaker K.O. Party* |
| **EID** | `.eid` | *I Spy Spooky Mansion* |
| **Eurocom MUSX** | — | *Dead Space: Extraction*, *The Mummy: Tomb of the Dragon Emperor* |
| **Eurocom MUSX (SBNK)** | — | *Dead Space: Extraction*, *The Mummy: Tomb of the Dragon Emperor* |
| **Exient BNK** | `.bnk` | *Need for Speed: Hot Pursuit* |
| **Exient WIIADPCM** | `.adpcm` | *Need for Speed: Hot Pursuit* |
| **Fishing Master `.mbd` sound banks** | `.mbd` | *Fishing Master* |
| **FMOD Designer `.fmod` name table** | `.fmod`, `.txtc` | *10 Minute Solution* |
| **FMOD FEV** | `.fev` | *WALL-E*, *Kung Fu Panda 2*, *Marvel Super Hero Squad* |
| **FRHD frame bounds** | — | *Let's Dance with Mel B* |
| **FSAR 6.2 sound archive** | `.fsst` | *Switch 2 Welcome Tour* |
| **GBA MP2k (Sappy)** | `.gba` | Game Boy Advance |
| **GC LPS / KCEJE DSP / YMF** | `.lps`, `.ymf` | GameCube |
| **GCSP** | `.sounds` | GameCube |
| **GCSW** | `.gcw` | GameCube |
| **GCub** | `.gcub` | GameCube |
| **HAL HPS** | `.hps` | *Super Smash Bros.* series, *Kirby* |
| **Hal SSM** | `.ssm` | GameCube |
| **Hasbro Family Game Night `.xpac`** | `.xpac`, `.xml`, `.dsp`, `.wav` | Wii |
| **Heatseeker `.ffs` archive** | `.ffs`, `.DAT` | Wii |
| **Hudson PDT** | `.pdt` | GameCube |
| **Humongous Entertainment `.(a)` game resources** | — | Wii |
| **Humongous Entertainment `.he2` speech bank** | `.he2`, `.HE0`, `.HE4` | Wii |
| **Humongous Entertainment `.HE4` music** | `.HE4` | Wii |
| **HVSIAFB `.AFB` audio bank wrapper** | `.AFB`, `.AOS`, `.aob`, `.aod` | *The Grim Adventures of Billy & Mandy* |
| **IDSP (Inevitable)** | `.idsp`, `.wav` | GC |
| **Igor `.mkr` cue markers** | `.mkr` | Wii |
| **Igor `.xab` sound bank** | `.xab` | Wii |
| **Inevitable IDSP** | `.idsp` | *Defender* |
| **Infernal ITL** | `.itl`, `.ds2` | *MX vs. ATV Untamed* |
| **IOI IOISNDSTREAM** | `.str` | Wii |
| **IOI Glacier texture bank** | `.TEX` (in the scene `.zip` packs) | Wii |
| **IOI Glacier sound bank** | `.WHD` + `.WAV` | Wii |
| **IOI Glacier localisation** | `.LOC` | Wii |
| **IOI Glacier model container** | `.prm` | Wii |
| **ISH + ISD** | `.ish`, `.isd` | GameCube |
| **JAudio AAF** | `.aaf`, `.aw` | GameCube |
| **Konami `.vas` (Wii)** | `.vas`, `.bin` | *Jikkyou Powerful Pro Yakyuu*, *MLB Power Pros*, *Next* |
| **Konami `PACS` sound pack (`.sep`)** | `.sep`, `.afs` | *Mahjong Fight Club Wii* |
| **Konami VAS** | `.vas` | *Jikkyou Powerful Pro Yakyuu*, *MLB Power Pros* |
| **Krome MUS** | `.mus` | *Star Wars: The Force Unleashed* |
| **KSS Wave Link Data** | `.dsp`, `.vas` | *MLB Power Pros* |
| **KTSS** | `.kns`, `.kno`, `.ktss` | Switch |
| **KT WiiBGM / G1L** | `.g1l` | Wii / Wii U |
| **KTSS (DSP)** | `.kns`, `.kno`, `.ktss`, `.wav` | Switch |
| **Kuju KDSP voices** | `.ksp` | Wii |
| **KWA** | `.kwa` | *The Lord of the Rings: Aragorn's Quest* |
| **Kylotonn SND** | `.snd` | *Cocoto Festival* |
| **Madagascar SNDS** | `.snds` | *Madagascar: Escape 2 Africa* |
| **Major Minor WAF** | `.waf` | *Major Minor's Majestic March* |
| **Marines WWB (`.wwb`)** | `.wwb` | *Marines: Modern Urban Combat* |
| **Marvelous gcax sound bank (`.mpb` / `.mlt`)** | `.mpb`, `.mlt` | *Harvest Moon: Magical Melody* |
| **Metroid Prime RSF** | `.rsf` | *Metroid Prime* |
| **Microsoft IMA ADPCM WAV (`.wav`, tag 0x11)** | `.wav` | Wii |
| **MIDI → WAV** | `.sf2` | Audio |
| **Midway ADS** | `.ads` | GameCube |
| **Midway ASF bank** | `.ASF` | Wii |
| **Midway SFX bank** | `.sfx` | Wii |
| **Miles/RAD sound bank (`.mssbnk`, `RADBFile`)** | `.mssbnk` | *Fit in Six* |
| **Mix Superstar `.mxs` samples** | `.mxs`, `.wav` | Wii |
| **Mobiclip (`.mo`, `.moflex`, `.mods`)** | `.mo`, `.moflex`, `.mods` | DS |
| **Monster Games SFX** | `.sfx` | *Excite Truck*, *ExciteBots* |
| **Monster Games SNG** | `.sng`, `.sn0` + `.sf0` | *ExciteBots* |
| **MusyX bank (`.arc` + `.sam`)** | `.arc`, `.sam`, `.pool`, `.proj` | *Biohazard 0*, *Biohazard* |
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
| **Ogg Speex (`.spx`)** | `.spx` | *Let's Dance* |
| **Paon KNON** | `.str`, `.asr` | *Donkey Kong: Barrel Blast* |
| **Paon KNON wave bank** | `.WVD` | *Donkey Kong: Barrel Blast* |
| **Pikmin 1 `pikibank.bx`** | `.aw` | *Pikmin* |
| **Plain DSP as `.wav`/`.adp`/`.ddsp`/`.aus`** | `.wav`, `.adp`, `.ddsp`, `.aus` | *Wacky Races: Crash & Dash*, *Arctic Tale* |
| **PlatinumGames RSP sound pack** | `.rsp`, `.rsd` | *MadWorld* |
| **Procyon SADL** | `.sad` | DS |
| **Procyon Studio sound bank `.swd`** | `.swd`, `.sed`, `.vld` | *Arc Rise Fantasia* |
| **PROCYON-SMD** | `.smd` | *Professor Layton and the Diabolical Box*, *Pokémon Mystery Dungeon: Explorers of Sky* |
| **PROCYON-SWD** | `.swd` | *Professor Layton and the Diabolical Box*, *Inazuma Eleven*, *Pokémon Mystery Dungeon: Explorers of Sky* |
| **Punchers Impact MYSPD** | `.myspd` | *U-Sing* |
| **Racquet Sports `.SGC` speech bank** | `.SGC` | Wii |
| **Red Ball `.mus` music** | `.mus`, `.loc` | *The Ultimate Red Ball Challenge* |
| **Red Ball music** | `.mus` | *The Ultimate Red Ball Challenge* |
| **Reflections 04SW** | `.XA` | *Driver: Parallel Lines*, *Emergency Heroes* |
| **RenderWare ADPCM stream** | `.rws` | *The Legend of Spyro: Dawn of the Dragon*, *How to Train Your Dragon* |
| **Rhythm Heaven Groove CWV** | — | *Rhythm Heaven Groove* |
| **Rhythm Heaven Groove sound / tempo map (CWV / WMB)** | `.cwv`, `.wmb` | *Rhythm Heaven Groove* |
| **RIFX WAVE** | `.wax`, `.rifx` | *2 in 1 Meine Tierpension* |
| **Rig Racer 2 `.dspl` / `.dspr`** | `.dspl`, `.dspr` | Wii |
| **Robinsons `.eat` PCM stream** | `.eat` | *Meet the Robinsons* |
| **Rockstar Table Tennis `.spd`/`.spt` sound banks** | `.spd`, `.spt` | Wii |
| **Rockstar Table Tennis `.spt` + `.spd`** | `.spt`, `.spd`, `.cue` | Wii |
| **RSEQ / CSEQ / FSEQ / SSEQ** | `.rseq`, `.cseq`, `.fseq`, `.sseq` | Wii / 3DS / Wii U / DS |
| **RWSD (single wave)** | `.rwsd`, `.brwsd` | Wii |
| **SADL** | `.sad`, `.sadl` | *Professor Layton* |
| **SDAT** | `.sdat` | DS |
| **Sega gcax sound bank** | `.dat` | Wii |
| **Shin'en OGL** | `.ogl` | *Jett Rocket* |
| **Slam Games `.atd` / `.sad`** | `.atd`, `.sad`, `.std` | *Tele 7 Jeux*, *Margot's Word Brain* |
| **Sony SCEI sound bank** | `.hd` | *Momotarou Dentetsu 2010* |
| **Sony SCEI sound bank (`sound.bin`)** | `.hd` | *Momotarou Dentetsu 2010* |
| **Sony VAGp (`.vag`)** | `.vag` | *Skate City Heroes* |
| **SPD / SPT audio bank** | `.snd`, `.spd`, `.spt` | *Worms: Battle Islands*, *Beastly*, *Babysitting Party*, *Burger Bot* |
| **Squeeballs Party `.ast` (PCM16)** | `.ast` | *Squeeballs Party* |
| **Staff of Kings `MONSTER.SOU` (Creative Voice)** | — | *Indiana Jones and the Staff of Kings*, *Fate of Atlantis* |
| **Standalone `.pcm` DSP-ADPCM sounds** | `.pcm`, `.wav`, `.wt` | *Alan Hansen's Sports Challenge* |
| **Star Trek: Conquest `.WSB` + `.WSS` sound banks** | `.WSB`, `.WSS` | Wii |
| **Stormfront SEG** | `.seg` | *The Spiderwick Chronicles* |
| **Sumo ABC** | `.abc` | *Sonic & SEGA All-Stars Racing* |
| **Switch Opus** | `.opus`, `.lopus`, `.opusnx` | Switch |
| **Sygnus soundstream / PSND** | `.soundstream`, `.psnd` (from PARC) | *Kirby Air Riders* |
| **Tecmo GSND** | `.gsp`, `.gsb` | Wii |
| **Telltale `ERTM` audio (`.aud`)** | `.aud`, `.ttarch`, `.vox` | *Back to the Future: The Game* |
| **Telltale `ERTM` voice (`.vox`, Speex)** | `.vox`, `.ttarch` | *Back to the Future*, *Sam & Max* |
| **Terminal Reality GCA** | `.gca` | *Metal Slug Anthology* |
| **Terminal Reality SMP** | `.smp` | *Guilty Party* |
| **Treyarch NSLB WBK** | `.wbk` | *Kung Fu Panda*, *Call of Duty 3* |
| **tri-Crescendo CAF** | `.caf`, `.cfn` | *Fragile Dreams* |
| **UBER sound bank (`.uber` + `.samp`)** | `.uber`, `.samp`, `.dsp` | *Rampage: Total Destruction* |
| **Ubisoft BAO package** | `.pk`, `.lpk`, `.spk` | *Rayman Raving Rabbids: TV Party*, *Just Dance*, *We Dare*, *Avatar* |
| **Ubisoft GWB+GWD** | `.gwb`, `.gwd` | *Monster 4x4: World Circuit* |
| **Ubisoft Imagine ADPCM** | `.adpcm` | *Beastly* |
| **Ubisoft LySE wrapper (`.wav`)** | `.wav` | *Just Dance 2* |
| **Ubisoft SB** | `.sb7` | *Red Steel* |
| **Video containers (identify only)** | `.bik`, `.bk2`, `.dpg`, `.h4m` | Video |
| **Voxler `.vxla` karaoke annotations** | `.vxla` | *Let's Sing*, *The Voice*, *Sing 4* |
| **waud PCM sound** | `.wtxs`, `.wtss` | *Cooking Mama: Cook Off* |
| **WAVEBATCH** | `.wavebatch` | 3DS |
| **WayForward WAVE** | `.wave` | 3DS / DS / Wii |
| **Wrapped interleaved DSP** | `.gcm`, `.idsp`, `.sad` | *Transformers*, *Mario Strikers Charged*, *Disaster: Day of Crisis* |
| **WT** | `.wt` | *Octomania* |
| **WWE SmackDown vs. Raw `.yaf` (SFAY) sound bank** | `.yaf` | Wii |
| **Wwise BNK** | `.wem` | *Epic Mickey* |
| **YDSP** | `.ydsp` | GameCube |
| **Z-Axis ZSND** | `.zss`, `.zsm` | *Marvel: Ultimate Alliance 2* |
| **Z-Axis ZSND (GCUB)** | `.zss`, `.zsm` | *Marvel: Ultimate Alliance 2* |

---

### Layouts, Text & Game Data

| Format | Extensions | Games |
| --- | --- | --- |
| **10 Minute Solution / Zhu Zhu Pets `.txtc` text catalog** | `.txtc`, `.fmod` | Wii |
| **ADEV adventure events** | — | *Soul Eater: Monotone Princess*, * groups, 8 * |
| **ai00 / aicd AI tables** | — | *One Piece Unlimited Adventure / Cruise* |
| **AiLive LiveMove LMC** | `.lmc` | Wii |
| **Another Code R `.MESS` text** | `.MESS`, `.BMG` | */MESS/* |
| **Arctic Tale `Language_*.lng` string tables** | — | *Arctic Tale* |
| **ATCD text catalog** | — | *Safar'Wii* |
| **Athletics-engine animation (`.ANM`)** | `.ANM` | *International Athletics* |
| **BAAO attack attribute tables** | — | *One Piece Unlimited Adventure / Cruise* |
| **Baked light list / culling boxes / foliage tables** | `.bklight`, `.bbox`, `.flnt` | *The Legend of Zelda: Ocarina of Time* (Switch 2) |
| **Batman B&B `.lipb` lip sync** | `.lipb` | *Batman: The Brave and the Bold* |
| **Battle of Giants `.bof` language table** | `.bof` | *Battle of Giants: Dinosaurs Strike* |
| **BGDT stage background tables** | — | *Teolsil Kirby Iyagi*, *Kirby's Epic Yarn* |
| **Big League Sports `.bxml`** | `.bxml` | *Big League Sports* |
| **Cabela's `TextLib.<lang>.bkp` text library** | — | Wii |
| **Chicken Little `.dct` string dictionary** | `.dct` | *Disney's Chicken Little: Ace in Action* |
| **CSVD spawn table** | — | *Link's Crossbow Training* |
| **Destiny of Zorro `.lso` string table** | `.lso` | Wii |
| **EA Montreal `.loc` / `.sub`** | `.loc`, `.sub` | *Boogie SuperStar* |
| **Earth Seeker `.ffx` (SYSM messages)** | `.ffx` | Wii |
| **EqGUI image sets** | — | *Mary King's Riding School 2* |
| **Gems engine `.lang` string table** | `.lang`, `.ebs`, `.eui` | *Groovin' Blocks* |
| **GFF0 effect database** | — | *Trauma Center: New Blood* |
| **Harvest Moon: Animal Parade `.stb` (STBL)** | `.stb` | Wii |
| **HXAA** | `.Hvb` | *Rune Factory: Frontier* |
| **HXGB** | `.Hvg`, `.Mod` | *Rune Factory: Frontier* |
| **HXMB** | `.Hvm` | *Rune Factory: Frontier* |
| **HXTB** | `.hvt`, `.Hvt` | *Rune Factory: Frontier* |
| **Igor `.xga` animation** | `.xga` | Wii |
| **JAudio1 `.jam` sequence** | `.jam` | *Pikmin* |
| **Localisation string table (`.str`, UTF-16BE)** | `.str` | *Freddi Fish: Kelp Seed Mystery*, 01 00 00 00, u32 count, count * |
| **Lua 5.1 bytecode (big endian)** | `.luac` | Wii |
| **Manhunt 2 world and model geometry (WRLD / CLMP)** | `.bsp`, `.dff`, `.obj`, `.mtl` | *Manhunt 2* |
| **Mario Kart World, not converted** | `.bcul`, `.bbox`, `.pccg`, `.ghost` | Wii |
| **Marvelous `MSGP` `.mpc`** | `.mpc` | *Harvest Moon: Animal Parade* |
| **MCPK mesh package (container only)** | `.mc` | *The Legend of Zelda: Ocarina of Time* (Switch 2) |
| **AINB AI node graph** | `.ainb` | *Splatoon 3*, *Tears of the Kingdom*, *Mario Kart World*, *The Legend of Zelda: Ocarina of Time* (Switch 2) |
| **BFEVFL event flow** | `.bfevfl` | *The Legend of Zelda: Ocarina of Time* (Switch 2), *Tears of the Kingdom*-style event flows |
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
| **Necro-Nesia `.bmsg` message table** | `.bmsg` | Wii |
| **Ninja motion (`MDMN`/`MACN`)** | `.njm` | *Radirgy Noa*, *Illvelo* |
| **NRR0 (module list)** | `.nrr` | Switch 2 |
| **NRR0 (Switch 2 module list)** | `.nrr`, `.nro` | Switch 2 |
| **NSO (+ ZBIC segments)** | `.nso` | *Ocarina of Time*, *ZBIC* |
| **Opoona animation (.mot)** | — | Wii |
| **Opoona message bank** | `msg_*.bin` | *Opoona* |
| **Opoona parameter tables** | `*_param.bin`, `encounter.bin` | *Opoona* |
| **Oregon Trail `.stp` string pack** | `.stp` | *The Oregon Trail* |
| **Ousama Monogatari `MDE1` tables** | — | *Ousama Monogatari / Little King's Story* |
| **Phantom Brave battle map** | `.mpd` | *Phantom Brave: We Meet Again* |
| **Phineas and Ferb `.lcl` localisation** | `.lcl` | Wii |
| **Pikmin 1 `.blo`** | `.blo` | *Pikmin*, *_blo/* |
| **Pikmin 1 `.gst`** | `.gst` | *Pikmin* |
| **Pikmin 1 `.key`** | `.key` | *Pikmin* |
| **Pikmin 1 `.pcr`** | `.pcr` | *Pikmin* |
| **Pikmin 1 `camera/camepara.bin`** | — | *Pikmin* |
| **Pikmin 1 `tekipara/*.bin`** | — | *Pikmin* |
| **Pikmin 1 parameter lists (`parms/*.bin`, `pikis`, `pelMgr`, ...)** | `.bin` | *Pikmin* |
| **Pipeworks PWK `.pvd` / `.mcl` strings** | `.pvd`, `.mcl`, `.pvm` | *Monster High: Ghoul Spirit* |
| **PLANETG** | `.MWG`, `.MSP` | *Bermuda Triangle: Saving the Coral*, *Rock Blast*, *Veggy World*, *Ocean Commander*, *Arcade Shooting Gallery* |
| **NAV** | `.nav` | *Mercury Meltdown Revolution* |
| **PGF** | `.pgf` | *Bermuda Triangle: Saving the Coral*, *Rock Blast*, *Veggy World*, *Ocean Commander*, *Arcade Shooting Gallery*, *Offshore Tycoon* |
| **pop'n music SEN / AEPT** | `.sen`, `.aept`, `.aepc` | *pop'n music* |
| **RC chr fighter record** | `.chr` | *Power Punch* |
| **Real Stories: Veterinarian `.edf` / `dic_*.dic`** | `.edf`, `.ldp` | Wii |
| **Rebellion Asura STR** | `.str` | *PDC World Championship Darts* |
| **Rebellion Asura string table (`.str`)** | `.str` | *PDC World Championship Darts* |
| **Rhythm Heaven Groove WMB** | — | Switch |
| **Rockstar GXT text** | `.gxt` | Wii |
| **Rockstar Leeds GRF** | `.csv` | *Manhunt 2* |
| **Rockstar Leeds material table** | — | *Manhunt 2* |
| **Rockstar Leeds MLS** | `.mls` | *Manhunt 2* |
| **Rockstar Leeds streamed animations** | `.csv` | *Manhunt 2* |
| **RSTB / RESTBL** | `.rstb`, `.rsizetable` | *Breath of the Wild*, *Tears of the Kingdom*, *Mario Kart World* |
| **Samba de Amigo challenge tables** | — | *Samba de Amigo* |
| **Scaleform GFx movie listing (plain `GFX`)** | `.zzz` | *X-Men: Destiny* |
| **Scrabble rack-leave tables** | — | *Scrabble Interactive* |
| **SDF** | `.sdf` | *I Spy Spooky Mansion* |
| **Sengoku Basara 3 LSP / SDL** | `.lsp`, `.sdl` | *Sengoku Basara 3* |
| **Sims vault** | `.vlt`, `.bin` | *SimAnimals*, *SimAnimals Africa*, *MySims Kingdom*, *MySims Agents* |
| **Sims vault (`VALT` `.vlt` + `VBIN` `.bin`)** | `.vlt`, `.bin` | *SimAnimals*, *SimAnimals Africa*, *MySims Kingdom* |
| **Sonic Team `.xtb` text table** | `.xtb` | Wii |
| **Sonic Team BBINA** | — | particle lists) |
| **Sonic Team NGIF layout (`.gncp`)** | `.gncp` | Wii |
| **SPHC TXF** | — | *Pool Hall Pro*, *Vertigo* |
| **Spyro: Dawn of the Dragon KCS** | `.KCS` | *The Legend of Spyro: Dawn of the Dragon* |
| **Spyro DotD KRV (language)** | `.KRV` | *The Legend of Spyro: Dawn of the Dragon* |
| **SSRT parameter database** | — | *Sonic & Sega All-Stars Racing* |
| **STGS** | — | *MySims Agents* |
| **Storybook Workshop `.mct` menu text** | `.mct` | Wii |
| **STR module path list** | `.str` | Wii titles |
| **STRT** | — | *Unlimited Adventure/Cruise* |
| **Sukeban `event.dat` lists** | — | *Sukeban Shachou Rena Wii* |
| **Sumo `.sbn` script packages** | `.sbn` | *Sonic & SEGA All-Stars Racing* |
| **Super Monkey Ball `.tdb` text table** | `.tdb` | Wii |
| **Table Tennis string table `.strtbl`** | `.strtbl` | *Rockstar Games Presents Table Tennis* |
| **TDNS sound names** | `.bin` | Wii |
| **TDXS typed tables** | — | *Shawn Johnson Gymnastics* |
| **Thrillville serialised objects** | `.prk`, `.tbp`, `.atr` | *Thrillville: Off the Rails* |
| **Tomodachi Life: Living the Dream, partly converted** | `.skybin` | Switch |
| **Top Model Academy `.ctx` text table** | `.ctx` | Wii |
| **TorqueScript `.dso`** | `.dso` | *The Destiny of Zorro* |
| **Toshi terrain locators** | — | *Nickelodeon Barnyard* |
| **Toshi TRZ** | `.trz`, `.trb` | *Nickelodeon Barnyard* |
| **Toshi TSFB (TRB/TTL)** | `.ttl`, `.tkl`, `.trb`, `.trz` | *Nickelodeon Barnyard* |
| **Toshi TSO** | `.tso` | *Nickelodeon Barnyard* |
| **TSCB** | `.tscb` | *Mario Kart World*, *The Legend of Zelda: Ocarina of Time* (Switch 2) |
| **Ubisoft Imagine LOC** | `.LOCV1.0` | *Beastly*, *Babysitting Party*, *Babyz* |
| **Ubisoft LOC text** | `.loc` | Wii |
| **v1b arrangement tables** | — | *Nights: Journey of Dreams* |
| **Valhalla Knights `.bxml`** | `.bxml` | *Valhalla Knights: Eldar Saga*, *Big League Sports* |
| **VDDB string database (`.vddb`)** | `.vddb` | *My Baby Boy and Girl 2* |
| **Virtua Tennis 2009 `WorldTour.jam`** | — | Wii |
| **VISLIST** | `.vis` | *Aqua Panic!*, *SPRay*, *Cyberbike*, other Eko Software games |
| **VT2009 animation tables** | `.dat` | *Virtua Tennis 2009* |
| **WB-L10n** | — | *So Blonde: Back to the Island* |
| **WCES / WCDR scripts** | — | *Oha Suta Produce! Gentei Collabo Game Disc* |
| **We Cheer 2 keyboard layouts** | — | *We Cheer 2* |
| **WSC shot scenarios** | — | *WSC Real 09 - World Snooker Championship* |
| **WT + PCM → SF2** | `.pcm`, `.mid` | Wii |
| **XBIN (HAL XData: YAML / FDG)** | `.bin`, `.dat`, `.archive.bin` | *Kirby and the Forgotten Land* |
| **XLNK (XLink2 effect / sound links)** | `.belnk`, `.bslnk` | Switch (*Tears of the Kingdom*-era layout checked on *Tomodachi Life: Living the Dream*) |
| **BAEV (animation events)** | `.baev` | *Tomodachi Life: Living the Dream* |
| **ASB (animation sequence binary)** | `.asb` | *Tomodachi Life: Living the Dream* |
| **Phive cloth / Havok tag files** | `.bphcl`, `.bphnm` | *Tomodachi Life: Living the Dream*, *The Legend of Zelda: Ocarina of Time* (Switch 2) |
| **VoiceText database** | `.vtdb2` | *Tomodachi Life: Living the Dream* |
| **XB** | `.xml` (binary) | *Mario Party 10* |
| **XMB** | `.xmb` | *Super Smash Bros. for 3DS / Wii U*, *Super Smash Bros. Ultimate* |
| **ZEN** | `.zen` | *Mercury Meltdown Revolution* |
| **Manhunt 2 INST / COL / GRF / MLS** | `.inst`, `.col`, `.grf`, `.mls` | *Manhunt 2* |
| **Manhunt 2 material table / streamed animation index** | `mat_wii.bin`, `strmanim_wii.bin` | *Manhunt 2* |
| **Rockstar Leeds IFP (ANCT / ANPK)** | `.ifp` | *Manhunt 2* |

`Byte-Exact Roundtrip` = encode → semantic text → re-encode reproduces the file's bytes. Exercised by `t_byte_fixed_points()` in `tests/regress.sh`. BRLYT/BRLAN canonical fixed point and semantic roundtrips are validated against retail Wii layouts.

---

### Compression & Encoding Formats

| Algorithm | Identifier | Used in |
| --- | --- | --- |
| **`.rz` compressed module** | `.rz`, `.rso` | *And-Kensaku* |
| **`LMPK` / `LMP2` `.ngr` files** | `.ngr` | DS |
| **`ntcompress2` wrapper (`.crfnt`)** | `.crfnt` | *Animal Kingdom: Wildlife Expedition*, */text/* |
| **`PACK` LZ10 files** | `.set`, `.fnt`, `.bin`, `.def` | DS |
| **`RLE!` blocks** | `.til`, `.msk`, `.tail` | DS |
| **`SLBT` banks** | `.atb` | DS |
| **ALZ1** | `ALZ1` | GameCube / Wii |
| **ASH0** | `ASH0` | Wii System Menu, Animal Crossing, My Pokémon Ranch |
| **BLZ** | ARM9 overlay trailer | DS titles (ARM9 overlays) |
| **BPE / GFCP** | `GFCP` (zip mode 1) | Wii Kirby's Epic Yarn / Yoshi's Woolly World |
| **Bzip2** | `BZh` | Many cross-platform titles |
| **Camelot LZ** | `0x01` / `0x02` prefix | *Mario Golf*, *Mario Tennis* |
| **Chunked-LZF object files (`.et` `.map` `.rgn` `.loc` `.cin` ...)** | `.et`, `.map`, `.rgn`, `.loc` | *Despicable Me: The Game* |
| **CMP** | `.cmp` (`0x11` prefix) | HAL Laboratory titles |
| **DAD** | — | *Soma Bringer* |
| **Deflate / Zlib** | `78 01`, `78 9C`, `78 DA` | Many cross-platform titles |
| **Diff8 / Diff16** | `0x81`, `0x82` | DS titles |
| **Disaster: Day of Crisis `.ppz`/`.epz`/`.saz` (SZ00 zlib)** | `.ppz`, `.epz`, `.saz`, `.red` | Wii |
| **EA Tiger Woods `.sas` RefPack stream chain** | `.sas` | Wii |
| **fpack `.fpk` (CyberConnect2)** | `.fpk` | *Naruto Shippuden: Clash of Ninja Revolution III*, *Tatsunoko vs. Capcom* |
| **FZIP** | `FZIP` | *Game & Wario* |
| **HAL LZ4** | `.cmp`, `.cmpbin` (u32 size + `04 22 4D 18`) | *Kirby and the Forgotten Land* |
| **Huffman (4-bit / 8-bit)** | `0x24`, `0x28` | DS / GBA titles |
| **Hydroventure `.lpf2` level packs** | `.lpf2` | Wii |
| **INIO-LZO** | — | *Osu! Tatakae! Ouendan*, *Moero! Nekketsu Rhythm Damashii*, *Elite Beat Agents* |
| **Jimmie Johnson `.ibz` (zlib)** | `.ibz` | Wii |
| **LZ10** | `0x10` (LZSS) | GameCube / Wii / DS / GBA |
| **LZ11** | `0x11` (Extended LZSS) | DS / 3DS |
| **LZ4** | `04 22 4D 18` | Switch and many cross-platform titles |
| **LZE** | — | DS |
| **LZO / LZOvl** | Overlay trailer | DS titles (reverse LZO overlays) |
| **LZX** | `LZX` | DS |
| **Military Madness: Nectaris `.sir` streams** | `.sir` | Wii |
| **Moki Moki `.pacz` (LZS v5)** | `.pacz` | Wii |
| **MTXZ zlib (`.mz`)** | `.mz` | *Shin Chan: Las Nuevas Aventuras* |
| **MVDK** | `MVDK` | DS |
| **Natsume add PAC / LZS** | `.pac` | *Gintama: Yorozuya Chuubu - Tsukkomable Douga* |
| **Neko LZSS** | `.pc`, `.GCN` | *Cocoto Kart Racer 2*, *Cocoto Magic Circus* |
| **Next Level .zlib / NLOC** | `.zlib`, `.loc` | *Mario Strikers Charged* |
| **PSDK** | `PSDK` / `AT4PX` | DS |
| **PuCrunch** | `0x50 0x75` (`Pu`) | DS titles |
| **QuickLZ** | `QLZ` | Various third-party titles |
| **RLE** | `0x30` | DS titles |
| **RNC1 / RNC2** | `RNC\1`, `RNC\2` | Rob Northen ProPack titles |
| **SSZL** | `SSZL` | Wii |
| **The Amazing Brain Train `.blk.bz2`** | — | Wii |
| **Tornado Outbreak BGW** | `.bgw` | *Tornado Outbreak* |
| **Tron Evolution Battle Grids `*_` LZ files** | — | Wii |
| **Ubisoft Imagine LZSS** | `.wiz`, `.wsp`, `.wsn`, `.tan`, `.wan` | *Babysitting Party*, *Babyz*, *Beastly* |
| **Ubisoft Imagine LZSS (`0x87654321`)** | `.wiz`, `.wsp`, `.wsn`, `.tan` | *Babysitting Party*, *Babyz*, *Beastly* |
| **VLX** | `VLX` | DS |
| **WIS block-LZSS** | `.wis` | *Cabela's* Wii titles |
| **Yay0 (SZP)** | `Yay0` | Nintendo 64 / GameCube |
| **Yay0 / Yaz0** | — | Compression |
| **Yaz0 (SZS)** | `Yaz0` | GameCube / Wii / Switch |
| **Zstandard (Zstd)** | `28 B5 2F FD` | Switch / F-Zero 99 / Switch 2 Zelda (`Pack/ZsDic.pack.zs` dictionaries) |
| **zstd dictionaries (`ZsDic.pack.zs`)** | `.zsdic` | *The Legend of Zelda: Ocarina of Time* |

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

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version. See the [LICENSE](LICENSE) file (also `project/gpl-2.0.txt`) or <http://www.gnu.org/licenses/gpl-2.0.txt> for details.

Based on **Wiimms SZS Tools** by Dirk Clemens (*Wiimm*), licensed under GPL v2. See [CREDITS.md](CREDITS.md) for attributions.

