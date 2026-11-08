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
| `wii\0` `.ARC` | Ski-Doo Snowmobile Challenge, MX vs. ATV | BE typed memory-image pack, no stored names: header count at +8, data base at +0x10, 0x20-byte entries at +0x100 `{0, type, size, offset-in-type-section, id, 0, 0x20, 0}`; payloads are relocatable images, not decoded |
| `.wiz` / `.wsp` / `.LOC` | Babyz, Beastly, Babysitting Party | Ubisoft Imagine engine; `.LOC` starts `LOCV1.0` and is compressed; headers not understood |
| `.ast` payloads | I Spy Spooky Mansion, I Spy Game Pack, Spore Hero | `SDASSETF` container is walked; model/animation/texture chunks are not decoded |
| `PAK\3` | International Athletics (`GameData.pak`) | not analysed |
| `.mee` language tables | Slam Games titles | custom compressed text |
| `maxa` payloads | Kid Adventures: Sky Captain | inner payloads of the already extracted `PAK\0` members |
