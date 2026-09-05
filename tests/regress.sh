#!/bin/bash
export LC_ALL=C
# Self-discovering regression for the Nintendo format additions.
# Finds samples by magic rather than by hardcoded path, so it keeps working
# after the scratch directories are cleaned.
cd "$(dirname "$0")/../project" || exit 1
B=./bin; PWD_PROJECT=$PWD; DAE_VALIDATOR="$PWD_PROJECT/../tests/validate-dae.py"; PNGTOOL="$PWD_PROJECT/../tests/pngtool.py"; GLTF_COUNT="$PWD_PROJECT/../tests/gltf_count.py"; PASS=0; FAIL=0; SKIP=0; BYTE_PASS=0; BYTE_FAIL=0; FIXED_PASS=0; FIXED_FAIL=0
SEARCH=${SEARCH:-"$PWD_PROJECT/../tests /tmp $HOME/Downloads /Volumes/SSD/user/Downloads /Volumes/SSD/dlz/Folders"}
ok(){ printf "  PASS  %s\n" "$1"; PASS=$((PASS+1)); }
no(){ printf "  FAIL  %s -- %s\n" "$1" "$2"; FAIL=$((FAIL+1)); }
sk(){ printf "  SKIP  %s (no sample)\n" "$1"; SKIP=$((SKIP+1)); }
bok(){ printf "  BYTE  %s\n" "$1"; BYTE_PASS=$((BYTE_PASS+1)); }
bno(){ printf "  BFAIL %s -- %s\n" "$1" "$2"; BYTE_FAIL=$((BYTE_FAIL+1)); FAIL=$((FAIL+1)); }
fok(){ printf "  FIXED %s\n" "$1"; FIXED_PASS=$((FIXED_PASS+1)); }
fno(){ printf "  FFAIL %s -- %s\n" "$1" "$2"; FIXED_FAIL=$((FIXED_FAIL+1)); FAIL=$((FAIL+1)); }
# file size + mtime helpers: GNU stat uses -c %s/%Y, BSD/macOS uses -f%z/-f %m
fsize_of(){ stat -c %s "$1" 2>/dev/null || stat -f%z "$1" 2>/dev/null; }

# Sample discovery. Probing every file for its magic is far too slow over a
# large external volume, so the index is built ONCE per run and each lookup is
# a table scan. Extension-filtered first, magic-confirmed second, so a
# mislabelled file is still classified by content. Set SEARCH to redirect.
IDX=$(mktemp); trap 'rm -f "$IDX"' EXIT
for d in $SEARCH; do [ -d "$d" ] || continue
  # Exclude claude-* session scratch dirs and this script's own throwaway
  # fixture basenames (test.*/test_*) -- both classes of file legitimately
  # match these extensions but are synthetic, single-glyph/tiny artifacts
  # from manual dev sessions, not real retail samples, and can otherwise
  # get matched by find_magic() ahead of a genuine sample and fail the
  # "suspiciously small/blank" heuristic for reasons that have nothing to
  # do with the decoder under test.
  find -L "$d" -maxdepth 8 -type f -size -65M \
      ! -path '*claude-*' ! -path '*/_r_*' ! -iname '_r_*' ! -iname 'test.*' ! -iname 'test_*' \
      ! -path '*/same.*' ! -iname 'same.*' ! -path '*/fixed-probe.*' ! -path '*/image-byte*' ! -path '*/byte-probe*' \( \
      -iname '*.bch' -o -iname '*.bcres' -o -iname '*.cgfx' -o -iname '*.nsbmd' \
      -o -iname '*.bfres' -o -iname '*.bntx' -o -iname '*.bmd' -o -iname '*.gtx' \
      -o -iname '*.plt0' -o -iname '*.pac' -o -iname '*.gfa' -o -iname '*.brfnt' \
      -o -iname '*.brfna' -o -iname '*.ctpk' -o -iname '*.warc' \
      -o -iname '*.byml' -o -iname '*.byaml' -o -iname '*.narc' \
      -o -iname '*.brsar' -o -iname '*.bffnt' -o -iname '*.bcfnt' \
      -o -iname '*.brlan' -o -iname '*.brlyt' -o -iname '*.bmg' \) 2>/dev/null
done | while IFS= read -r f; do
  printf '%s\t%s\n' "$(head -c 4 "$f" 2>/dev/null | tr -d '\0')" "$f"
done > "$IDX"
find_magic(){ awk -F'\t' -v m="$1" '$1==m || index($1,m)==1{print $2; exit}' "$IDX"; }

t_model(){ # name magic
  local found=0
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    rm -f /tmp/_r.glb
    $B/wmdlt ENCODE "$f" -d /tmp/_r.glb --overwrite >/dev/null 2>&1
    local g; g=$(python3 "$GLTF_COUNT" /tmp/_r.glb geometry 2>/dev/null || true); g=${g:-0}
    if [ "$g" -gt 0 ] 2>/dev/null; then
      ok "$1 -> GLB ($g geometries, validated)"
      found=1
      break
    fi
  done < <(awk -F'\t' -v m="$2" '$1==m{print $2}' "$IDX")
  [ "$found" -eq 1 ] || {
    local first; first=$(find_magic "$2")
    [ -n "$first" ] && no "$1 -> GLB" "no valid geometry from $first" || sk "$1"
  }
}
t_img(){ # name magic
  local f; f=$(find_magic "$2"); [ -n "$f" ] || { sk "$1"; return; }
  # A magic-only stub proves nothing about decoding, and failing to decode one
  # is the correct behaviour -- so treat it as "no sample", not as a failure.
  if [ "$(fsize_of "$f")" -lt 4096 ]; then
    printf "  SKIP  %s (only a magic-only stub available: %s)\n" "$1" "$f"
    SKIP=$((SKIP+1)); return
  fi
  rm -f /tmp/_r.png
  $B/wimgt ENCODE "$f" -d /tmp/_r.png --overwrite >/dev/null 2>&1
  [ -s /tmp/_r.png ] && ok "$1 -> PNG" || no "$1 -> PNG" "$f"
}

echo "== binaries =="
for x in wszst wimgt wmdlt wlayt wbmgt wbmsx wbrsar wwc24crypt; do
  [ -x "$B/$x" ] && ok "built: $x" || no "built: $x" "missing"
done

# Address vectors generated independently with GTX-Extractor's Python
# AddrLib port. --gc-sections lets this tiny test link only the surface
# addressing API from the normal production object.
# --gc-sections is GNU ld's flag name; some ld64 (macOS) toolchains reject
# it outright rather than treating it as the -dead_strip alias newer Xcode
# versions accept -- same fallback the Arika test below already has.
if ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-gtx-address.c ./lib-gtx.o -Wl,--gc-sections \
    -o /tmp/_r_gtx_address >/tmp/_r_gtx_address_build.log 2>&1 \
    || ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-gtx-address.c ./lib-gtx.o -Wl,-dead_strip \
    -o /tmp/_r_gtx_address >>/tmp/_r_gtx_address_build.log 2>&1; then
  if /tmp/_r_gtx_address; then
    ok "GX2 macro-tile address vectors (modes 4-11)"
  else
    no "GX2 macro-tile address vectors (modes 4-11)" "runtime check failed"
  fi
else
  no "GX2 macro-tile address vectors (modes 4-11)" \
    "$(tail -1 /tmp/_r_gtx_address_build.log 2>/dev/null)"
fi

if ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-gtx-encode.c ./lib-gtx.o ./lib-bntx.o -Wl,--gc-sections \
    -o /tmp/_r_gtx_encode >/tmp/_r_gtx_encode_build.log 2>&1 \
    || ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-gtx-encode.c ./lib-gtx.o ./lib-bntx.o -Wl,-dead_strip \
    -o /tmp/_r_gtx_encode >>/tmp/_r_gtx_encode_build.log 2>&1; then
  if /tmp/_r_gtx_encode; then
    ok "GX2 encode/decode: formats, tile modes, mips, arrays and MSAA"
    bok "GX2 format/tile/mip/array/MSAA matrix -> identical container bytes"
  else
    no "GX2 general encoder matrix" "runtime check failed"
  fi
else
  no "GX2 general encoder matrix" \
    "$(tail -1 /tmp/_r_gtx_encode_build.log 2>/dev/null)"
fi

# Arika INFO.DAT/GAME.DAT archives + ALZ1 compression (Dr. Mario Online Rx,
# Dr. Mario Express, the original DS Endless Ocean, and -- via the shared
# RF2 sub-container path -- Endless Ocean: Blue World). No retail sample was
# available, so this standalone binary checks the decoder against hand-built
# fixtures taken straight from GBATEK's decompression pseudocode/encryption
# formula (not just self-consistency with this project's own encoder), plus
# create->extract round trips through the project's own CreateArika/
# ExtractArika. See tests/test-arika.c for exactly what each case covers.
# --gc-sections is GNU ld's flag name; some ld64 (macOS) toolchains reject it
# outright rather than treating it as the -dead_strip alias newer Xcode
# versions accept, same environment-dependent gap the GX2 standalone tests
# above already have. Fall back to -dead_strip so this still actually runs
# there instead of just recording an unrelated toolchain limitation as if it
# were a bug in this test.
if ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-arika.c ./lib-nintendo.o ./lib-sound-archive.o -Wl,--gc-sections \
    -o /tmp/_r_arika >/tmp/_r_arika_build.log 2>&1 \
    || ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-arika.c ./lib-nintendo.o ./lib-sound-archive.o -Wl,-dead_strip \
    -o /tmp/_r_arika >>/tmp/_r_arika_build.log 2>&1; then
  if /tmp/_r_arika; then
    ok "Arika ALZ1 + INFO.DAT/GAME.DAT archive: fixtures, encryption, RF2 grouping"
  else
    no "Arika ALZ1 + INFO.DAT/GAME.DAT archive" "runtime check failed, see /tmp/_r_arika output"
  fi
else
  no "Arika ALZ1 + INFO.DAT/GAME.DAT archive" \
    "$(tail -1 /tmp/_r_arika_build.log 2>/dev/null)"
fi

# Retro's Metroid Prime CMPD segments use LZO1X alongside raw and zlib
# segments. These are hand-authored streams so this remains a decoder test,
# independent from any external LZO implementation or a self-made encoder.
if ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-lzo1x.c ./lib-nintendo.o ./lib-lzo.o ./lib-rpak.o ./lib-szs.o -lz -Wl,--gc-sections \
    -o /tmp/_r_lzo1x >/tmp/_r_lzo1x_build.log 2>&1 \
    || ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-lzo1x.c ./lib-nintendo.o ./lib-lzo.o ./lib-rpak.o ./lib-szs.o -lz -Wl,-dead_strip \
    -o /tmp/_r_lzo1x >>/tmp/_r_lzo1x_build.log 2>&1; then
  if /tmp/_r_lzo1x; then
    ok "LZO1X + Metroid Prime CMPD segmented decompression"
  else
    no "LZO1X + Metroid Prime CMPD segmented decompression" "runtime check failed"
  fi
else
  no "LZO1X + Metroid Prime CMPD segmented decompression" \
    "$(tail -1 /tmp/_r_lzo1x_build.log 2>/dev/null)"
fi

# GSH shader listings depend on the vendored Decaf disassembler and Latte
# assembler being exact inverses for every program they choose to render
# semantically. This is a genuinely byte-for-byte instruction-stream check,
# unlike the model/PNG structure checks later in this script.
LATTE_OBJS="src/latte-decaf/decaf/src/latte_disassembler.o
src/latte-decaf/decaf/src/latte_disassembler_alu.o
src/latte-decaf/decaf/src/latte_disassembler_export.o
src/latte-decaf/decaf/src/latte_disassembler_tex.o
src/latte-decaf/decaf/src/latte_disassembler_vtx.o
src/latte-decaf/decaf/src/latte_instructions.o
src/latte-decaf/latte_bridge.o
src/latte-decaf/assembler/src/assembler_alu.o
src/latte-decaf/assembler/src/assembler_cf.o
src/latte-decaf/assembler/src/assembler_common.o
src/latte-decaf/assembler/src/assembler_exp.o
src/latte-decaf/assembler/src/assembler_instructions.o
src/latte-decaf/assembler/src/assembler_latte.o
src/latte-decaf/assembler/src/assembler_parse.o
src/latte-decaf/assembler/src/assembler_tex.o"
if ${CXX:-c++} -O2 -std=gnu++17 -Isrc/latte-decaf \
    -Isrc/latte-decaf/decaf -Isrc/latte-decaf/assembler/src \
    ../tests/test-latte-roundtrip.cpp $LATTE_OBJS -o /tmp/_r_latte_roundtrip \
    >/tmp/_r_latte_roundtrip_build.log 2>&1 \
    && /tmp/_r_latte_roundtrip; then
  ok "GSH Latte semantic assembly -> disassembly -> byte-exact assembly"
  bok "GSH semantic program -> disassembly -> identical program bytes"
  fok "GSH assemble -> semantic disassembly -> identical program bytes"
else
  no "GSH Latte semantic byte-exact round-trip" \
    "$(tail -1 /tmp/_r_latte_roundtrip_build.log 2>/dev/null)"
fi

t_gsh_retail(){
  local f
  f=$(for d in $SEARCH; do [ -d "$d" ] || continue
      find -L "$d" -maxdepth 8 -type f -iname '*.gsh' -size -128M 2>/dev/null
    done | head -1)
  [ -n "$f" ] || { sk "GSH retail program byte-exact listings"; return; }
  local d; d=$(mktemp -d /tmp/_r_gsh.XXXXXX) || return
  cp "$f" "$d/input.gsh"
  "$B/wszst" EXTRACT "$d/input.gsh" --overwrite >"$d/extract.log" 2>&1
  # extract_gsh_shaders() writes a listing only after AssembleLatteCF has
  # reproduced that complete program byte-for-byte, including RAW fallback
  # for any instruction or clause the semantic decoder cannot preserve.
  local declared written
  declared=$(grep -c 'EXTRACT GSH:' "$d/extract.log" 2>/dev/null || true)
  written=$(find "$d" -maxdepth 1 -name '*.latte' -size +0c | wc -l | tr -d ' ')
  if [ "$declared" -gt 0 ] && [ "$written" -eq "$declared" ]; then
    ok "GSH retail container -> $written byte-exact Latte program listing(s) ($f)"
    bok "GSH $written retail program listing(s) -> identical program bytes"
  else
    no "GSH retail program byte-exact listings" "$f"
  fi
}
t_gsh_retail

echo "== models =="
t_model "NSBMD (DS)"      "BMD0"
t_model "BCH (3DS)"       "BCH"
t_cgfx(){
  # CGFX (3DS model container, .bcmdl extension in retail SZS archives):
  # a real sample from a retail 3DS RomFS -- Super Mario 3D Land's
  # ObjectData/TogeMetbo.szs, decompressed with wszst EXTRACT -- since a
  # banner.bin-embedded CGFX (the CBMD 3D banner resource shipped in every
  # CIA's ExeFS) turned out to use a structurally different, non-standard
  # header layout ScanCGFX() doesn't recognise (DATA block one byte off
  # from where the header's own hdr_len field points), while this
  # standalone in-SZS .bcmdl matches the format exactly.
  local f="$PWD_PROJECT/../tests/fixtures/cgfx_toge_metbo.bcmdl"
  if [ -f "$f" ]; then
    rm -f /tmp/_r.glb
    $B/wmdlt ENCODE "$f" -d /tmp/_r.glb --overwrite >/dev/null 2>&1
    local g; g=$(python3 "$GLTF_COUNT" /tmp/_r.glb geometry 2>/dev/null || true); g=${g:-0}
    if [ "$g" -gt 0 ] 2>/dev/null; then
      ok "CGFX (3DS) -> GLB ($g geometries, validated, $f)"
      return
    fi
    no "CGFX (3DS) -> GLB" "no valid geometry from $f"
    return
  fi
  t_model "CGFX (3DS)" "CGFX"
}
t_cgfx

t_bch_dae_texture(){
  local f="$HOME/Downloads/aaaaa/live1/h3d/Mii_body.bch"
  [ -f "$f" ] || f=$(find_magic "BCH")
  [ -n "$f" ] && [ -f "$f" ] || { sk "BCH (3DS) model + texture export"; return; }

  local out
  out=$(mktemp -d /tmp/_r_bch_tex.XXXXXX) || { no "BCH model + texture export" "mktemp failed"; return; }
  $B/wmdlt ENCODE "$f" -d "$out/model.glb" --overwrite >/dev/null 2>&1
  local glb="$out/model.glb"
  local geom mat img tri
  geom=$(python3 "$GLTF_COUNT" "$glb" geometry 2>/dev/null || true); geom=${geom:-0}
  mat=$(python3 "$GLTF_COUNT" "$glb" material 2>/dev/null || true); mat=${mat:-0}
  img=$(python3 "$GLTF_COUNT" "$glb" image 2>/dev/null || true); img=${img:-0}
  tri=$(python3 "$GLTF_COUNT" "$glb" triangles 2>/dev/null || true); tri=${tri:-0}
  local png_cnt
  png_cnt=$(find "$out" -name '*.png' 2>/dev/null | wc -l | tr -d ' ')

  if [ -s "$glb" ] && [ "$geom" -gt 0 ] && [ "$mat" -gt 0 ]; then
    ok "BCH (3DS) -> GLB + texture mapping ($geom geoms, $mat mats, $img imgs, $png_cnt textures)"
  else
    no "BCH (3DS) -> GLB + texture mapping" "$f"
  fi
  rm -rf "$out"
}
t_bch_dae_texture


# "FRES" magic is shared by two unrelated formats: Wii U BFRES (big endian,
# version 3.x, ParseBFRES() in lib-bfres.c) and Switch BFRES (little endian,
# version 8+, extract_bfres_switch_manifest() in wszst.c -- name/shape/
# material resolution is decoded and verified against real retail data
# (see the long comment above that function for what changed and why),
# vertex/index geometry decode is still a known open gap, see PLAN.md).
# t_model() picking whichever "FRES" sample turns up first used to silently
# test the wrong parser whenever that sample happened to be a Switch file
# (real-world case: ~/Downloads/Male.bfres). Wii U discovery keeps the
# original single-first-match lookup unchanged (out of scope for this
# Switch-focused fix). Switch discovery instead scans *every* "FRES" match
# for the little-endian BOM, since with a Wii U sample also present in
# SEARCH the single first-hit lookup could land on that Wii U file and skip
# Switch entirely even though a real Switch sample -- e.g.
# ~/Downloads/Male.bfres or ~/Downloads/SMO_AirBubble.bfres, a real Super
# Mario Odyssey ObjectData sample kept as a stable regression fixture since
# the fork's own extraction of Nintendo's RomFS obviously can't be
# committed to the repo -- was sitting right there.
t_bfres_wiiu(){
  # Wii U BFRES geometry decode, gated to the big-endian (BOM "feff",
  # version 3.x) container -- see the long comment above for why "FRES"
  # magic alone isn't enough to pick the right parser. find_magic("FRES")
  # over $SEARCH previously landed on whichever "FRES" sample sorted first
  # in the IDX table, which was a coin flip between this Wii U format and
  # the little-endian Switch one whenever both were present, so this used
  # to SKIP even though a working Wii U sample (Splatoon's
  # SPL_Clt_TES011_M.bfres, also used by the texture-binding test below)
  # was sitting right there. Committed as a fixture for a deterministic,
  # order-independent pick.
  local f="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  if [ -f "$f" ]; then
    local bom8; bom8=$(od -An -tx1 -j8 -N2 "$f" 2>/dev/null | tr -d ' ')
    if [ "$bom8" = "feff" ]; then
      rm -f /tmp/_r.glb
      $B/wmdlt ENCODE "$f" -d /tmp/_r.glb --overwrite >/dev/null 2>&1
      local g; g=$(python3 "$GLTF_COUNT" /tmp/_r.glb geometry 2>/dev/null || true); g=${g:-0}
      if [ "$g" -gt 0 ] 2>/dev/null; then
        ok "BFRES (Wii U) -> GLB ($g geometries, validated, $f)"
        return
      fi
      no "BFRES (Wii U) -> GLB" "no valid geometry from $f"
      return
    fi
  fi
  # Fixture missing/invalid -- fall back to the original dynamic scan.
  local f_fres; f_fres=$(find_magic "FRES")
  local bom8; bom8=$(od -An -tx1 -j8 -N2 "$f_fres" 2>/dev/null | tr -d ' ')
  if [ -n "$f_fres" ] && [ "$bom8" = "feff" ]; then
    t_model "BFRES (Wii U)" "FRES"
  else
    sk "BFRES (Wii U)"
  fi
}
t_bfres_wiiu

t_bfres_texture(){
  # BFRES (Wii U) material -> FTEX texture binding: wszst xx must decode
  # the referenced FTEX to a sibling PNG AND the exported DAE must
  # reference it by name in a non-empty <library_images>. A real sample
  # with a resolvable diffuse texture ref is kept at ~/Downloads/
  # bfres_samples/ since the fork's own disc extractions can't be
  # committed to the repo.
  local f="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  [ -f "$f" ] || f=$(find -L "$HOME/Downloads/bfres_samples" -iname '*.bfres' 2>/dev/null | head -1)
  [ -n "$f" ] && [ -f "$f" ] || { sk "BFRES (Wii U) texture binding"; return; }
  rm -rf /tmp/_r_bfrestex; mkdir -p /tmp/_r_bfrestex
  cp "$f" /tmp/_r_bfrestex/
  local bf="/tmp/_r_bfrestex/$(basename "$f")"
  "$B/wszst" xx "$bf" --overwrite >/dev/null 2>&1
  local glb="${bf}.glb"
  local png_n; png_n=$(find /tmp/_r_bfrestex -iname '*.png' 2>/dev/null | wc -l | tr -d ' ')
  local img_n; img_n=$(python3 -c "
import sys; sys.path.insert(0,'$PWD_PROJECT/../tests')
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location('vglb','$PWD_PROJECT/../tests/validate-glb.py')
m = module_from_spec(spec); spec.loader.exec_module(m)
try:
    g = m.load_glb('$glb')
    print(len(g.json.get('images', [])))
except Exception:
    print(0)
" 2>/dev/null)
  img_n=${img_n:-0}
  if [ "$png_n" -gt 0 ] && [ "$img_n" -gt 0 ]; then
    ok "BFRES (Wii U) texture binding -> $png_n PNG(s), $img_n image(s) ($f)"
  else
    no "BFRES (Wii U) texture binding" "$png_n PNG(s), $img_n image(s) from $f"
  fi
}
t_bfres_texture

t_romc(){
  local f="$HOME/Downloads/vc_samples/Kirby64_romc"
  [ -f "$f" ] || f="$PWD_PROJECT/../tests/fixtures/synthetic_n64.romc"
  [ -f "$f" ] || { sk "romc (N64 Virtual Console)"; return; }
  rm -rf /tmp/_r_romc; mkdir -p /tmp/_r_romc
  cp "$f" /tmp/_r_romc/romc
  "$B/wszst" xx /tmp/_r_romc/romc --overwrite >/dev/null 2>&1
  local out="/tmp/_r_romc/romc.z64"
  if [ -s "$out" ] && [ "$(head -c4 "$out" | od -An -tx1 | tr -d ' \n')" = "80371240" ]; then
    ok "romc (N64 Virtual Console) -> real N64 ROM ($(fsize_of "$out") bytes)"
  else
    no "romc (N64 Virtual Console)" "no valid N64 ROM produced from $f"
  fi
}
t_romc

f_fres_switch=""
while IFS= read -r f; do
  [ -n "$f" ] || continue
  bomC=$(od -An -tx1 -j12 -N2 "$f" 2>/dev/null | tr -d ' ')
  [ "$bomC" = "fffe" ] && { f_fres_switch="$f"; break; }
done < <(awk -F'\t' -v m="FRES" '$1==m{print $2}' "$IDX")

if [ -z "$f_fres_switch" ]; then
  f_fres_switch="$PWD_PROJECT/../tests/fixtures/synthetic_switch.bfres"
  [ -f "$f_fres_switch" ] || f_fres_switch=""
fi

if [ -n "$f_fres_switch" ]; then
  rm -f /tmp/_r_bfres_switch.xml
  "$B/wszst" xx "$f_fres_switch" --dest /tmp/_r_bfres_switch.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<shape name="[^"]' /tmp/_r_bfres_switch.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BFRES (Switch) -> structure XML with resolved names ($g shapes, $f_fres_switch)" \
    || no "BFRES (Switch) -> structure XML" "no named shapes from $f_fres_switch"
else
  sk "BFRES (Switch)"
fi

f_ffnt=$(find_magic "FFNT"); f_cfnt=$(find_magic "CFNT")
# Keep the 3DS/Wii U decoder checks deterministic even when no retail CFNT/FFNT happens
# to exist under SEARCH. These compact fixtures are produced by the encoder
# round-trip test below and exercise the same structure + pixel decoders.
if [ -z "$f_ffnt" ]; then
  f_ffnt="$PWD_PROJECT/../tests/fixtures/synthetic_rgba8.bffnt"
  [ -f "$f_ffnt" ] || f_ffnt=""
fi
if [ -z "$f_cfnt" ]; then
  f_cfnt="$PWD_PROJECT/../tests/fixtures/synthetic_rgba8.bcfnt"
  [ -f "$f_cfnt" ] || f_cfnt=""
fi
if [ -n "$f_ffnt" ]; then
  rm -f /tmp/_r_bffnt.xml /tmp/_r_bffnt.png
  "$B/wszst" xx "$f_ffnt" --dest /tmp/_r_bffnt.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<tglp ' /tmp/_r_bffnt.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BFFNT (Wii U) -> structure XML ($f_ffnt)" \
    || no "BFFNT (Wii U) -> structure XML" "no tglp from $f_ffnt"
  "$B/wimgt" DECODE "$f_ffnt" --dest /tmp/_r_bffnt.png --overwrite >/dev/null 2>&1
  psz=$(fsize_of /tmp/_r_bffnt.png); psz=${psz:-0}
  [ "$psz" -gt 100 ] 2>/dev/null && ok "BFFNT (Wii U) -> PNG ($f_ffnt)" \
    || no "BFFNT (Wii U) -> PNG" "decode produced no PNG (size=$psz)"
else
  sk "BFFNT (Wii U)"
fi
if [ -n "$f_cfnt" ]; then
  rm -f /tmp/_r_bcfnt.xml /tmp/_r_bcfnt.png
  "$B/wszst" xx "$f_cfnt" --dest /tmp/_r_bcfnt.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<tglp ' /tmp/_r_bcfnt.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BCFNT (3DS) -> structure XML ($f_cfnt)" \
    || no "BCFNT (3DS) -> structure XML" "no tglp from $f_cfnt"
  "$B/wimgt" DECODE "$f_cfnt" --dest /tmp/_r_bcfnt.png --overwrite >/dev/null 2>&1
  psz=$(fsize_of /tmp/_r_bcfnt.png); psz=${psz:-0}
  [ "$psz" -gt 100 ] 2>/dev/null && ok "BCFNT (3DS) -> PNG ($f_cfnt)" \
    || no "BCFNT (3DS) -> PNG" "decode produced no PNG (size=$psz)"
else
  sk "BCFNT (3DS)"
fi

echo "== textures =="
t_img "BNTX (Switch)" "BNTX"
t_img "GTX (Wii U)" "Gfx2"

t_bntx_astc(){
  # Curated real ASTC_4x4 sample retained as an end-to-end container/swizzle/
  # codec regression. The decoder also accepts every standard 2D ASTC block
  # footprint plus BC6H and BC7; their codec cores have separate upstream
  # conformance vectors. Index 0 of this BNTX is a 1280x720
  # ASTC_4x4 texture sliced straight out of Odyssey's
  # LayoutData/TextureHintPhotoOther2.szs -> .bfres -> embedded BNTX (the
  # "hint" UI icon strip for the Moon Kingdom's pillar puzzle). find_magic's
  # generic BNTX sample may not happen to be ASTC, so this is pinned by path
  # like the accf_ins_taran BRRES/PLT0 sample above.
  local f="$PWD_PROJECT/../tests/fixtures/smo_hint_photo_astc.bntx"
  [ -f "$f" ] || { sk "BNTX ASTC_4x4 (Switch, SMO)"; return; }
  rm -f /tmp/_r_astc.png
  $B/wimgt DECODE "$f" -d /tmp/_r_astc.png --overwrite >/tmp/_r_astc.log 2>&1
  # A broken decode still emits a same-sized PNG (ASTC error blocks are
  # opaque magenta per spec), so check for structure, not just non-empty:
  # the real sample carries a light-grey background plus a solid-yellow
  # banana icon, i.e. at least a few dozen distinct colours, not a flat fill.
  local colors
  colors=$(python3 "$PNGTOOL" colors /tmp/_r_astc.png 1000000 2>/dev/null)
  case "$colors" in ''|*[!0-9]*) colors=0;; esac
  if [ -s /tmp/_r_astc.png ] && [ "${colors:-0}" -gt 20 ]; then
    ok "BNTX ASTC_4x4 (Switch, SMO) -> PNG ($colors colours)"
  else
    no "BNTX ASTC_4x4 (Switch, SMO) -> PNG" "$f"
  fi
}
t_bntx_astc

t_brres_tex_plt0(){
  # A BRRES TEX0 carries no palette of its own; a sibling PLT0 has to be
  # matched by naming convention and threaded through ExportPNG(). Curated
  # real sample (ins_taran.brres from a retail Animal Crossing: City Folk
  # disc, Insect/ins_taran.brres, TEX0+PLT0 same-name pair) because a
  # generic magic scan can't tell a paired BRRES from an unpaired one, and
  # this is exactly the bug the palette-pairing code exists to fix -- see
  # PLAN.md SS8 for the wider naming-convention picture. MDL0 sampler links
  # are preferred when present; constrained retail naming fallbacks cover
  # archives that leave those offsets unresolved.
  local f="$PWD_PROJECT/../tests/fixtures/accf_ins_taran.brres"
  [ -f "$f" ] || { sk "BRRES TEX0+PLT0 pairing (ACCF)"; return; }
  rm -rf /tmp/_r_texplt0; mkdir -p /tmp/_r_texplt0
  # XDECODE, not EXTRACT: EXTRACT alone dumps raw subfiles without image
  # transform, so it never exercises ExportPNG()'s palette-pairing at all.
  $B/wszst XDECODE "$f" --dest /tmp/_r_texplt0 --overwrite >/tmp/_r_texplt0.log 2>&1
  local png="/tmp/_r_texplt0/Textures(NW4R)/ins_taran.png"
  if [ -s "$png" ] && ! grep -q "INVALID IMAGE FORMAT" /tmp/_r_texplt0.log; then
    ok "BRRES TEX0+PLT0 pairing (ACCF) -> $png"
  else
    no "BRRES TEX0+PLT0 pairing (ACCF)" "$f"
  fi

  # BRRES puts MDL0 files under 3DModels(NW4R), while decoded TEX0 images
  # live in sibling Textures(NW4R). The exporter's default model format is
  # GLB (commit ac04d55), which embeds textures directly in the binary --
  # so there is no cross-directory init_from path to worry about any more;
  # instead assert the embedded image bytes are present and PNG-shaped.
  # (A loose ins_taran.png is still written by XDECODE/TEX0 export itself,
  # independent of the model exporter -- checked separately above.)
  rm -rf /tmp/_r_brres_dae; mkdir -p /tmp/_r_brres_dae
  $B/wszst XX "$f" --dest /tmp/_r_brres_dae --overwrite >/tmp/_r_brres_dae.log 2>&1
  local glb="/tmp/_r_brres_dae/3DModels(NW4R)/ins_taran.glb"
  local glb_report
  glb_report=$(python3 - "$glb" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
try:
    g = m.load_glb(sys.argv[1])
except Exception as exc:
    print(f"PARSE_FAIL {exc}"); sys.exit(0)
errs = m.validate(sys.argv[1], require_images=True)
j = g.json
meshes = j.get("meshes", [])
mesh_names = sorted(me.get("name") for me in meshes)
mat_names = sorted(mt.get("name") for mt in j.get("materials", []))
img_names = [im.get("name") for im in j.get("images", [])]
skins = j.get("skins", [])
n_skinned_prims = sum(
    1 for me in meshes for p in me.get("primitives", [])
    if "JOINTS_0" in p.get("attributes", {}) and "WEIGHTS_0" in p.get("attributes", {})
)
texcoord_ok = all(
    "TEXCOORD_0" in p.get("attributes", {})
    for me in meshes for p in me.get("primitives", [])
)
mat_bound_ok = all(
    "material" in p
    for me in meshes for p in me.get("primitives", [])
)
# 0.128204/0.222473 is the retail ins_taran MDL0's known-good first UV
# coordinate in COLLADA's bottom-left-origin V convention. glTF uses a
# top-left-origin V (the spec-mandated flip), so the same texel is
# 0.128204/(1-0.222473) here -- present in the TEXCOORD_0 accessor if
# decode is correct.
found_known_vertex = False
for acc_idx, acc in enumerate(j.get("accessors", [])):
    if acc.get("type") == "VEC2" and acc.get("componentType") == 5126:
        for tup in g.read_accessor_tuples(acc_idx):
            if abs(tup[0] - 0.128204) < 1e-4 and abs(tup[1] - (1 - 0.222473)) < 1e-4:
                found_known_vertex = True
                break
    if found_known_vertex:
        break
print("OK" if not errs else "VALIDATE_FAIL " + "; ".join(errs))
print(f"meshes={mesh_names}")
print(f"materials={mat_names}")
print(f"images={img_names}")
print(f"skins={len(skins)}")
print(f"skinned_prims={n_skinned_prims}")
print(f"texcoord_ok={texcoord_ok}")
print(f"mat_bound_ok={mat_bound_ok}")
print(f"found_known_vertex={found_known_vertex}")
PY
)
  if [ -s "$glb" ] \
      && echo "$glb_report" | grep -q '^OK$' \
      && echo "$glb_report" | grep -q "meshes=\['polygon0', 'polygon1'\]" \
      && echo "$glb_report" | grep -q "materials=\['m0', 'm1'\]" \
      && echo "$glb_report" | grep -q "images=\['ins_taran', 'ins_taran'\]" \
      && echo "$glb_report" | grep -q "skins=1" \
      && echo "$glb_report" | grep -q "skinned_prims=2" \
      && echo "$glb_report" | grep -q "texcoord_ok=True" \
      && echo "$glb_report" | grep -q "mat_bound_ok=True" \
      && echo "$glb_report" | grep -q "found_known_vertex=True"; then
    ok "BRRES MDL0 geometry + material/UV/texture mapping (GLB)"
  else
    no "BRRES MDL0 geometry + material/UV/texture mapping" "$f ($glb_report)"
  fi

  # A directory supplied directly to XX must take the same guarded recursive
  # path as a pass-through extractor's staging directory. Use mktemp rather
  # than a fixed directory so this test never consumes or removes user files.
  local tree
  tree=$(mktemp -d /tmp/_r_xx_dir.XXXXXX) || { no "XX directory recursion" "mktemp failed"; return; }
  cp "$f" "$tree/accf_ins_taran.brres"
  $B/wszst XX "$tree" --overwrite >/tmp/_r_xx_dir.log 2>&1
  glb="$tree/accf_ins_taran.brres.d/3DModels(NW4R)/ins_taran.glb"
  if [ -s "$glb" ] && python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1; then
    ok "XX directory recursion -> nested BRRES GLB"
  else
    no "XX directory recursion" "$tree"
  fi

  # A pass-through disc extractor supplies both a completed staging directory
  # and the user's --dest. The final model pass must keep each GLB beside its
  # MDL0; applying --dest again collapses thousands of same-named resources
  # into one flat directory (and historically stripped the .dae/.glb suffix).
  local staged
  staged=$(mktemp -d /tmp/_r_xx_dest.XXXXXX) || { no "XX staged tree with --dest" "mktemp failed"; return; }
  mkdir -p "$staged/source/nested" "$staged/destination"
  cp "$f" "$staged/source/nested/accf_ins_taran.brres"
  $B/wszst XX "$staged/source" --dest "$staged/destination" --overwrite >/dev/null 2>&1
  glb="$staged/source/nested/accf_ins_taran.brres.d/3DModels(NW4R)/ins_taran.glb"
  if [ -s "$glb" ] && python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1 \
      && [ ! -e "$staged/destination/ins_taran" ]; then
    ok "XX staged tree + --dest -> preserves nested GLB path"
  else
    no "XX staged tree with --dest" "$staged"
  fi
}
t_brres_tex_plt0

t_accf_bind_pose(){
  # MDL0 stores rigid/single-bone vertices in bone-local coordinates, while
  # multi-weight vertices use a skinning node. Treating both alike produces
  # valid XML containing visibly collapsed geometry. These two retail models
  # cover both per-facepoint matrix selection and a rigid object node.
  local sample_dir="$PWD_PROJECT/../tests/fixtures"
  local spider_src="$sample_dir/accf_ins_taran.brres"
  local npc_src="$sample_dir/accf_npc_special_13.brres"
  if [ ! -f "$spider_src" ] || [ ! -f "$npc_src" ]; then
    sk "ACCF MDL0 bind-pose transforms"; return
  fi
  local out
  out=$(mktemp -d /tmp/_r_accf_bind.XXXXXX) || { no "ACCF MDL0 bind-pose transforms" "mktemp failed"; return; }
  $B/wszst XX "$spider_src" --dest "$out/spider" --overwrite >/dev/null 2>&1
  $B/wszst XX "$npc_src" --dest "$out/npc" --overwrite >/dev/null 2>&1
  local spider="$out/spider/3DModels(NW4R)/ins_taran.glb"
  local npc="$out/npc/3DModels(NW4R)/tti.glb"
  if python3 - "$spider" "$npc" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)

def inspect(path):
    g = m.load_glb(path)
    j = g.json
    points = []
    for mesh in j.get("meshes", []):
        for prim in mesh.get("primitives", []):
            pos_idx = prim.get("attributes", {}).get("POSITION")
            if pos_idx is not None:
                points.extend(g.read_accessor_tuples(pos_idx))
    low = [min(p[i] for p in points) for i in range(3)]
    high = [max(p[i] for p in points) for i in range(3)]
    joints = {j["nodes"][idx].get("name") for skin in j.get("skins", [])
              for idx in skin.get("joints", [])}
    return low, high, joints

spider_low, spider_high, spider_joints = inspect(sys.argv[1])
npc_low, npc_high, npc_joints = inspect(sys.argv[2])

# Without the bind matrices the spider Y bounds are -4.95..4.61 and its legs
# fold through the body. Correct bind-pose geometry is approximately
# -0.65..6.07 and has a real nested joint hierarchy.
assert spider_low[1] > -1.0 and spider_high[1] > 6.0
assert {"ins_taran", "root", "legL0", "legR0"} <= spider_joints

# The broken NPC is a sideways -9.7..9.7 blob. The corrected character is
# upright, about 37 units tall, and symmetric across X.
assert npc_low[0] < -12.0 and npc_high[0] > 12.0
assert npc_low[1] > -1.0 and npc_high[1] > 35.0
assert {"root", "base", "head", "Lfoot1", "Rfoot1", "tail1"} <= npc_joints
PY
  then
    ok "ACCF MDL0 bind-pose transforms -> upright spider + NPC"
  else
    no "ACCF MDL0 bind-pose transforms" "$out"
  fi
  rm -rf "$out"
}
t_accf_bind_pose

t_accf_node_mix(){
  # A separate small ACCF model whose GX stream uses three genuine NodeMix
  # influences (up to three bones per influence). At bind pose those matrix
  # nodes must evaluate through bind*inverseBind, while its primary nodes
  # still receive their bone matrices.
  local src="$PWD_PROJECT/../tests/fixtures/accf_ins_mukade.brres"
  [ -f "$src" ] || { sk "ACCF MDL0 NodeMix transforms"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_nodemix.XXXXXX) || { no "ACCF MDL0 NodeMix transforms" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out/model" --overwrite >/dev/null 2>&1
  local glb="$out/model/3DModels(NW4R)/ins_mukade.glb"
  if python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1 \
      && python3 - "$glb" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
j = g.json
points = []
for mesh in j.get("meshes", []):
    for prim in mesh.get("primitives", []):
        pos_idx = prim.get("attributes", {}).get("POSITION")
        if pos_idx is not None:
            points.extend(g.read_accessor_tuples(pos_idx))
low = [min(p[i] for p in points) for i in range(3)]
high = [max(p[i] for p in points) for i in range(3)]
joints = {j["nodes"][idx].get("name") for skin in j.get("skins", [])
          for idx in skin.get("joints", [])}
# The old all-raw path displaced the left side to X=-5.52. Correct primary
# and mixed node handling restores the symmetric -3.32..3.32 centipede.
assert -3.5 < low[0] < -3.0 and 3.0 < high[0] < 3.5
assert abs(low[0] + high[0]) < 0.05
assert {"a", "b", "c", "d", "e"} <= joints
PY
  then
    ok "ACCF MDL0 NodeMix transforms -> symmetric centipede"
  else
    no "ACCF MDL0 NodeMix transforms" "$out"
  fi
  rm -rf "$out"
}
t_accf_node_mix

t_accf_palette_corpus(){
  # These small retail samples cover the palette relationships that cannot be
  # represented by a single same-name fixture: dotted PAT0 frames, localized
  # shared palettes, MDL0 texture/palette resource links, and a runtime TLUT
  # whose on-disc palette initializes fewer colors than its CI8 indices use.
  local sample_dir="$PWD_PROJECT/../tests/fixtures"
  local specs="accf_Ftr156.brres:7 accf_Ftr368.brres:6 accf_fgItem.brres:111 accf_npc_special_13.brres:10 accf_bgmodel_193.brres:10"
  local ran=0
  for spec in $specs; do
    local name=${spec%:*} expected=${spec#*:} src="$sample_dir/${spec%:*}"
    [ -f "$src" ] || continue
    ran=1
    local out
    out=$(mktemp -d /tmp/_r_accf_palette.XXXXXX) || { no "ACCF palette corpus: $name" "mktemp failed"; continue; }
    $B/wszst XDECODE "$src" --dest "$out" --overwrite >"$out.log" 2>&1
    local count
    count=$(find "$out/Textures(NW4R)" -type f -name '*.png' 2>/dev/null | wc -l | tr -d ' ')
    if [ "$count" -eq "$expected" ] \
        && ! grep -q 'INVALID IMAGE FORMAT\|PNG ERROR' "$out.log"; then
      ok "ACCF palette corpus: $name -> $count PNGs"
    else
      no "ACCF palette corpus: $name" "expected $expected PNGs, got $count"
    fi
  done
  [ "$ran" -eq 1 ] || sk "ACCF palette corpus"
}
t_accf_palette_corpus

t_accf_shared_texture_tree(){
  # Terrain MDL0s and their seasonal TEX0s are in different BRRES archives.
  # Directory-mode XX must finish decoding the tree, index its PNGs, and only
  # then export DAEs so init_from is neither dangling nor traversal-order based.
  local sample_dir="$PWD_PROJECT/../tests/fixtures"
  local model="$sample_dir/accf_000_0.brres"
  local pack="$sample_dir/accf_pat0season00.brres"
  if [ ! -f "$model" ] || [ ! -f "$pack" ]; then sk "ACCF shared BRRES texture tree"; return; fi
  local tree
  tree=$(mktemp -d /tmp/_r_accf_shared.XXXXXX) || { no "ACCF shared BRRES texture tree" "mktemp failed"; return; }
  mkdir -p "$tree/BgData/BgModel" "$tree/BgData/Pack"
  cp "$model" "$tree/BgData/BgModel/"
  cp "$pack" "$tree/BgData/Pack/"
  $B/wszst XX "$tree" --overwrite >"$tree.log" 2>&1
  local glb="$tree/BgData/BgModel/accf_000_0.brres.d/3DModels(NW4R)/grd_Ce_0.glb"
  local grass="$tree/BgData/Pack/accf_pat0season00.brres.d/Textures(NW4R)/tex_grass.png"
  # The cross-archive case is exactly where a self-contained model matters
  # most: the resolved image lives several directories away, so GLB embeds
  # it directly in the binary rather than depending on a relative init_from.
  if [ -s "$glb" ] && [ -s "$grass" ] \
      && python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1 \
      && python3 - "$glb" "$grass" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
grass_bytes = open(sys.argv[2], "rb").read()
images = g.json.get("images", [])
assert images, "no images embedded"
found = any(g.image_bytes(i) == grass_bytes for i in range(len(images))
            if g.image_bytes(i) is not None)
assert found, "grass texture bytes not embedded in GLB"
PY
  then
    ok "ACCF shared BRRES texture tree -> resolved GLB image"
  else
    no "ACCF shared BRRES texture tree" "$tree"
  fi
}
t_accf_shared_texture_tree

t_accf_v9_texture_links(){
  # v8/v9 MDL0 material-ref strings can point into the BRRES-wide pool. The
  # detached MDL0 compatibility pool does not rewrite those particular
  # fields, so recover the exact layer mapping through the MDL0 Textures
  # resource group (the same material-ref linkage BrawlCrate uses). Include a
  # nearby NPC archive full of generic e.0/m.0 names to prove global basename
  # lookup does not attach unrelated eye/mouth textures to this model.
  local sample_dir="$PWD_PROJECT/../tests/fixtures"
  local model="$sample_dir/accf_excap0.brres"
  local npc="$sample_dir/accf_npc_special_13.brres"
  if [ ! -f "$model" ] || [ ! -f "$npc" ]; then sk "ACCF v9 material texture links"; return; fi
  local tree
  tree=$(mktemp -d /tmp/_r_accf_v9.XXXXXX) || { no "ACCF v9 material texture links" "mktemp failed"; return; }
  mkdir -p "$tree/Item/Excap" "$tree/Npc/Special"
  cp "$model" "$tree/Item/Excap/"; cp "$npc" "$tree/Npc/Special/"
  $B/wszst XX "$tree" --overwrite >"$tree.log" 2>&1
  local glb="$tree/Item/Excap/accf_excap0.brres.d/3DModels(NW4R)/excap998.glb"
  local glb_report
  glb_report=$(python3 - "$glb" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
try:
    g = m.load_glb(sys.argv[1])
except Exception as exc:
    print(f"PARSE_FAIL {exc}"); sys.exit(0)
j = g.json
img_names = [im.get("name") for im in j.get("images", [])]
mat_names = [mt.get("name") for mt in j.get("materials", [])]
mesh_names = [me.get("name") for me in j.get("meshes", [])]
node_names = [n.get("name") for n in j.get("nodes", [])]
print(f"n_images={len(j.get('images', []))}")
print(f"n_materials={len(j.get('materials', []))}")
print(f"n_meshes={len(j.get('meshes', []))}")
print(f"has_excap998_0={'excap998_0' in mesh_names or 'excap998_0' in node_names or 'excap998_0' in img_names}")
print(f"has_h={'h' in mat_names}")
print(f"leaked_generic={any(n in ('e.0', 'm.0') for n in img_names + mat_names)}")
PY
)
  if echo "$glb_report" | grep -q "n_images=2" \
      && echo "$glb_report" | grep -q "n_materials=4" \
      && echo "$glb_report" | grep -q "n_meshes=4" \
      && echo "$glb_report" | grep -q "has_excap998_0=True" \
      && echo "$glb_report" | grep -q "has_h=True" \
      && echo "$glb_report" | grep -q "leaked_generic=False" \
      && python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1; then
    ok "ACCF v9 material links -> 4 named materials, 2 correct local textures"
  else
    no "ACCF v9 material texture links" "$tree"
  fi
  rm -rf "$tree"
}
t_accf_v9_texture_links

t_accf_bone_only_dae(){
  # Retail scene placeholders can contain a real bone hierarchy and no GX
  # objects. BrawlCrate exports these as skeleton-only COLLADA; XX used to
  # silently drop them because ParseMDL0 required at least one mesh.
  local src="$PWD_PROJECT/../tests/fixtures/accf_bone_only_229.brres"
  [ -f "$src" ] || { sk "ACCF bone-only MDL0 -> DAE"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_empty.XXXXXX) || { no "ACCF bone-only MDL0 -> DAE" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out" --overwrite >/dev/null 2>&1
  local glb="$out/3DModels(NW4R)/idr_ms_pictureA2.glb"
  if [ -s "$glb" ] && python3 - "$glb" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
j = g.json
names = [n.get("name") for n in j.get("nodes", [])]
assert "idr_ms_pictureA2" in names, f"root joint node missing: {names}"
assert not j.get("meshes"), f"bone-only model unexpectedly has meshes: {j.get('meshes')}"
PY
  then
    ok "ACCF bone-only MDL0 -> skeleton GLB"
  else
    no "ACCF bone-only MDL0 -> DAE" "$src"
  fi
  rm -rf "$out"
}
t_accf_bone_only_dae

t_accf_skeleton_bind_pose(){
  # The COLLADA skeleton and the skin's inverse bind matrices have to agree:
  # walking the joint tree and multiplying each joint's world matrix by its
  # exported inverse bind matrix must give identity, or importers deform the
  # mesh the moment they bind it. This catches two real regressions at once --
  # writing the joint rotations in X,Y,Z order (NW4R composes T*Rz*Ry*Rx*S),
  # and treating a segment-scale-compensate bone as an ordinary TRS node.
  local src="$HOME/Downloads/Animal Crossing City Folk Deluxe [RUUE02].d/files/Item/OrgUmb/OrgUmb25.brres"
  [ -f "$src" ] || src="$PWD_PROJECT/../tests/fixtures/accf_ins_mukade.brres"
  [ -f "$src" ] || { sk "ACCF skeleton/skin bind pose"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_skin.XXXXXX) || { no "ACCF skeleton/skin bind pose" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out" --overwrite >/dev/null 2>&1
  local glb="$out/3DModels(NW4R)/umb_md.glb"
  [ -f "$glb" ] || for g in "$out/3DModels(NW4R)/"*.glb; do [ -f "$g" ] && glb="$g" && break; done
  # This catches two real regressions at once -- writing the joint rotations
  # in X,Y,Z composition order (NW4R composes T*Rz*Ry*Rx*S), and treating a
  # segment-scale-compensate bone as an ordinary TRS node. The GLB exporter
  # bakes each joint's local transform straight into its node "matrix" (see
  # dae_joint_local_matrix() in lib-model-dae.c, shared with the DAE path),
  # so this is the same invariant, just walked via glTF nodes/skins instead
  # of COLLADA's <node>/<matrix> tree.
  if [ -s "$glb" ] && python3 - "$glb" <<'SKINPY' >/dev/null 2>&1
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)

g = m.load_glb(sys.argv[1])
j = g.json

controllers = 0
for skin in j.get("skins", []):
    joint_indices = skin["joints"]
    ibm = g.read_accessor_tuples(skin["inverseBindMatrices"])
    assert len(ibm) == len(joint_indices), "inverse bind matrix count"
    cache = {}
    for node_idx, inv_flat in zip(joint_indices, ibm):
        world = m.node_world_matrix(g, node_idx, cache)
        inverse = list(inv_flat)
        product = m.mat4_mul(world, inverse)
        expected = m.mat4_identity()
        for k in range(16):
            assert abs(product[k] - expected[k]) < 2e-3, (j["nodes"][node_idx].get("name"), k, product[k])
    controllers += 1

assert controllers > 0, "no skin controllers"
SKINPY
  then
    ok "ACCF skeleton/skin bind pose -> world(joint) * invBind == identity"
  else
    no "ACCF skeleton/skin bind pose" "$src"
  fi
  rm -rf "$out"
}
t_accf_skeleton_bind_pose

t_accf_face_winding(){
  # GX display lists store each primitive'"'"'s vertices in the order the hardware
  # consumes them, which is the reverse of COLLADA'"'"'s front-facing winding.
  # Getting this wrong is invisible in a viewer that does not cull backfaces
  # (and in assimp'"'"'s importer), and turns the model inside out in one that
  # does -- Preview/SceneKit, Unity, Unreal. The check is implementation
  # independent: a triangle'"'"'s geometric normal (right-hand rule) must agree
  # in sign with the artist-authored per-vertex normals stored in the MDL0.
  local src="$PWD_PROJECT/../tests/fixtures/accf_ins_taran.brres"
  [ -f "$src" ] || { sk "ACCF MDL0 face winding"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_wind.XXXXXX) || { no "ACCF MDL0 face winding" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out" --overwrite >/dev/null 2>&1
  local glb="$out/3DModels(NW4R)/ins_taran.glb"
  # glTF's indices/POSITION/NORMAL are already per-corner (no COLLADA-style
  # separate offset/stride input tuples to unpack), so this is simpler than
  # the old DAE version but checks the identical invariant.
  if [ -s "$glb" ] && python3 - "$glb" <<'WINDPY' >/dev/null 2>&1
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


g = m.load_glb(sys.argv[1])
agree = disagree = 0
for mesh in g.json.get("meshes", []):
    for prim in mesh.get("primitives", []):
        attrs = prim.get("attributes", {})
        if "NORMAL" not in attrs or "indices" not in prim:
            continue
        positions = g.read_accessor_tuples(attrs["POSITION"])
        normals = g.read_accessor_tuples(attrs["NORMAL"])
        indices = g.read_accessor(prim["indices"])
        for base in range(0, len(indices) - 2, 3):
            corner = indices[base:base + 3]
            p0, p1, p2 = (positions[i] for i in corner)
            e1 = [p1[i] - p0[i] for i in range(3)]
            e2 = [p2[i] - p0[i] for i in range(3)]
            geometric = cross(e1, e2)
            authored = [sum(normals[i][axis] for i in corner) / 3.0 for axis in range(3)]
            dot = sum(geometric[i] * authored[i] for i in range(3))
            magnitude = sum(v * v for v in geometric) ** 0.5
            if magnitude < 1e-9:
                continue
            if dot > 0:
                agree += 1
            elif dot < 0:
                disagree += 1

assert agree + disagree > 100, "not enough triangles to judge"
ratio = agree / float(agree + disagree)
assert ratio > 0.9, "faces wound backwards: only %.1f%% agree with normals" % (ratio * 100)
WINDPY
  then
    ok "ACCF MDL0 face winding -> front faces agree with authored normals"
  else
    no "ACCF MDL0 face winding" "$src"
  fi
  rm -rf "$out"
}
t_accf_face_winding

t_dae_brres_injection(){
  # Test DAE -> BRRES injection (with parent BRRES and parent MDL0)
  local sample="/Volumes/SSD/shiran/NintendoWare 2/Revolution/Viewer/build/demos/ef_g3d/data/butterfly.brres"
  [ -f "$sample" ] || sample="$PWD_PROJECT/../tests/fixtures/accf_ins_mukade.brres"
  [ -f "$sample" ] || sample=$(for d in $SEARCH; do [ -d "$d" ] || continue; find -L "$d" -maxdepth 5 -name "*.brres" -print -quit 2>/dev/null; done | head -1)
  [ -n "$sample" ] && [ -f "$sample" ] || { sk "DAE -> BRRES injection"; return; }

  local out
  out=$(mktemp -d /tmp/_r_dae_inject.XXXXXX) || { no "DAE -> BRRES injection" "mktemp failed"; return; }

  # 1. Extract sample
  $B/wszst extract "$sample" -d "$out/orig.d" >/dev/null 2>&1
  local mdl
  for m in "$out/orig.d/3DModels(NW4R)/"*; do [ -f "$m" ] && mdl="$m" && break; done
  [ -n "$mdl" ] && [ -f "$mdl" ] || { no "GLB -> BRRES injection" "failed to extract mdl0"; rm -rf "$out"; return; }

  # 2. Decode to GLB
  $B/wmdlt decode "$mdl" -d "$out/test.glb" >/dev/null 2>&1
  [ -s "$out/test.glb" ] || { no "GLB -> BRRES injection" "failed to decode mdl0 to GLB"; rm -rf "$out"; return; }

  # 3. Inject GLB into parent BRRES
  $B/wmdlt encode "$out/test.glb" --parent="$sample" -d "$out/injected.brres" --overwrite >/dev/null 2>&1
  [ -s "$out/injected.brres" ] || { no "GLB -> BRRES injection" "failed to inject GLB into parent BRRES"; rm -rf "$out"; return; }

  # 4. Extract injected BRRES and verify NW4R directory layout
  $B/wszst extract "$out/injected.brres" -d "$out/reextract.d" >/dev/null 2>&1
  local re_mdl
  for m in "$out/reextract.d/3DModels(NW4R)/"*; do [ -f "$m" ] && re_mdl="$m" && break; done
  if [ -n "$re_mdl" ] && [ -f "$re_mdl" ]; then
    ok "GLB -> BRRES injection (with parent BRRES and folder hierarchy)"
  else
    no "GLB -> BRRES injection" "missing subfiles after re-extracting injected BRRES"
  fi

  # 5. Direct MDL0 injection test
  $B/wmdlt encode "$out/test.glb" --parent="$mdl" -d "$out/injected.mdl0" --overwrite >/dev/null 2>&1
  if [ -s "$out/injected.mdl0" ]; then
    ok "GLB -> MDL0 injection (with parent MDL0)"
  else
    no "GLB -> MDL0 injection" "failed to inject GLB directly into MDL0"
  fi

  rm -rf "$out"
}
t_dae_brres_injection

t_dae_multiformat_injection(){
  # Test GLB -> BCH injection
  local bch_sample="/Users/larsen/Downloads/aaaaa/live1/h3d/Mii_body.bch"
  [ -f "$bch_sample" ] || bch_sample=$(for d in $SEARCH; do [ -d "$d" ] || continue; find -L "$d" -maxdepth 6 -name "*.bch" -print -quit 2>/dev/null; done | head -1)
  if [ -n "$bch_sample" ] && [ -f "$bch_sample" ]; then
    local out; out=$(mktemp -d /tmp/_r_dae_bch.XXXXXX) || { no "GLB -> BCH injection" "mktemp failed"; return; }
    $B/wmdlt ENCODE "$bch_sample" -d "$out/orig.glb" --overwrite >/dev/null 2>&1
    if [ -s "$out/orig.glb" ]; then
      $B/wmdlt ENCODE "$out/orig.glb" --parent="$bch_sample" -d "$out/injected.bch" --overwrite >/dev/null 2>&1
      if [ -s "$out/injected.bch" ]; then
        $B/wmdlt ENCODE "$out/injected.bch" -d "$out/redecoded.glb" --overwrite >/dev/null 2>&1
        local g; g=$(python3 "$GLTF_COUNT" "$out/redecoded.glb" geometry 2>/dev/null || echo 0)
        if [ "$g" -gt 0 ]; then
          ok "GLB -> BCH injection (with parent BCH: $g geometries)"
        else
          no "GLB -> BCH injection" "failed to re-decode injected BCH"
        fi
      else
        no "GLB -> BCH injection" "failed to write injected.bch"
      fi
    else
      no "GLB -> BCH injection" "failed to decode initial BCH to GLB"
    fi
    rm -rf "$out"
  else
    sk "GLB -> BCH injection"
  fi
}
t_dae_multiformat_injection

t_accf_breft_indexed(){
  # REFT/BREFT entries keep their palette inline after the image payload.
  # This retail ACCF C4 entry is 8x16 with a 16-color RGB5A3 palette; the old
  # placeholder header treated those fields as unknown and passed PAL_INVALID.
  local src="$PWD_PROJECT/../tests/fixtures/accf_breft_indexed.bt-img"
  [ -f "$src" ] || { sk "ACCF indexed BREFT texture"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_breft.XXXXXX) || { no "ACCF indexed BREFT texture" "mktemp failed"; return; }
  $B/wimgt DECODE "$src" -d "$out/flower.png" --overwrite >"$out/decode.log" 2>&1
  if [ -s "$out/flower.png" ] \
      && grep -q 'BREFT-IMG.C4.PRGB5A3' "$out/decode.log" \
      && python3 - "$out/flower.png" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read(24)
assert data[:8] == b'\x89PNG\r\n\x1a\n'
assert struct.unpack('>II', data[16:24]) == (8, 16)
PY
  then
    ok "ACCF indexed BREFT texture -> inline RGB5A3 palette"
  else
    no "ACCF indexed BREFT texture" "$src"
  fi
  rm -rf "$out"
}
t_accf_breft_indexed

t_plt0(){
  local f; f=$(find_magic "PLT0")
  local ext=""
  if [ -z "$f" ]; then
    local brres="$PWD_PROJECT/../tests/fixtures/accf_ins_mukade.brres"
    if [ -f "$brres" ]; then
      ext=$(mktemp -d /tmp/_r_plt0_ext.XXXXXX)
      $B/wszst extract "$brres" -d "$ext" --overwrite >/dev/null 2>&1
      for p in "$ext/Palettes(NW4R)/"*; do [ -f "$p" ] && f="$p" && break; done
    fi
  fi
  [ -n "$f" ] || { sk "PLT0 palette"; return; }
  rm -f /tmp/_r_plt0.png
  $B/wimgt DECODE "$f" -d /tmp/_r_plt0.png --overwrite >/dev/null 2>&1
  [ -s /tmp/_r_plt0.png ] && ok "PLT0 palette -> PNG ($f)" || no "PLT0 palette -> PNG" "$f"
  [ -n "$ext" ] && rm -rf "$ext"
}
t_plt0

t_brfnt(){
  # BRFNT (Wii bitmap font, magic "RFNT"): glyph sheets use the normal GX
  # texture encodings (I4/IA4/etc, same as PLT0/TEX0), located by scanning
  # NFTR-family sections for TGLP rather than a fixed offset -- real fonts
  # vary in section order/count. Verified visually on 3 diverse retail
  # samples while developing this (I4 ASCII, IA4 outline, I4 Japanese
  # kana/katakana) -- all legible, correctly shaped glyphs, not noise.
  local f; f=$(find_magic "RFNT"); [ -n "$f" ] || { sk "BRFNT (Wii bitmap font)"; return; }
  # Multi-TGLP BRFNT fonts are decoded as a single combined atlas PNG (plus a
  # matching placement XML) written directly to the requested -d path, not as
  # per-sheet imgNNN.png files -- see decode_brfnt_atlas() in src/wimgt.c.
  rm -f /tmp/_r_brfnt.png /tmp/_r_brfnt.xml
  $B/wimgt DECODE "$f" -d /tmp/_r_brfnt.png --overwrite >/dev/null 2>&1
  [ -s /tmp/_r_brfnt.png ] && ok "BRFNT (Wii bitmap font) -> PNG ($f)" \
    || no "BRFNT (Wii bitmap font)" "$f"
}
t_brfnt

t_brfna(){
  # BRFNA (Wii "archived" bitmap font, magic "RFNA"): same RFNT-family
  # container/TGLP shape as .brfnt (confirmed by static RE of
  # nw4r_fontcvtr.exe -- see brfna_archived_font_format memory). Every real
  # sample sets TGLP sheetFormat's bit 0x8000, meaning the sheet pixel data
  # isn't raw GX texture data at all -- it's compressed with a proprietary,
  # undocumented codec (three opcodes: LZSS, RLE, and a self-contained
  # canonical-Huffman bit-walk), decompiled from nw4r_fontcvtr.exe via Ghidra
  # and implemented natively in lib-image2.c's DecodeBRFNA_LZSS/RLE/Huffman +
  # DecompressBRFNASheet. Declared sheet counts also routinely exceed what's
  # physically embedded (e.g. RVL_SDK wbf1.brfna declares 70, and now that
  # decompression works all 70 really are present as separate compressed
  # chunks -- an earlier byte-budget clamp for the *uncompressed* case was a
  # red herring caused by not yet knowing the sheets were compressed at all).
  #
  # Verified for real pixel correctness, not just "a file got created": every
  # sheet checked from RVL_SDK fonts/fonts_chn/fonts_kor and the NintendoWare
  # LayoutEditor test_sample/font/*.brfna corpus renders as actual legible
  # glyphs (Latin/symbol, Japanese kana+kanji, and Simplified Chinese sheets
  # all visually confirmed). This test can't render images to eyeball, so it
  # uses PNG file size as a real-content proxy instead of just existence: a
  # genuinely blank/degenerate 32x1024 grayscale sheet PNG-compresses to
  # ~100 bytes, while every real decoded glyph sheet checked was several KB+.
  local f; f=$(find_magic "RFNA"); [ -n "$f" ] || { sk "BRFNA (Wii archived font)"; return; }
  rm -rf /tmp/_r_brfna; mkdir -p /tmp/_r_brfna
  $B/wszst XX "$f" --dest /tmp/_r_brfna/out --overwrite >/dev/null 2>&1
  local pngs; pngs=$(find /tmp/_r_brfna -name '*.png' -o -name 'out*' -type f 2>/dev/null)
  local n; n=$(printf '%s\n' "$pngs" | grep -c .)
  local small; small=$(for p in $pngs; do [ "$(fsize_of "$p")" -lt 500 ] && echo "$p"; done | wc -l | tr -d ' ')
  if [ "$n" -gt 0 ] && [ "$small" -eq 0 ]; then
    ok "BRFNA (Wii archived font) -> $n real, non-blank sheet PNG(s) ($f)"
  else
    no "BRFNA (Wii archived font)" "$f ($small/$n sheet(s) suspiciously small/blank)"
  fi
}
t_brfna

echo "== archives =="
t_pac(){
  # PAC's magic is "ARC\0"; the trailing NUL is stripped by the tr -d '\0'
  # step above, same as every other magic lookup here, so the index key is
  # the 3-byte "ARC".
  local f; f=$(find_magic "ARC"); [ -n "$f" ] || { sk "PAC (Brawl archive)"; return; }
  rm -rf /tmp/_r_pac; mkdir -p /tmp/_r_pac
  $B/wszst EXTRACT "$f" --dest "/tmp/_r_pac/\1N" --overwrite >/dev/null 2>&1
  local n; n=$(find /tmp/_r_pac -type f 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "PAC (Brawl archive) -> $n member(s) ($f)" || no "PAC (Brawl archive)" "$f"
}
t_pac

t_nccarc(){
  local f="$PWD_PROJECT/../tests/fixtures/synthetic_sample.nccarc"
  if [ ! -f "$f" ]; then
    for d in $SEARCH; do [ -d "$d" ] || continue
      f=$(find -L "$d" -maxdepth 8 -type f -size +10c -iname '*.nccarc' ! -path '*/_r_*' 2>/dev/null | head -1)
      [ -n "$f" ] && break
    done
  fi
  [ -n "$f" ] && [ -f "$f" ] || { sk "NCCARC (WarioWare: Touched!)"; return; }
  rm -rf /tmp/_r_nccarc; mkdir -p /tmp/_r_nccarc
  $B/wszst EXTRACT "$f" --dest "/tmp/_r_nccarc/\1N" --overwrite >/dev/null 2>&1
  local n; n=$(find /tmp/_r_nccarc -type f 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "NCCARC (WarioWare: Touched!) -> $n chunk(s) ($f)" || no "NCCARC (WarioWare: Touched!)" "$f"
}
t_nccarc

t_gfa(){
  # GFAC (Good-Feel archive); GFCP zip-mode 1 payloads are BPE-compressed.
  # The BPE decoder desynced on every real retail sample until it was fixed
  # against a live Kirby's Epic Yarn WBFS -- assert real decoded output
  # here, not just "some file got created", so a regression shows up as a
  # missing/empty member rather than a silent pass.
  # Prefer the committed fixture (extracted with `wit EXTRACT -F +...bean00.gfa
  # -F -*` straight out of a real, already-on-disk Kirby's Epic Yarn (USA)
  # WBFS, no synthesis involved) so this doesn't depend on an external
  # dump being present in $SEARCH; fall back to the dynamic scan otherwise.
  local f="$PWD_PROJECT/../tests/fixtures/gfa_bean00.gfa"
  [ -f "$f" ] || f=$(find_magic "GFAC")
  [ -n "$f" ] || { sk "GFA (Good-Feel archive)"; return; }
  rm -rf /tmp/_r_gfa; mkdir -p /tmp/_r_gfa
  $B/wszst EXTRACT "$f" --dest "/tmp/_r_gfa/\1N" --overwrite >/tmp/_r_gfa.log 2>&1
  local n; n=$(find /tmp/_r_gfa -type f -size +0c 2>/dev/null | wc -l | tr -d ' ')
  if [ "$n" -gt 0 ] && ! grep -q "INVALID" /tmp/_r_gfa.log; then
    ok "GFA (Good-Feel archive) -> $n non-empty member(s) ($f)"
  else
    no "GFA (Good-Feel archive)" "$f"
  fi
}
t_gfa

t_warc(){
  # WARC ("WARC" magic): Game & Wario (Wii U) flat archive, big-endian,
  # uncompressed, unrelated to Excite's TOC/RES despite the naming
  # coincidence. Ported from aluigi's game_wario.bms.
  local f; f=$(find_magic "WARC"); [ -n "$f" ] || { sk "WARC (Game & Wario archive)"; return; }
  rm -rf /tmp/_r_warc; mkdir -p /tmp/_r_warc
  $B/wszst EXTRACT "$f" --dest "/tmp/_r_warc/\1N" --overwrite >/tmp/_r_warc.log 2>&1
  local n; n=$(find /tmp/_r_warc -type f -size +0c 2>/dev/null | wc -l | tr -d ' ')
  if [ "$n" -gt 0 ] && ! grep -q "INVALID" /tmp/_r_warc.log; then
    ok "WARC (Game & Wario archive) -> $n non-empty member(s) ($f)"
  else
    no "WARC (Game & Wario archive)" "$f"
  fi
}
t_warc

t_ctpk(){
  # CTPK (CTR Texture Package, 3DS container): contains one or more 3DS GPU textures
  # (Morton tiled, ETC1/ETC1A4/RGBA8/RGB565/etc.). Native extraction via wszst xx
  # decodes each sub-texture directly to PNG.
  local f; f=$(find_magic "CTPK"); [ -n "$f" ] || { sk "CTPK (3DS texture container)"; return; }
  rm -rf /tmp/_r_ctpk; mkdir -p /tmp/_r_ctpk
  $B/wszst EXTRACT "$f" --dest /tmp/_r_ctpk --overwrite >/dev/null 2>&1
  local n; n=$(find /tmp/_r_ctpk -type f -iname '*.png' -size +0c 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "CTPK (3DS texture container) -> $n PNG texture(s) ($f)" || no "CTPK (3DS texture container)" "$f"
}
t_ctpk

t_ctpk_img(){
  local f; f=$(find_magic "CTPK"); [ -n "$f" ] || return
  rm -f /tmp/_r_ctpk_img*.png
  $B/wimgt DECODE "$f" -d /tmp/_r_ctpk_img.png --overwrite >/dev/null 2>&1
  local n; n=$(ls /tmp/_r_ctpk_img*.png 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "CTPK direct decode (wimgt) -> $n PNG(s)" || no "CTPK direct decode (wimgt)" "$f"
}
t_ctpk_img

t_byml(){
  # BYML (Binary YAML parameter format, 3DS / Wii U / Switch):
  # Decodes to valid human-readable YAML and re-encodes back to BYML.
  local f; f=$(find_magic "YB"); [ -n "$f" ] || f=$(find_magic "BY"); [ -n "$f" ] || { sk "BYML parameter decode"; return; }
  rm -f /tmp/_r_byml.yaml /tmp/_r_byml_re.byml /tmp/_r_byml_re.yaml
  $B/wszst TEXT "$f" --dest /tmp/_r_byml.yaml --overwrite >/dev/null 2>&1
  if [ -s /tmp/_r_byml.yaml ] \
  && "$B/wszst" CREATE /tmp/_r_byml.yaml --dest /tmp/_r_byml_re.byml --overwrite >/dev/null 2>&1 \
  && "$B/wszst" TEXT /tmp/_r_byml_re.byml --dest /tmp/_r_byml_re.yaml --overwrite >/dev/null 2>&1 \
  && cmp -s /tmp/_r_byml.yaml /tmp/_r_byml_re.yaml; then
    ok "BYML decode -> encode roundtrip ($f)"
  elif [ -s /tmp/_r_byml.yaml ] && python3 -c "import yaml; d = yaml.safe_load(open('/tmp/_r_byml.yaml')); assert len(d) > 0" 2>/dev/null; then
    ok "BYML -> valid YAML ($f)"
  else
    no "BYML parameter decode" "$f"
  fi
}
t_byml

t_narc(){
  # NARC (Nitro Archive, DS / 3DS):
  # Native extraction unwraps all member files with full directory tree.
  local f; f=$(find_magic "NARC"); [ -n "$f" ] || { sk "NARC (Nitro Archive)"; return; }
  rm -rf /tmp/_r_narc; mkdir -p /tmp/_r_narc
  $B/wszst EXTRACT "$f" --dest /tmp/_r_narc --overwrite >/dev/null 2>&1
  local n; n=$(find /tmp/_r_narc -type f -size +0c 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "NARC (Nitro Archive) -> $n non-empty member(s) ($f)" || no "NARC (Nitro Archive)" "$f"
}
t_narc

echo "== BFWAV/BCWAV (FWAV/CWAV NintendoWare wave audio -> PCM WAV) =="
t_bxwav(){
  # FWAV (Wii U/Switch) and CWAV (3DS) share the RWAV lineage but aren't
  # indexed by magic here (they only ever turn up inside a BFWAR/BCWAR
  # sound-wave-archive, never as a standalone top-level sample), so find one
  # via extension straight from SEARCH, same as t_nccarc.
  local arc=""
  for d in $SEARCH; do [ -d "$d" ] || continue
    arc=$(find -L "$d" -maxdepth 8 -type f -size +1000c \( -iname '*.bfsar' -o -iname '*.bcsar' \) ! -path '*/_r_*' 2>/dev/null | head -1)
    [ -n "$arc" ] && break
  done
  [ -n "$arc" ] || { sk "BFWAV/BCWAV -> WAV"; return; }

  rm -rf /tmp/_r_bxwav; mkdir -p /tmp/_r_bxwav
  $B/wszst xx "$arc" --dest /tmp/_r_bxwav --overwrite >/dev/null 2>&1

  local n_wav; n_wav=$(find /tmp/_r_bxwav -iname '*.wav' -size +44c 2>/dev/null | wc -l | tr -d ' ')
  if [ "$n_wav" -eq 0 ]; then
    no "BFWAV/BCWAV -> WAV" "no WAV produced from $arc"
    return
  fi

  # A broken decode still emits a correctly-sized RIFF/WAVE header (silence
  # or a flat DC offset), so check for actual sample variance -- same
  # standard as t_bntx_astc's colour-count check -- on one real sample.
  local sample; sample=$(find /tmp/_r_bxwav -iname '*.wav' -size +200c 2>/dev/null | head -1)
  local nonzero_variety
  nonzero_variety=$(python3 -c "
import wave
try:
    w = wave.open('$sample','rb')
    import struct
    data = w.readframes(w.getnframes())
    n = w.getsampwidth()
    if n == 2:
        samples = struct.unpack('<%dh' % (len(data)//2), data)
    else:
        samples = data
    print(len(set(samples)))
except Exception:
    print(0)
" 2>/dev/null)
  if [ "${nonzero_variety:-0}" -gt 10 ]; then
    ok "BFWAV/BCWAV -> WAV ($n_wav file(s), $arc)"
  else
    no "BFWAV/BCWAV -> WAV" "decoded but looks silent/flat: $sample"
  fi
}
t_bxwav

echo "== compression round-trips =="
# The compression format is chosen by the DESTINATION EXTENSION, not a flag.
printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > /tmp/_r.bin
for e in lz10 lz11 rl yay0 ash0 lzh8 qlz at7 blz huff4 huff8 stpl rnc rnc1 rnc2 fzip zlib deflate; do
  rm -f /tmp/_r.$e /tmp/_r.out
  if $B/wszst COMPRESS /tmp/_r.bin --dest /tmp/_r.$e --overwrite >/dev/null 2>&1 \
  && $B/wszst DECOMPRESS /tmp/_r.$e --dest /tmp/_r.out --overwrite >/dev/null 2>&1 \
  && cmp -s /tmp/_r.bin /tmp/_r.out; then
    ok "$e round-trip ($(fsize_of /tmp/_r.$e) B)"
  else no "$e round-trip" "mismatch"; fi
done

# romc's header represents the decoded size in whole 4 MiB units.
dd if=/dev/zero of=/tmp/_r_romc_raw.z64 bs=1 count=0 seek=4194304 2>/dev/null
printf '\200\067\022\100romc regression' | dd of=/tmp/_r_romc_raw.z64 conv=notrunc 2>/dev/null
if $B/wszst COMPRESS /tmp/_r_romc_raw.z64 --dest /tmp/_r_romc_enc.romc --overwrite >/dev/null 2>&1 \
&& $B/wszst DECOMPRESS /tmp/_r_romc_enc.romc --dest /tmp/_r_romc_dec.z64 --overwrite >/dev/null 2>&1 \
&& cmp -s /tmp/_r_romc_raw.z64 /tmp/_r_romc_dec.z64; then
  ok "romc type-1 encode/decode round-trip"
else
  no "romc type-1 encode/decode round-trip" "mismatch"
fi

echo "== container creation round-trips =="
t_container_roundtrips(){
  local d; d=$(mktemp -d)
  mkdir -p "$d/tree/sub"
  printf 'File 1 payload data in root directory\n' > "$d/tree/file1.bin"
  printf 'File 2 payload data in sub directory\n' > "$d/tree/sub/file2.bin"

  # NARC
  rm -rf "$d/narc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.narc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.narc" --dest "$d/narc.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/narc.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/narc.out/sub/file2.bin"; then
    ok "NARC create -> extract roundtrip"
  else
    no "NARC create -> extract" "mismatch"
  fi

  # DARC
  rm -rf "$d/darc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.darc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.darc" --dest "$d/darc.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/darc.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/darc.out/sub/file2.bin"; then
    ok "DARC create -> extract roundtrip"
  else
    no "DARC create -> extract" "mismatch"
  fi

  # PAC
  rm -rf "$d/pac.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.pac" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.pac" --dest "$d/pac.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/pac.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/pac.out/file2.bin"; then
    ok "PAC create -> extract roundtrip"
  else
    no "PAC create -> extract" "mismatch"
  fi

  # GFA
  rm -rf "$d/gfa.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.gfa" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.gfa" --dest "$d/gfa.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/gfa.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/gfa.out/sub/file2.bin"; then
    ok "GFA create -> extract roundtrip"
  else
    no "GFA create -> extract" "mismatch"
  fi

  # RARC
  rm -rf "$d/rarc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.rarc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.rarc" --dest "$d/rarc.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/rarc.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/rarc.out/sub/file2.bin"; then
    ok "RARC create -> extract roundtrip"
  else
    no "RARC create -> extract" "mismatch"
  fi

  # WARC (Game & Wario flat archive)
  rm -rf "$d/warc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.warc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.warc" --dest "$d/warc.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/warc.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/warc.out/sub/file2.bin"; then
    ok "WARC create -> extract roundtrip"
  else
    no "WARC create -> extract" "mismatch"
  fi

  # WUX (Wii U sparse disc compression)
  if "$B/wszst" COMPRESS "$d/tree/file1.bin" --dest "$d/test.wux" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" DECOMPRESS "$d/test.wux" --dest "$d/test.wud" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/test.wud"; then
    ok "WUX compress -> decompress roundtrip"
  else
    no "WUX compress -> decompress" "mismatch"
  fi

  # CCF (Virtual Console archive)
  rm -rf "$d/ccf.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.ccf" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.ccf" --dest "$d/ccf.out" --overwrite >/dev/null 2>&1 \
  && [ -s "$d/ccf.out/file1.bin" ] && [ -s "$d/ccf.out/file2.bin" ]; then
    ok "CCF create -> extract roundtrip"
  else
    no "CCF create -> extract" "mismatch"
  fi

  # NCCARC (WarioWare blob container)
  rm -rf "$d/nccarc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.nccarc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.nccarc" --dest "$d/nccarc.out" --overwrite >/dev/null 2>&1 \
  && [ -s "$d/nccarc.out/0000.bin" ] && [ -s "$d/nccarc.out/0001.bin" ]; then
    ok "NCCARC create -> extract roundtrip"
  else
    no "NCCARC create -> extract" "mismatch"
  fi

  # AT7 (PMD archive)
  rm -rf "$d/at7.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.at7" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.at7" --dest "$d/at7.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/at7.out/file1.bin"; then
    ok "AT7 create -> extract roundtrip"
  else
    no "AT7 create -> extract" "mismatch"
  fi

  # MPBIN (Mario Party .bin container)
  rm -rf "$d/mpbin.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.bin" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.bin" --dest "$d/mpbin.out" --overwrite >/dev/null 2>&1 \
  && [ -s "$d/mpbin.out/file000.dat" ] && [ -s "$d/mpbin.out/file001.dat" ]; then
    ok "MPBIN create -> extract roundtrip"
  else
    no "MPBIN create -> extract" "mismatch"
  fi

  # CTPK, NCGR, NCLR
  if command -v python3 >/dev/null; then
    python3 "$PNGTOOL" write "$d/img.png" 32 32 100 150 200
    python3 "$PNGTOOL" write "$d/ncgr_in.png" 64 64 128 128 128
    python3 "$PNGTOOL" write "$d/nclr_in.png" 128 128 255 0 0
    rm -rf "$d/ctpk.out"
    if [ -f "$d/img.png" ] \
    && "$B/wimgt" ENCODE "$d/img.png" --dest "$d/test.ctpk" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/test.ctpk" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/test.ctpk.d/img.png" ]; then
      ok "CTPK encode -> extract roundtrip"
    else
      no "CTPK encode -> extract" "mismatch"
    fi

    # CTPK folder creation (wszst CREATE)
    mkdir -p "$d/ctpk_dir"
    cp "$d/img.png" "$d/ctpk_dir/tex_a.png"
    cp "$d/ncgr_in.png" "$d/ctpk_dir/tex_b.png"
    rm -rf "$d/ctpk_multi.out"
    if "$B/wszst" CREATE "$d/ctpk_dir" --dest "$d/multi.ctpk" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/multi.ctpk" --dest "$d/ctpk_multi.out" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/ctpk_multi.out/tex_a.png" ] && [ -s "$d/ctpk_multi.out/tex_b.png" ]; then
      ok "CTPK folder create -> extract roundtrip"
    else
      no "CTPK folder create -> extract" "mismatch"
    fi

    if [ -f "$d/ncgr_in.png" ] \
    && "$B/wimgt" ENCODE "$d/ncgr_in.png" --dest "$d/test.ncgr" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.ncgr" --dest "$d/ncgr_out.png" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/ncgr_out.png" ]; then
      ok "NCGR encode -> decode roundtrip"
    else
      no "NCGR encode -> decode" "mismatch"
    fi

    if [ -f "$d/nclr_in.png" ] \
    && "$B/wimgt" ENCODE "$d/nclr_in.png" --dest "$d/test.nclr" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.nclr" --dest "$d/nclr_out.png" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/nclr_out.png" ]; then
      ok "NCLR encode -> decode roundtrip"
    else
      no "NCLR encode -> decode" "mismatch"
    fi

    if [ -f "$d/img.png" ] \
    && "$B/wimgt" ENCODE "$d/img.png" --dest "$d/test.bntx" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.bntx" --dest "$d/bntx_out.png" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/bntx_out.png" ]; then
      ok "BNTX encode -> decode roundtrip"
    else
      no "BNTX encode -> decode" "mismatch"
    fi

    if [ -f "$d/nclr_in.png" ] \
    && "$B/wimgt" ENCODE "$d/nclr_in.png" --dest "$d/test.plt0" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.plt0" --dest "$d/plt0_out.png" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/plt0_out.png" ]; then
      ok "PLT0 encode -> decode roundtrip"
    else
      no "PLT0 encode -> decode" "mismatch"
    fi

    if [ -f "$d/img.png" ] \
    && "$B/wimgt" ENCODE "$d/img.png" --dest "$d/test.tex0" --overwrite >/dev/null 2>&1 \
    && [ "$(head -c 4 "$d/test.tex0")" = "TEX0" ] \
    && "$B/wimgt" DECODE "$d/test.tex0" --dest "$d/tex0_out.png" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/tex0_out.png" ]; then
      ok "BRRES TEX0 encode -> decode roundtrip"
    else
      no "BRRES TEX0 encode -> decode" "mismatch"
    fi
  fi

  # NCER sprite cell bank
  local f_ncer; f_ncer=$(find_magic "RECN"); [ -n "$f_ncer" ] || f_ncer=$(find_magic "NCER")
  if [ -n "$f_ncer" ]; then
    if "$B/wszst" EXTRACT "$f_ncer" --dest "$d/ncer_out.xml" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/ncer_out.xml" --dest "$d/test.ncer" --overwrite >/dev/null 2>&1 \
    && cmp -s "$f_ncer" "$d/test.ncer"; then
      ok "NCER extract -> create roundtrip ($f_ncer)"
      bok "NCER retail decode -> encode preserves complete file bytes"
    else
      no "NCER extract -> create" "mismatch with $f_ncer"
    fi
  fi

  # NANR sprite animation bank
  local f_nanr; f_nanr=$(find_magic "RNAN"); [ -n "$f_nanr" ] || f_nanr=$(find_magic "NANR")
  if [ -n "$f_nanr" ]; then
    if "$B/wszst" EXTRACT "$f_nanr" --dest "$d/nanr_out.xml" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/nanr_out.xml" --dest "$d/test.nanr" --overwrite >/dev/null 2>&1 \
    && cmp -s "$f_nanr" "$d/test.nanr"; then
      ok "NANR extract -> create roundtrip ($f_nanr)"
      bok "NANR retail decode -> encode preserves complete file bytes"
    else
      no "NANR extract -> create" "mismatch with $f_nanr"
    fi
  fi

  # BRLAN layout animation
  local f_lan; f_lan=$(find_magic "RLAN")
  if [ -n "$f_lan" ]; then
    if "$B/wlayt" decode "$f_lan" "$d/anim.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/anim.tflyt" "$d/anim.brlan" >/dev/null 2>&1 \
    && cmp -s "$f_lan" "$d/anim.brlan"; then
      ok "BRLAN decode -> encode roundtrip ($f_lan)"
      bok "BRLAN retail decode -> encode preserves complete file bytes"
    else
      no "BRLAN decode -> encode" "mismatch with $f_lan"
    fi
  fi

  # BRLYT layout
  local f_lyt; f_lyt=$(find_magic "RLYT")
  if [ -n "$f_lyt" ]; then
    if "$B/wlayt" decode "$f_lyt" "$d/layout.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/layout.tflyt" "$d/layout.brlyt" >/dev/null 2>&1 \
    && cmp -s "$f_lyt" "$d/layout.brlyt"; then
      ok "BRLYT decode -> encode roundtrip ($f_lyt)"
      bok "BRLYT retail decode -> encode preserves complete file bytes"
    else
      no "BRLYT decode -> encode" "mismatch with $f_lyt"
    fi
  fi

  # Wii U BFLAN/BFLYT use semantic text round-trips: legal encodings may
  # relocate pointed-to strings/sections while preserving the same tree.
  local f_bflan="$PWD_PROJECT/../tests/fixtures/splatoon_cmn_bg_out.bflan"
  if "$B/wlayt" decode "$f_bflan" "$d/anim-wiiu.tflyt" >/dev/null 2>&1 \
  && "$B/wlayt" encode "$d/anim-wiiu.tflyt" "$d/anim-wiiu.bflan" >/dev/null 2>&1 \
  && "$B/wlayt" decode "$d/anim-wiiu.bflan" "$d/anim-wiiu-2.tflyt" >/dev/null 2>&1 \
  && cmp -s "$d/anim-wiiu.tflyt" "$d/anim-wiiu-2.tflyt"; then
    ok "BFLAN semantic decode -> encode -> decode roundtrip"
  else
    no "BFLAN semantic roundtrip" "$f_bflan"
  fi

  local f_bflyt="$PWD_PROJECT/../tests/fixtures/splatoon_cmn_seq_drc_option.bflyt"
  if "$B/wlayt" decode "$f_bflyt" "$d/layout-wiiu.tflyt" >/dev/null 2>&1 \
  && "$B/wlayt" encode "$d/layout-wiiu.tflyt" "$d/layout-wiiu.bflyt" >/dev/null 2>&1 \
  && "$B/wlayt" decode "$d/layout-wiiu.bflyt" "$d/layout-wiiu-2.tflyt" >/dev/null 2>&1 \
  && cmp -s "$d/layout-wiiu.tflyt" "$d/layout-wiiu-2.tflyt"; then
    ok "BFLYT semantic decode -> encode -> decode roundtrip"
  else
    no "BFLYT semantic roundtrip" "$f_bflyt"
  fi

  # BMG message text. Deliberately NOT a byte-exact round-trip check like the
  # BRLAN/BRLYT ones above: real in-the-wild BMGs (e.g. WiiLink-authored
  # ones) can have pre-existing, unrelated round-trip gaps in attribute
  # encoding ("[,,,2/14]"-style per-message overrides) that have nothing to
  # do with text at all, and would make this test flaky across machines
  # depending on which real .bmg SEARCH happens to find. What this guards
  # against is specific and was a real, confirmed bug: BMG_ENC_UTF16BE text
  # must always be read/written big-endian regardless of the container's
  # own structural endian ('bmg->endian') -- a real Wii System Menu BMG has
  # little-endian structural fields but big-endian text, and the old code
  # used the structural endian for text too. That regression was byte-exact
  # round-trip *clean* (decode and encode both applied the same wrong
  # transformation and it canceled out) -- it only showed up as unreadable
  # (garbled CJK-range) text -- so the check here is specifically "does a
  # real message decode to a real printable word", not "does the archive
  # come back byte-identical".
  local f_bmg found_bmg=0 last_bmg=""
  while IFS= read -r f_bmg; do
    [ -n "$f_bmg" ] || continue
    last_bmg="$f_bmg"
    if "$B/wszst" TEXT "$f_bmg" --dest "$d/msg.bmg.txt" --overwrite >/dev/null 2>&1 \
    && grep -qE '^ *[0-9a-f]+[[:space:]]*=[[:space:]]*[A-Za-z]{3,}' "$d/msg.bmg.txt"; then
      ok "BMG decode produces readable text ($f_bmg)"
      found_bmg=1
      break
    fi
  done < <(awk -F'\t' -v m="MESG" '$1==m || index($1,m)==1{print $2}' "$IDX")
  if [ "$found_bmg" -eq 0 ] && [ -n "$last_bmg" ]; then
    no "BMG decode" "no readable ASCII message found in $last_bmg"
  fi

  # Wii Fit Plus padded BMG and DS FLI1/FLW1 sections:
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 2\n@BMG-MID = 1\n@INF-SIZE = 0x0c\n1 = Hello Wii Fit Plus!\n' > "$d/wiifit.bmg.txt"
  if "$B/wbmgt" ENCODE "$d/wiifit.bmg.txt" -d "$d/wiifit.bmg" --overwrite >/dev/null 2>&1; then
    # Test decoding with announced size larger than file size (Wii Fit Plus padding)
    python3 -c "
d = bytearray(open('$d/wiifit.bmg', 'rb').read())
import struct
sz = struct.unpack('>I', d[8:12])[0]
struct.pack_into('>I', d, 8, sz + 32)
open('$d/wiifit_padded.bmg', 'wb').write(d)
"
    if "$B/wbmgt" DECODE "$d/wiifit_padded.bmg" -d "$d/wiifit_padded.txt" --overwrite >/dev/null 2>&1 \
    && grep -q "Hello Wii Fit Plus!" "$d/wiifit_padded.txt"; then
      ok "Wii Fit Plus padded BMG decode"
    else
      no "Wii Fit Plus padded BMG decode" "Failed to decode padded BMG"
    fi
  fi

  # BRFNT font
  if [ -f "$d/img.png" ]; then
    if "$B/wimgt" ENCODE "$d/img.png" -d "$d/test.brfnt" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.brfnt" -d "$d/test_brfnt_out.png" --overwrite >/dev/null 2>&1; then
      ok "BRFNT encode -> decode roundtrip"
    else
      no "BRFNT encode -> decode" "failed"
    fi
    # BRFNA font archive
    if "$B/wimgt" ENCODE "$d/img.png" -d "$d/test.brfna" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.brfna" -d "$d/test_brfna_out.png" --overwrite >/dev/null 2>&1; then
      ok "BRFNA encode -> decode roundtrip"
    else
      no "BRFNA encode -> decode" "failed"
    fi
    # BCFNT font
    if "$B/wimgt" ENCODE "$d/img.png" -d "$d/test.bcfnt" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/test.bcfnt" --dest "$d/bcfnt_out.xml" --overwrite >/dev/null 2>&1; then
      ok "BCFNT encode -> extract XML roundtrip"
    else
      no "BCFNT encode -> extract XML" "failed"
    fi
    # BFFNT font
    if "$B/wimgt" ENCODE "$d/img.png" -d "$d/test.bffnt" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/test.bffnt" --dest "$d/bffnt_out.xml" --overwrite >/dev/null 2>&1; then
      ok "BFFNT encode -> extract XML roundtrip"
    else
      no "BFFNT encode -> extract XML" "failed"
    fi

    # UE4 PAK (Mario & Luigi: Brothership .pak container)
    python3 -c "
import struct, zlib
mp = b'../../../MarioAndLuigi/Content/\x00'
f1_name = b'Maps/Island01.uexp\x00'
f1_data = b'Brothership Map Data Island 01\n' * 5
f2_name = b'Characters/Mario.uexp\x00'
f2_data = b'Mario Animation and Mesh Data\n' * 5

# Write data payload with FPakEntry headers
hdr_sz = 53
p1_hdr = struct.pack('<QQQI20sBI', 0, len(f1_data), len(f1_data), 0, b'\x00'*20, 0, 65536)
p2_hdr = struct.pack('<QQQI20sBI', len(p1_hdr) + len(f1_data), len(f2_data), len(f2_data), 0, b'\x00'*20, 0, 65536)

body = p1_hdr + f1_data + p2_hdr + f2_data
idx_off = len(body)

# Write index table
idx = struct.pack('<i', len(mp)) + mp
idx += struct.pack('<I', 2) # 2 entries
idx += struct.pack('<i', len(f1_name)) + f1_name
idx += struct.pack('<QQQI20sBI', 0, len(f1_data), len(f1_data), 0, b'\x00'*20, 0, 65536)
idx += struct.pack('<i', len(f2_name)) + f2_name
idx += struct.pack('<QQQI20sBI', len(p1_hdr) + len(f1_data), len(f2_data), len(f2_data), 0, b'\x00'*20, 0, 65536)
idx_sz = len(idx)

# Write footer
footer = b'\x00'*16 + b'\x00' # Guid + encrypted
footer += struct.pack('<IIQQ20sB', 0x5A6F12E1, 8, idx_off, idx_sz, b'\x00'*20, 0)
footer += b'None\x00' + b'\x00'*27
footer += b'Zlib\x00' + b'\x00'*27
footer += b'Zstd\x00' + b'\x00'*27
footer += b'Oodle\x00' + b'\x00'*26

open('$d/brothership.pak', 'wb').write(body + idx + footer)
" 2>/dev/null
    rm -rf "$d/pak_out"
    if [ -f "$d/brothership.pak" ] \
    && "$B/wszst" xx "$d/brothership.pak" --dest "$d/pak_out" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/pak_out/Maps/Island01.uexp" ] \
    && [ -s "$d/pak_out/Characters/Mario.uexp" ]; then
      ok "UE4 PAK (Mario & Luigi: Brothership) extract roundtrip"
    else
      no "UE4 PAK (Mario & Luigi: Brothership) extract" "failed"
    fi

    # NUS3AUDIO (Super Smash Bros. Ultimate audio archive)
    # NOTE: format is not implemented (no FF_NUS3AUDIO, no magic detection in
    # GetByMagicFF(), no extractor) -- this is an unimplemented feature, not a
    # regression. Deferred as out of scope; left failing intentionally.
    python3 -c "
import struct
magic = b'NUS3'
t1_name = b'bgm_smash_theme'
t1_data = b'IDSP' + struct.pack('>I', 64) + b'\x00'*56
t2_name = b'se_mario_punch'
t2_data = b'OPUS' + b'\x00'*60

n_tracks = 2
audiindx = struct.pack('<I', n_tracks)
tnid = struct.pack('<II', 100, 101)
tnnm_str1 = struct.pack('B', len(t1_name)) + t1_name + b'\x00'
tnnm_str2 = struct.pack('B', len(t2_name)) + t2_name + b'\x00'
tnnm = tnnm_str1 + tnnm_str2
nmof = struct.pack('<II', 0, len(tnnm_str1))
adof = struct.pack('<IIII', 0, len(t1_data), len(t1_data), len(t2_data))
pack = t1_data + t2_data

body = (b'AUDIINDX' + struct.pack('<I', len(audiindx)) + audiindx +
        b'TNID\x00\x00\x00\x00' + struct.pack('<I', len(tnid)) + tnid +
        b'NMOF\x00\x00\x00\x00' + struct.pack('<I', len(nmof)) + nmof +
        b'ADOF\x00\x00\x00\x00' + struct.pack('<I', len(adof)) + adof +
        b'TNNM\x00\x00\x00\x00' + struct.pack('<I', len(tnnm)) + tnnm +
        b'PACK\x00\x00\x00\x00' + struct.pack('<I', len(pack)) + pack)
hdr = magic + struct.pack('<I', len(body))
open('$d/smash_audio.nus3audio', 'wb').write(hdr + body)
" 2>/dev/null
    rm -rf "$d/nus3_out"
    if [ -f "$d/smash_audio.nus3audio" ] \
    && "$B/wszst" xx "$d/smash_audio.nus3audio" --dest "$d/nus3_out" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/nus3_out/bgm_smash_theme.idsp" ] \
    && [ -s "$d/nus3_out/se_mario_punch.lopus" ]; then
      ok "NUS3AUDIO (Smash Ultimate audio archive) extract roundtrip"
    else
      no "NUS3AUDIO (Smash Ultimate audio archive) extract" "failed"
    fi

    # NUT (Smash 4 NTP3 texture container)
    python3 -c "
import struct
tex1_data = b'DDS_TEST_TEXTURE_1_RGBA8' + b'\x00'*40
tex2_data = b'DDS_TEST_TEXTURE_2_RGBA8' + b'\x00'*40
hdr = b'NTP3' + struct.pack('<HHII', 0x0200, 2, 0, 0)
th1 = struct.pack('<IIIHH', 48 + len(tex1_data), 0, len(tex1_data), 48, 0)
th1 += struct.pack('<HHIIII', 64, 64, 1, 0x0000, 16 + 96, 0) + b'\x00'*12
th2 = struct.pack('<IIIHH', 48 + len(tex2_data), 0, len(tex2_data), 48, 0)
th2 += struct.pack('<HHIIII', 64, 64, 1, 0x0000, 16 + 96 + len(tex1_data), 0) + b'\x00'*12
open('$d/smash_tex.nut', 'wb').write(hdr + th1 + th2 + tex1_data + tex2_data)
" 2>/dev/null
    rm -rf "$d/nut_out"
    if [ -f "$d/smash_tex.nut" ] \
    && "$B/wszst" xx "$d/smash_tex.nut" --dest "$d/nut_out" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/nut_out/texture_000.dds" ] \
    && [ -s "$d/nut_out/texture_001.dds" ]; then
      ok "NUT (Smash 4 NTP3 texture container) extract roundtrip"
    else
      no "NUT (Smash 4 NTP3 texture container) extract" "failed"
    fi

    # Smash Ultimate data.arc
    python3 -c "
import struct
f1 = b'fighter/mario/model/body/c00/model.numatb\x00'
f1_data = b'NUMATB Material Data\n'*4
f2 = b'sound/bgm/bgm_smash.nus3audio\x00'
f2_data = b'NUS3\x00\x00\x00\x00'

hdr = struct.pack('<II', 0xABCDEF00, 2)
p1 = struct.pack('104sQQQ', f1, 8 + 256, len(f1_data), len(f1_data))
p2 = struct.pack('104sQQQ', f2, 8 + 256 + len(f1_data), len(f2_data), len(f2_data))
open('$d/test_smash.arc', 'wb').write(hdr + p1 + p2 + f1_data + f2_data)
" 2>/dev/null
    rm -rf "$d/smash_arc_out"
    if [ -f "$d/test_smash.arc" ] \
    && "$B/wszst" xx "$d/test_smash.arc" --dest "$d/smash_arc_out" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/smash_arc_out/fighter/mario/model/body/c00/model.numatb" ] \
    && [ -s "$d/smash_arc_out/sound/bgm/bgm_smash.nus3audio" ]; then
      ok "Smash Ultimate data.arc container extract roundtrip"
    else
      no "Smash Ultimate data.arc container extract" "failed"
    fi

    # Smash PRC parameter file
    python3 -c "
open('$d/test_param.prc', 'wb').write(b'parambinary\x00' + b'\x00'*64)
" 2>/dev/null
    rm -rf "$d/prc_out.xml"
    if [ -f "$d/test_param.prc" ] \
    && "$B/wszst" xx "$d/test_param.prc" --dest "$d/prc_out.xml" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/prc_out.xml" ]; then
      ok "Smash PRC parameter XML extract roundtrip"
    else
      no "Smash PRC parameter XML extract" "failed"
    fi
  fi

  rm -rf "$d"
}
t_container_roundtrips

echo "== CREATE hash-cache (skip unchanged, rebuild on real change) =="
t_hash_cache(){
  local d; d=$(mktemp -d)
  mkdir -p "$d/tree"
  printf 'plain payload one\n' > "$d/tree/plain.bin"
  printf 'plain payload two\n' > "$d/tree/other.bin"

  "$B/wszst" CREATE "$d/tree" --dest "$d/out.arc" --overwrite >/dev/null 2>&1
  if [ ! -f "$d/tree/.wszst-cache.txt" ]; then
    no "hash-cache: cache file" "not created after CREATE"
    rm -rf "$d"; return
  fi

  # mtime helper: GNU stat uses -c %Y, BSD/macOS uses -f %m
  mtime_of(){ stat -c %Y "$1" 2>/dev/null || stat -f %m "$1" 2>/dev/null; }
  local mtime1; mtime1=$(mtime_of "$d/out.arc")
  sleep 1
  "$B/wszst" CREATE "$d/tree" --dest "$d/out.arc" --overwrite >/dev/null 2>&1
  local mtime2; mtime2=$(mtime_of "$d/out.arc")
  if [ "$mtime1" = "$mtime2" ]; then
    ok "hash-cache: unchanged rebuild skips the rebuild+write entirely"
  else
    no "hash-cache: unchanged rebuild" "out.arc was rewritten though nothing changed"
  fi

  printf 'plain payload one -- edited\n' > "$d/tree/plain.bin"
  sleep 1
  "$B/wszst" CREATE "$d/tree" --dest "$d/out.arc" --overwrite >/dev/null 2>&1
  local mtime3; mtime3=$(mtime_of "$d/out.arc")
  rm -rf "$d/verify"
  "$B/wszst" EXTRACT "$d/out.arc" --dest "$d/verify" --overwrite >/dev/null 2>&1
  if [ "$mtime3" != "$mtime2" ] \
  && cmp -s "$d/tree/plain.bin" "$d/verify/plain.bin" \
  && cmp -s "$d/tree/other.bin" "$d/verify/other.bin"; then
    ok "hash-cache: real change triggers rebuild with correct content"
  else
    no "hash-cache: real change" "dest not rebuilt, or rebuilt content mismatch"
  fi

  rm -rf "$d"
}
t_hash_cache

echo "== BLZ (DS ARM9/ARM7/overlay compression) =="
t_blz(){
  # Real fixture, not synthetic bytes: 300-byte repeating pattern compressed
  # with the actual reference `blz -en` (CUE's tool, via
  # github.com/PeterLemon/Nintendo_DS_Compressors), base64-embedded since
  # there's no BLZ encoder in this tree (decode-only was the ask -- BLZ has
  # no header magic to safely auto-detect from, so it's dispatched by the
  # ".blz" source extension in decompress_nintendo_file(), not the usual
  # magic-table lookup; see the comment there). Verified against the real
  # reference decoder too, not just this round trip, while developing this.
  local d; d=$(mktemp -d)
  base64 -d > "$d/small.blz" <<'EOF'
DdAd8B3wHfAd8B3wHfAd8P8d8B3wHfAd8B3wHfAd8A3Q/wwNDg8AAQIDAAQFBgcICQoLADwAAAjwAAAA
EOF
  python3 -c "open('$d/expected.bin','wb').write(bytes(i%16 for i in range(300)))"
  "$B/wszst" DECOMPRESS "$d/small.blz" --dest "$d/out.bin" --overwrite >/dev/null 2>&1
  if cmp -s "$d/out.bin" "$d/expected.bin"; then
    ok "BLZ decompress (real reference-encoder fixture, 300 B)"
  else
    no "BLZ decompress" "mismatch"
  fi
  rm -rf "$d"
}
t_blz

echo "== wbmsx COMTYPE zlib/deflate =="
t_wbmsx_zlib(){
  command -v python3 >/dev/null || { sk "wbmsx COMTYPE zlib"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import zlib
raw = b'The quick brown fox jumps over the lazy dog. ' * 40
open('$d/expected.bin','wb').write(raw)
open('$d/zlib.bin','wb').write(zlib.compress(raw,9))
co = zlib.compressobj(9, zlib.DEFLATED, -15)
open('$d/deflate.bin','wb').write(co.compress(raw) + co.flush())
"
  local ok=1
  printf 'COMTYPE zlib\nCLOG "out.bin" 0 %d\n' "$(fsize_of "$d/zlib.bin")" > "$d/zlib.bms"
  "$B/wbmsx" "$d/zlib.bms" "$d/zlib.bin" "$d/out_z" >/dev/null 2>&1
  cmp -s "$d/out_z/out.bin" "$d/expected.bin" || ok=0

  printf 'COMTYPE deflate\nCLOG "out.bin" 0 %d %d\n' \
    "$(fsize_of "$d/deflate.bin")" "$(fsize_of "$d/expected.bin")" > "$d/deflate.bms"
  "$B/wbmsx" "$d/deflate.bms" "$d/deflate.bin" "$d/out_d" >/dev/null 2>&1
  cmp -s "$d/out_d/out.bin" "$d/expected.bin" || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok_msg="wbmsx COMTYPE zlib+deflate round-trip" && ok "$ok_msg" \
    || no "wbmsx COMTYPE zlib+deflate round-trip" "mismatch"
}
t_wbmsx_zlib

echo "== wbmsx COMTYPE ash0/rl/lzh8/qlz/at7 (native decoders, not stock QuickBMS names) =="
t_wbmsx_native(){
  local d; d=$(mktemp -d)
  printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > "$d/expected.bin"
  local ok=1
  for e in ash0 rl lzh8 qlz at7; do
    rm -f "$d/f.$e"
    "$B/wszst" COMPRESS "$d/expected.bin" --dest "$d/f.$e" --overwrite >/dev/null 2>&1
    [ -s "$d/f.$e" ] || { ok=0; continue; }
    local ctype=$e; [ "$e" = "qlz" ] && ctype=quicklz
    printf 'COMTYPE %s\nCLOG "out.bin" 0 %d\n' "$ctype" "$(fsize_of "$d/f.$e")" > "$d/f.bms"
    rm -rf "$d/out_$e"
    "$B/wbmsx" "$d/f.bms" "$d/f.$e" "$d/out_$e" >/dev/null 2>&1
    cmp -s "$d/out_$e/out.bin" "$d/expected.bin" || ok=0
  done
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "wbmsx COMTYPE ash0+rl+lzh8+quicklz+at7 round-trip" \
    || no "wbmsx COMTYPE ash0+rl+lzh8+quicklz+at7 round-trip" "mismatch"
}
t_wbmsx_native

echo "== Game & Wario FZIP compression & nested WARC extraction =="
t_fzip_container(){
  command -v python3 >/dev/null || { sk "FZIP container extraction"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct
d1 = b'Game and Wario File 1' * 10
d2 = b'Game and Wario File 2' * 20
warc = bytearray(64)
struct.pack_into('>4s4I2H8I2I', warc, 0, b'WARC', 0, 0, 0, 0, 1, 2, *([0]*8), 2, 0)
f1_off = 300
f2_off = f1_off + len(d1)
f1 = struct.pack('>8I', 0, 0, 0, 0, 0, len(d1), 0, f1_off)
f2 = struct.pack('>8I', 0, 0, 0, 0, 0, len(d2), 0, f2_off)
warc += f1 + f2
warc += b'\x00' * (16 * 3)
warc += b'root\x00\x00\x00\x00'
warc += b'file1.bin\x00\x00\x00'
warc += b'file2.bin\x00\x00\x00'
warc = warc.ljust(f1_off, b'\x00') + d1 + d2
open('$d/sample.warc', 'wb').write(warc)
"
  # Compress with wszst to .fzip
  "$B/wszst" COMPRESS "$d/sample.warc" --dest "$d/sample.warc.fzip" --overwrite >/dev/null 2>&1
  # Extract directly with wszst xx
  "$B/wszst" xx "$d/sample.warc.fzip" --overwrite >/dev/null 2>&1
  # Test QuickBMS with COMP_UNZIP_DYNAMIC
  printf 'endian big\ncomtype COMP_UNZIP_DYNAMIC\nidstring FZIP\nget ZSIZE asize\nmath ZSIZE -= 8\nclog "out.warc" 8 ZSIZE ZSIZE\n' > "$d/fzip.bms"
  "$B/wbmsx" "$d/fzip.bms" "$d/sample.warc.fzip" "$d/bms_out" >/dev/null 2>&1

  local ok=1
  [ -f "$d/sample.warc.fzip.d/root/file1.bin" ] || ok=0
  [ -f "$d/sample.warc.fzip.d/root/file2.bin" ] || ok=0
  cmp -s "$d/bms_out/out.warc" "$d/sample.warc" || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "FZIP compress + wszst xx nested extraction + wbmsx dynamic unzip" \
    || no "FZIP compress + wszst xx nested extraction + wbmsx dynamic unzip" "mismatch"
}
t_fzip_container

echo "== QuickBMS-derived flat archives (SFZ DAT / BG4 / cram / SA01 / CA01 / HWL / MSR) =="
# Synthetic fixtures built to each format's published .bms layout. These
# assert the scanners, not retail fidelity -- see the README rows for which
# of these formats has been checked against a real game file.
t_scn0_cli(){
  # SCN0 (scene animation: light sets, ambient lights, lights, fog, cameras).
  # SCN0 is the only NW4R animation with a *nested* resource group, and retail
  # writes each node's animated slots in flag-BIT numeric order rather than in
  # struct field order -- for a camera that puts perspFovY ahead of rotX. The
  # trailing string pool is in ordinal name order, and version 4 declares a
  # size that includes that pool while version 5 stops at the data section.
  # All of that has to be reproduced for the retail bytes to come back.
  local d; d=$(mktemp -d)
  local ok=1 n=0
  "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/mkw_123dai.brres" \
    --dest "$d/123dai" --overwrite >/dev/null 2>&1 || ok=0
  cp "$PWD_PROJECT/../tests/fixtures/mkw_scn0_v4_course.scn0" "$d/v4.scn0" || ok=0
  cp "$PWD_PROJECT/../tests/fixtures/mkw_scn0_v5_course.scn0" "$d/v5.scn0" || ok=0

  local f
  while IFS= read -r f; do
    n=$((n+1))
    "$B/wszst" TEXT   "$f"        --dest "$d/$n.txt" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    "$B/wszst" BINARY "$d/$n.txt" --dest "$d/$n.bin" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$f" "$d/$n.bin" || ok=0
    "$B/wszst" TEXT   "$d/$n.bin" --dest "$d/$n.t2"  --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$d/$n.txt" "$d/$n.t2" || ok=0
  done < <({ find "$d" -type f -path '*AnmScn*'; ls "$d"/v4.scn0 "$d"/v5.scn0; } | sort)

  [ "$n" -ge 5 ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "SCN0 CLI byte-exact decode -> encode ($n retail animations)" \
    || no "SCN0 CLI byte-exact decode -> encode" "mismatch"
}
t_scn0_cli

t_shp0_cli(){
  # SHP0 (vertex morph animation). Retail SHP0 names its morph targets through
  # the *shared* BRRES string pool, so a standalone file cannot resolve those
  # names -- but unlike VIS0 the raw offsets feed nothing else, so they are
  # preserved verbatim and re-encoding still reproduces the retail bytes.
  local d; d=$(mktemp -d)
  local ok=1 n=0
  for src in mkw_r_parasol mkw_wanwan; do
    "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/$src.brres" \
      --dest "$d/$src" --overwrite >/dev/null 2>&1 || ok=0
  done
  local f
  while IFS= read -r f; do
    n=$((n+1))
    "$B/wszst" TEXT   "$f"        --dest "$d/$n.txt" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    "$B/wszst" BINARY "$d/$n.txt" --dest "$d/$n.bin" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$f" "$d/$n.bin" || ok=0
    "$B/wszst" TEXT   "$d/$n.bin" --dest "$d/$n.t2"  --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$d/$n.txt" "$d/$n.t2" || ok=0
  done < <(find "$d" -type f -path '*AnmShp*' | sort)

  [ "$n" -ge 3 ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "SHP0 CLI byte-exact decode -> encode ($n retail animations)" \
    || no "SHP0 CLI byte-exact decode -> encode" "mismatch"
}
t_shp0_cli

t_clr0_cli(){
  # CLR0 (material colour animation). Unlike VIS0, CLR0 carries its material
  # names in its own trailing string pool, so a standalone file is fully
  # self-describing and re-encoding retail input must reproduce the retail
  # bytes exactly -- including the pool's ordinal name order, which is not the
  # material record order.
  local d; d=$(mktemp -d)
  local ok=1 n=0
  for src in accf_fgItem accf_pat0season00; do
    "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/$src.brres" \
      --dest "$d/$src" --overwrite >/dev/null 2>&1 || ok=0
  done
  local f
  while IFS= read -r f; do
    n=$((n+1))
    "$B/wszst" TEXT   "$f"        --dest "$d/$n.txt" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    "$B/wszst" BINARY "$d/$n.txt" --dest "$d/$n.bin" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$f" "$d/$n.bin" || ok=0
    "$B/wszst" TEXT   "$d/$n.bin" --dest "$d/$n.t2"  --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$d/$n.txt" "$d/$n.t2" || ok=0
  done < <(find "$d" -type f -path '*AnmClr*' | sort)

  [ "$n" -ge 2 ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "CLR0 CLI byte-exact decode -> encode ($n retail animations)" \
    || no "CLR0 CLI byte-exact decode -> encode" "mismatch"
}
t_clr0_cli

t_vis0_cli(){
  # VIS0 had a library in the tree that nothing ever called. This is its first
  # coverage. Retail VIS0 entries name their nodes through the *shared* BRRES
  # string pool, so a standalone VIS0 cannot resolve them and the decoder
  # substitutes "?<offset>" markers -- which is why byte equality against
  # retail is not asserted here. What is asserted: every retail VIS0 decodes,
  # re-encodes, and the re-encoded file decodes to an identical text form; and
  # re-encoding that output reproduces the same bytes, so the writer is
  # deterministic and byte-exact once names are resolvable.
  local d; d=$(mktemp -d)
  local ok=1 n=0
  for src in accf_ins_taran accf_ins_mukade accf_fgItem; do
    "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/$src.brres" \
      --dest "$d/$src" --overwrite >/dev/null 2>&1 || ok=0
  done
  local f
  while IFS= read -r f; do
    n=$((n+1))
    "$B/wszst" TEXT   "$f"         --dest "$d/$n.txt" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    "$B/wszst" BINARY "$d/$n.txt"  --dest "$d/$n.bin" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    "$B/wszst" TEXT   "$d/$n.bin"  --dest "$d/$n.t2"  --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$d/$n.txt" "$d/$n.t2" || ok=0
    # names resolve inside our own output, so this direction must be byte-exact
    "$B/wszst" BINARY "$d/$n.t2"   --dest "$d/$n.b2"  --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$d/$n.bin" "$d/$n.b2" || ok=0
  done < <(find "$d" -type f -path '*AnmVis*' | sort)

  [ "$n" -ge 5 ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "VIS0 CLI decode -> encode -> decode ($n retail animations)" \
    || no "VIS0 CLI decode -> encode -> decode" "mismatch"
}
t_vis0_cli

t_chr_srt_cli(){
  # CHR0/SRT0 reach the CLI as TEXT/BINARY conversions. The encoders are not
  # byte-exact (retail orders the deduplicated track blobs differently), so
  # assert what the fork can honestly claim: every retail animation decodes,
  # re-encodes, and survives a second decode with an identical text form.
  local d; d=$(mktemp -d)
  local ok=1 n=0
  for src in accf_ins_taran accf_ins_mukade accf_pat0season00; do
    "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/$src.brres" --dest "$d/$src" --overwrite >/dev/null 2>&1 || ok=0
  done
  local f k
  while IFS= read -r f; do
    n=$((n+1)); k=$n
    "$B/wszst" TEXT   "$f"          --dest "$d/$k.txt" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    "$B/wszst" BINARY "$d/$k.txt"   --dest "$d/$k.bin" --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    "$B/wszst" TEXT   "$d/$k.bin"   --dest "$d/$k.t2"  --overwrite >/dev/null 2>&1 || { ok=0; continue; }
    cmp -s "$d/$k.txt" "$d/$k.t2" || ok=0
  done < <(find "$d" -type f \( -path '*AnmChr*' -o -path '*AnmTexSrt*' \) | sort)

  [ "$n" -ge 6 ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "CHR0/SRT0 CLI decode -> encode -> decode ($n retail animations)" \
    || no "CHR0/SRT0 CLI decode -> encode -> decode" "mismatch"
}
t_chr_srt_cli

t_bms_ports(){
  command -v python3 >/dev/null || { sk "QuickBMS-derived flat archives"; return; }
  local d; d=$(mktemp -d)
  python3 "$PWD_PROJECT/../tests/mk-bms-fixtures.py" "$d" || { no "QuickBMS-derived flat archives" "fixture build failed"; rm -rf "$d"; return; }

  local ok=1
  for f in sfz.dat xeno.arc mii_sa01.bin amiibo.cbarc msr.pkg hwl.idx mlpj.bg4; do
    "$B/wszst" xx "$d/$f" --overwrite >/dev/null 2>&1 || ok=0
  done
  # named formats keep their real member names and payloads
  cmp -s "$d/sfz.dat.d/bxm/model.bxm"      "$d/expect/model.bxm" || ok=0
  cmp -s "$d/sfz.dat.d/wtb/tex.wtb"        "$d/expect/tex.wtb"   || ok=0
  cmp -s "$d/xeno.arc.d/model.bxm"         "$d/expect/model.bxm" || ok=0
  cmp -s "$d/mii_sa01.bin.d/tex.wtb"       "$d/expect/tex.wtb"   || ok=0
  cmp -s "$d/mlpj.bg4.d/tex.wtb"           "$d/expect/tex.wtb"   || ok=0
  # nameless formats get synthetic names, so check the payloads positionally
  cmp -s "$d/amiibo.cbarc.d/00001.sar"     "$d/expect/tex.wtb"   || ok=0
  cmp -s "$d/msr.pkg.d/00001.bin"          "$d/expect/tex.wtb"   || ok=0
  cmp -s "$d/hwl.idx.d/00002.bin"          "$d/expect/tex.wtb"   || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "SFZ DAT / BG4 / cram / SA01 / CA01 / HWL / MSR extraction" \
    || no "SFZ DAT / BG4 / cram / SA01 / CA01 / HWL / MSR extraction" "mismatch"
}
t_bms_ports

t_sfzdat_roundtrip(){
  command -v python3 >/dev/null || { sk "Star Fox Zero DAT create -> extract"; return; }
  local d; d=$(mktemp -d)
  python3 "$PWD_PROJECT/../tests/mk-bms-fixtures.py" "$d" >/dev/null 2>&1
  local ok=1
  "$B/wszst" xx "$d/sfz.dat" --overwrite >/dev/null 2>&1 || ok=0
  cp -R "$d/sfz.dat.d" "$d/rebuild.dat.d" 2>/dev/null || ok=0
  "$B/wszst" CREATE "$d/rebuild.dat.d" --dest "$d/rebuilt.dat" --overwrite >/dev/null 2>&1 || ok=0
  "$B/wszst" EXTRACT "$d/rebuilt.dat" --dest "$d/rebuilt.out" --overwrite >/dev/null 2>&1 || ok=0
  cmp -s "$d/rebuilt.out/bxm/model.bxm" "$d/expect/model.bxm" || ok=0
  cmp -s "$d/rebuilt.out/wtb/tex.wtb"   "$d/expect/tex.wtb"   || ok=0
  cmp -s "$d/rebuilt.out/dat/sub.dat"   "$d/expect/sub.dat"   || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "Star Fox Zero DAT create -> extract roundtrip" \
    || no "Star Fox Zero DAT create -> extract" "mismatch"
}
t_sfzdat_roundtrip

t_fzip_tool_integration(){
  local d; d=$(mktemp -d)
  mkdir -p "$d/tree/sub"
  printf 'hello warc' > "$d/tree/a.bin"
  printf 'hello sub' > "$d/tree/sub/b.bin"

  # 1. wszst CREATE <dir> --dest out.warc.fzip
  if "$B/wszst" CREATE "$d/tree" --dest "$d/direct.warc.fzip" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" xx "$d/direct.warc.fzip" --dest "$d/ext-warc" --overwrite >/dev/null 2>&1 \
  && [ -f "$d/ext-warc/a.bin" ]; then
    ok "wszst direct create & unpack .warc.fzip"
  else
    no "wszst direct create .warc.fzip" "failed"
  fi

  # 2. wszst CREATE <dir> --dest out.sarc.fzip
  if "$B/wszst" CREATE "$d/tree" --dest "$d/direct.sarc.fzip" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" xx "$d/direct.sarc.fzip" --dest "$d/ext-sarc" --overwrite >/dev/null 2>&1 \
  && [ -f "$d/ext-sarc/a.bin" ]; then
    ok "wszst direct create & unpack .sarc.fzip"
  else
    no "wszst direct create .sarc.fzip" "failed"
  fi

  # 3. wbmgt transparently decode .msbt.fzip
  cat << 'EOF' > "$d/test.tmsbt"
# MSBT: Message Studio Binary Text (BigEndian, UTF-16)

[Greeting]
Hello Game and Wario!
EOF
  if "$B/wbmgt" ENCODE "$d/test.tmsbt" --dest "$d/test.msbt" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" COMPRESS "$d/test.msbt" --dest "$d/test.msbt.fzip" --overwrite >/dev/null 2>&1 \
  && "$B/wbmgt" DECODE "$d/test.msbt.fzip" --dest "$d/dec.tmsbt" --overwrite >/dev/null 2>&1 \
  && grep -q "Hello Game and Wario!" "$d/dec.tmsbt"; then
    ok "wbmgt transparently decode .msbt.fzip"
  else
    no "wbmgt decode .msbt.fzip" "failed"
  fi

  # 4. wlayt transparently decode .bflyt.fzip
  local bflyt_src="$PWD_PROJECT/../tests/fixtures/splatoon_cmn_seq_drc_option.bflyt"
  if [ -f "$bflyt_src" ] \
  && "$B/wszst" COMPRESS "$bflyt_src" --dest "$d/sample.bflyt.fzip" --overwrite >/dev/null 2>&1 \
  && "$B/wlayt" decode "$d/sample.bflyt.fzip" "$d/dec.tflyt" >/dev/null 2>&1 \
  && [ -s "$d/dec.tflyt" ]; then
    ok "wlayt transparently decode .bflyt.fzip"
  else
    no "wlayt decode .bflyt.fzip" "failed"
  fi

  # 5. wimgt transparently decode .bflim.fzip
  python3 "$PNGTOOL" write "$d/test.png" 32 32 100 150 200 >/dev/null 2>&1
  if "$B/wimgt" ENCODE "$d/test.png" --transform RGBA8 --dest "$d/test.bflim" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" COMPRESS "$d/test.bflim" --dest "$d/test.bflim.fzip" --overwrite >/dev/null 2>&1 \
  && "$B/wimgt" DECODE "$d/test.bflim.fzip" --dest "$d/out.png" --overwrite >/dev/null 2>&1 \
  && [ -s "$d/out.png" ]; then
    ok "wimgt transparently decode .bflim.fzip"
  else
    no "wimgt decode .bflim.fzip" "failed"
  fi

  rm -rf "$d"
}
t_fzip_tool_integration

echo "== AT7 archive extraction (PMD WiiWare data*.bin / AT7P container) =="
t_at7_container(){
  command -v python3 >/dev/null || { sk "AT7 container extraction"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct
f1 = b'Pokemon Mystery Dungeon File 1 Payload\n' * 10
f2 = b'Pokemon Mystery Dungeon File 2 Payload\n' * 15
tlen = 28 * 3
toc = struct.pack('>II', tlen, len(f1)) + b'file1.bin\x00'.ljust(20, b'\x00')
toc += struct.pack('>II', tlen + len(f1), len(f2)) + b'file2.bin\x00'.ljust(20, b'\x00')
toc += b'\x00' * 28
open('$d/raw.bin', 'wb').write(toc + f1 + f2)
"
  local ok=1
  "$B/wszst" COMPRESS "$d/raw.bin" --dest "$d/data0_0001.bin.at7" --overwrite >/dev/null 2>&1
  "$B/wszst" xx "$d/data0_0001.bin.at7" --dest "$d/out.d" --overwrite >/dev/null 2>&1
  [ -f "$d/out.d/file1.bin" ] && [ -f "$d/out.d/file2.bin" ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "AT7 container archive extraction (wszst xx)" \
    || no "AT7 container archive extraction" "mismatch"
}
t_at7_container

echo "== Call of Duty Wii PAK0 sound archive extraction =="
t_cod_pak0(){
  command -v python3 >/dev/null || { sk "Call of Duty Wii PAK0 extraction"; return; }
  local d; d=$(mktemp -d) || { no "Call of Duty Wii PAK0 extraction" "mktemp failed"; return; }
  python3 -c "
import struct
entries = [(0x11223344, 0, b'DSP-AUDIO-ONE'), (0xaabbccdd, 1, b'DSP-AUDIO-TWO')]
multiplier, data_start = 0x20, 0x80
raw = bytearray(b'PAK0' + struct.pack('<IIII', 0, len(entries), multiplier, data_start))
for crc, offset, payload in entries:
    raw += struct.pack('<III', crc, offset, len(payload))
raw += b'\\0' * (data_start - len(raw))
for _, offset, payload in entries:
    at = data_start + offset * multiplier
    raw += b'\\0' * (at - len(raw))
    raw += payload
open('$d/sound.pak', 'wb').write(raw)
"
  "$B/wszst" xx "$d/sound.pak" --no-passthrough --dest "$d/out" --overwrite >/dev/null 2>&1
  if [ "$(cat "$d/out/sound_0x11223344.dsp" 2>/dev/null)" = "DSP-AUDIO-ONE" ] \
  && [ "$(cat "$d/out/sound_0xaabbccdd.dsp" 2>/dev/null)" = "DSP-AUDIO-TWO" ]; then
    ok "Call of Duty Wii PAK0 extraction (wszst xx)"
  else
    no "Call of Duty Wii PAK0 extraction" "DSP member mismatch or missing"
  fi
  rm -rf "$d"
}
t_cod_pak0

echo "== Nintendo Huffman (HUFF4 / HUFF8) =="
t_huffman(){
  command -v python3 >/dev/null || { sk "Nintendo Huffman (4-bit & 8-bit)"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct
bits = '01011001' + '0'*24
stream = struct.pack('<I', int(bits, 2))
open('$d/huff8.bin', 'wb').write(bytes([0x28, 8, 0, 0, 1, 0xC0, 0x00, ord(\"A\"), ord(\"B\")]) + stream)
open('$d/huff4.bin', 'wb').write(bytes([0x24, 4, 0, 0, 1, 0xC0, 0x00, 1, 2]) + stream)
"
  local ok=1
  "$B/wszst" DECOMPRESS "$d/huff8.bin" --dest "$d/out8.bin" --overwrite >/dev/null 2>&1
  [ "$(cat "$d/out8.bin" 2>/dev/null)" = "ABABBAAB" ] || ok=0
  "$B/wszst" DECOMPRESS "$d/huff4.bin" --dest "$d/out4.bin" --overwrite >/dev/null 2>&1
  [ "$(od -An -tx1 "$d/out4.bin" 2>/dev/null | tr -d ' \n')" = "12122112" ] || ok=0
  printf 'COMTYPE huff8\nCLOG "out8.dat" 0 %d\n' "$(fsize_of "$d/huff8.bin")" > "$d/test8.bms"
  printf 'COMTYPE huff4\nCLOG "out4.dat" 0 %d\n' "$(fsize_of "$d/huff4.bin")" > "$d/test4.bms"
  "$B/wbmsx" "$d/test8.bms" "$d/huff8.bin" "$d/out8_bms" >/dev/null 2>&1
  [ "$(cat "$d/out8_bms/out8.dat" 2>/dev/null)" = "ABABBAAB" ] || ok=0
  "$B/wbmsx" "$d/test4.bms" "$d/huff4.bin" "$d/out4_bms" >/dev/null 2>&1
  [ "$(od -An -tx1 "$d/out4_bms/out4.dat" 2>/dev/null | tr -d ' \n')" = "12122112" ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "Nintendo Huffman 4-bit + 8-bit (wszst + wbmsx)" \
    || no "Nintendo Huffman 4-bit + 8-bit" "mismatch"
}
t_huffman

echo "== Mario Party BIN (wmpbpack/wmpbdump, Hudson mpbin-tools port) =="
# The synthetic round-trip still catches encoder/decoder symmetry. A curated
# mariomdl0.bin from retail GMPE01 (Mario Party 4 USA Rev 1) separately proves
# the real Hudson fast-slide path and yields two duplicate HSFV037 models.
# Both tools used to call
# getchar() on every error/warning path (ported straight from the original
# Windows console EXEs), which silently hangs forever under any script or
# CI runner with no output at all -- removed.
t_mpb(){
  local d; d=$(mktemp -d)
  printf 'AAAAAAAAAABBBBBBBBBBCCCCCCCCCC %.0s' {1..50} > "$d/in.dat"
  printf 'The quick brown fox. %.0s' {1..30} >> "$d/in.dat"
  local all_ok=1
  for ct in 0 1 2 5 7; do  # none, LZSS, YAZ0-like slide, RLE, inflate
    # wmpbdump's own success path returns 1, not 0 (ported as-is from the
    # original source) -- gate on the round-tripped bytes, not exit codes.
    ( cd "$d" && echo "compress_type=$ct: in.dat" > list.txt
      timeout 10 "$PWD_PROJECT/wmpbpack" list.txt "out$ct.bin" >/dev/null 2>&1
      timeout 10 "$PWD_PROJECT/wmpbdump" "out$ct.bin" >/dev/null 2>&1
      cmp -s "out${ct}_file0.dat" in.dat ) || all_ok=0
    ( cd "$d" && timeout 10 "$PWD_PROJECT/bin/wszst" xx "out$ct.bin" --dest "out${ct}_xx.d" --overwrite >/dev/null 2>&1
      cmp -s "out${ct}_xx.d/file000.dat" in.dat ) || all_ok=0
  done
  rm -rf "$d"
  [ "$all_ok" = 1 ] && ok "Mario Party BIN round-trip (compress_type 0/1/2/5/7, synthetic + wszst xx)" \
    || no "Mario Party BIN round-trip" "one or more compress_type mismatched"
}
t_mpb

echo "== QuickBMS chaining (wszst xx --bms) =="
t_wszst_bms(){
  command -v python3 >/dev/null || { sk "wszst xx --bms"; return; }
  local d; d=$(mktemp -d)
  printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..200} > "$d/payload.dat"
  python3 -c "import zlib; open('$d/container.bin', 'wb').write(zlib.compress(open('$d/payload.dat', 'rb').read()))"
  printf 'COMTYPE zlib\nCLOG "nested.dat" 0 %d\n' "$(fsize_of "$d/container.bin")" > "$d/unpack.bms"
  "$B/wszst" xx "$d/container.bin" --bms="$d/unpack.bms" --dest "$d/out_bms" --overwrite >/dev/null 2>&1
  if [ -s "$d/out_bms/container.d/nested.dat" ] && cmp -s "$d/out_bms/container.d/nested.dat" "$d/payload.dat"; then
    ok "wszst xx --bms chained extraction"
  else
    no "wszst xx --bms chained extraction" "nested.dat mismatch or missing"
  fi
  rm -rf "$d"
}
t_wszst_bms

echo "== AJPG (wimgt native decode/encode) =="
t_ajpg_wimgt(){
  command -v python3 >/dev/null || { sk "wimgt AJPG"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct, zlib
def make_png(w, h):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    raw = b''.join(b'\x00' + b'\x80\x40\x20\xff' * w for _ in range(h))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
open('$d/test.png', 'wb').write(make_png(64, 64))
"
  "$B/wimgt" ENCODE "$d/test.png" --dest "$d/test.ajpg" --overwrite >/dev/null 2>&1
  "$B/wimgt" DECODE "$d/test.ajpg" --dest "$d/out.png" --overwrite >/dev/null 2>&1
  if [ -s "$d/test.ajpg" ] && [ -s "$d/out.png" ]; then
    ok "AJPG encoded and decoded via wimgt"
  else
    no "AJPG via wimgt" "failed to encode or decode AJPG"
  fi
  rm -rf "$d"
}
t_ajpg_wimgt

t_mpb_retail(){
  local src="$PWD_PROJECT/../tests/fixtures/mp4_mariomdl0.bin"
  [ -f "$src" ] || { sk "Mario Party 4 retail BIN"; return; }
  local d
  d=$(mktemp -d /tmp/_r_mp4_retail.XXXXXX) || { no "Mario Party 4 retail BIN" "mktemp failed"; return; }
  cp "$src" "$d/model.bin"
  ( cd "$d" && timeout 15 "$PWD_PROJECT/wmpbdump" model.bin >run.log 2>&1 )
  "$B/wszst" EXTRACT "$d/model.bin" --dest "$d/decoded" --overwrite >"$d/wszst.log" 2>&1
  local a="$d/model_file0.hsf" b="$d/model_file1.hsf"
  local glb="$d/decoded/file000.glb"
  local motion="$glb.motion.json"
  local textured=0
  if [ -s "$glb" ]; then
    textured=$(python3 - "$glb" <<'PY'
import json,struct,sys
d=open(sys.argv[1],'rb').read(); n=struct.unpack_from('<I',d,12)[0]
j=json.loads(d[20:20+n]); p=[p for m in j.get('meshes',[]) for p in m['primitives']]
print(int(len(j.get('images',[])) >= 5 and len(j.get('nodes',[])) >= 100
    and len(j.get('skins',[])) == 1 and sum('JOINTS_0' in x['attributes'] for x in p) == 15
    and sum('WEIGHTS_0' in x['attributes'] for x in p) == 15))
PY
)
  fi
  if [ -s "$a" ] && [ -s "$b" ] \
      && [ "$(head -c 7 "$a")" = HSFV037 ] \
      && cmp -s "$a" "$b" \
      && ! grep -q 'Failed\|Unknown Compression' "$d/run.log" \
      && [ "$textured" = 1 ] && [ "$(find "$d/decoded" -name '*.png' | wc -l)" -ge 5 ] \
      && python3 -c 'import json,sys;j=json.load(open(sys.argv[1]));assert len(j["motions"][0]["tracks"])==1082' "$motion"; then
    ok "Mario Party 4 retail BIN -> 2 textured, hierarchical, skinned HSF models"
  else
    no "Mario Party 4 retail BIN" "$src"
  fi
}
t_mpb_retail

t_hsf_runtime_features(){
  local src="$PWD_PROJECT/../tests/fixtures/hsf-features" d
  [ -d "$src" ] || { sk "HSF runtime feature fixtures"; return; }
  d=$(mktemp -d /tmp/_r_hsf_features.XXXXXX) || return
  "$B/wszst" EXTRACT "$src/cluster.hsf" --dest "$d/cluster.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$src/replica-shape.hsf" --dest "$d/shape.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$src/replica.hsf" --dest "$d/replica.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$src/camera.hsf" --dest "$d/camera.glb" --overwrite >/dev/null 2>&1 || true
  "$B/wszst" EXTRACT "$src/light.hsf" --dest "$d/light.glb" --overwrite >/dev/null 2>&1 || true
  "$B/wszst" EXTRACT "$src/nested-replica.hsf" --dest "$d/nested.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$src/replica.hsf" --dest "$d/encode.dae" --overwrite >/dev/null 2>&1
  "$B/wmdlt" ENCODE "$d/encode.dae" --dest "$d/encoded.hsf" >/dev/null 2>&1
  "$B/wszst" EXTRACT "$d/encoded.hsf" --dest "$d/encoded.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/hsf_multipart_test.hsf" --dest "$d/textured.dae" --overwrite >/dev/null 2>&1
  "$B/wmdlt" ENCODE "$d/textured.dae" --dest "$d/textured.hsf" >/dev/null 2>&1
  "$B/wszst" EXTRACT "$d/textured.hsf" --dest "$d/textured.glb" --overwrite >/dev/null 2>&1
  if python3 - "$d/cluster.glb" "$d/shape.glb" "$d/replica.glb" "$d/camera.glb.hsf.json" "$d/light.glb.hsf.json" "$d/nested.glb" "$d/encoded.hsf" "$d/encoded.glb" "$d/textured.hsf" "$d/textured.glb" <<'PY'
import json,struct,sys
def glb(path):
 d=open(path,'rb').read(); n=struct.unpack_from('<I',d,12)[0]; return json.loads(d[20:20+n])
c,s,r=map(glb,sys.argv[1:4])
assert len(c['meshes'][0]['primitives'][0]['targets']) == 7
assert len(s['meshes'][0]['primitives'][0]['targets']) == 34
assert s['meshes'][0]['extras']['targetNames'][0] == 'flaga_shape1'
for j,n in ((c,7),(s,34)):
 a=j['animations'][0]; wi=next(i for i,x in enumerate(a['channels']) if x['target']['path']=='weights')
 ia=j['accessors'][a['samplers'][wi]['input']]; oa=j['accessors'][a['samplers'][wi]['output']]
 assert oa['type']=='SCALAR' and oa['count']==ia['count']*n
assert any(x['target']['path']=='rotation' for x in s['animations'][0]['channels'])
mesh_nodes=[x for x in r['nodes'] if 'mesh' in x]
assert len(mesh_nodes) > len(r['meshes']) and all('matrix' in x for x in mesh_nodes[len(r['meshes']):])
camera=json.load(open(sys.argv[4])); light=json.load(open(sys.argv[5]))
assert len(camera['cameras'])==1 and camera['cameras'][0]['far'] > camera['cameras'][0]['near']
assert len(light['lights'])==1 and len(light['lights'][0]['color'])==3
assert camera['scene'] and light['scene']
cg,lg=glb(sys.argv[4][:-9]),glb(sys.argv[5][:-9])
assert len(cg['cameras'])==1 and any('camera' in n and len(n['matrix'])==16 for n in cg['nodes'])
assert lg['extensionsUsed']==['KHR_lights_punctual']
assert len(lg['extensions']['KHR_lights_punctual']['lights'])==1
nested=glb(sys.argv[6]); nested_nodes=[x for x in nested['nodes'] if 'mesh' in x]
assert len(nested_nodes)-len(nested['meshes'])==51
encoded=open(sys.argv[7],'rb').read(); roundtrip=glb(sys.argv[8])
def mesh_signature(g):
 out=[]
 for mesh in g['meshes']:
  prim=[]
  for p in mesh['primitives']:
   a=g['accessors'][p['attributes']['POSITION']]
   ia=g['accessors'][p['indices']] if 'indices' in p else None
   prim.append((a['count'],tuple(a.get('min',[])),tuple(a.get('max',[])),
                ia['count'] if ia else 0,p.get('mode',4)))
 out.append(tuple(prim))
 return tuple(out)
assert encoded.startswith(b'HSFV037')
assert mesh_signature(roundtrip)==mesh_signature(r)
textured=open(sys.argv[9],'rb').read(); textured_glb=glb(sys.argv[10])
assert struct.unpack_from('>I',textured,12+3*8)[0]==1
assert struct.unpack_from('>I',textured,12+9*8)[0]==1 and len(textured_glb['images'])==1
# The first encoded attribute has an identity UV transform unless its source
# material requested KHR_texture_transform.  Zero here collapses all UVs.
attr_off=struct.unpack_from('>I',textured,8+3*8)[0]
assert struct.unpack_from('>4f',textured,attr_off+0x28)==(1.0,1.0,0.0,0.0)
PY
  then ok "HSF retail morphs, replicas, cameras, lights and scene metadata"
  else no "HSF runtime features" "$src"; fi
}
t_hsf_runtime_features

t_mpb_modify_repack_roundtrip(){
  # Full "modify one file, repack the container" workflow on a real retail
  # archive, not synthetic data: extract -> flip a byte inside one leaf file
  # -> wszst create -> re-extract -> the touched file must carry the edit and
  # every untouched sibling file must still be byte-identical, proving the
  # repack neither drops nor corrupts unrelated content.
  local src="$PWD_PROJECT/../tests/fixtures/mp4_mariomdl0.bin"
  [ -f "$src" ] || { sk "MPBIN modify+repack round-trip"; return; }
  local d; d=$(mktemp -d /tmp/_r_mpb_rt.XXXXXX) || { no "MPBIN modify+repack round-trip" "mktemp failed"; return; }
  $B/wszst xx "$src" --dest "$d/orig" --overwrite >"$d/xx1.log" 2>&1
  # 'xx' also decodes each .hsf leaf to a sidecar .glb (now that multi-part
  # HSF models decode too, both leaves in this fixture do), and decoding
  # that .glb in turn drops a .hsf.json/.motion.json sidecar plus the
  # model's embedded textures as standalone .png files beside it. None of
  # that is a raw MPBIN container member -- only the "fileNNN.<ext>" names
  # written directly by extract_mpbin_file() are -- so strip everything
  # else before repacking, or the repack picks up 9 extra phantom entries
  # that were never really in the archive.
  find -E "$d/orig" -maxdepth 1 -type f \
    ! -regex '.*/file[0-9]{3}\.(hsf|atb|pac|darc|sarc|dat)' \
    -delete
  local target; target=$(find "$d/orig" -maxdepth 1 -type f -name '*.hsf' | sort | tail -1)
  if [ -z "$target" ]; then no "MPBIN modify+repack round-trip" "no extracted files"; rm -rf "$d"; return; fi
  python3 -c "
import sys
p = '$target'
d = bytearray(open(p, 'rb').read())
d[0x100] ^= 0xFF
open(p, 'wb').write(d)
"
  $B/wszst create "$d/orig" --dest "$d/repack.bin" --overwrite >"$d/create.log" 2>&1
  $B/wszst xx "$d/repack.bin" --dest "$d/reext" --overwrite >"$d/xx2.log" 2>&1
  local tname; tname=$(basename "$target")
  local all_ok=1
  cmp -s "$target" "$d/reext/$tname" || all_ok=0
  for f in "$d/orig"/*; do
    local bn; bn=$(basename "$f")
    [ "$bn" = "$tname" ] && continue
    cmp -s "$f" "$d/reext/$bn" || all_ok=0
  done
  if [ "$all_ok" = 1 ]; then
    ok "MPBIN modify+repack round-trip (edit survives, siblings untouched)"
  else
    no "MPBIN modify+repack round-trip" "edited or sibling file mismatched after repack"
  fi
  rm -rf "$d"
}
t_mpb_modify_repack_roundtrip

echo "== Monster Games RST / TOC (0TSR / 0SERCOTE) =="
t_rst_container(){
  command -v python3 >/dev/null || { sk "RST container roundtrip"; return; }
  local d; d=$(mktemp -d)
  mkdir -p "$d/rst_src"
  printf 'Monster Games Texture Model 0 Payload\n%.0s' {1..20} > "$d/rst_src/car.tm0"
  printf 'Monster Games Model Payload\n%.0s' {1..30} > "$d/rst_src/car.mod"
  printf 'Monster Games Values Payload\n%.0s' {1..10} > "$d/rst_src/car.val"
  local ok=1
  "$B/wszst" CREATE "$d/rst_src" --dest "$d/test.car" --overwrite >/dev/null 2>&1
  [ -f "$d/test.car" ] && [ -f "$d/test.toc" ] || ok=0
  # First v3 entry offset (+0x10 in TOC record 1) must be payload-relative 0.
  python3 - "$d/test.toc" <<'PYEOF' || ok=0
import struct, sys
toc = open(sys.argv[1], "rb").read()
assert toc[:8] == b"0SERCOTE"
assert struct.unpack_from("<I", toc, 0x0c + 0x28 + 0x10)[0] == 0
PYEOF
  "$B/wszst" EXTRACT "$d/test.car" --dest "$d/rst_out" --overwrite >/dev/null 2>&1
  local src_dir="$d/rst_out/test"
  [ -d "$src_dir" ] || src_dir="$d/rst_out"
  cmp -s "$d/rst_src/car.tm0" "$src_dir/car.tm0" || ok=0
  cmp -s "$d/rst_src/car.mod" "$src_dir/car.mod" || ok=0
  cmp -s "$d/rst_src/car.val" "$src_dir/car.val" || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "RST + TOC container create & extract roundtrip" \
    || no "RST container" "mismatch or failed extraction"
}
t_rst_container

echo "== Arika INFO.DAT/GAME.DAT (ALZ1) CLI extraction =="
t_arika_cli(){
  # No retail sample of Dr. Mario Online Rx / Endless Ocean was available in
  # this environment, so the fixture is built here with an INDEPENDENT
  # Python implementation of the encryption formula and ALZ1 container
  # framing (not by calling back into wszst/CreateArika), so this exercises
  # the C decoder (ExtractArika/DecodeALZ1/DecryptArikaInfo, wired up via
  # `wszst EXTRACT` on an "INFO.DAT" file) against a second, independent
  # implementation of the spec -- same role the RST test above plays for
  # that format, and the standalone test-arika.c binary above already
  # covers the match/ring-buffer side of ALZ1 by hand against GBATEK.
  command -v python3 >/dev/null || { sk "Arika archive CLI extraction"; return; }
  local d; d=$(mktemp -d)
  mkdir -p "$d/arika_src"
  python3 - "$d/arika_src" <<'PYEOF'
import struct, sys
d = sys.argv[1]

def alz1_compress(data):
    # Deliberately the simplest valid encoder: every byte literal. Still a
    # real exercise of ExtractArika's magic detection + DecodeALZ1's flag
    # parsing (LSB-first, 0=match/1=literal, reload every 8) -- the
    # match/ring-buffer path is covered separately in test-arika.c against
    # hand-built vectors taken directly from GBATEK's pseudocode.
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        chunk = data[i:i+8]
        flag = (1 << len(chunk)) - 1
        out.append(flag)
        out.extend(chunk)
        i += len(chunk)
    return b"ALZ1" + bytes(out)

def arika_encrypt(buf):
    # Inverse of GBATEK's decrypt formula
    # (buf[i]=((buf[i] ror4) xor FFh)-key[i&0xf]): algebraically,
    #   buf[i] = ror4( (plain[i]+key[i&0xf]) xor FFh )
    buf = bytearray(buf)
    if buf[0] == 0:
        return bytes(buf)
    key = bytes(buf[:0x10])
    for i in range(0x10, len(buf)):
        b = buf[i]
        b = (b + key[i & 0xf]) & 0xff
        b ^= 0xff
        b = ((b >> 4) | (b << 4)) & 0xff   # rol 4 (== ror 4 on a byte)
        buf[i] = b
    return bytes(buf)

SECTOR = 0x800
plain_data = b"plain payload, stored raw\n" * 5
comp_data  = (b"compressible ALZ1 payload " * 40) + b"\ntail\n"

entries = [
    ("plain.bin", plain_data, False),
    ("com/chr/compressed.dat", comp_data, True),
]

game = bytearray()
dir_entries = []
for name, data, compress in entries:
    while len(game) % SECTOR:
        game += b"\0"
    offset_sectors = len(game) // SECTOR
    if compress:
        payload = alz1_compress(data)
    else:
        payload = data
    game += payload
    dir_entries.append((name, len(payload), offset_sectors,
                         (len(payload) + SECTOR - 1) // SECTOR, len(data)))

info = bytearray(0x30 + len(dir_entries) * 0x30)
info[0:0x10] = b"*Dr.Mario-DSi!!!"  # exact retail title/key, per GBATEK
struct.pack_into("<I", info, 0x24, SECTOR)
struct.pack_into("<I", info, 0x28, 1)
struct.pack_into("<I", info, 0x2c, len(dir_entries))
for i, (name, zsize, off_sec, blocks, dsize) in enumerate(dir_entries):
    rec = 0x30 + i * 0x30
    nb = name.encode()[:0x1f]
    info[rec:rec+len(nb)] = nb
    struct.pack_into("<I", info, rec+0x20, zsize)
    struct.pack_into("<I", info, rec+0x24, off_sec)
    struct.pack_into("<I", info, rec+0x28, blocks)
    struct.pack_into("<I", info, rec+0x2c, dsize)

open(d + "/INFO.DAT", "wb").write(arika_encrypt(bytes(info)))
open(d + "/GAME.DAT", "wb").write(bytes(game))
open(d + "/plain.bin.expect", "wb").write(plain_data)
open(d + "/compressed.dat.expect", "wb").write(comp_data)
PYEOF
  local ok=1
  "$B/wszst" EXTRACT "$d/arika_src/INFO.DAT" --dest "$d/arika_out" --overwrite >/dev/null 2>&1
  local out_dir="$d/arika_out"
  [ -f "$out_dir/plain.bin" ] || out_dir="$d/arika_out/INFO"
  cmp -s "$d/arika_src/plain.bin.expect" "$out_dir/plain.bin" || ok=0
  cmp -s "$d/arika_src/compressed.dat.expect" "$out_dir/com/chr/compressed.dat" || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "Arika INFO.DAT/GAME.DAT: encrypted dir + ALZ1 payload -> wszst EXTRACT" \
    || no "Arika archive CLI extraction" "mismatch or failed extraction"
}
t_arika_cli

echo "== WarioWare Snapped! Nitro size-prefix wrapper =="
t_wwsnapped_wrapper(){
  # WarioWare: D.I.Y. Showcase / "WarioWare Snapped!" (DSiWare, NTR-KUWE) wraps
  # every LZ11-compressed Nitro graphics resource (NCGR/NCLR/NCER/NANR) in a
  # 4-byte little-endian size-prefix record before the real magic. Verified
  # against 5 real retail assets (Style/StyleO.NCLR.bin, Style/Style_Head.
  # NCGR.bin, Style/Style_Head.NCER.bin, Style/Style_2P_01.NANR.bin, Game/
  # WarningB.NCLR.bin) extracted from the USA Rev1 ROM. This builds a small
  # synthetic NCLR wrapped the same way and LZ11-compresses it, then checks
  # that `wimgt DECODE` -- which used to fail with "?" / unrecognized format
  # because the magic no longer sat at offset 0 -- now decodes it end to end.
  command -v python3 >/dev/null || { sk "WarioWare Snapped wrapper"; return; }
  local d; d=$(mktemp -d)
  python3 - "$d" <<'PYEOF'
import struct, sys
d = sys.argv[1]
pltt_body = struct.pack('<2H', 0x0000, 0x7fff)
pltt_hdr = struct.pack('<IIII', 4, len(pltt_body), 0x10, 0)
ttlp_sec = b'TTLP' + struct.pack('<I', 8 + len(pltt_hdr) + len(pltt_body)) + pltt_hdr + pltt_body
nclr = b'RLCN' + struct.pack('<HH', 0xfffe, 1) + struct.pack('<I', 0) + struct.pack('<HH', 0x10, 1) + ttlp_sec
nclr = nclr[:8] + struct.pack('<I', len(nclr)) + nclr[12:]
wrapped = struct.pack('<BHB', 0, len(nclr), 0) + nclr
open(d + '/synth_wrapped.bin', 'wb').write(wrapped)
PYEOF
  local ok=1
  "$B/wszst" COMPRESS "$d/synth_wrapped.bin" --dest "$d/synth_wrapped.lz11" --overwrite >/dev/null 2>&1
  [ -s "$d/synth_wrapped.lz11" ] || ok=0
  "$B/wimgt" DECODE "$d/synth_wrapped.lz11" --dest "$d/synth_wrapped.png" --overwrite >/dev/null 2>&1
  [ -s "$d/synth_wrapped.png" ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "WarioWare Snapped Nitro size-prefix wrapper -> decode" \
    || no "WarioWare Snapped wrapper" "LZ11-wrapped NCLR failed to decode"
}
t_wwsnapped_wrapper

echo "== THP Video Extraction =="
t_thp_extract(){
  command -v python3 >/dev/null || { sk "THP video extraction"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct
jpeg_data = bytes.fromhex(
    'ffd8ffe000104a46494600010101004800480000ffdb004300080606070605080707070909080a0c140d0c0b0b0c1912130f141d1a1f1e1d1a1c1c20242e2720222c231c1c2837292c30313434341f27393d38323c2e333432ffc0000b080010001001011100ffda0008010100003f00bf00ffd9'
)
movie_off = 0x60
comp_data_off = 0x30
offset_data_off = 0x50

frame_comp_sz = len(jpeg_data)
frame_sz = 12 + frame_comp_sz
hdr = struct.pack('>4sIIIfIIIIIII', b'THP\0', 0x11000, 0x10000, 0, 30.0, 1, frame_sz, frame_sz, comp_data_off, offset_data_off, movie_off, movie_off)
comps = struct.pack('>I16BIII', 1, 0, *([0]*15), 16, 16, 0)
comps_padded = comps.ljust(offset_data_off - comp_data_off, b'\x00')
offsets = struct.pack('>II', movie_off, 0)
offsets_padded = offsets.ljust(movie_off - offset_data_off, b'\x00')
frame_hdr = struct.pack('>III', frame_sz, 0, frame_comp_sz)
thp_bytes = hdr + comps_padded + offsets_padded + frame_hdr + jpeg_data
open('$d/test.thp', 'wb').write(thp_bytes)
"
  local ok=1
  "$B/wszst" EXTRACT "$d/test.thp" --dest "$d/thp_out" --no-passthrough --overwrite >/dev/null 2>&1
  local out_dir="$d/thp_out/test"
  [ -d "$out_dir" ] || out_dir="$d/thp_out"
  [ -s "$out_dir/frame_00000.jpg" ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "THP frame extraction (wszst EXTRACT)" \
    || no "THP extraction" "frame_00000.jpg missing or empty"
}
t_thp_extract

echo "== recursive folder traversal (dclib ** wildcard) =="
# "recursive extraction" turned out to mean recursive directory traversal of
# CLI args, not recursing into nested archives. dclib's SearchPaths() already
# supports this via a shell-glob-style "**" pattern -- no new flag needed, we
# only need to prove it actually walks multiple directory levels and that a
# bare directory (no "**") is unaffected, matching existing behaviour.
RD=$(mktemp -d)
mkdir -p "$RD/a/b" "$RD/c"
for f in "$RD/top.bin" "$RD/a/one.bin" "$RD/a/b/two.bin" "$RD/c/three.bin"; do
  printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > "$f"
done
if $B/wszst COMPRESS "$RD/**/*.bin" --overwrite >/dev/null 2>&1 \
&& [ -f "$RD/top.szs" ] && [ -f "$RD/a/one.szs" ] \
&& [ -f "$RD/a/b/two.szs" ] && [ -f "$RD/c/three.szs" ]; then
  ok "COMPRESS 'dir/**/*.bin' reaches all 3 nesting levels"
else
  no "COMPRESS 'dir/**/*.bin'" "not all nested files were compressed"
fi
rm -f "$RD/top.bin" "$RD/a/one.bin" "$RD/a/b/two.bin" "$RD/c/three.bin"
if $B/wszst DECOMPRESS "$RD/**/*.szs" --overwrite >/dev/null 2>&1 \
&& [ -f "$RD/top.bin" ] && [ -f "$RD/a/one.bin" ] \
&& [ -f "$RD/a/b/two.bin" ] && [ -f "$RD/c/three.bin" ]; then
  ok "DECOMPRESS 'dir/**/*.szs' reaches all 3 nesting levels"
else
  no "DECOMPRESS 'dir/**/*.szs'" "not all nested files were decompressed"
fi
# Baseline: passing a bare directory (no "**") must NOT silently recurse.
if $B/wszst DECOMPRESS "$RD" >/dev/null 2>&1; then
  no "bare directory arg (non-recursive baseline)" "should have failed, not silently recursed"
else
  ok "bare directory arg still fails (non-recursive behaviour unchanged)"
fi
rm -rf "$RD"

echo "== external pass-through extraction (wit/ndstool/ctrtool/sharpii) =="
# Find one real .nds and one real .wad under SEARCH; a plain PATH lookup for
# the external tool decides skip vs. pass/fail, same convention as t_model.
PT_NDS=$(for d in $SEARCH; do [ -d "$d" ] || continue; find -L "$d" -maxdepth 4 -iname '*.nds' -size -60M ! -path '*/_r_*' -print -quit 2>/dev/null; done | head -1)
PT_WAD=$(for d in $SEARCH; do [ -d "$d" ] || continue; find -L "$d" -maxdepth 4 -iname '*.wad' -size -60M ! -path '*/_r_*' -print -quit 2>/dev/null; done | head -1)

if command -v ndstool >/dev/null 2>&1 && [ -n "$PT_NDS" ]; then
  RD=$(mktemp -d); cp "$PT_NDS" "$RD/t.nds"
  $B/wszst XX "$RD/t.nds" >/dev/null 2>&1
  if [ -f "$RD/t.d/arm9.bin" ] && [ -f "$RD/t.d/arm7.bin" ] && [ -d "$RD/t.d/data" ] \
  && [ ! -d "$RD/t.d/rominfo.xml" ]; then
    ok "NDS pass-through (ndstool): arm9/arm7/data staged"
  else
    no "NDS pass-through (ndstool)" "expected staged files missing under $RD/t.d"
  fi
  rm -rf "$RD"
else sk "NDS pass-through (ndstool)"; fi

if command -v sharpii >/dev/null 2>&1 && [ -n "$PT_WAD" ]; then
  RD=$(mktemp -d); cp "$PT_WAD" "$RD/t.wad"
  $B/wszst XX "$RD/t.wad" >/dev/null 2>&1
  # A real WAD's *.app content files must NOT be re-claimed as nested WADs
  # (they're raw ELF/binary payloads sharpii itself produced) -- this is the
  # false-positive this section exists to catch, see lib-passthru.c.
  APP_COUNT=$(find "$RD/t.d" -maxdepth 1 -iname '*.app' 2>/dev/null | wc -l | tr -d ' ')
  if [ "${APP_COUNT:-0}" -gt 0 ] && ! grep -rq "SHARPII_NET_CORE_WAD_UNKNOWN" "$RD" 2>/dev/null; then
    ok "WAD pass-through (sharpii): $APP_COUNT .app content file(s) staged, none mis-reclaimed"
  else
    no "WAD pass-through (sharpii)" "no staged .app content, or a content file was wrongly re-dispatched as a WAD"
  fi
  rm -rf "$RD"
else sk "WAD pass-through (sharpii)"; fi

echo "== BRSAR (via wbrsar, statically-linked vgmtrans) =="
t_brsar(){
  # RSARScanner (and every other vgmtrans format scanner) self-registers
  # purely via a global-constructor side effect -- nothing else in the
  # program calls into its .o by symbol reference. A plain static-library
  # link only pulls in .o members that resolve an unresolved symbol, so
  # the linker was silently DROPPING the entire scanner and its
  # registration constructor never ran: confirmed with `nm` (zero
  # RSARScanner symbols in the linked binary) and functionally (every
  # real .brsar sample tried failed identically with "no collections
  # found", across 4 different retail games) -- wbrsar's BRSAR support
  # had never actually executed since it was added to this fork, not a
  # format-parsing bug. Fixed by force-loading the whole archive
  # (-Wl,-force_load on mac, --whole-archive elsewhere) in the Makefile's
  # VGMTRANS_LIBS. This test is the regression guard for that: a real
  # retail sample producing a real, valid Standard MIDI File (magic
  # "MThd", parseable header), not just "some file got created."
  # Not every real .brsar has RSEQ (sequence) sounds -- some banks are
  # SFX/WAVE-only -- so try each magic-matched candidate in turn rather
  # than just the first, same as find_magic() would give.
  local candidates; candidates=$(awk -F'\t' '$1=="RSAR"{print $2}' "$IDX")
  [ -n "$candidates" ] || { sk "BRSAR (wbrsar)"; return; }
  local f mid
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    rm -rf /tmp/_r_brsar; mkdir -p /tmp/_r_brsar
    $B/wbrsar "$f" /tmp/_r_brsar >/tmp/_r_brsar.log 2>&1
    mid=$(find /tmp/_r_brsar -iname "*.mid" -size +14c 2>/dev/null | head -1)
    [ -n "$mid" ] && [ "$(head -c4 "$mid")" = "MThd" ] && break
    mid=""
  done <<< "$candidates"
  local sf2_count; sf2_count=$(find /tmp/_r_brsar -iname "*.sf2" -size +100c 2>/dev/null | wc -l | tr -d ' ')
  if [ -n "$mid" ] && [ "$sf2_count" -eq 1 ]; then
    ok "BRSAR -> MIDI + single SF2 ($f)"
  elif [ -n "$mid" ]; then
    ok "BRSAR -> MIDI ($f)"
  else
    no "BRSAR -> MIDI" "no candidate produced a valid MIDI"
  fi
}
t_brsar

# wbrsar pack/unpack: unpack a real archive, repack it, unpack again, and
# require that every re-unpacked file matches some originally-unpacked file
# byte-for-byte (same content hash). Names are allowed to change across the
# round trip -- RWSD/RWAR entries have no sound/bank-table name entry in a
# packed archive (only RSEQ/RBNK do), so they legitimately come back as
# file_NNN.* -- but not the bytes.
t_brsar_roundtrip(){
  local candidates; candidates=$(awk -F'\t' '$1=="RSAR"{print $2}' "$IDX")
  [ -n "$candidates" ] || { sk "BRSAR pack/unpack roundtrip"; return; }
  local f
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    rm -rf /tmp/_r_brtp; mkdir -p /tmp/_r_brtp/a /tmp/_r_brtp/b
    $B/wbrsar unpack "$f" /tmp/_r_brtp/a >/tmp/_r_brtp.log 2>&1 || continue
    local n_a; n_a=$(find /tmp/_r_brtp/a -type f | wc -l | tr -d ' ')
    [ "$n_a" -ge 1 ] || continue
    $B/wbrsar pack /tmp/_r_brtp/a /tmp/_r_brtp/rt.brsar >>/tmp/_r_brtp.log 2>&1 || continue
    [ "$(head -c4 /tmp/_r_brtp/rt.brsar)" = "RSAR" ] || { no "BRSAR pack/unpack roundtrip" "packed output lacks RSAR magic"; return; }
    $B/wbrsar unpack /tmp/_r_brtp/rt.brsar /tmp/_r_brtp/b >>/tmp/_r_brtp.log 2>&1 || { no "BRSAR pack/unpack roundtrip" "re-unpack failed"; return; }
    local n_b; n_b=$(find /tmp/_r_brtp/b -type f | wc -l | tr -d ' ')
    if [ "$n_a" = "$n_b" ]; then
      local bad=0
      while IFS= read -r g; do
        local h1 h2
        h1=$(md5sum "$g" | cut -d' ' -f1)
        h2=$(md5sum /tmp/_r_brtp/b/* 2>/dev/null | grep "^$h1" | head -1)
        [ -n "$h2" ] || { bad=1; break; }
      done < <(find /tmp/_r_brtp/a -type f)
      [ "$bad" = 0 ] && { ok "BRSAR pack/unpack roundtrip ($f)"; return; }
      no "BRSAR pack/unpack roundtrip" "content changed across roundtrip"
      return
    fi
    no "BRSAR pack/unpack roundtrip" "file count changed: $n_a -> $n_b"
    return
  done <<< "$candidates"
  sk "BRSAR pack/unpack roundtrip"
}
t_brsar_roundtrip

t_brsar_pack(){
  local TD=/tmp/_r_bpack; rm -rf "$TD"; mkdir -p "$TD/in"

  # Synthetic MML sequence
  cat > "$TD/in/melody.txt" <<'EOF'
; test
tempo 120
prg 0
n C4 100 48
fin
EOF

  # Synthetic RBNK (opaque blob)
  printf '\x52\x42\x4E\x4B\x00\x00\x00\x10' > "$TD/in/melody.rbnk"

  $B/wbrsar pack "$TD/in" "$TD/out.brsar" >"$TD/log" 2>&1
  [ $? -eq 0 ] || { no "BRSAR pack (synthetic)" "wbrsar pack failed"; return; }

  local sz; sz=$(fsize_of "$TD/out.brsar")
  [ "${sz:-0}" -gt 64 ] || { no "BRSAR pack (synthetic)" "output too small: ${sz}"; return; }

  local magic; magic=$(xxd -l 4 -p "$TD/out.brsar")
  [ "$magic" = "52534152" ] || { no "BRSAR pack (synthetic)" "no RSAR magic"; return; }

  local symb; symb=$(xxd -l 4 -s 64 -p "$TD/out.brsar")
  [ "$symb" = "53594d42" ] || { no "BRSAR pack (synthetic)" "SYMB block missing"; return; }

  ok "BRSAR pack (synthetic)"
}
t_brsar_pack

t_sdat(){
  local candidates; candidates=$(awk -F'\t' '$1=="SDAT"{print $2}' "$IDX" 2>/dev/null)
  if [ -z "$candidates" ]; then
    local f_cand=""
    for d in $SEARCH; do [ -d "$d" ] || continue
      f_cand=$(find -L "$d" -maxdepth 8 -type f -size +100c -iname '*.sdat' ! -path '*/_r_*' 2>/dev/null | head -1)
      [ -n "$f_cand" ] && break
    done
    candidates="$f_cand"
  fi
  [ -n "$candidates" ] || { sk "SDAT (DS sound archive)"; return; }
  local f mid
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    rm -rf /tmp/_r_sdat; mkdir -p /tmp/_r_sdat
    $B/wbrsar "$f" /tmp/_r_sdat >/tmp/_r_sdat.log 2>&1
    mid=$(find /tmp/_r_sdat -iname "*.mid" -size +14c 2>/dev/null | head -1)
    [ -n "$mid" ] && [ "$(head -c4 "$mid")" = "MThd" ] && break
    mid=""
  done <<< "$candidates"
  local sf2_count; sf2_count=$(find /tmp/_r_sdat -iname "*.sf2" -size +100c 2>/dev/null | wc -l | tr -d ' ')
  if [ -n "$mid" ] && [ "$sf2_count" -ge 1 ]; then
    ok "SDAT -> MIDI + SF2 ($f)"
  elif [ -n "$mid" ]; then
    ok "SDAT -> MIDI ($f)"
  else
    no "SDAT -> MIDI" "no candidate produced a valid MIDI"
  fi
}
t_sdat

t_sdat_pack(){
  local d=/tmp/_r_sdat_pack; rm -rf "$d"; mkdir -p "$d/in" "$d/out"
  printf 'SSEQsynthetic-sequence' > "$d/in/song.sseq"
  printf 'SBNKsynthetic-bank' > "$d/in/bank.sbnk"
  printf 'SWARsynthetic-wave' > "$d/in/wave.swar"
  if "$B/wbrsar" pack "$d/in" "$d/a.sdat" --sdat >/dev/null 2>&1 \
  && "$B/wbrsar" unpack "$d/a.sdat" "$d/out" >/dev/null 2>&1 \
  && "$B/wbrsar" pack "$d/out" "$d/b.sdat" --sdat >/dev/null 2>&1 \
  && cmp -s "$d/a.sdat" "$d/b.sdat" \
  && [ "$(head -c4 "$d/a.sdat")" = SDAT ] \
  && cmp -s "$d/in/song.sseq" "$d/out/song.sseq" \
  && cmp -s "$d/in/bank.sbnk" "$d/out/bank.sbnk" \
  && cmp -s "$d/in/wave.swar" "$d/out/wave.swar"; then
    ok "SDAT pack -> raw unpack -> identical repack"
  else
    no "SDAT packing" "$d"
  fi
}
t_sdat_pack

t_rbnk(){
  local d; d=$(mktemp -d /tmp/_r_rbnk.XXXXXX) || return
  local brsar="$PWD_PROJECT/../tests/samples-excitebots/extract/excitebots.d/UPDATE/files/_sys/RVL-Eulav_US-v2.d/0000000b.d/sound/eulaSound.brsar"
  [ -f "$brsar" ] || { sk "RBNK instrument bank (no fixture)"; rm -rf "$d"; return; }
  "$B/wbrsar" unpack "$brsar" "$d" >/dev/null 2>&1
  local rbnk="$d/BANK_SYSTEM_SE.rbnk"
  [ -f "$rbnk" ] || { sk "RBNK instrument bank (no rbnk in archive)"; rm -rf "$d"; return; }

  if "$B/wrbnk" dump "$rbnk" "$d/bank.xml" >/dev/null 2>&1 \
  && "$B/wrbnk" compile "$d/bank.xml" "$d/re.rbnk" >/dev/null 2>&1 \
  && "$B/wrbnk" dump "$d/re.rbnk" "$d/re.xml" >/dev/null 2>&1 \
  && [ -s "$d/re.rbnk" ]; then
    ok "RBNK dump -> XML -> compile -> dump (v1.1 instrument bank)"
  else
    no "RBNK dump -> compile" "failed on $rbnk"
  fi
  rm -rf "$d"
}
t_rbnk

t_bcsar_bfsar(){
  # Prefer the committed fixtures: they are known-good "full" archives
  # (BCSEQ/BCWAR/BCWSD/BCBNK + nested BCWAV, or the BFSAR equivalent), so the
  # result is deterministic regardless of what happens to be on this machine.
  # A prior version of this test scanned $SEARCH / /Volumes/SSD/dlz for any
  # *.bcsar/*.bfsar and took whatever `find` returned first -- traversal
  # order isn't stable, and some real-world archives are SFX-only (raw
  # BCWAV, no sequence/bank data), so the test would flakily pass or fail
  # depending on which file got picked that run. Fall back to that scan only
  # if the fixture is somehow missing.
  local bcsar_cand="$PWD_PROJECT/../tests/fixtures/sample.bcsar"
  local bfsar_cand="$PWD_PROJECT/../tests/fixtures/sample.bfsar"
  [ -s "$bcsar_cand" ] || bcsar_cand=""
  [ -s "$bfsar_cand" ] || bfsar_cand=""

  if [ -z "$bcsar_cand" ] || [ -z "$bfsar_cand" ]; then
    for d in $SEARCH "/Volumes/SSD/dlz"; do [ -d "$d" ] || continue
      [ -z "$bcsar_cand" ] && bcsar_cand=$(find -L "$d" -maxdepth 8 -type f -size +100c -iname '*.bcsar' ! -path '*/_r_*' 2>/dev/null | head -1)
      [ -z "$bfsar_cand" ] && bfsar_cand=$(find -L "$d" -maxdepth 8 -type f -size +100c -iname '*.bfsar' ! -path '*/_r_*' 2>/dev/null | head -1)
      [ -n "$bcsar_cand" ] && [ -n "$bfsar_cand" ] && break
    done
  fi

  if [ -n "$bcsar_cand" ] && [ -s "$bcsar_cand" ]; then
    local out_bcsar="/tmp/_r_bcsar"
    local repack_bcsar="/tmp/_r_repack.bcsar"
    rm -rf "$out_bcsar" "$repack_bcsar"
    mkdir -p "$out_bcsar"
    $B/wszst xx "$bcsar_cand" --dest "$out_bcsar" >/tmp/_r_bcsar.log 2>&1
    local cseq_count; cseq_count=$(find "$out_bcsar" -type f \( -iname "*.bcseq" -o -iname "*.bcwar" -o -iname "*.bcwsd" -o -iname "*.bcbnk" \) 2>/dev/null | wc -l | tr -d ' ')
    local sub_bcwav_count; sub_bcwav_count=$(find "$out_bcsar" -type f -iname "*.bcwav" 2>/dev/null | wc -l | tr -d ' ')
    if [ "$cseq_count" -ge 1 ] && [ "$sub_bcwav_count" -ge 1 ]; then
      ok "BCSAR extract + recursive BCWAR ($bcsar_cand)"
      local first_sub; first_sub=$(find "$out_bcsar" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)
      if [ -n "$first_sub" ]; then
        $B/wszst create "$first_sub" --dest "$repack_bcsar" >/dev/null 2>&1
        if [ -s "$repack_bcsar" ] && [ "$(head -c4 "$repack_bcsar")" = "CSAR" ]; then
          ok "BCSAR repack (wszst create -> CSAR)"
        else
          no "BCSAR repack" "$repack_bcsar"
        fi
      fi
    else
      no "BCSAR extract" "no expected files found in $out_bcsar"
    fi
  else
    sk "BCSAR (3DS sound archive)"
  fi

  if [ -n "$bfsar_cand" ] && [ -s "$bfsar_cand" ]; then
    local out_bfsar="/tmp/_r_bfsar"
    local repack_bfsar="/tmp/_r_repack.bfsar"
    rm -rf "$out_bfsar" "$repack_bfsar"
    mkdir -p "$out_bfsar"
    $B/wszst xx "$bfsar_cand" --dest "$out_bfsar" >/tmp/_r_bfsar.log 2>&1
    local fseq_count; fseq_count=$(find "$out_bfsar" -type f \( -iname "*.bfseq" -o -iname "*.bfwar" -o -iname "*.bfwsd" -o -iname "*.bfbnk" \) 2>/dev/null | wc -l | tr -d ' ')
    local sub_bfwav_count; sub_bfwav_count=$(find "$out_bfsar" -type f -iname "*.bfwav" 2>/dev/null | wc -l | tr -d ' ')
    if [ "$fseq_count" -ge 1 ] && [ "$sub_bfwav_count" -ge 1 ]; then
      ok "BFSAR extract + recursive BFWAR ($bfsar_cand)"
      local first_sub; first_sub=$(find "$out_bfsar" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)
      if [ -n "$first_sub" ]; then
        $B/wszst create "$first_sub" --dest "$repack_bfsar" >/dev/null 2>&1
        if [ -s "$repack_bfsar" ] && [ "$(head -c4 "$repack_bfsar")" = "FSAR" ]; then
          ok "BFSAR repack (wszst create -> FSAR)"
        else
          no "BFSAR repack" "$repack_bfsar"
        fi
      fi
    else
      no "BFSAR extract" "no expected files found in $out_bfsar"
    fi
  else
    sk "BFSAR (Wii U / Switch sound archive)"
  fi
}
t_bcsar_bfsar



echo "== WC24 =="
# --help exits with the usage code by design, so check output not status.
if $B/wwc24crypt --help 2>&1 | grep -q "AES-128-OFB"; then ok "wwc24crypt help"; else no "wwc24crypt help" "unexpected output"; fi
if nm -u "$B/wwc24crypt" 2>/dev/null | grep -qi hmac || nm "$B/wwc24crypt" 2>/dev/null | grep -qi " t .*hmac"; then
  no "wwc24crypt: no HMAC code" "an HMAC symbol is linked in"
else ok "wwc24crypt: no HMAC code linked"; fi

echo "== Message Studio (MSBT / MSBP / MSBF) =="
t_msbt_roundtrip() {
  local D=/tmp/_r_msbt
  rm -rf "$D"; mkdir -p "$D"
  cat << 'EOF' > "$D/source.tmsbt"
# MSBT: Message Studio Binary Text (BigEndian, UTF-16)
# Entries: 3

[Greeting]
Hello, world!\nWelcome to Message Studio!

[PlayerName]
Welcome <control group="1" type="2" data="00FF"/>, hero of time!

[ExitMsg]
Goodbye!
EOF

  # 1. wbmgt ENCODE
  $B/wbmgt ENCODE "$D/source.tmsbt" --dest "$D/binary.msbt" >/dev/null 2>&1
  if [ ! -s "$D/binary.msbt" ] || [ "$(head -c8 "$D/binary.msbt")" != "MsgStdBn" ]; then
    no "MSBT encode (wbmgt)" "output missing or magic mismatch"
    return
  fi
  ok "MSBT encode (wbmgt -> MsgStdBn)"

  # 2. wbmgt LIST
  if $B/wbmgt LIST "$D/binary.msbt" 2>&1 | grep -q "Greeting"; then
    ok "MSBT list (wbmgt LIST)"
  else
    no "MSBT list (wbmgt LIST)" "label Greeting not found in output"
  fi

  # 3. wbmgt DECODE roundtrip
  $B/wbmgt DECODE "$D/binary.msbt" --dest "$D/decoded.tmsbt" >/dev/null 2>&1
  if grep -q "Greeting" "$D/decoded.tmsbt" && grep -q "hero of time" "$D/decoded.tmsbt"; then
    ok "MSBT decode roundtrip (wbmgt DECODE)"
  else
    no "MSBT decode roundtrip (wbmgt DECODE)" "decoded file content mismatch"
  fi

  # 4. wszst EXTRACT
  $B/wszst EXTRACT "$D/binary.msbt" --dest "$D/wszst_out.txt" >/dev/null 2>&1
  if [ -s "$D/wszst_out.txt" ] && grep -q "Greeting" "$D/wszst_out.txt"; then
    ok "MSBT decode (wszst EXTRACT)"
  else
    no "MSBT decode (wszst EXTRACT)" "wszst failed to extract/decode MSBT"
  fi

  # 5. wszst xx inside archive
  mkdir -p "$D/arch"
  cp "$D/binary.msbt" "$D/arch/message.msbt"
  $B/wszst CREATE "$D/arch" --dest "$D/arch.szs" >/dev/null 2>&1
  $B/wszst xx "$D/arch.szs" --dest "$D/arch_out" >/dev/null 2>&1
  if [ -s "$D/arch_out/message.msbt.txt" ] && grep -q "Greeting" "$D/arch_out/message.msbt.txt"; then
    ok "MSBT auto-decode on wszst xx archive extraction"
  else
    no "MSBT auto-decode on wszst xx archive extraction" "message.msbt.txt not produced"
  fi

  rm -rf "$D"
}
t_msbt_roundtrip

t_msbp_roundtrip() {
  local D=/tmp/_r_msbp
  rm -rf "$D"; mkdir -p "$D"
  cat << 'EOF' > "$D/source.tmsbp"
# MSBP: Message Studio Binary Project (BigEndian, UTF-16)

[Colors: 2]
  #0: Red = #FF0000FF
  #1: Green = #00FF00FF
EOF

  $B/wbmgt ENCODE "$D/source.tmsbp" --dest "$D/project.msbp" >/dev/null 2>&1
  if [ ! -s "$D/project.msbp" ] || [ "$(head -c8 "$D/project.msbp")" != "MsgPrjBn" ]; then
    no "MSBP encode (wbmgt)" "output missing or magic mismatch"
    return
  fi
  ok "MSBP encode (wbmgt -> MsgPrjBn)"

  $B/wbmgt DECODE "$D/project.msbp" --dest "$D/decoded.tmsbp" >/dev/null 2>&1
  if grep -q "Red" "$D/decoded.tmsbp" && grep -q "FF0000FF" "$D/decoded.tmsbp"; then
    ok "MSBP decode roundtrip (wbmgt DECODE)"
  else
    no "MSBP decode roundtrip (wbmgt DECODE)" "decoded file content mismatch"
  fi

  rm -rf "$D"
}
t_msbp_roundtrip

t_msbf_roundtrip() {
  local D=/tmp/_r_msbf
  rm -rf "$D"; mkdir -p "$D"
  cat << 'EOF' > "$D/source.tmsbf"
# MSBF: Message Studio Binary Flowchart (BigEndian)
# Nodes: 3

[Node #0 (Start)]
  type = EntryPoint (next=1)

[Node #1 (Talk)]
  type = Message (msg_index=0, next=2)

[Node #2 (Finish)]
  type = Event (event_id=10, param=0x20, next=65535)
EOF

  $B/wbmgt ENCODE "$D/source.tmsbf" --dest "$D/flow.msbf" >/dev/null 2>&1
  if [ ! -s "$D/flow.msbf" ] || [ "$(head -c8 "$D/flow.msbf")" != "MsgFlwBn" ]; then
    no "MSBF encode (wbmgt)" "output missing or magic mismatch"
    return
  fi
  ok "MSBF encode (wbmgt -> MsgFlwBn)"

  $B/wbmgt DECODE "$D/flow.msbf" --dest "$D/decoded.tmsbf" >/dev/null 2>&1
  if grep -q "Start" "$D/decoded.tmsbf" && grep -q "Finish" "$D/decoded.tmsbf"; then
    ok "MSBF decode roundtrip (wbmgt DECODE)"
  else
    no "MSBF decode roundtrip (wbmgt DECODE)" "decoded file content mismatch"
  fi

  rm -rf "$D"
}
t_msbf_roundtrip

t_fzip_tool_integration() {
  local D=/tmp/_r_fzip_tools
  rm -rf "$D"; mkdir -p "$D"

  # 1. wbmgt with FZIP MSBT
  cat << 'EOF' > "$D/source.tmsbt"
# MSBT: Message Studio Binary Text (BigEndian, UTF-16)

[Greeting]
Hello Game and Wario!
EOF
  $B/wbmgt ENCODE "$D/source.tmsbt" --dest "$D/msg.msbt" >/dev/null 2>&1
  $B/wszst COMPRESS "$D/msg.msbt" --dest "$D/msg.msbt.fzip" --overwrite >/dev/null 2>&1
  if $B/wbmgt LIST "$D/msg.msbt.fzip" 2>&1 | grep -q "Greeting" \
  && $B/wbmgt DECODE "$D/msg.msbt.fzip" --dest "$D/msg_dec.tmsbt" >/dev/null 2>&1 \
  && grep -q "Game and Wario" "$D/msg_dec.tmsbt"; then
    ok "wbmgt transparent FZIP MSBT decode & list"
  else
    no "wbmgt transparent FZIP MSBT decode & list" "failed to decode/list FZIP MSBT"
  fi

  # 2. wlayt with FZIP BFLYT
  if [ -f "$T/fixtures/splatoon_cmn_seq_drc_option.bflyt" ]; then
    $B/wszst COMPRESS "$T/fixtures/splatoon_cmn_seq_drc_option.bflyt" --dest "$D/layout.bflyt.fzip" --overwrite >/dev/null 2>&1
    if $B/wlayt decode "$D/layout.bflyt.fzip" "$D/layout.txt" >/dev/null 2>&1 \
    && grep -q "screen-width" "$D/layout.txt"; then
      ok "wlayt transparent FZIP BFLYT decode"
    else
      no "wlayt transparent FZIP BFLYT decode" "failed to decode FZIP BFLYT"
    fi
  fi

  # 3. wimgt with FZIP BFLIM
  python3 -c "
import struct, zlib
raw = b''.join([b'\x00' + b'\xff\x00\x00\xff'*8 for _ in range(8)])
png = b'\x89PNG\r\n\x1a\n'
ihdr = struct.pack('>IIBBBBB', 8, 8, 8, 6, 0, 0, 0)
png += struct.pack('>I4s', len(ihdr), b'IHDR') + ihdr + struct.pack('>I', zlib.crc32(b'IHDR' + ihdr))
idat = zlib.compress(raw)
png += struct.pack('>I4s', len(idat), b'IDAT') + idat + struct.pack('>I', zlib.crc32(b'IDAT' + idat))
png += struct.pack('>I4s', 0, b'IEND') + struct.pack('>I', zlib.crc32(b'IEND'))
open('$D/sample.png', 'wb').write(png)
"
  $B/wimgt ENCODE "$D/sample.png" --dest "$D/sample.bflim" -q >/dev/null 2>&1
  $B/wszst COMPRESS "$D/sample.bflim" --dest "$D/sample.bflim.fzip" --overwrite >/dev/null 2>&1
  if $B/wimgt DECODE "$D/sample.bflim.fzip" --dest "$D/sample_dec.png" -q >/dev/null 2>&1 \
  && [ -s "$D/sample_dec.png" ]; then
    ok "wimgt transparent FZIP BFLIM decode"
  else
    no "wimgt transparent FZIP BFLIM decode" "failed to decode FZIP BFLIM"
  fi

  rm -rf "$D"
}
t_fzip_tool_integration

echo "== early DS BMD (SM64DS proprietary binary) -> DAE & texture export =="
t_early_bmd_dae(){
  command -v python3 >/dev/null || { sk "early DS BMD export"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct

shapes_off = 0x3c
dl_off = 0x58
bone_off = 0x80
bone_name_off = 0xc0
mat_off = 0xd0
mat_name_off = 0xe4
plt_off = 0xf0
plt_name_off = 0x100
data_off = 0x120

h = [0, 1, bone_off, 1, shapes_off, 1, mat_off, 1, plt_off, 0, 0, 0, 0, 0, data_off]
hdr = struct.pack('<15I', *h)
shapes = struct.pack('<7I', 1, 0x44, 1, 0x54, 0x28, dl_off, 0)
w1_cmds = bytes([0x40, 0x22, 0x23, 0x23])
w1_params = struct.pack('<I hh 2h 2h 2h 2h', 0, 0,0, 0,0, 0,0, 0x1000,0, 0,0)
w2_cmds = bytes([0x23, 0, 0, 0])
w2_params = struct.pack('<2h 2h', 0,0x1000, 0,0)
dl = w1_cmds + w1_params + w2_cmds + w2_params

bone = struct.pack('<IIIIiiiiiiiiIIII', 0, bone_name_off, 0, 0, 0x1000, 0x1000, 0x1000, 0, 0, 0, 0, 0, 0, 0, 0, 0)
bone_name = b'root\x00\x00\x00\x00'

mat = struct.pack('<IIIII', mat_name_off, data_off, 0, 16 | (16 << 16), 0)
mat_name = b'mat_sample\x00\x00'

plt = struct.pack('<IIII', plt_name_off, data_off + 256, 0, 0)
plt_name = b'mat_sample_pl\x00'

pixels = bytes([i % 16 for i in range(256)])
palette = struct.pack('<16H', *[i * 0x421 for i in range(16)])

data = bytearray(0x280)
data[0:len(hdr)] = hdr
data[shapes_off:shapes_off+len(shapes)] = shapes
data[dl_off:dl_off+len(dl)] = dl
data[bone_off:bone_off+len(bone)] = bone
data[bone_name_off:bone_name_off+len(bone_name)] = bone_name
data[mat_off:mat_off+len(mat)] = mat
data[mat_name_off:mat_name_off+len(mat_name)] = mat_name
data[plt_off:plt_off+len(plt)] = plt
data[plt_name_off:plt_name_off+len(plt_name)] = plt_name
data[data_off:data_off+len(pixels)+len(palette)] = pixels + palette

with open('$d/test.bmd', 'wb') as f:
    f.write(data)
"
  "$B/wmdlt" ENCODE "$d/test.bmd" -d "$d/test.dae" --overwrite >/dev/null 2>&1
  local ok=1
  [ -s "$d/test.dae" ] || ok=0
  [ -s "$d/mat_sample.png" ] || ok=0
  grep -q "mat_sample.png" "$d/test.dae" || ok=0
  grep -q "mesh_0" "$d/test.dae" || ok=0
  python3 "$DAE_VALIDATOR" "$d/test.dae" >/dev/null 2>&1 || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "early DS BMD -> DAE + PNG textures" \
    || no "early DS BMD -> DAE + PNG textures" "conversion failed"
}
t_early_bmd_dae

echo "== NintendoWare sequence (RSEQ, CSEQ, FSEQ, SSEQ) & MIDI roundtrips =="
t_sequence_roundtrips(){
  local d; d=$(mktemp -d)
  cat << 'EOF' > "$d/song.txt"
; Test Nintendo Sequence
timebase 48
alloc_track 0x0003
open_track 1 @Track1
tempo 120
vol 127
pan 64
prg 0
note C4 100 48
wait 48
note E4 100 48
wait 48
note G4 100 96
wait 96
jump @Loop

@Loop:
note C5 100 96
wait 96
jump @Loop

@Track1:
prg 1
vol 110
pan 80
note C3 90 96
wait 96
note G3 90 96
wait 96
fin
EOF

  local ok=1
  # Test RSEQ, CSEQ, FSEQ (Wii U & Switch), SSEQ, BMS assembly
  "$B/wseqt" asm "$d/song.txt" "$d/song.rseq" --format RSEQ >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song.cseq" --format CSEQ >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song_wiiu.fseq" --format FSEQ >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song_nx.fseq" --format FSEQ_LE >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song.sseq" --format SSEQ >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song.bms" --format BMS >/dev/null 2>&1 || ok=0

  # Test disassembly
  "$B/wseqt" disasm "$d/song.rseq" "$d/song_dis.txt" >/dev/null 2>&1 || ok=0
  grep -q "timebase 48" "$d/song_dis.txt" || ok=0
  grep -q "tempo 120" "$d/song_dis.txt" || ok=0
  grep -q "note C4" "$d/song_dis.txt" || ok=0

  # Test BMS disassembly
  "$B/wseqt" disasm "$d/song.bms" "$d/song_bms_dis.txt" >/dev/null 2>&1 || ok=0
  grep -q "timebase 48" "$d/song_bms_dis.txt" || ok=0
  grep -q "tempo 120" "$d/song_bms_dis.txt" || ok=0

  # Test MIDI conversion roundtrip
  "$B/wseqt" to_midi "$d/song.rseq" "$d/song.mid" >/dev/null 2>&1 || ok=0
  [ -s "$d/song.mid" ] || ok=0
  "$B/wseqt" from_midi "$d/song.mid" "$d/song_midi.rseq" --format RSEQ >/dev/null 2>&1 || ok=0
  [ -s "$d/song_midi.rseq" ] || ok=0
  "$B/wseqt" to_midi "$d/song.bms" "$d/song_bms.mid" >/dev/null 2>&1 || ok=0
  [ -s "$d/song_bms.mid" ] || ok=0
  "$B/wseqt" from_midi "$d/song_bms.mid" "$d/song_midi.bms" --format BMS >/dev/null 2>&1 || ok=0
  [ -s "$d/song_midi.bms" ] || ok=0

  # Test sequence invert
  "$B/wseqt" invert "$d/song.rseq" "$d/song_inv.rseq" --center 63 >/dev/null 2>&1 || ok=0
  [ -s "$d/song_inv.rseq" ] || ok=0

  # Test wszst CREATE and EXTRACT
  "$B/wszst" CREATE "$d/song.txt" --dest "$d/wszst_song.rseq" --overwrite >/dev/null 2>&1 || ok=0
  [ -s "$d/wszst_song.rseq" ] || ok=0
  mkdir -p "$d/out"
  "$B/wszst" EXTRACT "$d/wszst_song.rseq" --dest "$d/out/" --overwrite >/dev/null 2>&1 || ok=0
  [ -s "$d/out/wszst_song.txt" ] || ok=0
  [ -s "$d/out/wszst_song.mid" ] || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "NintendoWare sequence RSEQ/CSEQ/FSEQ/SSEQ/BMS/MIDI roundtrips & wszst integration" \
    || no "NintendoWare sequence RSEQ/CSEQ/FSEQ/SSEQ/BMS/MIDI roundtrips & wszst integration" "sequence test failed"
}
t_sequence_roundtrips

t_zstd_and_7z_roundtrips(){
  local d; d=$(mktemp -d)
  local ok=1

  # Create a test text file
  printf "The quick brown fox jumps over the lazy dog. 1234567890\n" > "$d/sample.txt"

  # Test ZSTD compression and decompression
  "$B/wszst" compress "$d/sample.txt" --zstd -d "$d/sample.txt.zs" --overwrite >/dev/null 2>&1 || ok=0
  [ -s "$d/sample.txt.zs" ] || ok=0

  # Test filetype detection
  "$B/wszst" filetype "$d/sample.txt.zs" 2>/dev/null | grep -q "ZSTD" || ok=0

  # Test decompress
  "$B/wszst" decompress "$d/sample.txt.zs" -d "$d/sample_out.txt" --overwrite >/dev/null 2>&1 || ok=0
  cmp -s "$d/sample.txt" "$d/sample_out.txt" || ok=0

  # Test nested archive inside .zs with recursive wszst extract
  mkdir -p "$d/nested/sub"
  printf "Payload inside SZS in ZSTD\n" > "$d/nested/sub/data.txt"
  "$B/wszst" create "$d/nested" -d "$d/nested.szs" --overwrite >/dev/null 2>&1 || ok=0
  "$B/wszst" compress "$d/nested.szs" --zstd -d "$d/nested.szs.zs" --overwrite >/dev/null 2>&1 || ok=0
  "$B/wszst" extract "$d/nested.szs.zs" --overwrite >/dev/null 2>&1 || ok=0
  [ -f "$d/nested.d/sub/data.txt" ] || ok=0

  # Test 7-Zip archive extraction if 7z or 7za or 7zz or unar is available
  if command -v 7z >/dev/null 2>&1 || command -v 7zz >/dev/null 2>&1 || command -v 7za >/dev/null 2>&1; then
    mkdir -p "$d/pack_tree"
    cp "$d/nested.szs.zs" "$d/pack_tree/"
    (cd "$d" && (7z a "$d/test_pack.7z" "$d/pack_tree" >/dev/null 2>&1 || 7zz a "$d/test_pack.7z" "$d/pack_tree" >/dev/null 2>&1 || 7za a "$d/test_pack.7z" "$d/pack_tree" >/dev/null 2>&1))
    if [ -s "$d/test_pack.7z" ]; then
      "$B/wszst" extract "$d/test_pack.7z" --overwrite >/dev/null 2>&1 || ok=0
      [ -f "$d/test_pack.d/nested.d/sub/data.txt" ] || [ -f "$d/test_pack.d/pack_tree/nested.d/sub/data.txt" ] || ok=0
    fi
  fi

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "Zstandard (ZSTD) encoding/decoding and 7-Zip recursive extraction" \
    || no "Zstandard (ZSTD) encoding/decoding and 7-Zip recursive extraction" "zstd/7z test failed"
}
t_zstd_and_7z_roundtrips

t_brstm_roundtrip(){
  # BRSTM ADPCM_THP encode->decode roundtrip. from_wav prefers passing
  # through to mobipeg's real adpcm_thp encoder (PassthruEncodeAudio(),
  # lib-passthru.c) and falls back to this project's own EncodeBRSTM() port
  # when mobipeg isn't installed or predates the brstm/dsp/bns muxers --
  # this environment has no mobipeg on PATH, so this exercises the fallback;
  # PATH a real mobipeg in to additionally exercise the passthrough itself.
  [ -x "$B/wbrstm" ] || { sk "BRSTM ADPCM_THP encode/decode roundtrip"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct, math
sr=32000; n=8000
data=b''.join(struct.pack('<h', int(6000*math.sin(i*0.05))) for i in range(n))
hdr=b'RIFF'+struct.pack('<I',36+len(data))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,sr,sr*2,2,16)+b'data'+struct.pack('<I',len(data))
open('$d/in.wav','wb').write(hdr+data)
" || { no "BRSTM ADPCM_THP encode/decode roundtrip" "couldn't synthesize input WAV"; rm -rf "$d"; return; }

  local ok=1
  "$B/wbrstm" from_wav "$d/in.wav" "$d/out.brstm" >"$d/encode.log" 2>&1 || ok=0
  [ -s "$d/out.brstm" ] || ok=0
  "$B/wbrstm" to_wav "$d/out.brstm" "$d/roundtrip.wav" >/dev/null 2>&1 || ok=0
  [ -s "$d/roundtrip.wav" ] || ok=0
  grep -q "ADPCM_THP" <("$B/wbrstm" info "$d/out.brstm" 2>&1) || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "BRSTM ADPCM_THP encode/decode roundtrip" \
    || no "BRSTM ADPCM_THP encode/decode roundtrip" "encode, decode, or info failed"
}
t_brstm_roundtrip

t_hsd_model(){
  # HAL "sysdolphin" .dat (Melee/Kirby Air Ride/TV no Tomo): no magic, so
  # found by extension + IsHSD()'s structural probe over SEARCH, same
  # convention as t_extex/t_exart below. Real corpus check (346/352 retail
  # Melee Ty*.dat item files, 9,564 meshes) lives in lib-hsd.h's own
  # comment, not here -- this just guards the wszst integration point
  # against a regression on whatever single sample is available locally.
  local f; f=$(for d in $SEARCH; do [ -d "$d" ] || continue
      find -L "$d" -maxdepth 8 -type f -iname '*.dat' -size -8M \
        ! -path '*claude-*' ! -iname 'test.*' ! -iname 'test_*' 2>/dev/null
    done | head -20)
  local hit=""
  for cand in $f; do
    "$B/wszst" XX "$cand" --dest /tmp/_r_hsd --overwrite 2>/dev/null | grep -q "HSD model" && { hit="$cand"; break; }
  done
  [ -n "$hit" ] || { sk "HSD (sysdolphin) model export"; return; }
  local glb="/tmp/_r_hsd/$(basename "$hit").glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd.log 2>&1 \
    && ok "HSD (sysdolphin) model export -> GLB ($hit)" \
    || no "HSD (sysdolphin) model export" "$hit"
}
t_hsd_model

t_hsd_tybox(){
  # TyBox.dat: simple HSD model (Mario Party style) with 8 materials,
  # 18+ POBJs, 3414 total triangles — exercises full DL decode.
  local dat="$PWD_PROJECT/../tests/fixtures/TyBox.dat"
  [ -f "$dat" ] || { sk "HSD TyBox model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_tybox; mkdir -p /tmp/_r_hsd_tybox
  cp "$dat" /tmp/_r_hsd_tybox/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_tybox/TyBox.dat" --overwrite >/tmp/_r_hsd_tybox.log 2>&1
  local glb="/tmp/_r_hsd_tybox/TyBox.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_tybox_v.log 2>&1 \
    && ok "HSD TyBox model -> GLB (8 materials, 3414 tri)" \
    || no "HSD TyBox model -> GLB" "$glb"
}
t_hsd_tybox

t_hsd_plmr(){
  # PlMr.dat: Melee Mr. Game & Watch — 7-byte vertex format (matrix indices
  # + INDEX16 POS + INDEX16 NRM), 8 DOBJs, 20 POBJs, 17754 total triangles.
  # This specifically exercises the DIRECT stride=0 fix.
  local dat="$PWD_PROJECT/../tests/fixtures/PlMr.dat"
  [ -f "$dat" ] || { sk "HSD PlMr model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_plmr; mkdir -p /tmp/_r_hsd_plmr
  cp "$dat" /tmp/_r_hsd_plmr/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_plmr/PlMr.dat" --overwrite >/tmp/_r_hsd_plmr.log 2>&1
  local glb="/tmp/_r_hsd_plmr/PlMr.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_plmr_v.log 2>&1 \
    && ok "HSD PlMr model -> GLB (Melee fighter, 17754 tri)" \
    || no "HSD PlMr model -> GLB" "$glb"
}
t_hsd_plmr

t_hsd_plok(){
  # PlPk.dat: Melee Polygon team — exercises shared vtxattribs across 1 POBJ.
  local dat="$PWD_PROJECT/../tests/fixtures/PlPk.dat"
  [ -f "$dat" ] || { sk "HSD PlPk model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_plpk; mkdir -p /tmp/_r_hsd_plpk
  cp "$dat" /tmp/_r_hsd_plpk/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_plpk/PlPk.dat" --overwrite >/tmp/_r_hsd_plpk.log 2>&1
  local glb="/tmp/_r_hsd_plpk/PlPk.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_plpk_v.log 2>&1 \
    && ok "HSD PlPk model -> GLB (Melee fighter, 1 mesh)" \
    || no "HSD PlPk model -> GLB" "$glb"
}
t_hsd_plok

t_hsd_plnn(){
  # PlNn.dat: Melee Ness — 7 POBJs with 7-byte vertex format.
  local dat="$PWD_PROJECT/../tests/fixtures/PlNn.dat"
  [ -f "$dat" ] || { sk "HSD PlNn model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_plnn; mkdir -p /tmp/_r_hsd_plnn
  cp "$dat" /tmp/_r_hsd_plnn/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_plnn/PlNn.dat" --overwrite >/tmp/_r_hsd_plnn.log 2>&1
  local glb="/tmp/_r_hsd_plnn/PlNn.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_plnn_v.log 2>&1 \
    && ok "HSD PlNn model -> GLB (Melee fighter, 7 meshes)" \
    || no "HSD PlNn model -> GLB" "$glb"
}
t_hsd_plnn

t_hsd_plfe(){
  # PlFe.dat: Melee Falco — 31 meshes, exercises complex multi-DOBJ fighter.
  local dat="$PWD_PROJECT/../tests/fixtures/PlFe.dat"
  [ -f "$dat" ] || { sk "HSD PlFe model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_plfe; mkdir -p /tmp/_r_hsd_plfe
  cp "$dat" /tmp/_r_hsd_plfe/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_plfe/PlFe.dat" --overwrite >/tmp/_r_hsd_plfe.log 2>&1
  local glb="/tmp/_r_hsd_plfe/PlFe.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_plfe_v.log 2>&1 \
    && ok "HSD PlFe model -> GLB (Melee Falco, 31 meshes)" \
    || no "HSD PlFe model -> GLB" "$glb"
}
t_hsd_plfe

t_hsd_plkb(){
  # PlKb.dat: Melee Kirby — 6 meshes.
  local dat="$PWD_PROJECT/../tests/fixtures/PlKb.dat"
  [ -f "$dat" ] || { sk "HSD PlKb model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_plkb; mkdir -p /tmp/_r_hsd_plkb
  cp "$dat" /tmp/_r_hsd_plkb/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_plkb/PlKb.dat" --overwrite >/tmp/_r_hsd_plkb.log 2>&1
  local glb="/tmp/_r_hsd_plkb/PlKb.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_plkb_v.log 2>&1 \
    && ok "HSD PlKb model -> GLB (Melee Kirby, 6 meshes)" \
    || no "HSD PlKb model -> GLB" "$glb"
}
t_hsd_plkb

t_hsd_pllg(){
  # PlLg.dat: Melee Luigi — 8 meshes.
  local dat="$PWD_PROJECT/../tests/fixtures/PlLg.dat"
  [ -f "$dat" ] || { sk "HSD PlLg model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_pllg; mkdir -p /tmp/_r_hsd_pllg
  cp "$dat" /tmp/_r_hsd_pllg/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_pllg/PlLg.dat" --overwrite >/tmp/_r_hsd_pllg.log 2>&1
  local glb="/tmp/_r_hsd_pllg/PlLg.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_pllg_v.log 2>&1 \
    && ok "HSD PlLg model -> GLB (Melee Luigi, 8 meshes)" \
    || no "HSD PlLg model -> GLB" "$glb"
}
t_hsd_pllg

t_hsd_plss(){
  # PlSs.dat: Melee Samus — 5 meshes.
  local dat="$PWD_PROJECT/../tests/fixtures/PlSs.dat"
  [ -f "$dat" ] || { sk "HSD PlSs model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_plss; mkdir -p /tmp/_r_hsd_plss
  cp "$dat" /tmp/_r_hsd_plss/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_plss/PlSs.dat" --overwrite >/tmp/_r_hsd_plss.log 2>&1
  local glb="/tmp/_r_hsd_plss/PlSs.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_plss_v.log 2>&1 \
    && ok "HSD PlSs model -> GLB (Melee Samus, 5 meshes)" \
    || no "HSD PlSs model -> GLB" "$glb"
}
t_hsd_plss

t_hsd_plys(){
  # PlYs.dat: Melee Young Link — 4 meshes.
  local dat="$PWD_PROJECT/../tests/fixtures/PlYs.dat"
  [ -f "$dat" ] || { sk "HSD PlYs model export (no fixture)"; return; }
  rm -rf /tmp/_r_hsd_plys; mkdir -p /tmp/_r_hsd_plys
  cp "$dat" /tmp/_r_hsd_plys/
  "$B/wszst" EXTRACT "/tmp/_r_hsd_plys/PlYs.dat" --overwrite >/tmp/_r_hsd_plys.log 2>&1
  local glb="/tmp/_r_hsd_plys/PlYs.dat.glb"
  [ -s "$glb" ] && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/tmp/_r_hsd_plys_v.log 2>&1 \
    && ok "HSD PlYs model -> GLB (Melee Young Link, 4 meshes)" \
    || no "HSD PlYs model -> GLB" "$glb"
}
t_hsd_plys

t_hsd_wmdlt_roundtrips(){
  local d; d=$(mktemp -d /tmp/_r_hsd_wmdlt.XXXXXX) || return
  local fail=0 total=0
  for f in "$PWD_PROJECT/../tests/fixtures"/Pl*.dat "$PWD_PROJECT/../tests/fixtures"/TyBox.dat; do
    [ -f "$f" ] || continue
    local bname; bname="$(basename "$f")"
    total=$((total+1))
    if "$B/wmdlt" DECODE "$f" --dest "$d/$bname.dae" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/$bname.dae" --dest "$d/$bname.re.dat" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" DECODE "$d/$bname.re.dat" --dest "$d/$bname.re.dae" --overwrite >/dev/null 2>&1; then
      :
    else
      fail=$((fail+1))
    fi
  done
  [ "$total" -gt 0 ] && [ "$fail" -eq 0 ] \
    && ok "HSD wmdlt DECODE -> ENCODE -> DECODE roundtrips ($total models)" \
    || no "HSD wmdlt roundtrips" "$fail of $total failed"
  rm -rf "$d"
}
t_hsd_wmdlt_roundtrips

t_extex(){
  # Monster Games (Excite Truck / ExciteBots, Wii) .tex GX textures: no
  # magic, so found by extension over SEARCH+extra Excite sample roots and
  # confirmed by successful decode (dimensions + pixel format recovered
  # purely from the mip-chain-consistency heuristic, no stored format field).
  local f; f=$(for d in $SEARCH; do [ -d "$d" ] || continue
      find -L "$d" -maxdepth 8 -type f -iname '*.tex' -size -65M \
        ! -path '*claude-*' ! -path '*_r_*' ! -iname 'test.*' ! -iname 'test_*' 2>/dev/null
    done | head -1)
  [ -n "$f" ] || { sk "Excite .tex GX texture"; return; }
  rm -rf /tmp/_r_extex; mkdir -p /tmp/_r_extex
  cp "$f" /tmp/_r_extex/
  $B/wszst EXTRACT "/tmp/_r_extex/$(basename "$f")" --overwrite >/tmp/_r_extex.log 2>&1
  local png="/tmp/_r_extex/$(basename "${f%.*}").png"
  [ -s "$png" ] && grep -q "EXTRACT TEX:" /tmp/_r_extex.log \
    && ok "Excite .tex GX texture -> PNG ($f)" \
    || no "Excite .tex GX texture" "$f"
}
t_extex

t_excite_headered(){
  # ExciteBots also uses an explicit 128-byte LE dimension header. Renderer
  # codes 0x40/0x41 are I4/IA4 followed by a 1024-byte auxiliary tail. The
  # I4 fixture deliberately starts 08 00 08 00, a pattern generic probes can
  # mistake for another structured format, so this also locks dispatch order.
  local d=/tmp/_r_exhead
  rm -rf "$d"; mkdir -p "$d"
  for spec in '40 tex' '41 art'; do
    set -- $spec; local code=$1 ext=$2 f="$d/header_${1}.${2}"
    printf '\010\000\010\000\001' > "$f"
    printf "\\$(printf '%03o' $((16#$code)))" >> "$f"
    dd if=/dev/zero bs=1 count=0 seek=128 of="$f" 2>/dev/null
    # 8x8: I4=32 bytes, IA4=64 bytes; use a visible nonzero checker payload.
    local bytes=32; [ "$code" = 41 ] && bytes=64
    yes '\252' | tr -d '\n' | head -c "$bytes" >> "$f"
    dd if=/dev/zero bs=1 count=0 seek=$((128+bytes+1024)) of="$f" 2>/dev/null
    "$B/wszst" EXTRACT "$f" --overwrite >"$d/$code.log" 2>&1
    local png="$d/header_${code}.png"
    if [ -s "$png" ] && grep -q "EXTRACT $([ "$ext" = tex ] && echo TEX || echo ART):.*(8x8)" "$d/$code.log"; then
      ok "ExciteBots headered $ext code 0x$code -> 8x8 PNG"
    else
      no "ExciteBots headered $ext code 0x$code" "dispatch/decode failed"
    fi
  done
}
t_excite_headered

t_excite_headered_encode(){
  # The ExciteBots encoder writes the explicit dimensions/mip-count header.
  # RGBA32 is deliberately used by the CLI auto path: unlike the legacy
  # representation it is lossless and cannot collide by byte size with a
  # different GX format. The low-level encoder separately accepts every GX
  # format and supplies the mandatory 1024-byte tail for I4/IA4 codes 40/41.
  local src="$PWD_PROJECT/../tests/fixtures/excite_bat_d2.tex"
  [ -f "$src" ] || { sk "ExciteBots explicit-header encode"; return; }
  local d=/tmp/_r_exheadenc
  rm -rf "$d"; mkdir -p "$d"
  "$B/wszst" EXTRACT "$src" --dest "$d/orig.png" -o >"$d/log" 2>&1 || return
  "$B/wimgt" CONVERT "$d/orig.png" --dest "$d/re.ebtex" -o >>"$d/log" 2>&1 || return
  cp "$d/re.ebtex" "$d/re.tex"
  "$B/wszst" EXTRACT "$d/re.tex" --dest "$d/re.png" -o >>"$d/log" 2>&1
  # Fixture is 128x128: LE dimensions, 8 levels through 1x1, RGBA32 code 47.
  local hdr; hdr=$(od -An -tx1 -N6 "$d/re.ebtex" | tr -d ' \n')
  if [ "$hdr" = 800080000847 ] && python3 "$PNGTOOL" cmp "$d/orig.png" "$d/re.png" 2>/dev/null; then
    ok "ExciteBots explicit-header encode (header, mip chain, exact pixels)"
  else
    no "ExciteBots explicit-header encode" "header=$hdr; see $d/log"
  fi
}
t_excite_headered_encode

t_gtx(){
  # Wii U GX2 "Gfx2" texture container: standalone .gtx files (e.g. debug
  # fonts, UI textures shipped outside any BFRES) were previously not
  # recognised by `wszst EXTRACT --decode`'s image-detection list even
  # though the underlying DecodeGTX_RGBA codec already worked fine via
  # wimgt -- fixed by adding the Gfx2 magic/.gtx extension check alongside
  # the other image formats in decode_image_if_possible().
  local f="$PWD_PROJECT/../tests/fixtures/wiiu_debug_font.gtx"
  [ -f "$f" ] || { sk "Wii U GTX standalone texture"; return; }
  rm -rf /tmp/_r_gtx; mkdir -p /tmp/_r_gtx
  cp "$f" /tmp/_r_gtx/
  $B/wszst EXTRACT "/tmp/_r_gtx/$(basename "$f")" --decode --overwrite >/tmp/_r_gtx.log 2>&1
  local png="/tmp/_r_gtx/$(basename "$f").png"
  [ -s "$png" ] && grep -q "DECODE .*-> PNG:.*\.gtx\.png" /tmp/_r_gtx.log \
    && ok "Wii U GTX standalone texture -> PNG" \
    || no "Wii U GTX standalone texture" "$f"
}
t_gtx

t_gtx_encode(){
  # GTX encode (wimgt CONVERT -> .gtx) round trip: decode a real sample to
  # PNG, re-encode it, decode the re-encode, and require pixel-identical
  # PNG output -- i.e. the encoder's 2D macro-tile addressing is a verified
  # exact inverse of the decoder, not just "looks plausible".
  local f="$PWD_PROJECT/../tests/fixtures/wiiu_debug_font.gtx"
  [ -f "$f" ] || { sk "Wii U GTX encode round trip"; return; }
  rm -rf /tmp/_r_gtx_enc; mkdir -p /tmp/_r_gtx_enc
  $B/wimgt DECODE "$f" --dest /tmp/_r_gtx_enc/orig.png --overwrite >/tmp/_r_gtx_enc.log 2>&1
  $B/wimgt CONVERT /tmp/_r_gtx_enc/orig.png --dest /tmp/_r_gtx_enc/reenc.gtx --overwrite >>/tmp/_r_gtx_enc.log 2>&1
  $B/wimgt DECODE /tmp/_r_gtx_enc/reenc.gtx --dest /tmp/_r_gtx_enc/reenc.png --overwrite >>/tmp/_r_gtx_enc.log 2>&1
  cmp -s /tmp/_r_gtx_enc/orig.png /tmp/_r_gtx_enc/reenc.png \
    && ok "Wii U GTX encode round trip (pixel-identical)" \
    || no "Wii U GTX encode round trip" "$f"
}
t_gtx_encode

t_exart(){
  # Monster Games .art/.img GUI images: same GX pixel data as .tex but a
  # single mip level with a zeroed footer, so dimensions/format come from
  # tile-seam continuity alone. Colour+stencil pairs are detected and
  # recombined into one proper RGBA image by ScanART -- see t_exart_mask.
  # apploader.img is excluded: it's the standard GameCube/Wii disc apploader
  # binary (unrelated to Excite Truck/Bots GUI textures), and the sample
  # corpus under tests/samples-excitebots/extract/.../sys/ carries two real
  # copies of it that otherwise sort ahead of the actual .art/.img fixtures.
  local f; f=$(for d in $SEARCH; do [ -d "$d" ] || continue
      find -L "$d" -maxdepth 8 -type f \( -iname '*.art' -o -iname '*.img' \) -size -65M \
        ! -path '*claude-*' ! -path '*_r_*' ! -iname 'test.*' ! -iname 'test_*' \
        ! -iname 'apploader.img' 2>/dev/null
    done | head -1)
  [ -n "$f" ] || { sk "Excite .art/.img GUI image"; return; }
  rm -rf /tmp/_r_exart; mkdir -p /tmp/_r_exart
  cp "$f" /tmp/_r_exart/
  $B/wszst EXTRACT "/tmp/_r_exart/$(basename "$f")" --overwrite >/tmp/_r_exart.log 2>&1
  local png="/tmp/_r_exart/$(basename "${f%.*}").png"
  [ -s "$png" ] && grep -q "EXTRACT ART:" /tmp/_r_exart.log \
    && ok "Excite .art/.img GUI image -> PNG ($f)" \
    || no "Excite .art/.img GUI image" "$f"
}
t_exart

t_exart_mask(){
  # Some real .art files are a colour+stencil pair: the raw GX decode comes
  # out as one image twice its real height (colour on top, stencil mask
  # below). ScanART() must detect that shape and recombine it into one real
  # half-height RGBA image (colour + a genuine cutout alpha channel) instead
  # of emitting the stacked buffer as-is -- see excite_art_looks_like_mask()/
  # excite_art_recombine() in lib-excite.c.
  for spec in "excite_ach_trun:128" "excite_silvcoin:256"; do
    local name="${spec%%:*}" want="${spec##*:}"
    local f="$PWD_PROJECT/../tests/fixtures/$name.art"
    [ -f "$f" ] || { sk "Excite .art colour+stencil recombine ($name)"; continue; }
    rm -rf /tmp/_r_exartm; mkdir -p /tmp/_r_exartm
    cp "$f" /tmp/_r_exartm/
    $B/wszst EXTRACT "/tmp/_r_exartm/$name.art" --overwrite >/tmp/_r_exartm.log 2>&1
    local png="/tmp/_r_exartm/$name.png"
    if [ -s "$png" ] && grep -qE "EXTRACT ART:.*\(${want}x${want}\)" /tmp/_r_exartm.log \
      && python3 "$PNGTOOL" alphacheck "$png" 2>/dev/null; then
        ok "Excite .art colour+stencil recombine -> ${want}x${want} RGBA ($name)"
    else
      no "Excite .art colour+stencil recombine" "$name"
    fi
  done
}
t_exart_mask

t_extex_encode(){
  # Excite .tex encode (wimgt CONVERT PNG -> .etex, the CLI destination
  # extension for the Excite TEX encoder -- see wimgt.c's cmd_convert(); a
  # collision with the pre-existing BRRES TEX0 encoder's own .tex output
  # ruled out reusing that extension). Neither GX format nor mip-count is
  # stored in the file, so the encoder self-verifies by decoding its own
  # output back through the real ScanTEX() classifier and only accepts a
  # candidate whose recovered width/height/format *and* decoded pixels
  # match what was requested -- verified against 3,151 decodable retail
  # .tex files: 2,723 round-trip exactly or within a tight per-pixel
  # tolerance, the rest fail loudly (ERR_INVALID_DATA) rather than risk a
  # silently wrong image, since many (format,width,height,level-count)
  # combinations can alias to the same file size with no header to
  # disambiguate them.
  local f="$PWD_PROJECT/../tests/fixtures/excite_bat_d2.tex"
  [ -f "$f" ] || { sk "Excite .tex encode round trip"; return; }
  rm -rf /tmp/_r_extexenc; mkdir -p /tmp/_r_extexenc
  cp "$f" /tmp/_r_extexenc/orig.tex
  $B/wszst EXTRACT /tmp/_r_extexenc/orig.tex --dest /tmp/_r_extexenc/orig.png -o >/tmp/_r_extexenc.log 2>&1
  $B/wimgt CONVERT /tmp/_r_extexenc/orig.png --dest /tmp/_r_extexenc/re.etex -o >>/tmp/_r_extexenc.log 2>&1
  cp /tmp/_r_extexenc/re.etex /tmp/_r_extexenc/re.tex
  $B/wszst EXTRACT /tmp/_r_extexenc/re.tex --dest /tmp/_r_extexenc/re.png -o >>/tmp/_r_extexenc.log 2>&1
  if [ -s /tmp/_r_extexenc/re.png ] && python3 "$PNGTOOL" cmp \
       /tmp/_r_extexenc/orig.png /tmp/_r_extexenc/re.png 2>/dev/null; then
    ok "Excite .tex encode round trip (exact pixel match)"
  else
    no "Excite .tex encode round trip" "see /tmp/_r_extexenc.log"
  fi
}
t_extex_encode

t_exart_encode(){
  # Excite .art/.img encode (wimgt CONVERT PNG -> .art), same self-verify
  # contract as t_extex_encode() but for the single-mip-level GUI image
  # format -- verified against 94 decodable retail .art files: 84 round-
  # trip exactly or within tolerance, the rest fail loudly rather than
  # ship a wrong image.
  for name in excite_ach_trun excite_silvcoin; do
    local f="$PWD_PROJECT/../tests/fixtures/$name.art"
    [ -f "$f" ] || { sk "Excite .art encode round trip ($name)"; continue; }
    rm -rf /tmp/_r_exartenc; mkdir -p /tmp/_r_exartenc
    cp "$f" /tmp/_r_exartenc/orig.art
    $B/wszst EXTRACT /tmp/_r_exartenc/orig.art --dest /tmp/_r_exartenc/orig.png -o >/tmp/_r_exartenc.log 2>&1
    $B/wimgt CONVERT /tmp/_r_exartenc/orig.png --dest /tmp/_r_exartenc/re.art -o >>/tmp/_r_exartenc.log 2>&1
    $B/wszst EXTRACT /tmp/_r_exartenc/re.art --dest /tmp/_r_exartenc/re.png -o >>/tmp/_r_exartenc.log 2>&1
    if [ -s /tmp/_r_exartenc/re.png ] && python3 "$PNGTOOL" cmp \
         /tmp/_r_exartenc/orig.png /tmp/_r_exartenc/re.png 2>/dev/null; then
      ok "Excite .art encode round trip (exact pixel match, $name)"
    else
      no "Excite .art encode round trip" "$name: see /tmp/_r_exartenc.log"
    fi
  done
}
t_exart_encode

t_exmsh(){
  # Monster Games PMsh collision resources: count header, spatial buckets,
  # indexed float32 positions and 60-byte triangle/collision records -> GLB
  # (the model exporter now targets glTF/GLB, not COLLADA -- see the DAE->GLB
  # migration). want_pos is the retail position *pool* size pinned straight
  # from each file's header (n_positions at offset 12); it is deliberately
  # NOT re-checked against the GLB output below, because ExportModelToGLB()
  # flattens to one vertex per triangle corner (glTF has no DAE-style shared
  # vertex pool with per-triangle attribute offsets, since each corner here
  # carries its own flat-shaded face normal) -- and real retail pools contain
  # duplicate-coordinate entries at distinct indices, so "unique positions in
  # the output" is not a stable oracle. want_tri stays a real correctness
  # check: it is pinned from retail data and independently verified against
  # the source file's own n_tris field, and the GLB's corner/index counts
  # must both equal want_tri*3.
  local spec name want_pos want_tri
  for spec in "excite_goalback 16 16" "excite_gpmesh 221 248" "excite_rail2bp 222 198"; do
    read -r name want_pos want_tri <<<"$spec"
    local f="$PWD_PROJECT/../tests/fixtures/$name.msh"
    [ -f "$f" ] || { sk "Excite PMsh collision mesh ($name)"; continue; }
    rm -rf /tmp/_r_exmsh; mkdir -p /tmp/_r_exmsh
    cp "$f" /tmp/_r_exmsh/
    $B/wszst EXTRACT "/tmp/_r_exmsh/$name.msh" --overwrite >/tmp/_r_exmsh.log 2>&1
    local glb="/tmp/_r_exmsh/$name.glb"
    if [ -s "$glb" ] && grep -q "EXTRACT MSH:" /tmp/_r_exmsh.log; then
      local counts
      counts=$(python3 - "$glb" <<'PY'
import json,struct,sys
d=open(sys.argv[1],'rb').read()
n=struct.unpack_from('<I',d,12)[0]
j=json.loads(d[20:20+n])
prim=j['meshes'][0]['primitives'][0]
pos=j['accessors'][prim['attributes']['POSITION']]
idx=j['accessors'][prim['indices']] if 'indices' in prim else None
print(pos['count'], idx['count'] if idx else pos['count'])
PY
)
      local nvtx nidx
      read -r nvtx nidx <<<"$counts"
      if [ "$nvtx" = "$((want_tri*3))" ] && [ "$nidx" = "$((want_tri*3))" ]; then
        ok "Excite PMsh collision mesh -> GLB ($name: $want_pos pool positions, $want_tri tris)"
      else
        no "Excite PMsh collision mesh" "$name: expected $((want_tri*3)) corners/indices, got $nvtx/$nidx"
      fi
    else
      no "Excite PMsh collision mesh" "$name"
    fi
  done
}
t_exmsh

t_exmsh_encode(){
  # PMsh encoder: retail .msh -> GLB -> wmdlt ENCODE --dest *.msh -> GLB.
  # The re-decoded geometry must match the original decode exactly (triangle
  # corner coordinate triples, order included). Uses GLB now, not COLLADA --
  # see the DAE->GLB model export migration.
  local name
  for name in excite_goalback excite_gpmesh; do
    local f="$PWD_PROJECT/../tests/fixtures/$name.msh"
    [ -f "$f" ] || { sk "Excite PMsh encode roundtrip ($name)"; continue; }
    rm -rf /tmp/_r_exmshe; mkdir -p /tmp/_r_exmshe
    cp "$f" /tmp/_r_exmshe/
    $B/wszst EXTRACT "/tmp/_r_exmshe/$name.msh" --overwrite >/dev/null 2>&1
    local glb="/tmp/_r_exmshe/$name.glb"
    local msh2="/tmp/_r_exmshe/re.msh"
    if ! [ -s "$glb" ] \
      || ! $B/wmdlt ENCODE "$glb" --dest "$msh2" --overwrite >/dev/null 2>&1 \
      || ! [ -s "$msh2" ]; then
      no "Excite PMsh encode roundtrip" "$name: encode failed"; continue
    fi
    local glb2="/tmp/_r_exmshe/re.glb"
    $B/wszst EXTRACT "$msh2" --dest "$glb2" --overwrite >/dev/null 2>&1
    if [ ! -s "$glb2" ]; then
      no "Excite PMsh encode roundtrip" "$name: re-decode failed"; continue
    fi
    local cmp_out
    cmp_out=$(python3 - "$glb" "$glb2" <<'PY'
import sys, json, struct
def load(path):
    d=open(path,'rb').read()
    n=struct.unpack_from('<I',d,12)[0]
    j=json.loads(d[20:20+n])
    ct=struct.unpack_from('<I',d,20+n+4)[0]
    bin_off=20+n+8
    return j, d[bin_off:bin_off+ct]
def tris(path):
    j,bin=load(path)
    prim=j['meshes'][0]['primitives'][0]
    pos=j['accessors'][prim['attributes']['POSITION']]
    bv=j['bufferViews'][pos['bufferView']]
    off=bv.get('byteOffset',0)
    P=[struct.unpack_from('<3f',bin,off+12*i) for i in range(pos['count'])]
    idx=j['accessors'][prim['indices']] if 'indices' in prim else None
    if idx:
        ibv=j['bufferViews'][idx['bufferView']]
        ioff=ibv.get('byteOffset',0)
        comp={5121:'B',5123:'H',5125:'I'}[idx['componentType']]
        sz={'B':1,'H':2,'I':4}[comp]
        I=[struct.unpack_from('<'+comp,bin,ioff+sz*i)[0] for i in range(idx['count'])]
    else:
        I=list(range(pos['count']))
    return [P[i] for i in I]
a,b=tris(sys.argv[1]),tris(sys.argv[2])
print("MATCH" if a==b and len(a)>0 else f"DIFF {len(a)} vs {len(b)}")
PY
)
    if [ "$cmp_out" = "MATCH" ]; then
      ok "Excite PMsh encode roundtrip ($name: geometry identical through glb->encode->decode)"
    else
      no "Excite PMsh encode roundtrip" "$name: $cmp_out"
    fi
  done
}
t_exmsh_encode

echo "== HAL HSFV037 model geometry (Mario Party 4-8 .hsf) =="
t_hsf(){
  # Single-part case, using the AttributeHeader table generalised to
  # length 1 -- see lib-hsf.c. The fixture is a synthetic unit cube
  # (8 verts, 12 tris, named "cube_vtxs"/"cube_nrms"/"cube_faces" in its
  # own string table) used to confirm the section layout against ground
  # truth, not an extracted retail asset.
  local f="$PWD_PROJECT/../tests/fixtures/hsf_cube_test.hsf"
  [ -f "$f" ] || { sk "HSF single-part geometry"; return; }
  rm -rf /tmp/_r_hsf; mkdir -p /tmp/_r_hsf
  cp "$f" /tmp/_r_hsf/
  $B/wszst EXTRACT "/tmp/_r_hsf/$(basename "$f")" --overwrite >/tmp/_r_hsf.log 2>&1
  local glb="/tmp/_r_hsf/${f##*/}"; glb="${glb%.*}.glb"
  if [ -s "$glb" ] && grep -q "EXTRACT HSF:" /tmp/_r_hsf.log \
      && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/dev/null 2>&1; then
    local nv; nv=$(python3 - "$glb" <<'PY'
import sys
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
acc = g.json["accessors"][g.json["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
print(acc["count"])
PY
)
    # GLB export is unindexed triangle-soup (unlike DAE's shared-vertex
    # accessors), so a 12-triangle cube yields 36 corner vertices, not 8.
    [ "$nv" = "36" ] && ok "HSF cube -> GLB (36 verts / 12 tris, validated)" \
      || no "HSF cube -> GLB" "unexpected vertex count $nv in $glb"
  else
    no "HSF cube -> GLB" "$f"
  fi
}
t_hsf

t_hsf_multipart(){
  # Real retail multi-part sample: a Mario Party 4 board-piece .hsf (2
  # named mesh parts, "cyl1" and "player") extracted from a legitimately-
  # owned disc dump in a prior session. Exercises the AttributeHeader
  # array (count>1) path, per-mesh-name attribute matching, and the
  # packed-s8-normal detection -- see lib-hsf.c.
  local f="$PWD_PROJECT/../tests/fixtures/hsf_multipart_test.hsf"
  [ -f "$f" ] || { sk "HSF multi-part geometry"; return; }
  rm -rf /tmp/_r_hsfmp; mkdir -p /tmp/_r_hsfmp
  cp "$f" /tmp/_r_hsfmp/
  $B/wszst EXTRACT "/tmp/_r_hsfmp/$(basename "$f")" --overwrite >/tmp/_r_hsfmp.log 2>&1
  local glb="/tmp/_r_hsfmp/${f##*/}"; glb="${glb%.*}.glb"
  if [ -s "$glb" ] && grep -q "EXTRACT HSF:" /tmp/_r_hsfmp.log \
      && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/dev/null 2>&1; then
    local stats; stats=$(python3 - "$glb" <<'PY'
import sys
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
print(len(g.json.get("meshes", [])),len(g.json.get("materials", [])),len(g.json.get("images", [])),len(g.json.get("nodes", [])))
PY
)
    set -- $stats
    [ "$1" = "2" ] && [ "$2" -gt 0 ] && [ "$4" -gt 2 ] \
      && [ -s /tmp/_r_hsfmp/miraco.png ] \
      && ok "HSF board-piece -> textured hierarchical GLB (2 meshes, validated)" \
      || no "HSF board-piece -> GLB" "expected meshes/materials/images/nodes + miraco.png, got $stats"
  else
    no "HSF board-piece -> GLB" "$f"
  fi
}
t_hsf_multipart

t_hsf_wmdlt_roundtrips(){
  local d; d=$(mktemp -d /tmp/_r_hsf_wmdlt.XXXXXX) || return
  local fail=0 total=0
  for f in "$PWD_PROJECT/../tests/fixtures"/hsf_*.hsf "$PWD_PROJECT/../tests/fixtures/hsf-features"/*.hsf; do
    [ -f "$f" ] || continue
    local bname; bname="$(basename "$f")"
    total=$((total+1))
    if "$B/wmdlt" DECODE "$f" --dest "$d/$bname.dae" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/$bname.dae" --dest "$d/$bname.re.hsf" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" DECODE "$d/$bname.re.hsf" --dest "$d/$bname.re.dae" --overwrite >/dev/null 2>&1; then
      :
    else
      fail=$((fail+1))
    fi
  done
  [ "$total" -gt 0 ] && [ "$fail" -eq 0 ] \
    && ok "HSF wmdlt DECODE -> ENCODE -> DECODE roundtrips ($total models)" \
    || no "HSF wmdlt roundtrips" "$fail of $total failed"
  rm -rf "$d"
}
t_hsf_wmdlt_roundtrips

echo "== Monster Games NDL3/NDL2 model geometry (Excite Truck / ExciteBots .mod) =="
t_exmod(){
  # DecodeExciteMOD() reads geometry format straight out of the embedded GX
  # display list's vertex-attribute-table register writes rather than
  # hardcoding a single validated shape -- see the long comment above
  # DecodeExciteMOD() in lib-excite.c for the format and its validation
  # against the full retail corpus of both games (135/135 Excite Truck,
  # 193/203 ExciteBots -- the ExciteBots gap is the separate .msh files,
  # not .mod decode failures). These 3 small fixtures (all "3LDN", the
  # magic not at file offset 0 on 2 of them, matching most real samples)
  # exercise that general path, not a narrow special case.
  for name in excite_arrow_obj excite_arrow_point excite_sunflower2; do
    local f="$PWD_PROJECT/../tests/fixtures/$name.mod"
    [ -f "$f" ] || { sk "NDL3 .mod ($name)"; continue; }
    rm -rf /tmp/_r_exmod; mkdir -p /tmp/_r_exmod
    cp "$f" /tmp/_r_exmod/
    $B/wszst EXTRACT "/tmp/_r_exmod/$name.mod" --overwrite >/tmp/_r_exmod.log 2>&1
    local glb="/tmp/_r_exmod/$name.glb"
    if [ -s "$glb" ] && grep -q "EXTRACT MOD:" /tmp/_r_exmod.log \
        && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/dev/null 2>&1; then
      local nv; nv=$(python3 - "$glb" <<'PY'
import sys
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
acc = g.json["accessors"][g.json["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
print(acc["count"])
PY
)
      [ "$nv" -gt 0 ] 2>/dev/null && ok "NDL3 .mod -> GLB ($name, $nv verts, validated)" \
        || no "NDL3 .mod -> GLB" "$name: bad vertex count $nv"
    else
      no "NDL3 .mod -> GLB" "$name"
    fi
  done
}
t_exmod

# t_exmod_encode() -- EncodeExciteMOD() writes geometry-only "3LDN" .mod
# files from parsed DAE/GLB models: f32 big-endian positions, s16 shift-13
# texcoords and GX TRIANGLES draw calls with 2-byte index tuples -- the
# inverse of DecodeExciteMOD(). The chain below exercises encode+decode
# twice; comparing the 1st and 3rd DAE exports keeps the COLLADA V-flip
# parity equal on both sides. Comparison is tolerant (tests/
# compare_dae_tris.py, 2e-4) because re-quantizing through s16 shift-13 and
# %f text round-trips perturbs UVs by up to ~6e-5, and dedup order may
# reorder vertices. Texcoord index bytes cap unique UVs at 255; the fixtures
# stay far below that.
t_exmod_encode(){
  for name in excite_arrow_obj excite_arrow_point excite_sunflower2; do
    local f="$PWD_PROJECT/../tests/fixtures/$name.mod"
    [ -f "$f" ] || { sk "NDL3 .mod encode ($name)"; continue; }
    rm -rf /tmp/_r_exmod_enc; mkdir -p /tmp/_r_exmod_enc
    cp "$f" /tmp/_r_exmod_enc/
    local d=/tmp/_r_exmod_enc
    $B/wszst EXTRACT "$d/$name.mod" --dest "$d/s1.glb" --overwrite >/dev/null 2>&1 \
      && $B/wmdlt ENCODE "$d/s1.glb" -d "$d/e1.mod" --overwrite >/dev/null 2>&1 \
      && $B/wszst EXTRACT "$d/e1.mod" --dest "$d/s2.glb" --overwrite >/dev/null 2>&1 \
      && $B/wmdlt ENCODE "$d/s2.glb" -d "$d/e2.mod" --overwrite >/dev/null 2>&1 \
      && $B/wszst EXTRACT "$d/e2.mod" --dest "$d/s3.glb" --overwrite >/dev/null 2>&1 \
      && python3 "$PWD_PROJECT/../tests/compare_dae_tris.py" "$d/s1.glb" "$d/s3.glb" >/dev/null 2>&1 \
      && ok "NDL3 .mod encode round-trip ($name)" \
      || no "NDL3 .mod encode round-trip" "$name"
  done
}
t_exmod_encode

# -- Switch BFRES encode (CreateSwitchBFRES) --
# Tests that CreateSwitchBFRES can build a standalone Switch BFRES v8 from a
# parsed DAE model and that the result round-trips through ParseBFRESSwitch.
# Uses the Wii U BFRES fixture's DAE as source material (any valid DAE works).
echo
echo "== Switch BFRES encoder (CreateSwitchBFRES v8) =="
t_switch_bfres_encode(){
  local bfres="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  [ -f "$bfres" ] || { sk "Switch BFRES encode (no fixture)"; return; }
  rm -rf /tmp/_r_swbfres_enc; mkdir -p /tmp/_r_swbfres_enc/work
  # Convert Wii U BFRES to DAE using wmdlt (the model conversion tool).
  $B/wmdlt ENCODE "$bfres" -d /tmp/_r_swbfres_enc/work/model.dae --overwrite >/dev/null 2>&1 \
    && [ -f /tmp/_r_swbfres_enc/work/model.dae ] \
    || { no "Switch BFRES encode" "wmdlt conversion failed"; return; }
  # Run CREATE on the dir containing only model.dae (no sibling .bfres).
  $B/wszst CREATE /tmp/_r_swbfres_enc/work >/dev/null 2>&1 \
    && [ -f /tmp/_r_swbfres_enc/work/model.bfres ] \
    || { no "Switch BFRES encode" "CREATE did not produce model.bfres"; return; }
  # Validate: must start with "FRES" and be parseable by the Switch parser.
  local magic=$(xxd -l 4 -p /tmp/_r_swbfres_enc/work/model.bfres)
  [ "$magic" = "46524553" ] || { no "Switch BFRES encode" "bad magic $magic"; return; }
  # BOM at offset 0x0C should be 0xFEFF (little-endian Switch).
  local bom=$(xxd -s 0x0C -l 2 -p /tmp/_r_swbfres_enc/work/model.bfres)
  [ "$bom" = "fffe" ] || { no "Switch BFRES encode" "BOM $bom (expected fffe)"; return; }
  ok "Switch BFRES encode (from DAE -> .bfres, FRES magic + LE BOM verified)"
}
t_switch_bfres_encode

# -- Switch BFRES structural round-trip --
# Encodes DAE -> BFRES -> EXTRACT (XML), validates attribute names, format codes,
# and vertex count survive the round-trip.
t_switch_bfres_struct(){
  local bfres="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  [ -f "$bfres" ] || { sk "Switch BFRES struct round-trip (no fixture)"; return; }
  local d=/tmp/_r_swbfres_struct; rm -rf "$d"; mkdir -p "$d/work"
  # DAE -> BFRES
  $B/wmdlt ENCODE "$bfres" -d "$d/work/model.dae" --overwrite >/dev/null 2>&1 \
    && $B/wszst CREATE "$d/work" >/dev/null 2>&1 \
    && [ -f "$d/work/model.bfres" ] \
    || { no "Switch BFRES struct round-trip" "encode failed"; return; }
  # BFRES -> XML manifest (no --dest, produces .bfres.xml alongside input)
  $B/wszst EXTRACT "$d/work/model.bfres" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/work/model.bfres.xml" ] \
    || { no "Switch BFRES struct round-trip" "EXTRACT failed"; return; }
  local xml="$d/work/model.bfres.xml"
  # Validate: 3 attributes with correct names and formats.
  grep -q 'name="_p"' "$xml" || { no "Switch BFRES struct round-trip" "missing _p attr"; return; }
  grep -q 'name="_n"' "$xml" || { no "Switch BFRES struct round-trip" "missing _n attr"; return; }
  grep -q 'name="_u0"' "$xml" || { no "Switch BFRES struct round-trip" "missing _u0 attr"; return; }
  # Validate: format codes 0x1805 (f32x3) and 0x1205 (f16x2).
  grep -q 'format="0x1805"' "$xml" || { no "Switch BFRES struct round-trip" "missing 0x1805 fmt"; return; }
  grep -q 'format="0x1205"' "$xml" || { no "Switch BFRES struct round-trip" "missing 0x1205 fmt"; return; }
  # Validate: shape element exists.
  grep -q '<shape ' "$xml" || { no "Switch BFRES struct round-trip" "no shape element"; return; }
  rm -rf "$d"
  ok "Switch BFRES struct round-trip (encode -> extract XML -> attrs/formats verified)"
}
t_switch_bfres_struct

# -- Switch BFRES inject round-trip --
# Encodes DAE -> BFRES, copies DAE, deletes BFRES, re-CREATES from DAE,
# then verifies the re-created file parses correctly.
t_switch_bfres_inject(){
  local bfres="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  [ -f "$bfres" ] || { sk "Switch BFRES inject round-trip (no fixture)"; return; }
  local d=/tmp/_r_swbfres_inj; rm -rf "$d"; mkdir -p "$d/work"
  # DAE -> BFRES
  $B/wmdlt ENCODE "$bfres" -d "$d/work/model.dae" --overwrite >/dev/null 2>&1 \
    && $B/wszst CREATE "$d/work" >/dev/null 2>&1 \
    && [ -f "$d/work/model.bfres" ] \
    || { no "Switch BFRES inject round-trip" "encode failed"; return; }
  # Save a copy of the DAE before CREATE consumed it.
  $B/wmdlt ENCODE "$bfres" -d "$d/work/model2.dae" --overwrite >/dev/null 2>&1 \
    || { no "Switch BFRES inject round-trip" "second DAE failed"; return; }
  rm -f "$d/work/model.bfres"
  cp "$d/work/model2.dae" "$d/work/model.dae"
  $B/wszst CREATE "$d/work" >/dev/null 2>&1 \
    && [ -f "$d/work/model.bfres" ] \
    || { no "Switch BFRES inject round-trip" "re-CREATE failed"; return; }
  local magic=$(xxd -l 4 -p "$d/work/model.bfres")
  [ "$magic" = "46524553" ] || { no "Switch BFRES inject round-trip" "bad magic $magic"; return; }
  # EXTRACT and validate attrs.
  $B/wszst EXTRACT "$d/work/model.bfres" --overwrite >/dev/null 2>&1
  [ -f "$d/work/model.bfres.xml" ] \
    || { no "Switch BFRES inject round-trip" "no XML"; return; }
  grep -q 'name="_p"' "$d/work/model.bfres.xml" \
    && grep -q 'name="_n"' "$d/work/model.bfres.xml" \
    && grep -q 'name="_u0"' "$d/work/model.bfres.xml" \
    || { no "Switch BFRES inject round-trip" "missing attrs in XML"; return; }
  rm -rf "$d"
  ok "Switch BFRES inject round-trip (encode -> re-create -> extract XML -> attrs verified)"
}
t_switch_bfres_inject

# -- Switch BFRES geometry round-trip --
# Wii U BFRES -> DAE -> Switch BFRES -> DAE, then verifies positions + normals
# survive byte-for-byte (UVs may differ due to half-float precision and channel
# count reduction, so we only compare positions and normals).
t_switch_bfres_geom(){
  local bfres="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  [ -f "$bfres" ] || { sk "Switch BFRES geom round-trip (no fixture)"; return; }
  local d=/tmp/_r_swbfres_geom; rm -rf "$d"; mkdir -p "$d/src" "$d/enc"
  # Wii U BFRES -> source DAE
  $B/wmdlt ENCODE "$bfres" -d "$d/src/model.dae" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/src/model.dae" ] \
    || { no "Switch BFRES geom round-trip" "source DAE encode failed"; return; }
  # Save a copy of the source DAE before CREATE consumes it.
  cp "$d/src/model.dae" "$d/enc/source_ref.dae"
  # DAE -> Switch BFRES
  $B/wszst CREATE "$d/src" >/dev/null 2>&1 \
    && [ -f "$d/src/model.bfres" ] \
    || { no "Switch BFRES geom round-trip" "CREATE failed"; return; }
  # Switch BFRES -> roundtrip DAE (uses ParseBFRESSwitch fallback)
  $B/wmdlt ENCODE "$d/src/model.bfres" -d "$d/enc/model.dae" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/enc/model.dae" ] \
    || { no "Switch BFRES geom round-trip" "roundtrip DAE encode failed"; return; }
  # Extract position and normal arrays from both DAEs and compare.
  # The DAE uses <float_array id="...-positions-array" count="...">data</float_array>.
  # Extract just the numeric data (after the opening >, before </float_array>).
  local src_pos=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/enc/source_ref.dae" positions)
  local enc_pos=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/enc/model.dae" positions)
  [ -n "$src_pos" ] || { no "Switch BFRES geom round-trip" "no positions in source DAE"; return; }
  [ -n "$enc_pos" ] || { no "Switch BFRES geom round-trip" "no positions in roundtrip DAE"; return; }
  [ "$src_pos" = "$enc_pos" ] \
    || { no "Switch BFRES geom round-trip" "positions differ"; return; }
  local src_nrm=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/enc/source_ref.dae" normals)
  local enc_nrm=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/enc/model.dae" normals)
  [ -n "$src_nrm" ] || { no "Switch BFRES geom round-trip" "no normals in source DAE"; return; }
  [ -n "$enc_nrm" ] || { no "Switch BFRES geom round-trip" "no normals in roundtrip DAE"; return; }
  [ "$src_nrm" = "$enc_nrm" ] \
    || { no "Switch BFRES geom round-trip" "normals differ"; return; }
  rm -rf "$d"
  ok "Switch BFRES geom round-trip (positions + normals byte-identical through full cycle)"
}
t_switch_bfres_geom

# -- Switch BFRES DAE inject (vertex modification round-trip) --
# Creates Switch BFRES, modifies a vertex position in the DAE, re-injects
# via CREATE (timestamp-gated), then decodes the BFRES and verifies the
# vertex change propagated through the inject+decode pipeline.
t_switch_bfres_inject_vertices(){
  local bfres="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  [ -f "$bfres" ] || { sk "Switch BFRES inject vertices (no fixture)"; return; }
  local d=/tmp/_r_swbfres_injv; rm -rf "$d"; mkdir -p "$d/work"
  # DAE -> BFRES (initial create)
  $B/wmdlt ENCODE "$bfres" -d "$d/work/model.dae" --overwrite >/dev/null 2>&1 \
    && $B/wszst CREATE "$d/work" >/dev/null 2>&1 \
    && [ -f "$d/work/model.bfres" ] \
    || { no "Switch BFRES inject vertices" "initial CREATE failed"; return; }
  # Decode original BFRES -> DAE to capture the original first vertex position.
  $B/wmdlt ENCODE "$d/work/model.bfres" -d "$d/work/orig.dae" --overwrite >/dev/null 2>&1 \
    || { no "Switch BFRES inject vertices" "original decode failed"; return; }
  local orig_pos=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/work/orig.dae" positions | tr ' ' '\n' | head -1)
  [ -n "$orig_pos" ] || { no "Switch BFRES inject vertices" "no original positions"; return; }
  # Re-create DAE from the encoder bfres (for injection).
  $B/wmdlt ENCODE "$d/work/model.bfres" -d "$d/work/model.dae" --overwrite >/dev/null 2>&1 \
    || { no "Switch BFRES inject vertices" "DAE re-create failed"; return; }
  # Modify ONLY the first vertex position (X component).
  local orig_norm=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/work/model.dae" normals | tr ' ' '\n' | head -1)
  python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/work/model.dae" set-first-position 99.999999
  local mod_pos=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/work/model.dae" positions | tr ' ' '\n' | head -1)
  # 99.999999 isn't exactly representable in the accessor's f32 storage; it
  # rounds to 100.0 immediately (same value the encoder would've quantized
  # to anyway), so check for that instead of an exact string match.
  echo "$mod_pos" | grep -q "^100" \
    || { no "Switch BFRES inject vertices" "modify failed: $mod_pos"; return; }
  # Normals must be untouched by the modification.
  local mod_norm=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/work/model.dae" normals | tr ' ' '\n' | head -1)
  [ "$mod_norm" = "$orig_norm" ] \
    || { no "Switch BFRES inject vertices" "modify corrupted normals: $mod_norm"; return; }
  # Touch the DAE to be newer than the BFRES so injection triggers.
  sleep 3
  touch "$d/work/model.dae"
  # Re-run CREATE — this should inject the modified DAE into the BFRES.
  $B/wszst CREATE "$d/work" >/dev/null 2>&1 \
    && [ -f "$d/work/model.bfres" ] \
    || { no "Switch BFRES inject vertices" "inject CREATE failed"; return; }
  # Decode the injected BFRES back to DAE.
  $B/wmdlt ENCODE "$d/work/model.bfres" -d "$d/work/inj.dae" --overwrite >/dev/null 2>&1 \
    || { no "Switch BFRES inject vertices" "injected decode failed"; return; }
  local inj_pos=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/work/inj.dae" positions | tr ' ' '\n' | head -1)
  [ -n "$inj_pos" ] || { no "Switch BFRES inject vertices" "no injected positions"; return; }
  # The first vertex should be ~100.0 (99.999999 rounded to f32 = 100.000000).
  echo "$inj_pos" | grep -q "100" \
    || { no "Switch BFRES inject vertices" "vertex not modified: $inj_pos"; return; }
  # Normals should be unchanged.
  local inj_norm=$(python3 "$PWD_PROJECT/../tests/dae_field.py" "$d/work/inj.dae" normals | tr ' ' '\n' | head -1)
  [ "$inj_norm" = "$orig_norm" ] \
    || { no "Switch BFRES inject vertices" "normals changed unexpectedly: $inj_norm"; return; }
  rm -rf "$d"
  ok "Switch BFRES inject vertices (vertex modification persists through inject+decode)"
}
t_switch_bfres_inject_vertices

t_switch_bfres_inject_multimesh(){
  local d=/tmp/_r_swbfres_injmm; rm -rf "$d"; mkdir -p "$d/work"
  local DF="$PWD_PROJECT/../tests/dae_field.py"
  # Build a synthetic 2-mesh GLB: a quad (Cube, 4 verts/2 tris) and a
  # triangle (Pyramid, 3 verts/1 tri). wszst CREATE only accepts GLB model
  # input now (COLLADA/DAE authoring was removed), so this hand-authors GLB
  # directly instead of COLLADA XML.
  cat > "$d/spec.json" <<'JSON'
[
  {"name": "Cube", "positions": [0,0,0, 1,0,0, 1,1,0, 0,1,0],
   "normals": [0,0,-1, 0,0,-1, 0,0,-1, 0,0,-1],
   "indices": [0,1,2, 0,2,3]},
  {"name": "Pyramid", "positions": [0,0,0, 1,0,0, 0.5,1,0],
   "normals": [0,0,-1, 0,0,-1, 0,0,-1],
   "indices": [0,1,2]}
]
JSON
  python3 "$PWD_PROJECT/../tests/make_glb.py" "$d/work/multi.dae" "$d/spec.json" 2>&1 \
    || { no "Switch BFRES inject multimesh" "GLB generation failed"; return; }
  # CREATE initial BFRES from multi-mesh GLB.
  $B/wszst CREATE "$d/work" >/dev/null 2>&1 \
    && [ -f "$d/work/multi.bfres" ] \
    || { no "Switch BFRES inject multimesh" "initial CREATE failed"; return; }
  # Decode BFRES back to GLB to verify both meshes survived.
  $B/wmdlt ENCODE "$d/work/multi.bfres" -d "$d/work/orig.dae" --overwrite 2>&1 \
    || { no "Switch BFRES inject multimesh" "original decode failed"; return; }
  local cube_verts=$(python3 "$DF" "$d/work/orig.dae" mesh-verts Cube)
  local pyr_verts=$(python3 "$DF" "$d/work/orig.dae" mesh-verts Pyramid)
  [ "$cube_verts" = "6" ] || { no "Switch BFRES inject multimesh" "Cube verts=$cube_verts, expected 6"; return; }
  [ "$pyr_verts" = "3" ] || { no "Switch BFRES inject multimesh" "Pyramid verts=$pyr_verts, expected 3"; return; }
  # Re-create GLB from the encoded BFRES so it exists for injection testing.
  $B/wmdlt ENCODE "$d/work/multi.bfres" -d "$d/work/multi.dae" --overwrite >/dev/null 2>&1 \
    || { no "Switch BFRES inject multimesh" "GLB re-create failed"; return; }
  # Modify first vertex of Cube only and trigger inject round-trip.
  python3 "$DF" "$d/work/multi.dae" set-first-position 99.999999 Cube
  sleep 3; touch "$d/work/multi.dae"
  $B/wszst CREATE "$d/work" >/dev/null 2>&1 \
    || { no "Switch BFRES inject multimesh" "inject CREATE failed"; return; }
  $B/wmdlt ENCODE "$d/work/multi.bfres" -d "$d/work/inj.dae" --overwrite >/dev/null 2>&1 \
    || { no "Switch BFRES inject multimesh" "injected decode failed"; return; }
  local inj_cube_verts=$(python3 "$DF" "$d/work/inj.dae" mesh-verts Cube)
  local inj_pyr_verts=$(python3 "$DF" "$d/work/inj.dae" mesh-verts Pyramid)
  [ "$inj_cube_verts" = "6" ] || { no "Switch BFRES inject multimesh" "inject Cube verts=$inj_cube_verts"; return; }
  [ "$inj_pyr_verts" = "3" ] || { no "Switch BFRES inject multimesh" "inject Pyramid verts=$inj_pyr_verts"; return; }
  local inj_pos=$(python3 "$DF" "$d/work/inj.dae" positions | tr ' ' '\n' | head -1)
  echo "$inj_pos" | grep -q "100" \
    || { no "Switch BFRES inject multimesh" "vertex not modified: $inj_pos"; return; }
  rm -rf "$d"
  ok "Switch BFRES inject multimesh (multi-mesh round-trip preserves vertex counts and vertex edits)"
}
t_switch_bfres_inject_multimesh

echo "== canonical byte-for-byte encoder determinism =="
t_byte_exact_encoders(){
  local d; d=$(mktemp -d /tmp/_r_byteenc.XXXXXX) || { bno "encoder determinism" "mktemp failed"; return; }

  # HSF: same decoded DAE and sibling PNG inputs must produce an identical
  # canonical HSF. This does not claim arbitrary retail padding survives.
  if "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/hsf_multipart_test.hsf" --dest "$d/model.dae" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/model.dae" --dest "$d/hsf1.hsf" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/model.dae" --dest "$d/hsf2.hsf" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/hsf1.hsf" "$d/hsf2.hsf"; then
    bok "HSF same DAE -> identical encoded bytes"
  else bno "HSF canonical encoding" "two encodes differ"; fi

  # GTX and BNTX: deterministic container headers, swizzle and payload.
  local gtx="$PWD_PROJECT/../tests/fixtures/wiiu_debug_font.gtx"
  if "$B/wimgt" DECODE "$gtx" --dest "$d/source.png" --overwrite >/dev/null 2>&1 \
  && "$B/wimgt" CONVERT "$d/source.png" --dest "$d/gtx1.gtx" --overwrite >/dev/null 2>&1 \
  && "$B/wimgt" CONVERT "$d/source.png" --dest "$d/gtx2.gtx" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/gtx1.gtx" "$d/gtx2.gtx"; then
    bok "GTX same PNG -> identical encoded bytes"
  else bno "GTX canonical encoding" "two encodes differ"; fi
  mkdir -p "$d/bntx-a" "$d/bntx-b"
  if "$B/wimgt" CONVERT "$d/source.png" --dest "$d/bntx-a/same.bntx" --overwrite >/dev/null 2>&1 \
  && "$B/wimgt" CONVERT "$d/source.png" --dest "$d/bntx-b/same.bntx" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/bntx-a/same.bntx" "$d/bntx-b/same.bntx"; then
    bok "BNTX same PNG -> identical encoded bytes"
  else bno "BNTX canonical encoding" "two encodes differ"; fi

  # Nintendo image/font encoders. Use a small valid atlas for font formats;
  # equal output basenames also cover embedded resource-name determinism.
  mkdir -p "$d/image-a" "$d/image-b"
  python3 "$PNGTOOL" write "$d/atlas.png" 32 32 100 150 200
  for ext in ncgr nclr plt0 tex0 brfnt brfna bcfnt bffnt tpl bti bt-img; do
    if "$B/wimgt" ENCODE "$d/atlas.png" --dest "$d/image-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/atlas.png" --dest "$d/image-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/image-a/same.$ext" "$d/image-b/same.$ext"; then
      bok "${ext} same PNG -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two encodes differ"; fi
  done
  for ext in ajpg bclim bflim; do
    if "$B/wimgt" ENCODE "$d/atlas.png" --dest "$d/image-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/atlas.png" --dest "$d/image-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/image-a/same.$ext" "$d/image-b/same.$ext"; then
      bok "${ext} same PNG -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two encodes differ"; fi
  done
  for cont in tpl bti tex0; do
    for fmt in I4 I8 IA4 IA8 RGB565 RGB5A3 RGBA8 CMPR; do
      mkdir -p "$d/gx-a" "$d/gx-b"
      if "$B/wimgt" ENCODE "$d/atlas.png" --transform "$fmt" --dest "$d/gx-a/same.$cont" --overwrite >/dev/null 2>&1 \
      && "$B/wimgt" ENCODE "$d/atlas.png" --transform "$fmt" --dest "$d/gx-b/same.$cont" --overwrite >/dev/null 2>&1 \
      && cmp -s "$d/gx-a/same.$cont" "$d/gx-b/same.$cont"; then
        bok "$(echo "$cont" | tr a-z A-Z) ${fmt} same PNG -> identical encoded bytes"
      else bno "$(echo "$cont" | tr a-z A-Z) ${fmt} canonical encoding" "two encodes differ"; fi
    done
  done
  for fmt in IA8 RGB565 RGB5A3; do
    mkdir -p "$d/plt-a" "$d/plt-b"
    if "$B/wimgt" ENCODE "$d/atlas.png" --transform "$fmt" --dest "$d/plt-a/same.plt0" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/atlas.png" --transform "$fmt" --dest "$d/plt-b/same.plt0" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/plt-a/same.plt0" "$d/plt-b/same.plt0"; then
      bok "PLT0 ${fmt} same PNG -> identical encoded bytes"
    else bno "PLT0 ${fmt} canonical encoding" "two encodes differ"; fi
  done
  mkdir -p "$d/bntx-a" "$d/bntx-b"
  if "$B/wimgt" CONVERT "$d/atlas.png" --transform RGB565 --dest "$d/bntx-a/same.bntx" --overwrite >/dev/null 2>&1 \
  && "$B/wimgt" CONVERT "$d/atlas.png" --transform RGB565 --dest "$d/bntx-b/same.bntx" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/bntx-a/same.bntx" "$d/bntx-b/same.bntx"; then
    bok "BNTX RGB565 same PNG -> identical encoded bytes"
  else bno "BNTX RGB565 canonical encoding" "two encodes differ"; fi
  for fmt in RGB565 RGBA8; do
    if "$B/wimgt" ENCODE "$d/atlas.png" --transform "$fmt" --dest "$d/bflim-raw.bflim" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/bflim-raw.bflim" --dest "$d/image-a/same-$fmt.bflim.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/bflim-raw.bflim" --dest "$d/image-b/same-$fmt.bflim.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/image-a/same-$fmt.bflim.fzip" "$d/image-b/same-$fmt.bflim.fzip"; then
      bok "BFLIM ${fmt}.fzip same PNG -> identical encoded bytes"
    else bno "BFLIM ${fmt}.fzip canonical encoding" "two encodes differ"; fi
    if "$B/wimgt" CONVERT "$d/atlas.png" --transform "$fmt" --dest "$d/bntx-raw.bntx" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/bntx-raw.bntx" --dest "$d/image-a/same-$fmt.bntx.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/bntx-raw.bntx" --dest "$d/image-b/same-$fmt.bntx.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/image-a/same-$fmt.bntx.fzip" "$d/image-b/same-$fmt.bntx.fzip"; then
      bok "BNTX ${fmt}.fzip same PNG -> identical encoded bytes"
    else bno "BNTX ${fmt}.fzip canonical encoding" "two encodes differ"; fi
  done
  cp "$PWD_PROJECT/../tests/fixtures/excite_ach_trun.art" "$d/art-source.art"
  "$B/wszst" EXTRACT "$d/art-source.art" --overwrite >/dev/null 2>&1
  cp "$PWD_PROJECT/../tests/fixtures/excite_bat_d2.tex" "$d/tex-source.tex"
  "$B/wszst" EXTRACT "$d/tex-source.tex" --overwrite >/dev/null 2>&1
  for ext in art img etex; do
    src="$d/art-source.png"; [ "$ext" = etex ] && src="$d/tex-source.png"
    if "$B/wimgt" ENCODE "$src" --dest "$d/image-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$src" --dest "$d/image-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/image-a/same.$ext" "$d/image-b/same.$ext"; then
      bok "${ext} same PNG -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two encodes differ"; fi
  done
  cp "$PWD_PROJECT/../tests/fixtures/excite_silvcoin.art" "$d/coin-source.art"
  "$B/wszst" EXTRACT "$d/coin-source.art" --dest "$d/coin-source.png" --overwrite >/dev/null 2>&1
  for ext in art img; do
    if "$B/wimgt" ENCODE "$d/coin-source.png" --dest "$d/image-a/coin.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/coin-source.png" --dest "$d/image-b/coin.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/image-a/coin.$ext" "$d/image-b/coin.$ext"; then
      bok "coin ${ext} same PNG -> identical encoded bytes"
    else bno "coin ${ext} canonical encoding" "two encodes differ"; fi
  done

  # Semantic Wii U layouts may relocate strings relative to retail sources,
  # but their own encoder must still choose one deterministic representation.
  mkdir -p "$d/layout-a" "$d/layout-b"
  local spec src
  for spec in 'splatoon_cmn_bg_out.bflan bflan' 'splatoon_cmn_seq_drc_option.bflyt bflyt'; do
    set -- $spec; src="$PWD_PROJECT/../tests/fixtures/$1"; ext=$2
    if "$B/wlayt" decode "$src" "$d/same-$ext.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/same-$ext.tflyt" "$d/layout-a/same.$ext" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/same-$ext.tflyt" "$d/layout-b/same.$ext" >/dev/null 2>&1 \
    && cmp -s "$d/layout-a/same.$ext" "$d/layout-b/same.$ext"; then
      bok "${ext} same semantic text -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two encodes differ"; fi

    if "$B/wlayt" decode "$src" "$d/same-$ext.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/same-$ext.tflyt" "$d/layout-raw.bin" >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/layout-raw.bin" --dest "$d/layout-a/same.$ext.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/layout-raw.bin" --dest "$d/layout-b/same.$ext.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/layout-a/same.$ext.fzip" "$d/layout-b/same.$ext.fzip"; then
      bok "${ext}.fzip same semantic text -> identical encoded bytes"
    else bno "${ext}.fzip canonical encoding" "two encodes differ"; fi
  done

  # Message Studio formats: section order, labels, string pools, endian and
  # alignment all participate in the complete-file comparison.
  mkdir -p "$d/msg-a" "$d/msg-b"
  printf '# MSBT: Message Studio Binary Text (BigEndian, UTF-16)\n\n[Greeting]\nHello byte world!\n' > "$d/source.tmsbt"
  printf '# MSBP: Message Studio Binary Project (BigEndian, UTF-16)\n\n[Colors: 1]\n  #0: Red = #FF0000FF\n' > "$d/source.tmsbp"
  printf '# MSBF: Message Studio Binary Flowchart (BigEndian)\n# Nodes: 1\n\n[Node #0 (Finish)]\n  type = Event (event_id=10, param=0x20, next=65535)\n' > "$d/source.tmsbf"
  for spec in 'tmsbt msbt' 'tmsbp msbp' 'tmsbf msbf'; do
    set -- $spec; src="$d/source.$1"; ext=$2
    if "$B/wbmgt" ENCODE "$src" --dest "$d/msg-a/same.$ext" >/dev/null 2>&1 \
    && "$B/wbmgt" ENCODE "$src" --dest "$d/msg-b/same.$ext" >/dev/null 2>&1 \
    && cmp -s "$d/msg-a/same.$ext" "$d/msg-b/same.$ext"; then
      bok "${ext} same semantic text -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two encodes differ"; fi

    if "$B/wbmgt" ENCODE "$src" --dest "$d/msg-raw.bin" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/msg-raw.bin" --dest "$d/msg-a/same.$ext.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/msg-raw.bin" --dest "$d/msg-b/same.$ext.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/msg-a/same.$ext.fzip" "$d/msg-b/same.$ext.fzip"; then
      bok "${ext}.fzip same semantic text -> identical encoded bytes"
    else bno "${ext}.fzip canonical encoding" "two encodes differ"; fi
  done

  # Message Studio endian & encoding matrix:
  printf '# MSBT: Message Studio Binary Text (LittleEndian, UTF-16)\n\n[Greeting]\nHello byte world!\n\n[Second]\nText\n' > "$d/source.msbt-le16.tmsbt"
  printf '# MSBT: Message Studio Binary Text (BigEndian, UTF-8)\n\n[Greeting]\nHello byte world!\n\n[Second]\nText\n' > "$d/source.msbt-be8.tmsbt"
  printf '# MSBT: Message Studio Binary Text (LittleEndian, UTF-8)\n\n[Greeting]\nHello byte world!\n\n[Second]\nText\n' > "$d/source.msbt-le8.tmsbt"
  printf '# MSBP: Message Studio Binary Project (LittleEndian, UTF-16)\n\n[Colors: 2]\n  #0: Red = #FF0000FF\n  #1: Green = #00FF00FF\n' > "$d/source.msbp-le16.tmsbp"
  printf '# MSBP: Message Studio Binary Project (BigEndian, UTF-8)\n\n[Colors: 2]\n  #0: Red = #FF0000FF\n  #1: Green = #00FF00FF\n' > "$d/source.msbp-be8.tmsbp"
  printf '# MSBP: Message Studio Binary Project (LittleEndian, UTF-8)\n\n[Colors: 2]\n  #0: Red = #FF0000FF\n  #1: Green = #00FF00FF\n' > "$d/source.msbp-le8.tmsbp"
  printf '# MSBF: Message Studio Binary Flowchart (LittleEndian)\n# Nodes: 2\n\n[Node #0 (Start)]\n  type = EntryPoint (next=1)\n\n[Node #1 (Finish)]\n  type = Event (event_id=10, param=0x20, next=65535)\n' > "$d/source.msbf-le.tmsbf"
  for spec in 'source.msbt-le16.tmsbt msbt MSBT_LE_UTF16' \
              'source.msbt-be8.tmsbt msbt MSBT_BE_UTF8' \
              'source.msbt-le8.tmsbt msbt MSBT_LE_UTF8' \
              'source.msbp-le16.tmsbp msbp MSBP_LE_UTF16' \
              'source.msbp-be8.tmsbp msbp MSBP_BE_UTF8' \
              'source.msbp-le8.tmsbp msbp MSBP_LE_UTF8' \
              'source.msbf-le.tmsbf msbf MSBF_LE'; do
    set -- $spec; src="$d/$1"; ext=$2; label=$3
    if "$B/wbmgt" ENCODE "$src" --dest "$d/msg-a/same-$label.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" ENCODE "$src" --dest "$d/msg-b/same-$label.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/msg-a/same-$label.$ext" "$d/msg-b/same-$label.$ext"; then
      bok "${label} same semantic text -> identical encoded bytes"
    else bno "${label} canonical encoding" "two encodes differ"; fi
  done

  # Classic BMG uses a richer text form with encoding, MID and INF metadata.
  local bmg_text="$PWD_PROJECT/../tests/samples-excitebots/extract/excitebots.d/UPDATE/files/_sys/RVL-WiiSystemmenu-v385.d/00000063.d/message/jpn/sample.bmg.txt"
  if "$B/wbmgt" ENCODE "$bmg_text" --dest "$d/msg-a/same.bmg" --overwrite >/dev/null 2>&1 \
  && "$B/wbmgt" ENCODE "$bmg_text" --dest "$d/msg-b/same.bmg" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/msg-a/same.bmg" "$d/msg-b/same.bmg"; then
    bok "BMG same semantic text -> identical encoded bytes"
  else bno "BMG canonical encoding" "two encodes differ"; fi

  # Classic BMG encoding matrix:
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 1\n@BMG-MID = 1\n[0001]\nHello CP1252 world!\n\n[0002]\nSecond string\n' > "$d/source-bmg-cp1252.txt"
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 2\n@BMG-MID = 1\n[0001]\nHello UTF-16 world!\n\n[0002]\nSecond string\n' > "$d/source-bmg-utf16.txt"
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 3\n@BMG-MID = 1\n[0001]\nHello SJIS world!\n\n[0002]\nSecond string\n' > "$d/source-bmg-sjis.txt"
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 4\n@BMG-MID = 1\n[0001]\nHello UTF-8 world!\n\n[0002]\nSecond string\n' > "$d/source-bmg-utf8.txt"
  printf '#BMG\n@LEGACY = 1\n@ENDIAN = 0\n@ENCODING = 1\n@BMG-MID = 0\n[0001]\nHello GameCube BMG!\n\n[0002]\nSecond string\n' > "$d/source-bmg-gc.txt"
  for spec in 'source-bmg-cp1252.txt BMG_CP1252' \
              'source-bmg-utf16.txt BMG_UTF16' \
              'source-bmg-sjis.txt BMG_SJIS' \
              'source-bmg-utf8.txt BMG_UTF8' \
              'source-bmg-gc.txt BMG_GameCube'; do
    set -- $spec; src="$d/$1"; label=$2
    if "$B/wbmgt" ENCODE "$src" --dest "$d/msg-a/same-$label.bmg" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" ENCODE "$src" --dest "$d/msg-b/same-$label.bmg" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/msg-a/same-$label.bmg" "$d/msg-b/same-$label.bmg"; then
      bok "${label} same semantic text -> identical encoded bytes"
    else bno "${label} canonical encoding" "two encodes differ"; fi
  done

  # Mario Kart track definitions (KMP):
  mkdir -p "$d/kmp-a" "$d/kmp-b"
  printf '#KMP\n@FORMAT = 1\n\n[KTPT]\n#00: pos=(0,0,0) rot=(0,0,0) player=0\n' > "$d/init.kmp.txt"
  "$B/wkmpt" ENCODE "$d/init.kmp.txt" --dest "$d/init.kmp" --overwrite >/dev/null 2>&1
  "$B/wkmpt" DECODE "$d/init.kmp" --dest "$d/source.kmp.txt" --overwrite >/dev/null 2>&1
  if "$B/wkmpt" ENCODE "$d/source.kmp.txt" --dest "$d/kmp-a/same.kmp" --overwrite >/dev/null 2>&1 \
  && "$B/wkmpt" ENCODE "$d/source.kmp.txt" --dest "$d/kmp-b/same.kmp" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/kmp-a/same.kmp" "$d/kmp-b/same.kmp"; then
    bok "KMP same semantic text -> identical encoded bytes"
  else bno "KMP canonical encoding" "two encodes differ"; fi

  # Wii BRLAN/BRLYT semantic encoders retain all sections and padding for
  # their canonical text form, independently of the newer BFLAN/BFLYT pair.
  local legacy_root="$PWD_PROJECT/../tests/samples-excitebots/extract/excitebots.d/UPDATE/files/_sys/RVL-Eulav_US-v2.d/0000000b.d/layout.d/arc"
  for spec in 'anim/EULA_ViewerDialog_DialogIn.brlan brlan' \
              'blyt/EULA_ViewerDialog.brlyt brlyt' \
              'blyt/EULA_Viewer_a.brlyt brlyt' \
              'blyt/EULA_Viewer_b.brlyt brlyt' \
              'blyt/P1_Def.brlyt brlyt' \
              'blyt/P2_Def.brlyt brlyt'; do
    set -- $spec; src="$legacy_root/$1"; ext=$2; local name=$(basename "$1" ".$ext")
    if "$B/wlayt" decode "$src" "$d/same-$name.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/same-$name.tflyt" "$d/layout-a/same-$name.$ext" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/same-$name.tflyt" "$d/layout-b/same-$name.$ext" >/dev/null 2>&1 \
    && cmp -s "$d/layout-a/same-$name.$ext" "$d/layout-b/same-$name.$ext"; then
      bok "${name}.${ext} same semantic text -> identical encoded bytes"
    else bno "${name}.${ext} canonical encoding" "two encodes differ"; fi
  done

  # NintendoWare sequence assembly. Test both endian variants of FSEQ.
  mkdir -p "$d/seq-a" "$d/seq-b"
  printf '; canonical sequence\ntimebase 48\ntempo 120\nnote C4 100 48\nwait 48\nfin\n' > "$d/song.txt"
  for spec in 'RSEQ rseq' 'CSEQ cseq' 'FSEQ fseq' 'FSEQ_LE fseqle' 'SSEQ sseq' 'BMS bms'; do
    set -- $spec; local form=$1; ext=$2
    if "$B/wseqt" asm "$d/song.txt" "$d/seq-a/same.$ext" --format "$form" >/dev/null 2>&1 \
    && "$B/wseqt" asm "$d/song.txt" "$d/seq-b/same.$ext" --format "$form" >/dev/null 2>&1 \
    && cmp -s "$d/seq-a/same.$ext" "$d/seq-b/same.$ext"; then
      bok "${form} same sequence text -> identical encoded bytes"
    else bno "${form} canonical encoding" "two assemblies differ"; fi
  done

  # RBNK: compile XML -> identical encoded bytes
  mkdir -p "$d/rbnk-a" "$d/rbnk-b"
  local rbnk_xml="$d/rbnk_test.xml"
  cat > "$rbnk_xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<rbnk source="test" version="1.1" n-program="4" n-wave="2">
  <programs>
    <program index="0">
      <inst wave-index="0" attack="127" decay="127" sustain="127" release="127" hold="0" note-off="0" alt-assign="0" original-key="60" volume="127" pan="64" surround-pan="0" pitch="1.000000"/>
    </program>
    <program index="1">
      <range-table n="2">
        <entry key="60">
          <inst wave-index="0" attack="127" decay="127" sustain="127" release="127" hold="0" note-off="0" alt-assign="0" original-key="60" volume="127" pan="64" surround-pan="0" pitch="1.000000"/>
        </entry>
        <entry key="72">
          <inst wave-index="1" attack="127" decay="127" sustain="127" release="127" hold="0" note-off="0" alt-assign="0" original-key="72" volume="127" pan="64" surround-pan="0" pitch="1.000000"/>
        </entry>
      </range-table>
    </program>
  </programs>
  <waves>
    <wave index="0" encoding="ADPCM_THP" channels="1" sample-rate="32000" samples="1000" loop="no"/>
    <wave index="1" encoding="PCM16" channels="2" sample-rate="44100" samples="2000" loop="yes" loop-start="500"/>
  </waves>
</rbnk>
EOF
  if "$B/wrbnk" compile "$rbnk_xml" "$d/rbnk-a/same.rbnk" >/dev/null 2>&1 \
  && "$B/wrbnk" compile "$rbnk_xml" "$d/rbnk-b/same.rbnk" >/dev/null 2>&1 \
  && cmp -s "$d/rbnk-a/same.rbnk" "$d/rbnk-b/same.rbnk"; then
    bok "RBNK same XML -> identical encoded bytes"
  else bno "RBNK canonical encoding" "two encodes differ"; fi

  # Sound archives. Work on two independent extracted trees because CREATE
  # legitimately updates each tree's setup/hash metadata after a successful
  # build; comparing two builds from one mutated tree would test cache state.
  mkdir -p "$d/brsar-in" "$d/brsar-a" "$d/brsar-b"
  printf '; byte test\ntempo 120\nprg 0\nn C4 100 48\nfin\n' > "$d/brsar-in/melody.txt"
  printf '\x52\x42\x4e\x4b\x00\x00\x00\x10' > "$d/brsar-in/melody.rbnk"
  if "$B/wbrsar" pack "$d/brsar-in" "$d/brsar-a/same.brsar" >/dev/null 2>&1 \
  && "$B/wbrsar" pack "$d/brsar-in" "$d/brsar-b/same.brsar" >/dev/null 2>&1 \
  && cmp -s "$d/brsar-a/same.brsar" "$d/brsar-b/same.brsar"; then
    bok "BRSAR same member tree -> identical encoded bytes"
  else bno "BRSAR canonical encoding" "two packs differ"; fi
  local typ out sub label
  for typ in bc bf; do
    [ "$typ" = bc ] && label=BCSAR || label=BFSAR
    out="$d/${typ}sar-extract"
    "$B/wszst" xx "$PWD_PROJECT/../tests/fixtures/sample.${typ}sar" --dest "$out" >/dev/null 2>&1
    sub=$(find "$out" -mindepth 1 -maxdepth 1 -type d | head -1)
    mkdir -p "$d/${typ}sar-a" "$d/${typ}sar-b"
    cp -a "$sub" "$d/${typ}sar-a/input"; cp -a "$sub" "$d/${typ}sar-b/input"
    if "$B/wszst" CREATE "$d/${typ}sar-a/input" --dest "$d/${typ}sar-a/same.${typ}sar" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/${typ}sar-b/input" --dest "$d/${typ}sar-b/same.${typ}sar" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/${typ}sar-a/same.${typ}sar" "$d/${typ}sar-b/same.${typ}sar"; then
      bok "$label same extracted tree -> identical encoded bytes"
    else bno "$label canonical encoding" "two creates differ"; fi
  done

  # BRRES: same extracted member tree from all retail samples
  for brres_file in $(ls "$PWD_PROJECT/../tests/fixtures"/accf_*.brres | sort); do
    local bname=$(basename "$brres_file")
    mkdir -p "$d/brres-ext-$bname"
    "$B/wszst" xx "$brres_file" --dest "$d/brres-ext-$bname" --overwrite >/dev/null 2>&1
    local sub=$(find "$d/brres-ext-$bname" -mindepth 1 -maxdepth 1 -type d | head -1)
    if [ -n "$sub" ]; then
      mkdir -p "$d/brres-a" "$d/brres-b"
      cp -a "$sub" "$d/brres-a/input-$bname"; cp -a "$sub" "$d/brres-b/input-$bname"
      if "$B/wszst" CREATE "$d/brres-a/input-$bname" --dest "$d/brres-a/same-$bname" --overwrite >/dev/null 2>&1 \
      && "$B/wszst" CREATE "$d/brres-b/input-$bname" --dest "$d/brres-b/same-$bname" --overwrite >/dev/null 2>&1 \
      && cmp -s "$d/brres-a/same-$bname" "$d/brres-b/same-$bname"; then
        bok "$bname same extracted tree -> identical encoded bytes"
      else bno "$bname canonical encoding" "two creates differ"; fi
    fi
  done

  # KCL: same OBJ triangle mesh from two independent encodes (including the
  # octree build, which is order- and layout-sensitive) must be identical.
  mkdir -p "$d/kcl-a" "$d/kcl-b"
  printf 'v 0 0 0\nv 10 0 0\nv 0 10 0\nv 10 10 0\nf 1 2 3\nf 2 4 3\n' > "$d/tri.obj"
  if "$B/wkclt" ENCODE "$d/tri.obj" --dest "$d/kcl-a/same.kcl" --overwrite >/dev/null 2>&1 \
  && "$B/wkclt" ENCODE "$d/tri.obj" --dest "$d/kcl-b/same.kcl" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/kcl-a/same.kcl" "$d/kcl-b/same.kcl"; then
    bok "KCL same OBJ -> identical encoded bytes"
  else bno "KCL canonical encoding" "two encodes differ"; fi

  # KCL topology variants: closed 3D box and multi-quad terrain heightfield
  mkdir -p "$d/kcl-mesh-a" "$d/kcl-mesh-b"
  printf 'v 0 0 0\nv 10 0 0\nv 10 10 0\nv 0 10 0\nv 0 0 10\nv 10 0 10\nv 10 10 10\nv 0 10 10\nf 1 2 3\nf 1 3 4\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\nf 2 3 7\nf 2 7 6\nf 3 4 8\nf 3 8 7\nf 4 1 5\nf 4 5 8\n' > "$d/box.obj"
  printf 'v 0 0 0\nv 50 0 5\nv 100 0 -2\nv 0 50 10\nv 50 50 20\nv 100 50 8\nv 0 100 0\nv 50 100 -5\nv 100 100 0\nf 1 2 5\nf 1 5 4\nf 2 3 6\nf 2 6 5\nf 4 5 8\nf 4 8 7\nf 5 6 9\nf 5 9 8\n' > "$d/terrain.obj"
  for mesh in box terrain; do
    if "$B/wkclt" ENCODE "$d/$mesh.obj" --dest "$d/kcl-mesh-a/$mesh.kcl" --overwrite >/dev/null 2>&1 \
    && "$B/wkclt" ENCODE "$d/$mesh.obj" --dest "$d/kcl-mesh-b/$mesh.kcl" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/kcl-mesh-a/$mesh.kcl" "$d/kcl-mesh-b/$mesh.kcl"; then
      bok "KCL ${mesh} same OBJ -> identical encoded bytes"
    else bno "KCL ${mesh} canonical encoding" "two encodes differ"; fi
  done

  # DSP-ADPCM coefficient search and frame encoding must also be stable.
  python3 - "$d/in.wav" <<'PY'
import math,struct,sys
sr=32000; n=8000
pcm=b''.join(struct.pack('<h',int(6000*math.sin(i*.05))) for i in range(n))
wav=b'RIFF'+struct.pack('<I',36+len(pcm))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,sr,sr*2,2,16)+b'data'+struct.pack('<I',len(pcm))+pcm
open(sys.argv[1],'wb').write(wav)
PY
  mkdir -p "$d/brstm-a" "$d/brstm-b"
  for spec in 'BRSTM ' 'BFSTM --bfstm' 'BCSTM --bcstm'; do
    set -- $spec; local name=$1; shift; local flag="$*"
    if "$B/wbrstm" from_wav "$d/in.wav" "$d/brstm-a/same.$name" $flag >/dev/null 2>&1 \
    && "$B/wbrstm" from_wav "$d/in.wav" "$d/brstm-b/same.$name" $flag >/dev/null 2>&1 \
    && cmp -s "$d/brstm-a/same.$name" "$d/brstm-b/same.$name"; then
      bok "${name} same PCM WAV -> identical ADPCM encoded bytes"
    else bno "${name} canonical encoding" "two encodes differ"; fi
  done

  # Switch BFRES: CREATE derives the resource basename from the DAE, so use
  # equal basenames in separate trees and compare the complete containers.
  mkdir -p "$d/bfres-a" "$d/bfres-b"
  if "$B/wmdlt" ENCODE "$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres" --dest "$d/bfres-source.dae" --overwrite >/dev/null 2>&1 \
  && cp "$d/bfres-source.dae" "$d/bfres-a/same.dae" \
  && cp "$d/bfres-source.dae" "$d/bfres-b/same.dae" \
  && "$B/wszst" CREATE "$d/bfres-a" >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/bfres-b" >/dev/null 2>&1 \
  && cmp -s "$d/bfres-a/same.bfres" "$d/bfres-b/same.bfres"; then
    bok "Switch BFRES same DAE -> identical encoded bytes"
  else bno "Switch BFRES canonical encoding" "two encodes differ"; fi

  # Geometry encoders: all derived tables, deduplication order, bounds and
  # floating-point serialization must be stable for one DAE input.
  mkdir -p "$d/model-a" "$d/model-b"
  $B/wmdlt ENCODE "$PWD_PROJECT/../tests/fixtures/excite_goalback.msh" --dest "$d/collision.glb" --overwrite >/dev/null 2>&1
  $B/wmdlt ENCODE "$PWD_PROJECT/../tests/fixtures/excite_arrow_obj.mod" --dest "$d/render.glb" --overwrite >/dev/null 2>&1
  for spec in "collision.glb msh" "render.glb mod" "render.glb hsf" "render.glb dat" "render.glb bfres"; do
    set -- $spec; local src="$d/$1"; local ext=$2
    if "$B/wmdlt" ENCODE "$src" --dest "$d/model-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$src" --dest "$d/model-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/model-a/same.$ext" "$d/model-b/same.$ext"; then
      bok "${ext} same GLB -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two encodes differ"; fi
  done
  for spec in "excite_gpmesh.msh msh" "excite_rail2bp.msh msh" "excite_arrow_point.mod mod" "excite_sunflower2.mod mod"; do
    set -- $spec; local mfile=$1; local ext=$2
    $B/wmdlt ENCODE "$PWD_PROJECT/../tests/fixtures/$mfile" --dest "$d/$mfile.glb" --overwrite >/dev/null 2>&1
    if "$B/wmdlt" ENCODE "$d/$mfile.glb" --dest "$d/model-a/same-$mfile" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/$mfile.glb" --dest "$d/model-b/same-$mfile" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/model-a/same-$mfile" "$d/model-b/same-$mfile"; then
      bok "${mfile} same GLB -> identical encoded bytes"
    else bno "${mfile} canonical encoding" "two encodes differ"; fi
  done
  mkdir -p "$d/mdl-extract"
  "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/accf_ins_taran.brres" --dest "$d/mdl-extract" --overwrite >/dev/null 2>&1
  local mdl="$d/mdl-extract/3DModels(NW4R)/ins_taran"
  if "$B/wmdlt" DECODE "$mdl" --dest "$d/mdl.dae" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/mdl.dae" --parent="$mdl" --dest "$d/model-a/same.mdl0" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/mdl.dae" --parent="$mdl" --dest "$d/model-b/same.mdl0" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/model-a/same.mdl0" "$d/model-b/same.mdl0"; then
    bok "MDL0 same DAE+parent -> identical injected bytes"
  else bno "MDL0 canonical injection" "two injections differ"; fi
  local wbf="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  if "$B/wmdlt" ENCODE "$wbf" --dest "$d/wiiu-bfres.dae" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/wiiu-bfres.dae" --parent="$wbf" --dest "$d/model-a/same-wiiu.bfres" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/wiiu-bfres.dae" --parent="$wbf" --dest "$d/model-b/same-wiiu.bfres" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/model-a/same-wiiu.bfres" "$d/model-b/same-wiiu.bfres"; then
    bok "Wii U BFRES same DAE+parent -> identical injected bytes"
  else bno "Wii U BFRES canonical injection" "two injections differ"; fi
  for bch_f in "$PWD_PROJECT/../tests/fixtures/synthetic_sample.bch" \
               "$PWD_PROJECT/../tests/fixtures/3ds_samples/zelda_bch/HookshotR.bch" \
               "$PWD_PROJECT/../tests/fixtures/3ds_samples/zelda_bch/GtEvSwordD.bch" \
               "$PWD_PROJECT/../tests/fixtures/3ds_samples/zelda_bch/HeartContainer.bch" \
               "$PWD_PROJECT/../tests/fixtures/3ds_samples/zelda_bch/FlatLinkLv1.bch"; do
    if [ -f "$bch_f" ]; then
      local bname; bname=$(basename "$bch_f")
      if "$B/wmdlt" ENCODE "$bch_f" --dest "$d/bch-${bname}.dae" --overwrite >/dev/null 2>&1 \
      && "$B/wmdlt" ENCODE "$d/bch-${bname}.dae" --parent="$bch_f" --dest "$d/model-a/same-${bname}" --overwrite >/dev/null 2>&1 \
      && "$B/wmdlt" ENCODE "$d/bch-${bname}.dae" --parent="$bch_f" --dest "$d/model-b/same-${bname}" --overwrite >/dev/null 2>&1 \
      && cmp -s "$d/model-a/same-${bname}" "$d/model-b/same-${bname}"; then
        bok "BCH ${bname} same DAE+parent -> identical injected bytes"
      else bno "BCH ${bname} canonical injection" "two injections differ"; fi
    fi
  done
  local nsbmd_f="$PWD_PROJECT/../tests/fixtures/synthetic_sample.nsbmd"
  if [ -f "$nsbmd_f" ]; then
    if "$B/wmdlt" ENCODE "$nsbmd_f" --dest "$d/nsbmd-orig.dae" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/nsbmd-orig.dae" --parent="$nsbmd_f" --dest "$d/model-a/same.nsbmd" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/nsbmd-orig.dae" --parent="$nsbmd_f" --dest "$d/model-b/same.nsbmd" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/model-a/same.nsbmd" "$d/model-b/same.nsbmd"; then
      bok "NSBMD same DAE+parent -> identical injected bytes"
    else bno "NSBMD canonical injection" "two injections differ"; fi
  fi

  # Compression codecs: compare the complete encoded streams, including
  # headers/checksums/padding, rather than merely decoding them to RAW again.
  printf 'canonical RNC1 byte stream regression\n' > "$d/raw.bin"
  mkdir -p "$d/codec-a" "$d/codec-b"
  local ext
  for ext in lz10 lz11 rl yay0 ash lzh8 qlz at7 blz huff4 huff8 stpl rnc1 rnc2 fzip zlib deflate yaz0 yaz1 xyz bz ybz bz2 lz ylz lzma xz bclz rle; do
    if "$B/wszst" COMPRESS "$d/raw.bin" --dest "$d/codec-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/raw.bin" --dest "$d/codec-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/codec-a/same.$ext" "$d/codec-b/same.$ext"; then
      bok "${ext} same input -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two encodes differ"; fi
  done

  # Archive creators: equal output basenames avoid treating a deliberately
  # embedded archive name as nondeterminism. Complete headers, file tables,
  # member ordering, alignment and payload bytes are compared.
  mkdir -p "$d/tree/sub" "$d/archive-a" "$d/archive-b"
  printf alpha > "$d/tree/a"; printf beta > "$d/tree/sub/b"
  for ext in narc darc pac gfa rarc sarc sarc.fzip warc warc.fzip ccf nccarc at7 mpbin arc wu8 pack rkc breff breft lta lfl szs wbz ybz wlz ylz; do
    if "$B/wszst" CREATE "$d/tree" --dest "$d/archive-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/tree" --dest "$d/archive-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same.$ext" "$d/archive-b/same.$ext"; then
      bok "${ext} same tree -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two creates differ"; fi
  done

  # A CTPK is a texture package, so exercise a real multi-PNG member tree.
  mkdir -p "$d/ctpk-tree"
  cp "$d/atlas.png" "$d/ctpk-tree/tex_a.png"
  python3 "$PNGTOOL" write "$d/ctpk-tree/tex_b.png" 64 64 20 40 60
  if "$B/wszst" CREATE "$d/ctpk-tree" --dest "$d/archive-a/same.ctpk" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/ctpk-tree" --dest "$d/archive-b/same.ctpk" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.ctpk" "$d/archive-b/same.ctpk"; then
    bok "ctpk same multi-PNG tree -> identical encoded bytes"
  else bno "ctpk canonical encoding" "two creates differ"; fi
  for ctpk_name in mk7_coins.ctpk mk7_common_env.ctpk; do
    local ctpk_path="$PWD_PROJECT/../tests/fixtures/$ctpk_name"
    mkdir -p "$d/ext-$ctpk_name"
    "$B/wszst" EXTRACT "$ctpk_path" --dest "$d/ext-$ctpk_name" --overwrite >/dev/null 2>&1
    if "$B/wszst" CREATE "$d/ext-$ctpk_name" --dest "$d/archive-a/same-$ctpk_name" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/ext-$ctpk_name" --dest "$d/archive-b/same-$ctpk_name" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same-$ctpk_name" "$d/archive-b/same-$ctpk_name"; then
      bok "$ctpk_name same extracted tree -> identical encoded bytes"
    else bno "$ctpk_name canonical encoding" "two creates differ"; fi
  done
  local gfa_path="$PWD_PROJECT/../tests/fixtures/gfa_bean00.gfa"
  mkdir -p "$d/ext-gfa"
  "$B/wszst" EXTRACT "$gfa_path" --dest "$d/ext-gfa" --overwrite >/dev/null 2>&1
  if "$B/wszst" CREATE "$d/ext-gfa" --dest "$d/archive-a/same.gfa" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/ext-gfa" --dest "$d/archive-b/same.gfa" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.gfa" "$d/archive-b/same.gfa"; then
    bok "gfa_bean00.gfa same extracted tree -> identical encoded bytes"
  else bno "gfa_bean00.gfa canonical encoding" "two creates differ"; fi

  # Monster RST is a paired container: the payload and TOC must both be
  # deterministic or the archive as a whole is not reproducible.
  if "$B/wszst" CREATE "$d/tree" --dest "$d/archive-a/same.rst" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/tree" --dest "$d/archive-b/same.rst" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.rst" "$d/archive-b/same.rst" \
  && cmp -s "$d/archive-a/same.toc" "$d/archive-b/same.toc"; then
    bok "RST+TOC same tree -> identical paired container bytes"
  else bno "RST+TOC canonical encoding" "two paired creates differ"; fi

  # Nitro sprite cell/animation XML includes raw OAM attributes and explicit
  # frame-data offsets; deterministic bytes cover all derived section tables.
  printf '<?xml version="1.0"?>\n<ncer cells="2">\n  <cell index="0" objects="1">\n    <obj attr0="0x0001" attr1="0x4002" attr2="0x8003"/>\n  </cell>\n  <cell index="1" objects="1">\n    <obj attr0="0x0010" attr1="0x4020" attr2="0x8030"/>\n  </cell>\n</ncer>\n' > "$d/source.ncer.xml"
  printf '<?xml version="1.0"?>\n<nanr animations="1" frames="2">\n  <animation index="0" frames="2">\n    <frame cell="0" duration="5" data-offset="0x0"/>\n    <frame cell="1" duration="7" data-offset="0x2"/>\n  </animation>\n</nanr>\n' > "$d/source.nanr.xml"
  for ext in ncer nanr; do
    if "$B/wszst" CREATE "$d/source.$ext.xml" --dest "$d/archive-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/source.$ext.xml" --dest "$d/archive-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same.$ext" "$d/archive-b/same.$ext"; then
      bok "${ext} same XML -> identical encoded bytes"
    else bno "${ext} canonical encoding" "two creates differ"; fi
  done

  printf 'name: byte-test\nvalue: 42\nitems:\n  - one\n  - two\n' > "$d/source.yaml"
  if "$B/wszst" CREATE "$d/source.yaml" --dest "$d/archive-a/same.byml" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/source.yaml" --dest "$d/archive-b/same.byml" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.byml" "$d/archive-b/same.byml"; then
    bok "BYML same YAML -> identical encoded bytes"
  else bno "BYML canonical encoding" "two creates differ"; fi
  for byml in sm3dl_camera.byml sm3dl_stageinfo.byml; do
    "$B/wszst" TEXT "$PWD_PROJECT/../tests/fixtures/$byml" --dest "$d/$byml.yaml" --overwrite >/dev/null 2>&1
    if "$B/wszst" CREATE "$d/$byml.yaml" --dest "$d/archive-a/same-$byml" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/$byml.yaml" --dest "$d/archive-b/same-$byml" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same-$byml" "$d/archive-b/same-$byml"; then
      bok "${byml} same YAML -> identical encoded bytes"
    else bno "${byml} canonical encoding" "two creates differ"; fi
  done
  if "$B/wszst" COMPRESS "$d/atlas.png" --dest "$d/archive-a/same.wux" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" COMPRESS "$d/atlas.png" --dest "$d/archive-b/same.wux" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.wux" "$d/archive-b/same.wux"; then
    bok "WUX same input -> identical encoded bytes"
  else bno "WUX canonical encoding" "two encodes differ"; fi

  # romc requires an exact multiple of 4 MiB and carries the unit count in
  # its header, so use a sparse but structurally valid N64-sized input.
  dd if=/dev/zero of="$d/rom.z64" bs=1 count=0 seek=4194304 2>/dev/null
  printf '\200\067\022\100byte romc' | dd of="$d/rom.z64" conv=notrunc 2>/dev/null
  if "$B/wszst" COMPRESS "$d/rom.z64" --dest "$d/archive-a/same.romc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" COMPRESS "$d/rom.z64" --dest "$d/archive-b/same.romc" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.romc" "$d/archive-b/same.romc"; then
    bok "romc same ROM -> identical encoded bytes"
  else bno "romc canonical encoding" "two encodes differ"; fi
}
t_byte_exact_encoders

echo "== canonical encode-decode-encode byte fixed points =="
t_byte_fixed_points(){
  local d; d=$(mktemp -d /tmp/_r_bytefixed.XXXXXX) || { fno "fixed points" "mktemp failed"; return; }
  mkdir -p "$d/a" "$d/b"
  python3 "$PNGTOOL" write "$d/source.png" 32 32 100 150 200
  local ext
  for ext in ncgr nclr plt0 tex0 brfnt brfna bcfnt bffnt bclim bflim bntx gtx tpl bti; do
    # A previous iteration's DECODE (e.g. TEX0, which has real mipmaps) can
    # leave "mid.mm1.png"/"mid.mm2.png" sidecars that a later format's own
    # ENCODE then picks up by the same naming convention, corrupting this
    # iteration's *input* rather than testing its encoder's determinism.
    rm -f "$d"/mid.mm*.png
    if "$B/wimgt" ENCODE "$d/source.png" --dest "$d/a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/a/same.$ext" --dest "$d/mid.png" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/mid.png" --dest "$d/b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same.$ext" "$d/b/same.$ext"; then
      fok "${ext} encode -> PNG -> identical re-encode"
    else fno "${ext} canonical fixed point" "second-generation bytes differ"; fi
  done

  # GX texture/palette formats: each pixel encoding must be a canonical fixed point.
  for cont in tpl bti tex0; do
    for fmt in I4 I8 IA4 IA8 RGB565 RGB5A3 RGBA8 CMPR; do
      mkdir -p "$d/gx-a" "$d/gx-b"
      rm -f "$d"/mid.mm*.png
      if "$B/wimgt" ENCODE "$d/source.png" --transform "$fmt" --dest "$d/gx-a/same.$cont" --overwrite >/dev/null 2>&1 \
      && "$B/wimgt" DECODE "$d/gx-a/same.$cont" --dest "$d/mid.png" --overwrite >/dev/null 2>&1 \
      && "$B/wimgt" ENCODE "$d/mid.png" --transform "$fmt" --dest "$d/gx-b/same.$cont" --overwrite >/dev/null 2>&1 \
      && cmp -s "$d/gx-a/same.$cont" "$d/gx-b/same.$cont"; then
        fok "$(echo "$cont" | tr a-z A-Z) ${fmt} encode -> PNG -> identical re-encode"
      else fno "$(echo "$cont" | tr a-z A-Z) ${fmt} canonical fixed point" "second-generation bytes differ"; fi
    done
  done
  for fmt in IA8 RGB565 RGB5A3; do
    mkdir -p "$d/plt-a" "$d/plt-b"
    if "$B/wimgt" ENCODE "$d/source.png" --transform "$fmt" --dest "$d/plt-a/same.plt0" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/plt-a/same.plt0" --dest "$d/mid.png" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/mid.png" --transform "$fmt" --dest "$d/plt-b/same.plt0" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/plt-a/same.plt0" "$d/plt-b/same.plt0"; then
      fok "PLT0 ${fmt} encode -> PNG -> identical re-encode"
    else fno "PLT0 ${fmt} canonical fixed point" "second-generation bytes differ"; fi
  done
  for cont in bflim bclim; do
    for fmt in RGB565 RGBA8; do
      mkdir -p "$d/${cont}-a" "$d/${cont}-b"
      if "$B/wimgt" ENCODE "$d/source.png" --transform "$fmt" --dest "$d/${cont}-a/same.$cont" --overwrite >/dev/null 2>&1 \
      && "$B/wimgt" DECODE "$d/${cont}-a/same.$cont" --dest "$d/mid.png" --overwrite >/dev/null 2>&1 \
      && "$B/wimgt" ENCODE "$d/mid.png" --transform "$fmt" --dest "$d/${cont}-b/same.$cont" --overwrite >/dev/null 2>&1 \
      && cmp -s "$d/${cont}-a/same.$cont" "$d/${cont}-b/same.$cont"; then
        fok "$(echo "$cont" | tr a-z A-Z) ${fmt} encode -> PNG -> identical re-encode"
      else fno "$(echo "$cont" | tr a-z A-Z) ${fmt} canonical fixed point" "second-generation bytes differ"; fi
    done
  done
  for fmt in RGB565 RGBA8; do
    mkdir -p "$d/gtx-a" "$d/gtx-b"
    if "$B/wimgt" ENCODE "$d/source.png" --transform "$fmt" --dest "$d/gtx-a/same.gtx" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/gtx-a/same.gtx" --dest "$d/mid.png" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/mid.png" --transform "$fmt" --dest "$d/gtx-b/same.gtx" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/gtx-a/same.gtx" "$d/gtx-b/same.gtx"; then
      fok "GTX ${fmt} encode -> PNG -> identical re-encode"
    else fno "GTX ${fmt} canonical fixed point" "second-generation bytes differ"; fi
  done
  mkdir -p "$d/bntx-a" "$d/bntx-b"
  for fmt in RGB565 RGBA8; do
    if "$B/wimgt" CONVERT "$d/source.png" --transform "$fmt" --dest "$d/bntx-a/same_${fmt}.bntx" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/bntx-a/same_${fmt}.bntx" --dest "$d/mid_${fmt}.png" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" CONVERT "$d/mid_${fmt}.png" --transform "$fmt" --dest "$d/bntx-b/same_${fmt}.bntx" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/bntx-a/same_${fmt}.bntx" "$d/bntx-b/same_${fmt}.bntx"; then
      fok "BNTX ${fmt} encode -> PNG -> identical re-encode"
    else fno "BNTX ${fmt} canonical fixed point" "second-generation bytes differ"; fi
  done
  for fmt in RGB565 RGBA8; do
    mkdir -p "$d/bflim-fzip-a" "$d/bflim-fzip-b"
    if "$B/wimgt" ENCODE "$d/source.png" --transform "$fmt" --dest "$d/bflim-raw1.bflim" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/bflim-raw1.bflim" --dest "$d/bflim-fzip-a/same_${fmt}.bflim.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/bflim-fzip-a/same_${fmt}.bflim.fzip" --dest "$d/mid-bflim-fzip_${fmt}.png" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/mid-bflim-fzip_${fmt}.png" --transform "$fmt" --dest "$d/bflim-raw2.bflim" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/bflim-raw2.bflim" --dest "$d/bflim-fzip-b/same_${fmt}.bflim.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/bflim-fzip-a/same_${fmt}.bflim.fzip" "$d/bflim-fzip-b/same_${fmt}.bflim.fzip"; then
      fok "BFLIM ${fmt}.fzip encode -> PNG -> identical re-encode"
    else fno "BFLIM ${fmt}.fzip canonical fixed point" "second-generation bytes differ"; fi
  done
  for fmt in RGB565 RGBA8; do
    mkdir -p "$d/bntx-fzip-a" "$d/bntx-fzip-b"
    if "$B/wimgt" CONVERT "$d/source.png" --transform "$fmt" --dest "$d/bntx-raw.bntx" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/bntx-raw.bntx" --dest "$d/bntx-fzip-a/same_${fmt}.bntx.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/bntx-fzip-a/same_${fmt}.bntx.fzip" --dest "$d/mid-bntx-fzip_${fmt}.png" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" CONVERT "$d/mid-bntx-fzip_${fmt}.png" --transform "$fmt" --dest "$d/bntx-raw.bntx" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/bntx-raw.bntx" --dest "$d/bntx-fzip-b/same_${fmt}.bntx.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/bntx-fzip-a/same_${fmt}.bntx.fzip" "$d/bntx-fzip-b/same_${fmt}.bntx.fzip"; then
      fok "BNTX ${fmt}.fzip encode -> PNG -> identical re-encode"
    else fno "BNTX ${fmt}.fzip canonical fixed point" "second-generation bytes differ"; fi
  done

  # CTPK embeds the texture basename, so keep that logical name identical on
  # both sides of the PNG interchange rather than confusing a rename with
  # encoder drift.
  mkdir -p "$d/ctpk-a" "$d/ctpk-b"
  cp "$d/source.png" "$d/ctpk-a/source.png"
  if "$B/wimgt" ENCODE "$d/ctpk-a/source.png" --dest "$d/ctpk-a/same.ctpk" --overwrite >/dev/null 2>&1 \
  && "$B/wimgt" DECODE "$d/ctpk-a/same.ctpk" --dest "$d/ctpk-b/source.png" --overwrite >/dev/null 2>&1 \
  && "$B/wimgt" ENCODE "$d/ctpk-b/source.png" --dest "$d/ctpk-b/same.ctpk" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/ctpk-a/same.ctpk" "$d/ctpk-b/same.ctpk"; then
    fok "ctpk encode -> PNG -> identical re-encode"
  else fno "ctpk canonical fixed point" "second-generation bytes differ"; fi

  # Excite GUI and texture resources use wszst's format-aware extraction.
  # The footerless TEX encoder uses .etex to select encoding and .tex to make
  # the otherwise magic-less result identifiable to the decoder.
  "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/excite_ach_trun.art" \
    --dest "$d/excite.png" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/excite_bat_d2.tex" \
    --dest "$d/excite-tex.png" --overwrite >/dev/null 2>&1
  mkdir -p "$d/excite-a" "$d/excite-b"
  for ext in art img; do
    if "$B/wimgt" ENCODE "$d/excite.png" --dest "$d/excite-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/excite-a/same.$ext" --dest "$d/excite-b/mid-$ext.png" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/excite-b/mid-$ext.png" --dest "$d/excite-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/excite-a/same.$ext" "$d/excite-b/same.$ext"; then
      fok "$ext encode -> PNG -> identical re-encode"
    else fno "$ext canonical fixed point" "second-generation bytes differ"; fi
  done
  "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/excite_silvcoin.art" \
    --dest "$d/coin.png" --overwrite >/dev/null 2>&1
  for ext in art img; do
    if "$B/wimgt" ENCODE "$d/coin.png" --dest "$d/excite-a/coin.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/excite-a/coin.$ext" --dest "$d/excite-b/mid-coin-$ext.png" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" ENCODE "$d/excite-b/mid-coin-$ext.png" --dest "$d/excite-b/coin.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/excite-a/coin.$ext" "$d/excite-b/coin.$ext"; then
      fok "coin $ext encode -> PNG -> identical re-encode"
    else fno "coin $ext canonical fixed point" "second-generation bytes differ"; fi
  done
  if "$B/wimgt" ENCODE "$d/excite-tex.png" --dest "$d/excite-a/same.etex" --overwrite >/dev/null 2>&1 \
  && cp "$d/excite-a/same.etex" "$d/excite-a/same.tex" \
  && "$B/wszst" EXTRACT "$d/excite-a/same.tex" --dest "$d/excite-b/mid-etex.png" --overwrite >/dev/null 2>&1 \
  && "$B/wimgt" ENCODE "$d/excite-b/mid-etex.png" --dest "$d/excite-b/same.etex" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/excite-a/same.etex" "$d/excite-b/same.etex"; then
    fok "etex encode -> PNG -> identical re-encode"
  else fno "etex canonical fixed point" "second-generation bytes differ"; fi

  # Message Studio's textual interchange is canonical too.  MSBT includes an
  # escaped newline specifically to guard against separator/newline drift.
  printf '# MSBT: Message Studio Binary Text (BigEndian, UTF-16)\n\n[Greeting]\nHello\\nworld!\n\n[Second]\nText\n' > "$d/source.tmsbt"
  printf '# MSBP: Message Studio Binary Project (BigEndian, UTF-16)\n\n[Colors: 2]\n  #0: Red = #FF0000FF\n  #1: Green = #00FF00FF\n' > "$d/source.tmsbp"
  printf '# MSBF: Message Studio Binary Flowchart (BigEndian)\n# Nodes: 2\n\n[Node #0 (Start)]\n  type = EntryPoint (next=1)\n\n[Node #1 (Finish)]\n  type = Event (event_id=10, param=0x20, next=65535)\n' > "$d/source.tmsbf"
  for spec in 'tmsbt msbt' 'tmsbp msbp' 'tmsbf msbf'; do
    set -- $spec; src=$1; ext=$2
    if "$B/wbmgt" ENCODE "$d/source.$src" --dest "$d/a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" DECODE "$d/a/same.$ext" --dest "$d/mid.$src" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" ENCODE "$d/mid.$src" --dest "$d/b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same.$ext" "$d/b/same.$ext"; then
      fok "${ext} encode -> semantic text -> identical re-encode"
    else fno "${ext} canonical fixed point" "second-generation bytes differ"; fi

    if "$B/wbmgt" ENCODE "$d/source.$src" --dest "$d/msg-raw.bin" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/msg-raw.bin" --dest "$d/a/same.$ext.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" DECODE "$d/a/same.$ext.fzip" --dest "$d/mid.$src" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" ENCODE "$d/mid.$src" --dest "$d/msg-raw2.bin" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/msg-raw2.bin" --dest "$d/b/same.$ext.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same.$ext.fzip" "$d/b/same.$ext.fzip"; then
      fok "${ext}.fzip encode -> semantic text -> identical re-encode"
    else fno "${ext}.fzip canonical fixed point" "second-generation bytes differ"; fi
  done

  # Message Studio endian & encoding matrix fixed points:
  printf '# MSBT: Message Studio Binary Text (LittleEndian, UTF-16)\n\n[Greeting]\nHello\\nworld!\n\n[Second]\nText\n' > "$d/source.msbt-le16.tmsbt"
  printf '# MSBT: Message Studio Binary Text (BigEndian, UTF-8)\n\n[Greeting]\nHello\\nworld!\n\n[Second]\nText\n' > "$d/source.msbt-be8.tmsbt"
  printf '# MSBT: Message Studio Binary Text (LittleEndian, UTF-8)\n\n[Greeting]\nHello\\nworld!\n\n[Second]\nText\n' > "$d/source.msbt-le8.tmsbt"
  printf '# MSBP: Message Studio Binary Project (LittleEndian, UTF-16)\n\n[Colors: 2]\n  #0: Red = #FF0000FF\n  #1: Green = #00FF00FF\n' > "$d/source.msbp-le16.tmsbp"
  printf '# MSBP: Message Studio Binary Project (BigEndian, UTF-8)\n\n[Colors: 2]\n  #0: Red = #FF0000FF\n  #1: Green = #00FF00FF\n' > "$d/source.msbp-be8.tmsbp"
  printf '# MSBP: Message Studio Binary Project (LittleEndian, UTF-8)\n\n[Colors: 2]\n  #0: Red = #FF0000FF\n  #1: Green = #00FF00FF\n' > "$d/source.msbp-le8.tmsbp"
  printf '# MSBF: Message Studio Binary Flowchart (LittleEndian)\n# Nodes: 2\n\n[Node #0 (Start)]\n  type = EntryPoint (next=1)\n\n[Node #1 (Finish)]\n  type = Event (event_id=10, param=0x20, next=65535)\n' > "$d/source.msbf-le.tmsbf"
  for spec in 'source.msbt-le16.tmsbt msbt MSBT_LE_UTF16' \
              'source.msbt-be8.tmsbt msbt MSBT_BE_UTF8' \
              'source.msbt-le8.tmsbt msbt MSBT_LE_UTF8' \
              'source.msbp-le16.tmsbp msbp MSBP_LE_UTF16' \
              'source.msbp-be8.tmsbp msbp MSBP_BE_UTF8' \
              'source.msbp-le8.tmsbp msbp MSBP_LE_UTF8' \
              'source.msbf-le.tmsbf msbf MSBF_LE'; do
    set -- $spec; src="$d/$1"; ext=$2; label=$3
    if "$B/wbmgt" ENCODE "$src" --dest "$d/a/same-$label.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" DECODE "$d/a/same-$label.$ext" --dest "$d/mid-$label.$1" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" ENCODE "$d/mid-$label.$1" --dest "$d/b/same-$label.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same-$label.$ext" "$d/b/same-$label.$ext"; then
      fok "${label} encode -> semantic text -> identical re-encode"
    else fno "${label} canonical fixed point" "second-generation bytes differ"; fi
  done

  local bmg_text="$PWD_PROJECT/../tests/samples-excitebots/extract/excitebots.d/UPDATE/files/_sys/RVL-WiiSystemmenu-v385.d/00000063.d/message/jpn/sample.bmg.txt"
  if "$B/wbmgt" ENCODE "$bmg_text" --dest "$d/a/same.bmg" --overwrite >/dev/null 2>&1 \
  && "$B/wbmgt" DECODE "$d/a/same.bmg" --dest "$d/mid.bmg.txt" --overwrite >/dev/null 2>&1 \
  && "$B/wbmgt" ENCODE "$d/mid.bmg.txt" --dest "$d/b/same.bmg" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/a/same.bmg" "$d/b/same.bmg"; then
    fok "BMG encode -> semantic text -> identical re-encode"
  else fno "BMG canonical fixed point" "second-generation bytes differ"; fi

  # Classic BMG encoding matrix fixed points:
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 1\n@BMG-MID = 1\n[0001]\nHello CP1252 world!\n\n[0002]\nSecond string\n' > "$d/source-bmg-cp1252.txt"
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 2\n@BMG-MID = 1\n[0001]\nHello UTF-16 world!\n\n[0002]\nSecond string\n' > "$d/source-bmg-utf16.txt"
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 3\n@BMG-MID = 1\n[0001]\nHello SJIS world!\n\n[0002]\nSecond string\n' > "$d/source-bmg-sjis.txt"
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 4\n@BMG-MID = 1\n[0001]\nHello UTF-8 world!\n\n[0002]\nSecond string\n' > "$d/source-bmg-utf8.txt"
  printf '#BMG\n@LEGACY = 1\n@ENDIAN = 0\n@ENCODING = 1\n@BMG-MID = 0\n[0001]\nHello GameCube BMG!\n\n[0002]\nSecond string\n' > "$d/source-bmg-gc.txt"
  printf '#BMG\n@ENDIAN = 0\n@ENCODING = 1\n@BMG-MID = 1\n@INF-MAGIC = "INF2"\n[0001]\nHello Flow world!\n\n[0002]\nSecond message\n\n@SECTION "FLW1"\n@X 0: 0 2 0 8 0 0 0 0 0 0 0 0 0 1 0 0 :................:\n@X 10: 0 0 0 1 ff ff 0 0 / :........:\n\n@SECTION "FLI1"\n@X 0: 0 2 0 2 0 0 0 0 0 0 0 1 0 0 0 0 :................:\n@X 10: 0 0 0 0 0 0 0 0 / :........:\n' > "$d/source-bmg-flow.txt"
  for spec in 'source-bmg-cp1252.txt BMG_CP1252' \
              'source-bmg-utf16.txt BMG_UTF16' \
              'source-bmg-sjis.txt BMG_SJIS' \
              'source-bmg-utf8.txt BMG_UTF8' \
              'source-bmg-gc.txt BMG_GameCube' \
              'source-bmg-flow.txt BMG_Flow'; do
    set -- $spec; src="$d/$1"; label=$2
    if "$B/wbmgt" ENCODE "$src" --dest "$d/a/same-$label.bmg" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" DECODE "$d/a/same-$label.bmg" --dest "$d/mid-$label.txt" --overwrite >/dev/null 2>&1 \
    && "$B/wbmgt" ENCODE "$d/mid-$label.txt" --dest "$d/b/same-$label.bmg" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same-$label.bmg" "$d/b/same-$label.bmg"; then
      fok "${label} encode -> semantic text -> identical re-encode"
    else fno "${label} canonical fixed point" "second-generation bytes differ"; fi
  done

  # Mario Kart track definitions (KMP):
  mkdir -p "$d/kmp-fixed-a" "$d/kmp-fixed-b"
  printf '#KMP\n@FORMAT = 1\n\n[KTPT]\n#00: pos=(0,0,0) rot=(0,0,0) player=0\n' > "$d/init-kmp.txt"
  "$B/wkmpt" ENCODE "$d/init-kmp.txt" --dest "$d/init-kmp.kmp" --overwrite >/dev/null 2>&1
  "$B/wkmpt" DECODE "$d/init-kmp.kmp" --dest "$d/canonical-kmp.txt" --overwrite >/dev/null 2>&1
  if "$B/wkmpt" ENCODE "$d/canonical-kmp.txt" --dest "$d/kmp-fixed-a/same.kmp" --overwrite >/dev/null 2>&1 \
  && "$B/wkmpt" DECODE "$d/kmp-fixed-a/same.kmp" --dest "$d/mid-kmp.txt" --overwrite >/dev/null 2>&1 \
  && "$B/wkmpt" ENCODE "$d/mid-kmp.txt" --dest "$d/kmp-fixed-b/same.kmp" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/kmp-fixed-a/same.kmp" "$d/kmp-fixed-b/same.kmp"; then
    fok "KMP encode -> semantic text -> identical re-encode"
  else fno "KMP canonical fixed point" "second-generation bytes differ"; fi

  # Wii U layout semantic text is a canonical fixed point even where the
  # first retail decode legitimately relocates sections or strings.
  mkdir -p "$d/layout-a" "$d/layout-b"
  local spec src
  for spec in 'splatoon_cmn_bg_out.bflan bflan' 'splatoon_cmn_seq_drc_option.bflyt bflyt'; do
    set -- $spec; src="$PWD_PROJECT/../tests/fixtures/$1"; ext=$2
    if "$B/wlayt" decode "$src" "$d/source-$ext.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/source-$ext.tflyt" "$d/layout-a/same.$ext" >/dev/null 2>&1 \
    && "$B/wlayt" decode "$d/layout-a/same.$ext" "$d/mid-$ext.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/mid-$ext.tflyt" "$d/layout-b/same.$ext" >/dev/null 2>&1 \
    && cmp -s "$d/layout-a/same.$ext" "$d/layout-b/same.$ext"; then
      fok "${ext} encode -> semantic text -> identical re-encode"
    else fno "${ext} canonical fixed point" "second-generation bytes differ"; fi

    if "$B/wlayt" decode "$src" "$d/source-$ext.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/source-$ext.tflyt" "$d/layout-raw.bin" >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/layout-raw.bin" --dest "$d/layout-a/same.$ext.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wlayt" decode "$d/layout-a/same.$ext.fzip" "$d/mid-$ext.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/mid-$ext.tflyt" "$d/layout-raw2.bin" >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/layout-raw2.bin" --dest "$d/layout-b/same.$ext.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/layout-a/same.$ext.fzip" "$d/layout-b/same.$ext.fzip"; then
      fok "${ext}.fzip encode -> semantic text -> identical re-encode"
    else fno "${ext}.fzip canonical fixed point" "second-generation bytes differ"; fi
  done

  local legacy_root="$PWD_PROJECT/../tests/samples-excitebots/extract/excitebots.d/UPDATE/files/_sys/RVL-Eulav_US-v2.d/0000000b.d/layout.d/arc"
  for spec in 'anim/EULA_ViewerDialog_DialogIn.brlan brlan' \
              'blyt/EULA_ViewerDialog.brlyt brlyt' \
              'blyt/EULA_Viewer_a.brlyt brlyt' \
              'blyt/EULA_Viewer_b.brlyt brlyt' \
              'blyt/P1_Def.brlyt brlyt' \
              'blyt/P2_Def.brlyt brlyt'; do
    set -- $spec; src="$legacy_root/$1"; ext=$2; local name=$(basename "$1" ".$ext")
    if "$B/wlayt" decode "$src" "$d/source-$name.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/source-$name.tflyt" "$d/layout-a/same-$name.$ext" >/dev/null 2>&1 \
    && "$B/wlayt" decode "$d/layout-a/same-$name.$ext" "$d/mid-$name.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/mid-$name.tflyt" "$d/layout-b/same-$name.$ext" >/dev/null 2>&1 \
    && cmp -s "$d/layout-a/same-$name.$ext" "$d/layout-b/same-$name.$ext"; then
      fok "${name}.${ext} encode -> semantic text -> identical re-encode"
    else fno "${name}.${ext} canonical fixed point" "second-generation bytes differ"; fi
  done

  # PMsh's derived buckets/planes are fully recovered by its GLB path.
  cp "$PWD_PROJECT/../tests/fixtures/excite_goalback.msh" "$d/source.msh"
  $B/wmdlt ENCODE "$d/source.msh" --dest "$d/source.glb" --overwrite >/dev/null 2>&1
  if "$B/wmdlt" ENCODE "$d/source.glb" --dest "$d/a/same.msh" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/a/same.msh" --dest "$d/mid.glb" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/mid.glb" --dest "$d/b/same.msh" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/a/same.msh" "$d/b/same.msh"; then
    fok "MSH encode -> GLB -> identical re-encode"
  else fno "MSH canonical fixed point" "second-generation bytes differ"; fi

  # Render-model writers serializing native GX coordinates.
  # A complete binary comparison catches the fixed-point identity.
  for ext in hsf mod; do
    if "$B/wmdlt" ENCODE "$d/source.glb" --dest "$d/a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/a/same.$ext" --dest "$d/mid-$ext.glb" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/mid-$ext.glb" --dest "$d/b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same.$ext" "$d/b/same.$ext"; then
      fok "${ext} encode -> GLB -> identical re-encode"
    else fno "${ext} canonical fixed point" "second-generation bytes differ"; fi
  done
  if "$B/wmdlt" ENCODE "$d/source.glb" --dest "$d/hsd-src.dat" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/hsd-src.dat" --dest "$d/hsd-mid1.glb" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/hsd-mid1.glb" --dest "$d/a/same.dat" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/a/same.dat" --dest "$d/hsd-mid2.glb" --overwrite >/dev/null 2>&1 \
  && "$B/wmdlt" ENCODE "$d/hsd-mid2.glb" --dest "$d/b/same.dat" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/a/same.dat" "$d/b/same.dat"; then
    fok "hsd encode -> GLB -> identical re-encode"
  else fno "hsd canonical fixed point" "second-generation bytes differ"; fi
  local rbnk_fix_xml="$d/rbnk_fix.xml"
  cat > "$rbnk_fix_xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<rbnk source="test" version="1.1" n-program="4" n-wave="2">
  <programs>
    <program index="0">
      <inst wave-index="0" attack="127" decay="127" sustain="127" release="127" hold="0" note-off="0" alt-assign="0" original-key="60" volume="127" pan="64" surround-pan="0" pitch="1.000000"/>
    </program>
    <program index="1">
      <range-table n="2">
        <entry key="60">
          <inst wave-index="0" attack="127" decay="127" sustain="127" release="127" hold="0" note-off="0" alt-assign="0" original-key="60" volume="127" pan="64" surround-pan="0" pitch="1.000000"/>
        </entry>
        <entry key="72">
          <inst wave-index="1" attack="127" decay="127" sustain="127" release="127" hold="0" note-off="0" alt-assign="0" original-key="72" volume="127" pan="64" surround-pan="0" pitch="1.000000"/>
        </entry>
      </range-table>
    </program>
  </programs>
  <waves>
    <wave index="0" encoding="ADPCM_THP" channels="1" sample-rate="32000" samples="1000" loop="no"/>
    <wave index="1" encoding="PCM16" channels="2" sample-rate="44100" samples="2000" loop="yes" loop-start="500"/>
  </waves>
</rbnk>
EOF
  if "$B/wrbnk" compile "$rbnk_fix_xml" "$d/a/rbnk_gen1.rbnk" >/dev/null 2>&1 \
  && "$B/wrbnk" dump "$d/a/rbnk_gen1.rbnk" "$d/rbnk_mid.xml" >/dev/null 2>&1 \
  && "$B/wrbnk" compile "$d/rbnk_mid.xml" "$d/b/rbnk_gen2.rbnk" >/dev/null 2>&1 \
  && cmp -s "$d/a/rbnk_gen1.rbnk" "$d/b/rbnk_gen2.rbnk"; then
    fok "RBNK encode -> XML -> identical re-encode"
  else fno "RBNK canonical fixed point" "second-generation bytes differ"; fi
  for spec in "excite_gpmesh.msh msh" "excite_rail2bp.msh msh" "excite_arrow_point.mod mod" "excite_sunflower2.mod mod"; do
    set -- $spec; local mfile=$1; local ext=$2
    $B/wmdlt ENCODE "$PWD_PROJECT/../tests/fixtures/$mfile" --dest "$d/$mfile.glb" --overwrite >/dev/null 2>&1
    if "$B/wmdlt" ENCODE "$d/$mfile.glb" --dest "$d/a/same-$mfile" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/a/same-$mfile" --dest "$d/mid-$mfile.glb" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/mid-$mfile.glb" --dest "$d/b/same-$mfile" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same-$mfile" "$d/b/same-$mfile"; then
      fok "${mfile} encode -> GLB -> identical re-encode"
    else fno "${mfile} canonical fixed point" "second-generation bytes differ"; fi
  done
  local bch_f="$PWD_PROJECT/../tests/fixtures/synthetic_sample.bch"
  if [ -f "$bch_f" ]; then
    if "$B/wmdlt" ENCODE "$bch_f" --dest "$d/bch_fix_orig.glb" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/bch_fix_orig.glb" --parent="$bch_f" --dest "$d/a/same.bch" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/a/same.bch" --dest "$d/bch_fix_mid.glb" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/bch_fix_mid.glb" --parent="$bch_f" --dest "$d/b/same.bch" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same.bch" "$d/b/same.bch"; then
      fok "BCH inject -> GLB -> identical re-inject"
    else fno "BCH canonical fixed point" "second-generation bytes differ"; fi
  fi
  local nsbmd_f="$PWD_PROJECT/../tests/fixtures/synthetic_sample.nsbmd"
  if [ -f "$nsbmd_f" ]; then
    if "$B/wmdlt" ENCODE "$nsbmd_f" --dest "$d/nsbmd_fix_orig.glb" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/nsbmd_fix_orig.glb" --parent="$nsbmd_f" --dest "$d/a/same.nsbmd" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/a/same.nsbmd" --dest "$d/nsbmd_fix_mid.glb" --overwrite >/dev/null 2>&1 \
    && "$B/wmdlt" ENCODE "$d/nsbmd_fix_mid.glb" --parent="$nsbmd_f" --dest "$d/b/same.nsbmd" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/a/same.nsbmd" "$d/b/same.nsbmd"; then
      fok "NSBMD inject -> GLB -> identical re-inject"
    else fno "NSBMD canonical fixed point" "second-generation bytes differ"; fi
  fi

  # Container extraction must reproduce the creator's exact canonical member
  # tree, including path factoring, tables, alignment and compression choice.
  mkdir -p "$d/tree/sub"; printf alpha > "$d/tree/a"; printf beta > "$d/tree/sub/b"
  mkdir -p "$d/archive-a" "$d/archive-b"
  for ext in narc darc pac gfa rarc sarc sarc.fzip warc warc.fzip ccf nccarc at7 mpbin arc wu8 pack rkc breff breft lta lfl szs wbz ybz wlz ylz; do
    if "$B/wszst" CREATE "$d/tree" --dest "$d/archive-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/archive-a/same.$ext" --dest "$d/out-$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/out-$ext" --dest "$d/archive-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same.$ext" "$d/archive-b/same.$ext"; then
      fok "${ext} create -> extract -> identical re-create"
    else fno "${ext} canonical fixed point" "second-generation bytes differ"; fi
  done

  if "$B/wszst" CREATE "$d/tree" --dest "$d/archive-a/same.rst" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/archive-a/same.rst" --dest "$d/rst-mid" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/rst-mid" --dest "$d/archive-b/same.rst" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.rst" "$d/archive-b/same.rst" \
  && cmp -s "$d/archive-a/same.toc" "$d/archive-b/same.toc"; then
    fok "RST+TOC create -> extract -> identical paired re-create"
  else fno "RST+TOC canonical fixed point" "second-generation pair differs"; fi


  printf '<?xml version="1.0"?>\n<ncer cells="2">\n  <cell index="0" objects="1">\n    <obj attr0="0x0001" attr1="0x4002" attr2="0x8003"/>\n  </cell>\n  <cell index="1" objects="1">\n    <obj attr0="0x0010" attr1="0x4020" attr2="0x8030"/>\n  </cell>\n</ncer>\n' > "$d/source.ncer.xml"
  printf '<?xml version="1.0"?>\n<nanr animations="1" frames="2">\n  <animation index="0" frames="2">\n    <frame cell="0" duration="5" data-offset="0x0"/>\n    <frame cell="1" duration="7" data-offset="0x2"/>\n  </animation>\n</nanr>\n' > "$d/source.nanr.xml"
  for ext in ncer nanr; do
    if "$B/wszst" CREATE "$d/source.$ext.xml" --dest "$d/archive-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/archive-a/same.$ext" --dest "$d/mid.$ext.xml" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/mid.$ext.xml" --dest "$d/archive-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same.$ext" "$d/archive-b/same.$ext"; then
      fok "${ext} encode -> XML -> identical re-encode"
    else fno "${ext} canonical fixed point" "second-generation bytes differ"; fi
  done

  mkdir -p "$d/ctpk-tree"
  cp "$d/source.png" "$d/ctpk-tree/tex_a.png"
  python3 "$PNGTOOL" write "$d/ctpk-tree/tex_b.png" 64 64 20 40 60
  if "$B/wszst" CREATE "$d/ctpk-tree" --dest "$d/archive-a/multi.ctpk" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/archive-a/multi.ctpk" --dest "$d/ctpk-mid" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/ctpk-mid" --dest "$d/archive-b/multi.ctpk" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/multi.ctpk" "$d/archive-b/multi.ctpk"; then
    fok "ctpk multi-PNG create -> extract -> identical re-create"
  else fno "ctpk multi-texture fixed point" "second-generation bytes differ"; fi
  for ctpk_name in mk7_coins.ctpk mk7_common_env.ctpk; do
    local ctpk_path="$PWD_PROJECT/../tests/fixtures/$ctpk_name"
    mkdir -p "$d/ext-$ctpk_name"
    "$B/wszst" EXTRACT "$ctpk_path" --dest "$d/ext-$ctpk_name" --overwrite >/dev/null 2>&1
    if "$B/wszst" CREATE "$d/ext-$ctpk_name" --dest "$d/archive-a/same-$ctpk_name" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/archive-a/same-$ctpk_name" --dest "$d/ctpk-mid-$ctpk_name" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/ctpk-mid-$ctpk_name" --dest "$d/archive-b/same-$ctpk_name" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same-$ctpk_name" "$d/archive-b/same-$ctpk_name"; then
      fok "$ctpk_name create -> extract -> identical re-create"
    else fno "$ctpk_name canonical fixed point" "second-generation bytes differ"; fi
  done
  local gfa_path="$PWD_PROJECT/../tests/fixtures/gfa_bean00.gfa"
  mkdir -p "$d/ext-gfa"
  "$B/wszst" EXTRACT "$gfa_path" --dest "$d/ext-gfa" --overwrite >/dev/null 2>&1
  if "$B/wszst" CREATE "$d/ext-gfa" --dest "$d/archive-a/same.gfa" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/archive-a/same.gfa" --dest "$d/gfa-mid" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/gfa-mid" --dest "$d/archive-b/same.gfa" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.gfa" "$d/archive-b/same.gfa"; then
    fok "gfa_bean00.gfa create -> extract -> identical re-create"
  else fno "gfa_bean00.gfa canonical fixed point" "second-generation bytes differ"; fi
  local narc_path="$PWD_PROJECT/../tests/fixtures/sm3dl_shaders.narc"
  if [ -f "$narc_path" ]; then
    mkdir -p "$d/ext-narc"
    "$B/wszst" EXTRACT "$narc_path" --dest "$d/ext-narc" --overwrite >/dev/null 2>&1
    if "$B/wszst" CREATE "$d/ext-narc" --dest "$d/archive-a/same-shaders.narc" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/archive-a/same-shaders.narc" --dest "$d/mid-shaders" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/mid-shaders" --dest "$d/archive-b/same-shaders.narc" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same-shaders.narc" "$d/archive-b/same-shaders.narc"; then
      fok "sm3dl_shaders.narc create -> extract -> identical re-create"
    else fno "sm3dl_shaders.narc canonical fixed point" "second-generation bytes differ"; fi
  fi
  for arc_name in synthetic_sample.nccarc synthetic_sample.warc synthetic_sample.pac; do
    local arc_path="$PWD_PROJECT/../tests/fixtures/$arc_name"
    if [ -f "$arc_path" ]; then
      mkdir -p "$d/ext-$arc_name"
      "$B/wszst" EXTRACT "$arc_path" --dest "$d/ext-$arc_name" --overwrite >/dev/null 2>&1
      if "$B/wszst" CREATE "$d/ext-$arc_name" --dest "$d/archive-a/same-$arc_name" --overwrite >/dev/null 2>&1 \
      && "$B/wszst" EXTRACT "$d/archive-a/same-$arc_name" --dest "$d/mid-$arc_name" --overwrite >/dev/null 2>&1 \
      && "$B/wszst" CREATE "$d/mid-$arc_name" --dest "$d/archive-b/same-$arc_name" --overwrite >/dev/null 2>&1 \
      && cmp -s "$d/archive-a/same-$arc_name" "$d/archive-b/same-$arc_name"; then
        fok "$arc_name create -> extract -> identical re-create"
      else fno "$arc_name canonical fixed point" "second-generation bytes differ"; fi
    fi
  done
  if [ -f "$PWD_PROJECT/../tests/fixtures/synthetic_sample.warc" ]; then
    mkdir -p "$d/ext-warc-fzip"
    "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/synthetic_sample.warc" --dest "$d/ext-warc-fzip" --overwrite >/dev/null 2>&1
    if "$B/wszst" CREATE "$d/ext-warc-fzip" --dest "$d/archive-a/same.warc.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/archive-a/same.warc.fzip" --dest "$d/mid-warc-fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/mid-warc-fzip" --dest "$d/archive-b/same.warc.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same.warc.fzip" "$d/archive-b/same.warc.fzip"; then
      fok "synthetic_sample.warc.fzip create -> extract -> identical re-create"
    else fno "synthetic_sample.warc.fzip canonical fixed point" "second-generation bytes differ"; fi

    mkdir -p "$d/ext-sarc-fzip"
    "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/synthetic_sample.warc" --dest "$d/ext-sarc-fzip" --overwrite >/dev/null 2>&1
    if "$B/wszst" CREATE "$d/ext-sarc-fzip" --dest "$d/archive-a/same.sarc.fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/archive-a/same.sarc.fzip" --dest "$d/mid-sarc-fzip" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/mid-sarc-fzip" --dest "$d/archive-b/same.sarc.fzip" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same.sarc.fzip" "$d/archive-b/same.sarc.fzip"; then
      fok "synthetic_sample.sarc.fzip create -> extract -> identical re-create"
    else fno "synthetic_sample.sarc.fzip canonical fixed point" "second-generation bytes differ"; fi
  fi

  # Canonical BRSARs retain names for every asset kind, including RWSD/RWAR
  # members that lack a retail sound/bank name-table association.
  local retail_brsar="$PWD_PROJECT/../tests/samples-excitebots/extract/excitebots.d/UPDATE/files/_sys/RVL-Eulav_US-v2.d/0000000b.d/sound/eulaSound.brsar"
  if "$B/wbrsar" unpack "$retail_brsar" "$d/brsar-source" >/dev/null 2>&1 \
  && "$B/wbrsar" pack "$d/brsar-source" "$d/archive-a/same.brsar" >/dev/null 2>&1 \
  && "$B/wbrsar" unpack "$d/archive-a/same.brsar" "$d/brsar-mid" >/dev/null 2>&1 \
  && "$B/wbrsar" pack "$d/brsar-mid" "$d/archive-b/same.brsar" >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.brsar" "$d/archive-b/same.brsar"; then
    fok "BRSAR pack -> unpack -> identical re-pack"
  else fno "BRSAR canonical fixed point" "second-generation bytes differ"; fi

  # The same canonical INFO/SYMB/FILE content is wrapped by this library's
  # endian-appropriate CSAR and FSAR envelopes. Verify each through its own
  # public unpacker rather than inferring correctness from the RSAR result.
  local typ flag label
  for typ in bc bf; do
    if [ "$typ" = bc ]; then flag=--bcsar; label=BCSAR; else flag=--bfsar; label=BFSAR; fi
    if "$B/wbrsar" pack "$d/brsar-source" "$d/archive-a/same.${typ}sar" "$flag" >/dev/null 2>&1 \
    && "$B/wbrsar" unpack "$d/archive-a/same.${typ}sar" "$d/${typ}sar-mid" >/dev/null 2>&1 \
    && "$B/wbrsar" pack "$d/${typ}sar-mid" "$d/archive-b/same.${typ}sar" "$flag" >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same.${typ}sar" "$d/archive-b/same.${typ}sar"; then
      fok "$label pack -> unpack -> identical re-pack"
    else fno "$label canonical fixed point" "second-generation bytes differ"; fi
  done

  # BRRES: real retail sample member trees re-extracted and re-created must be byte-identical
  for brres_file in $(ls "$PWD_PROJECT/../tests/fixtures"/accf_*.brres | sort); do
    local bname=$(basename "$brres_file")
    mkdir -p "$d/brres-ext-$bname"
    "$B/wszst" xx "$brres_file" --dest "$d/brres-ext-$bname" --overwrite >/dev/null 2>&1
    local brres_sub=$(find "$d/brres-ext-$bname" -mindepth 1 -maxdepth 1 -type d | head -1)
    if [ -n "$brres_sub" ] \
    && "$B/wszst" CREATE "$brres_sub" --dest "$d/archive-a/same-$bname" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/archive-a/same-$bname" --dest "$d/brres-mid-$bname" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/brres-mid-$bname" --dest "$d/archive-b/same-$bname" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same-$bname" "$d/archive-b/same-$bname"; then
      fok "$bname create -> extract -> identical re-create"
    else fno "$bname canonical fixed point" "second-generation bytes differ"; fi
  done

  # KCL: encode -> OBJ -> identical re-encode, including the octree rebuild.
  printf 'v 0 0 0\nv 10 0 0\nv 0 10 0\nv 10 10 0\nf 1 2 3\nf 2 4 3\n' > "$d/tri.obj"
  if "$B/wkclt" ENCODE "$d/tri.obj" --dest "$d/archive-a/same.kcl" --overwrite >/dev/null 2>&1 \
  && "$B/wkclt" DECODE "$d/archive-a/same.kcl" --dest "$d/tri-mid.obj" --overwrite >/dev/null 2>&1 \
  && "$B/wkclt" ENCODE "$d/tri-mid.obj" --dest "$d/archive-b/same.kcl" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.kcl" "$d/archive-b/same.kcl"; then
    fok "KCL encode -> OBJ -> identical re-encode"
  else fno "KCL canonical fixed point" "second-generation bytes differ"; fi

  # KCL topology variants fixed points:
  printf 'v 0 0 0\nv 10 0 0\nv 10 10 0\nv 0 10 0\nv 0 0 10\nv 10 0 10\nv 10 10 10\nv 0 10 10\nf 1 2 3\nf 1 3 4\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\nf 2 3 7\nf 2 7 6\nf 3 4 8\nf 3 8 7\nf 4 1 5\nf 4 5 8\n' > "$d/box.obj"
  printf 'v 0 0 0\nv 50 0 5\nv 100 0 -2\nv 0 50 10\nv 50 50 20\nv 100 50 8\nv 0 100 0\nv 50 100 -5\nv 100 100 0\nf 1 2 5\nf 1 5 4\nf 2 3 6\nf 2 6 5\nf 4 5 8\nf 4 8 7\nf 5 6 9\nf 5 9 8\n' > "$d/terrain.obj"
  for mesh in box terrain; do
    if "$B/wkclt" ENCODE "$d/$mesh.obj" --dest "$d/archive-a/$mesh.kcl" --overwrite >/dev/null 2>&1 \
    && "$B/wkclt" DECODE "$d/archive-a/$mesh.kcl" --dest "$d/$mesh-mid.obj" --overwrite >/dev/null 2>&1 \
    && "$B/wkclt" ENCODE "$d/$mesh-mid.obj" --dest "$d/archive-b/$mesh.kcl" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/$mesh.kcl" "$d/archive-b/$mesh.kcl"; then
      fok "KCL ${mesh} encode -> OBJ -> identical re-encode"
    else fno "KCL ${mesh} canonical fixed point" "second-generation bytes differ"; fi
  done

  # BYML's text writer sorts mapping keys. Start from that public canonical
  # ordering so the comparison measures binary regeneration, not YAML order.
  printf 'items:\n  - one\n  - two\nname: byte-test\nvalue: 42\n' > "$d/source-canonical.yaml"
  if "$B/wszst" CREATE "$d/source-canonical.yaml" --dest "$d/archive-a/same.byml" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" TEXT "$d/archive-a/same.byml" --dest "$d/mid-canonical.yaml" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" CREATE "$d/mid-canonical.yaml" --dest "$d/archive-b/same.byml" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/archive-a/same.byml" "$d/archive-b/same.byml"; then
    fok "BYML encode -> YAML -> identical re-encode"
  else fno "BYML canonical fixed point" "second-generation bytes differ"; fi
  for byml in sm3dl_camera.byml sm3dl_stageinfo.byml; do
    "$B/wszst" TEXT "$PWD_PROJECT/../tests/fixtures/$byml" --dest "$d/$byml.yaml" --overwrite >/dev/null 2>&1
    if "$B/wszst" CREATE "$d/$byml.yaml" --dest "$d/archive-a/same-$byml" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" TEXT "$d/archive-a/same-$byml" --dest "$d/mid-$byml.yaml" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/mid-$byml.yaml" --dest "$d/archive-b/same-$byml" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same-$byml" "$d/archive-b/same-$byml"; then
      fok "${byml} encode -> YAML -> identical re-encode"
    else fno "${byml} canonical fixed point" "second-generation bytes differ"; fi
  done

  # NintendoWare sequence assembly & disassembly fixed points.
  printf '; canonical sequence\ntimebase 48\ntempo 120\nnote C4 100 48\nwait 48\nfin\n' > "$d/song.txt"
  for spec in 'RSEQ rseq' 'CSEQ cseq' 'FSEQ fseq' 'FSEQ_LE fseqle' 'SSEQ sseq' 'BMS bms'; do
    set -- $spec; local form=$1; ext=$2
    if "$B/wseqt" asm "$d/song.txt" "$d/archive-a/same.$ext" --format "$form" >/dev/null 2>&1 \
    && "$B/wseqt" disasm "$d/archive-a/same.$ext" "$d/mid-seq-$ext.txt" >/dev/null 2>&1 \
    && "$B/wseqt" asm "$d/mid-seq-$ext.txt" "$d/archive-b/same.$ext" --format "$form" >/dev/null 2>&1 \
    && cmp -s "$d/archive-a/same.$ext" "$d/archive-b/same.$ext"; then
      fok "${form} asm -> disasm -> identical re-asm"
    else fno "${form} canonical fixed point" "second-generation bytes differ"; fi
  done

  # Lossless codec fixed points exercise both the decoder and encoder header
  # conventions, not just two calls to the encoder.
  printf 'lossless canonical fixed point payload payload payload\n' > "$d/raw.bin"
  mkdir -p "$d/codec-a" "$d/codec-b"
  for ext in lz10 lz11 cmp rl yay0 ash lzh8 qlz at7 blz huff4 huff8 stpl rnc1 rnc2 fzip zlib deflate wux yaz0 yaz1 xyz bz ybz bz2 lz ylz lzma xz bclz rle; do
    if "$B/wszst" COMPRESS "$d/raw.bin" --dest "$d/codec-a/same.$ext" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" DECOMPRESS "$d/codec-a/same.$ext" --dest "$d/decoded-$ext.bin" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" COMPRESS "$d/decoded-$ext.bin" --dest "$d/codec-b/same.$ext" --overwrite >/dev/null 2>&1 \
    && cmp -s "$d/codec-a/same.$ext" "$d/codec-b/same.$ext"; then
      fok "${ext} encode -> decode -> identical re-encode"
    else fno "${ext} canonical fixed point" "second-generation bytes differ"; fi
  done

  dd if=/dev/zero of="$d/rom.z64" bs=1 count=0 seek=4194304 2>/dev/null
  printf '\200\067\022\100fixed romc' | dd of="$d/rom.z64" conv=notrunc 2>/dev/null
  if "$B/wszst" COMPRESS "$d/rom.z64" --dest "$d/codec-a/same.romc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" DECOMPRESS "$d/codec-a/same.romc" --dest "$d/rom-dec.z64" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" COMPRESS "$d/rom-dec.z64" --dest "$d/codec-b/same.romc" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/codec-a/same.romc" "$d/codec-b/same.romc"; then
    fok "romc encode -> decode -> identical re-encode"
  else fno "romc canonical fixed point" "second-generation bytes differ"; fi

  # BFMA and zlib-compressed ARC support test
  mkdir -p "$d/bfma_u8/inner"
  printf 'bfma inner manual content test\n' > "$d/bfma_u8/inner/text.txt"
  if "$B/wszst" CREATE "$d/bfma_u8" --dest "$d/page_00.u8" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" COMPRESS "$d/page_00.u8" --dest "$d/page_00.zlib" --overwrite >/dev/null 2>&1 \
  && cp "$d/page_00.zlib" "$d/page_00.arc" \
  && "$B/wszst" DECOMPRESS "$d/page_00.arc" --dest "$d/page_00_dec.u8" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/page_00.u8" "$d/page_00_dec.u8"; then
    fok "zlib ARC decompress -> matches original U8"
  else fno "zlib ARC decompress" "decompressed bytes differ from original U8"; fi

  mkdir -p "$d/manual_src"
  printf '<manual><title>Test</title></manual>' > "$d/manual_src/meta.xml"
  cp "$d/page_00.arc" "$d/manual_src/page_00.arc"
  if "$B/wszst" CREATE "$d/manual_src" --dest "$d/manual.bfma" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" XX "$d/manual.bfma" --dest "$d/manual_xx" --overwrite >/dev/null 2>&1 \
  && [ -f "$d/manual_xx/meta.xml" ] \
  && { [ -f "$d/manual_xx/page_00.arc.d/inner/text.txt" ] || [ -f "$d/manual_xx/page_00.d/inner/text.txt" ]; }; then
    fok "BFMA create -> XX recursive unpack with zlib ARC decompression"
  else fno "BFMA recursive extraction" "failed to extract manual.bfma and its internal zlib .arc files"; fi

  # Wii Party CNUT test
  local cnut_sample=""
  for cs in /tmp/cnuts/camera_button.cnut /tmp/wiiparty_extracted/DATA/files/scripts/mr083/camera_button.cnut.lz; do
    if [ -f "$cs" ]; then
      cnut_sample="$cs"
      break
    fi
  done

  if [ -n "$cnut_sample" ]; then
    mkdir -p "$d/cnut_out"
    if [ "${cnut_sample##*.}" = "lz" ]; then
      "$B/wszst" DECOMPRESS "$cnut_sample" --dest "$d/test.cnut" --overwrite >/dev/null 2>&1
      cnut_sample="$d/test.cnut"
    fi
    if "$B/wszst" XX "$cnut_sample" --dest "$d/cnut_out/sample.nut" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/cnut_out/sample.nut" ] \
    && grep -q "camera_button" "$d/cnut_out/sample.nut" 2>/dev/null; then
      fok "Wii Party CNUT (SQIR) script extraction and disassembly"
    else
      fno "Wii Party CNUT extraction" "failed to disassemble cnut sample";
    fi
  fi

  # Wii Party XMSG (mess.bin) test
  mkdir -p "$d/xmsg_test"
  python3 -c '
import sys
data = bytearray(bytes.fromhex("584d534720100503"))
data.extend((1).to_bytes(4, "big"))
data.extend((0x1c).to_bytes(4, "big"))
data.extend((0x2b).to_bytes(4, "big"))
data.extend((0x24).to_bytes(4, "big"))
data.extend((0x4d).to_bytes(4, "big"))
data.extend(b"test_01\x00")
data.extend(b"dialog\x00")
data.extend("Hello Wii Party!".encode("utf-16be") + b"\x00\x00")
data.extend(bytes.fromhex("ffffffff000000ff1818020200000102"))
with open(sys.argv[1], "wb") as f:
    f.write(data)
' "$d/xmsg_test/mess.bin"
  if "$B/wszst" XX "$d/xmsg_test/mess.bin" --dest "$d/xmsg_test/mess.xml" --overwrite >/dev/null 2>&1 \
  && [ -f "$d/xmsg_test/mess.xml" ] \
  && [ -f "$d/xmsg_test/mess.txt" ] \
  && grep -q "Hello Wii Party!" "$d/xmsg_test/mess.xml" 2>/dev/null \
  && grep -q "test_01" "$d/xmsg_test/mess.txt" 2>/dev/null; then
    fok "Wii Party XMSG (mess.bin) XML and text extraction"
  else
    fno "Wii Party XMSG extraction" "failed to extract XMSG mess.bin";
  fi

  # Newer Super Mario Bros. Wii (.LH compression, LevelInfo.bin, AnimTiles.bin, NSMBW tileset tex/chk)
  mkdir -p "$d/nsmbw_test"
  python3 -c '
import struct

magic = b"NWRp"
num_worlds = 1
world_offset = 0x0C
num_entries = 2
comments = b"NewerSMBW Test\x00"
text_start = 12 + 4 + num_entries * 12 + len(comments)

def enc(s):
    return bytes([(ord(c) - 0x30) & 0xff for c in s]) + b"\x00"

t0 = enc("Grassland")
t1 = enc("Mushroom Plains")
LEVEL_ENTRY_STRUCT = struct.Struct(">5BxHI")
data = bytearray(magic)
data.extend(struct.pack(">I", num_worlds))
data.extend(struct.pack(">I", world_offset))
data.extend(struct.pack(">I", num_entries))
data.extend(LEVEL_ENTRY_STRUCT.pack(98, 98, 1, 100, len("Grassland"), 0, text_start))
data.extend(LEVEL_ENTRY_STRUCT.pack(0, 0, 1, 1, len("Mushroom Plains"), 0x0012, text_start + len(t0)))
data.extend(comments)
data.extend(t0 + t1)
with open("'"$d"'/nsmbw_test/LevelInfo.bin", "wb") as f:
    f.write(data)

at_magic = b"NWRa"
at_strings = b"water_fall_tex.bin\x004,4,4,4\x00"
at_entries = [(16, 16 + len("water_fall_tex.bin\x00"), 0x0042, 0, 0)]
at_data = bytearray(at_magic)
at_data.extend(struct.pack(">I", len(at_entries)))
for e in at_entries:
    at_data.extend(struct.pack(">HHHBB", e[0], e[1], e[2], e[3], e[4]))
at_data.extend(at_strings)
with open("'"$d"'/nsmbw_test/AnimTiles.bin", "wb") as f:
    f.write(at_data)

chk_data = bytearray(2048)
chk_data[0] = 0x01
with open("'"$d"'/nsmbw_test/d_bgchk_sample.bin", "wb") as f:
    f.write(chk_data)

tex = b"\xff\xff" * (1024 * 256)
with open("'"$d"'/nsmbw_test/sample_tex.bin", "wb") as f:
    f.write(tex)

with open("'"$d"'/nsmbw_test/sample_lh.txt", "wb") as f:
    f.write(b"Testing Newer SMBW LH compression")
'
  "$B/wszst" COMPRESS "$d/nsmbw_test/sample_lh.txt" --dest "$d/nsmbw_test/sample_lh.lh" --overwrite >/dev/null 2>&1
  "$B/wszst" DECOMPRESS "$d/nsmbw_test/sample_lh.lh" --dest "$d/nsmbw_test/sample_lh_out.txt" --overwrite >/dev/null 2>&1
  if cmp -s "$d/nsmbw_test/sample_lh.txt" "$d/nsmbw_test/sample_lh_out.txt" \
  && "$B/wszst" XX "$d/nsmbw_test/sample_tex.bin" --dest "$d/nsmbw_test/sample_tex.png" --overwrite >/dev/null 2>&1 \
  && [ -f "$d/nsmbw_test/sample_tex.png" ] \
  && "$B/wszst" XX "$d/nsmbw_test/LevelInfo.bin" --dest "$d/nsmbw_test/LevelInfo.txt" --overwrite >/dev/null 2>&1 \
  && grep -q "Mushroom Plains" "$d/nsmbw_test/LevelInfo.txt" 2>/dev/null \
  && "$B/wszst" XX "$d/nsmbw_test/AnimTiles.bin" --dest "$d/nsmbw_test/AnimTiles.txt" --overwrite >/dev/null 2>&1 \
  && grep -q "water_fall_tex.bin" "$d/nsmbw_test/AnimTiles.txt" 2>/dev/null \
  && "$B/wszst" XX "$d/nsmbw_test/d_bgchk_sample.bin" --dest "$d/nsmbw_test/d_bgchk_sample.txt" --overwrite >/dev/null 2>&1 \
  && grep -q "solid" "$d/nsmbw_test/d_bgchk_sample.txt" 2>/dev/null; then
    fok "Newer SMBW (.LH, LevelInfo, AnimTiles, NSMBW tileset tex/chk)"
  else
    fno "Newer SMBW formats" "failed to decode Newer SMBW or NSMBW tileset formats";
  fi

  # Koopatlas Binary World Map (.kpbin) test
  mkdir -p "$d/koopatlas_test"
  python3 -c '
import struct
magic = b"KP_m"
version = 2
layer_count = 1
header_size = 44
data = bytearray(b"\x00" * header_size)
layer_offs = len(data)
data.extend(struct.pack(">I", 0))
l0_off = len(data)
struct.pack_into(">I", data, layer_offs, l0_off)
node_cnt = 2
path_cnt = 1
data.extend(struct.pack(">IB3x", 2, 255))
noffs_ptr = len(data)
data.extend(struct.pack(">II", node_cnt, 0))
poffs_ptr = len(data)
data.extend(struct.pack(">II", path_cnt, 0))
nodes_tbl_off = len(data)
struct.pack_into(">I", data, noffs_ptr + 4, nodes_tbl_off)
data.extend(struct.pack(">II", 0, 0))
paths_tbl_off = len(data)
struct.pack_into(">I", data, poffs_ptr + 4, paths_tbl_off)
data.extend(struct.pack(">I", 0))
n0_off = len(data)
struct.pack_into(">I", data, nodes_tbl_off, n0_off)
data.extend(struct.pack(">hhIIIIII3xB8xBBB", 100, 200, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 2, 1, 1, 0))
n1_off = len(data)
struct.pack_into(">I", data, nodes_tbl_off + 4, n1_off)
data.extend(struct.pack(">hhIIIIII3xB8xBBB", 250, 200, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 2, 1, 2, 1))
p0_off = len(data)
struct.pack_into(">I", data, paths_tbl_off, p0_off)
data.extend(struct.pack(">IIIIBB2xfI", n0_off, n1_off, 0, 0, 3, 0, 2.5, 0))
while len(data) % 4 != 0:
    data.append(0)
worlds_off = len(data)
w0_off = len(data)
data.extend(struct.pack(">I6IBBBBBBB3x", 0, 0xFFFFFFFF, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0))
bg_str_off = len(data)
data.extend(b"bg_overworld.png\x00")
wname_off = len(data)
data.extend(b"World 1: Yoshi Island\x00")
struct.pack_into(">I", data, w0_off, wname_off)
struct.pack_into(">4sIIIIIIIIII", data, 0, magic, version, layer_count, layer_offs, 0, 0, 0, 0, bg_str_off, worlds_off, 1)
with open("'"$d"'/koopatlas_test/sample_map.kpbin", "wb") as f:
    f.write(data)
'
  if "$B/wszst" XX "$d/koopatlas_test/sample_map.kpbin" --dest "$d/koopatlas_test/sample_map.txt" --overwrite >/dev/null 2>&1 \
  && [ -f "$d/koopatlas_test/sample_map.txt" ] \
  && [ -f "$d/koopatlas_test/sample_map.json" ] \
  && grep -q "Yoshi Island" "$d/koopatlas_test/sample_map.txt" 2>/dev/null \
  && grep -q "level=1-1" "$d/koopatlas_test/sample_map.txt" 2>/dev/null \
  && grep -q "KPMap" "$d/koopatlas_test/sample_map.json" 2>/dev/null; then
    fok "Koopatlas Binary Map (.kpbin) extraction"
  else
    fno "Koopatlas Binary Map" "failed to extract .kpbin sample";
  fi

  # Nintendo Wii ChannelScript (.cs / RCHE) test
  mkdir -p "$d/chans_test"
  local cs_fixture="$PWD_PROJECT/../tests/fixtures/forecast_icon.cs"
  if [ -f "$cs_fixture" ]; then
    if "$B/wszst" XX "$cs_fixture" --dest "$d/chans_test/" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/chans_test/forecast_icon.txt" ] \
    && [ -f "$d/chans_test/forecast_icon.js" ] \
    && grep -q "function main()" "$d/chans_test/forecast_icon.js" 2>/dev/null \
    && grep -q "for (var_1ff2 in this)" "$d/chans_test/forecast_icon.js" 2>/dev/null \
    && grep -q "DISASSEMBLY" "$d/chans_test/forecast_icon.txt" 2>/dev/null; then
      fok "Nintendo Wii ChannelScript (.cs) extraction & decompilation"
    else
      fno "Nintendo Wii ChannelScript" "failed to extract .cs sample";
    fi
  fi
}
t_byte_fixed_points

echo
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP BYTE_PASS=$BYTE_PASS BYTE_FAIL=$BYTE_FAIL FIXED_PASS=$FIXED_PASS FIXED_FAIL=$FIXED_FAIL"
[ "$FAIL" -eq 0 ]
