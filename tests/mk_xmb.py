"""Write a minimal Smash XMB (.xmb) v1 file.

Layout mirrors Sammi-Husky/SSBU-TOOLS XMBDec.py: a "model" root with one
property and one "draw" child with one property. All offsets are absolute
file offsets; name/value offsets are relative to their string tables.

    mk_xmb.py OUT.xmb
"""
import struct, sys

def main(out):
    names = b"model\0draw\0type\0buffer\0"          # 23 bytes
    values = b"effect_main\0" + b"0\0"              # 14 bytes
    # name offsets into the names table: model=0, draw=6, type=11, buffer=16
    OFF_MODEL, OFF_DRAW, OFF_TYPE, OFF_BUFFER = 0, 6, 11, 16

    p_str_off = 0x40
    p_nodes = p_str_off + 4 * 4
    p_props = p_nodes + 2 * 16
    p_map = p_props + 2 * 8                        # 0 mapped nodes: empty
    p_names = p_map
    p_values = p_names + len(names)

    end = p_values + len(values)
    buf = bytearray(end)

    struct.pack_into("<4sIIII", buf, 0, b"XMB ", 2, 2, 4, 0)
    struct.pack_into("<IIIIII", buf, 20,
                     p_str_off, p_nodes, p_props, p_map, p_names, p_values)

    # string-offsets index, sorted by name: buffer, draw, model, type
    struct.pack_into("<4I", buf, p_str_off,
                     OFF_BUFFER, OFF_DRAW, OFF_MODEL, OFF_TYPE)

    # node 0: "model", 1 prop, 1 child, props from 0, parent -1
    struct.pack_into("<Ihhhhhh", buf, p_nodes,
                     OFF_MODEL, 1, 1, 0, 1, -1, -1)
    # node 1: "draw", 1 prop, 0 children, props from 1, parent 0
    struct.pack_into("<Ihhhhhh", buf, p_nodes + 16,
                     OFF_DRAW, 1, 0, 1, -1, 0, -1)

    # prop 0: type="effect_main"; prop 1: buffer="0"
    struct.pack_into("<II", buf, p_props, OFF_TYPE, 0)
    struct.pack_into("<II", buf, p_props + 8, OFF_BUFFER, 12)

    buf[p_names:p_names + len(names)] = names
    buf[p_values:p_values + len(values)] = values

    open(out, "wb").write(buf)

if __name__ == "__main__":
    main(sys.argv[1])
