#!/usr/bin/env python3
# Stdlib-only synthetic Opoona (Wii, ArtePiazza) fixtures for regress.sh.
#
# These formats have no public documentation anywhere; the layouts below
# were reverse-engineered from scratch against the retail disc's own
# character/ files (see lib-opoona.c/.h for the full writeup of what is and
# is not understood). Only the confirmed pieces are exercised here:
#   sample.mol   .mol manifest: (offset,size,name) record table, one NULL
#                placeholder slot and two named entries
#   sample.mot   .mot clip: header + bind-pose bone array only (no
#                animation-curve pool -- deliberately non-animated, so the
#                fixture is fully, honestly decodable end to end)
# Usage: mk_opoona.py OUTDIR
import struct
import sys
import os


def mol_record(name, offset, size):
    nm = name.encode('ascii')
    assert len(nm) < 16
    return struct.pack('<II', offset, size) + nm + b'\0' * (16 - len(nm)) + b'\0' * 8


def opoona_mol():
    # 8-byte header (content not fully understood; kept as zero, matching
    # the low-order bytes seen across real samples closely enough for a
    # from-scratch synthetic fixture).
    header = b'\0' * 8
    records = b''.join([
        mol_record('NULL', 0, 0),
        mol_record('m003_idle.mot', 0x10020, 21104),
        mol_record('m003_hit.mot', 0x152a0, 14264),
    ])
    return header + records


def be_f32(v):
    return struct.pack('>f', v)


def bone_record(scale, translate, quat):
    # 80 bytes: scale xyz, 40 reserved bytes, translate xyz, quaternion xyzw
    return (struct.pack('>3f', *scale) + b'\0' * 40 + struct.pack('>3f', *translate)
        + struct.pack('>4f', *quat))


def opoona_mot():
    bones = [
        # bone 0: identity, mirrored X scale (matches the real disc's root
        # bone convention observed across several characters)
        ((-1.0, 1.0, 1.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)),
        # bone 1: default scale, small translation, identity rotation
        ((1.0, 1.0, 1.0), (0.0, 3.0, 0.0), (0.0, 0.0, 0.0, 1.0)),
        # bone 2: default scale, a real (non-identity) unit quaternion --
        # a 90-degree rotation, same shape as seen in real clips
        ((1.0, 1.0, 1.0), (1.5, 0.5, 0.0), (0.7071068, 0.7071068, 0.0, 0.0)),
    ]
    bone_count = len(bones)

    header = bytearray(0x70)
    struct.pack_into('>f', header, 0x04, 12.0) # duration (frames)
    struct.pack_into('>f', header, 0x08, 30.0) # frame rate (constant in every real sample)
    struct.pack_into('>I', header, 0x0c, bone_count + 1) # bone_count+1, confirmed 600-file relationship
    # table_ptr (0x28) / footer_ptr (0x2c) are filled in once the total
    # size is known, matching the real files' footer convention exactly
    # (footer_ptr == file_size - 32).

    body = b''.join (bone_record (*b) for b in bones)
    payload = bytearray (bytes (header) + body)
    footer = bytearray(32)
    table_ptr = len(payload) # synthetic: points past the bone array, like real files' second pointer does
    footer_ptr = len(payload) # == file_size - 32 once the footer itself is appended
    struct.pack_into('>I', footer, 0, table_ptr)
    struct.pack_into('>I', footer, 4, footer_ptr) # mirrors the real files' t1 == filesize-32 footer word
    struct.pack_into('>I', payload, 0x28, table_ptr)
    struct.pack_into('>I', payload, 0x2c, footer_ptr)

    return payload + bytes(footer)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    files = {
        'sample.mol': opoona_mol(),
        'sample.mot': opoona_mot(),
    }
    for name, blob in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(blob)
        print('wrote %s (%d bytes)' % (name, len(blob)))


main()
