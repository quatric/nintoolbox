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

`.fez` (Yard Sale Hidden Treasures), `.pk3` (Simple Series Vol. 3: The Mahjong), `.blk.bz2` (The Amazing Brain Train), `.PTX` with its `.PTXH` header (Around the World), `.mxs` (Mix Superstar, DSP-ADPCM behind a chunk table) `.sir` (Military Madness: Nectaris, zlib `ZBLK` streams) `.epk` (Stunt Cars, raw-deflate entries with a path directory) `.pacz` (Moki Moki, LZS v5 plus the `add` pack inside) and `.lpf2` (Hydroventure, Huffman then LZ77); see the rows in `FORMATS.md`.

`.s3d_pack` (Aya and the Cubes of Light) and `.stk` (Babel Rising) were solved by statistics rather than by finding the loader: `.s3d_pack` is a fixed 256-byte substitution table (found in the executable by searching for a 256-byte involution) plus a 4-byte checksum at the end of every 0x1000-byte page, and `.stk` is a plain repeating 4-byte XOR whose key is stored in its header. Both then hold ordinary zlib streams.

`.ppm` (FMPP, Where's Wally) was not found in the executable either; it was solved from the data. The header's unpacked-size field and the 0x1f bytes recurring every 33 bytes (a 32 byte literal run) pointed at an LZ77 variant with 5-bit literal-run control bytes, and the first byte's leading `001` matched FastLZ level 2; the unpacked size then came out exactly right for all five samples, and the decoded buffer is a palette, an alpha image and a colour image in GX 8x4 tiles.

## Looked at, not supported

`.epk`, `.pacz` and `.lpf2` were solved by reading the games' own code: the decoder is found by locating the code that checks the magic (byte compares for `LZS\0`, the version-mismatch message of the level pack loader) and porting what it does. The remaining formats did not give way to that. Standard zlib, raw deflate, bzip2, LZMA, LZ4 and LZF find nothing in them (short matches of 6-30 bytes that "inflate" to 100+ bytes are noise; real streams, like the `.epk` entries, end with a proper end-of-stream marker).

| Extension | Example | What is known |
| --- | --- | --- |
| `.alb` | Line Attack Heroes | split into its wrapper and sections (`BAI` offsets table in `ai_data.alb`, one block in the stage maps); the content is serialized game objects (`tpb`, `BAR`, `BAI` blocks of floats, hashes and tile indices) and is not decoded |
