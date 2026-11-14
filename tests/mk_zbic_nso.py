#!/usr/bin/env python3
# Fixture for the Switch 2 "main" NSO segment packer ZBIC: zstd with the FSE table headers (sequence tables and
# Huffman weights) stored by binary interpolative coding instead of the FSE_writeNCount layout, frame magic "ZBIC".
#
#   mk_zbic_nso.py DIR   writes DIR/main.nso (text segment ZBIC, ro/data stored) and DIR/text.expect
#
# There is no ZBIC encoder around, so this one re-encodes a standard zstd frame: every FSE table header is
# replaced by its BIC form, everything else (literal / sequence bit streams, block structure) stays untouched.
# The reader side (lib zstd.c, ZBIC_readNCount) was verified on the real segments of a Switch 2 game against the
# SHA-256 hashes stored in the NSO header; this script is the inverse of that reader.
import hashlib
import os
import random
import struct
import subprocess
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
L = 1 << 40


# ---- standard FSE table header reader (zstd FSE_readNCount) -------------------------------------------------------
def read_ncount(b, maxal):
    pos = 0

    def rd(n):
        nonlocal pos
        v = 0
        for i in range(n):
            v |= ((b[pos >> 3] >> (pos & 7)) & 1) << i
            pos += 1
        return v

    al = rd(4) + 5
    assert al <= maxal
    remaining = (1 << al) + 1
    threshold = 1 << al
    nb = al + 1
    counts = []
    prev0 = False
    while remaining > 1:
        if prev0:
            while True:
                r = rd(2)
                counts += [0] * r
                if r != 3:
                    break
            prev0 = False
            continue
        mx = (2 * threshold - 1) - remaining
        save = pos
        low = rd(nb - 1)
        if low < mx:
            count = low
        else:
            pos = save
            full = rd(nb)
            count = full - mx if full >= threshold else full
        count -= 1
        remaining -= abs(count)
        counts.append(count)
        prev0 = count == 0
        while remaining < threshold:
            nb -= 1
            threshold >>= 1
    return al, counts, (pos + 7) // 8


# ---- ZBIC table header writer (inverse of the reader in zstd.c) ---------------------------------------------------
def bic_nodes(n):
    nodes = []

    def rec(lo, hi):
        if hi - lo < 2:
            return
        mid = (lo + hi) // 2
        nodes.append((mid, lo, hi))
        rec(mid, hi)
        rec(lo, mid)

    rec(0, n)
    nodes.append((0, 0, 1))
    return nodes


def bic_encode(al, counts):
    use = 1 if any(c < 0 for c in counts) else 0
    charnum = len(counts) - 1
    assert 2 <= charnum + 1 <= 53 and 5 <= al <= 12
    n = 1
    while n <= charnum:
        n <<= 1
    cum = [0]
    for c in counts:
        cum.append(cum[-1] + c + use)
    total = cum[-1]
    target = cum + [total] * (n + 2 - len(cum))
    rem = 1 << al
    sym3 = (total - charnum - 2) if use else (total - 1)
    assert 0 <= sym3 < rem
    ops = [(52, charnum - 1), (8, al - 5), (rem, sym3)]
    sim = [0] * (n + 2)
    sim[n] = total
    for mid, lo, hi in bic_nodes(n):
        if sim[lo] == sim[hi]:
            for k in range(lo + 1, hi):
                sim[k] = sim[lo]
        else:
            span = sim[hi] - sim[lo] + 1
            s = target[mid] - sim[lo] if mid != 0 else 0
            assert 0 <= s < span, (mid, lo, hi, s, span)
            ops.append((span, s))
            sim[mid] = sim[lo] + s
    # reverse simulation of the decoder state machine (rANS style, 8 bit renormalisation below 2^40)
    x = 0
    chunks = []  # bytes loaded by each refill, reverse decode order
    emitted = False
    for r, s in reversed(ops):
        done = False
        for m in range(0, 4):
            q = x >> (8 * m)
            if m == 0:
                valid = x >= L or not emitted
            else:
                valid = q < L and (q << (8 * (m - 1))) < L and x >= L
            if not valid:
                continue
            xp = q * r + s
            if xp >= (1 << 48):
                continue
            if emitted or m > 0:
                if xp < L and (emitted or m > 0):
                    continue
            chunks.append(bytes((x >> (8 * (m - 1 - i))) & 255 for i in range(m)))  # big endian load order
            if m:
                emitted = True
            x = xp
            done = True
            break
        assert done, "cannot encode table"
    # x is now the state after the initial refill: it must be >= 2^40 unless the whole stream is shorter
    first = x.to_bytes((x.bit_length() + 7) // 8 or 1, "big")
    body = first + b"".join(reversed(chunks))
    # stream bytes in decode load order -> memory order ip[R0] .. ip[1]
    raw = body[::-1]
    assert len(raw) < 128
    return bytes([len(raw) | (use << 7)]) + raw


# ---- ZBIC transcoder for a standard zstd frame --------------------------------------------------------------------
def lit_header(t, f, rs, cs):
    if f in (0, 1):
        v = t | (f << 2) | (rs << 4) | (cs << 14)
        return v.to_bytes(3, "little")
    if f == 2:
        v = t | (f << 2) | (rs << 4) | (cs << 18)
        return v.to_bytes(4, "little")
    v = t | (f << 2) | (rs << 4) | (cs << 22)
    return v.to_bytes(5, "little")


def transcode_block(b):
    t = b[0] & 3
    f = (b[0] >> 2) & 3
    if t < 2:
        hl = {0: 1, 1: 2, 2: 1, 3: 3}[f]
        rs = {0: b[0] >> 3, 2: b[0] >> 3, 1: (b[0] | b[1] << 8) >> 4, 3: int.from_bytes(b[:3], "little") >> 4}[f]
        litend = hl + (rs if t == 0 else 1)
        lit = b[:litend]
    else:
        hl, rs, cs = {
            0: (3, (int.from_bytes(b[:3], "little") >> 4) & 0x3FF, (int.from_bytes(b[:3], "little") >> 14) & 0x3FF),
            1: (3, (int.from_bytes(b[:3], "little") >> 4) & 0x3FF, (int.from_bytes(b[:3], "little") >> 14) & 0x3FF),
            2: (4, (int.from_bytes(b[:4], "little") >> 4) & 0x3FFF, (int.from_bytes(b[:4], "little") >> 18) & 0x3FFF),
            3: (5, (int.from_bytes(b[:5], "little") >> 4) & 0x3FFFF, (int.from_bytes(b[:5], "little") >> 22) & 0x3FFFF),
        }[f]
        litend = hl + cs
        body = b[hl:litend]
        if t == 2:
            hb = body[0]
            assert hb < 128
            al, counts, used = read_ncount(body[1 : 1 + hb], 6)
            new = bic_encode(al, counts) + body[1 + used : 1 + hb]
            assert len(new) < 128
            body = bytes([len(new)]) + new + body[1 + hb :]
        newcs = len(body)
        limit = {0: 1 << 10, 1: 1 << 10, 2: 1 << 14, 3: 1 << 18}[f]
        assert newcs < limit, "literal size format overflow"
        lit = lit_header(t, f, rs, newcs) + body
    p = litend
    seq = b[p:]
    nb = seq[0]
    q = 1 if nb < 128 else (2 if nb < 255 else 3)
    if nb == 0:
        return lit + seq
    modes = seq[q]
    q += 1
    res = seq[:q]
    for shift, maxal in ((6, 9), (4, 8), (2, 9)):
        mode = (modes >> shift) & 3
        if mode == 2:
            al, counts, used = read_ncount(seq[q:], maxal)
            res += bic_encode(al, counts)
            q += used
        elif mode == 1:
            res += seq[q : q + 1]
            q += 1
    res += seq[q:]
    return lit + res


def to_zbic(z):
    assert z[:4] == bytes.fromhex("28b52ffd")
    fhd = z[4]
    p = 5
    if not fhd & 0x20:
        p += 1
    p += {0: 1 if fhd & 0x20 else 0, 1: 2, 2: 4, 3: 8}[fhd >> 6]
    p += (0, 1, 2, 4)[fhd & 3]
    res = bytearray(b"ZBIC" + z[4:p])
    while True:
        h = z[p] | z[p + 1] << 8 | z[p + 2] << 16
        last, typ, sz = h & 1, (h >> 1) & 3, h >> 3
        blk = z[p + 3 : p + 3 + sz]
        if typ == 2:
            blk = transcode_block(blk)
        elif typ == 1:
            blk = blk[:1]
        hdr = last | (typ << 1) | ((sz if typ in (0, 1) else len(blk)) << 3)
        res += hdr.to_bytes(3, "little") + blk
        p += 3 + (1 if typ == 1 else sz)
        if last:
            break
    return bytes(res)


# ---- test data: ARM64-looking code with structure (so both Huffman and FSE tables are used) ----------------------
rnd = random.Random(7)
words = [rnd.getrandbits(32) for _ in range(400)]
text = bytearray()
while len(text) < 300000:
    w = words[min(int(rnd.expovariate(0.02)), len(words) - 1)]
    text += struct.pack("<I", w ^ (rnd.getrandbits(8) if rnd.random() < 0.3 else 0))
text = bytes(text)
z = subprocess.run(["zstd", "-q", "-c", "-3", "--no-check", "--single-thread"], input=text, check=True, capture_output=True).stdout
zb = to_zbic(z)

ro = bytes(range(256)) * 4
data = b"\x01\x02\x03\x04" * 64
off_text = 0x104
off_ro = off_text + len(zb)
off_data = off_ro + len(ro)
hdr = bytearray(0x104)
hdr[0:4] = b"NSO0"
struct.pack_into("<I", hdr, 0x0C, 0x01 | 0x08 | 0x10 | 0x20)  # text compressed, all hashes
struct.pack_into("<IIII", hdr, 0x10, off_text, 0, len(text), 0x100)
struct.pack_into("<IIII", hdr, 0x20, off_ro, 0x400000, len(ro), 4)
struct.pack_into("<IIII", hdr, 0x30, off_data, 0x401000, len(data), 0)
struct.pack_into("<III", hdr, 0x60, len(zb), len(ro), len(data))
hdr[0x100:0x104] = b"Owl\0"
for i, seg in enumerate((text, ro, data)):
    hdr[0xA0 + 32 * i : 0xC0 + 32 * i] = hashlib.sha256(seg).digest()
open(out + "/main.nso", "wb").write(bytes(hdr) + zb + ro + data)
open(out + "/text.expect", "wb").write(text)
open(out + "/ro.expect", "wb").write(ro)
