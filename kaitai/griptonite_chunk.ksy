meta:
  id: griptonite_chunk
  title: Griptonite Games tagged-chunk resource container
  file-extension:
    - cin   # cinematic/cutscene script
    - loc   # localized string table (per-language subfolder)
    - et    # entity template
    - rgn   # region/level-chunk data
    - map   # map/level data
  endian: be
  license: CC0-1.0
doc: |
  Generic tagged binary "chunk tree" container used by the proprietary engine
  shared by Griptonite Games' D3 Publisher-era Wii titles, e.g.
  "Ben 10: Alien Force" (RVL-B2AE) and "Build-A-Bear Workshop: A Friend
  Fur All Seasons" (RVL-B2VE). Empirically confirmed byte-identical across
  both discs for the `.cin`, `.loc`, `.et`, `.rgn` and `.map` extensions
  (only the *contents* of the leaf chunks differ per file type - the
  envelope/tree grammar is shared middleware, not game-specific).

  Every file is one big-endian tree of "chunks". A chunk is a 4-byte
  sentinel marker, a 4-byte length, and then a body. Two marker values
  were observed:

    - `0xBBBBBBBB` ("block"): a container. Its body begins with a `u4`
      child count. What immediately follows the count is *not*
      uniformly a list of further sentinel-marked chunks, though - at
      shallow depth it is (root's one child is itself a `block` chunk),
      but one level deeper the "children" turn out to be raw,
      variable-length typed records with no marker/length wrapper of
      their own (e.g. `resource_header` below, whose own leading `u4`
      looks like a marker/type-tag but is not one of the two sentinels
      and is immediately followed by fixed fields, not a nested chunk).
      Decoding those inner records fully would require recovering the
      engine's per-field-type read functions (effectively decompiling
      the serializer) and is out of scope here; this .ksy therefore only
      recurses one level (root -> its single child) and otherwise leaves
      block bodies as opaque bytes beyond the child count.
    - `0xBEBEBEBE` ("leaf/data"): opaque payload bytes (floats, indices,
      sub-records, ...) whose interpretation depends on the containing
      file type / schema position and was not further decoded here.

  IMPORTANT: the `len` field of a chunk is the **total size of the
  chunk including its own 8-byte marker+len header**, not just the size
  of the body that follows it (i.e. `body_size = len - 8`). This was
  confirmed by the fact that, read this way, the root chunk's length
  always makes the parse land exactly on end-of-file across every
  sample checked (5 different files, 2 different discs, sizes from
  ~600 B to ~14 MB) - reading it as "body-only" length instead
  overshoots end-of-file by exactly 8 bytes every time.

  At a fixed offset (0x20) in every sample examined, the tree contains
  a `resource_header` record (see below) giving the resource's own
  8-byte hex GUID - which is *also* used as the file's own base
  filename (e.g. `582CE604CDB935C1.et`) - plus a human-readable name
  string. This is effectively the closest analogue to a classic
  archive's name table, since there is no separate outer index /
  table-of-contents file; each chunk file names itself inline.
seq:
  - id: magic
    contents: [0xfa, 0xaf, 0xfa, 0xaf]
    doc: Fixed file magic, identical across every sample examined.
  - id: version
    type: u4
    valid: 8
    doc: Always observed as 8 (0x00000008); presumed format/version tag.
  - id: root
    type: chunk
    doc: The single top-level chunk; always marker `block` in every sample seen.
instances:
  header:
    pos: 0x20
    type: resource_header
    doc: |
      Convenience shortcut for "named resource" files (`.cin`, `.et`,
      `.rgn`, `.map` in every sample checked): a `resource_header` record
      (see below) begins at fixed absolute offset 0x20, one level inside
      `root`'s single child block, whenever that child block's own child
      count (at offset 0x14) is 1. Parsed independently of the
      `chunk`/`block` structure above since it is not itself wrapped in
      a marker+len chunk header (see the block-chunk doc note).

      NOT applicable to `.loc` string-table files: their root's child
      block has child count 2 (not 1) and a different, un-decoded layout
      at 0x20 - accessing `header` on those will raise a validation
      error on `resource_header`'s `type_tag`, which is expected.
types:
  chunk:
    doc: One node of the tagged tree - a sentinel marker, a total-size field, then the rest of the chunk.
    seq:
      - id: marker
        type: u4
        enum: marker_type
      - id: len_total
        type: u4
        doc: Total size of this chunk in bytes, INCLUDING the 8 bytes of `marker`+`len_total` themselves.
      - id: body
        size: len_total - 8
        type:
          switch-on: marker
          cases:
            'marker_type::block': block_body
            _: leaf_body
  leaf_body:
    doc: Opaque payload of a `leaf`-marker chunk (or of any chunk this .ksy does not further decode).
    seq:
      - id: data
        size-eos: true
  block_body:
    doc: |
      Body layout of a `block`-marker chunk: a `u4` child count followed
      by child data. Only the root's single child is itself recursed
      into as another sentinel-marked `chunk` (matching every sample
      seen); deeper children are left as opaque `rest` bytes since they
      are raw typed records rather than further marker+len chunks (see
      the top-level doc and `resource_header`).
    seq:
      - id: num_children
        type: u4
      - id: rest
        size-eos: true
  resource_header:
    doc: |
      Observed layout of the record found at fixed offset 0x20 in every
      sample (`.cin`, `.loc`, `.et`, `.rgn`, `.map`): a small type tag,
      a hash/checksum, the resource's own 8-byte GUID (which matches the
      file's own base filename in hex), and a length-prefixed ASCII name.
    seq:
      - id: type_tag
        type: u4
        valid: 0x20
      - id: hash
        type: u4
        doc: Likely a CRC/hash of the name string; not verified.
      - id: guid
        size: 8
        doc: 8-byte id; hex-encoded, this equals the file's own base filename.
      - id: name_len
        type: u4
      - id: name
        type: str
        size: name_len
        encoding: ASCII
enums:
  marker_type:
    0xbbbbbbbb: block
    0xbebebebe: leaf
