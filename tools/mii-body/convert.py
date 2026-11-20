#!/usr/bin/env python3
"""Converts Mii body models from the Nintendo Mii editors and Miitomo to cleaned-up .glb files.

Usage: convert.py --sources <assets dir> [--out <dir>] [--wmdlt <path>] <target>... | --list

Each target in recipes.toml names model files under --sources. wmdlt decodes every model with its
animations to GLB, then the recipe's clean-up steps run on that GLB (see recipes.toml for what each
one does). Outputs go to <out>/<target>/<name>.glb. Python 3.11+, standard library only.
"""

import argparse
import copy
import json
import math
import shutil
import struct
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_WMDLT = HERE.parent.parent / "project" / "bin" / "wmdlt"

# --------------------------------------------------------------------------- math

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qnorm(q):
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / n for c in q)


def qrot(q, v):
    """Rotates vector v by quaternion q."""
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qconj(q))
    return (p[0], p[1], p[2])


def qaxis(axis, degrees):
    h = math.radians(degrees) / 2
    s = math.sin(h)
    return {"x": (s, 0, 0, math.cos(h)), "y": (0, s, 0, math.cos(h)), "z": (0, 0, s, math.cos(h))}[axis]


def mat_from_trs(t, q, s):
    """Column-major 4x4."""
    x, y, z, w = q
    r = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
    m = [0.0] * 16
    for c in range(3):
        for k in range(3):
            m[c * 4 + k] = r[c * 3 + k] * s[c]
    m[12], m[13], m[14], m[15] = t[0], t[1], t[2], 1.0
    return m


def mat_mul(a, b):
    """Column-major a * b."""
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def mat_inverse(m):
    """Inverse of a column-major affine 4x4."""
    a = [[m[c * 4 + r] for c in range(3)] for r in range(3)]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
           - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-20:
        return [1.0 if i % 5 == 0 else 0.0 for i in range(16)]
    inv = [[0.0] * 3 for _ in range(3)]
    for r in range(3):
        for c in range(3):
            r1, r2 = [i for i in range(3) if i != c]
            c1, c2 = [i for i in range(3) if i != r]
            minor = a[r1][c1] * a[r2][c2] - a[r1][c2] * a[r2][c1]
            inv[r][c] = (-1) ** (r + c) * minor / det
    t = [m[12], m[13], m[14]]
    out = [0.0] * 16
    for c in range(3):
        for r in range(3):
            out[c * 4 + r] = inv[r][c]
    for r in range(3):
        out[12 + r] = -sum(inv[r][k] * t[k] for k in range(3))
    out[15] = 1.0
    return out


def decompose(m):
    """Column-major 4x4 -> (t, q, s)."""
    t = (m[12], m[13], m[14])
    cols = [m[0:3], m[4:7], m[8:11]]
    s = [math.sqrt(sum(v * v for v in col)) for col in cols]
    r = [[cols[c][k] / s[c] if s[c] else 0.0 for c in range(3)] for k in range(3)]
    det = (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1])
           - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
           + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))
    if det < 0:
        s[0] = -s[0]
        for k in range(3):
            r[k][0] = -r[k][0]
    tr = r[0][0] + r[1][1] + r[2][2]
    if tr > 0:
        k = math.sqrt(tr + 1.0) * 2
        q = ((r[2][1] - r[1][2]) / k, (r[0][2] - r[2][0]) / k, (r[1][0] - r[0][1]) / k, 0.25 * k)
    elif r[0][0] > r[1][1] and r[0][0] > r[2][2]:
        k = math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2
        q = (0.25 * k, (r[0][1] + r[1][0]) / k, (r[0][2] + r[2][0]) / k, (r[2][1] - r[1][2]) / k)
    elif r[1][1] > r[2][2]:
        k = math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2
        q = ((r[0][1] + r[1][0]) / k, 0.25 * k, (r[1][2] + r[2][1]) / k, (r[0][2] - r[2][0]) / k)
    else:
        k = math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2
        q = ((r[0][2] + r[2][0]) / k, (r[1][2] + r[2][1]) / k, 0.25 * k, (r[1][0] - r[0][1]) / k)
    return t, qnorm(q), tuple(s)

# --------------------------------------------------------------------------- glTF

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
FORMATS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}


class Gltf:
    """A .glb in memory. New data is appended to the binary chunk; save() drops what is unused."""

    def __init__(self, path):
        data = Path(path).read_bytes()
        if data[:4] != b"glTF":
            raise ValueError(f"{path}: not a .glb")
        json_len = struct.unpack_from("<I", data, 12)[0]
        self.j = json.loads(data[20:20 + json_len])
        self.bin = bytearray()
        offset = 20 + json_len
        if offset + 8 <= len(data):
            bin_len = struct.unpack_from("<I", data, offset)[0]
            self.bin = bytearray(data[offset + 8:offset + 8 + bin_len])
        for key in ("nodes", "meshes", "materials", "accessors", "bufferViews", "animations", "skins"):
            self.j.setdefault(key, [])
        self.normalize_nodes()

    # accessors -------------------------------------------------------------
    def read(self, index):
        """An accessor's elements as a list of tuples (floats or ints as stored)."""
        a = self.j["accessors"][index]
        n = COMPONENTS[a["type"]]
        fmt = FORMATS[a["componentType"]]
        size = struct.calcsize(fmt)
        if "bufferView" not in a:
            return [tuple([0] * n)] * a["count"]
        view = self.j["bufferViews"][a["bufferView"]]
        base = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = view.get("byteStride", size * n)
        out = []
        for i in range(a["count"]):
            values = struct.unpack_from("<" + fmt * n, self.bin, base + i * stride)
            if a.get("normalized"):
                top = {5121: 255, 5123: 65535, 5120: 127, 5122: 32767}.get(a["componentType"], 1)
                values = tuple(max(v / top, -1.0) for v in values)
            out.append(values)
        return out

    def add(self, rows, kind, component_type=5126, target=None, minmax=False):
        """Appends an accessor holding rows; returns its index."""
        fmt = FORMATS[component_type]
        n = COMPONENTS[kind]
        while len(self.bin) % 4:
            self.bin.append(0)
        offset = len(self.bin)
        for row in rows:
            self.bin += struct.pack("<" + fmt * n, *row)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(self.bin) - offset}
        if target:
            view["target"] = target
        self.j["bufferViews"].append(view)
        accessor = {"bufferView": len(self.j["bufferViews"]) - 1, "componentType": component_type,
                    "count": len(rows), "type": kind}
        if minmax and rows:
            accessor["min"] = [min(r[i] for r in rows) for i in range(n)]
            accessor["max"] = [max(r[i] for r in rows) for i in range(n)]
        self.j["accessors"].append(accessor)
        return len(self.j["accessors"]) - 1

    # nodes -----------------------------------------------------------------
    def normalize_nodes(self):
        """Gives every node an explicit translation/rotation/scale instead of a matrix."""
        for node in self.j["nodes"]:
            if "matrix" in node:
                t, q, s = decompose(node.pop("matrix"))
                node["translation"], node["rotation"], node["scale"] = list(t), list(q), list(s)
            node.setdefault("translation", [0.0, 0.0, 0.0])
            node.setdefault("rotation", [0.0, 0.0, 0.0, 1.0])
            node.setdefault("scale", [1.0, 1.0, 1.0])

    def parents(self):
        parent = {}
        for i, node in enumerate(self.j["nodes"]):
            for c in node.get("children", []):
                parent[c] = i
        return parent

    def find(self, name):
        for i, node in enumerate(self.j["nodes"]):
            if node.get("name") == name:
                return i
        return None

    def local(self, i):
        n = self.j["nodes"][i]
        return mat_from_trs(n["translation"], n["rotation"], n["scale"])

    def world(self, i, parent=None):
        parent = self.parents() if parent is None else parent
        m = self.local(i)
        while i in parent:
            i = parent[i]
            m = mat_mul(self.local(i), m)
        return m

    def preorder(self):
        """Node indices, parents before children, roots in scene order."""
        order = []
        stack = list(reversed(self.j["scenes"][self.j.get("scene", 0)]["nodes"]))
        while stack:
            i = stack.pop()
            order.append(i)
            stack.extend(reversed(self.j["nodes"][i].get("children", [])))
        return order

    def subtree(self, root):
        out, stack = [], [root]
        while stack:
            i = stack.pop()
            out.append(i)
            stack.extend(self.j["nodes"][i].get("children", []))
        return set(out)

    def mesh_nodes(self):
        return [i for i, n in enumerate(self.j["nodes"]) if "mesh" in n]

    def material_name(self, primitive):
        m = primitive.get("material")
        return self.j["materials"][m].get("name", "") if m is not None else ""

    # output ----------------------------------------------------------------
    def save(self, path):
        """Writes the .glb, keeping only the nodes, meshes, accessors and data still referenced."""
        j = self.j
        # Nodes reachable from the scene, plus every skin joint.
        keep_nodes = set(self.preorder())
        for skin in j["skins"]:
            keep_nodes.update(skin["joints"])
        node_map = {old: new for new, old in enumerate(sorted(keep_nodes))}
        nodes = [j["nodes"][old] for old in sorted(keep_nodes)]
        for node in nodes:
            if "children" in node:
                node["children"] = [node_map[c] for c in node["children"] if c in node_map]
                if not node["children"]:
                    del node["children"]
        for scene in j["scenes"]:
            scene["nodes"] = [node_map[n] for n in scene["nodes"] if n in node_map]
        for skin in j["skins"]:
            skin["joints"] = [node_map[n] for n in skin["joints"]]
            if "skeleton" in skin:
                skin["skeleton"] = node_map.get(skin["skeleton"], skin["joints"][0])
        for anim in j["animations"]:
            anim["channels"] = [c for c in anim["channels"] if c["target"].get("node") in node_map]
            for c in anim["channels"]:
                c["target"]["node"] = node_map[c["target"]["node"]]
        j["nodes"] = nodes
        # Meshes and materials still used.
        mesh_ids = sorted({n["mesh"] for n in nodes if "mesh" in n})
        mesh_map = {old: new for new, old in enumerate(mesh_ids)}
        j["meshes"] = [j["meshes"][old] for old in mesh_ids]
        for n in nodes:
            if "mesh" in n:
                n["mesh"] = mesh_map[n["mesh"]]
        mat_ids = sorted({p["material"] for m in j["meshes"] for p in m["primitives"] if "material" in p})
        mat_map = {old: new for new, old in enumerate(mat_ids)}
        j["materials"] = [j["materials"][old] for old in mat_ids]
        for m in j["meshes"]:
            for p in m["primitives"]:
                if "material" in p:
                    p["material"] = mat_map[p["material"]]
        # Animations: drop empty ones and unused samplers.
        animations = []
        for anim in j["animations"]:
            used = sorted({c["sampler"] for c in anim["channels"]})
            smap = {old: new for new, old in enumerate(used)}
            anim["samplers"] = [anim["samplers"][old] for old in used]
            for c in anim["channels"]:
                c["sampler"] = smap[c["sampler"]]
            if anim["channels"]:
                animations.append(anim)
        j["animations"] = animations
        # Accessors and buffer views still referenced, rewritten into a fresh buffer.
        used = set()
        for m in j["meshes"]:
            for p in m["primitives"]:
                used.update(p["attributes"].values())
                if "indices" in p:
                    used.add(p["indices"])
                for t in p.get("targets", []):
                    used.update(t.values())
        for skin in j["skins"]:
            if "inverseBindMatrices" in skin:
                used.add(skin["inverseBindMatrices"])
        for anim in j["animations"]:
            for s in anim["samplers"]:
                used.update((s["input"], s["output"]))
        acc_map = {old: new for new, old in enumerate(sorted(used))}
        accessors = [j["accessors"][old] for old in sorted(used)]
        views_used = sorted({a["bufferView"] for a in accessors if "bufferView" in a}
                            | {img["bufferView"] for img in j.get("images", []) if "bufferView" in img})
        view_map = {}
        new_bin = bytearray()
        views = []
        for old in views_used:
            v = dict(j["bufferViews"][old])
            while len(new_bin) % 4:
                new_bin.append(0)
            start = v.get("byteOffset", 0)
            chunk = self.bin[start:start + v["byteLength"]]
            v["byteOffset"] = len(new_bin)
            new_bin += chunk
            view_map[old] = len(views)
            views.append(v)
        for a in accessors:
            if "bufferView" in a:
                a["bufferView"] = view_map[a["bufferView"]]
        for img in j.get("images", []):
            if "bufferView" in img:
                img["bufferView"] = view_map[img["bufferView"]]
        j["accessors"], j["bufferViews"] = accessors, views
        for m in j["meshes"]:
            for p in m["primitives"]:
                p["attributes"] = {k: acc_map[v] for k, v in p["attributes"].items()}
                if "indices" in p:
                    p["indices"] = acc_map[p["indices"]]
                if "targets" in p:
                    p["targets"] = [{k: acc_map[v] for k, v in t.items()} for t in p["targets"]]
        for skin in j["skins"]:
            if "inverseBindMatrices" in skin:
                skin["inverseBindMatrices"] = acc_map[skin["inverseBindMatrices"]]
        for anim in j["animations"]:
            for s in anim["samplers"]:
                s["input"], s["output"] = acc_map[s["input"]], acc_map[s["output"]]
        while len(new_bin) % 4:
            new_bin.append(0)
        j["buffers"] = [{"byteLength": len(new_bin)}]
        for key in ("animations", "skins", "materials"):
            if not j[key]:
                del j[key]
        text = json.dumps(j, separators=(",", ":")).encode()
        text += b" " * (-len(text) % 4)
        out = bytearray(b"glTF" + struct.pack("<II", 2, 12 + 8 + len(text) + 8 + len(new_bin)))
        out += struct.pack("<I", len(text)) + b"JSON" + text
        out += struct.pack("<I", len(new_bin)) + b"BIN\0" + new_bin
        Path(path).write_bytes(out)

# --------------------------------------------------------------------------- clean-up steps

def matches(name, patterns):
    return any(p == "*" or p.lower() in name.lower() for p in patterns)


def drop_meshes(g, patterns):
    """Removes mesh nodes whose mesh or material name matches."""
    j = g.j
    dropped = []
    for i in g.mesh_nodes():
        mesh = j["meshes"][j["nodes"][i]["mesh"]]
        names = [mesh.get("name", "")] + [g.material_name(p) for p in mesh["primitives"]]
        if any(matches(n, patterns) for n in names):
            dropped.append(i)
    for i in dropped:
        del j["nodes"][i]["mesh"]
        j["nodes"][i].pop("skin", None)
        for scene in j["scenes"]:
            if i in scene["nodes"]:
                scene["nodes"].remove(i)
    return len(dropped)


def drop_channels(g, bones):
    ids = {g.find(b) for b in bones} - {None}
    for anim in g.j["animations"]:
        anim["channels"] = [c for c in anim["channels"] if c["target"].get("node") not in ids]


def strip_textures(g):
    j = g.j
    for key in ("images", "textures", "samplers"):
        j.pop(key, None)
    for m in j["materials"]:
        pbr = m.setdefault("pbrMetallicRoughness", {})
        pbr.pop("baseColorTexture", None)
        pbr.pop("metallicRoughnessTexture", None)
        pbr.setdefault("baseColorFactor", [0.8, 0.8, 0.8, 1.0])
        for key in ("normalTexture", "occlusionTexture", "emissiveTexture"):
            m.pop(key, None)
        m.pop("extensions", None)
    for m in j["meshes"]:
        for p in m["primitives"]:
            for key in [k for k in p["attributes"] if k.startswith("TEXCOORD_") and k != "TEXCOORD_0"]:
                del p["attributes"][key]
    j.pop("extensionsUsed", None)
    j.pop("extensionsRequired", None)


def own_sampler(anim, channel):
    """The channel's sampler, copied first if other channels share it."""
    index = channel["sampler"]
    if sum(1 for c in anim["channels"] if c["sampler"] == index) > 1:
        anim["samplers"].append(dict(anim["samplers"][index]))
        channel["sampler"] = len(anim["samplers"]) - 1
    return anim["samplers"][channel["sampler"]]


def map_keys(g, node, path, fn):
    """Replaces every animation key of node's path with fn(key)."""
    for anim in g.j["animations"]:
        for c in anim["channels"]:
            if c["target"].get("node") == node and c["target"]["path"] == path:
                sampler = own_sampler(anim, c)
                rows = [fn(tuple(v)) for v in g.read(sampler["output"])]
                sampler["output"] = g.add(rows, "VEC4" if path == "rotation" else "VEC3")


def anim_rotate(g, specs):
    for spec in specs:
        node = g.find(spec["bone"])
        if node is None:
            print(f"warning: anim_rotate: no bone '{spec['bone']}'")
            continue
        extra = qaxis(spec["axis"], spec["degrees"])
        map_keys(g, node, "rotation", lambda q: qnorm(qmul(q, extra)))


def reset_joint_rotations(g, root_name, align_to_model):
    """Gives every bone under root the orientation of root's parent (or of the model axes)
    without changing the pose: H_i is bone i's bind rotation measured from that frame, rest and
    animated rotations q -> H_parent q H_i^-1, translations t -> H_parent t, and each inverse bind
    matrix O -> Rot(H_i) O."""
    root = g.find(root_name)
    if root is None:
        print(f"warning: no bone '{root_name}' to reset rotations under")
        return
    parent = g.parents()
    base = (0.0, 0.0, 0.0, 1.0)
    if align_to_model and root in parent:
        base = decompose(g.world(parent[root], parent))[1]
    frames = {}
    stack = [(root, base)]
    while stack:
        i, parent_frame = stack.pop()
        own = qnorm(qmul(parent_frame, tuple(g.j["nodes"][i]["rotation"])))
        frames[i] = (own, parent_frame)
        for c in g.j["nodes"][i].get("children", []):
            stack.append((c, own))
    frames[root] = (frames[root][0], (0.0, 0.0, 0.0, 1.0))
    for i, (own, par) in frames.items():
        n = g.j["nodes"][i]
        n["rotation"] = list(qnorm(qmul(qmul(par, tuple(n["rotation"])), qconj(own))))
        n["translation"] = list(qrot(par, n["translation"]))
        map_keys(g, i, "rotation", lambda q, own=own, par=par: qnorm(qmul(qmul(par, q), qconj(own))))
        map_keys(g, i, "translation", lambda t, par=par: qrot(par, t))
    for skin in g.j["skins"]:
        ibms = g.read(skin["inverseBindMatrices"])
        out = []
        for joint, m in zip(skin["joints"], ibms):
            if joint in frames:
                m = mat_mul(mat_from_trs((0, 0, 0), frames[joint][0], (1, 1, 1)), list(m))
            out.append(tuple(m))
        skin["inverseBindMatrices"] = g.add(out, "MAT4")


def mesh_order(g, patterns):
    """Reorders the meshes (and their nodes) so earlier patterns come first; stable otherwise."""
    j = g.j

    def rank(node):
        mesh = j["meshes"][j["nodes"][node]["mesh"]]
        names = [mesh.get("name", "")] + [g.material_name(p) for p in mesh["primitives"]]
        for k, p in enumerate(patterns):
            if any(p in n for n in names):
                return k
        return len(patterns)

    nodes = sorted(g.mesh_nodes(), key=rank)
    # meshes array in the same order (raylib keeps meshes by position, without names)
    old_meshes = j["meshes"]
    j["meshes"] = []
    for n in nodes:
        j["meshes"].append(old_meshes[j["nodes"][n]["mesh"]])
        j["nodes"][n]["mesh"] = len(j["meshes"]) - 1
    # mesh nodes in the same order among their slots in the scene
    for scene in j["scenes"]:
        slots = [k for k, n in enumerate(scene["nodes"]) if n in nodes]
        ordered = [n for n in nodes if n in scene["nodes"]]
        for k, n in zip(slots, ordered):
            scene["nodes"][k] = n


def unify_skin(g, skin_root):
    """One skin over every weighted bone plus its unweighted ancestors up to skin_root, in
    parent-before-child order, shared by every skinned mesh node."""
    j = g.j
    if not j["skins"]:
        print("warning: the model has no skin")
        return
    parent = g.parents()
    weighted = set()
    for i in g.mesh_nodes():
        node = j["nodes"][i]
        if "skin" not in node:
            continue
        joints = j["skins"][node["skin"]]["joints"]
        for p in j["meshes"][node["mesh"]]["primitives"]:
            if "JOINTS_0" not in p["attributes"]:
                continue
            for js, ws in zip(g.read(p["attributes"]["JOINTS_0"]), g.read(p["attributes"]["WEIGHTS_0"])):
                weighted.update(joints[jj] for jj, w in zip(js, ws) if w > 0)
    wanted = set(weighted)
    root = g.find(skin_root) if skin_root else None
    if skin_root and root is None:
        print(f"warning: skin_root '{skin_root}' is not a node; ignored")
    if root is not None:
        under = g.subtree(root)
        for b in weighted:
            if b not in under:
                continue
            k = parent.get(b)
            while k is not None and k != parent.get(root):
                wanted.add(k)
                k = parent.get(k)
    order = [n for n in g.preorder() if n in wanted]
    # inverse bind matrices: keep the stored ones, compute missing ones from the bind pose
    known = {}
    for skin in j["skins"]:
        for joint, m in zip(skin["joints"], g.read(skin["inverseBindMatrices"])):
            known.setdefault(joint, tuple(m))
    ibms = [known.get(n) or tuple(mat_inverse(g.world(n, parent))) for n in order]
    index = {n: k for k, n in enumerate(order)}
    new_skin = {"joints": order, "inverseBindMatrices": g.add(ibms, "MAT4")}
    for i in g.mesh_nodes():
        node = j["nodes"][i]
        if "skin" not in node:
            continue
        joints = j["skins"][node["skin"]]["joints"]
        # In place, so the mesh order stays; a mesh shared by several nodes is copied.
        if sum(1 for n in j["nodes"] if n.get("mesh") == node["mesh"]) > 1:
            j["meshes"].append(copy.deepcopy(j["meshes"][node["mesh"]]))
            node["mesh"] = len(j["meshes"]) - 1
        mesh = j["meshes"][node["mesh"]]
        for p in mesh["primitives"]:
            if "JOINTS_0" not in p["attributes"]:
                continue
            rows = []
            for js, ws in zip(g.read(p["attributes"]["JOINTS_0"]), g.read(p["attributes"]["WEIGHTS_0"])):
                rows.append(tuple(index[joints[jj]] if w > 0 else 0 for jj, w in zip(js, ws)))
            p["attributes"]["JOINTS_0"] = g.add(rows, "VEC4", 5123, target=34962)
        node["skin"] = 0
    j["skins"] = [new_skin]


def scale(g, factor):
    j = g.j
    for n in j["nodes"]:
        n["translation"] = [v * factor for v in n["translation"]]
    for m in j["meshes"]:
        for p in m["primitives"]:
            if "POSITION" in p["attributes"]:
                rows = [tuple(v * factor for v in r) for r in g.read(p["attributes"]["POSITION"])]
                p["attributes"]["POSITION"] = g.add(rows, "VEC3", target=34962, minmax=True)
    for skin in j["skins"]:
        rows = [tuple(v * factor if k in (12, 13, 14) else v for k, v in enumerate(m))
                for m in g.read(skin["inverseBindMatrices"])]
        skin["inverseBindMatrices"] = g.add(rows, "MAT4")
    for anim in j["animations"]:
        for c in anim["channels"]:
            if c["target"]["path"] == "translation":
                sampler = own_sampler(anim, c)
                rows = [tuple(v * factor for v in r) for r in g.read(sampler["output"])]
                sampler["output"] = g.add(rows, "VEC3")


def mesh_groups(g, groups, donor=None, keep=()):
    """Merges the primitives into one mesh per group (by material name). With a donor model on
    the same skeleton, only primitives matching keep stay and the donor's are added."""
    j = g.j
    if not j["skins"]:
        raise SystemExit("error: mesh_groups needs a skinned model")
    skin = j["skins"][0]
    names = [j["nodes"][n].get("name") for n in skin["joints"]]

    def primitives_of(model, joint_map):
        out = []
        for i in model.mesh_nodes():
            node = model.j["nodes"][i]
            for p in model.j["meshes"][node["mesh"]]["primitives"]:
                attrs = {k: model.read(v) for k, v in p["attributes"].items()}
                if "JOINTS_0" in attrs:
                    attrs["JOINTS_0"] = [tuple(joint_map[x] if w > 0 else 0 for x, w in zip(js, ws))
                                         for js, ws in zip(attrs["JOINTS_0"], attrs["WEIGHTS_0"])]
                out.append({"material": model.material_name(p),
                            "indices": [r[0] for r in model.read(p["indices"])] if "indices" in p
                            else list(range(len(attrs["POSITION"]))),
                            "attrs": attrs})
        return out

    prims = primitives_of(g, list(range(len(names))))
    if donor is not None:
        dskin = donor.j["skins"][0]
        dibm = donor.read(dskin["inverseBindMatrices"])
        ibm = list(g.read(skin["inverseBindMatrices"]))
        dmap = []
        for k, dj in enumerate(dskin["joints"]):
            name = donor.j["nodes"][dj].get("name")
            if name in names:
                dmap.append(names.index(name))
            else:
                node = g.find(name)
                if node is None:
                    raise SystemExit(f"error: donor joint '{name}' is not in the model")
                skin["joints"].append(node)
                names.append(name)
                ibm.append(tuple(dibm[k]))
                dmap.append(len(names) - 1)
        skin["inverseBindMatrices"] = g.add(ibm, "MAT4")
        prims = [p for p in prims if matches(p["material"], keep)] + primitives_of(donor, dmap)

    taken = set()
    new_nodes = []
    for group in groups:
        members = [k for k, p in enumerate(prims) if k not in taken and matches(p["material"], group["materials"])]
        taken.update(members)
        if not members:
            print(f"warning: mesh group '{group['name']}' is empty")
            continue
        parts = [prims[k] for k in members]
        semantics = [s for s in parts[0]["attrs"] if all(s in p["attrs"] for p in parts)]
        attributes = {}
        for s in semantics:
            rows = [r for p in parts for r in p["attrs"][s]]
            if s == "JOINTS_0":
                attributes[s] = g.add(rows, "VEC4", 5123, target=34962)
            elif s == "POSITION":
                attributes[s] = g.add(rows, "VEC3", target=34962, minmax=True)
            else:
                kind = {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}[len(rows[0])]
                attributes[s] = g.add([tuple(float(v) for v in r) for r in rows], kind, target=34962)
        indices, base = [], 0
        for p in parts:
            indices += [(i + base,) for i in p["indices"]]
            base += len(p["attrs"]["POSITION"])
        j["materials"].append({"name": group["name"], "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1.0]}})
        j["meshes"].append({"name": group["name"], "primitives": [{
            "attributes": attributes, "indices": g.add(indices, "SCALAR", 5125, target=34963),
            "material": len(j["materials"]) - 1}]})
        j["nodes"].append({"name": group["name"], "mesh": len(j["meshes"]) - 1, "skin": 0,
                           "translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]})
        new_nodes.append(len(j["nodes"]) - 1)
    left = len(prims) - len(taken)
    if left:
        print(f"warning: {left} primitive(s) matched no mesh group and were dropped")
    old = set(g.mesh_nodes()) - set(new_nodes)
    for scene in j["scenes"]:
        scene["nodes"] = [n for n in scene["nodes"] if n not in old] + new_nodes
    for i in old:
        del j["nodes"][i]["mesh"]
        j["nodes"][i].pop("skin", None)

# --------------------------------------------------------------------------- driver

def decode(wmdlt, sources, recipe, source, dest, with_anims=True):
    command = [str(wmdlt), "DECODE", str(sources / source), "--dest", str(dest), "-o", "-q"]
    if recipe.get("submodel"):
        command += ["--submodel", recipe["submodel"]]
    if with_anims:
        for anim in recipe.get("anims", []):
            command += ["--anim", str(sources / anim)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0 or not dest.exists():
        raise SystemExit(f"error: wmdlt failed on {source}:\n{result.stdout}{result.stderr}")


def clean(g, recipe, keep_meshes=False):
    """The per-model steps, in the order Arian's merge tool applies them."""
    if recipe.get("drop_meshes"):
        if not drop_meshes(g, recipe["drop_meshes"]):
            print(f"warning: no mesh matched drop_meshes {recipe['drop_meshes']}")
    if recipe.get("drop_channels"):
        drop_channels(g, recipe["drop_channels"])
    if recipe.get("anim_rotate"):
        anim_rotate(g, recipe["anim_rotate"])
    if recipe.get("strip_textures"):
        strip_textures(g)
    if recipe.get("reset_joint_rotations"):
        reset_joint_rotations(g, recipe["reset_joint_rotations"], False)
    if recipe.get("align_joints_to_model"):
        reset_joint_rotations(g, recipe["align_joints_to_model"], True)
    if recipe.get("mesh_order") and not keep_meshes:
        mesh_order(g, recipe["mesh_order"])
    unify_skin(g, recipe.get("skin_root"))
    if recipe.get("scale", 1.0) != 1.0:
        scale(g, recipe["scale"])


def convert(wmdlt, sources, out, target, recipe):
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        raw = work / "model.glb"
        decode(wmdlt, sources, recipe, recipe["source"], raw)
        g = Gltf(raw)
        clean(g, recipe)
        if recipe.get("mesh_groups"):
            donor = None
            if recipe.get("donor_source"):
                donor_raw = work / "donor.glb"
                decode(wmdlt, sources, {k: v for k, v in recipe.items() if k != "submodel"},
                       recipe["donor_source"], donor_raw, with_anims=False)
                donor = Gltf(donor_raw)
                clean(donor, recipe, keep_meshes=True)
            mesh_groups(g, recipe["mesh_groups"], donor, recipe.get("donor_keep_materials", ()))
        output = out / target / f"{recipe['name']}.glb"
        output.parent.mkdir(parents=True, exist_ok=True)
        g.save(output)
    saved = Gltf(output).j
    meshes = ", ".join(m.get("name", "?") for m in saved["meshes"])
    print(f"wrote {output} (meshes: {meshes}; {len(saved.get('animations', []))} animations)")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("targets", nargs="*", help="recipe targets, e.g. sugar nigaoe (default: all)")
    parser.add_argument("--sources", type=Path, help="folder holding nigaoe/, sugar/, ctr-medit/, ...")
    parser.add_argument("--out", type=Path, default=Path("out"), help="output folder (default: out/)")
    parser.add_argument("--wmdlt", type=Path, default=DEFAULT_WMDLT, help="the wmdlt binary")
    parser.add_argument("--recipes", type=Path, default=HERE / "recipes.toml")
    parser.add_argument("--list", action="store_true", help="list the targets")
    args = parser.parse_args()
    targets = tomllib.loads(args.recipes.read_text())["target"]
    if args.list:
        print("\n".join(targets))
        return
    if not args.sources:
        parser.error("--sources is required")
    wmdlt = args.wmdlt if args.wmdlt.exists() else shutil.which("wmdlt")
    if not wmdlt:
        sys.exit("error: wmdlt not found; build it or pass --wmdlt")
    for target in args.targets or list(targets):
        if target not in targets:
            sys.exit(f"error: no target '{target}' (have: {', '.join(targets)})")
        for recipe in targets[target]:
            convert(wmdlt, args.sources.resolve(), args.out.resolve(), target, recipe)


if __name__ == "__main__":
    main()
