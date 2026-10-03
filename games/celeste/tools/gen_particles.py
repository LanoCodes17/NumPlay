#!/usr/bin/env python3
"""Celeste's particle types (ParticleTypes.Load in the game's code) -> src/ptypes.c / ptypes.h.

    CELESTE_SRC=<the game's code, decompiled with ILSpy> python3 tools/gen_particles.py

Each `X.Y = new ParticleType(base) { ... }` becomes a PType named P_X_Y (P_Y for
ParticleTypes' own). Colors are stored unpremultiplied with their alpha."""
import math
import os
import re

SRC = os.environ.get("CELESTE_SRC", os.path.expanduser("~/Documents/Assets/gamesrc/celeste-src"))
HERE = os.path.dirname(os.path.abspath(__file__))

XNA = {"LightSkyBlue": (135, 206, 250, 255), "White": (255, 255, 255, 255), "Black": (0, 0, 0, 255), "LightGray": (211, 211, 211, 255),
       "Aqua": (0, 255, 255, 255), "Red": (255, 0, 0, 255), "Transparent": (0, 0, 0, 0)}
CONSTS = {"Player.TwoDashesHairColor": (0xFF, 0x6D, 0xEF, 255), "Player.NormalHairColor": (0xAC, 0x32, 0x32, 255),
          "Player.UsedHairColor": (0x44, 0xB7, 0xFF, 255), "Player.FlyPowerHairColor": (0xF2, 0xEB, 0x6D, 255),
          "Water.SurfaceColor": (int(135 * .8), int(206 * .8), int(250 * .8), int(255 * .8)),
          "RisingLava.Hot[0]": (0xFF, 0x89, 0x33, 255), "RisingLava.Hot[1]": (0xF2, 0x5E, 0x29, 255),
          "RisingLava.Hot[2]": (0xD0, 0x1C, 0x01, 255), "RisingLava.Cold[0]": (0x33, 0xFF, 0xE7, 255),
          "RisingLava.Cold[1]": (0x4C, 0xA2, 0xEB, 255), "RisingLava.Cold[2]": (0x01, 0x51, 0xD0, 255)}


class Col:
    def __init__(self, r, g, b, a=255):
        self.v = (r, g, b, a)

    def __mul__(self, k):
        r, g, b, a = self.v
        return Col(int(r * k), int(g * k), int(b * k), int(a * k))


def hexcol(h):
    h = h.lstrip("#")
    return Col(int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), int(h[6:8], 16) if len(h) >= 8 else 255)


def lerp(a, b, t):
    return Col(*(int(x + (y - x) * t) for x, y in zip(a.v, b.v)))


def py_expr(e):
    e = e.strip()
    e = re.sub(r"(\d+(\.\d+)?)f\b", r"\1", e)
    e = e.replace("(float)Math.PI", "math.pi").replace("Math.PI", "math.pi").replace("(float)", "")
    e = re.sub(r'Calc\.HexToColor\("([0-9A-Fa-f]+)"\)', r'hexcol("\1")', e)
    e = re.sub(r"Color\.Lerp\(", "lerp(", e)
    for k, v in CONSTS.items():
        e = e.replace(k, "Col(%d,%d,%d,%d)" % v)
    e = e.replace("Color.White * ", "Col(255,255,255,255) * ")
    e = re.sub(r"Color\.(\w+)", lambda m: "Col(%d,%d,%d,%d)" % XNA[m.group(1)], e)
    e = re.sub(r"new Vector2\(([^,]+),([^)]+)\)", r"(\1,\2)", e)
    e = e.replace("Vector2.UnitY", "(0,1)").replace("Vector2.UnitX", "(1,0)").replace("Vector2.Zero", "(0,0)")
    e = re.sub(r"ParticleType\.\w+\.(\w+)", r'"\1"', e)
    e = e.replace("true", "True").replace("false", "False")
    return e


def vmul(a, k):
    return (a[0] * k, a[1] * k)


def evaluate(e, choosers):
    pe = py_expr(e)
    m = re.match(r'GFX\.Game\["([^"]+)"\]', pe)
    if m:
        return ("tex", [m.group(1)])
    if pe in choosers:
        return ("tex", choosers[pe])
    m = re.match(r"\((-?[\d.]+), *(-?[\d.]+)\) *\* *(-?[\d.]+)", pe)
    if m:
        return (float(m.group(1)) * float(m.group(3)), float(m.group(2)) * float(m.group(3)))
    return eval(pe, {"math": math, "hexcol": hexcol, "lerp": lerp, "Col": Col})


DEFAULTS = dict(Color=Col(255, 255, 255), Color2=Col(255, 255, 255), ColorMode="Static", FadeMode="None",
                SpeedMin=0, SpeedMax=0, SpeedMultiplier=1, Acceleration=(0, 0), Friction=0, Direction=0,
                DirectionRange=0, LifeMin=0, LifeMax=0, Size=2, SizeRange=0, SpinMin=0, SpinMax=0,
                SpinFlippedChance=False, RotationMode="None", ScaleOut=False, UseActualDeltaTime=False,
                Source=None)


def main():
    text = open(os.path.join(SRC, "Celeste", "ParticleTypes.cs")).read()
    body = text[text.index("public static void Load()"):]
    choosers = {}
    for m in re.finditer(r"Chooser<MTexture> (\w+) = new Chooser<MTexture>\(([^;]+)\);", body):
        choosers[m.group(1)] = re.findall(r'GFX\.Game\["([^"]+)"\]', m.group(2))
    types = {}
    order = []
    # statements of Load(), split at top-level semicolons
    stmts, depth, cur = [], 0, ""
    for ch in body[body.index("{") + 1:]:
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        if ch == ";" and depth == 0:
            stmts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    for st in stmts:
        m = re.match(r"([\w.]+) = new ParticleType(\(([\w.]+)\))?\s*(\{(.*)\})?$", st, re.S)
        if m:
            name, base, fields = m.group(1), m.group(3), m.group(5) or ""
            t = dict(types[base]) if base else dict(DEFAULTS)
            for fm in re.finditer(r"(\w+) = (.+?),?\n", fields + "\n"):
                k, v = fm.group(1), fm.group(2).rstrip(",")
                if k == "SourceChooser":
                    k = "Source"
                t[k] = evaluate(v, choosers)
            types[name] = t
            if name not in order:
                order.append(name)
            continue
        m = re.match(r"([\w.]+) = ([\w.]+)$", st)
        if m and m.group(2) in types:
            types[m.group(1)] = dict(types[m.group(2)])
            order.append(m.group(1))
            continue
        m = re.match(r"([\w.]+)\.(\w+) = (.+)$", st, re.S)
        if m and m.group(1) in types:
            k = "Source" if m.group(2) == "SourceChooser" else m.group(2)
            types[m.group(1)][k] = evaluate(m.group(3), choosers)
    textures = set()
    H = ["/* Generated by tools/gen_particles.py from the game's ParticleTypes. */", "#ifndef PTYPES_H",
         "#define PTYPES_H", '#include "fx.h"']
    C = ["/* Generated by tools/gen_particles.py from the game's ParticleTypes. */", '#include "fx.h"', ""]
    modes_c = {"Static": "PC_STATIC", "Choose": "PC_CHOOSE", "Blink": "PC_BLINK", "Fade": "PC_FADE"}
    modes_f = {"None": "PF_NONE", "Linear": "PF_LINEAR", "Late": "PF_LATE", "InAndOut": "PF_INOUT"}
    modes_r = {"None": "PR_NONE", "Random": "PR_RANDOM", "SameAsDirection": "PR_SAMEASDIR"}

    def col(c):
        r, g, b, a = c.v
        if a:
            r, g, b = (min(255, round(x * 255 / a)) for x in (r, g, b))
        return "0x%02X%02X%02X%02X" % (a, r, g, b)

    def f(x):
        v = "%.7g" % x
        if "." not in v and "e" not in v:
            v += ".0"
        return v + "f"
    for name in order:
        t = types[name]
        cname = "P_" + name.replace("ParticleTypes.", "").replace(".", "_")
        src = t["Source"][1] if t["Source"] else []
        textures.update(src)
        srcs = ", ".join("T_" + re.sub(r"[^A-Za-z0-9]", "_", s) for s in src) or "0"
        H.append("extern const PType %s;" % cname)
        C.append("const PType %s = {%s, %s, %s, %s, %s, %d, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %d, {%s, %s}, %s, {%s}, %d, %d};" % (
            cname, col(t["Color"]), col(t["Color2"]), modes_c[t["ColorMode"]], modes_f[t["FadeMode"]],
            modes_r[t["RotationMode"]], 1 if t["ScaleOut"] else 0, f(t["Size"]), f(t["SizeRange"]),
            f(t["SpeedMin"]), f(t["SpeedMax"]), f(t["SpeedMultiplier"]), f(t["LifeMin"]), f(t["LifeMax"]),
            f(t["Direction"]), f(t["DirectionRange"]), f(t["SpinMin"]), f(t["SpinMax"]),
            1 if t["SpinFlippedChance"] else 0, f(t["Acceleration"][0]), f(t["Acceleration"][1]), f(t["Friction"]),
            srcs, len(src), 1 if t["UseActualDeltaTime"] else 0))
    H.append("#endif")
    open(os.path.join(HERE, "..", "src", "ptypes.h"), "w").write("\n".join(H) + "\n")
    open(os.path.join(HERE, "..", "src", "ptypes.c"), "w").write("\n".join(C) + "\n")
    open(os.path.join(HERE, "ptype_textures.txt"), "w").write("\n".join(sorted(textures)) + "\n")
    print(len(order), "particle types,", len(textures), "textures")


if __name__ == "__main__":
    main()
