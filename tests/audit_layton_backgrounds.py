"""Compare every Layton background pixel with an independent palette/tile renderer."""
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


def unpack(data):
    kind = int.from_bytes(data[:4], 'little')
    signatures = {1: 0x30, 2: 0x10, 3: 0x24, 4: 0x28}
    if kind not in signatures or len(data) < 8 or data[4] != signatures[kind]:
        return data
    data = data[4:]
    size = int.from_bytes(data[1:4], 'little')
    pos = 4
    if not size:
        size = int.from_bytes(data[4:8], 'little'); pos = 8
    if not 0 < size <= 8*1024*1024:
        raise ValueError('invalid expansion size')
    out = bytearray()
    if kind == 1:
        while len(out) < size:
            command = data[pos]; pos += 1
            if command & 128:
                count = (command & 127) + 3
                out.extend(bytes([data[pos]]) * count); pos += 1
            else:
                count = command + 1
                out.extend(data[pos:pos+count]); pos += count
    elif kind == 2:
        while len(out) < size:
            flags = data[pos]; pos += 1
            for bit in range(7, -1, -1):
                if len(out) == size:
                    break
                if flags & (1 << bit):
                    a, b = data[pos:pos+2]; pos += 2
                    distance = ((a & 15) << 8 | b) + 1
                    for _ in range((a >> 4) + 3):
                        out.append(out[-distance])
                else:
                    out.append(data[pos]); pos += 1
    else:
        tree_end = pos + (data[pos] + 1)*2
        # Materialize the tree independently of the stream traversal.
        def tree(offset, leaf=False):
            if offset >= tree_end:
                raise ValueError('invalid Huffman tree offset')
            node = data[offset]
            if leaf:
                return node
            children = (offset & ~1) + ((node & 63)+1)*2
            return (tree(children, bool(node & 128)), tree(children+1, bool(node & 64)))
        root = tree(pos+1)
        pos = tree_end
        symbols = []
        node = root
        needed = size * (2 if kind == 3 else 1)
        while len(symbols) < needed:
            word = struct.unpack_from('<I', data, pos)[0]; pos += 4
            for bit in range(31, -1, -1):
                node = node[(word >> bit) & 1]
                if isinstance(node, int):
                    symbols.append(node); node = root
                    if len(symbols) == needed:
                        break
        if kind == 3:
            out = bytearray(symbols[i] | symbols[i+1] << 4 for i in range(0, needed, 2))
        else:
            out = bytearray(symbols)
    if len(out) != size:
        raise ValueError('decoded size mismatch')
    return out


def render(data):
    colors = struct.unpack_from('<I', data)[0]
    if not 0 < colors <= 256:
        raise ValueError('invalid palette')
    palette = []
    for i, (color,) in enumerate(struct.iter_unpack('<H', data[4:4+colors*2])):
        rgb = [(color >> shift) & 31 for shift in (0, 5, 10)]
        palette.append(bytes([(v << 3) | (v >> 2) for v in rgb] + [255 if i or color & 32768 else 0]))
    count = struct.unpack_from('<I', data, 4+colors*2)[0]
    start = 8+colors*2
    end = start + count*64
    columns, rows = struct.unpack_from('<HH', data, end)
    if not count or not columns or not rows or end+4+columns*rows*2 != len(data):
        raise ValueError('invalid tile map')
    tiles = [b''.join(palette[c] for c in data[pos:pos+64]) for pos in range(start, end, 64)]
    indices = struct.unpack_from('<'+'H'*(columns*rows), data, end+4)
    raster = b''.join(tiles[indices[y//8*columns+x]][y%8*32:y%8*32+32]
                      for y in range(rows*8) for x in range(columns))
    return columns*8, rows*8, raster


def audit(path, binary):
    raw = path.read_bytes()
    width, height, expected = render(unpack(raw))
    with tempfile.TemporaryDirectory(prefix='layton-bg-audit-') as tmp:
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
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=8)
    args=parser.parse_args()
    if args.jobs < 1:
        parser.error('jobs must be positive')
    files=sorted({p.resolve() for root in args.roots for p in root.rglob('*')
                  if p.is_file() and p.suffix.lower() in ('.arc','.arb')})
    if not files:
        parser.error('no background candidates')
    with tempfile.TemporaryDirectory(prefix='layton-bg-tool-') as tmp:
        binary=Path(tmp)/args.binary.name
        shutil.copy2(args.binary.resolve(),binary)
        def check(path):
            try:
                return audit(path,binary)
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
