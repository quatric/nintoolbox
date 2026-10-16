#!/usr/bin/env python3
"""Compare wszst's Ubisoft BAO package extraction with vgmstream-cli, sample for sample.

Usage: audit_ubibao.py PACKAGE_DIR VGMSTREAM_CLI [--wszst PATH] [--limit N]
Every *.pk is extracted with `wszst x`; the PCM of each resulting .wav is compared with the PCM of every
vgmstream subsong (looping ignored). Reports how many wszst outputs have an exact vgmstream twin.
"""
import argparse, glob, hashlib, os, subprocess, sys, tempfile

def pcm(path):
    d = open(path, 'rb').read()
    i = d.index(b'data')
    return d[i + 8:]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('packages'); ap.add_argument('vgmstream')
    ap.add_argument('--wszst', default=os.path.join(os.path.dirname(__file__), '..', 'project', 'bin', 'wszst'))
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    pks = sorted(glob.glob(os.path.join(a.packages, '*.pk')))
    if a.limit:
        pks = pks[:a.limit]
    total = exact = 0
    bad = []
    for pk in pks:
        subprocess.run([a.wszst, 'x', pk, '--overwrite'], capture_output=True)
        refs = set()
        with tempfile.TemporaryDirectory() as tmp:
            # subsongs may fail individually (sequences, layers); stop after a long run of failures
            fails = 0
            s = 0
            while fails < 12:
                s += 1
                out = os.path.join(tmp, 'r.wav')
                r = subprocess.run([a.vgmstream, '-i', '-s', str(s), '-o', out, pk], capture_output=True)
                if r.returncode == 0:
                    fails = 0
                    refs.add(hashlib.md5(pcm(out)).hexdigest())
                else:
                    fails += 1
        done = set()
        for w in sorted(glob.glob(pk + '.d/*.wav')):
            if w in done:
                continue
            total += 1
            data = pcm(w)
            if w.endswith('_ch0.wav'):
                # multi-channel DSP sounds are split per channel: interleave the pair for the comparison
                w1 = w[:-8] + '_ch1.wav'
                if os.path.exists(w1):
                    done.add(w1)
                    d1 = pcm(w1)
                    data = b''.join(data[i:i + 2] + d1[i:i + 2] for i in range(0, min(len(data), len(d1)), 2))
            if hashlib.md5(data).hexdigest() in refs:
                exact += 1
            else:
                bad.append(w)
    print('packages %d, wav outputs %d, exact vgmstream matches %d' % (len(pks), total, exact))
    for b in bad[:20]:
        print('  no match:', b)
    return 0 if exact == total else 1

if __name__ == '__main__':
    sys.exit(main())
