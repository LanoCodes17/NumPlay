#!/usr/bin/env python3
"""Builds src/assets.c and src/assets.h from the PNGs in assets/.

Art: Balatro's 1x textures scaled down to the calculator's card size (35x47)
with a box filter, reduced to a few dozen colours per sprite and to one
255-colour palette per group, then coded with tools/rc.py.
Fonts: m6x11plus (Daniel Linssen) at its native size, a medium size made from
it, and a small 3-wide version drawn after it (tools/font_small.py).

Usage: mkassets.py [--check]"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
import faces  # noqa: E402
import rc  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
A = os.path.join(ROOT, "assets")
CW, CH = 35, 47
MAXCOL = 24  # colours per sprite


def sheet(name):
    return Image.open(os.path.join(A, name)).convert("RGBA")


def cell(im, x, y, w=71, h=95):
    return im.crop((x * w, y * h, x * w + w, y * h + h))


def card(im, x, y):
    return cell(im, x, y).resize((CW, CH), Image.BOX)


def crisp_alpha(img, thr=128):
    a = np.array(img)
    a[:, :, 3] = np.where(a[:, :, 3] >= thr, 255, 0)
    return Image.fromarray(a, "RGBA")


# ------------------------------------------------------------------ sprites
# order in P_CENTERS (1..150) -> atlas position (x, y)
JOKER_POS = [
    (0, 0), (6, 1), (7, 1), (8, 1), (9, 1), (2, 0), (3, 0), (4, 0), (5, 0), (6, 0), (0, 14), (1, 14), (2, 14), (3, 14),
    (4, 14), (7, 0), (2, 5), (6, 6), (4, 1), (5, 1), (5, 5), (1, 2), (2, 2), (3, 2), (4, 2), (0, 5), (6, 2), (4, 7),
    (8, 2), (1, 0), (1, 5), (7, 2), (2, 3), (3, 3), (4, 3), (5, 2), (6, 3), (7, 6), (8, 3), (9, 3), (0, 4), (1, 4),
    (2, 4), (1, 6), (3, 5), (0, 10), (1, 10), (2, 10), (3, 10), (4, 10), (5, 10), (6, 10), (7, 10), (8, 10), (9, 10),
    (0, 11), (1, 11), (2, 11), (3, 11), (4, 11), (5, 11), (6, 11), (7, 11), (8, 11), (9, 11), (0, 12), (1, 12), (2, 12),
    (3, 12), (4, 12), (5, 12), (6, 12), (7, 12), (8, 12), (9, 12), (0, 13), (1, 13), (2, 13), (3, 13), (4, 13), (5, 13),
    (6, 13), (7, 13), (8, 13), (9, 13), (7, 5), (0, 1), (1, 1), (9, 0), (9, 2), (5, 14), (6, 14), (7, 14), (8, 14),
    (9, 14), (0, 15), (1, 15), (4, 15), (7, 15), (2, 15), (8, 15), (3, 15), (9, 15), (6, 15), (5, 15), (5, 3), (3, 4),
    (2, 1), (3, 1), (9, 5), (0, 2), (8, 8), (4, 6), (5, 7), (9, 6), (9, 7), (0, 8), (1, 8), (2, 8), (1, 3), (6, 5),
    (0, 6), (0, 3), (0, 0), (8, 0), (5, 6), (6, 7), (4, 4), (4, 5), (8, 5), (5, 4), (6, 4), (7, 4), (8, 4), (9, 4),
    (8, 6), (1, 7), (7, 7), (8, 7), (2, 6), (0, 7), (7, 3), (2, 7), (3, 7), (9, 8), (3, 8), (4, 8), (5, 8), (6, 8),
    (7, 8),
]
# Shorter cards (the game shows only the top of their cell), and Wee Joker, the Joker art at 70%
CROP = {15: 28, 64: 35, 77: 39}
WEE = 123
SOUL_POS = [(2, 9), (3, 9), (4, 9), (5, 9), (6, 9), (7, 9)]  # Hologram, Canio, Triboulet, Yorick, Chicot, Perkeo

TAROT_POS = [(i, 0) for i in range(10)] + [(i, 1) for i in range(10)] + [(0, 2), (1, 2)]
PLANET_POS = [(0, 3), (1, 3), (2, 3), (3, 3), (4, 3), (5, 3), (6, 3), (7, 3), (8, 3), (9, 2), (8, 2), (3, 2)]
SPECTRAL_POS = [(i, 4) for i in range(10)] + [(i, 5) for i in range(6)] + [(2, 2), (9, 3)]
VOUCHER_POS = [(0, 0), (3, 0), (4, 0), (0, 2), (2, 2), (3, 2), (5, 0), (6, 0), (1, 0), (2, 0), (1, 2), (7, 0), (4, 2),
               (5, 2), (6, 2), (7, 2)]
VOUCHER_POS += [(x, y + 1) for (x, y) in VOUCHER_POS]
# kinds: Arcana, Celestial, Standard, Buffoon, Spectral; sizes normal, jumbo, mega
BOOSTER_POS = [[(0, 0), (0, 2), (2, 2)], [(0, 1), (0, 3), (2, 3)], [(0, 6), (0, 7), (2, 7)], [(0, 8), (2, 8), (3, 8)],
               [(0, 4), (2, 4), (3, 4)]]
# Enhancers.png: back, base, stone, gold, bonus, mult, wild, lucky, glass, steel
ENH_POS = [(0, 0), (1, 0), (5, 0), (6, 0), (1, 1), (2, 1), (3, 1), (4, 1), (5, 1), (6, 1)]
SEAL_POS = [(2, 0), (5, 4), (6, 4), (4, 4)]  # Gold, Red, Blue, Purple
BLIND_ROWS = 31  # BlindChips.png rows (first frame of each), 34x34
TAG_POS = [(0, 0), (1, 0), (2, 0), (3, 0), (0, 1), (1, 1), (2, 1), (3, 1), (0, 2), (1, 2), (2, 2), (3, 2), (4, 2),
           (1, 3), (2, 3), (3, 3), (4, 0), (5, 0), (5, 1), (5, 3), (4, 1), (0, 3), (5, 2), (4, 3)]


def collect():
    """[(group, name, RGBA image)]"""
    out = []
    J = sheet("Jokers.png")
    for i, (x, y) in enumerate(JOKER_POS):
        im = card(J, x, y)
        if i in CROP:
            im = im.crop((0, 0, CW, CROP[i]))
        if i == WEE:
            im = cell(J, x, y).resize((25, 33), Image.BOX)
        out.append(("joker", f"J{i}", im))
    for i, (x, y) in enumerate(SOUL_POS):
        out.append(("joker", f"JSOUL{i}", card(J, x, y)))
    T = sheet("Tarots.png")
    for i, (x, y) in enumerate(TAROT_POS + PLANET_POS + SPECTRAL_POS):
        out.append(("cons", f"C{i}", card(T, x, y)))
    E = sheet("Enhancers.png")
    out.append(("cons", "SOULGEM", card(E, 0, 1)))
    V = sheet("Vouchers.png")
    for i, (x, y) in enumerate(VOUCHER_POS):
        out.append(("vouch", f"V{i}", card(V, x, y)))
    B = sheet("boosters.png")
    for k, row in enumerate(BOOSTER_POS):
        for s, (x, y) in enumerate(row):
            out.append(("vouch", f"P{k}_{s}", card(B, x, y)))
    for i, (x, y) in enumerate(ENH_POS):
        out.append(("deck", f"E{i}", card(E, x, y)))
    for i, (x, y) in enumerate(SEAL_POS):
        out.append(("deck", f"SEAL{i}", card(E, x, y)))
    out.append(("deck", "DEBUFF", card(E, 4, 0)))
    for i, f in enumerate(faces.make_faces(os.path.join(A, "8BitDeck.png"))):
        out.append(("deck", f"F{i}", f))
    BC = sheet("BlindChips.png")
    for r in range(BLIND_ROWS):
        out.append(("misc", f"B{r}", cell(BC, 0, r, 34, 34).resize((26, 26), Image.BOX)))
    TG = sheet("tags.png")
    for i, (x, y) in enumerate(TAG_POS):
        out.append(("misc", f"T{i}", cell(TG, x, y, 34, 34).resize((19, 19), Image.BOX)))
    logo = sheet("balatro.png")
    out.append(("logo", "LOGO", logo.resize((212, 138), Image.BOX)))
    sign = cell(sheet("ShopSignAnimation.png"), 0, 0, 113, 57)
    out.append(("misc", "SHOP", sign.resize((72, 36), Image.BOX)))
    chips = sheet("chips.png")
    out.append(("misc", "STAKE", chips.crop((0, 0, 29, 29)).resize((15, 15), Image.BOX)))
    out.append(("misc", "CHIP", chips.crop((0, 0, 29, 29)).resize((11, 11), Image.BOX)))  # beside numbers
    return out


def quantize_local(img, k):
    a = np.array(img)
    al = a[:, :, 3]
    rgb = Image.fromarray(np.ascontiguousarray(a[:, :, :3]))
    q = rgb.quantize(k, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGB")
    return np.array(q), al


def build_group(items, ncol=255):
    """Returns palette (list of rgb), [index arrays]."""
    locs = [quantize_local(im, MAXCOL) for (_, _, im) in items]
    pix = np.concatenate([rgb[al >= 128] for rgb, al in locs])
    uniq = np.unique(pix, axis=0)
    if len(uniq) <= ncol:
        pal = [tuple(c) for c in uniq]
    else:
        img = Image.fromarray(pix.reshape(1, -1, 3).astype(np.uint8))
        q = img.quantize(ncol, method=Image.MEDIANCUT, dither=Image.NONE)
        p = q.getpalette()[:3 * ncol]
        pal = [tuple(p[i:i + 3]) for i in range(0, len(p), 3)]
    P = np.array(pal, np.int32)
    out = []
    for rgb, al in locs:
        flat = rgb.reshape(-1, 3).astype(np.int32)
        d = ((flat[:, None, :] - P[None, :, :]) ** 2).sum(-1)
        idx = d.argmin(1).reshape(rgb.shape[:2]) + 1
        idx[al < 128] = 0
        # at most 64 distinct values per sprite for the coder
        out.append(idx)
    return pal, out


def template(arrs):
    st = np.stack([a for a in arrs if a.shape == (CH, CW)])
    t = np.zeros(st.shape[1:], np.int32)
    for y in range(st.shape[1]):
        for x in range(st.shape[2]):
            v, c = np.unique(st[:, y, x], return_counts=True)
            t[y, x] = v[c.argmax()]
    return t


def rgb565(c):
    r, g, b = (int(v) for v in c)
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


# ------------------------------------------------------------------ fonts
# Three sizes of m6x11 (Daniel Linssen), for Balatro's type hierarchy:
#   large  m6x11 at its native size (11 pixel capitals): values, titles
#   medium the same shapes 9 pixels high and 5 wide: names, headings
#   small  the shapes redrawn 3 pixels wide (7 pixel capitals): labels, texts
# plus condensed digits of the large size for long numbers. (The condensed
# medium, one column narrower, is made from the medium glyphs as they are
# drawn: see src/gfx.c.)
def glyphs_large():
    f = ImageFont.truetype(os.path.join(A, "m6x11plus.ttf"), 16)
    out = {}
    for c in range(32, 127):
        ch = chr(c)
        im = Image.new("L", (16, 20), 0)
        ImageDraw.Draw(im).text((0, 0), ch, font=f, fill=255)
        a = np.array(im)[3:17] > 127
        cols = np.nonzero(a.any(0))[0]
        gw = (cols.max() + 1) if len(cols) else 0
        adv = int(f.getlength(ch))
        if ch == " ":
            adv = 4
        out[ch] = (max(adv, gw + 1), a[:, :gw])
    return out, 14


def narrow(m):
    """One column less: remove a column whose ink never belongs to a
    two pixel wide stroke, preferring the middle."""
    w = m.shape[1]
    for c in (w // 2, w // 2 - 1, w // 2 + 1, w // 2 - 2, w // 2 + 2):
        if c <= 0 or c >= w - 1:
            continue
        ok = True
        for r in range(m.shape[0]):
            if not m[r, c]:
                continue
            run_l = c
            while run_l > 0 and m[r, run_l - 1]:
                run_l -= 1
            run_r = c
            while run_r < w - 1 and m[r, run_r + 1]:
                run_r += 1
            if run_r - run_l + 1 == 2:
                ok = False
                break
        if ok:
            return np.delete(m, c, axis=1)
    return np.delete(m, w // 2, axis=1)


def glyphs_medium():
    big, _ = glyphs_large()
    out = {}
    for ch, (adv, m) in big.items():
        m = np.delete(m, [1, 5], axis=0)  # the doubled top bar and middle row
        if m.shape[1] == 6 or (m.shape[1] == 7 and ch in "xXvVyY"):
            m = narrow(m)
            adv -= 1
        out[ch] = (adv, m)
    return out, 12


def glyphs_large_digits():
    """Condensed digits (5 wide) and punctuation for long numbers."""
    big, _ = glyphs_large()
    out = {}
    for ch in "0123456789":
        adv, m = big[ch]
        out[ch] = (adv - 1, narrow(m))
    return out


def glyphs_small():
    import font_small
    out = {}
    for ch, rows in font_small.GLYPHS.items():
        w = max(len(r) for r in rows)
        m = np.zeros((font_small.HEIGHT, w), bool)
        for y, r in enumerate(rows):
            for x, v in enumerate(r):
                m[y, x] = v == "#"
        out[ch] = (w + 1 if ch != " " else 3, m)
    return out, font_small.HEIGHT


def pack_font(gl, h, chars=None):
    """Glyph table: per char (offset, width, advance); column-major bits."""
    bits = []
    table = []
    for ch in (chars or [chr(c) for c in range(32, 127)]):
        adv, m = gl[ch]
        w = m.shape[1]
        table.append((len(bits), w, adv))
        for x in range(w):
            for y in range(h):
                bits.append(1 if (y < m.shape[0] and m[y, x]) else 0)
    data = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            data[i >> 3] |= 1 << (i & 7)
    return table, bytes(data)


# ------------------------------------------------------------------ output
GROUPS = ["joker", "cons", "vouch", "deck", "misc", "logo"]
TEMPLATED = {"joker", "cons", "vouch"}


def carr(name, data, typ="uint8_t", per=24):
    s = f"const {typ} {name}[{len(data)}] = {{\n"
    for i in range(0, len(data), per):
        s += "  " + ",".join(str(v) for v in data[i:i + per]) + ",\n"
    return s + "};\n"


def main():
    check = "--check" in sys.argv
    items = collect()
    blob = bytearray()
    sprites = []  # (off, w, h, group)
    pals = []
    tmpls = {}
    names = []
    total_by_group = {}
    for gi, g in enumerate(GROUPS):
        its = [it for it in items if it[0] == g]
        pal, arrs = build_group(its)
        pals.append(pal)
        tm = None
        if g in TEMPLATED:
            tm = template(arrs)
            tmpls[g] = len(sprites)  # the template is coded as an extra sprite, before the group
            data = rc.encode_sprite(tm.tolist(), CW, CH)
            sprites.append((len(blob), CW, CH, gi))
            names.append(f"TMPL_{g.upper()}")
            blob += data
        start = len(blob)
        for (grp, name, im), arr in zip(its, arrs):
            h, w = arr.shape
            if len(set(arr.flatten().tolist())) > 256:
                raise SystemExit(f"{name}: too many colours")
            t = tm.tolist() if tm is not None and w == CW else None
            data = rc.encode_sprite(arr.tolist(), w, h, t)
            if check:
                back = rc.decode_sprite(data, w, h, t)
                assert back == arr.tolist(), name
            sprites.append((len(blob), w, h, gi))
            names.append(name)
            blob += data
        total_by_group[g] = len(blob) - start
    blob += bytes(4)  # the decoder may read a few bytes ahead
    # fonts
    fs, hs = glyphs_small()
    fm, hm = glyphs_medium()
    fl, hl = glyphs_large()
    ts, ds = pack_font(fs, hs, [chr(c) for c in range(32, 131)])
    tm, dm = pack_font(fm, hm)
    tl, dl = pack_font(fl, hl)
    tc, dc = pack_font(glyphs_large_digits(), hl, "0123456789")

    h = ["/* Generated by tools/mkassets.py: do not edit. */", "#ifndef ASSETS_H", "#define ASSETS_H",
         "#include <stdint.h>", ""]
    h.append(f"#define ART_CW {CW}\n#define ART_CH {CH}")
    h.append(f"#define ART_NSPRITES {len(sprites)}\n#define ART_NGROUPS {len(GROUPS)}")
    for g, i in tmpls.items():
        h.append(f"#define ART_TMPL_{g.upper()} {i}")
    for gi, g in enumerate(GROUPS):
        h.append(f"#define ART_G_{g.upper()} {gi}")
    # first index of each named family
    fam = {}
    for i, n in enumerate(names):
        key = n.rstrip("0123456789_")
        fam.setdefault(key, i)
    for k, v in fam.items():
        h.append(f"#define SPR_{k} {v}")
    h.append("typedef struct { uint32_t off : 24, group : 8; uint8_t w, h; } art_sprite_t;")
    h.append("extern const art_sprite_t art_sprites[ART_NSPRITES];")
    h.append("extern const uint8_t art_blob[];")
    h.append("extern const uint16_t art_pal[]; /* the groups' palettes, one after the other */")
    h.append("extern const uint16_t art_pal_off[ART_NGROUPS];")
    h.append("extern const int16_t art_tmpl_of_group[ART_NGROUPS];")
    h.append("typedef struct { uint16_t off; uint8_t w, adv; } glyph_t;")
    h.append(f"#define FONT_SMALL_H {hs}\n#define FONT_MED_H {hm}\n#define FONT_LARGE_H {hl}")
    h.append("extern const glyph_t font_small[99], font_med[95], font_large[95], font_large_digits[10];")
    h.append("extern const uint8_t font_small_bits[], font_med_bits[], font_large_bits[], font_large_digits_bits[];")
    h.append("#endif")
    c = ["/* Generated by tools/mkassets.py from Balatro's art (LocalThunk) and the m6x11 font"
         " (Daniel Linssen): do not edit. */", '#include "assets.h"', ""]
    c.append("const art_sprite_t art_sprites[ART_NSPRITES] = {")
    for off, w, hh, g in sprites:
        c.append(f"  {{{off}, {g}, {w}, {hh}}},")
    c.append("};")
    c.append(carr("art_blob", blob))
    # each palette only as long as it is used (entry 0 is transparent)
    c.append("const uint16_t art_pal[] = {")
    offs, n = [], 0
    for pal in pals:
        vals = [0] + [rgb565(p) for p in pal]
        offs.append(n)
        n += len(vals)
        c.append("  " + ",".join(f"0x{v:04X}" for v in vals) + ",")
    c.append("};")
    c.append("const uint16_t art_pal_off[ART_NGROUPS] = {" + ",".join(map(str, offs)) + "};")
    c.append("const int16_t art_tmpl_of_group[ART_NGROUPS] = {" +
             ",".join(str(tmpls.get(g, -1)) for g in GROUPS) + "};")
    for nm, tab, data in (("font_small", ts, ds), ("font_med", tm, dm), ("font_large", tl, dl),
                          ("font_large_digits", tc, dc)):
        c.append(f"const glyph_t {nm}[{len(tab)}] = {{" + ",".join(f"{{{o},{w},{a}}}" for o, w, a in tab) + "};")
        c.append(carr(nm + "_bits", data))
    open(os.path.join(ROOT, "src", "assets.h"), "w").write("\n".join(h) + "\n")
    open(os.path.join(ROOT, "src", "assets.c"), "w").write("\n".join(c) + "\n")
    print(f"sprites {len(sprites)}, art {len(blob)} bytes:",
          ", ".join(f"{g} {n}" for g, n in total_by_group.items()),
          f"| fonts {len(ds) + len(dm) + len(dl) + len(dc)} bytes, palettes {2 * sum(len(p) + 1 for p in pals)}")


if __name__ == "__main__":
    main()
