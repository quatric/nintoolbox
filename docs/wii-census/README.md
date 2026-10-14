# Wii format census

Per-game file-format survey of the Redump WBFS set (2,132 unique games), used to decide which formats to support next.

- `wii-format-census.json`: one object per game, keyed by the six-character game ID. Each value has `title`, `total` (bytes of the data partition), `groups` (`[extension, file count, size in MB, "<detected type>/<result>,..."]`, where the result is `X` for decoded by `wszst xx`, `F` for not decoded and `-` for plain media) and `magics` (per extension, `[first four bytes as hex, file count, total bytes, example path]`).
- `formats.csv`: the same data flattened to one row per game for spreadsheets.

Results were produced with `wszst xx`; an `F` on `.arc`, `.mo`, `.bin` and similar can also mean the sample was routed elsewhere or the audit tool could not find the output, so check the extension against `docs/FORMATS.md` before treating it as a gap.
