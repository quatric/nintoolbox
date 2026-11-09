# Wii format census

Per-game file-format survey of the Redump WBFS set (2,132 unique games), used to decide which formats to support next.

- `wii-format-census.json`: one object per game, keyed by the six-character game ID. Each value has `title`, `total` (bytes of the data partition), `groups` (`[extension, file count, size in MB, "<detected type>/<result>,..."]`, where the result is `X` for decoded by `wszst xx`, `F` for not decoded and `-` for plain media) and `magics` (per extension, `[first four bytes as hex, file count, total bytes, example path]`).
- `formats.csv`: the same data flattened to one row per game for spreadsheets.

Results were produced with `wszst xx`; an `F` on `.arc`, `.mo`, `.bin` and similar can also mean the sample was routed elsewhere or the audit tool could not find the output, so check the extension against `docs/FORMATS.md` before treating it as a gap.

## Re-audits

The original census is stale: many extractors have been added since, and several `F` marks (CPK, BGIB, `.spd`/`.spt`, ...) are now supported. Games are re-audited with the current build (download, `wit x`, `wszst xx` on samples, delete) and merged in by `merge_audit.py <sweep dir>`; re-audited rows carry the date in the `audited` column (and `audited` in the JSON), empty means the row is still from the original census. `.rcf` / `.dsf` files seen in about ten games are Wii save images, not game data.

## Known open formats (2026-11-08)

Web searches (QuickBMS, TCRF, WiiBrew, GitHub) found no public documentation for these; notes are what was worked out from retail files so far.

| Format | Games | State |
|---|---|---|
| `OIBB` `.pak` | Doctor Fizzwizzle's Animal Rescue (`Story_*.pak`) | LE serialized object graph. `TXTR` object: u32 size, data size, data offset, width, height, GX format 9 (C8); pixels are GX C8 tiles (8x4, width padded to 504) and render correctly with an RGB5A3 palette, but only 160 palette entries are stored near the object while indices reach 255; where entries 160-255 live is unsolved |
| `wii\0` `.ARC` non-texture records | Ski-Doo Snowmobile Challenge, MX vs. ATV, Ultimate Board Game Collection, PDC Darts, Petanque, World Sports Party | the container unpacks (see the *Wii resource ARC* row in `docs/FORMATS.md`) and the type-1 GX textures already decode to PNG (Rebellion Asura engine, `asurarc.inc`); the other record types (`.t3` materials, `.t4` meshes, animations, ...) are raw memory images and not decoded |
| `.wiz` / `.wsp` / `.LOC` (inner formats) | Babyz, Beastly, Babysitting Party | Ubisoft Imagine engine; the files are LZSS-compressed (`0x87654321`, now extracted); the decompressed `.wsp` sprites / `.wiz` models / `.tan` `.wan` animations are BE structures not decoded yet; `.LOC` starts `LOCV1.0` with a different compression |
| `.ast` payloads | I Spy Spooky Mansion, I Spy Game Pack, Spore Hero | `SDASSETF` container is walked; model/animation/texture chunks are not decoded |
| `MEEX` `code.mee` | Slam Games titles (Super Fruit Fall, Top Trumps Adventures, Golden Balls, ...) | the `*strings.mee` / `lang*.mee` language tables are plain NUL-separated text and already export to `strings.tsv`; only the `MEEX` code image (`code.mee`, header `MEEX` + LE words, ~860 KB in Super Fruit Fall) is not decoded |
| `P\x02\0\0` asset blobs | Kid Adventures: Sky Captain (`packfile.dat` members) | the `maxa...` members are plain-text config (`maxassetcount 2` / `stringbufferSizebytes 40`), nothing to decode; the 41 `P\x02` members are memory images with a `{u32 size, u32 count, char name[64]}` pool table at +0x10 (`Temp`, `graphicsmem2`, `default`, `graphics`, `sound`, `soundmem2`) followed by relocatable data, not decoded |
