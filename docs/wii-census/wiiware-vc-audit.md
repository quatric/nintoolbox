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

`.fez` (Yard Sale Hidden Treasures), `.pk3` (Simple Series Vol. 3: The Mahjong) and `.blk.bz2` (The Amazing Brain Train); see the rows in `FORMATS.md`.

## Looked at, not supported

| Extension | Example | What is known |
| --- | --- | --- |
| `.epk` | Stunt Cars `game.epk` (30 MB) | `IGEL*epk` header, then data with entropy 7.99; no standard compressor decodes it |
| `.s3d_pack`, `.stk` | Aya and the Cubes of Light, Babel Rising | Shark 3D packs; the data after the header looks XOR-obfuscated (the zero padding reads as `9f 9f 9f 9f`), key not recovered |
| `.ppm`, `.GWVideo` | Where's Wally: Fantastic Journey | `FMPP` header (`.GWVideo` wraps one at offset 0x1d), custom compressed stream |
| `.pacz` | Moki Moki | `LZS\0` header with a `temp.tmp` entry, custom compression |
| `.lpf2` | Hydroventure | `LevelPackV2` header, custom compression |
| `.mxs` | Mix Superstar | BE header, audio samples, codec not identified |
| `.sir` | Military Madness: Nectaris | table of `{id, size, offset, next}`-like rows whose offsets do not fit the file; layout not understood |
| `.alb` | Line Attack Heroes | not a container: a named wrapper around serialized game objects (`tpb`, `BAR`, `BAI` blocks of floats and hashes) |
| `.PTX` | Around the World | 1 MiB of mostly zeros, probably a raw font texture; dimensions and pixel format not known |
