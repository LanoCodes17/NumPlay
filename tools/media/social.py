#!/usr/bin/env python3
"""Renders docs/media/social.png, the 1280x640 picture shown when the
repository is shared (upload it in the repository's settings)."""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import fontgen  # noqa: E402
import make_icon  # noqa: E402

W, H = 1280, 640
bg = Image.new("RGB", (W, H))
px = bg.load()
a, b = (40, 22, 110), (12, 10, 40)
for y in range(H):
    for x in range(W):
        t = (x * 0.4 + y * 0.6) / (W * 0.4 + H * 0.6)
        px[x, y] = tuple(int(p + (q - p) * t) for p, q in zip(a, b))
img = bg.convert("RGBA")
games = {g["id"]: g for g in json.load(open(os.path.join(ROOT, "games", "games.json")))}
shots = [("numdash", 1), ("crossyroad", 1), ("numdrive", 0), ("tetris", 1)]
cw, ch = 290, 218
for k, (gid, i) in enumerate(shots):
    x, y = 650 + (k % 2) * (cw + 24), 90 + (k // 2) * (ch + 24)
    im = Image.open(os.path.join(ROOT, "games", gid, games[gid]["shots"][i])).convert("RGB").resize((cw, ch), Image.LANCZOS)
    sh = Image.new("RGBA", (cw + 60, ch + 60), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([30, 36, cw + 30, ch + 36], radius=18, fill=(0, 0, 0, 160))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(12)), (x - 30, y - 30))
    card = Image.new("RGBA", (cw + 10, ch + 10), (0, 0, 0, 0))
    ImageDraw.Draw(card).rounded_rectangle([0, 0, cw + 9, ch + 9], radius=18, fill=(255, 255, 255, 255))
    m = Image.new("L", (cw, ch), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, cw - 1, ch - 1], radius=13, fill=255)
    card.paste(im, (5, 5), m)
    img.alpha_composite(card, (x - 5, y - 5))
img.alpha_composite(make_icon.icon(150, 153).convert("RGBA"), (70, 100))
font = fontgen.font_file()
f1 = ImageFont.truetype(font, 100)
f1.set_variation_by_axes([900])
f2 = ImageFont.truetype(font, 36)
f2.set_variation_by_axes([700])
d = ImageDraw.Draw(img)
d.text((64, 282), "NumPlay", font=f1, fill=(255, 255, 255))
d.multiline_text((72, 410), "Every game for your\nNumWorks calculator,\nin one app.", font=f2, fill=(220, 215, 255),
                 spacing=8)
img.convert("RGB").save(os.path.join(ROOT, "docs", "media", "social.png"), optimize=True)
