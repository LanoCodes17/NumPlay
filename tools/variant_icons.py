#!/usr/bin/env python3
"""Draws the icons of NumPlay's two discreet versions (55x56, like the
calculator's own apps):

- invisible: plain white, the colour of the home screen, so it blends in;
- matrices: a "Matrices" app in the style of the calculator's own icons
  (light grey tile, charcoal brackets, orange and slate cells).

Usage: variant_icons.py launcher/assets [docs/media]  (the second folder gets
bigger previews for the README, the invisible one with a faint outline)"""
import os
import sys

from PIL import Image, ImageDraw

W, H, S = 55, 56, 8
INK, ORANGE, SLATE = (65, 65, 71), (255, 183, 52), (101, 105, 117)
BANDS = ((0, 245), (17, 236), (35, 217), (50, 210))  # grey bands of the calculator's light icons, from the top


def invisible():
    return Image.new("RGB", (W, H), (255, 255, 255))


def matrices(size=(W, H)):
    im = Image.new("RGB", (W * S, H * S), (255, 255, 255))
    tile = Image.new("RGB", im.size)
    d = ImageDraw.Draw(tile)
    for y, grey in BANDS:
        d.rectangle([0, y * S, W * S, H * S], fill=(grey,) * 3)
    # brackets
    t = 3 * S
    for x0, x1, inner in ((9, 17, 1), (38, 46, -1)):
        a, b = x0 * S, x1 * S
        d.rectangle([a if inner > 0 else b - t, 9 * S, a + t - 1 if inner > 0 else b - 1, 47 * S - 1], fill=INK)
        d.rectangle([a, 9 * S, b - 1, 9 * S + t - 1], fill=INK)
        d.rectangle([a, 47 * S - t, b - 1, 47 * S - 1], fill=INK)
    # 2x2 cells, the diagonal in orange
    for i in range(2):
        for j in range(2):
            x, y = (16.5 + j * 12) * S, (16 + i * 12) * S
            d.rounded_rectangle([x, y, x + 10 * S, y + 10 * S], radius=2 * S, fill=ORANGE if i == j else SLATE)
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W * S - 1, H * S - 1], radius=7 * S, fill=255)
    im.paste(tile, (0, 0), mask)
    return im.resize(size, Image.LANCZOS)


def invisible_preview(size):
    w, h = size[0] * 4, size[1] * 4
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=w // 8, fill=(255, 255, 255, 255))
    dash, gap, t, c = w // 14, w // 20, max(2, w // 60), (190, 190, 196, 255)
    inset = t * 2
    for x in range(inset + w // 8, w - inset - w // 8, dash + gap):
        for y in (inset, h - inset - t):
            d.rectangle([x, y, min(x + dash, w - inset - w // 8), y + t], fill=c)
    for y in range(inset + h // 8, h - inset - h // 8, dash + gap):
        for x in (inset, w - inset - t):
            d.rectangle([x, y, x + t, min(y + dash, h - inset - h // 8)], fill=c)
    return im.resize(size, Image.LANCZOS)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "launcher/assets"
    invisible().save(os.path.join(out, "icon-invisible.png"))
    matrices().save(os.path.join(out, "icon-matrices.png"))
    if len(sys.argv) > 2:
        invisible_preview((128, 130)).save(os.path.join(sys.argv[2], "icon-invisible.png"))
        matrices((128, 130)).save(os.path.join(sys.argv[2], "icon-matrices.png"))
