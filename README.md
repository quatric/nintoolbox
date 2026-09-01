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

| Format | Extensions | Games / Platforms Using This Format |
|---|---|---|
| **ARC / U8** | `.arc`, `.szs` | *Mario Kart Wii*, *Super Mario Galaxy 1 & 2*, *New Super Mario Bros. Wii*, *Wii Sports*, *Zelda: Skyward Sword* |
| **Arika Archive** | `INFO.DAT`, `GAME.DAT` | *Dr. Mario Online Rx*, *Dr. Mario Express*, *Endless Ocean 1 & 2* (DS/Wii) |
| **ARCV** | `.arc` | *Pac-Man Party* (Wii) |
| **AT7** | `.at7` | *Another Century's Episode*, Koei Tecmo titles (Wii/PS2) |
| **BG4** | `.bg4` | *Mario & Luigi: Paper Jam*, *Paper Mario MIX* (3DS) |
| **BIGF** | `.big` | EA Wii titles (*Littlest Pet Shop*, etc.) |
| **CA01 / SA01** | `.ca01`, `.sa01` | *Mii Maker* (Wii U), *amiibo Settings* (3DS) |
| **CCF** | `.ccf` | Virtual Console Arcade / TurboGrafx-16 releases (Wii / Switch) |
| **CRAM** | `.arc`, `.cram` | *Xenoblade Chronicles 3D* (3DS) |
| **DARC** | `.darc` | *Mario Kart 7*, *Luigi's Mansion: Dark Moon*, *Super Smash Bros. 3DS* |
| **FSYS** | `.fsys` | *Pokémon Colosseum*, *Pokémon XD: Gale of Darkness*, *Pokémon Battle Revolution* (GC/Wii) |
| **GFA** | `.gfa` | *Pokémon Sun & Moon*, *Pokémon Ultra Sun & Ultra Moon* (3DS) |
| **Hyrule Warriors** | `.idx`, `.bin` | *Hyrule Warriors Legends* (3DS) |
| **MPBIN** | `.bin` | *Mario Party 4, 5, 6, 7, 8* (GameCube / Wii) |
| **NARC** | `.narc` | *Pokémon Diamond/Pearl/Platinum/HGSS/BW/BW2*, *Mario Kart DS*, *Animal Crossing: Wild World* |
| **NCCARC** | `.nccarc` | *WarioWare: Touched!* (DS) |
| **NDS / SRL / DSI** | `.nds`, `.srl`, `.dsi` | Nintendo DS, DSiWare, and DS ROM images |
| **PAC / MRG** | `.pac`, `.mrg` | *Super Smash Bros. Brawl* (Wii) |
| **RARC** | `.rarc`, `.arc` | *Zelda: The Wind Waker*, *Zelda: Twilight Princess*, *Super Mario Sunshine*, *Luigi's Mansion* |
| **RFL_Res** | `RFL_Res.dat`, `.dat` | Wii System NAND, *Mii Channel*, *Wii Sports*, *Wii Party*, *Wii Fit* |
| **SARC** | `.sarc`, `.szs` | *Zelda: Breath of the Wild*, *Super Mario Odyssey*, *Mario Kart 8 / Deluxe*, *Splatoon 1/2/3*, *Animal Crossing: New Horizons* |
| **SZE** | `.sze` | *F-Zero 99*, NST Nintendo Switch titles |
| **WARC** | `.warc` | *Game & Wario* (Wii U) |
| **WUD / WUX** | `.wud`, `.wux` | Nintendo Wii U retail disc images |

---

### 3D Models & Geometry

| Format | Extensions | Games / Platforms Using This Format |
|---|---|---|
| **BCH** | `.bch` | *Pokémon X/Y/ORAS/Sun/Moon*, *Zelda: A Link Between Worlds*, *Luigi's Mansion: Dark Moon* (3DS) |
| **BCRES** | `.bcres` | *Super Mario 3D Land*, *Mario Kart 7*, *Zelda: Ocarina of Time 3D / Majora's Mask 3D* (3DS) |
| **BFRES** | `.bfres` | *Super Mario Odyssey*, *Mario Kart 8 / Deluxe*, *Zelda: BotW*, *Splatoon*, *Super Mario 3D World*, *Captain Toad* (Wii U / Switch) |
| **BMD** | `.bmd`, `.bdhc` | *Super Mario 64 DS*, early Nintendo DS 3D titles |
| **CGFX** | `.cgfx` | *Super Smash Bros. 3DS*, *Mario & Luigi: Dream Team* (3DS) |
| **HSD** | `.dat` | *Super Smash Bros. Melee*, *Kirby Air Ride* (GameCube) |
| **HSF** | `.hsf` | *Mario Party 4, 5, 6, 7, 8* (GameCube / Wii) |
| **MDL0 / BRRES** | `.mdl0`, `.brres` | *Mario Kart Wii*, *Super Smash Bros. Brawl*, *Super Mario Galaxy 1 & 2*, *Donkey Kong Country Returns* (Wii) |
| **MOD** | `.mod` | *Excite Truck*, *ExciteBots: Trick Racing*, *NASCAR Heat* (Wii) |
| **MSH (PMsh)** | `.msh` | *Excite Truck*, *ExciteBots: Trick Racing* collision meshes (Wii) |
| **NSBMD** | `.nsbmd`, `.bmd` | *Pokémon HGSS/BW/BW2*, *New Super Mario Bros. DS*, *Zelda: Phantom Hourglass* (DS) |

---

### Textures & 2D Graphics

| Format | Extensions | Games / Platforms Using This Format |
|---|---|---|
| **ART / IMG** | `.art`, `.img` | *Excite Truck*, *ExciteBots: Trick Racing* UI images (Wii) |
| **BCFNT / BFFNT / BRFNT** | `.bcfnt`, `.bffnt`, `.brfnt` | 3DS, Wii U, and Wii system fonts and game UI text |
| **BCLIM / CTPK** | `.bclim`, `.ctpk` | *Mario Kart 7*, *Animal Crossing: New Leaf*, *Super Mario 3D Land* (3DS) |
| **BFLIM** | `.bflim` | *Mario Kart 8*, *Super Mario 3D World*, *Captain Toad*, *Splatoon* (Wii U) |
| **BNTX** | `.bntx` | *Super Mario Odyssey*, *Mario Kart 8 Deluxe*, *Splatoon 2 & 3*, *Zelda: BotW / TotK*, *ARMS* (Switch) |
| **BREFT** | `.breft` | *Super Smash Bros. Brawl*, *Mario Kart Wii* particle effect textures |
| **BTI / TPL** | `.bti`, `.tpl` | *Super Mario Sunshine*, *Luigi's Mansion*, *Mario Kart: Double Dash!!*, *Mario Kart Wii* |
| **GTX** | `.gtx` | *Donkey Kong Country: Tropical Freeze*, *Pikmin 3*, *Zelda: Wind Waker HD / Twilight Princess HD*, *Nintendo Land* (Wii U) |
| **NCGR / NCLR / NCER / NANR** | `.ncgr`, `.nclr`, `.ncer`, `.nanr` | *Pokémon Diamond/Pearl/Platinum/HGSS/BW/BW2*, *Mario Kart DS*, *Chrono Trigger DS*, *Kirby Super Star Ultra* (DS) |
| **NSBTX** | `.nsbtx` | *Pokémon Platinum/HGSS/BW2*, *Mario Kart DS*, *Dragon Quest IX* (DS) |
| **NUTEXB** | `.nutexb` | *Super Smash Bros. Ultimate* (Switch) |
| **TEX** | `.tex` | *Excite Truck*, *ExciteBots: Trick Racing* GX textures (Wii) |
| **TEX0** | `.tex0` | *Mario Kart Wii*, *Super Smash Bros. Brawl*, *Super Mario Galaxy* (Wii) |

---

### Audio, Sound & Music

| Format | Extensions | Games / Platforms Using This Format |
|---|---|---|
| **BCSAR / BCWAR / BCWAV** | `.bcsar`, `.bcwar`, `.bcwav` | *Mario Kart 7*, *Super Mario 3D Land*, *Luigi's Mansion: Dark Moon*, *Kid Icarus: Uprising* (3DS) |
| **BFSAR / BFWAR / BFWAV** | `.bfsar`, `.bfwar`, `.bfwav` | *Super Mario Odyssey*, *Mario Kart 8 / Deluxe*, *Zelda: BotW*, *Splatoon 1/2/3* (Wii U / Switch) |
| **BRSAR / RBNK / RWAV** | `.brsar`, `.rbnk`, `.rwav` | *Mario Kart Wii*, *Super Smash Bros. Brawl*, *Wii Sports*, *Super Mario Galaxy 1 & 2* (Wii) |
| **BRSTM / BCSTM / BFSTM** | `.brstm`, `.bcstm`, `.bfstm` | Multi-channel audio streams across Wii, 3DS, Wii U, and Switch titles |
| **RSEQ / CSEQ / FSEQ / SSEQ** | `.rseq`, `.cseq`, `.fseq`, `.sseq` | Sequence music and MIDI across Wii, 3DS, Wii U, Switch, and DS |
| **SDAT** | `.sdat` | *Pokémon Platinum/HGSS/BW/BW2*, *Mario Kart DS*, *New Super Mario Bros.*, *Chrono Trigger DS* (DS) |

---

### Layouts, Text & Game Data

| Format | Extensions | Games / Platforms Using This Format |
|---|---|---|
| **BCLYT / BCLAN** | `.bclyt`, `.bclan` | *Mario Kart 7*, *Animal Crossing: New Leaf*, *Super Mario 3D Land* (3DS) |
| **BFLYT / BFLAN** | `.bflyt`, `.bflan` | *Mario Kart 8*, *Super Mario 3D World*, *Splatoon*, *Pikmin 3* (Wii U) |
| **BMG** | `.bmg` | *Mario Kart Wii*, *Super Mario Galaxy*, *Zelda: Twilight Princess* (Wii / GC) |
| **BRLYT / BRLAN** | `.brlyt`, `.brlan` | *Mario Kart Wii*, *Super Smash Bros. Brawl*, *New Super Mario Bros. Wii*, *Wii Sports Resort* (Wii) |
| **BYAML / BYML** | `.byaml`, `.byml` | *Super Mario Galaxy 1 & 2*, *Super Mario 3D World*, *Super Mario Odyssey*, *Zelda: BotW* |
| **MSBT / MSBP / MSBF** | `.msbt`, `.msbp`, `.msbf` | *Animal Crossing: New Leaf / New Horizons*, *Zelda: BotW*, *Mario Kart 8 / Deluxe*, *Super Mario Odyssey* |

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
