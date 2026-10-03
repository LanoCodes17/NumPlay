"""Text glyphs from the game's own fonts (Font assets hold their TrueType data): rendered offline at the size the game
shows them, as sprites."""
import functools, io
import numpy as np
from PIL import Image, ImageFont, ImageDraw
import unity, art


@functools.lru_cache(maxsize=None)
def font_data(name):
    """A Font asset's TrueType data, by its name (resources and shared assets)."""
    for path in ("sharedassets0.assets", "resources.assets"):
        f = unity.asset_file(path)
        for o in f.objects.values():
            if o.type.name == "Font":
                t = o.read_typetree()
                if t["m_Name"] == name and t.get("m_FontData"):
                    return bytes(t["m_FontData"])
    raise KeyError(name)


def glyphs(name, chars, em_px, scale_units):
    """Sprites (as actors.build's records) of chars in the font at em_px pixels an em; scale_units: units a pixel.
    -> ([sprite], [advance in units])"""
    ss = 4
    ft = ImageFont.truetype(io.BytesIO(font_data(name)), int(round(em_px * ss)))
    asc, desc = ft.getmetrics()
    out, adv = [], []
    for ch in chars:
        l, t, r, b = ft.getbbox(ch)
        w, h = max(1, r - l + 2 * ss), max(1, b - t + 2 * ss)
        im = Image.new("L", (w, h), 0)
        ImageDraw.Draw(im).text((ss - l, ss - t), ch, font=ft, fill=255)
        W, H = max(1, round(w / ss)), max(1, round(h / ss))
        a = np.asarray(im.resize((W, H), Image.BOX))
        rgba = np.zeros((H, W, 4), np.uint8)
        rgba[..., :3] = 255
        rgba[..., 3] = a
        # (the glyph's top left from the pen, on the baseline: units)
        lx, ty = (l - ss) / ss * scale_units, (asc - (t - ss)) / ss * scale_units
        out.append({"key": ("font", name, ch, em_px), "job": art.ImageJob(rgba, "glyph/%s" % ch), "lx": lx, "ty": ty,
                    "tu": w / ss * scale_units / W, "tv": h / ss * scale_units / H})
        adv.append(ft.getlength(ch) / ss * scale_units)
    return out, adv


def bitmaps(name, chars, em_px, phase=0.0, ss=4, gamma=0.8):
    """Glyphs at em_px pixels an em, unhinted (as the game's text is), from 4x, the pen phase pixels right of a whole
    pixel: [(alpha rows, left, top from the pen's pixel on the baseline, advance in pixels)]. (Thin strokes a little
    stronger: gamma.)"""
    ft = ImageFont.truetype(io.BytesIO(font_data(name)), em_px * ss)
    pad = int(em_px) + 2
    out = []
    for ch in chars:
        adv = ft.getlength(ch) / ss
        W, H = (int(em_px * 3) + 2 * pad) * ss, (int(em_px * 3) + 2 * pad) * ss
        im = Image.new("L", (W, H), 0)
        # the pen at (pad, base) in pixels: the baseline at a whole pixel
        base = int(em_px * 2) + pad
        ImageDraw.Draw(im).text((pad * ss + round(phase * ss), base * ss), ch, font=ft, fill=255, anchor="ls")
        a = np.asarray(im.resize((W // ss, H // ss), Image.BOX)).astype(np.float32) / 255
        a = (a ** gamma * 255).round().astype(np.uint8)
        ys, xs = np.nonzero(a > 8)
        if not len(ys):
            out.append((np.zeros((0, 0), np.uint8), 0, 0, adv))
            continue
        a = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        out.append((a, int(xs.min()) - pad, base - int(ys.min()), adv))
    return out
