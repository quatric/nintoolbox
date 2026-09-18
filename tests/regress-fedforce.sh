#!/bin/bash
# Federation Force regression tests (standalone).
#
# Metroid Prime: Federation Force (3DS, Next Level Games): LE .dict/.data
# pair whose chunk table carries B000 models (47 material vertex layouts),
# 7100 skeletons, B500 PICA textures, NLOC text and NLG font text.
# Fixtures are synthetic, built by tests/mk-fedforce-fixtures.py from the
# format notes in project/src/lib-fedforce.h (no retail bytes).
#
# Run from project/:  bash ../tests/regress-fedforce.sh
# Follows tests/regress.sh conventions (ok/no/sk counters). Wire into
# regress.sh once its in-progress rewrite settles.
B=./bin; PWD_PROJECT=$PWD
PASS=0; FAIL=0; SKIP=0
ok(){ printf "  PASS  %s\n" "$1"; PASS=$((PASS+1)); }
no(){ printf "  FAIL  %s -- %s\n" "$1" "$2"; FAIL=$((FAIL+1)); }
sk(){ printf "  SKIP  %s\n" "$1"; SKIP=$((SKIP+1)); }

fx="$PWD_PROJECT/../tests/fixtures"
m="$fx/fedforce_model_tri.fedmodel"; s="$fx/fedforce_skel_1bone.fedskel"
t="$fx/fedforce_tex_8x8.fedtex"; d="$fx/fedforce_dict.dict"
for f in "$m" "$s" "$t" "$d"; do
  [ -f "$f" ] || { sk "Federation Force ($f missing; run mk-fedforce-fixtures.py)"; echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"; exit 0; }
done
rm -rf /tmp/_r_fed; mkdir -p /tmp/_r_fed

# Standalone model -> GLB: 1 mesh, skeleton nodes, valid glTF.
if "$B/wmdlt" DECODE "$m" --dest /tmp/_r_fed/tri.glb --overwrite >/tmp/_r_fed.log 2>&1 \
&& [ -s /tmp/_r_fed/tri.glb ] \
&& [ "$(head -c4 /tmp/_r_fed/tri.glb)" = "glTF" ] \
&& python3 ../tests/validate-glb.py /tmp/_r_fed/tri.glb >/dev/null 2>&1 \
&& python3 -c '
import json, struct
b = open("/tmp/_r_fed/tri.glb", "rb").read()
ln, typ = struct.unpack_from("<II", b, 12)
assert typ == 0x4E4F534A, "first chunk is not JSON"
doc = json.loads(b[20:20+ln])
assert len(doc.get("meshes", [])) == 1, "expected exactly 1 mesh"
assert doc["meshes"][0]["primitives"], "mesh has no primitives"
assert len(doc.get("nodes", [])) >= 1, "expected skeleton nodes"
' 2>/dev/null; then
  ok "Federation Force model triangle -> valid 1-mesh glTF with joints"
else
  no "Federation Force model triangle" "wmdlt DECODE did not yield a valid 1-mesh glTF"
fi

# Standalone skeleton -> GLB carrying joints.
if "$B/wmdlt" DECODE "$s" --dest /tmp/_r_fed/skel.glb --overwrite >/dev/null 2>&1 \
&& python3 ../tests/validate-glb.py /tmp/_r_fed/skel.glb >/dev/null 2>&1; then
  ok "Federation Force skeleton -> valid glTF"
else
  no "Federation Force skeleton" "wmdlt DECODE did not yield a valid glTF"
fi

# Standalone texture -> PNG with exact texels (proves PICA untile order).
if "$B/wimgt" DECODE "$t" --dest /tmp/_r_fed/tex.png --overwrite >/dev/null 2>&1 \
&& [ -s /tmp/_r_fed/tex.png ] \
&& python3 ../tests/pngtool.py pixel /tmp/_r_fed/tex.png 0 0 255 0 0 255 2>/dev/null \
&& python3 ../tests/pngtool.py pixel /tmp/_r_fed/tex.png 7 7 255 224 240 255 2>/dev/null; then
  ok "Federation Force texture 8x8 -> PNG with exact texels"
else
  no "Federation Force texture" "wimgt DECODE did not yield the expected 8x8 PNG"
fi

# Full .dict/.data extraction: typed members + derived GLB/PNG/text.
if "$B/wszst" xx "$d" --dest /tmp/_r_fed/x --overwrite >/tmp/_r_fed_xx.log 2>&1 \
&& ls /tmp/_r_fed/x/*model.fedmodel >/dev/null 2>&1 \
&& ls /tmp/_r_fed/x/*model.fedmodel.glb >/dev/null 2>&1 \
&& ls /tmp/_r_fed/x/*tex.fedtex.png >/dev/null 2>&1 \
&& ls /tmp/_r_fed/x/*7020.txt >/dev/null 2>&1 \
&& ls /tmp/_r_fed/x/*7010.nlgfont >/dev/null 2>&1 \
&& python3 ../tests/validate-glb.py /tmp/_r_fed/x/*model.fedmodel.glb >/dev/null 2>&1 \
&& grep -q "12345678" /tmp/_r_fed/x/*7020.txt 2>/dev/null; then
  ok "Federation Force dict -> typed members + GLB/PNG/text"
else
  no "Federation Force dict" "wszst xx did not yield model GLB + texture PNG + NLOC text + font"
fi

echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
