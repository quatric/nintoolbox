#!/usr/bin/env python3
"""Classify every `F` (not decoded) group in the census.

usage: classify_gaps.py [--csv out.csv]

Per (extension, detected type) it prints games / files / MB and one class:
  raw-by-design  xx leaves it raw on purpose (README "Audit artifacts")
  needs-sibling  fails when the audit copies one file alone; re-check with siblings
  type-supported an extractor gated on this extension names the detected type: re-audit, likely fixed
  ext-covered    an extractor takes this extension but none names the type (or the type is `?`): variant or bug
  type-elsewhere no extractor takes this extension, but one names the detected type: probably routed by magic
  no-ext         no extension: only magic-sniffing extractors could take it
  no-extractor   nothing in the source handles the extension or the type: real gap
Reads the census JSON and the extractor sources (project/src/wszst_cmd); classes are heuristics, not proof.
"""
import csv, json, os, re, sys, collections

here = os.path.dirname(os.path.abspath(__file__))
census = json.load(open(os.path.join(here, "wii-format-census.json")))
src = os.path.join(here, "..", "..", "project", "src", "wszst_cmd")
norm = lambda t: re.sub(r"[^a-z0-9]", "", t.lower())
ext_text = collections.defaultdict(str)  # extension -> normalized source of every extractor gated on it
all_text = ""
for fn in sorted(os.listdir(os.path.join(src, "extract"))):
    if not fn.endswith(".inc"):
        continue
    t = open(os.path.join(src, "extract", fn), encoding="utf-8", errors="replace").read()
    all_text += norm(t)
    for e in set(re.findall(r'is_ext\s*\([^,()]*,\s*"\.([^"]+)"', t)):
        ext_text[e.lower()] += norm(t)
assert "adx" in ext_text,"extractor scan found nothing: wrong path?"

RAW = {"brfnt", "brfna"}
SIBLING = {"spd", "spt", "dir", "msh", "vtx"}

def klass(ext, typ):
    if ext in RAW:
        return "raw-by-design"
    if ext in SIBLING:
        return "needs-sibling"
    t = norm(typ) if typ != "?" else ""
    if ext in ext_text:
        # shortcut: substring match on the type name, a short one like "u8" can hit by accident; tighten if it misleads
        return "type-supported" if t and t in ext_text[ext] else "ext-covered"
    if ext == "(none)":
        return "no-ext"
    return "type-elsewhere" if t and t in all_text else "no-extractor"

agg = collections.defaultdict(lambda: [set(), 0, 0.0])
for id6, g in census.items():
    for ext, n, mb, labels in g["groups"]:
        ls = labels.split(",")
        fs = [l for l in ls if l.endswith("/F")]
        if not fs:
            continue
        share = len(fs) / len(ls)  # label list is per sampled file: scale files/MB by the F share
        for typ in {l.rsplit("/", 1)[0] for l in fs}:
            a = agg[(ext, typ)]
            a[0].add(id6)
            a[1] += round(n * share)
            a[2] += mb * share

rows = sorted(
    ((e, t, klass(e, t), len(a[0]), a[1], round(a[2])) for (e, t), a in agg.items()),
    key=lambda r: -r[5],
)

if "--csv" in sys.argv:
    with open(sys.argv[sys.argv.index("--csv") + 1], "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["ext", "type", "class", "games", "files", "mb"])
        w.writerows(rows)

tot = collections.Counter()
print(f"{'ext':10}{'type':16}{'class':15}{'games':>6}{'files':>9}{'MB':>9}")
for r in rows:
    tot[r[2]] += r[5]
    if r[5] >= 50 or r[2] == "no-extractor":
        print(f"{r[0]:10}{r[1]:16}{r[2]:15}{r[3]:>6}{r[4]:>9}{r[5]:>9}")
print("\nMB by class:", dict(tot))
no_audit = [k for k, v in census.items() if not v.get("audited")]
print("never re-audited:", no_audit)
