"""Celeste's fonts (Renogare, at the sizes the engine draws: the game's 1920x1080
interface shown at 1/6) and its English dialog, for pack.py."""
import os
import re
import struct

import numpy as np
from PIL import Image

from cel import CELESTE, Atlas

# (face file, scale): body text (64 / 6 = 10.7 px), and the 192 face at 2x and 3x the body
FONT_SIZES = [("renogare64", 1 / 6), ("renogare192", 1 / 9)]


def read_fnt(name):
    t = open(os.path.join(CELESTE, "Dialog", "Fonts", name + ".fnt")).read()
    chars = {}
    for m in re.finditer(r'<char id="(\d+)" x="(\d+)" y="(\d+)" width="(\d+)" height="(\d+)" xoffset="(-?\d+)" '
                         r'yoffset="(-?\d+)" xadvance="(-?\d+)"', t):
        chars[int(m.group(1))] = tuple(int(v) for v in m.groups()[1:])
    kern = {}
    for m in re.finditer(r'<kerning first="(\d+)" second="(\d+)" amount="(-?\d+)"', t):
        kern[(int(m.group(1)), int(m.group(2)))] = int(m.group(3))
    common = re.search(r'lineHeight="(\d+)" base="(\d+)"', t)
    return chars, kern, int(common.group(1)), int(common.group(2))


def font_section(W, codes):
    """FONTS: u16 count, u32 offsets; per size: u16 nglyphs, u16 line height x16, u16 base x16, u16 nkern,
    glyphs (u16 code, s16 xo x16, s16 yo, u8 w, u8 h, u16 advance x16, u32 bitmap offset), kerning (u16 a, u16 b, s16 x16),
    then 4-bit alpha bitmaps (rows padded to bytes)."""
    gui = Atlas("Gui")
    out = []
    for face, scale in FONT_SIZES:
        chars, kern, line_h, base = read_fnt(face)
        page = gui[face + "_0"].image()[..., 3]
        glyphs = []
        bitmaps = bytearray()
        for code in sorted(codes):
            if code not in chars:
                continue
            x, y, w, h, xo, yo, xa = chars[code]
            if w and h:
                g = page[y:y + h, x:x + w].astype(np.float32)
                # place at the scaled offset's fraction, then area-scale
                fx, fy = xo * scale - np.floor(xo * scale), yo * scale - np.floor(yo * scale)
                ow, oh = int(np.ceil(w * scale + fx)) + 1, int(np.ceil(h * scale + fy)) + 1
                big = np.zeros((int(round(oh / scale)) + 2, int(round(ow / scale)) + 2), np.float32)
                ox, oy = int(round(fx / scale)), int(round(fy / scale))
                big[oy:oy + h, ox:ox + w] = g
                im = Image.fromarray(big).resize((ow, oh), Image.BOX)
                a = np.array(im)
                # trim
                rows = np.where(a.max(axis=1) > 8)[0]
                cols = np.where(a.max(axis=0) > 8)[0]
                if len(rows) and len(cols):
                    a = a[rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1]
                    gx, gy = int(np.floor(xo * scale)) + cols[0], int(np.floor(yo * scale)) + rows[0]
                else:
                    a = np.zeros((0, 0))
                    gx = gy = 0
            else:
                a = np.zeros((0, 0))
                gx = gy = 0
            q = np.clip(np.round(a / 255 * 15), 0, 15).astype(np.uint8)
            off = len(bitmaps)
            for row in q:
                r = list(row) + ([0] if len(row) % 2 else [])
                bitmaps += bytes(r[i] | r[i + 1] << 4 for i in range(0, len(r), 2))
            glyphs.append((code, gx, gy, q.shape[1] if q.size else 0, q.shape[0] if q.size else 0,
                           int(round(xa * scale * 16)), off))
        kerns = [(a, b, int(round(v * scale * 16))) for (a, b), v in kern.items() if a in codes and b in codes]
        w = W()
        w.u16(len(glyphs))
        w.u16(int(round(line_h * scale * 16)))
        w.u16(int(round(base * scale * 16)))
        w.u16(len(kerns))
        head = 8 + 14 * len(glyphs) + 6 * len(kerns)
        for code, gx, gy, gw, gh, adv, off in glyphs:
            w.u16(code)
            w.s16(gx * 16)
            w.s16(gy)
            w.u8(gw)
            w.u8(gh)
            w.u16(adv)
            w.u32(head + off)
        for a, b, v in kerns:
            w.u16(a)
            w.u16(b)
            w.s16(v)
        w.raw(bitmaps)
        out.append(w)
    sec = W()
    sec.u16(len(out))
    sec.u16(0)
    base = 4 + 4 * len(out)
    body = W()
    offs = []
    for w in out:
        body.align(4)
        offs.append(base + len(body))
        body.raw(w.b)
    for o in offs:
        sec.u32(o)
    sec.raw(body.b)
    return sec


def read_dialog():
    """Language.FromTxt for English.txt: {key: text}, inserts resolved, portraits as {portrait ...}."""
    command = re.compile(r"\{(.*?)\}")
    insert = re.compile(r"\{\+\s*(.*?)\}")
    variable = re.compile(r"^\w+\=.*")
    portrait = re.compile(r"\[(?P<content>[^\[\\]*(?:\\.[^\]\\]*)*)\]")
    dialog = {}
    key = ""
    buf = ""
    prev = ""
    for line in open(os.path.join(CELESTE, "Dialog", "English.txt"), encoding="utf-8-sig"):
        t = line.strip()
        if not t or t[0] == "#":
            continue
        if "[" in t:
            t = portrait.sub(lambda m: "{portrait " + m.group("content") + "}", t)
        t = t.replace("\\#", "#")
        if not t:
            continue
        if variable.match(t):
            if key:
                dialog[key] = buf
            k, _, v = t.partition("=")
            k, v = k.strip(), v.strip()
            if k.lower() in ("language", "icon", "order", "font", "split_regex", "commas", "periods"):
                continue
            key = k.upper()
            buf = v
        else:
            if buf and not buf.endswith("{break}") and not buf.endswith("{n}") and command.sub("", prev):
                buf += "{break}"
            buf += t
        prev = t
    if key:
        dialog[key] = buf
    for k in list(dialog):
        v = dialog[k]
        while True:
            ms = list(insert.finditer(v))
            if not ms:
                break
            for m in ms:
                v = v.replace(m.group(0), dialog.get(m.group(1).upper(), "[XXX]"))
        dialog[k] = v
    return dialog


def fnv(s):
    h = 2166136261
    for c in s.encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h
