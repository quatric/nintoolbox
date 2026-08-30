#!/usr/bin/env python3
import json
import os
import subprocess
import shutil
import time
import re
import sys
from collections import Counter

WSZST = os.path.abspath("project/bin/wszst")
WORK_DIR = "/tmp/ds_work"
LOG_DIR = "/tmp/ds_logs"
RESULTS_FILE = "/tmp/ds_test_results.json"
QUEUE_FILE = "/tmp/ds_final_queue.json"

os.makedirs(WORK_DIR, exist_ok=True)
os.makedirs(LOG_DIR, exist_ok=True)

with open(QUEUE_FILE, "r") as f:
    queue = json.load(f)

results = []
if os.path.exists(RESULTS_FILE):
    try:
        with open(RESULTS_FILE, "r") as f:
            results = json.load(f)
    except Exception:
        results = []

completed_titles = {r["title"] for r in results if r.get("status_icon") in ["✅", "🟡"]}

print(f"Loaded {len(queue)} total games. Already completed: {len(completed_titles)}", flush=True)

for idx, item in enumerate(queue):
    title = item["title"]
    rclone_file = item["rclone_file"]
    
    if title in completed_titles:
        print(f"[{idx+1}/{len(queue)}] SKIPPING already tested: {title}", flush=True)
        continue
    
    print(f"\n==========================================", flush=True)
    print(f"[{idx+1}/{len(queue)}] TESTING: {title}", flush=True)
    print(f"File: {rclone_file}", flush=True)
    print(f"==========================================", flush=True)
    
    # 1. Download
    zip_path = os.path.join(WORK_DIR, os.path.basename(rclone_file))
    rclone_remote = f"mcubeds:Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/{rclone_file}"
    
    t0 = time.time()
    cmd_dl = ["rclone", "copyto", rclone_remote, zip_path]
    res_dl = subprocess.run(cmd_dl, capture_output=True, text=True)
    if res_dl.returncode != 0:
        print(f"ERROR downloading {rclone_file}: {res_dl.stderr}", flush=True)
        item_res = {
            "title": title,
            "rclone_file": rclone_file,
            "exit_code": -1,
            "status_icon": "❌",
            "status_text": "FAIL_DOWNLOAD",
            "real_ops": 0,
            "total_ops": 0,
            "extracted_files": 0,
            "extracted_size": "0B",
            "top_formats": "",
            "error_count": 1,
            "errors": [res_dl.stderr.strip()],
            "elapsed_sec": round(time.time() - t0, 1)
        }
        results = [r for r in results if r["title"] != title]
        results.append(item_res)
        with open(RESULTS_FILE, "w") as f:
            json.dump(results, f, indent=2)
        continue
    
    # 2. Unzip
    res_unzip = subprocess.run(["unzip", "-o", zip_path, "-d", WORK_DIR], capture_output=True, text=True)
    nds_files = [os.path.join(WORK_DIR, f) for f in os.listdir(WORK_DIR) if f.endswith(".nds")]
    if not nds_files:
        print(f"ERROR: No .nds found after unzipping {zip_path}", flush=True)
        item_res = {
            "title": title,
            "rclone_file": rclone_file,
            "exit_code": -1,
            "status_icon": "❌",
            "status_text": "FAIL_UNZIP",
            "real_ops": 0,
            "total_ops": 0,
            "extracted_files": 0,
            "extracted_size": "0B",
            "top_formats": "",
            "error_count": 1,
            "errors": ["No .nds file found inside zip"],
            "elapsed_sec": round(time.time() - t0, 1)
        }
        results = [r for r in results if r["title"] != title]
        results.append(item_res)
        with open(RESULTS_FILE, "w") as f:
            json.dump(results, f, indent=2)
        if os.path.exists(zip_path):
            os.remove(zip_path)
        continue
    
    nds_file = nds_files[0]
    
    # 3. Run wszst xx
    log_file = os.path.join(LOG_DIR, f"{idx+1:03d}_{re.sub(r'[^a-zA-Z0-9]', '_', title)}.log")
    try:
        with open(log_file, "w") as lf:
            p = subprocess.run(
                [WSZST, "xx", nds_file, "-vv"],
                stdout=lf,
                stderr=subprocess.STDOUT,
                timeout=180,
                cwd=WORK_DIR
            )
            exit_code = p.returncode
    except subprocess.TimeoutExpired:
        exit_code = -999 # Timeout
        with open(log_file, "a") as lf:
            lf.write("\n!! TIMEOUT EXPIRED after 180s !!\n")
    
    elapsed = round(time.time() - t0, 1)
    
    # 4. Analyze log and extracted directory
    stem, _ = os.path.splitext(nds_file)
    stage_dir = f"{stem}.d"
    extracted_bytes = 0
    extracted_files = 0
    if os.path.exists(stage_dir):
        for root, dirs, files in os.walk(stage_dir):
            extracted_files += len(files)
            for f in files:
                try:
                    extracted_bytes += os.path.getsize(os.path.join(root, f))
                except Exception:
                    pass
    
    # Parse operations from log
    ops_counter = Counter()
    total_ops = 0
    real_ops = 0
    error_lines = []
    
    if os.path.exists(log_file):
        with open(log_file, "r", errors="replace") as lf:
            for line in lf:
                line_str = line.strip()
                if line_str.startswith("!! wszst: ERROR"):
                    error_lines.append(line_str)
                elif line_str.startswith("DECODE ") or line_str.startswith("EXTRACT ") or line_str.startswith("DECOMPRESS "):
                    total_ops += 1
                    parts = line_str.split(":", 1)
                    op_type = parts[0].strip()
                    if " " in op_type:
                        fmt = op_type.split(None, 1)[1]
                    else:
                        fmt = op_type
                    ops_counter[fmt] += 1
                    if "passthrough" not in line_str.lower():
                        real_ops += 1
    
    # Format size string
    if extracted_bytes > 1024 * 1024 * 1024:
        size_str = f"{extracted_bytes / (1024**3):.2f}GB"
    elif extracted_bytes > 1024 * 1024:
        size_str = f"{extracted_bytes / (1024**2):.1f}MB"
    else:
        size_str = f"{extracted_bytes / 1024:.1f}KB"
    
    # Status determination
    if exit_code == 0:
        status_icon = "✅"
        status_text = "PASS"
    elif exit_code == -999:
        status_icon = "❌"
        status_text = "TIMEOUT"
    elif exit_code in [28, 36, 38, 66, 82]:
        status_icon = "🟡" if real_ops > 0 or extracted_files > 5 else "❌"
        status_text = f"ERROR_EXIT{exit_code}"
    elif exit_code < 0:
        status_icon = "❌"
        status_text = f"CRASH_SIG{-exit_code}"
    else:
        status_icon = "🟡" if real_ops > 0 or extracted_files > 5 else "❌"
        status_text = f"EXIT_{exit_code}"
    
    top_fmts = ", ".join([f"{k}:{v}" for k, v in ops_counter.most_common(5)])
    
    item_res = {
        "title": title,
        "rclone_file": rclone_file,
        "exit_code": exit_code,
        "status_icon": status_icon,
        "status_text": status_text,
        "real_ops": real_ops,
        "total_ops": total_ops,
        "extracted_files": extracted_files,
        "extracted_size": size_str,
        "top_formats": top_fmts,
        "error_count": len(error_lines),
        "errors": error_lines[:5],
        "elapsed_sec": elapsed
    }
    
    print(f"Status: {status_icon} {status_text} | Real/Total Ops: {real_ops}/{total_ops} | Files: {extracted_files} ({size_str}) | Errors: {len(error_lines)} | Time: {elapsed}s", flush=True)
    if top_fmts:
        print(f"Top formats: {top_fmts}", flush=True)
    if error_lines:
        print(f"Sample error: {error_lines[0]}", flush=True)
    
    results = [r for r in results if r["title"] != title]
    results.append(item_res)
    with open(RESULTS_FILE, "w") as f:
        json.dump(results, f, indent=2)
    
    # 5. Clean up work dir
    for f in os.listdir(WORK_DIR):
        full_p = os.path.join(WORK_DIR, f)
        try:
            if os.path.isdir(full_p):
                shutil.rmtree(full_p)
            else:
                os.remove(full_p)
        except Exception as e:
            print(f"Warning cleaning {full_p}: {e}", flush=True)

print(f"\nCompleted testing {len(results)}/{len(queue)} games!", flush=True)
