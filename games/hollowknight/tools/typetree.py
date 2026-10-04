"""Type trees for the game's MonoBehaviours, made from its own assemblies (TypeTreeGeneratorAPI), and a reader
for their serialized fields."""
import struct, functools
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator
GEN = None
MANAGED = None          # the game's Managed folder (set by unity.py)
UNITY_VERSION = "6000.0.61f1"


def gen():
    global GEN
    if GEN is None:
        GEN = TypeTreeGenerator(UNITY_VERSION)
        GEN.load_local_dll_folder(MANAGED)
    return GEN

class Node:
    __slots__ = ("level", "type", "name", "flag", "children")
    def __init__(s, l, t, n, f): s.level, s.type, s.name, s.flag, s.children = l, t, n, f, []

@functools.lru_cache(maxsize=None)
def tree(asm, full):
    base = gen().get_nodes(asm if asm.endswith(".dll") else asm + ".dll", full)
    root = None; stack = []
    for b in base:
        n = Node(b.m_Level, b.m_Type, b.m_Name, b.m_MetaFlag or 0)
        if b.m_Level == 1 and b.m_Name == "m_Enabled": n.flag |= 0x4000
        while stack and stack[-1].level >= n.level: stack.pop()
        if stack: stack[-1].children.append(n)
        else: root = n
        stack.append(n)
    return root

PRIM = {"int": "<i", "SInt32": "<i", "UInt32": "<I", "unsigned int": "<I", "float": "<f", "double": "<d", "SInt64": "<q",
        "UInt64": "<Q", "SInt16": "<h", "UInt16": "<H", "short": "<h", "unsigned short": "<H", "SInt8": "<b", "UInt8": "<B",
        "char": "<B", "bool": "<B", "long long": "<q", "FileSize": "<Q"}

class R:
    def __init__(s, b): s.b, s.p = b, 0
    def align(s): s.p = (s.p + 3) & ~3

def read(node, r, path=""):
    p = path + "/" + node.name
    if node.children and node.children[0].type == "Array":
        arr = node.children[0]
        n = struct.unpack_from("<i", r.b, r.p)[0]; r.p += 4
        if n < 0 or n > 10_000_000: raise ValueError(f"bad array len {n} at {r.p} in {p}")
        el = arr.children[1]
        if el.type in ("UInt8", "char", "SInt8", "bool") and not el.children:
            v = r.b[r.p:r.p + n]; r.p += n
        else:
            v = [read(el, r, p + f"[{i}]") for i in range(n)]
        if arr.flag & 0x4000: r.align()
    elif node.type in PRIM:
        fmt = PRIM[node.type]; v = struct.unpack_from(fmt, r.b, r.p)[0]; r.p += struct.calcsize(fmt)
    elif node.type == "string":
        n = struct.unpack_from("<i", r.b, r.p)[0]
        if n < 0 or r.p + 4 + n > len(r.b): raise ValueError(f"bad string len {n} at {r.p} in {p}")
        v = r.b[r.p + 4:r.p + 4 + n].decode("utf8", "replace"); r.p += 4 + n; r.align()
    elif node.type == "Array":
        raise ValueError("bare array " + p)
    else:
        v = {}
        for c in node.children: v[c.name] = read(c, r, p)
    if node.flag & 0x4000: r.align()
    return v

def parse(obj, script):
    full = (script.m_Namespace + "." if script.m_Namespace else "") + script.m_ClassName
    t = tree(script.m_AssemblyName, full)
    raw = obj.get_raw_data()
    r = R(raw)
    v = read(t, r)
    return v, r.p, len(raw)
