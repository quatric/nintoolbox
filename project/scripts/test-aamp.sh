#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WSZST="$DIR/wszst"
if [[ ! -x "$WSZST" ]]; then
    WSZST="wszst"
fi

echo "Using wszst: $WSZST"
TMPDIR="$(mktemp -d -t test-aamp-XXXXXX)"
trap "rm -rf '$TMPDIR'" EXIT

cat << 'EOF' > "$TMPDIR/test_v2.aamp.yml"
aamp_version: 2
io_version: 0
type: xml
endian: little
param_root: !list
  objects:
    test_obj: !obj
      bool_t: true
      bool_f: false
      int_val: -42
      uint_val: 1000
      float_val: 12.345
      vec2_val: !vec2 [1.5, -2.5]
      vec3_val: !vec3 [1.0, 2.0, 3.0]
      vec4_val: !vec4 [0.1, 0.2, 0.3, 0.4]
      color_val: !color [1.0, 0.5, 0.25, 1.0]
      quat_val: !quat [0.0, 0.0, 0.0, 1.0]
      str32_val: !str32 short_name
      str64_val: !str64 longer_string_identifier
      str256_val: !str256 a_very_long_string_that_can_take_up_to_256_bytes_in_aamp
      strref_val: !strRef string_table_reference_value
      buf_int: !BufferInt [ 10, -20, 30, -40 ]
      buf_uint: !BufferUint [ 100, 200, 300 ]
      buf_float: !BufferFloat [ 1.1, 2.2, 3.3, 4.4 ]
      buf_bin: !BufferBinary AQIDBAUG
      curve1_val: !curve1 [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]
  lists:
    sub_list: !list
      objects:
        child_obj: !obj
          name: !str32 child_node
      lists: {}
EOF

echo "== 1. Create AAMP V2 =="
"$WSZST" create "$TMPDIR/test_v2.aamp.yml" --dest "$TMPDIR/test_v2.aamp"

echo "== 2. Dump AAMP V2 =="
"$WSZST" dump "$TMPDIR/test_v2.aamp"

echo "== 3. Decode to YAML =="
"$WSZST" text "$TMPDIR/test_v2.aamp" --dest "$TMPDIR/roundtrip_v2.yml"

echo "== 4. Decode to JSON =="
"$WSZST" text "$TMPDIR/test_v2.aamp" --dest "$TMPDIR/test_v2.json"
grep -q '"aamp_version": 2' "$TMPDIR/test_v2.json"
grep -q '"endian": "little"' "$TMPDIR/test_v2.json"

echo "== 5. Create from Roundtrip YAML =="
"$WSZST" create "$TMPDIR/roundtrip_v2.yml" --dest "$TMPDIR/rebuilt_v2.aamp"

cat << 'EOF' > "$TMPDIR/test_v1.aamp.yml"
aamp_version: 1
io_version: 0
type: xml
endian: big
param_root: !list
  objects:
    Actor: !obj
      name: !str32 Mario
      Speed: 25.0
      IsActive: true
      Count: 777
  lists: {}
EOF

echo "== 6. Create AAMP V1 Big-Endian =="
"$WSZST" create "$TMPDIR/test_v1.aamp.yml" --dest "$TMPDIR/test_v1.aamp"

echo "== 7. Dump AAMP V1 =="
"$WSZST" dump "$TMPDIR/test_v1.aamp"

echo "== 8. Decode AAMP V1 to YAML =="
"$WSZST" text "$TMPDIR/test_v1.aamp" --dest "$TMPDIR/roundtrip_v1.yml"
grep -q 'endian: big' "$TMPDIR/roundtrip_v1.yml"
grep -q 'aamp_version: 1' "$TMPDIR/roundtrip_v1.yml"

echo "== 9. Test XX extract and create =="
"$WSZST" XX "$TMPDIR/test_v1.aamp" --dest "$TMPDIR/extracted/"
[[ -f "$TMPDIR/extracted/test_v1.aamp.yml" ]]
rm -f "$TMPDIR/extracted/test_v1.aamp"
"$WSZST" create "$TMPDIR/extracted/test_v1.aamp.yml"
[[ -f "$TMPDIR/extracted/test_v1.aamp" ]]

echo "== ALL AAMP TESTS PASSED =="
