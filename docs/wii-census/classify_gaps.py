#!/usr/bin/env python3
"""Classify every `F` (not decoded) group in the census.

usage: classify_gaps.py [--csv out.csv]

Per (extension, detected type) it prints games / files / MB and one class:
  raw-by-design  xx leaves it raw on purpose (README "Audit artifacts")
  needs-sibling  fails when the audit copies one file alone; re-check with siblings
  documented     extension appears in docs/FORMATS.md, so a later extractor may cover it: re-audit
  unknown        detected type is `?` or the extension is not in FORMATS.md: real gap candidate
Reads only the census JSON and FORMATS.md; classes are heuristics, not proof.
"""
import csv, json, os, re, sys, collections

here = os.path.dirname(os.path.abspath(__file__))
census = json.load(open(os.path.join(here, "wii-format-census.json")))
formats_md = open(os.path.join(here, "..", "FORMATS.md"), encoding="utf-8", errors="replace").read().lower()

RAW = {"brfnt", "brfna"}
SIBLING = {"spd", "spt", "dir", "msh", "vtx"}

def klass(ext, typ):
    if ext in RAW:
        return "raw-by-design"
    if ext in SIBLING:
        return "needs-sibling"
    if typ != "?" and typ:
        return "documented"  # a named type means the sniffer knows it; xx failed on it
    # shortcut: substring match, a short ext like "bf" matches loosely; tighten if it misleads
    if re.search(r"\.%s\b" % re.escape(ext), formats_md):
        return "documented"
    return "unknown"

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
    if r[5] >= 50 or r[2] == "unknown":
        print(f"{r[0]:10}{r[1]:16}{r[2]:15}{r[3]:>6}{r[4]:>9}{r[5]:>9}")
print("\nMB by class:", dict(tot))
no_audit = [k for k, v in census.items() if not v.get("audited")]
print("never re-audited:", no_audit)
