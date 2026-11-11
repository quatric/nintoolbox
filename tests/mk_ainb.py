"""Write a minimal AI node graph (AINB 0x407).

    mk_ainb.py OUT.ainb

Command "Start" -> node 0 "Root" (root flag, int property Count = 7, float input Speed = 1.5, child plug "Next" to
node 1 "Leaf"); blackboard S32 "Var" = 3; category "AI".
"""
import struct, sys


class Pool:
    def __init__(self):
        self.data = bytearray(b'\0')
        self.at = {'': 0}

    def __call__(self, s):
        if s not in self.at:
            self.at[s] = len(self.data)
            self.data += s.encode() + b'\0'
        return self.at[s]


def main(out):
    s = Pool()
    guid = bytes(range(16))
    cmd = struct.pack('<I', s('Start')) + guid + struct.pack('<HH', 0, 0)

    def node(type_, index, flags, name, body):
        return struct.pack('<HhHBxIIIIHHHxxIHHI', type_, index, 0, flags, s(name), 0, 0, body, 0, 0, 0, 0, 0, 0, 0) + guid

    at = 0x74 + len(cmd) + 2 * 0x3c
    bb = at
    bb_data = b''.join(struct.pack('<HHHH', 1 if t == 1 else 0, 0, 0 if t <= 1 else 4, 0) for t in range(6))
    bb_data += struct.pack('<II', s('Var'), 0) + struct.pack('<i', 3)
    body = bb + len(bb_data)
    plug_at = body + 0x90 + 20 + 4
    props = plug_at + 8
    io = props + 0x18 + 0x0c
    inputs = io + 0x30
    multi = inputs + 0x10
    body_data = struct.pack('<12I', 0, 1, *([0] * 10))
    body_data += b''.join(struct.pack('<4I', 0, 1 if t == 2 else 0, 0, 0) for t in range(6))
    info = bytearray(20)
    info[4], info[5] = 1, 0
    body_data += bytes(info) + struct.pack('<I', plug_at)
    plug = struct.pack('<iI', 1, s('Next'))
    prop_data = struct.pack('<6I', props + 0x18, *([props + 0x18 + 0x0c] * 5)) + struct.pack('<IIi', s('Count'), 0, 7)
    io_data = b''.join(struct.pack('<II', inputs if t <= 2 else multi, inputs if t < 2 else multi) for t in range(6))
    io_data += struct.pack('<IhhIf', s('Speed'), -1, 0, 0x800000, 1.5)
    tail = multi
    actions = tail
    modules = actions + 4
    bb_id = modules + 4
    enums = bb_id + 8
    repl = enums + 4
    x6c = repl + 8
    pool = x6c + 4
    tail_data = struct.pack('<I', 0) * 2 + struct.pack('<II', 0x12345678, 0) + struct.pack('<I', 0) + bytes(8) + struct.pack('<I', 0)

    hdr = b'AIB ' + struct.pack('<I', 0x407)
    hdr += struct.pack('<8I', s('Test'), 1, 2, 0, 0, 0, bb, pool)
    hdr += struct.pack('<19I', enums, props, multi, io, multi, multi, multi, 0, repl, multi, multi, 0, 0, modules, s('AI'), 0,
                       actions, x6c, bb_id)
    data = hdr + cmd + node(0, 0, 4, 'Root', body) + node(0, 1, 0, 'Leaf', 0) + bb_data + body_data + plug + prop_data
    data += io_data + tail_data
    assert len(data) == pool, (hex(len(data)), hex(pool))
    with open(out, 'wb') as f:
        f.write(data + bytes(s.data))


if __name__ == '__main__':
    main(sys.argv[1])
