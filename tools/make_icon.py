#!/usr/bin/env python3
"""Draws NumPlay's home screen icon (55x56, like the calculator's own apps):
a white game pad on a warm-to-cool gradient. Usage: make_icon.py out.png [size]"""
import sys

from PIL import Image, ImageDraw

S = 8


def icon(w=55, h=56):
    """Drawn for 55x56 and scaled for other sizes."""
    S = 8 * w / 55
    W, H = int(w * 8), int(h * 8)
    # diagonal gradient
    top, bottom = (255, 94, 98), (123, 67, 255)
    grad = Image.new("RGB", (W, H))
    px = grad.load()
    for y in range(H):
        for x in range(W):
            t = (x * 0.35 + y * 0.65) / (W * 0.35 + H * 0.65)
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(top, bottom))
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W - 1, H - 1], radius=12 * S, fill=255)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(out)
    # soft shadow, then the pad
    cx, cy = W / 2, H / 2
    pw, ph = 42 * S, 26 * S

    def pad(dx, dy, fill):
        d.rounded_rectangle([cx - pw / 2 + dx, cy - ph / 2 + dy, cx + pw / 2 + dx, cy + ph / 2 + dy], radius=ph / 2,
                            fill=fill)

    pad(0, 2 * S, (60, 20, 90, 90))
    pad(0, 0, (255, 255, 255, 255))
    ink = (120, 70, 220, 255)
    # d-pad
    dx, dy, a, b = cx - 10.5 * S, cy, 5.5 * S, 2 * S
    d.rounded_rectangle([dx - a, dy - b, dx + a, dy + b], radius=S, fill=ink)
    d.rounded_rectangle([dx - b, dy - a, dx + b, dy + a], radius=S, fill=ink)
    # buttons
    for bx, by, col in ((cx + 8 * S, cy + 2.5 * S, (255, 94, 98, 255)), (cx + 13 * S, cy - 2.5 * S, ink)):
        r = 3 * S
        d.ellipse([bx - r, by - r, bx + r, by + r], fill=col)
    return out.resize((w, h), Image.LANCZOS)


if __name__ == "__main__":
    im = icon()
    # the calculator ignores transparency: round the corners on the home screen's white
    flat = Image.new("RGB", im.size, (255, 255, 255))
    flat.paste(im, (0, 0), im)
    flat.save(sys.argv[1])
    if len(sys.argv) > 2:
        im.resize((int(sys.argv[2]), int(int(sys.argv[2]) * 56 / 55)), Image.LANCZOS).save(sys.argv[1].replace(".png", "@big.png"))
