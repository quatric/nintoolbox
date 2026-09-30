#!/usr/bin/env python3
"""Synthetic Nintendo DSi packages (see project/src/lib-twl.h), built with an
independent Python port of twltool's crypto (needs pycryptodome):

  T.tad   installable .tad (WAD-like): forged ticket, TMD, one content that is
          a modcrypted retail SRL whose plaintext carries the marker string
  E.bin   SD-card DSiWare export: banner, header, footer (SHA-1s + TW cert with
          the ConsoleID), tmd and app under the console key, public.sav
  M.srl   the modcrypted SRL on its own
  plain.srl the expected decrypted SRL, written for the test to compare with
usage: mk_dsi_tad.py OUTDIR"""
import hashlib, os, struct, sys

from Crypto.Cipher import AES

FIX_KEY = bytes.fromhex('3da3ea334c86a6b02aaedb5116ea9262')
KEYY = bytes.fromhex('ccfca7032061be84d3eba426b86dbec2')
DSI_COMMON = bytes.fromhex('af1bf516a807d21aea45984f04742861')
MARKER = b'DSI-MODCRYPT-MARKER'


def ecb(key):
    return AES.new(key, AES.MODE_ECB)


def f_xy(x, y):
    c = b''.join(struct.pack('<I', w) for w in (0x1a4f3e79, 0x2a680f5f, 0x29590258, 0xfffefb4e))
    s = (int.from_bytes(c, 'little') + int.from_bytes(bytes(a ^ b for a, b in zip(x, y)), 'little')) % (1 << 128)
    s = ((s << 42) | (s >> 86)) & ((1 << 128) - 1)
    return s.to_bytes(16, 'little')


def ctr_crypt(key, ctr, data):
    """twltool dsi_crypt_ctr: byte-reversed key/counter/keystream domain."""
    a = ecb(key[::-1])
    c = int.from_bytes(ctr[::-1], 'big')
    out = bytearray()
    for i in range(0, len(data) - len(data) % 16, 16):
        ks = a.encrypt(c.to_bytes(16, 'big'))[::-1]
        out += bytes(x ^ y for x, y in zip(data[i:i + 16], ks))
        c = (c + 1) % (1 << 128)
    return bytes(out) + data[len(out):]


def es_encrypt(key, data, nonce=b'\x00' * 12):
    a = ecb(key[::-1])
    size = len(data)
    mac = bytearray(16)
    mac[0] = (7 << 3) | 2
    mac[1:13] = nonce[::-1]
    mac[13:16] = ((size + 15) & ~15).to_bytes(3, 'big')
    mac = bytearray(a.encrypt(bytes(mac)))
    ctr = bytes([2]) + nonce[::-1] + b'\x00\x00\x00'   # already in AES domain
    c = int.from_bytes(ctr, 'big')
    s0 = a.encrypt(c.to_bytes(16, 'big'))[::-1]
    c += 1
    out = bytearray()
    for i in range(0, size, 16):
        blk = data[i:i + 16].ljust(16, b'\0')
        for k in range(16):
            mac[k] ^= blk[15 - k]
        mac = bytearray(a.encrypt(bytes(mac)))
        ks = a.encrypt(c.to_bytes(16, 'big'))[::-1]
        c += 1
        out += bytes(x ^ y for x, y in zip(blk, ks))
    tag = bytes(mac[15 - k] ^ s0[k] for k in range(16))
    info = bytearray(16)
    info[0] = 0x3a
    info[13:16] = size.to_bytes(3, 'big')
    ectr = b'\x00' + nonce + b'\x00\x00\x00'
    enc_info = bytearray(ctr_crypt(key, ectr, bytes(info)))
    enc_info[1:13] = nonce
    return bytes(out[:size]) + tag + bytes(enc_info)


def var_key(cid8):
    w0 = struct.unpack('>I', cid8[4:8])[0]
    w1 = struct.unpack('>I', cid8[0:4])[0]
    x = struct.pack('<IIII', 0x4e00004a, 0x4a00004e, w1 ^ 0xc80c4b72, w0)
    return f_xy(x, KEYY)


def make_srl():
    """A tiny TWL SRL with one modcrypt area (retail key)."""
    srl = bytearray(0x1000)
    srl[0:12] = b'SYNTH DSIWAR'
    srl[0x0c:0x10] = b'KTSE'
    srl[0x12] = 3                       # unit code: NDS+DSi
    srl[0x1c] = 0x03                    # TWL region + modcrypted, retail
    struct.pack_into('<II', srl, 0x220, 0x400, 0x200)
    srl[0x300:0x310] = bytes(range(0x30, 0x40))
    srl[0x314:0x324] = bytes(range(0x50, 0x60))
    srl[0x350:0x360] = bytes(range(0x90, 0xa0))
    srl[0x400:0x400 + len(MARKER)] = MARKER
    return bytes(srl)


def modcrypt(srl):
    keyx = b'Nintendo' + srl[0x0c:0x10] + srl[0x0c:0x10][::-1]
    key = f_xy(keyx, srl[0x350:0x360])
    off, ln = struct.unpack_from('<II', srl, 0x220)
    out = bytearray(srl)
    out[off:off + ln] = ctr_crypt(key, srl[0x300:0x310], srl[off:off + ln])
    return bytes(out)


def pad64(b):
    return b + b'\0' * (-len(b) % 64)


def make_tad(srl_enc, title_id, title_key):
    idx = 0
    csize = len(srl_enc)
    esize = (csize + 15) & ~15
    civ = idx.to_bytes(2, 'big') + b'\0' * 14
    content = AES.new(title_key, AES.MODE_CBC, civ).encrypt(srl_enc.ljust(esize, b'\0'))

    tmd = bytearray(0x140 + 0xa4 + 0x24)
    struct.pack_into('>I', tmd, 0, 0x10001)
    tmd[0x140:0x140 + 26] = b'Root-CA00000001-CP00000007'
    tmd[0x140 + 0x4c:0x140 + 0x54] = title_id
    struct.pack_into('>HH', tmd, 0x140 + 0x9e, 1, 0)
    rec = struct.pack('>IHHQ', 0, idx, 1, csize) + hashlib.sha1(srl_enc).digest()
    tmd[0x140 + 0xa4:] = rec

    tik = bytearray(0x2a4)
    struct.pack_into('>I', tik, 0, 0x10001)
    tik[0x140:0x140 + 8] = b'Root-CA0'
    wrapped = AES.new(DSI_COMMON, AES.MODE_CBC, title_id + b'\0' * 8).encrypt(title_key)
    tik[0x140 + 0x7f:0x140 + 0x8f] = wrapped
    tik[0x140 + 0x9c:0x140 + 0xa4] = title_id

    cert = b'CERT' * 16
    head = struct.pack('>I2sH6I', 0x20, b'Is', 0, len(cert), 0, len(tik), len(tmd), esize, 0)
    return pad64(head) + pad64(cert) + pad64(bytes(tik)) + pad64(bytes(tmd)) + pad64(content)


def make_bin(srl_enc, cid_hex):
    cid = bytes.fromhex(cid_hex)
    vk = var_key(cid)
    banner = (b'BNR-' + bytes(range(1, 250))).ljust(0x23c0, b'\x11').ljust(0x4000, b'\0')
    tmd = bytes(range(256)) * 2 + bytes(8)                      # 0x208
    pub = b'PUBLICSAV' * 7
    bsav = b'BANNERSAV' * 5
    head = bytearray(0xb4)
    head[0:4] = b'4ANT'
    head[0x10:0x18] = cid
    head[0x20:0x28] = bytes.fromhex('4b545345' '04000300')[::-1] if False else b'KTSE\x04\x00\x03\x00'
    sizes = [len(tmd), len(srl_enc)] + [0] * 7 + [len(pub), len(bsav)]
    struct.pack_into('>11I', head, 0x28, *sizes)
    foot = bytearray(0x440)
    foot[0:20] = hashlib.sha1(banner).digest()
    foot[0x14:0x28] = hashlib.sha1(bytes(head)).digest()
    foot[0x28:0x3c] = hashlib.sha1(tmd).digest()
    foot[0x3c:0x50] = hashlib.sha1(srl_enc).digest()
    foot[0xdc:0xf0] = hashlib.sha1(pub).digest()
    foot[0xf0:0x104] = hashlib.sha1(bsav).digest()
    name = ('Root-CA00000001-MS00000003-TW' + cid_hex.upper()).encode()
    foot[0x2c0 + 0xc4:0x2c0 + 0xc4 + len(name)] = name
    blob = es_encrypt(FIX_KEY, banner, bytes(range(12)))
    blob += es_encrypt(FIX_KEY, bytes(head), bytes(range(1, 13)))
    blob += es_encrypt(FIX_KEY, bytes(foot), bytes(range(2, 14)))
    blob += es_encrypt(vk, tmd, bytes(range(3, 15)))
    blob += es_encrypt(vk, srl_enc, bytes(range(4, 16)))
    blob += es_encrypt(FIX_KEY, pub, bytes(range(5, 17)))
    blob += es_encrypt(FIX_KEY, bsav, bytes(range(6, 18)))
    return blob


if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    plain = make_srl()
    enc = modcrypt(plain)
    assert modcrypt(enc) == plain            # the layer toggles
    title_id = bytes.fromhex('00030004' '4b545345')
    title_key = bytes(range(0xa0, 0xb0))
    for name, data in (('plain.srl', plain), ('M.srl', enc),
                       ('T.tad', make_tad(enc, title_id, title_key)),
                       ('E.bin', make_bin(enc, '0123456789abcdef'))):
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
