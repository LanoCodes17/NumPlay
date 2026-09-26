#!/usr/bin/env python3
"""Draws the launcher's icons as 4-bit masks into launcher/src/icons.c.

Shapes are drawn 8x supersampled with PIL and averaged down, so edges come
out anti-aliased. Usage: icongen.py out.c [preview.png]
"""
import math
import sys

import numpy as np
from PIL import Image, ImageDraw

S = 8  # supersampling


def canvas(w, h):
    im = Image.new("L", (w * S, h * S), 0)
    return im, ImageDraw.Draw(im)


def finish(im, w, h):
    a = np.asarray(im).astype(np.float32) / 255
    a = a.reshape(h, S, w, S).mean(axis=(1, 3))
    return np.clip(np.round(a * 15), 0, 15).astype(np.uint8)


def gear(size, teeth=8):
    im, d = canvas(size, size)
    c = size * S / 2
    r_out, r_in, r_hole = c * 0.96, c * 0.72, c * 0.32
    pts = []
    for k in range(teeth * 4):
        ang = 2 * math.pi * (k / (teeth * 4)) - math.pi / 2
        # tooth profile: two points on the outer radius, two on the inner one
        r = r_out if (k % 4) in (1, 2) else r_in
        pts.append((c + r * math.cos(ang), c + r * math.sin(ang)))
    d.polygon(pts, fill=255)
    d.ellipse([c - r_in * 0.98, c - r_in * 0.98, c + r_in * 0.98, c + r_in * 0.98], fill=255)
    d.ellipse([c - r_hole, c - r_hole, c + r_hole, c + r_hole], fill=0)
    return finish(im, size, size)


def warning(w, h):
    im, d = canvas(w, h)
    W, H = w * S, h * S
    m = S * 1.5
    # rounded triangle: draw a triangle with a thick rounded outline
    tri = [(W / 2, m), (W - m, H - m), (m, H - m)]
    d.polygon(tri, fill=255)
    d.line(tri + [tri[0]], fill=255, width=int(S * 3), joint="curve")
    for x, y in tri:
        rr = S * 1.5
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=255)
    # exclamation mark cut out
    bw = W * 0.09
    d.rounded_rectangle([W / 2 - bw, H * 0.33, W / 2 + bw, H * 0.68], radius=bw, fill=0)
    d.ellipse([W / 2 - bw * 1.1, H * 0.74, W / 2 + bw * 1.1, H * 0.74 + 2.2 * bw], fill=0)
    return finish(im, w, h)


def trash(w, h):
    im, d = canvas(w, h)
    W, H = w * S, h * S
    lw = S * 1.6
    d.rounded_rectangle([W * 0.08, H * 0.16, W * 0.92, H * 0.16 + lw * 1.1], radius=lw / 2, fill=255)
    d.rounded_rectangle([W * 0.36, H * 0.02, W * 0.64, H * 0.2], radius=lw, outline=255, width=int(lw))
    d.rounded_rectangle([W * 0.16, H * 0.3, W * 0.84, H * 0.98], radius=S * 2, outline=255, width=int(lw))
    for fx in (0.38, 0.62):
        d.line([(W * fx, H * 0.44), (W * fx, H * 0.84)], fill=255, width=int(lw * 0.9))
    return finish(im, w, h)


def play(w, h):
    im, d = canvas(w, h)
    W, H = w * S, h * S
    tri = [(W * 0.12, H * 0.06), (W * 0.96, H * 0.5), (W * 0.12, H * 0.94)]
    d.polygon(tri, fill=255)
    d.line(tri + [tri[0]], fill=255, width=int(S * 1.2), joint="curve")
    return finish(im, w, h)


def check(w, h):
    im, d = canvas(w, h)
    W, H = w * S, h * S
    d.line([(W * 0.12, H * 0.52), (W * 0.4, H * 0.8), (W * 0.9, H * 0.2)], fill=255, width=int(S * 2.6),
           joint="curve")
    for x, y in ((W * 0.12, H * 0.52), (W * 0.9, H * 0.2), (W * 0.4, H * 0.8)):
        rr = S * 1.3
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=255)
    return finish(im, w, h)


def chevron(w, h, left):
    im, d = canvas(w, h)
    W, H = w * S, h * S
    pts = [(W * 0.75, H * 0.1), (W * 0.25, H * 0.5), (W * 0.75, H * 0.9)]
    if not left:
        pts = [(W - x, y) for x, y in pts]
    d.line(pts, fill=255, width=int(S * 2), joint="curve")
    for x, y in pts:
        rr = S
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=255)
    return finish(im, w, h)


def back(w, h):
    im, d = canvas(w, h)
    W, H = w * S, h * S
    lw = S * 1.8
    d.line([(W * 0.9, H * 0.5), (W * 0.15, H * 0.5)], fill=255, width=int(lw))
    d.line([(W * 0.45, H * 0.18), (W * 0.13, H * 0.5), (W * 0.45, H * 0.82)], fill=255, width=int(lw), joint="curve")
    for x, y in ((W * 0.45, H * 0.18), (W * 0.45, H * 0.82), (W * 0.9, H * 0.5)):
        d.ellipse([x - lw / 2, y - lw / 2, x + lw / 2, y + lw / 2], fill=255)
    return finish(im, w, h)


def logo(w, h):
    """A little game pad."""
    im, d = canvas(w, h)
    W, H = w * S, h * S
    d.rounded_rectangle([0, H * 0.12, W, H * 0.92], radius=H * 0.4, fill=255)
    cx, cy, a, b = W * 0.28, H * 0.52, W * 0.13, H * 0.1
    d.rectangle([cx - a, cy - b * 0.55, cx + a, cy + b * 0.55], fill=0)
    d.rectangle([cx - b * 0.55 * W / H * 0.9, cy - a * H / W * 2.3, cx + b * 0.55 * W / H * 0.9, cy + a * H / W * 2.3],
                fill=0)
    for fx, fy in ((0.68, 0.42), (0.8, 0.6)):
        rr = H * 0.1
        d.ellipse([W * fx - rr, H * fy - rr, W * fx + rr, H * fy + rr], fill=0)
    return finish(im, w, h)


def heart(w, h):
    im, d = canvas(w, h)
    W, H = w * S, h * S
    r = W * 0.27
    for cx in (W * 0.28, W * 0.72):
        d.ellipse([cx - r, H * 0.08, cx + r, H * 0.08 + 2 * r], fill=255)
    d.polygon([(W * 0.04, H * 0.42), (W * 0.96, H * 0.42), (W * 0.5, H * 0.96)], fill=255)
    return finish(im, w, h)


ICONS = [
    ("gear", lambda: gear(42)),
    ("warning", lambda: warning(46, 40)),
    ("trash", lambda: trash(14, 16)),
    ("play", lambda: play(11, 12)),
    ("check", lambda: check(30, 30)),
    ("back", lambda: back(14, 12)),
    ("left", lambda: chevron(8, 14, True)),
    ("right", lambda: chevron(8, 14, False)),
    ("logo", lambda: logo(22, 15)),
    ("heart", lambda: heart(10, 9)),
]


def main(out, preview=None):
    lines = ["/* Generated by tools/icongen.py. */", '#include "gfx.h"', ""]
    sheet = []
    total = 0
    for name, fn in ICONS:
        q = fn()
        h, w = q.shape
        flat = q.flatten().tolist()
        if len(flat) % 2:
            flat.append(0)
        data = bytes(flat[i] | flat[i + 1] << 4 for i in range(0, len(flat), 2))
        total += len(data)
        lines.append(f"static const uint8_t {name}_data[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append(f"const np_icon_t np_icon_{name} = {{{w}, {h}, {name}_data}};")
        lines.append("")
        sheet.append(q)
    open(out, "w").write("\n".join(lines))
    print(f"icons: {total} bytes")
    if preview:
        W = sum(q.shape[1] + 4 for q in sheet)
        H = max(q.shape[0] for q in sheet)
        im = Image.new("L", (W, H), 0)
        x = 0
        for q in sheet:
            im.paste(Image.fromarray((q * 17).astype(np.uint8)), (x, 0))
            x += q.shape[1] + 4
        im.resize((W * 4, H * 4), Image.NEAREST).save(preview)


if __name__ == "__main__":
    main(*sys.argv[1:])
