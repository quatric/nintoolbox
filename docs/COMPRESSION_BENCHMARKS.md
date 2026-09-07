# Individual Compression Formats & Size Comparisons

This document presents the exact input and compressed output file sizes for each individual compression format implemented in `wiimms-szs-tools-plus`.

---

## 1. Small Text: *Bee Movie Script* (Excerpt)
- **Input File Size:** `16,977 bytes` (~16.6 KB)

| Compression Format / Codec | Input Size | Output Size | Compression Ratio | Space Saved (%) |
|---|---|---|---|---|
| **ALZ1 (Mario Party / Bomberman LZ77)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **ASH0 (Nintendo Huffman+LZSS)** | 16,977 B | 19,820 B | 116.75% | -16.75% |
| **BLZ (DS Nitro ARM9 backward LZ)** | 16,977 B | 4,300 B | 25.33% | 74.67% |
| **BPE / GFCP (Good-Feel Byte Pair Encoding)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **Bzip2 (Raw stream)** | 16,977 B | 2,981 B | 17.56% | 82.44% |
| **BZ / WBZ (Wiimms Bzip2 Container)** | 16,977 B | 2,997 B | 17.65% | 82.35% |
| **Camelot LZ** | 16,977 B | 4,218 B | 24.85% | 75.15% |
| **Deflate / Zlib (RFC 1950)** | 16,977 B | 3,181 B | 18.74% | 81.26% |
| **Raw Deflate (RFC 1951)** | 16,977 B | 3,175 B | 18.70% | 81.30% |
| **Diff8 (Nintendo DS Delta Filter 8-bit)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **Diff16 (Nintendo DS Delta Filter 16-bit)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **FZIP (Game & Wario Zlib Container)** | 16,977 B | 3,189 B | 18.78% | 81.22% |
| **Huffman 4-bit (Nintendo DS)** | 16,977 B | 14,237 B | 83.86% | 16.14% |
| **Huffman 8-bit (Nintendo DS)** | 16,977 B | 10,317 B | 60.77% | 39.23% |
| **LZ10 (Nintendo standard LZ77)** | 16,977 B | 4,210 B | 24.80% | 75.20% |
| **LZ11 (Nintendo extended LZSS / CMP)** | 16,977 B | 4,371 B | 25.75% | 74.25% |
| **LZMA (Raw stream)** | 16,977 B | 3,035 B | 17.88% | 82.12% |
| **LZ / WLZ (Wiimms LZMA Container)** | 16,977 B | 3,051 B | 17.97% | 82.03% |
| **LZH8 / LH** | 16,977 B | 3,456 B | 20.36% | 79.64% |
| **LZO / LZOvl (Nintendo DS Overlay)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **LZX (Capcom Ace Attorney / Ghost Trick)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **MVDK (Mario vs. Donkey Kong LZSS)** | 16,977 B | 4,210 B | 24.80% | 75.20% |
| **PSDK / AT4PX (Pokémon Mystery Dungeon)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **PuCrunch (Nitro hybrid LZ+RLE)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **QuickLZ / QLZ (Level 1/3)** | 16,977 B | 4,145 B | 24.42% | 75.58% |
| **RLE / RL (Nintendo Run-Length)** | 16,977 B | 16,578 B | 97.65% | 2.35% |
| **RNC1 (Rob Northen Method 1)** | 16,977 B | 17,021 B | 100.26% | -0.26% |
| **RNC2 (Rob Northen Method 2)** | 16,977 B | 4,197 B | 24.72% | 75.28% |
| **SSZL (Bandai Namco Museum Remix)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **VLX (Level-5 Layton / Inazuma)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **Yay0 / SZP (Nintendo 64 / GameCube)** | 16,977 B | 4,166 B | 24.54% | 75.46% |
| **YAZ0 / SZS (Nintendo Standard)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **YAZ1 (Nintendo Variant)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **YBZ (Yaz0 + Bzip2)** | 16,977 B | 2,997 B | 17.65% | 82.35% |
| **YLZ (Yaz0 + LZMA)** | 16,977 B | 3,051 B | 17.97% | 82.03% |
| **XYZ (Disguised Yaz0)** | 16,977 B | 4,165 B | 24.53% | 75.47% |
| **Zstandard / Zstd (Modern FSE)** | 16,977 B | 3,607 B | 21.25% | 78.75% |

---

## 2. Medium Text: *Alice in Wonderland* (Lewis Carroll)
- **Input File Size:** `174,311 bytes` (~170.2 KB)

| Compression Format / Codec | Input Size | Output Size | Compression Ratio | Space Saved (%) |
|---|---|---|---|---|
| **ALZ1 (Mario Party / Bomberman LZ77)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **ASH0 (Nintendo Huffman+LZSS)** | 174,311 B | 196,820 B | 112.91% | -12.91% |
| **BLZ (DS Nitro ARM9 backward LZ)** | 174,311 B | 81,302 B | 46.64% | 53.36% |
| **BPE / GFCP (Good-Feel Byte Pair Encoding)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **Bzip2 (Raw stream)** | 174,311 B | 48,911 B | 28.06% | 71.94% |
| **BZ / WBZ (Wiimms Bzip2 Container)** | 174,311 B | 48,927 B | 28.07% | 71.93% |
| **Camelot LZ** | 174,311 B | 84,036 B | 48.21% | 51.79% |
| **Deflate / Zlib (RFC 1950)** | 174,311 B | 61,203 B | 35.11% | 64.89% |
| **Raw Deflate (RFC 1951)** | 174,311 B | 61,197 B | 35.11% | 64.89% |
| **Diff8 (Nintendo DS Delta Filter 8-bit)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **Diff16 (Nintendo DS Delta Filter 16-bit)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **FZIP (Game & Wario Zlib Container)** | 174,311 B | 61,211 B | 35.12% | 64.88% |
| **Huffman 4-bit (Nintendo DS)** | 174,311 B | 151,417 B | 86.87% | 13.13% |
| **Huffman 8-bit (Nintendo DS)** | 174,311 B | 104,547 B | 59.98% | 40.02% |
| **LZ10 (Nintendo standard LZ77)** | 174,311 B | 81,510 B | 46.76% | 53.24% |
| **LZ11 (Nintendo extended LZSS / CMP)** | 174,311 B | 81,822 B | 46.94% | 53.06% |
| **LZMA (Raw stream)** | 174,311 B | 54,217 B | 31.10% | 68.90% |
| **LZ / WLZ (Wiimms LZMA Container)** | 174,311 B | 54,233 B | 31.11% | 68.89% |
| **LZH8 / LH** | 174,311 B | 62,692 B | 35.97% | 64.03% |
| **LZO / LZOvl (Nintendo DS Overlay)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **LZX (Capcom Ace Attorney / Ghost Trick)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **MVDK (Mario vs. Donkey Kong LZSS)** | 174,311 B | 81,510 B | 46.76% | 53.24% |
| **PSDK / AT4PX (Pokémon Mystery Dungeon)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **PuCrunch (Nitro hybrid LZ+RLE)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **QuickLZ / QLZ (Level 1/3)** | 174,311 B | 79,423 B | 45.56% | 54.44% |
| **RLE / RL (Nintendo Run-Length)** | 174,311 B | 175,194 B | 100.51% | -0.51% |
| **RNC1 (Rob Northen Method 1)** | 174,311 B | 174,525 B | 100.12% | -0.12% |
| **RNC2 (Rob Northen Method 2)** | 174,311 B | 78,991 B | 45.32% | 54.68% |
| **SSZL (Bandai Namco Museum Remix)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **VLX (Level-5 Layton / Inazuma)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **Yay0 / SZP (Nintendo 64 / GameCube)** | 174,311 B | 81,326 B | 46.66% | 53.34% |
| **YAZ0 / SZS (Nintendo Standard)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **YAZ1 (Nintendo Variant)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **YBZ (Yaz0 + Bzip2)** | 174,311 B | 48,927 B | 28.07% | 71.93% |
| **YLZ (Yaz0 + LZMA)** | 174,311 B | 54,233 B | 31.11% | 68.89% |
| **XYZ (Disguised Yaz0)** | 174,311 B | 81,325 B | 46.66% | 53.34% |
| **Zstandard / Zstd (Modern FSE)** | 174,311 B | 64,172 B | 36.81% | 63.19% |

---

## 3. Large Text: *The Holy Bible* (King James Version)
- **Input File Size:** `4,455,950 bytes` (~4.25 MB)

| Compression Format / Codec | Input Size | Output Size | Compression Ratio | Space Saved (%) |
|---|---|---|---|---|
| **ALZ1 (Mario Party / Bomberman LZ77)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **ASH0 (Nintendo Huffman+LZSS)** | 4,455,950 B | 5,013,664 B | 112.52% | -12.52% |
| **BLZ (DS Nitro ARM9 backward LZ)** | 4,455,950 B | 1,882,148 B | 42.24% | 57.76% |
| **BPE / GFCP (Good-Feel Byte Pair Encoding)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **Bzip2 (Raw stream)** | 4,455,950 B | 1,852,363 B | 41.57% | 58.43% |
| **BZ / WBZ (Wiimms Bzip2 Container)** | 4,455,950 B | 1,852,379 B | 41.57% | 58.43% |
| **Camelot LZ** | 4,455,950 B | 1,958,671 B | 43.96% | 56.04% |
| **Deflate / Zlib (RFC 1950)** | 4,455,950 B | 1,405,680 B | 31.55% | 68.45% |
| **Raw Deflate (RFC 1951)** | 4,455,950 B | 1,405,674 B | 31.55% | 68.45% |
| **Diff8 (Nintendo DS Delta Filter 8-bit)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **Diff16 (Nintendo DS Delta Filter 16-bit)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **FZIP (Game & Wario Zlib Container)** | 4,455,950 B | 1,405,688 B | 31.55% | 68.45% |
| **Huffman 4-bit (Nintendo DS)** | 4,455,950 B | 3,848,277 B | 86.36% | 13.64% |
| **Huffman 8-bit (Nintendo DS)** | 4,455,950 B | 2,584,975 B | 58.01% | 41.99% |
| **LZ10 (Nintendo standard LZ77)** | 4,455,950 B | 1,887,486 B | 42.36% | 57.64% |
| **LZ11 (Nintendo extended LZSS / CMP)** | 4,455,950 B | 1,895,887 B | 42.55% | 57.45% |
| **LZMA (Raw stream)** | 4,455,950 B | 1,706,192 B | 38.29% | 61.71% |
| **LZ / WLZ (Wiimms LZMA Container)** | 4,455,950 B | 1,706,208 B | 38.29% | 61.71% |
| **LZH8 / LH** | 4,455,950 B | 1,423,588 B | 31.95% | 68.05% |
| **LZO / LZOvl (Nintendo DS Overlay)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **LZX (Capcom Ace Attorney / Ghost Trick)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **MVDK (Mario vs. Donkey Kong LZSS)** | 4,455,950 B | 1,887,486 B | 42.36% | 57.64% |
| **PSDK / AT4PX (Pokémon Mystery Dungeon)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **PuCrunch (Nitro hybrid LZ+RLE)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **QuickLZ / QLZ (Level 1/3)** | 4,455,950 B | 1,795,128 B | 40.29% | 59.71% |
| **RLE / RL (Nintendo Run-Length)** | 4,455,950 B | 4,490,592 B | 100.78% | -0.78% |
| **RNC1 (Rob Northen Method 1)** | 4,455,950 B | 4,460,732 B | 100.11% | -0.11% |
| **RNC2 (Rob Northen Method 2)** | 4,455,950 B | 1,857,458 B | 41.68% | 58.32% |
| **SSZL (Bandai Namco Museum Remix)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **VLX (Level-5 Layton / Inazuma)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **Yay0 / SZP (Nintendo 64 / GameCube)** | 4,455,950 B | 1,880,310 B | 42.20% | 57.80% |
| **YAZ0 / SZS (Nintendo Standard)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **YAZ1 (Nintendo Variant)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **YBZ (Yaz0 + Bzip2)** | 4,455,950 B | 1,852,379 B | 41.57% | 58.43% |
| **YLZ (Yaz0 + LZMA)** | 4,455,950 B | 1,706,208 B | 38.29% | 61.71% |
| **XYZ (Disguised Yaz0)** | 4,455,950 B | 4,476,541 B | 100.46% | -0.46% |
| **Zstandard / Zstd (Modern FSE)** | 4,455,950 B | 2,035,958 B | 45.69% | 54.31% |
