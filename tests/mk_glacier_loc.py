"""Synthetic IO Interactive Glacier localisation tree ("t.LOC", Mini Ninjas): groups, an empty group, a name-only node,
plain strings and a dialogue string with an id. Writes <dir>/t.LOC; "check <dir>" verifies t.LOC.d/strings.tsv."""
import os
import struct
import sys


def group(children):
    if not children:
        return b"\0"
    offs, p = [], 0
    for c in children:
        offs.append(p)
        p += len(c)
    return bytes([len(children)]) + b"".join(struct.pack("<I", o) for o in offs[1:]) + b"".join(children)


def node(name, typ, payload=b""):
    return name.encode() + b"\0" + bytes([typ]) + payload


def text(s, ident=None):
    t = s.encode("utf-8") + b"\0"
    return t + (struct.pack(">II", ident, ident) if ident is not None else b"\0\0\0\0")


EXPECT = "Menu/Title\tMini Ninjas\nMenu/Help\tTab\\there\\nnew line\nDialog/Line0\tGrüße\t151136\n"

if __name__ == "__main__":
    if sys.argv[1] == "check":
        got = open(os.path.join(sys.argv[2], "t.LOC.d", "strings.tsv"), encoding="utf-8").read()
        assert got == EXPECT, repr(got)
        print("PASS")
    else:
        menu = node("Menu", 0x10, group([node("Title", 0x09, text("Mini Ninjas")), node("Empty", 0x10, group([])),
                                         node("Alias", 0x08), node("Help", 0x09, text("Tab\there\nnew line"))]))
        dialog = node("Dialog", 0x10, group([node("Line0", 0x29, text("Grüße", 0x24E60))]))
        open(os.path.join(sys.argv[1], "t.LOC"), "wb").write(group([menu, dialog]))
