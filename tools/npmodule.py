#!/usr/bin/env python3
"""Turn a game's partially linked object into a NumPlay module.

The input is the game linked with `ld -r -T tools/module.ld`: one `.text`
section (code and read-only data), one `.data` and one `.bss`. The output is
a relocatable object where:

- the code keeps its bytes and relocations, in a section named after the game
  (`.rodata.np.<n>.<game>`), so the launcher link can keep each game in one
  contiguous block of flash;
- `.data` becomes a read-only copy (`...init`) that the launcher copies into
  RAM before starting the game;
- nothing is allocated in RAM any more: every reference to the game's
  `.data` or `.bss` is redirected to the symbols `np_<game>_data` and
  `np_<game>_bss`, which the launcher places inside one arena shared by all
  games (only one game runs at a time);
- `main` is renamed `np_<game>_main` and is the only global definition left.

It also writes a small JSON file with the section sizes for the build.

Usage: npmodule.py in.o out.o --game NAME --index N --json sizes.json
"""
import argparse
import json
import struct
import sys

SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_REL, SHT_NOBITS = 0, 1, 2, 3, 9, 8
SHT_ARM_ATTRIBUTES = 0x70000003
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR, SHF_INFO_LINK = 1, 2, 4, 0x40
STB_LOCAL, STB_GLOBAL, STB_WEAK = 0, 1, 2
STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION = 0, 1, 2, 3
SHN_UNDEF, SHN_ABS = 0, 0xFFF1
R_ARM_ABS32 = 2

# Only these relocation types may point into the game's RAM.
RAM_RELOCS = {R_ARM_ABS32}


def fail(msg):
    sys.exit("npmodule: " + msg)


class Elf:
    def __init__(self, data):
        self.d = data
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
            fail("expected a 32-bit little-endian ELF")
        (self.type, self.machine) = struct.unpack_from("<HH", data, 16)
        if self.type != 1 or self.machine != 40:
            fail("expected an ARM relocatable object")
        self.flags, = struct.unpack_from("<I", data, 0x24)
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        self.secs = []
        for i in range(shnum):
            f = struct.unpack_from("<10I", data, shoff + i * shentsize)
            s = dict(zip("name type flags addr off size link info align entsize".split(), f))
            s["data"] = b"" if s["type"] == SHT_NOBITS else bytes(data[s["off"]:s["off"] + s["size"]])
            self.secs.append(s)
        shstr = self.secs[shstrndx]["data"]
        for s in self.secs:
            s["sname"] = shstr[s["name"]:shstr.index(b"\0", s["name"])].decode()
        symtabs = [i for i, s in enumerate(self.secs) if s["type"] == SHT_SYMTAB]
        if len(symtabs) != 1:
            fail("expected one symbol table")
        self.symtab_i = symtabs[0]
        st = self.secs[self.symtab_i]
        strtab = self.secs[st["link"]]["data"]
        self.syms = []
        for k in range(st["size"] // 16):
            name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", st["data"], 16 * k)
            self.syms.append(dict(name=strtab[name:strtab.index(b"\0", name)].decode(), value=value,
                                  size=size, bind=info >> 4, type=info & 15, other=other, shndx=shndx))

    def rels_for(self, target):
        out = []
        for s in self.secs:
            if s["type"] == SHT_REL and s["info"] == target:
                for k in range(s["size"] // 8):
                    off, info = struct.unpack_from("<II", s["data"], 8 * k)
                    out.append([off, info & 255, info >> 8])
            elif s["type"] == 4 and s["info"] == target:  # SHT_RELA
                fail("RELA relocations are not supported")
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--game", required=True)
    ap.add_argument("--index", type=int, required=True)
    ap.add_argument("--entry", default="main")
    ap.add_argument("--json")
    a = ap.parse_args()
    g = a.game
    e = Elf(open(a.inp, "rb").read())

    by_name = {}
    for i, s in enumerate(e.secs):
        if s["flags"] & SHF_ALLOC:
            if s["sname"] not in (".text", ".data", ".bss"):
                fail(f"unexpected allocated section {s['sname']} (update tools/module.ld)")
            by_name[s["sname"]] = i
    if ".text" not in by_name:
        fail("no .text section")
    text_i = by_name[".text"]
    data_i = by_name.get(".data")
    bss_i = by_name.get(".bss")
    text = e.secs[text_i]
    data = e.secs[data_i] if data_i is not None else None
    bss = e.secs[bss_i] if bss_i is not None else None
    data_size = data["size"] if data else 0
    bss_size = bss["size"] if bss else 0
    ram_align = max([8] + [s["align"] for s in (data, bss) if s])

    # ---- symbols of the output
    # index 0: null, 1: code section, 2: init section, then locals, then globals
    out_syms = [dict(name="", value=0, size=0, bind=0, type=0, other=0, shndx=0)]
    CODE, INIT = 1, 3  # section indexes (2 and 4 hold their relocations)
    out_syms.append(dict(name="", value=0, size=0, bind=STB_LOCAL, type=STT_SECTION, other=0, shndx=CODE))
    out_syms.append(dict(name="", value=0, size=0, bind=STB_LOCAL, type=STT_SECTION, other=0, shndx=INIT))
    remap = {}           # old symbol index -> new symbol index
    ram_sym = {}         # old symbol index -> (region, offset) for symbols defined in RAM
    entry_found = False
    globals_out = []
    undefined = {}
    for k, s in enumerate(e.syms):
        if k == 0:
            continue
        sh = s["shndx"]
        if s["type"] == STT_SECTION:
            if sh == text_i:
                remap[k] = 1  # the code section's symbol
            elif sh == data_i:
                ram_sym[k] = ("data", 0)
            elif sh == bss_i:
                ram_sym[k] = ("bss", 0)
            continue
        if s["type"] == 4:  # STT_FILE
            continue
        if sh == data_i and data_i is not None:
            ram_sym[k] = ("data", s["value"])
            continue
        if sh == bss_i and bss_i is not None:
            ram_sym[k] = ("bss", s["value"])
            continue
        if sh == SHN_UNDEF:
            if s["bind"] == STB_LOCAL:
                continue
            if s["name"] not in undefined:
                undefined[s["name"]] = dict(name=s["name"], value=0, size=0, bind=s["bind"],
                                            type=STT_NOTYPE, other=0, shndx=SHN_UNDEF)
            continue
        if sh == text_i:
            n = dict(s, shndx=CODE)
            if s["name"] == a.entry and s["bind"] != STB_LOCAL:
                n["name"] = f"np_{g}_main"
                n["bind"] = STB_GLOBAL
                n["other"] = 0
                entry_found = True
                globals_out.append((k, n))
            else:
                # every other definition stays private to the module
                n["bind"] = STB_LOCAL
                n["other"] = 0
                remap[k] = len(out_syms)
                out_syms.append(n)
            continue
        if sh == SHN_ABS:
            n = dict(s, bind=STB_LOCAL)
            remap[k] = len(out_syms)
            out_syms.append(n)
            continue
        fail(f"symbol {s['name']} defined in unexpected section {sh}")
    if not entry_found:
        fail(f"entry symbol {a.entry} not found")

    first_global = len(out_syms)
    for k, n in globals_out:
        remap[k] = len(out_syms)
        out_syms.append(n)
    # start of the .data image the launcher copies into RAM
    out_syms.append(dict(name=f"np_{g}_init", value=0, size=data_size, bind=STB_GLOBAL, type=STT_OBJECT,
                         other=0, shndx=INIT))
    ram_index = {}
    for region in ("data", "bss"):
        ram_index[region] = len(out_syms)
        out_syms.append(dict(name=f"np_{g}_{region}", value=0, size=0, bind=STB_GLOBAL,
                             type=STT_NOTYPE, other=0, shndx=SHN_UNDEF))
    und_index = {}
    for name, n in undefined.items():
        und_index[name] = len(out_syms)
        out_syms.append(n)
    for k, s in enumerate(e.syms):
        if k and s["shndx"] == SHN_UNDEF and s["bind"] != STB_LOCAL and s["type"] != STT_SECTION:
            remap[k] = und_index[s["name"]]

    # ---- relocations
    def convert(target_i, blob):
        blob = bytearray(blob)
        out = []
        for off, typ, si in e.rels_for(target_i):
            if si in ram_sym:
                region, value = ram_sym[si]
                if typ not in RAM_RELOCS:
                    fail(f"relocation type {typ} at {off:#x} points into RAM; only ABS32 is handled")
                if value:
                    addend, = struct.unpack_from("<I", blob, off)
                    struct.pack_into("<I", blob, off, (addend + value) & 0xFFFFFFFF)
                out.append((off, typ, ram_index[region]))
            elif si in remap:
                out.append((off, typ, remap[si]))
            else:
                fail(f"relocation at {off:#x} uses unsupported symbol {e.syms[si]}")
        return bytes(blob), out

    code_bytes, code_rels = convert(text_i, text["data"])
    init_bytes, init_rels = (b"", [])
    if data:
        init_bytes, init_rels = convert(data_i, data["data"])
    for s in e.secs:
        if s["type"] == SHT_REL and s["info"] not in (text_i, data_i):
            if e.secs[s["info"]]["flags"] & SHF_ALLOC:
                fail(f"relocations for unexpected section {e.secs[s['info']]['sname']}")

    # ---- write the object
    base = f".rodata.np.{a.index}.{g}"
    names = ["", base, ".rel" + base, base + ".init", ".rel" + base + ".init", ".symtab", ".strtab",
             ".shstrtab"]
    attrs = [s for s in e.secs if s["type"] == SHT_ARM_ATTRIBUTES]
    if attrs:
        names.append(".ARM.attributes")
    shstr = bytearray(b"\0")
    name_off = []
    for n in names:
        if not n:
            name_off.append(0)
            continue
        name_off.append(len(shstr))
        shstr += n.encode() + b"\0"

    strtab = bytearray(b"\0")
    str_off = {}

    def add_str(s):
        if not s:
            return 0
        if s not in str_off:
            str_off[s] = len(strtab)
            strtab.extend(s.encode() + b"\0")
        return str_off[s]

    symtab = bytearray()
    for s in out_syms:
        symtab += struct.pack("<IIIBBH", add_str(s["name"]), s["value"], s["size"],
                              (s["bind"] << 4) | s["type"], s["other"], s["shndx"])

    def rel_bytes(rels):
        return b"".join(struct.pack("<II", off, (si << 8) | typ) for off, typ, si in rels)

    code_align = max(4, text["align"])
    bodies = [
        None,
        (SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, code_bytes, 0, 0, code_align, 0),
        (SHT_REL, SHF_INFO_LINK, rel_bytes(code_rels), 5, 1, 4, 8),
        (SHT_PROGBITS, SHF_ALLOC, init_bytes, 0, 0, 4 if not data else max(4, data["align"]), 0),
        (SHT_REL, SHF_INFO_LINK, rel_bytes(init_rels), 5, 3, 4, 8),
        (SHT_SYMTAB, 0, bytes(symtab), 6, first_global, 4, 16),
        (SHT_STRTAB, 0, bytes(strtab), 0, 0, 1, 0),
        (SHT_STRTAB, 0, bytes(shstr), 0, 0, 1, 0),
    ]
    if attrs:
        bodies.append((SHT_ARM_ATTRIBUTES, 0, attrs[0]["data"], 0, 0, 1, 0))

    out = bytearray(52)
    headers = [bytes(40)]
    for i, b in enumerate(bodies):
        if b is None:
            continue
        typ, flags, blob, link, info, align, entsize = b
        while len(out) % max(align, 4):
            out.append(0)
        off = len(out)
        out += blob
        headers.append(struct.pack("<10I", name_off[i], typ, flags, 0, off, len(blob), link, info, align, entsize))
    while len(out) % 4:
        out.append(0)
    shoff = len(out)
    for h in headers:
        out += h
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<16sHHIIIIIHHHHHH", out, 0, ident, 1, 40, 1, 0, 0, shoff, e.flags, 52, 0, 0, 40,
                     len(headers), 7)
    open(a.out, "wb").write(out)

    info = dict(game=g, index=a.index, code=len(code_bytes), data=data_size, bss=bss_size,
                ram_align=ram_align, undefined=sorted(undefined))
    if a.json:
        json.dump(info, open(a.json, "w"), indent=1)
    print(f"module {g}: code+rodata {len(code_bytes)} B, data {data_size} B, bss {bss_size} B")


if __name__ == "__main__":
    main()
