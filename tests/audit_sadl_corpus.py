"""Compare native SADL WAV output with a supplied vgmstream-cli executable."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import wave


def read_audio(path):
    with wave.open(str(path), 'rb') as wav:
        return (wav.getnchannels(), wav.getframerate(), wav.getnframes(),
                hashlib.sha256(wav.readframes(wav.getnframes())).hexdigest())


def audit(path, native, reference):
    raw = path.read_bytes()
    result = {'path': str(path), 'source_sha256': hashlib.sha256(raw).hexdigest(),
              'channels': raw[0x32], 'codec': hex(raw[0x33] & 0xf0)}
    with tempfile.TemporaryDirectory(prefix='sadl-audit-') as tmp:
        ours, theirs = Path(tmp)/'native.wav', Path(tmp)/'reference.wav'
        for cmd in ([str(native), 'DECOMPRESS', str(path), '-d', str(ours), '--no-passthrough'],
                    [str(reference), '-i', '-o', str(theirs), str(path)]):
            run = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
            if run.returncode:
                result.update(ok=False, diagnostic=run.stderr[-2000:] + run.stdout[-2000:])
                return result
        a, b = read_audio(ours), read_audio(theirs)
        result.update(ok=a == b, native=a, reference=b)
        # Check exported inclusive WAV loop bounds against SADL's byte offsets.
        if raw[0x31]:
            data = ours.read_bytes()
            pcm_bytes = struct.unpack_from('<I', data, 40)[0]
            pos = 44 + pcm_bytes
            start, end = struct.unpack_from('<II', data, pos + 52)
            payload = struct.unpack_from('<I', raw, 0x48)[0]
            loop = struct.unpack_from('<I', raw, 0x54)[0]
            samples = 30 if raw[0x33] & 0xf0 == 0xb0 else 32
            expected = (loop - payload) // (raw[0x32] * 16) * samples
            result['loop_ok'] = data[pos:pos+4] == b'smpl' and start == expected and end == a[2]-1
            result['ok'] &= result['loop_ok']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('roots', type=Path, nargs='+')
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path(__file__).resolve().parents[1]/'project/bin/wszst')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('jobs must be positive')
    paths = sorted({p.resolve() for root in args.roots for p in root.rglob('*')
                    if p.is_file() and p.suffix.lower() in ('.sad', '.sadl')})
    if not paths:
        parser.error('no SADL streams found')
    with tempfile.TemporaryDirectory(prefix='sadl-audit-tool-') as tmp:
        binary = Path(tmp)/args.binary.name
        shutil.copy2(args.binary.resolve(), binary)
        def check(path):
            try:
                return audit(path, binary, args.reference.resolve())
            except (OSError, ValueError, IndexError, struct.error, wave.Error, subprocess.TimeoutExpired) as error:
                return {'path': str(path), 'ok': False, 'diagnostic': str(error)}
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = []
            for i, result in enumerate(pool.map(check, paths), 1):
                results.append(result)
                if i % 50 == 0:
                    print(f'{i}/{len(paths)} streams checked', flush=True)
    failures = sum(not r['ok'] for r in results)
    report = {'streams': len(results), 'failures': failures, 'results': results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(results)} streams, {failures} failures')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
