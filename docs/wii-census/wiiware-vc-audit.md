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

## Looked at, not supported

`.epk`, `.pacz` and `.lpf2` were solved by reading the games' own code: the decoder is found by locating the code that checks the magic (byte compares for `LZS\0`, the version-mismatch message of the level pack loader) and porting what it does. The remaining formats did not give way to that. Standard zlib, raw deflate, bzip2, LZMA, LZ4 and LZF find nothing in them (short matches of 6-30 bytes that "inflate" to 100+ bytes are noise; real streams, like the `.epk` entries, end with a proper end-of-stream marker).

| Extension | Example | What is known |
| --- | --- | --- |
| `.stk` | Babel Rising | Shark 3D stack file from a different engine build (no `shark3d_packstream_ident` in the executable). The bytes are substituted (zero padding reads as `20`) but not with the `.s3d_pack` table or any of the 256-byte permutations in the executable; not decoded. (`.s3d_pack` itself is now supported: see FORMATS.md.) |
| `.ppm`, `.GWVideo` | Where's Wally: Fantastic Journey | `FMPP`, 40-byte header: version bytes, three big-endian width/height pairs (641x251, the same padded to 8/4 = 648x252, 641x298), two unknown u16, payload size (file size - 40) and header size 40; the payload starts with `27 xx` and a repeated `00 02 00 20 00 0b 40 00` pattern. `.GWVideo` wraps one `FMPP` frame at offset 0x1d after a `{8, 2, frames, 0, fps, 0, 28}` header. The executable has no `FMPP` string or word compare; it does contain `FGWVideo.cpp` and `FJpegDecompress.cpp` assertions |
| `.alb` | Line Attack Heroes | not a container: a named wrapper around serialized game objects (`tpb`, `BAR`, `BAI` blocks of floats and hashes), nothing to extract |
