#!/usr/bin/env python3
"""Verify ALAR or Pikmin ARC/DIR extraction against every source member.

Usage: python3 tests/audit_retail_corpus.py /path/to/extracted/rom/files --output report.json
The report contains counts and failures; source files are never modified.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
import shutil
import subprocess
import tempfile


def alar_members(data):
    """Independent reference walk: v2 fixed records, v3 absolute record pointers."""
    count = struct.unpack_from('<H', data, 6)[0]
    for index in range(count):
        record = 16 + 16*index if data[4] == 2 else struct.unpack_from('<H', data, 18+2*index)[0]
        offset, size = struct.unpack_from('<II', data, record+4)
        if offset + size > len(data):
            raise ValueError(f'member {index} exceeds source bounds')
        if data[4] == 2:
            name = data[offset-34:offset-2].split(b'\0', 1)[0]
        else:
            name = data[record+18:data.index(0, record+18)]
        name = name.decode('utf-8')
        if not name or name.startswith('/') or '..' in name.split('/'):
            raise ValueError('unsafe or missing member name')
        yield name, data[offset:offset+size]


def pikmin_members(index, data):
    size, count = struct.unpack_from('>II', index)
    if size != len(index):
        raise ValueError('index size does not match header')
    pos = 8
    for number in range(count):
        offset, length, name_size = struct.unpack_from('>III', index, pos)
        pos += 12
        raw_name = index[pos:pos+name_size].split(b'\0', 1)[0]
        try:
            name = raw_name.decode('utf-8')
        except UnicodeDecodeError:
            name = raw_name.decode('shift_jis')
        pos += name_size
        if pos > len(index) or offset+length > len(data):
            raise ValueError(f'member {number} exceeds source bounds')
        if not name or name.startswith('/') or '..' in name.split('/'):
            raise ValueError('unsafe or missing member name')
        yield name, data[offset:offset+length]


def audit(path, binary, kind):
    data = path.read_bytes()
    expected = list(alar_members(data) if kind == 'alar' else
                    pikmin_members(path.with_suffix('.dir').read_bytes(), data))
    with tempfile.TemporaryDirectory(prefix='alar-audit-') as tmp:
        dest = Path(tmp) / 'out'
        result = subprocess.run([str(binary), 'EXTRACT', str(path), '-d', str(dest),
                                 '--no-passthrough', '--recurse=0'],
                                capture_output=True, text=True, timeout=180)
        mismatches = []
        for name, payload in expected:
            target = dest / name
            if not target.is_file() or target.read_bytes() != payload:
                mismatches.append(name)
        return {'path': str(path), 'sha256': hashlib.sha256(data).hexdigest(),
                'variant': data[4] if kind == 'alar' else 'arc-dir', 'members': len(expected),
                'exit_code': result.returncode, 'mismatches': mismatches,
                'diagnostic': result.stderr[-2000:] if result.returncode else ''}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--binary', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'project/bin/wszst')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--format', choices=('alar', 'pikmin'), default='alar')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not args.root.is_dir():
        parser.error('root must be an extracted game directory')
    if args.jobs < 1:
        parser.error('jobs must be positive')
    paths = []
    for path in sorted(args.root.rglob('*')):
        if path.is_file():
            if args.format == 'pikmin':
                if path.suffix.lower() == '.arc' and path.with_suffix('.dir').is_file():
                    paths.append(path.resolve())
            else:
                with path.open('rb') as source:
                    if source.read(4) == b'ALAR':
                        paths.append(path.resolve())
    if not paths:
        parser.error('no matching archives found')
    results = []
    def run(path):
        try:
            return audit(path, binary, args.format)
        except (OSError, ValueError, struct.error, subprocess.TimeoutExpired) as error:
            return {'path': str(path), 'members': 0, 'exit_code': -1,
                    'mismatches': [], 'diagnostic': str(error)}
    # Keep the executable stable if another build replaces the project tools.
    with tempfile.TemporaryDirectory(prefix='alar-audit-tool-') as tool_tmp, \
            ThreadPoolExecutor(max_workers=args.jobs) as pool:
        binary = Path(tool_tmp) / args.binary.name
        shutil.copy2(args.binary.resolve(), binary)
        for index, result in enumerate(pool.map(run, paths), 1):
            results.append(result)
            if index % 100 == 0:
                print(f'{index}/{len(paths)} archives checked', flush=True)
    failures = [r for r in results if r['exit_code'] or r['mismatches']]
    report = {'archives': len(results), 'members': sum(r['members'] for r in results),
              'failed_archives': len(failures), 'results': results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"{report['archives']} archives, {report['members']} members, {len(failures)} failures")
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
