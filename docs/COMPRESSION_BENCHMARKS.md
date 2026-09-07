# Compression Formats & Multi-Corpus Benchmark

This document presents the compressed output file sizes, compression ratios, and platform usage contexts for all compression formats baked into `wiimms-szs-tools-plus` across various text corpora.

## Green Eggs and Ham (Dr. Seuss)
- **Original File Size:** `3,472 bytes`

| Compression Format / Codec | Output Size | Compression Ratio | Space Saved (%) | Where It Is Used / Platform Context |
|---|---|---|---|---|
| **ALZ1 (Mario Party / Bomberman LZ77)** | 894 B | 25.75% | 74.25% | Hudson Soft Mario Party / Bomberman (GameCube / Wii) |
| **ASH0 (Nintendo Huffman+LZSS)** | 1,032 B | 29.72% | 70.28% | Wii System Menu, Animal Crossing: City Folk, My Pokémon Ranch |
| **BLZ (DS Nitro ARM9 backward LZ)** | 922 B | 26.56% | 73.44% | Nintendo DS Nitro ARM9/ARM7 executable overlays |
| **BPE / GFCP (Good-Feel Byte Pair)** | 894 B | 25.75% | 74.25% | Good-Feel Kirby's Epic Yarn, Yoshi's Woolly World (Wii) |
| **Bzip2 (Raw stream)** | 717 B | 20.65% | 79.35% | Standard high-compression block-sorting stream format |
| **BZ / WBZ (Wiimms Bzip2 Container)** | 733 B | 21.11% | 78.89% | Wiimms track distribution container (Mario Kart Wii) |
| **Camelot LZ** | 907 B | 26.12% | 73.88% | Camelot Mario Golf, Mario Tennis, We Love Golf! (GC / Wii) |
| **Deflate / Zlib (RFC 1950)** | 728 B | 20.97% | 79.03% | Nintendo standard zlib streams (Wii U / 3DS / Switch) |
| **Raw Deflate (RFC 1951)** | 722 B | 20.79% | 79.21% | Raw LZ77+Huffman deflate streams without wrappers |
| **Diff8 (Nintendo DS Delta Filter 8)** | 894 B | 25.75% | 74.25% | Nintendo DS differential delta filter encoding (8-bit) |
| **Diff16 (Nintendo DS Delta Filter 16)** | 894 B | 25.75% | 74.25% | Nintendo DS differential delta filter encoding (16-bit) |
| **FZIP (Game & Wario Zlib)** | 736 B | 21.20% | 78.80% | Game & Wario Zlib stream container (Wii U) |
| **Huffman 4-bit (Nintendo DS)** | 3,081 B | 88.74% | 11.26% | Nintendo DS Nitro SDK compact 4bpp graphic streams |
| **Huffman 8-bit (Nintendo DS)** | 2,021 B | 58.21% | 41.79% | Nintendo DS Nitro SDK font and sound resource trees |
| **LZ10 (Nintendo standard LZ77)** | 926 B | 26.67% | 73.33% | GameCube, Wii, Nintendo DS, GBA BIOS & game engines |
| **LZ11 (Nintendo extended LZSS / CMP)** | 940 B | 27.07% | 72.93% | Nintendo DS, DSi, 3DS, HAL Laboratory (Kirby) |
| **LZ4 (Standard frame stream)** | 1,390 B | 40.03% | 59.97% | Modern high-throughput real-time stream compression (Switch / PC) |
| **LZMA (Raw stream)** | 752 B | 21.66% | 78.34% | Universal high-ratio Lempel-Ziv-Markov chain stream |
| **LZ / WLZ (Wiimms LZMA Container)** | 768 B | 22.12% | 77.88% | Wiimms LZMA track archive format |
| **LZH8 / LH** | 884 B | 25.46% | 74.54% | Hudson Soft / Retro game engines |
| **LZO / LZOvl (Nintendo DS Overlay)** | 894 B | 25.75% | 74.25% | Nintendo DS reverse LZO overlay compression |
| **LZX (Capcom Ace Attorney / Ghost Trick)** | 894 B | 25.75% | 74.25% | Capcom Ace Attorney & Ghost Trick (Nintendo DS) |
| **MVDK (Mario vs. Donkey Kong LZSS)** | 926 B | 26.67% | 73.33% | Nintendo Mario vs. Donkey Kong LZSS (Nintendo DS) |
| **PSDK / AT4PX (Pokémon Mystery Dungeon)** | 894 B | 25.75% | 74.25% | Chunsoft Pokémon Mystery Dungeon Explorers (Nintendo DS) |
| **PuCrunch (Nitro hybrid LZ+RLE)** | 894 B | 25.75% | 74.25% | Griptonite Games / Retro Nitro hybrid stream compression |
| **QuickLZ / QLZ (Level 1/3)** | 920 B | 26.50% | 73.50% | Fast byte-oriented realtime block compression |
| **RLE / RL (Nintendo Run-Length)** | 3,504 B | 100.92% | -0.92% | Nintendo DS / GBA simple graphic run-length format |
| **RNC1 (Rob Northen Method 1)** | 3,504 B | 100.92% | -0.92% | Rob Northen Computing ProPack Method 1 (Huffman) |
| **RNC2 (Rob Northen Method 2)** | 889 B | 25.60% | 74.40% | Rob Northen Computing ProPack Method 2 (Byte LZSS) |
| **SSZL (Bandai Namco Museum Remix)** | 894 B | 25.75% | 74.25% | Bandai Namco Museum Remix LZSS0 stream (Wii) |
| **VLX (Level-5 Layton / Inazuma)** | 894 B | 25.75% | 74.25% | Level-5 Professor Layton & Inazuma Eleven (Nintendo DS) |
| **Yay0 / SZP (Nintendo 64 / GameCube)** | 896 B | 25.81% | 74.19% | Super Mario 64, Sunshine, Zelda Wind Waker (N64 / GC) |
| **YAZ0 / SZS (Nintendo Standard)** | 894 B | 25.75% | 74.25% | Mario Kart Wii, Smash Bros. Brawl, Zelda: TP (GC / Wii) |
| **YAZ1 (Nintendo Variant)** | 894 B | 25.75% | 74.25% | Rare 1st-party Nintendo Yaz variant |
| **YBZ (Yaz0 + Bzip2)** | 733 B | 21.11% | 78.89% | Hybrid container (Yaz0 header + Bzip2 payload) |
| **YLZ (Yaz0 + LZMA)** | 768 B | 22.12% | 77.88% | Hybrid container (Yaz0 header + LZMA payload) |
| **XYZ (Disguised Yaz0)** | 894 B | 25.75% | 74.25% | Disguised / obfuscated custom track archives |
| **Zstandard / Zstd (Modern FSE)** | 863 B | 24.86% | 75.14% | Modern Nintendo Switch titles (F-Zero 99) & emulators |

---

## Bee Movie Script (Excerpt)
- **Original File Size:** `16,977 bytes`

| Compression Format / Codec | Output Size | Compression Ratio | Space Saved (%) | Where It Is Used / Platform Context |
|---|---|---|---|---|
| **ALZ1 (Mario Party / Bomberman LZ77)** | 4,165 B | 24.53% | 75.47% | Hudson Soft Mario Party / Bomberman (GameCube / Wii) |
| **ASH0 (Nintendo Huffman+LZSS)** | 4,532 B | 26.69% | 73.31% | Wii System Menu, Animal Crossing: City Folk, My Pokémon Ranch |
| **BLZ (DS Nitro ARM9 backward LZ)** | 4,300 B | 25.33% | 74.67% | Nintendo DS Nitro ARM9/ARM7 executable overlays |
| **BPE / GFCP (Good-Feel Byte Pair)** | 4,165 B | 24.53% | 75.47% | Good-Feel Kirby's Epic Yarn, Yoshi's Woolly World (Wii) |
| **Bzip2 (Raw stream)** | 2,981 B | 17.56% | 82.44% | Standard high-compression block-sorting stream format |
| **BZ / WBZ (Wiimms Bzip2 Container)** | 2,997 B | 17.65% | 82.35% | Wiimms track distribution container (Mario Kart Wii) |
| **Camelot LZ** | 4,218 B | 24.85% | 75.15% | Camelot Mario Golf, Mario Tennis, We Love Golf! (GC / Wii) |
| **Deflate / Zlib (RFC 1950)** | 3,181 B | 18.74% | 81.26% | Nintendo standard zlib streams (Wii U / 3DS / Switch) |
| **Raw Deflate (RFC 1951)** | 3,175 B | 18.70% | 81.30% | Raw LZ77+Huffman deflate streams without wrappers |
| **Diff8 (Nintendo DS Delta Filter 8)** | 4,165 B | 24.53% | 75.47% | Nintendo DS differential delta filter encoding (8-bit) |
| **Diff16 (Nintendo DS Delta Filter 16)** | 4,165 B | 24.53% | 75.47% | Nintendo DS differential delta filter encoding (16-bit) |
| **FZIP (Game & Wario Zlib)** | 3,189 B | 18.78% | 81.22% | Game & Wario Zlib stream container (Wii U) |
| **Huffman 4-bit (Nintendo DS)** | 14,237 B | 83.86% | 16.14% | Nintendo DS Nitro SDK compact 4bpp graphic streams |
| **Huffman 8-bit (Nintendo DS)** | 10,317 B | 60.77% | 39.23% | Nintendo DS Nitro SDK font and sound resource trees |
| **LZ10 (Nintendo standard LZ77)** | 4,210 B | 24.80% | 75.20% | GameCube, Wii, Nintendo DS, GBA BIOS & game engines |
| **LZ11 (Nintendo extended LZSS / CMP)** | 4,371 B | 25.75% | 74.25% | Nintendo DS, DSi, 3DS, HAL Laboratory (Kirby) |
| **LZ4 (Standard frame stream)** | 5,850 B | 34.46% | 65.54% | Modern high-throughput real-time stream compression (Switch / PC) |
| **LZMA (Raw stream)** | 3,035 B | 17.88% | 82.12% | Universal high-ratio Lempel-Ziv-Markov chain stream |
| **LZ / WLZ (Wiimms LZMA Container)** | 3,051 B | 17.97% | 82.03% | Wiimms LZMA track archive format |
| **LZH8 / LH** | 3,456 B | 20.36% | 79.64% | Hudson Soft / Retro game engines |
| **LZO / LZOvl (Nintendo DS Overlay)** | 4,165 B | 24.53% | 75.47% | Nintendo DS reverse LZO overlay compression |
| **LZX (Capcom Ace Attorney / Ghost Trick)** | 4,165 B | 24.53% | 75.47% | Capcom Ace Attorney & Ghost Trick (Nintendo DS) |
| **MVDK (Mario vs. Donkey Kong LZSS)** | 4,210 B | 24.80% | 75.20% | Nintendo Mario vs. Donkey Kong LZSS (Nintendo DS) |
| **PSDK / AT4PX (Pokémon Mystery Dungeon)** | 4,165 B | 24.53% | 75.47% | Chunsoft Pokémon Mystery Dungeon Explorers (Nintendo DS) |
| **PuCrunch (Nitro hybrid LZ+RLE)** | 4,165 B | 24.53% | 75.47% | Griptonite Games / Retro Nitro hybrid stream compression |
| **QuickLZ / QLZ (Level 1/3)** | 4,145 B | 24.42% | 75.58% | Fast byte-oriented realtime block compression |
| **RLE / RL (Nintendo Run-Length)** | 16,578 B | 97.65% | 2.35% | Nintendo DS / GBA simple graphic run-length format |
| **RNC1 (Rob Northen Method 1)** | 17,021 B | 100.26% | -0.26% | Rob Northen Computing ProPack Method 1 (Huffman) |
| **RNC2 (Rob Northen Method 2)** | 4,197 B | 24.72% | 75.28% | Rob Northen Computing ProPack Method 2 (Byte LZSS) |
| **SSZL (Bandai Namco Museum Remix)** | 4,165 B | 24.53% | 75.47% | Bandai Namco Museum Remix LZSS0 stream (Wii) |
| **VLX (Level-5 Layton / Inazuma)** | 4,165 B | 24.53% | 75.47% | Level-5 Professor Layton & Inazuma Eleven (Nintendo DS) |
| **Yay0 / SZP (Nintendo 64 / GameCube)** | 4,166 B | 24.54% | 75.46% | Super Mario 64, Sunshine, Zelda Wind Waker (N64 / GC) |
| **YAZ0 / SZS (Nintendo Standard)** | 4,165 B | 24.53% | 75.47% | Mario Kart Wii, Smash Bros. Brawl, Zelda: TP (GC / Wii) |
| **YAZ1 (Nintendo Variant)** | 4,165 B | 24.53% | 75.47% | Rare 1st-party Nintendo Yaz variant |
| **YBZ (Yaz0 + Bzip2)** | 2,997 B | 17.65% | 82.35% | Hybrid container (Yaz0 header + Bzip2 payload) |
| **YLZ (Yaz0 + LZMA)** | 3,051 B | 17.97% | 82.03% | Hybrid container (Yaz0 header + LZMA payload) |
| **XYZ (Disguised Yaz0)** | 4,165 B | 24.53% | 75.47% | Disguised / obfuscated custom track archives |
| **Zstandard / Zstd (Modern FSE)** | 3,607 B | 21.25% | 78.75% | Modern Nintendo Switch titles (F-Zero 99) & emulators |

---

## Alice in Wonderland (Lewis Carroll)
- **Original File Size:** `174,311 bytes`

| Compression Format / Codec | Output Size | Compression Ratio | Space Saved (%) | Where It Is Used / Platform Context |
|---|---|---|---|---|
| **ALZ1 (Mario Party / Bomberman LZ77)** | 81,325 B | 46.66% | 53.34% | Hudson Soft Mario Party / Bomberman (GameCube / Wii) |
| **ASH0 (Nintendo Huffman+LZSS)** | 76,968 B | 44.16% | 55.84% | Wii System Menu, Animal Crossing: City Folk, My Pokémon Ranch |
| **BLZ (DS Nitro ARM9 backward LZ)** | 81,302 B | 46.64% | 53.36% | Nintendo DS Nitro ARM9/ARM7 executable overlays |
| **BPE / GFCP (Good-Feel Byte Pair)** | 81,325 B | 46.66% | 53.34% | Good-Feel Kirby's Epic Yarn, Yoshi's Woolly World (Wii) |
| **Bzip2 (Raw stream)** | 48,911 B | 28.06% | 71.94% | Standard high-compression block-sorting stream format |
| **BZ / WBZ (Wiimms Bzip2 Container)** | 48,927 B | 28.07% | 71.93% | Wiimms track distribution container (Mario Kart Wii) |
| **Camelot LZ** | 84,036 B | 48.21% | 51.79% | Camelot Mario Golf, Mario Tennis, We Love Golf! (GC / Wii) |
| **Deflate / Zlib (RFC 1950)** | 61,203 B | 35.11% | 64.89% | Nintendo standard zlib streams (Wii U / 3DS / Switch) |
| **Raw Deflate (RFC 1951)** | 61,197 B | 35.11% | 64.89% | Raw LZ77+Huffman deflate streams without wrappers |
| **Diff8 (Nintendo DS Delta Filter 8)** | 81,325 B | 46.66% | 53.34% | Nintendo DS differential delta filter encoding (8-bit) |
| **Diff16 (Nintendo DS Delta Filter 16)** | 81,325 B | 46.66% | 53.34% | Nintendo DS differential delta filter encoding (16-bit) |
| **FZIP (Game & Wario Zlib)** | 61,211 B | 35.12% | 64.88% | Game & Wario Zlib stream container (Wii U) |
| **Huffman 4-bit (Nintendo DS)** | 151,417 B | 86.87% | 13.13% | Nintendo DS Nitro SDK compact 4bpp graphic streams |
| **Huffman 8-bit (Nintendo DS)** | 104,547 B | 59.98% | 40.02% | Nintendo DS Nitro SDK font and sound resource trees |
| **LZ10 (Nintendo standard LZ77)** | 81,510 B | 46.76% | 53.24% | GameCube, Wii, Nintendo DS, GBA BIOS & game engines |
| **LZ11 (Nintendo extended LZSS / CMP)** | 81,822 B | 46.94% | 53.06% | Nintendo DS, DSi, 3DS, HAL Laboratory (Kirby) |
| **LZ4 (Standard frame stream)** | 99,866 B | 57.29% | 42.71% | Modern high-throughput real-time stream compression (Switch / PC) |
| **LZMA (Raw stream)** | 54,217 B | 31.10% | 68.90% | Universal high-ratio Lempel-Ziv-Markov chain stream |
| **LZ / WLZ (Wiimms LZMA Container)** | 54,233 B | 31.11% | 68.89% | Wiimms LZMA track archive format |
| **LZH8 / LH** | 62,692 B | 35.97% | 64.03% | Hudson Soft / Retro game engines |
| **LZO / LZOvl (Nintendo DS Overlay)** | 81,325 B | 46.66% | 53.34% | Nintendo DS reverse LZO overlay compression |
| **LZX (Capcom Ace Attorney / Ghost Trick)** | 81,325 B | 46.66% | 53.34% | Capcom Ace Attorney & Ghost Trick (Nintendo DS) |
| **MVDK (Mario vs. Donkey Kong LZSS)** | 81,510 B | 46.76% | 53.24% | Nintendo Mario vs. Donkey Kong LZSS (Nintendo DS) |
| **PSDK / AT4PX (Pokémon Mystery Dungeon)** | 81,325 B | 46.66% | 53.34% | Chunsoft Pokémon Mystery Dungeon Explorers (Nintendo DS) |
| **PuCrunch (Nitro hybrid LZ+RLE)** | 81,325 B | 46.66% | 53.34% | Griptonite Games / Retro Nitro hybrid stream compression |
| **QuickLZ / QLZ (Level 1/3)** | 79,423 B | 45.56% | 54.44% | Fast byte-oriented realtime block compression |
| **RLE / RL (Nintendo Run-Length)** | 175,194 B | 100.51% | -0.51% | Nintendo DS / GBA simple graphic run-length format |
| **RNC1 (Rob Northen Method 1)** | 174,525 B | 100.12% | -0.12% | Rob Northen Computing ProPack Method 1 (Huffman) |
| **RNC2 (Rob Northen Method 2)** | 78,991 B | 45.32% | 54.68% | Rob Northen Computing ProPack Method 2 (Byte LZSS) |
| **SSZL (Bandai Namco Museum Remix)** | 81,325 B | 46.66% | 53.34% | Bandai Namco Museum Remix LZSS0 stream (Wii) |
| **VLX (Level-5 Layton / Inazuma)** | 81,325 B | 46.66% | 53.34% | Level-5 Professor Layton & Inazuma Eleven (Nintendo DS) |
| **Yay0 / SZP (Nintendo 64 / GameCube)** | 81,326 B | 46.66% | 53.34% | Super Mario 64, Sunshine, Zelda Wind Waker (N64 / GC) |
| **YAZ0 / SZS (Nintendo Standard)** | 81,325 B | 46.66% | 53.34% | Mario Kart Wii, Smash Bros. Brawl, Zelda: TP (GC / Wii) |
| **YAZ1 (Nintendo Variant)** | 81,325 B | 46.66% | 53.34% | Rare 1st-party Nintendo Yaz variant |
| **YBZ (Yaz0 + Bzip2)** | 48,927 B | 28.07% | 71.93% | Hybrid container (Yaz0 header + Bzip2 payload) |
| **YLZ (Yaz0 + LZMA)** | 54,233 B | 31.11% | 68.89% | Hybrid container (Yaz0 header + LZMA payload) |
| **XYZ (Disguised Yaz0)** | 81,325 B | 46.66% | 53.34% | Disguised / obfuscated custom track archives |
| **Zstandard / Zstd (Modern FSE)** | 64,172 B | 36.81% | 63.19% | Modern Nintendo Switch titles (F-Zero 99) & emulators |

---

## The Odyssey (Homer)
- **Original File Size:** `717,784 bytes`

| Compression Format / Codec | Output Size | Compression Ratio | Space Saved (%) | Where It Is Used / Platform Context |
|---|---|---|---|---|
| **ALZ1 (Mario Party / Bomberman LZ77)** | 361,213 B | 50.32% | 49.68% | Hudson Soft Mario Party / Bomberman (GameCube / Wii) |
| **ASH0 (Nintendo Huffman+LZSS)** | 328,992 B | 45.83% | 54.17% | Wii System Menu, Animal Crossing: City Folk, My Pokémon Ranch |
| **BLZ (DS Nitro ARM9 backward LZ)** | 360,630 B | 50.24% | 49.76% | Nintendo DS Nitro ARM9/ARM7 executable overlays |
| **BPE / GFCP (Good-Feel Byte Pair)** | 361,213 B | 50.32% | 49.68% | Good-Feel Kirby's Epic Yarn, Yoshi's Woolly World (Wii) |
| **Bzip2 (Raw stream)** | 191,001 B | 26.61% | 73.39% | Standard high-compression block-sorting stream format |
| **BZ / WBZ (Wiimms Bzip2 Container)** | 191,017 B | 26.61% | 73.39% | Wiimms track distribution container (Mario Kart Wii) |
| **Camelot LZ** | 372,475 B | 51.89% | 48.11% | Camelot Mario Golf, Mario Tennis, We Love Golf! (GC / Wii) |
| **Deflate / Zlib (RFC 1950)** | 265,486 B | 36.99% | 63.01% | Nintendo standard zlib streams (Wii U / 3DS / Switch) |
| **Raw Deflate (RFC 1951)** | 265,480 B | 36.99% | 63.01% | Raw LZ77+Huffman deflate streams without wrappers |
| **Diff8 (Nintendo DS Delta Filter 8)** | 361,213 B | 50.32% | 49.68% | Nintendo DS differential delta filter encoding (8-bit) |
| **Diff16 (Nintendo DS Delta Filter 16)** | 361,213 B | 50.32% | 49.68% | Nintendo DS differential delta filter encoding (16-bit) |
| **FZIP (Game & Wario Zlib)** | 265,494 B | 36.99% | 63.01% | Game & Wario Zlib stream container (Wii U) |
| **Huffman 4-bit (Nintendo DS)** | 610,249 B | 85.02% | 14.98% | Nintendo DS Nitro SDK compact 4bpp graphic streams |
| **Huffman 8-bit (Nintendo DS)** | 411,151 B | 57.28% | 42.72% | Nintendo DS Nitro SDK font and sound resource trees |
| **LZ10 (Nintendo standard LZ77)** | 361,464 B | 50.36% | 49.64% | GameCube, Wii, Nintendo DS, GBA BIOS & game engines |
| **LZ11 (Nintendo extended LZSS / CMP)** | 361,846 B | 50.41% | 49.59% | Nintendo DS, DSi, 3DS, HAL Laboratory (Kirby) |
| **LZ4 (Standard frame stream)** | 440,538 B | 61.37% | 38.63% | Modern high-throughput real-time stream compression (Switch / PC) |
| **LZMA (Raw stream)** | 213,534 B | 29.75% | 70.25% | Universal high-ratio Lempel-Ziv-Markov chain stream |
| **LZ / WLZ (Wiimms LZMA Container)** | 213,550 B | 29.75% | 70.25% | Wiimms LZMA track archive format |
| **LZH8 / LH** | 270,428 B | 37.68% | 62.32% | Hudson Soft / Retro game engines |
| **LZO / LZOvl (Nintendo DS Overlay)** | 361,213 B | 50.32% | 49.68% | Nintendo DS reverse LZO overlay compression |
| **LZX (Capcom Ace Attorney / Ghost Trick)** | 361,213 B | 50.32% | 49.68% | Capcom Ace Attorney & Ghost Trick (Nintendo DS) |
| **MVDK (Mario vs. Donkey Kong LZSS)** | 361,464 B | 50.36% | 49.64% | Nintendo Mario vs. Donkey Kong LZSS (Nintendo DS) |
| **PSDK / AT4PX (Pokémon Mystery Dungeon)** | 361,213 B | 50.32% | 49.68% | Chunsoft Pokémon Mystery Dungeon Explorers (Nintendo DS) |
| **PuCrunch (Nitro hybrid LZ+RLE)** | 361,213 B | 50.32% | 49.68% | Griptonite Games / Retro Nitro hybrid stream compression |
| **QuickLZ / QLZ (Level 1/3)** | 348,837 B | 48.60% | 51.40% | Fast byte-oriented realtime block compression |
| **RLE / RL (Nintendo Run-Length)** | 723,037 B | 100.73% | -0.73% | Nintendo DS / GBA simple graphic run-length format |
| **RNC1 (Rob Northen Method 1)** | 718,576 B | 100.11% | -0.11% | Rob Northen Computing ProPack Method 1 (Huffman) |
| **RNC2 (Rob Northen Method 2)** | 348,600 B | 48.57% | 51.43% | Rob Northen Computing ProPack Method 2 (Byte LZSS) |
| **SSZL (Bandai Namco Museum Remix)** | 361,213 B | 50.32% | 49.68% | Bandai Namco Museum Remix LZSS0 stream (Wii) |
| **VLX (Level-5 Layton / Inazuma)** | 361,213 B | 50.32% | 49.68% | Level-5 Professor Layton & Inazuma Eleven (Nintendo DS) |
| **Yay0 / SZP (Nintendo 64 / GameCube)** | 361,213 B | 50.32% | 49.68% | Super Mario 64, Sunshine, Zelda Wind Waker (N64 / GC) |
| **YAZ0 / SZS (Nintendo Standard)** | 361,213 B | 50.32% | 49.68% | Mario Kart Wii, Smash Bros. Brawl, Zelda: TP (GC / Wii) |
| **YAZ1 (Nintendo Variant)** | 361,213 B | 50.32% | 49.68% | Rare 1st-party Nintendo Yaz variant |
| **YBZ (Yaz0 + Bzip2)** | 191,017 B | 26.61% | 73.39% | Hybrid container (Yaz0 header + Bzip2 payload) |
| **YLZ (Yaz0 + LZMA)** | 213,550 B | 29.75% | 70.25% | Hybrid container (Yaz0 header + LZMA payload) |
| **XYZ (Disguised Yaz0)** | 361,213 B | 50.32% | 49.68% | Disguised / obfuscated custom track archives |
| **Zstandard / Zstd (Modern FSE)** | 256,649 B | 35.76% | 64.24% | Modern Nintendo Switch titles (F-Zero 99) & emulators |

---

## The Holy Bible (King James Version)
- **Original File Size:** `4,455,950 bytes`

| Compression Format / Codec | Output Size | Compression Ratio | Space Saved (%) | Where It Is Used / Platform Context |
|---|---|---|---|---|
| **ALZ1 (Mario Party / Bomberman LZ77)** | 4,476,541 B | 100.46% | -0.46% | Hudson Soft Mario Party / Bomberman (GameCube / Wii) |
| **ASH0 (Nintendo Huffman+LZSS)** | 1,735,572 B | 38.95% | 61.05% | Wii System Menu, Animal Crossing: City Folk, My Pokémon Ranch |
| **BLZ (DS Nitro ARM9 backward LZ)** | 1,882,148 B | 42.24% | 57.76% | Nintendo DS Nitro ARM9/ARM7 executable overlays |
| **BPE / GFCP (Good-Feel Byte Pair)** | 4,476,541 B | 100.46% | -0.46% | Good-Feel Kirby's Epic Yarn, Yoshi's Woolly World (Wii) |
| **Bzip2 (Raw stream)** | 1,852,363 B | 41.57% | 58.43% | Standard high-compression block-sorting stream format |
| **BZ / WBZ (Wiimms Bzip2 Container)** | 1,852,379 B | 41.57% | 58.43% | Wiimms track distribution container (Mario Kart Wii) |
| **Camelot LZ** | 1,958,671 B | 43.96% | 56.04% | Camelot Mario Golf, Mario Tennis, We Love Golf! (GC / Wii) |
| **Deflate / Zlib (RFC 1950)** | 1,405,680 B | 31.55% | 68.45% | Nintendo standard zlib streams (Wii U / 3DS / Switch) |
| **Raw Deflate (RFC 1951)** | 1,405,674 B | 31.55% | 68.45% | Raw LZ77+Huffman deflate streams without wrappers |
| **Diff8 (Nintendo DS Delta Filter 8)** | 4,476,541 B | 100.46% | -0.46% | Nintendo DS differential delta filter encoding (8-bit) |
| **Diff16 (Nintendo DS Delta Filter 16)** | 4,476,541 B | 100.46% | -0.46% | Nintendo DS differential delta filter encoding (16-bit) |
| **FZIP (Game & Wario Zlib)** | 1,405,688 B | 31.55% | 68.45% | Game & Wario Zlib stream container (Wii U) |
| **Huffman 4-bit (Nintendo DS)** | 3,848,277 B | 86.36% | 13.64% | Nintendo DS Nitro SDK compact 4bpp graphic streams |
| **Huffman 8-bit (Nintendo DS)** | 2,584,975 B | 58.01% | 41.99% | Nintendo DS Nitro SDK font and sound resource trees |
| **LZ10 (Nintendo standard LZ77)** | 1,887,486 B | 42.36% | 57.64% | GameCube, Wii, Nintendo DS, GBA BIOS & game engines |
| **LZ11 (Nintendo extended LZSS / CMP)** | 1,895,887 B | 42.55% | 57.45% | Nintendo DS, DSi, 3DS, HAL Laboratory (Kirby) |
| **LZ4 (Standard frame stream)** | 2,307,096 B | 51.78% | 48.22% | Modern high-throughput real-time stream compression (Switch / PC) |
| **LZMA (Raw stream)** | 1,706,192 B | 38.29% | 61.71% | Universal high-ratio Lempel-Ziv-Markov chain stream |
| **LZ / WLZ (Wiimms LZMA Container)** | 1,706,208 B | 38.29% | 61.71% | Wiimms LZMA track archive format |
| **LZH8 / LH** | 1,423,588 B | 31.95% | 68.05% | Hudson Soft / Retro game engines |
| **LZO / LZOvl (Nintendo DS Overlay)** | 4,476,541 B | 100.46% | -0.46% | Nintendo DS reverse LZO overlay compression |
| **LZX (Capcom Ace Attorney / Ghost Trick)** | 4,476,541 B | 100.46% | -0.46% | Capcom Ace Attorney & Ghost Trick (Nintendo DS) |
| **MVDK (Mario vs. Donkey Kong LZSS)** | 1,887,486 B | 42.36% | 57.64% | Nintendo Mario vs. Donkey Kong LZSS (Nintendo DS) |
| **PSDK / AT4PX (Pokémon Mystery Dungeon)** | 4,476,541 B | 100.46% | -0.46% | Chunsoft Pokémon Mystery Dungeon Explorers (Nintendo DS) |
| **PuCrunch (Nitro hybrid LZ+RLE)** | 4,476,541 B | 100.46% | -0.46% | Griptonite Games / Retro Nitro hybrid stream compression |
| **QuickLZ / QLZ (Level 1/3)** | 1,795,128 B | 40.29% | 59.71% | Fast byte-oriented realtime block compression |
| **RLE / RL (Nintendo Run-Length)** | 4,490,592 B | 100.78% | -0.78% | Nintendo DS / GBA simple graphic run-length format |
| **RNC1 (Rob Northen Method 1)** | 4,460,732 B | 100.11% | -0.11% | Rob Northen Computing ProPack Method 1 (Huffman) |
| **RNC2 (Rob Northen Method 2)** | 1,857,458 B | 41.68% | 58.32% | Rob Northen Computing ProPack Method 2 (Byte LZSS) |
| **SSZL (Bandai Namco Museum Remix)** | 4,476,541 B | 100.46% | -0.46% | Bandai Namco Museum Remix LZSS0 stream (Wii) |
| **VLX (Level-5 Layton / Inazuma)** | 4,476,541 B | 100.46% | -0.46% | Level-5 Professor Layton & Inazuma Eleven (Nintendo DS) |
| **Yay0 / SZP (Nintendo 64 / GameCube)** | 1,880,310 B | 42.20% | 57.80% | Super Mario 64, Sunshine, Zelda Wind Waker (N64 / GC) |
| **YAZ0 / SZS (Nintendo Standard)** | 4,476,541 B | 100.46% | -0.46% | Mario Kart Wii, Smash Bros. Brawl, Zelda: TP (GC / Wii) |
| **YAZ1 (Nintendo Variant)** | 4,476,541 B | 100.46% | -0.46% | Rare 1st-party Nintendo Yaz variant |
| **YBZ (Yaz0 + Bzip2)** | 1,852,379 B | 41.57% | 58.43% | Hybrid container (Yaz0 header + Bzip2 payload) |
| **YLZ (Yaz0 + LZMA)** | 1,706,208 B | 38.29% | 61.71% | Hybrid container (Yaz0 header + LZMA payload) |
| **XYZ (Disguised Yaz0)** | 4,476,541 B | 100.46% | -0.46% | Disguised / obfuscated custom track archives |
| **Zstandard / Zstd (Modern FSE)** | 2,035,958 B | 45.69% | 54.31% | Modern Nintendo Switch titles (F-Zero 99) & emulators |

---

