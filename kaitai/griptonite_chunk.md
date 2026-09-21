# Griptonite "tagged chunk" resource container

**Kaitai definition:** [`griptonite_chunk.ksy`](griptonite_chunk.ksy)
**Compiles clean** with `kaitai-struct-compiler 0.11` (`-t python`), and
was verified with the generated Python parser against real disc samples
(see below).

## Where it's used

Discovered while surveying the disc image of *Ben 10: Alien Force* (Wii,
USA, `RVL-B2AE`), which is built with a proprietary engine belonging to
developer Griptonite Games. The disc's `DATA/files` tree contains ~3600
loose files using this container under several extensions:

| ext  | count (Ben 10) | folder            | content                         |
|------|----------------|--------------------|----------------------------------|
| `.loc` | 2939 | `<Category>/<lang>/` | localized string table |
| `.cin` | 472  | `Cinematics/`         | cutscene/cinematic script |
| `.et`  | 28   | `EntityTemplates/`    | entity/prefab template |
| `.rgn` | 96   | `Regions/`            | level region data |
| `.map` | 12   | `Maps/`               | level/map data |

The **same** magic/envelope was independently confirmed on a second,
unrelated disc, *Build-A-Bear Workshop: A Friend Fur All Seasons* (Wii,
USA, `RVL-B2VE`) — also a Griptonite/D3-era title — for `.cin`, `.loc`
and `.rgn` files, byte-for-byte identical envelope structure at multiple
nesting levels. This confirms the container is a **shared middleware /
engine-level serialization format**, not specific to either game.

Not covered here (already standard/documented elsewhere in this
project or in general Wii tooling, so skipped per the task scope):
- `.tpl` — standard Nintendo GX texture palette format.
- `.arc` (e.g. `HomeButton/homeBtn*.arc`) — standard Nintendo U8 archive
  (magic `55 AA 38 2D`).
- `.mus` — raw/compressed audio blob, no `FAAFFAAF` magic; not this
  container (not investigated further here).
- `Game.dsf` / `Game.rcf` — small files starting `RWTEG9`, likely a
  disc-signing/update config header (`Game.dlf` is a plain-text CSV-like
  manifest referencing them: `RVL_CONFIG_FILE,"Game.rcf",...,"Game.dsf"`);
  not the interesting per-asset archive format and out of scope.

## Byte layout summary

```
offset  size  field                          notes
0x00    4     magic "FA AF FA AF"            fixed
0x04    4     version = 0x00000008           always 8 in every sample
0x08    4     root.marker = 0xBBBBBBBB        "block" sentinel
0x0C    4     root.len_total                  TOTAL size of this chunk,
                                               INCLUDING these 8 bytes
                                               (i.e. body = len_total - 8)
0x10    4     root.body.num_children          always 1 (named-resource
                                               files) or 2 (.loc files)
0x14    4     child.marker = 0xBBBBBBBB
0x18    4     child.len_total
0x1C    4     child.body.num_children         2, in every named-resource
                                               sample
0x20    ...   resource_header (see below)     ONLY for named-resource
                                               files (num_children==1
                                               above); .loc files differ
```

`resource_header` (offset 0x20 in every non-`.loc` sample checked):

```
+0x00  u4    type_tag        = 0x00000020 (constant, "named resource"?)
+0x04  u4    hash            probably CRC/hash of name, not verified
+0x08  8     guid            binary id; hex form == the file's own
                              base filename, e.g. bytes
                              58 2C E6 04 CD B9 35 C1 for
                              "582CE604CDB935C1.et"
+0x10  u4    name_len
+0x14  name_len  name        ASCII, not further padded within this record
```

Beyond this point the tree mixes two different sub-encodings that were
*not* fully reverse-engineered:
- sentinel-marked chunks (`0xBBBBBBBB` block / `0xBEBEBEBE` leaf, each
  with an 8-byte-inclusive length prefix, same as the outer chunks), and
- raw fixed/variable-length typed records with no marker/length
  wrapper of their own (like `resource_header` itself).

Which encoding applies at a given position depends on the surrounding
schema (i.e. which field of which engine data structure is being
written), which would require recovering the engine's serializer code
to fully resolve. The `.ksy` therefore decodes the reliable outer
envelope + the one universal "named resource" header precisely, and
represents everything past that as opaque bytes rather than guessing.

## Validation performed

- Compiled with `kaitai-struct-compiler -t python` — no errors, no
  warnings.
- Ran the generated parser against 8 real files pulled from both discs
  (`.cin`, `.et`, `.rgn`, `.map`, `.loc`, across both titles). All 8
  parse the chunk envelope correctly and land exactly on EOF. 6 of 8
  (all the non-`.loc` ones) additionally decode `resource_header` and
  recover a GUID that exactly matches the file's own filename plus a
  human-readable name string (`vo_gorvan_shield2`, `ModView_B_HighBreed`,
  `L2_R6_Cave`, `Level_04_Hatchery`, `rtc_intro_StartIsl`, `vide2`). The
  2 `.loc` files correctly raise the documented, expected validation
  error on `resource_header.type_tag` since `.loc` files use a different
  sub-layout at that offset (their root child has `num_children == 2`,
  not `1`).
