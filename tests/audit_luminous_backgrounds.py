"""Compare SCB/IMB/PLB PNGs against a tile-first renderer and CUE LZE decoding."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from pngtool import _read_png


def render(screen, tiles, palette):
    columns, rows, bits, stride = struct.unpack_from('<4I',screen)
    if bits != 16 or stride != columns*2 or len(screen) != 16+columns*rows*2:
        raise ValueError('invalid screen header')
    colors = struct.unpack('<'+'H'*(len(palette)//2),palette)
    depth = 8 if len(colors) == 256 else 4
    decoded_tiles = [[tuple((v << 3)|(v >> 2) for v in
                       ((colors[n]>>shift)&31 for shift in (0,5,10))) + (255 if n else 0,)
                      for n in (list(tiles[o:o+64]) if depth==8 else
                                [n for b in tiles[o:o+32] for n in (b&15,b>>4)])]
                     for o in range(0,len(tiles),depth*8)]
    canvas = bytearray(columns*rows*64*4)
    for cell, entry in enumerate(struct.unpack_from('<'+'H'*(columns*rows),screen,16)):
        tile = decoded_tiles[entry&1023]
        for sy in range(8):
            for sx in range(8):
                dx = cell%columns*8 + (7-sx if entry&1024 else sx)
                dy = cell//columns*8 + (7-sy if entry&2048 else sy)
                pos = (dy*columns*8+dx)*4
                canvas[pos:pos+4] = bytes(tile[sy*8+sx])
    return columns*8, rows*8, canvas


def audit(path, binary, reference):
    raw = path.read_bytes()
    with tempfile.TemporaryDirectory(prefix='luminous-reference-') as tmp:
        resources = []
        for ext in ('.scb','.imb'):
            source = path.with_suffix(ext)
            dest = Path(tmp)/('decoded'+ext)
            result = subprocess.run([str(reference),'-d',str(source),str(dest)],
                                    capture_output=True,text=True,timeout=30)
            if result.returncode or 'WARNING' in result.stdout:
                raise ValueError(result.stdout+result.stderr)
            resources.append(dest.read_bytes())
        width,height,expected=render(*resources,path.with_suffix('.plb').read_bytes())
    with tempfile.TemporaryDirectory(prefix='luminous-bg-audit-') as tmp:
        out = Path(tmp)/'image.png'
        run = subprocess.run([str(binary), 'DECODE', str(path), '-d', str(out)],
                             capture_output=True, text=True, timeout=60)
        result = {'path': str(path), 'source_sha256': hashlib.sha256(raw).hexdigest(),
                  'width': width, 'height': height, 'ok': False}
        if run.returncode:
            result['diagnostic'] = run.stderr[-2000:]
            return result
        w, h, channels, pixel = _read_png(out)
        actual = b''.join(bytes(pixel(x,y)) for y in range(h) for x in range(w))
        result.update(ok=(w,h,actual)==(width,height,expected),
                      native_sha256=hashlib.sha256(actual).hexdigest(),
                      reference_sha256=hashlib.sha256(expected).hexdigest())
        return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('roots', type=Path, nargs='+')
    parser.add_argument('--binary', type=Path, default=Path(__file__).resolve().parents[1]/'project/bin/wimgt')
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=8)
    args=parser.parse_args()
    if args.jobs < 1:
        parser.error('jobs must be positive')
    files=sorted({p.resolve() for root in args.roots for p in root.rglob('*')
                  if p.is_file() and p.suffix.lower() == '.scb'})
    if not files:
        parser.error('no background candidates')
    with tempfile.TemporaryDirectory(prefix='luminous-bg-tool-') as tmp:
        binary=Path(tmp)/args.binary.name
        shutil.copy2(args.binary.resolve(),binary)
        def check(path):
            try:
                return audit(path,binary,args.reference.resolve())
            except (OSError, ValueError, IndexError, RecursionError, struct.error, subprocess.TimeoutExpired) as error:
                return {'path':str(path),'ok':False,'diagnostic':str(error)}
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results=[]
            for i,result in enumerate(pool.map(check,files),1):
                results.append(result)
                if i%100==0:
                    print(f'{i}/{len(files)} backgrounds checked',flush=True)
    failures=sum(not r['ok'] for r in results)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps({'backgrounds':len(results),'failures':failures,'results':results},indent=2)+'\n')
    print(f'{len(results)} backgrounds, {failures} failures')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
