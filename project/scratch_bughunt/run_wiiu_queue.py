#!/usr/bin/env python3
"""
Automated Wii U extraction test harness.
Runs through all 61 Wii U games from Wikipedia on mcubewiiu: / mcubewiqu:
Pulls images from remote to /Volumes/SSD/wiiu_work/ (APFS, no 4GB limit),
pairs disc keys from /tmp/wiiu_keys/, runs `wszst xx <target> -o -vv`,
parses operations, logs to /Volumes/`/wiiu_logs/, records results to
/Volumes/`/wiiu_test_results.json, and cleans up temp directories after each title.
"""

import os
import sys
import json
import time
import shutil
import subprocess
import glob
import re

QUEUE_FILE = "/Users/larsen/wiimms-szs-tools-plus/project/scratch_bughunt/wiiu_final_queue.json"
RESULTS_FILE = "/Volumes/`/wiiu_test_results.json"
WORK_DIR = "/Volumes/SSD/wiiu_work"
LOGS_DIR = "/Volumes/`/wiiu_logs"
KEYS_DIR = "/Volumes/SSD/wiiu_keys"
WSZST_BIN = "/Users/larsen/wiimms-szs-tools-plus/project/bin/mac/arm/debug/wszst"

SHARED_BUNDLE_PATTERNS = [
    "HomeButton", "strapImage", "UPDATE/files", "P1_Def", "P2_Def", "P3_Def", "P4_Def", "_sys"
]

def format_size(bytes_val):
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if bytes_val < 1024.0:
            return f"{bytes_val:.1f}{unit}"
        bytes_val /= 1024.0
    return f"{bytes_val:.1f}PB"

def get_dir_stats(path):
    total_size = 0
    total_files = 0
    if not os.path.exists(path):
        return 0, "0.0KB"
    for root, dirs, files in os.walk(path):
        total_files += len(files)
        for f in files:
            fp = os.path.join(root, f)
            try:
                if not os.path.islink(fp):
                    total_size += os.path.getsize(fp)
            except OSError:
                pass
    return total_files, format_size(total_size)

def parse_wszst_log(log_text):
    total_ops = 0
    real_ops = 0
    format_counts = {}
    errors = []
    
    op_regex = re.compile(r"^(?:DECODE|DECOMPRESS|EXTRACT|CREATE-TEXT|EXPORT MODEL)\s+([^:]+):(.*)$")
    
    for line in log_text.splitlines():
        line_clean = line.strip()
        if "ERROR" in line_clean or "FAILED" in line_clean:
            if line_clean.startswith("!! wszst: ERROR") or "FATAL" in line_clean:
                errors.append(line_clean)
        
        m = op_regex.match(line_clean)
        if m:
            total_ops += 1
            fmt = m.group(1).strip()
            path_part = m.group(2).strip()
            
            is_shared = any(p in path_part for p in SHARED_BUNDLE_PATTERNS)
            is_passthru = fmt.startswith("passthrough") or fmt == "U8" or fmt == "RAW"
            
            if not is_shared and not is_passthru:
                real_ops += 1
                format_counts[fmt] = format_counts.get(fmt, 0) + 1
    
    top_formats = sorted(format_counts.items(), key=lambda x: x[1], reverse=True)[:5]
    top_fmt_str = ", ".join([f"{k}:{v}" for k, v in top_formats]) if top_formats else "n/a"
    
    return total_ops, real_ops, top_fmt_str, errors

def main():
    os.makedirs(WORK_DIR, exist_ok=True)
    os.makedirs(LOGS_DIR, exist_ok=True)
    
    with open(QUEUE_FILE) as f:
        queue = json.load(f)
        
    results = []
    if os.path.exists(RESULTS_FILE):
        try:
            with open(RESULTS_FILE) as f:
                results = json.load(f)
        except Exception:
            results = []
            
    completed_titles = {r["title"]: r for r in results}
    
    print(f"Starting Wii U extraction test queue: {len(queue)} games total, {len(completed_titles)} already tested.", flush=True)
    print(f"Working directory on SSD: {WORK_DIR}", flush=True)
    print(f"Logs directory on volume: {LOGS_DIR}", flush=True)
    print(f"Results file: {RESULTS_FILE}", flush=True)
    
    for idx, item in enumerate(queue):
        title = item["title"]
        target_file = item["target_file"]
        remote_path = item["remote_path"]
        
        if title in completed_titles:
            print(f"[{idx+1}/{len(queue)}] SKIPPING already tested: {title}", flush=True)
            continue
            
        print(f"\n==========================================", flush=True)
        print(f"[{idx+1}/{len(queue)}] TESTING: {title}", flush=True)
        print(f"File: {target_file}", flush=True)
        print(f"==========================================", flush=True)
        
        # Clean work dir
        for entry in os.listdir(WORK_DIR):
            ep = os.path.join(WORK_DIR, entry)
            if os.path.isdir(ep):
                shutil.rmtree(ep, ignore_errors=True)
            else:
                try:
                    os.remove(ep)
                except OSError:
                    pass
                    
        local_file = os.path.join(WORK_DIR, target_file)
        
        # Step 1: Copy matching disc key if WUX
        if target_file.endswith(".wux"):
            base_key = target_file[:-4] + ".key"
            src_key = os.path.join(KEYS_DIR, base_key)
            if not os.path.exists(src_key):
                # Search fuzzy in keys dir
                prefix = target_file.split('(')[0].strip()
                matches = glob.glob(os.path.join(KEYS_DIR, f"{prefix}*.key"))
                if matches:
                    src_key = matches[0]
            if os.path.exists(src_key):
                dst_key = os.path.join(WORK_DIR, base_key)
                shutil.copy(src_key, dst_key)
                print(f"Placed disc key: {base_key}", flush=True)
            else:
                print(f"Warning: No disc key found for {target_file}", flush=True)
        
        # Step 2: Rclone copy
        t0 = time.time()
        print(f"Pulling from {remote_path}...", flush=True)
        pull_cmd = ["rclone", "copyto", remote_path, local_file]
        res_pull = subprocess.run(pull_cmd, capture_output=True, text=True)
        if res_pull.returncode != 0:
            print(f"Rclone copy failed: {res_pull.stderr}", flush=True)
            continue
            
        print(f"Download complete ({time.time() - t0:.1f}s, {format_size(os.path.getsize(local_file))}).", flush=True)
        
        # Step 3: Run wszst xx with -o
        print(f"Running wszst xx on {target_file}...", flush=True)
        t_start = time.time()
        cmd = [WSZST_BIN, "xx", local_file, "-o", "-vv"]
        log_file_path = os.path.join(LOGS_DIR, f"{idx+1:03d}_{re.sub(r'[^A-Za-z0-9_]', '_', title)}.log")
        
        status_text = "UNKNOWN"
        status_icon = "❓"
        log_content = ""
        
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=1500)
            elapsed = time.time() - t_start
            log_content = res.stdout + "\n" + res.stderr
            ret = res.returncode
            
            if ret == 0:
                status_text = "PASS"
                status_icon = "✅"
            elif ret < 0:
                status_text = f"CRASH_SIG{-ret}"
                status_icon = "❌"
            elif ret > 128:
                status_text = f"CRASH_SIG{ret-128}"
                status_icon = "❌"
            elif ret == 64:
                status_text = "EXIT_64"
                status_icon = "🟡"
            elif ret == 28:
                status_text = "ERROR_EXIT28"
                status_icon = "🟡"
            elif ret == 36:
                status_text = "ERROR_EXIT36"
                status_icon = "🟡"
            elif ret == 66:
                status_text = "ERROR_EXIT66"
                status_icon = "🟡"
            else:
                status_text = f"EXIT_{ret}"
                status_icon = "🟡"
        except subprocess.TimeoutExpired as te:
            elapsed = time.time() - t_start
            status_text = "TIMEOUT"
            status_icon = "❌"
            out_str = te.stdout.decode("utf-8", errors="ignore") if isinstance(te.stdout, bytes) else (te.stdout or "")
            err_str = te.stderr.decode("utf-8", errors="ignore") if isinstance(te.stderr, bytes) else (te.stderr or "")
            log_content = out_str + "\n" + err_str + "\n!! TIMEOUT AFTER 900 SECONDS"
        except Exception as e:
            elapsed = time.time() - t_start
            status_text = f"ERROR_{str(e)[:30]}"
            status_icon = "❌"
            log_content = str(e)
            
        with open(log_file_path, "w") as f:
            f.write(log_content)
            
        # Stats on extracted directory
        disc_d = os.path.join(WORK_DIR, target_file.rsplit('.', 1)[0] + ".d")
        if not os.path.exists(disc_d):
            disc_d = local_file + ".d"
        n_files, sz_str = get_dir_stats(disc_d)
        
        total_ops, real_ops, top_fmt_str, err_list = parse_wszst_log(log_content)
        
        print(f"Status: {status_icon} {status_text} | Real/Total Ops: {real_ops}/{total_ops} | Files: {n_files} ({sz_str}) | Errors: {len(err_list)} | Time: {elapsed:.1f}s", flush=True)
        print(f"Top formats: {top_fmt_str}", flush=True)
        if err_list:
            print(f"Sample error: {err_list[0][:80]}", flush=True)
            
        record = {
            "index": idx + 1,
            "title": title,
            "target_file": target_file,
            "status_icon": status_icon,
            "status_text": status_text,
            "real_ops": real_ops,
            "total_ops": total_ops,
            "top_formats": top_fmt_str,
            "extracted_files": n_files,
            "extracted_size": sz_str,
            "error_count": len(err_list),
            "sample_error": err_list[0] if err_list else "",
            "elapsed_sec": round(elapsed, 1)
        }
        
        results = [r for r in results if r["title"] != title]
        results.append(record)
        
        for entry in os.listdir(WORK_DIR):
            ep = os.path.join(WORK_DIR, entry)
            if os.path.isdir(ep):
                shutil.rmtree(ep, ignore_errors=True)
            else:
                try:
                    os.remove(ep)
                except OSError:
                    pass
                    
        with open(RESULTS_FILE, "w") as f:
            json.dump(results, f, indent=2)

    print("\nCompleted testing all Wii U games in queue!", flush=True)

if __name__ == "__main__":
    main()
