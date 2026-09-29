#!/usr/bin/env python3
"""Compare every Le-signature file with CUE's reference LZE decoder.

Reference: https://github.com/WonderfulToolchain/wf-nnpack/blob/master/lze.c
Only temporary output files are written; source resources are never changed.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def audit(path, binary, reference):
    with tempfile.TemporaryDirectory(prefix='lze-audit-') as tmp:
        root = Path(tmp)
        expected, actual = root / 'reference.bin', root / 'actual.bin'
        for command in ([str(reference), '-d', str(path), str(expected)],
                        [str(binary), 'DECOMPRESS', str(path), '-d', str(actual)]):
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if result.returncode or 'WARNING' in result.stdout:
                raise ValueError(f'{path}: {result.stdout} {result.stderr}')
        want, got = expected.read_bytes(), actual.read_bytes()
        if want != got:
            raise ValueError(f'{path}: decoded bytes differ')
        return {'path': str(path), 'size': len(got), 'sha256': hashlib.sha256(got).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--binary', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'project/bin/wszst')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not args.root.is_dir() or not args.reference.is_file() or args.jobs < 1:
        parser.error('root/reference must exist and jobs must be positive')
    paths = []
    for path in sorted(args.root.rglob('*')):
        if path.is_file():
            with path.open('rb') as source:
                if source.read(2) == b'Le':
                    paths.append(path.resolve())
    if not paths:
        parser.error('no LZE resources found')
    results, failures = [], []
    with tempfile.TemporaryDirectory(prefix='lze-tool-') as tmp:
        binary = Path(tmp) / 'wszst'
        shutil.copy2(args.binary, binary)
        def check(path):
            try:
                return audit(path, binary, args.reference.resolve()), None
            except Exception as error:
                return None, str(error)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for i, (result, error) in enumerate(pool.map(check, paths), 1):
                if error:
                    failures.append(error)
                else:
                    results.append(result)
                if i % 100 == 0:
                    print(f'{i}/{len(paths)} streams checked', flush=True)
    report = {'streams': len(paths), 'extensions': dict(Counter(p.suffix for p in paths)),
              'decoded_bytes': sum(r['size'] for r in results),
              'failures': failures, 'results': results}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(paths)} streams, {report["decoded_bytes"]} decoded bytes, {len(failures)} failures')
    for error in failures[:10]:
        print(error)
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
