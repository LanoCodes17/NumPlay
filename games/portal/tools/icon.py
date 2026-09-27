#!/usr/bin/env python3
"""Draws src/icon.png (55x56): the blue ring of the title screen's
"PORTAL" (an eight blade iris), redrawn smooth at icon size on the game's
dark gray, with rounded corners like the other NumPlay icons (about 3 KB
in the app)."""
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
S = 8  # supersampling
BLUE, BG = (24, 130, 231), (74, 73, 82)


def main():
    W, H = 55 * S, 56 * S
    cx, cy = W / 2, H / 2
    ro, ri, gap = 22.5 * S, 10.5 * S, 1.7 * S
    ring = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(ring)
    d.ellipse((cx - ro, cy - ro, cx + ro, cy + ro), fill=255)
    d.ellipse((cx - ri, cy - ri, cx + ri, cy + ri), fill=0)
    for k in range(8):  # the cuts between blades, tangent to the inner circle
        a = math.radians(k * 45 + 10)
        px, py = cx + ri * math.cos(a), cy + ri * math.sin(a)
        tx, ty = -math.sin(a), math.cos(a)
        nx, ny = math.cos(a), math.sin(a)
        L = ro * 2
        pts = [(px + nx * gap / 2 - tx * gap, py + ny * gap / 2 - ty * gap),
               (px + nx * gap / 2 + tx * L, py + ny * gap / 2 + ty * L),
               (px - nx * gap / 2 + tx * L, py - ny * gap / 2 + ty * L),
               (px - nx * gap / 2 - tx * gap, py - ny * gap / 2 - ty * gap)]
        d.polygon(pts, fill=0)
    bgmask = Image.new('L', (W, H), 0)
    ImageDraw.Draw(bgmask).rounded_rectangle((0, 0, W - 1, H - 1), radius=7 * S, fill=255)
    im = Image.new('RGBA', (W, H), BG + (255,))
    im.paste(Image.new('RGBA', (W, H), BLUE + (255,)), (0, 0), ring)
    im.putalpha(bgmask)
    im = im.resize((55, 56), Image.LANCZOS)
    # eight colors keep the edges smooth and the icon small in the app
    im = im.quantize(8, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    im.save(os.path.join(HERE, '..', 'src', 'icon.png'), optimize=True)


if __name__ == '__main__':
    main()
