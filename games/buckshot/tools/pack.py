#!/usr/bin/env python3
"""Packs assets/ (plates, sprites, art.json) into src/assets.c and src/assets.h.

Every picture is stored as indices into one shared palette (the original's
post-processing leaves few colors), index 0 being transparent, coded with
tools/pixcode.py, which src/pixcode.c decodes straight into the frame buffer.

Usage: pack.py
"""
import json
import os
import re
from pathlib import Path

import numpy as np
from PIL import Image

from pixcode import decode, encode

VERIFY = bool(os.environ.get("BR_VERIFY"))

HERE = Path(__file__).resolve().parent
A = HERE.parent / "assets"
SRC = HERE.parent / "src"

# colors the interface draws with, first in the palette: index 1.. (names for assets.h)
# Pictures drawn with another's data, moved to where their own render sits:
# items in neighbouring slots of a row look alike, so a row shares one render.
ALIAS = {}
for _i in range(9):
    for _g in (0, 1, 5):  # your side: its far row and its near row look alike
        ALIAS[f"it{_i}p{_g}"] = f"it{_i}p4"
    for _g in (2, 3, 7):
        ALIAS[f"it{_i}p{_g}"] = f"it{_i}p6"
    for _g in (1, 4, 5):
        ALIAS[f"it{_i}d{_g}"] = f"it{_i}d0"
    for _g in (3, 6, 7):
        ALIAS[f"it{_i}d{_g}"] = f"it{_i}d2"
    for _g in (1, 4, 5):  # the Dealer view: his far row, and his near row
        ALIAS[f"di{_i}{_g}"] = f"di{_i}0"
    for _g in (3, 6, 7):
        ALIAS[f"di{_i}{_g}"] = f"di{_i}2"
HALF_SPRITES = set()  # sprites cut from 160x120 renders
LOGO_POS = (14, 18)
SKIP = {"p_mag_l", "p_mag_b", "d_gun_aimp_saw", "p_inv0"}  # the barrel turned to you: the close-up follows

UI = [("BLACK", (0, 0, 0)), ("WHITE", (255, 255, 255)), ("GREY", (146, 146, 146)), ("DARK", (36, 36, 36)),
      ("RED", (219, 36, 36)), ("BLUE", (36, 73, 219)), ("DOT", (219, 255, 219)), ("YELLOW", (255, 219, 109))]


def ident(name):
    return re.sub(r"\W", "_", name).upper()


def main():
    art = json.loads((A / "art.json").read_text())
    pics = []  # (name, x, y, rgba array)
    half = set()
    for name, file in art["plates"].items():
        a = np.array(Image.open(A / file).convert("RGBA"))
        pics.append(("PL_" + ident(name), 0, 0, a))
        if a.shape[1] == 160:
            half.add("PL_" + ident(name))
    pics.append(("LOGO", LOGO_POS[0], LOGO_POS[1], np.array(Image.open(A / "logo.png").convert("RGBA"))))
    alias = []
    for name, pos in art["sprites"].items():
        if pos is None or name in SKIP:
            continue
        a = np.array(Image.open(A / "sprites" / f"{name}.png").convert("RGBA"))
        if name in ALIAS:
            src = ALIAS[name]
            b = np.array(Image.open(A / "sprites" / f"{src}.png").convert("RGBA"))
            ca = np.argwhere(a[..., 3] > 0).mean(axis=0)
            cb = np.argwhere(b[..., 3] > 0).mean(axis=0)
            sp = art["sprites"][src]
            alias.append((ident(name), ident(src), int(round(sp[0] + ca[1] + pos[0] - sp[0] - cb[1])),
                           int(round(sp[1] + ca[0] + pos[1] - sp[1] - cb[0]))))
            continue
        if name in HALF_SPRITES:
            half.add(ident(name))
            pos = [pos[0] * 2, pos[1] * 2]
        pics.append((ident(name), pos[0], pos[1], a))
    # shell in the chamber, over the magnifier pose
    for k in "lb":
        a = np.array(Image.open(A / "sprites" / f"p_mag_{k}.png").convert("RGBA"))
        b = np.array(Image.open(A / "sprites" / "p_mag.png").convert("RGBA"))
        d = np.any(a != b, axis=2)
        ys, xs = np.nonzero(d)
        y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        c = a[y0:y1, x0:x1].copy()
        c[..., 3] = d[y0:y1, x0:x1] * 255
        p = art["sprites"]["p_mag"]
        pics.append((f"P_MAG_{k.upper()}", p[0] + int(x0), p[1] + int(y0), c))
    colors = {c: i + 1 for i, (_, c) in enumerate(UI)}
    found = set()
    for _, _, _, a in pics:
        px = a[a[..., 3] > 0][:, :3]
        found |= {tuple(int(v) for v in t) for t in np.unique(px, axis=0)}
    for c in sorted(found - set(colors), key=lambda c: (c[0] * 3 + c[1] * 5 + c[2] * 2, c)):
        colors[c] = len(colors) + 1  # by brightness: neighbouring indices look alike
    assert len(colors) < 128, len(colors)
    lut = np.zeros(1 << 24, np.uint8)
    for c, i in colors.items():
        lut[c[0] << 16 | c[1] << 8 | c[2]] = i
    blob = bytearray()
    table = []
    seen = {}
    for name, x, y, a in pics:
        key = (x, y, a.shape, a.tobytes())
        if key in seen:
            alias.append((name, seen[key], x, y))
            continue
        seen[key] = name
        rgb = a[..., :3].astype(np.uint32)
        idx = lut[rgb[..., 0] << 16 | rgb[..., 1] << 8 | rgb[..., 2]]
        idx[a[..., 3] == 0] = 0
        z = encode(idx)
        if VERIFY:
            assert (decode(z, idx.shape[1], idx.shape[0]) == idx).all(), name
        table.append((name, x, y, a.shape[1], a.shape[0], len(blob) | (1 << 31 if name in half else 0)))
        blob += z
    byname = {t[0]: t for t in table}
    for name, src, x, y in alias:
        t = byname[src]
        table.append((name, x, y, t[3], t[4], t[5]))
    # keep the pictures in their listed order: the game counts on runs of them
    # (the 16 slots of an item, the shells, the symbols of the display)
    order = [p[0] for p in pics] + [a[0] for a in alias if a[0] not in {p[0] for p in pics}]
    rank = {n: i for i, n in enumerate(order)}
    listed = [ident(n) for n, pos in art["sprites"].items() if pos is not None]
    for i, n in enumerate(listed):
        rank.setdefault(n, len(order) + i)
    plates = [p[0] for p in pics if p[0].startswith("PL_")]
    seq = plates + ["LOGO"] + [n for n in listed if n in {t[0] for t in table}]
    seq += [t[0] for t in table if t[0] not in seq]
    pos_of = {n: i for i, n in enumerate(seq)}
    table.sort(key=lambda t: pos_of[t[0]])
    pal = sorted(colors.items(), key=lambda kv: kv[1])
    rgb565 = [0] + [(r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3 for (r, g, b), _ in pal]
    rgb888 = [0] + [r << 16 | g << 8 | b for (r, g, b), _ in pal]

    h = ["/* Generated by tools/pack.py from assets/: do not edit. */", "#ifndef BR_ASSETS_H", "#define BR_ASSETS_H",
         "#include <stdint.h>", "", "typedef struct {", "  int16_t x, y;", "  uint16_t w, h;", "  uint32_t off; /* top bit: stored at half size, drawn doubled */",
         "} img_t;", "", "enum {"]
    for name, *_ in table:
        h.append(f"  IMG_{name},")
    h += ["  IMG_COUNT", "};", "enum {"]
    for i, (n, _) in enumerate(UI):
        h.append(f"  C_{n} = {i + 1},")
    h += ["};", f"#define PAL_COUNT {len(rgb565)}", "extern const img_t art_img[IMG_COUNT + 1];",
          "extern const uint8_t art_data[];", "extern const uint32_t art_pal[PAL_COUNT];",
          "extern const int16_t slot_rect[2][8][4];", "extern const int16_t dslot_rect[8][4];", "#endif"]
    (SRC / "assets.h").write_text("\n".join(h) + "\n")

    # the screen rectangle of each table slot, from the items drawn in it (for the cursor):
    # the table view (player, Dealer) and the Dealer view (his slots)
    def rect(name):
        p = art["sprites"].get(name)
        im = Image.open(A / "sprites" / f"{name}.png")
        return [p[0] - 3, p[1] - 3, im.width + 6, im.height + 6]
    rects = [[rect(f"it3{side}{g}") for g in range(8)] for side in "pd"]
    drects = [rect(f"di3{g}") for g in range(8)]

    c = ["/* Generated by tools/pack.py from assets/: do not edit. */", '#include "assets.h"',
         "const img_t art_img[IMG_COUNT + 1] = {"]
    for name, x, y, w, hh, off in table:
        c.append(f"  {{{x}, {y}, {w}, {hh}, {off}}},")
    c.append(f"  {{0, 0, 0, 0, {len(blob)}}},")
    c.append("};")
    c.append(f"const uint32_t art_pal[PAL_COUNT] = {{{','.join(hex(v) for v in rgb888)}}};")
    c.append("const int16_t slot_rect[2][8][4] = {" + ",".join(
        "{" + ",".join("{" + ",".join(map(str, r)) + "}" for r in rs) + "}" for rs in rects) + "};")
    c.append("const int16_t dslot_rect[8][4] = {" + ",".join("{" + ",".join(map(str, r)) + "}" for r in drects) + "};")
    blob += bytes(4)  # the decoder may read a little ahead
    c.append("const uint8_t art_data[] = {")
    for k in range(0, len(blob), 32):
        c.append(",".join(str(b) for b in blob[k:k + 32]) + ",")
    c.append("};")
    (SRC / "assets.c").write_text("\n".join(c) + "\n")
    print(f"{len(table)} pictures, {len(colors)} colors, {len(blob)} bytes")
    sizes = {}
    for (name, *_ ), nxt in zip(table, table[1:] + [(None, 0, 0, 0, 0, len(blob))]):
        pass
    real = [t for t in table if t[0] not in {a[0] for a in alias}]
    offs = [t[5] & 0x7FFFFFFF for t in real] + [len(blob)]
    groups = {}
    for i, t in enumerate(real):
        k = re.match(r"[A-Z]+_?[A-Z]*", t[0]).group(0)[:6]
        groups[k] = groups.get(k, 0) + offs[i + 1] - offs[i]
    print(sorted(groups.items(), key=lambda kv: -kv[1]))


if __name__ == "__main__":
    main()
