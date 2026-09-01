# Supported Formats & Technical Reference

This document contains detailed technical notes, reverse-engineering findings, and the complete format capability registry for **Wiimms SZS Tools Plus**.

---

## Format Capability Index

| Format | Platform / Category | Decode | Encode | Notes |
|---|---|---|---|---|
| **AJJPG / AJPG** | GBA / Still Image | ✅ | ✅ | GBA-era still image container |
| **ALAR** | DS / Archive | ✅ | ✅ | Nitro ALAR archive |
| **ALZ1** | DS / Compression | ✅ | ✅ | Arika 4096-byte window LZSS with inverted flag bits |
| **Arika (INFO.DAT/GAME.DAT)** | DS/DSi / Archive | ✅ | ✅ | Obfuscated directory decryption and member decompression |
| **ARCV** | Wii / Archive | ✅ | ❌ | Pac-Man Party (Wii) archive |
| **ART / IMG** | Wii / Texture | ✅ | ✅ | Monster Games GUI image format |
| **ASH0** | GameCube/Wii / Compression | ✅ | ✅ | Nintendo ASH0 compression |
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
| **BIGF** | Wii / Archive | ✅ | ⛔ | EA BIGF container |
| **BFLAN** | Wii U / Layout | ✅ | ✅ | Wii U layout animation |
| **BFLIM** | Wii U / Texture | ✅ | ✅ | Wii U textures (BC1-BC5) |
| **BFLYT** | Wii U / Layout | ✅ | ✅ | Wii U binary layout |
| **BFRES** | Wii U / Switch / Model | ✅ | ✅ | GX2/NX model container -> GLB/DAE |
| **BFSAR** | Wii U / Switch / Audio Archive | ✅ | ✅ | Sound Archive (FSAR) |
| **BFWAV** | Wii U / Switch / Audio | ✅ | ✅ | Sound Wave |
| **BFWAR** | Wii U / Switch / Audio Archive | ✅ | ✅ | Sound Wave Archive (FWAR) |
| **BFGRP** | Wii U / Switch / Audio Archive | ✅ | ✅ | Sound Group Archive (FGRP) |
| **BLZ** | DS / Compression | ✅ | ✅ | Nitro backward-LZSS |
| **BMD** | DS / Model | ✅ | ✅ | Early Nitro 3D models |
| **BNTX** | Switch / Texture | ✅ | ✅ | Switch texture container (Tegra block-linear) |
| **BREFT** | Wii / Texture | ✅ | ✅ | Brawl effect texture |
| **BRFNA / BRFNT** | Wii / Font | ✅ | ✅ | NW4R font to PNG atlas + XML metrics |
| **BRLAN / BRLYT** | Wii / Layout | ✅ | ✅ | NW4R layout and animations |
| **BRRES (MDL0, TEX0)** | Wii / Graphics | ✅ | ✅ | NW4R models, textures, animations |
| **BRSAR** | Wii / Audio Archive | ✅ | ✅ | NW4R sound archive |
| **BRSTM / BFSTM / BCSTM** | Wii/Wii U/3DS / Audio Stream | ✅ | ✅ | Multichannel ADPCM/PCM streams |
| **BYAML / BYML** | Wii U / Switch / Data | ✅ | ✅ | Binary YAML format |
| **CA01 / SA01** | 3DS / Wii U / Archive | ✅ | ✅ | Mii Maker & amiibo settings flat archives |
| **CCF** | Wii / Switch / Archive | ✅ | ✅ | Virtual Console container |
| **CGFX** | 3DS / Model | ✅ | ✅ | CTR NW4C model container |
| **CRAM (.arc)** | 3DS / Archive | ✅ | ✅ | Xenoblade Chronicles 3D archive |
| **CTPK** | 3DS / Texture | ✅ | ✅ | CTR texture container |
| **DARC** | 3DS / Archive | ✅ | ✅ | Differential archive container |
| **DAT (Star Fox Zero)** | Wii U / Archive | ✅ | 🟡 | Big-endian flat archive |
| **Deflate** | Compression | ✅ | ✅ | Standard Deflate / Zlib streams |
| **FSYS** | GameCube / Archive | ✅ | ✅ | Genius Sonority Pokémon archive |
| **FZIP** | Wii U / Compression | ✅ | ✅ | Game & Wario Zlib container |
| **GFA** | 3DS / Archive | ✅ | ✅ | GFAC archive |
| **GTX** | Wii U / Texture | ✅ | ✅ | Wii U GX2 texture container |
| **GSH** | Wii U / Shader | ✅ | ✅ | Wii U Latte GPU shader container |
| **HSD (.dat)** | GameCube / Model | ✅ | ✅ | HAL Laboratory sysdolphin object graph |
| **HSF** | GameCube / Wii / Model | ✅ | ✅ | Hudson Mario Party 3D model |
| **Hyrule Warriors Legends** | 3DS / Archive | ✅ | ✅ | Split `.idx` / `.bin` archive pair |
| **MOD (NDL3/NDL2)** | Wii / Model | ✅ | ✅ | Monster Games 3D model container |
| **MSH (PMsh)** | Wii / Model | ✅ | ✅ | Monster Games collision mesh |
| **MSBF / MSBP / MSBT** | Wii/3DS/Wii U/Switch / Text | ✅ | ✅ | Message Studio Binary Text and Flow |
| **MSR** | 3DS / Archive | 🟡 | ⛔ | Metroid: Samus Returns archive |
| **MVDK** | DS / Compression | ✅ | ✅ | Mario vs. Donkey Kong Deflate/LZSS |
| **NDS / SRL / DSI** | DS / ROM Archive | ✅ | ✅ | Nitro ROM pass-through & unpacking |
| **NANR / NCER / NCGR / NCLR** | DS / 2D Graphics | ✅ | ✅ | Nitro 2D cell, sprites, palettes |
| **NCCARC** | DS / Archive | ✅ | ✅ | WarioWare: Touched! container |
| **NSBMD / NSBTX** | DS / 3D Graphics | ✅ | ✅ | Nitro 3D models and textures |
| **NUTEXB** | Switch / Texture | ✅ | ✅ | Super Smash Bros. Ultimate texture container |
| **PAC** | Wii / Archive | ✅ | ✅ | Super Smash Bros. Brawl archive |
| **PLT0** | Wii / Animation | ✅ | ✅ | NW4R palette animation |
| **PSDK** | Wii / Compression | ✅ | ✅ | Prosonic SDK LZSS container |
| **QuickLZ** | Compression | ✅ | ✅ | QLZ 1.20 and 1.4.0 streams |
| **RARC** | GameCube / Wii / Archive | ✅ | ✅ | Nintendo standard resource archive |
| **RFL_Res.dat** | Wii/3DS/Wii U / Mii Database | ✅ | ✅ | Revolution Face Library resource container |
| **RNC1 / RNC2** | Compression | ✅ | ✅ | ProPack compression |
| **RSEQ / CSEQ / FSEQ / SSEQ** | Wii/3DS/Wii U/DS / Sequence | ✅ | ✅ | Music sequence MML / MIDI |
| **SDAT** | DS / Sound Archive | ✅ | ✅ | Nitro Sound Archive |
| **SMDH** | 3DS / Metadata | ✅ | ✅ | Application icon & title metadata |
| **SSZL / VCRA** | Wii / Compression & Archive | ✅ | ✅ | Namco Museum Remix container |
| **SZE** | Switch / Encrypted Archive | ✅ | ✅ | F-Zero 99 AES-encrypted container |
| **TEX** | Wii / Texture | ✅ | ✅ | Monster Games GX texture |
| **WARC** | Wii U / Archive | ✅ | ✅ | Game & Wario flat archive |
| **WUD / WUX** | Wii U / Disc Image | ✅ | ✅ | Wii U disc extraction & compression |
| **Yay0 / Yaz0** | Compression | ✅ | ✅ | Nintendo standard LZ77 compression |

