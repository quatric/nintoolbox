#!/usr/bin/env python3
"""Merge freshly re-audited games into the census.

usage: merge_audit.py <sweep dir> [YYYY-MM-DD]

<sweep dir> holds `formats.csv` (rows written by the audit run) and `detail/<id6>.json` (same layout as
wii-format-census.json entries). Fresh entries replace the stale ones; every row gets an `audited` column
(the date of the re-audit, empty for rows still from the original census).
"""
import csv, json, os, sys, datetime

here = os.path.dirname(os.path.abspath(__file__))
sweep = sys.argv[1]
day = sys.argv[2] if len(sys.argv) > 2 else datetime.date.today().isoformat()

cj = os.path.join(here, "wii-format-census.json")
census = json.load(open(cj))
rows = {}
with open(os.path.join(here, "formats.csv"), newline="") as f:
    rd = csv.reader(f)
    next(rd)
    for r in rd:
        rows[r[0]] = r[:4] + [r[4] if len(r) > 4 else ""]

fresh = {}
with open(os.path.join(sweep, "formats.csv"), newline="") as f:
    rd = csv.reader(f)
    next(rd)
    for r in rd:
        fresh[r[0]] = r[:4]

n = 0
for id6, r in fresh.items():
    det = os.path.join(sweep, "detail", id6 + ".json")
    if not os.path.exists(det):
        continue
    d = json.load(open(det))
    d["audited"] = day
    census[id6] = d
    rows[id6] = r + [day]
    n += 1

json.dump(census, open(cj, "w"), separators=(",", ":"))
with open(os.path.join(here, "formats.csv"), "w", newline="") as f:
    w = csv.writer(f, lineterminator="\n")
    w.writerow(["id6", "title", "total_mb", "top_formats(ext:count:MB:[ftlabel/result])", "audited"])
    for id6 in sorted(rows, key=lambda k: (rows[k][1], k)):
        w.writerow(rows[id6])
print(f"merged {n} fresh games; {len(rows)} rows, {sum(1 for r in rows.values() if r[4])} audited")
