# WiiWare and Virtual Console WAD audit (2026-11-22)

All non-demo WiiWare titles (597) and Virtual Console entries (951, one per game and platform) from the No-Intro `Digital (WAD)` set were unpacked (`sharpii wad -u`, then `sharpii u8 -u` on every U8 `.app`), one region per title (USA, else Europe, World, Japan). 1,537 unpacked; 5 were skipped on file-name mismatches (Final Fantasy Crystal Chronicles: My Life as a King and four Dr. Mario & Bactericide titles). `wiiware-vc-extensions.csv` lists the 709 file extensions found, with title count, file count, bytes, whether the extension appears in `FORMATS.md`, the most common first four bytes and an example path. Most hits (`.arc`, `.brsar`, `.tpl`, `.csv`, `.txt`, `.ttc`) come from the channel wrapper every WAD carries, not from the game.

## Already handled by the current build (checked with `wszst xx`)

| Extension | Example | Result |
| --- | --- | --- |
| `.brmdl` | TV Show King | plain BRRES, unpacked to `<file>.d` |
| `.l77` | Voodoo Dice | LZ77 stream, written as `<file>.bin` and unpacked when it holds a U8 |
| `.cmpres` | Deer Captor | decompressed to `<file>.bin`, then unpacked (173 files in `animal.cmpres`) |
| `.3dn` | Drop Zone: Under Fire | gzip, inner file written beside it (the inner 3D format is not decoded) |
| `.w3d`, `.cwi` | Fun! Fun! Minigolf, Mister Bumblebee | decompressed to `<file>.bin` |
| `.wtso` | Spaceball Revolution | converted to WAV |

## Added in this pass

`.fez` (Yard Sale Hidden Treasures), `.pk3` (Simple Series Vol. 3: The Mahjong), `.blk.bz2` (The Amazing Brain Train), `.PTX` with its `.PTXH` header (Around the World), `.mxs` (Mix Superstar, DSP-ADPCM behind a chunk table) and `.sir` (Military Madness: Nectaris, zlib `ZBLK` streams); see the rows in `FORMATS.md`.

## Looked at, not supported

None of these decodes with zlib, raw deflate, bzip2, LZMA, LZ4 or LZF at any offset in the first 0x80 bytes, and a web search found no public description of any of them. Decoding them needs the games' own code (disassembly or emulation).

| Extension | Example | What is known |
| --- | --- | --- |
| `.epk` | Stunt Cars `game.epk` (30 MB) | `IGEL*epk` header, then data with entropy 7.99 |
| `.s3d_pack`, `.stk` | Aya and the Cubes of Light, Babel Rising | Shark 3D packs. XOR with `0x9f` (the zero padding reads as `9f 9f 9f 9f`) turns the padding into zeros but leaves repeated bit-shifted word patterns, so there is a second bit-level layer; `.stk` shows `0x20` runs in the same way |
| `.ppm`, `.GWVideo` | Where's Wally: Fantastic Journey | `FMPP` header (`.GWVideo` wraps one at offset 0x1d), custom compressed stream; the byte `0x05 0x08 0x01` pattern in the header is shared with `.pacz` |
| `.pacz` | Moki Moki | `LZS\0` header: `05 05 08 01`, data start offset (0x2a, name padded to an even length), unpacked size, file size, block size 0x20000, then a name (`temp.tmp`) and a custom block stream. The `LZS` magic is not compared as a word in the game executable (no `lis 0x4c5a`) |
| `.lpf2` | Hydroventure | `LevelPackV2` header, then a stream with long incrementing byte runs (`01 02 03 04 05 ...`) that looks like a delta/LZ hybrid |
| `.alb` | Line Attack Heroes | not a container: a named wrapper around serialized game objects (`tpb`, `BAR`, `BAI` blocks of floats and hashes), nothing to extract |
