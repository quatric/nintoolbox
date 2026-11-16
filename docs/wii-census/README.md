# Wii format census

Per-game file-format survey of the Redump WBFS set (2,132 unique games), used to decide which formats to support next.

- `wii-format-census.json`: one object per game, keyed by the six-character game ID. Each value has `title`, `total` (bytes of the data partition), `groups` (`[extension, file count, size in MB, "<detected type>/<result>,..."]`, where the result is `X` for decoded by `wszst xx`, `F` for not decoded and `-` for plain media) and `magics` (per extension, `[first four bytes as hex, file count, total bytes, example path]`).
- `formats.csv`: the same data flattened to one row per game for spreadsheets.

Results were produced with `wszst xx`; an `F` on `.arc`, `.mo`, `.bin` and similar can also mean the sample was routed elsewhere or the audit tool could not find the output, so check the extension against `docs/FORMATS.md` before treating it as a gap.

## Re-audits

The original census is stale: many extractors have been added since, and several `F` marks (CPK, BGIB, `.spd`/`.spt`, ...) are now supported. Games are re-audited with the current build (download, `wit x`, `wszst xx` on samples, delete) and merged in by `merge_audit.py <sweep dir>`; re-audited rows carry the date in the `audited` column (and `audited` in the JSON), empty means the row is still from the original census. `.rcf` / `.dsf` files seen in about ten games are Wii save images, not game data.

## Audit artifacts (2026-11-12)

An in-place re-check (`wszst xx` run next to the sample's sibling files on a fresh build) shows that several frequent `F` marks are not gaps:

- `.brfnt` / `.brfna` (about 370 / 180 games): `xx` deliberately leaves Wii fonts raw so `CREATE` can rebuild them; `wimgt decode` turns them into PNG sheets.
- Formats that need a sibling file fail when the audit copies one file alone: `.spd` + `.spt` banks, `.dir` + data (PIKARC), DIABOLIK resources, `.msh` / `.vtx` pairs.
- Rows audited before an extractor was added still say `F`, e.g. Casper's Scare School `.tex` (ATH-TEX), Movie Games `BUG\0` `.bf`, Wwise `RIFX` `.wav`, Crash Car Racer genome `.dat`.
- `.wav` with `RIFF` (about 100 games) is mostly plain PCM.

## Known open formats (2026-11-08)

Web searches (QuickBMS, TCRF, WiiBrew, GitHub) found no public documentation for these; notes are what was worked out from retail files so far.

| Format | Games | State |
|---|---|---|
| `wii\0` `.ARC` non-texture records | Ski-Doo Snowmobile Challenge, MX vs. ATV, Ultimate Board Game Collection, PDC Darts, Petanque, World Sports Party | the container unpacks (see the *Wii resource ARC* row in `docs/FORMATS.md`) and the type-1 GX textures already decode to PNG (Rebellion Asura engine, `asurarc.inc`); the other record types (`.t3` materials, `.t4` meshes, animations, ...) are raw memory images and not decoded |
| `.wiz` / `.wsp` / `.LOC` (inner formats) | Babyz, Beastly, Babysitting Party | Ubisoft Imagine engine; the files are LZSS-compressed (`0x87654321`, now extracted); the decompressed `.wsp` sprites, `.tan` animations and static `.wiz` models (to OBJ + PNG) are decoded; `.cam` camera paths (to `camera.json`) and `.wan` (`ATB`) are decoded; skinned `.wiz` and `.wsn` (a pointer-laden BE memory image of 0x30-byte float records, layout unresolved) are not decoded yet; `.LOC` is the same LZSS wrapper around a `LOCV1.0` string table and now exports `strings.tsv` |
| `.ast` payloads (Proteus `SDASSETF`) | I Spy Spooky Mansion, I Spy Game Pack, Goosebumps HorrorLand, Spore Hero, Nat Geo Challenge, Ultimate I Spy | the full chunk tree walks to EOF on all 1598 Goosebumps files and big-endian BMAP textures decode to PNG (CMPR, RGBA8, C4/C8 + CLUT); still open: the 22 little-endian textures, tiled 640x480 screens, and skinning (bones / weights), SKEL chunks; MDL models now export to GLB |
| `MEEX` `code.mee` | Slam Games titles (Super Fruit Fall, Top Trumps Adventures, Golden Balls, ...) | the `*strings.mee` / `lang*.mee` language tables are plain NUL-separated text and already export to `strings.tsv`; only the `MEEX` code image (`code.mee`, header `MEEX` + LE words, ~860 KB in Super Fruit Fall) is not decoded |
| `P\x02\0\0` asset blobs | Kid Adventures: Sky Captain (`packfile.dat` members) | the `maxa...` members are plain-text config (`maxassetcount 2` / `stringbufferSizebytes 40`), nothing to decode; the 41 `P\x02` members are memory images with a `{u32 size, u32 count, char name[64]}` pool table at +0x10 (`Temp`, `graphicsmem2`, `default`, `graphics`, `sound`, `soundmem2`) followed by relocatable data, not decoded |
| `vaps` `.wii` page sets | Disney Princess: Enchanted Journey, George of the Jungle | big-endian paged heap image: a 0x100 root block, then `vaps` directory blocks (`{magic, 0x00010003, count, ..., nbuckets, count, bucket off, entry off}`, hash buckets, then 12-byte `{hash, size, offset}` entries in 0x80 units); entries carry no names or type tags (raw GX tiles, palettes, vertex data), so they are not decoded |
| `MOC5` `.dat` movies | 89 games (e.g. `sys/mpls_movie/dat/movie4_nl.dat`) | already handled: the `MOC` magic routes to the Mobiclip extractor; the audit simply never listed the 4-byte `MOC5` string |
| `FVRT` `.dat` archives | My Body Coach, My Body Coach 2, Cocoto Festival | already listed as unsupported by the audit; now extracted (Kylotonn archive, see `docs/FORMATS.md`); the members (`.vid`, `.snd`, `.anm`, `.mdl`, ...) are not decoded |
| `StrE` `.bin` + `.vlt` | EA Playground, FIFA 10-15, EA Sports Active, Skate It, NHL Slapshot (26 games) | decoded and checked on one release of each of the 15 titles: Playground, Celebrity Sports, FIFA 10-15, 2010 World Cup, EA Sports Active 1/2 and NFL Training Camp (32-bit keys), Skate It, NHL Slapshot (EA Attrib vault, see `docs/FORMATS.md`): `db.vlt` turns into `attrib.txt`; the `.bin` is its data file and is not converted on its own |
| `VALT` `.vlt` + `VBIN` `.bin` | SimAnimals, SimAnimals Africa, MySims Kingdom, MySims Agents | decoded (Sims vault, see `docs/FORMATS.md`): all eight retail vaults of the four games turn into `attrib.txt`, identical to an independent Python decoder (two `.bin` header layouts, v2 and v3 class tables, array elements stored as `[%05d]` entries) |
| Unity `.assets` + `.ures` | My Animal Centre, JumpStart (4 games), El Chavo, Big Time Rush, Santa Claus Is Comin' to Town! | textures now decoded from the `.ures` streams (see the Unity row in `docs/FORMATS.md`); AudioClips in those streams are not (no DSP coefficients in the file). *Max & the Magic Marker* (older layout) was already handled |
| `GGXA` `.bin` archives | Radirgy Noa Wii | now extracted (`GGXArchiver1.00`, Gage BPE members, see `docs/FORMATS.md`); `.pvr` textures decode to PNG; the Ninja `.nj` models now export to OBJ (see `docs/FORMATS.md`); `.njm` motions and `.njc` cameras are only described, not converted |

## Re-check of the remaining gaps (2026-11-16)

Most of the census gap list turned out to be stale or plain data. Findings from fetching the games again:

| Format | Games | State |
|---|---|---|
| Mario Sports Mix `stage/*.bin` | Mario Sports Mix | not a gap: LZH8 (`0x40` header, 24-bit LE size) around a BRRES; `wszst xx` already unpacks it and exports the models |
| Sonic `STA1`/`STP0`-`STP6` | Secret Rings, Black Knight | not a gap: `stp.inc` dumps them to `stp.txt` |
| Tony Hawk's Downhill Jam `.pak.ngc` | Downhill Jam | not a gap: Neversoft PAK, extracts as NSPAK |
| Nancy Drew `CVS.DAT` (`CIF `) | The White Wolf of Icicle Creek | index file only; the CIF tree ships already expanded as loose files |
| Scrabble `AscSortedSyn<N>.bin` | Scrabble Interactive | now exported to `.tsv` (rack-leave values, record counts match `ElementsCount.bin`) |
| Tunguska `PAKb` | Secret Files: Tunguska | extracted already; members are now named from the archive's own name table |
| Thor v3 `.wii` | Happy Feet | now extracted (hash-bucket index) |
| `vaps` `.wii` | Toy Story Mania!, Disney Princess, George of the Jungle, Trivial Pursuit | now extracted flat by hash; contents not decoded. Toy Story Mania's 40 `*_s.wii` sound files use a different multi-table layout (`00000001 00000001 00000020 00000024` header, several typed record tables) and are still open. Looked at `s_arm2_s.wii`: after the typed header tables (a 0x164-entry bucket index, then 12-byte `{offset,size,hash}` records at 0x5e4 pointing at 0x98-0x860 byte descriptors made of float parameters and a `0x11` format word), the file is a bank of ~1.8 MB of sample/parameter blocks that are re-described by a second copy of the header at 0x1d0000; the descriptors carry no recognisable DSP-ADPCM coefficient header, so decoding needs the sound engine code (not done) |
| Happy Feet `.str` / `.sbk` | Happy Feet | headerless GameCube DSP-ADPCM (every 8-byte frame header has a predictor nibble <= 7); the sample rates and coefficients are not in the `.wii` resources checked (`explore_*` members are animation keyframes), so they need the sound engine's loader from `main.dol`; a brute-force scan of `main.dol` for a fixed 16-value coefficient table (decoding a stream with every plausible candidate and scoring smoothness) found none, so the coefficients are stored per sound. A few `fx_*` sound entities inside the `.wii` do carry an inline DSP-style block (16 big-endian s16 coefficients, a 22050 Hz rate and nibble addresses such as `0x2018`..`0x24ad`), but decoding the `.sbk` at those addresses with them gives noise, so the address unit / stream mapping is still unknown |
| Shawn Johnson `TDXS` (`BDD/*.bin`) | Shawn Johnson Gymnastics | big-endian typed table database: `{"TDXS", u16 1, u32 0x18, u32 string table, u32 end, ..., u32 records, u32 0x20}` then 32-byte records `{hash, type, length, name offset, ..., count, data offset}` over a hashed cell table; now dumped to `.tsv` (module/table names plus the equal-size word rows; column meanings not documented) |
| Samba de Amigo `challenge*.bin` (`UOCC`) | Samba de Amigo | 484-byte tables of 0x58-byte `UOCC` blocks holding 16-byte `IVD` items `{a,b,c,d,u32 x,u32 y}`; now dumped to `.tsv` (field meanings not documented) |
| Nodame Cantabile `*_msf.bin` (`_MSF`) | Nodame Cantabile: Dream Orchestra | little-endian score container with a `DATA` chunk of instrument-named event records (`violin_miki01_4`, ...); header and names now listed to `.txt`, event layout not decoded |
| SD Gundam G Generation Wars `afs/infohash.bin` (`AFSI`) | SD Gundam G Generation Wars | not a container gap: it is a 271 KB hash index (`AFSI`, per-AFS file counts 6273 / 10391 / 2854 as little-endian u32s, then a `HASH` section of 6-byte records) over `afs0..afs3.afs`, and the `.afs` files themselves already extract (`afs2.afs` -> 2854 files). Scad Hammers, Gashapon Wars and G Generation World have no `AFSI` file |
| Planet 51 `blocks/*.blq`, Red Steel `*.lin` | Planet 51 - The Game, Red Steel | not decoded. `.blq` is a family of big-endian serialised block records with at least five different headers (`0,2,2,0xdddd,...`, `0xa51,0x10001,...`, `0xe,0x100,0x100,1`, ...); the `0,2,2,0xdddd` variant is `{u32 len, name, 6 floats (an AABB), u32 flag}` records, the others differ. `.lin` holds a reflection-style type table (`Vector`, `iLeaf`, `Region`, `Zone`, ...) followed by serialised level objects |
| We Cheer 2 `keydata.bin` (`KBBE`), `NGWords.bin` (`MGBE`) | We Cheer 2 | `keydata.bin` (on-screen keyboard layouts of `{column, row, char}` cells) is now exported to `.tsv`; `NGWords.bin` is a UTF-16BE word filter and is deliberately not exported |
