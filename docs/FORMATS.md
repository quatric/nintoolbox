# Supported Formats & Technical Reference

This document contains detailed technical notes, reverse-engineering findings, and the complete format capability registry for **Wiimms SZS Tools Plus**.

---

## Format Capability Index

| Format | Platform / Category | Decode | Encode | Notes |
|---|---|---|---|---|
| **AJJPG / AJPG** | GBA / Still Image | ✅ | ✅ | GBA-era still image container |
| **ADJB** | Switch / Model sidecar | ✅ | ❌ | Smash Ultimate mesh triangle adjacency (model.adjb): per-mesh id + u16 index lists, sized like Adjb.cs |
| **ALAR** | DS / Archive | ✅ | ✅ | Nitro ALAR archive |
| **ALZ1** | DS / Compression | ✅ | ✅ | Arika 4096-byte window LZSS with inverted flag bits |
| **Arika (INFO.DAT/GAME.DAT)** | DS/DSi / Archive | ✅ | ✅ | Obfuscated directory decryption and member decompression |
| **ARCV** | Wii / Archive | ✅ | ✅ | Pac-Man Party (Wii) archive; byte-exact round-trip |
| **ART / IMG** | Wii / Texture | ✅ | ✅ | Monster Games GUI image format |
| **ASH0** | GameCube/Wii / Compression | ✅ | ✅ | Nintendo ASH0 compression (System Menu, Animal Crossing, My Pokémon Ranch; automatic 11/15-bit distance tree fallback) |
| **AT7** | PS2/Wii / Archive | ✅ | ✅ | Koei Tecmo container |
| **BCFNT** | 3DS / Font | ✅ | ✅ | 3DS bitmap font to PNG atlas |
| **BCH** | 3DS / Model | ✅ | ✅ | CTR H3D model container |
| **BCLAN** | 3DS / Layout | ✅ | ✅ | 3DS layout animation |
| **BCLIM** | 3DS / Texture | ✅ | ✅ | CTR image container |
| **BCLYT** | 3DS / Layout | ✅ | ✅ | 3DS binary layout |
| **BCRES** | 3DS / Model | ✅ | ✅ | CTR graphics container |
| **BCSAR** | 3DS / Audio Archive | ✅ | ✅ | CTR Sound Archive (CSAR) |
| **BCWAV** | 3DS / Audio | ✅ | ✅ | CTR Sound Wave |
| **BCWAR** | 3DS / Audio Archive | ✅ | ✅ | CTR Sound Wave Archive (CWAR) |
| **BCGRP** | 3DS / Audio Archive | ✅ | ✅ | CTR Sound Group Archive (CGRP) |
| **BFFNT** | Wii U / Font | ✅ | ✅ | Wii U bitmap font to PNG atlas |
| **BG4** | 3DS / Archive | ✅ | ✅ | Mario & Luigi flat archive with BLZ member compression |
| **BIGF** | Wii / Archive | ✅ | 🟡 | EA BIGF container (encode round-trips losslessly; not a byte-exact retail reproduction — no retail `.big` sample to derive member ordering/padding from) |
| **BFLAN** | Wii U / Layout | ✅ | ✅ | Wii U layout animation |
| **BFLIM** | Wii U / Texture | ✅ | ✅ | Wii U textures (BC1-BC5) |
| **BFLYT** | Wii U / Layout | ✅ | ✅ | Wii U binary layout |
| **BFRES** | Wii U / Switch / Model | ✅ | ✅ | GX2/NX model container (passthrough / extraction) |
| **BFSAR** | Wii U / Switch / Audio Archive | ✅ | ✅ | Sound Archive (FSAR) |
| **BFWAV** | Wii U / Switch / Audio | ✅ | ✅ | Sound Wave |
| **BFWAR** | Wii U / Switch / Audio Archive | ✅ | ✅ | Sound Wave Archive (FWAR) |
| **BFGRP** | Wii U / Switch / Audio Archive | ✅ | ✅ | Sound Group Archive (FGRP) |
| **BLZ** | DS / Compression | ✅ | ✅ | Nitro backward-LZSS |
| **BPE / GFCP** | Wii / Compression | ✅ | ✅ | Good-Feel Byte Pair Encoding (GFAC mode 1) |
| **BMD** | DS / Model | ✅ | ✅ | Early Nitro 3D models |
| **J3DBMD** | GameCube / Wii / Model | ✅ | ✅ | J3D binary model (`J3D2bmd3`, legacy `bmd2`); SuperBMD-compatible GLB round-trip (geometry, skinning, materials, GX textures incl. mipmaps; canonical single-TEV encode, RGBA32/CMPR, triangle lists) + material/texheader JSON sidecars + `--profile` |
| **J3DBDL** | GameCube / Wii / Model | ✅ | ✅ | J3D binary display list (`J3D2bdl4`); same coverage as J3DBMD, MDL3 section written as a parseable stub (prefer BMD for in-game use) |
| **BNTX** | Switch / Texture | ✅ | ✅ | Switch texture container (Tegra block-linear); native DDS/ASTC block export |
| **BREFT** | Wii / Texture | ✅ | ✅ | Brawl effect texture |
| **BRFNA / BRFNT** | Wii / Font | ✅ | ✅ | NW4R font to PNG atlas + XML metrics |
| **BRLAN / BRLYT** | Wii / Layout | ✅ | ✅ | NW4R layout and animations |
| **BRRES (MDL0, TEX0)** | Wii / Graphics | ✅ | ✅ | NW4R models, textures, animations |
| **BRSAR** | Wii / Audio Archive | ✅ | ✅ | NW4R sound archive |
| **BRSTM / BFSTM / BCSTM** | Wii/Wii U/3DS / Audio Stream | ✅ | ✅ | Multichannel ADPCM/PCM streams |
| **BYAML / BYML** | Wii U / Switch / Data | ✅ | ✅ | Binary YAML format (versions 1–7, big- and little-endian, string/path/hash tables incl. reloc maps); path tables ride any version, not just v1; Yaz0-wrapped BotW containers (`.sbyml`, `.smubin`, …) decode transparently and re-compress on CREATE; encoder honors `.be`/`.vN` dest markers and `# byml version=N endian=..` headers; `wszst BYMLFIND` searches keys/values |
| **CA01 / SA01** | 3DS / Wii U / Archive | ✅ | ✅ | Mii Maker & amiibo settings flat archives |
| **CCF** | Wii / Switch / Archive | ✅ | ✅ | Virtual Console container |
| **CGFX** | 3DS / Model | ✅ | ✅ | CTR NW4C model container |
| **CRAM (.arc)** | 3DS / Archive | ✅ | ✅ | Xenoblade Chronicles 3D archive |
| **CSB** | Switch/Wii U / Collision | ✅ | ✅ | Paper Mario collision scene (TTYD/Origami King LE, Color Splash BE): triangle meshes with material/collision flags + sphere/box trigger volumes, GLB round-trip with generated `.ctb` |
| **CS_DCT** | Wii / Archive + Graphics | ✅ | ❌ | *Chicken Shoot* (Wii) stage archive (`.dct`): background planes (LZ-compressed indexed pixels + RGB555 palette) → PNG, animated sprites with per-frame bounding geometry → PNG, object placement metadata → `manifest.json` and raw binaries |
| **CTB** | Switch/Wii U / Collision | ✅ | ❌ | Paper Mario collision search table (XZ-quadtree over `.csb` triangles; regenerated on encode, inspected with `wmdlt CAT`) |
| **CTPK** | 3DS / Texture | ✅ | ✅ | CTR texture container |
| **DARC** | 3DS / Archive | ✅ | ✅ | Differential archive container |
| **DSB (TXTR)** | DS / Texture | ✅ | ✅ | Animal Crossing: Wild World menu texture (RGB555 palette + A3I5 texels); retail regression covers decode → encode → decode pixels |
| **DSP** | GameCube / Wii / Audio | ✅ | ❌ | Nintendo GameCube/Wii DSP-ADPCM standalone audio stream (`.dsp`): mono/stereo ADPCM coefficients, nibble predictor/scale decoding → 16-bit PCM WAV |
| **Retro TXTR** | Wii / Texture | ✅ | ✅ | Retro Studios texture (*Metroid Prime 1-3*, *DKCR*): GX-tiled, indexed + direct |
| **Tropical TXTR** | Wii U / Texture | ✅ | ❌ | Retro Studios texture (*Tropical Freeze*): RFRM form, GX2 detile |
| **DAT (Star Fox Zero)** | Wii U / Archive | ✅ | 🟡 | Big-endian flat archive |
| **Deflate** | Compression | ✅ | ✅ | Standard Deflate / Zlib streams |
| **DTLS (dt00/ls00)** | Wii U / 3DS / Archive | ✅ | ✅ | Super Smash Bros. 4 composite resource package & lookup (Wii U `of02` 16-byte + 3DS `of01` 12-byte retail variants included) |
| **FSYS** | GameCube / Archive | ✅ | ✅ | Genius Sonority Pokémon archive |
| **FZIP** | Wii U / Compression | ✅ | ✅ | Game & Wario Zlib container |
| **GFA** | 3DS / Archive | ✅ | ✅ | GFAC archive |
| **GF1MOT** | 3DS / Animation | ✅ | ❌ | Game Freak XY/ORAS bone-motion pack (SPICA GF1MotionPack): skeleton + per-anim frame/octet listing as text |
| **GFLX** | Switch / Archive | ✅ | ❌ | Game Freak GFLXPack archive (SPICA GFLXPack): raw-LZ4 members, magic-sniffed extensions |
| **GFMOT** | 3DS / Animation | ✅ | ❌ | Game Freak skeletal/material/visibility motion (SPICA GFMotion): frames, tracks and constants as text |
| **GFMPACK** | 3DS / Archive | ✅ | ❌ | Game Freak model/texture/shader pack (SPICA GFModelPack): named members with sniffed extensions |
| **GFMODEL** | 3DS / Model | ✅ | ❌ | Game Freak GFL2 3D model (SPICA GFModel, Pokémon X/Y/ORAS): skeleton, materials with texture names, PICA200 command-buffer geometry with smooth skinning → GLB |
| **GFPKG** | 3DS / Archive | ✅ | ❌ | Game Freak Gen6/Gen7 package (SPICA GFPackage): offset-table members with sniffed extensions |
| **GFTEX** | 3DS / Texture | ✅ | ❌ | Game Freak GFL2 texture (SPICA GFTexture): PICA200 payloads incl. ETC1 → PNG |
| **MBN** | 3DS / Model | ✅ | ❌ | ModelBinary companion buffers (SPICA MBn): replacement vertex/index buffers applied onto the sibling `.bch` base scene → GLB; descriptor listing as text standalone |
| **MTMFX** | 3DS / Shader | ✅ | ❌ | Capcom MT Framework Mobile shader effects (SPICA MTShaderEffects): input-layout table (vertex attribute names/formats/offsets) as text; layouts drive `wmdlt` MOD decoding |
| **MTMOD** | 3DS / Model | ✅ | ❌ | Capcom MT Framework Mobile model (SPICA MTModel): skeleton always; vertices via sibling `.mfx`/`.lfx` input layouts (else validated float fallback); materials/textures via sibling `.mrl` → GLB |
| **MTMRL** | 3DS / Material | ✅ | ❌ | Capcom MT Framework Mobile materials (SPICA MTMaterials): CRC32-keyed texture bindings as text |
| **MTTEX** | 3DS / Texture | ✅ | ❌ | Capcom MT Framework Mobile texture (SPICA MTTexture): PICA200 payloads incl. ETC1 → PNG |
| **GTX** | Wii U / Texture | ✅ | ✅ | Wii U GX2 texture container |
| **GSH** | Wii U / Shader | ✅ | ✅ | Wii U Latte GPU shader container |
| **HSD (.dat)** | GameCube / Model | ✅ | ✅ | HAL Laboratory sysdolphin object graph; also archive bundles (many archives in one file, `scene_data` roots, e.g. Doraemon `map*_dat.mdl`) merged into one GLB |
| **HSF** | GameCube / Wii / Model | ✅ | ✅ | Hudson Mario Party 3D model |
| **IPK** | Wii / Wii U / Switch / Archive | ✅ | ❌ | Ubisoft UbiArt archive (*Just Dance*, *Rayman Origins/Legends*): stored + zlib/LZMA members, old/new path-name orders |
| **WADH** | Wii / Archive | ✅ | ❌ | Data Design Interactive `DataWII.wad` (*Ninjabread Man*): stored members, directory tree via last-child / previous-sibling links |
| **DC2 DCX / DCT** | Wii / Archive + Texture | ✅ | ❌ | *Jakers! Kart Racing*: `.dcx` directory archives (backslash paths, contiguous members); `.dct` GX CMPR / RGBA8 textures (top mip) → PNG |
| **NIF (Gamebryo 20.6, Wii)** | Wii / Model + Texture | ✅ | ❌ | *Pocoyo Racing*: big-endian `.nif` scene graph; `NiMesh` GX display-list / INDEX streams → GLB in world space (skinned meshes in bind pose, hidden collision meshes only when nothing else exists); embedded DXT1/RGBA `NiPersistentSrcTextureRendererData` → PNG |
| **Grip RES** | Wii / Archive + Texture | ✅ | ❌ | *Sesame Street: Elmo's Musical Monsterpiece*: `res\n` serialised resource packages split into per-section files (`strings.txt`, `index.txt`, `.fsb` sound banks, compiled Lua, raw object sections); `surf` textures (CMPR / RGB5A3 / I8, bottom-up rows) → PNG. Model object graphs (`body`/`bmsh`/`gshd`) are pointer-linked and stay as raw sections |
| **CDGaCube CAR** | Wii / Archive | ✅ | ❌ | Cat Daddy Games `birthday.CAR` (*Birthday Party Bash*): 24-byte records, 2048-byte sector offsets, trailing path table; members stored raw or as bare zlib streams (inflated on extract) |
| **Terminal Reality POD** | Wii / Archive | ✅ | ❌ | POD3 / POD4 / POD5 (*Nickelodeon Dance* `WII*.POD`): stored members with backslash paths (converted to `/`); compressed POD4 members are skipped. Layout after jopadan/termpod |
| **Heavy Iron HO** | Wii / Archive | ✅ | ❌ | Good Engine `.ho` packages (*WALL-E*, *Up*, also *Ratatouille* on other platforms): big-endian MAST / SECT / PSL / PSLD tables; assets written as `name.<type hash>` with debug names from PSLD; GX texture blobs (RGB5A3 / RGBA8 / CMPR) → PNG. The `.lo` text log beside each package lists asset type names |
| **Asobo BigFile** | Wii / Archive | ✅ | ❌ | Asobo Studio "Internal Cross Technology" `.DRV` volumes (*Ratatouille*): big-endian v1.06.63 blocks, stored or LZRS members, hashed names (Asobo CRC) with class names (`Bitmap_Z`, `Mesh_Z`, ...); `Bitmap_Z` (RGB565 / RGBA8 / CMPR) → PNG; `Sound_Z` (Nintendo DSP-ADPCM with a standard DSP header) → WAV; `Mesh_Z` (Wii GX display lists over s16 positions / s16 UVs / s8 normals) → GLB with one primitive per material and the material's first bitmap as texture (static bind pose; `Skin_Z` skinning and `Node_Z` transforms are not applied). Other classes stay raw. Format after widberg/bff |
| **FMOD FSB** | Wii / Audio bank | ✅ | ❌ | FSB4 (*Up*, *WALL-E*, *Cars 2*, *Birthday Party Bash*; incl. basic-header banks) and FSB5 (*Brave*): DSP-ADPCM (mono / stereo, 2-byte interleave) and PCM16 samples → WAV; other codecs copied out as `.bin`. FSB3 not handled |
| **Avalanche THB/TBB** | Wii / Texture | ✅ | ❌ | Avalanche Software engine textures (*Cars 2*, inside the game's `.zip` / `.tszip` archives): big-endian `.thb` header (one or many textures: offset table + 32-byte records) + tiled GX `.tbb` pixels (I8 / RGB5A3 / RGBA8 / CMPR) → PNG; atlases write `name_0.png`, `name_1.png`, ... |
| **Blitz REV** | Wii / GameCube / Archive | ✅ | ❌ | Blitz Games "Babel" `.rev` (Wii) and `.gcp` (GameCube) packages (*SpongeBob SquarePants: Creature from the Krusty Krab*, `Packages_Rev/` + `AudioRev/`): CRC-keyed index with names matched by CRC; textures (RGBA8 / RGB5A3 / RGB565 / CI4 / CI8 / CMPR / I4 / I8) → PNG; static and soft-skinned actors → GLB (bind pose, no bones) |
| **Cars GCT / GCG** | GameCube / Wii / Texture + Model | ✅ | ❌ | Rainbow Studios engine (*Disney-Pixar Cars*; assets ship loose on GameCube, inside U8 `.arc` on Wii): `.gct` CMPR / CI8 textures → PNG; `.gcg` indexed-strip geometry → GLB with the texture named by the material's `.gcm`. Multi-object animation `.gcg` files are recognised but not converted |
| **Hyrule Warriors Legends** | 3DS / Archive | ✅ | ✅ | Split `.idx` / `.bin` archive pair |
| **BFSHA** | Wii U / Switch / Shader Archive | ✅ | ❌ | NintendoWare shader archive (FSHA): per-model option choice values, sampler extras, uniform-block type names; embedded `.bnsh` sidecars |
| **BNSH** | Switch / Shader | ✅ | ❌ | NintendoWare binary shader: dual source/binary programs per variation, zlib-compressed stages, per-program MemoryData; stage blobs extract as `.bin` / `.glsl` / `.zlib.bin` sidecars |
| **MPR CMDL / SMDL / WMDL** | Switch / Model | ✅ | ❌ | Retro Studios static CMDL, skinned SMDL, and world WMDL models (*Metroid Prime Remastered*, *DKCTF*) |
| **MPR SKEL** | Switch / Skeleton | ✅ | ❌ | Retro Studios skeletal hierarchy (*Metroid Prime Remastered*, *DKCTF*) |
| **MPR PACK** | Switch / Archive | ✅ | ❌ | Retro Studios asset container (*Metroid Prime Remastered*): LE RFRM PACK v1 + TOCC v3, LZSS members |
| **MPR TXTR** | Switch / Texture | ✅ | ✅ | Retro Studios texture (*Metroid Prime Remastered*): LE RFRM TXTR v47/51, Tegra detile, BC1-7/ASTC; `wimgt ENCODE` writes single-mip RGBA8 with a `.mpr.txtr` destination |
| **MSH (PMsh)** | Wii / Model | ✅ | ✅ | Monster Games collision mesh |
| **MSBF / MSBP / MSBT** | Wii/3DS/Wii U/Switch / Text | ✅ | ✅ | Message Studio Binary Text and Flow |
| **MSR** | 3DS / Archive | 🟡 | ⛔ | Metroid: Samus Returns archive |
| **MVDK** | DS / Compression | ✅ | ✅ | Mario vs. Donkey Kong Deflate/LZSS |
| **NDS / SRL / DSI** | DS / ROM Archive | ✅ | ✅ | Nitro ROM pass-through & unpacking |
| **NANR / NCER / NCGR / NCLR** | DS / 2D Graphics | ✅ | ✅ | Nitro 2D cell, sprites, palettes |
| **NCCARC** | DS / Archive | ✅ | ✅ | WarioWare: Touched! container |
| **NLG DICT** | 3DS / Switch / Archive | ✅ | ❌ | Next Level Games dictionary archive (*Metroid Prime: Federation Force*, *Luigi's Mansion 2 HD*, *Luigi's Mansion 3*, *Mario Strikers: Battle League Football*). Typed chunk pass: LM2/LM3/Federation Force models, textures, skeletons, animations, scripts, NLOC/font/config |
| **FEDMODEL / FEDSKEL** | Multi / Model | ✅ | ❌ | Next Level Games model/skeleton containers (extractor intermediates, versions 1..3) → GLB |
| **FEDTEX** | Multi / Texture | ✅ | ❌ | Next Level Games texture container (extractor intermediate: PICA / Switch RGBA8/BCn/ASTC) → PNG |
| **SANIM** | Wii / Animation | ✅ | ❌ | Mario Strikers skeleton animation stream → text dump |
| **NSBMD / NSBTX** | DS / 3D Graphics | ✅ | ✅ | Nitro 3D models and textures |
| **NUD** | Wii U / 3DS / Model | ✅ | ✅ | Bandai Namco 3D model container (Smash 4 NDP3/NDWU and Pokkén NDWD multi-mesh) |
| **NUMATB** | Switch / Material | ✅ | ❌ | Bandai Namco SSBH material container (Smash Ultimate): text manifest plus MatLab-dialect MaterialLibrary XML |
| **NUANMB** | Switch / Animation | ✅ | ❌ | Bandai Namco SSBH skeletal/material animation (Smash Ultimate): group/node/track structure plus decoded Direct/Constant/Compressed keyframe payloads |
| **NUMSHB** | Switch / Model | ✅ | ❌ | Bandai Namco SSBH 3D mesh model (Smash Ultimate) |
| **TTMODEL** | Multi / Model | ✅ | ❌ | TT Games NTT engine model (*LEGO Star Wars: The Skywalker Saga*, `.model` → GLB) |
| **SHARC / SHARCFB** | Wii U / Switch / Shader Archive | ✅ | ✅ | NintendoWare shader source and binary archive (SHARC v10-12 round-trips; Wii U SHARCFB is compiled from GSH) |
| **VFXB** | Wii U / Switch / Effect Archive | ✅ | ❌ | NintendoWare particle effect binary archive |
| **NUT** | Wii U / 3DS / Texture | ✅ | ❌ | Bandai Namco texture package (Smash 4) |
| **NUTEXB** | Switch / Texture | ✅ | ✅ | Super Smash Bros. Ultimate texture container |
| **PAC** | Wii / Archive | ✅ | ✅ | Super Smash Bros. Brawl archive |
| **PLT0** | Wii / Animation | ✅ | ✅ | NW4R palette animation |
| **PRC** | Switch / Param | ✅ | ❌ | Smash Ultimate "paracobn" parameter binary to ParamXML-dialect XML (bool/sbyte/byte/short/ushort/int/uint/float/hash40/string/list/struct); Smash 4 era variants recognised |
| **PSDK** | Wii / Compression | ✅ | ✅ | Prosonic SDK LZSS container |
| **QuickLZ** | Compression | ✅ | ✅ | QLZ 1.20 and 1.4.0 streams |
| **RARC** | GameCube / Wii / Archive | ✅ | ✅ | Nintendo standard resource archive |
| **RFL_Res.dat** | Wii/3DS/Wii U / Mii Database | ✅ | ✅ | Revolution Face Library resource container |
| **RNC1 / RNC2** | Compression | ✅ | ✅ | ProPack compression |
| **RSEQ / CSEQ / FSEQ / SSEQ** | Wii/3DS/Wii U/DS / Sequence | ✅ | ✅ | Music sequence MML / MIDI |
| **SDAT** | DS / Sound Archive | ✅ | ✅ | Nitro Sound Archive |
| **SMDH** | 3DS / Metadata | ✅ | ✅ | Application icon & title metadata |
| **SSZL / VCRA** | Wii / Compression & Archive | ✅ | ✅ | Namco Museum Remix container |
| **TEX** | Wii / Texture | ✅ | ✅ | Monster Games GX texture |
| **TMPK** | Wii U / Archive | ✅ | ✅ | *The Legend of Zelda: Twilight Princess HD* flat archive. Verified against the retail `content/Shaders.pack.gz`: a plain gzip stream wraps a 10,402,512-byte TMPK archive that extracts to exactly 1568 non-empty members |
| **WARC** | Wii U / Archive | ✅ | ✅ | Game & Wario flat archive |
| **WUD / WUX** | Wii U / Disc Image | ✅ | ✅ | Wii U disc extraction & compression |
| **XTX** | Switch / Texture | ✅ | ❌ | Nintendo Switch intermediate texture container (`DFvN`/`HBvN`, Tegra block-linear RGBA8/BC1-BC7/ASTC; synthetic fixture, no retail sample) |
| **XMB** | Switch / Metadata | ✅ | ❌ | Smash XMB material/LOD metadata: node/property tables to XMBDec-mapping XML |
| **Yay0 / Yaz0** | Compression | ✅ | ✅ | Nintendo standard LZ77 compression |
