#!/usr/bin/env python3
"""Cuts the rendered frames (tools/render.py) into the game's art in assets/.

A plate is a whole 320x240 picture. A sprite is what changes between a frame
and the plate it was rendered against: since the game's dithering depends only
on the pixel position, the unchanged pixels are identical and the changed ones
(the object, its shadow, its light) are exactly what the original shows. Each
sprite is saved as a cropped RGBA PNG; assets/art.json lists where it goes.

Usage: RENDER_DIR=dir cut.py
"""
import json
import os
from pathlib import Path

import numpy as np
from scipy import ndimage
from PIL import Image

HERE = Path(__file__).resolve().parent
R = Path(os.environ.get("RENDER_DIR", HERE / ".renders"))
OUT = HERE.parent / "assets"


def save(a, path):
    """Saves exactly, as a paletted PNG (the renders have few colors)."""
    im = Image.fromarray(a)
    rgb = a[..., :3].reshape(-1, 3)
    cols = np.unique(rgb, axis=0)
    if len(cols) > 255:
        im.save(path, optimize=True)
        return
    pal = [tuple(int(v) for v in c) for c in cols]
    index = {c: i + 1 for i, c in enumerate(pal)}
    idx = np.array([index[tuple(int(v) for v in c)] for c in rgb], np.uint8).reshape(a.shape[:2])
    if a.shape[2] == 4:
        idx[a[..., 3] == 0] = 0
    p = Image.fromarray(idx, "P")
    flat = [0, 0, 0] + [v for c in pal for v in c]
    p.putpalette(flat + [0] * (768 - len(flat)))
    if a.shape[2] == 4:
        p.info["transparency"] = 0
        p.save(path, optimize=True, transparency=0)
    else:
        p.save(path, optimize=True)


SS = 4        # renders are 4x the screen size (tools/render.py)
LEVELS = int(os.environ.get("BR_LEVELS", 7))  # the original's posterization: 8 levels per channel


SOFT = float(os.environ.get("BR_SOFT", 0.6))  # a slight blur before posterizing: calmer dithering, much smaller art


STICKY = float(os.environ.get("BR_STICKY", 0.58))  # see settle()


def levels(a):
    """Averages SSxSS blocks (the look of the game on a small screen, without
    the aliasing of rendering straight at 320x240), in steps of the original's
    posterization: 8 levels per channel."""
    h, w = a.shape[0] // SS, a.shape[1] // SS
    m = a[:h * SS, :w * SS].astype(np.float32).reshape(h, SS, w, SS, 3).mean(axis=(1, 3)) / 255
    if SOFT:
        m = np.stack([ndimage.gaussian_filter(m[..., c], SOFT, mode="nearest") for c in range(3)], -1)
    return m * LEVELS


def to_rgb(q):
    return (q / LEVELS * 255 + 0.5).astype(np.uint8)


def shrink(a):
    """Posterized to the nearest level."""
    return to_rgb(np.floor(levels(a) + 0.5))


def settle(s, keep=None, kept=None, sticky=None):
    """Posterizes like shrink(), except that a pixel takes the color of the one
    on its left when that color is close enough (STICKY of a level): the same
    picture to the eye, with far fewer flips of the dithering between two
    levels, so it compresses much better. `keep` pixels are forced to `kept`
    (a sprite's surroundings: the plate it is drawn on)."""
    sticky = STICKY if sticky is None else sticky
    out = np.floor(s + 0.5)
    if keep is not None:
        out[keep] = kept[keep]
    for x in range(1, s.shape[1]):
        n = out[:, x - 1]
        ok = np.abs(n - s[:, x]).max(axis=1) <= sticky
        if keep is not None:
            ok &= ~keep[:, x]
        out[ok, x] = n[ok]
    return out


def mask_of(name):
    """The mask pass of a render (tools/render.py): where its posed objects cover
    at least half of a pixel."""
    a = np.array(Image.open(R / f"{name}__mask.png").convert("L")) > 127
    h, w = a.shape[0] // SS, a.shape[1] // SS
    return a[:h * SS, :w * SS].reshape(h, SS, w, SS).mean(axis=(1, 3)) >= 0.5


_cache = {}
_ccache = {}


def load_levels(name):
    """The render in (unrounded) levels, or None for the pictures made up of others."""
    if name.endswith("_none"):
        return None
    if name not in _ccache:
        _ccache.clear()  # only the last few are needed
        _ccache[name] = levels(np.array(Image.open(R / f"{name}.png").convert("RGB")))
    return _ccache[name]


# the scenes seen for a moment, away from the table: settled a little more
STICKIER = {"scenes/": 0.8, "rooms/": 0.8, "shells/": 0.78, "heaven/": 0.8, "dealer/Z_": 0.72, "dealer/E_": 0.78,
            "poses/P_god": 0.85, "poses/": 0.74}


def sticky_of(name):
    for k, v in STICKIER.items():
        if name.startswith(k):
            return max(v, STICKY)
    return STICKY


def load_settled(name):
    s = load_levels(name)
    return to_rgb(settle(s, sticky=sticky_of(name))) if s is not None else load(name)


def load(name):
    if name in _cache:
        return _cache[name]
    if name in ("health/H_round_none", "health/H_endless_none"):
        # the round display with no round lit: each round lights one circle,
        # so the majority of the three pictures is the unlit display
        stem = name[:-5]
        a, b, c = (load(f"{stem}{k}").astype(np.int32) for k in (1, 2, 3))
        r = np.where((a == b).all(axis=2, keepdims=True), a, c).astype(np.uint8)
    else:
        r = shrink(np.array(Image.open(R / f"{name}.png").convert("RGB")))
    _cache[name] = r
    return r


# plates: name -> render
PLATES = {
    "table": "table/T_plate1",
    "dealer": "dealer/D_plate1",
    "health": "health/H_plate",
    "shells": "shells/S_plate",
    "waiver": "scenes/W_waiver",
    "don": "scenes/Y_don",
    "car": "scenes/E_car",
    "zaim": "dealer/Z_aimp",
    "bath": "rooms/B_bath",
    "revive": "rooms/B_revive",
    "hall": "rooms/L_hall",
    # rendered at 160x120, shown doubled
    "heaven": "heaven/X_heaven",
}

ITEMS = 9
RAW = {"d_eyes"}  # tiny sprites: keep every changed pixel


def sprites():
    """(sprite name, render, plate render); a render's own "__base" plate wins when present."""
    s = [("t_grid", "table/T_plate2", "table/T_plate1"), ("d_grid", "dealer/D_plate2", "dealer/D_plate1")]
    for n in ["gun", "gunsaw", "box", "pcuff", "hands", "cuffed", "head0", "head1", "head2", "head3"]:
        s.append((f"t_{n}", f"table/T_{n}", None))
    for i in range(ITEMS):
        for side in "pd":
            for g in range(8):
                s.append((f"it{i}{side}{g}", f"table/T_it_{i}_{side}{g}", None))
    for i in range(ITEMS):
        for g in range(8):
            s.append((f"di{i}{g}", f"dealer/D_it_{i}_{g}", None))
    for n in ["gun", "gunsaw", "head", "hands", "eyes", "cuffed", "getcuff", "adr0", "adr1", "beer0", "beer1", "phone0", "phone1",
              "cig0", "cig1", "med0", "med1", "cuffs0", "cuffs1", "saw0", "saw1", "saw2", "inv0", "inv1", "mag0", "mag1", "medfall",
              "gun_load", "gun_load1", "gun_aimp", "gun_aims", "gun_rack", "gun_racks", "gun_aimp_saw", "gun_aims_saw",
              "gun_aimp0", "gun_aims0", "gun_lift",
              "fly0", "fly1", "eject_l", "eject_b"]:
        s.append((f"d_{n}", f"dealer/D_{n}", None))
    for n in ["p_aimd", "p_aimd_saw"]:
        s.append((n, f"dealer/{n[0].upper()}{n[1:]}", None))
    for n in ["brief0", "brief1", "brief2"]:
        s.append((f"e_{n}", f"dealer/E_{n}", None))
    for n in ["hold", "lbl_dealer", "aims", "aims_saw", "rack", "god", "mag", "mag_l", "mag_b", "mag0", "saw0", "saw1",
              "beer0", "beer1", "cig0", "cig1", "phone0", "phone1", "med0", "med1", "adr0", "adr1", "inv0", "inv1", "eject_l", "eject_b"]:
        s.append((f"p_{n}", f"poses/P_{n}", None))
    s.append(("h_frame", "health/H_frame", "health/H_plate"))
    for k in range(12):
        s.append((f"h_sym{k}", f"health/H_sym{k}", "health/H_frame"))
    for k in range(4):
        s.append((f"h_skull{k}", f"health/H_skull{k}", "health/H_frame"))
    s.append(("h_round0", "health/H_round_none", "health/H_plate"))
    for k in range(1, 4):
        s.append((f"h_round{k}", f"health/H_round{k}", "health/H_round_none"))
    s.append(("h_endless0", "health/H_endless_none", "health/H_round_none"))
    for k in range(1, 4):
        s.append((f"h_endless{k}", f"health/H_endless{k}", "health/H_endless_none"))
    s += [("h_err_p", "health/H_err_p", "health/H_plate"), ("h_err_d", "health/H_err_d", "health/H_plate")]
    s.append(("c_frame", "health/C_frame", None))
    for k in range(12):
        s.append((f"c_sym{k}", f"health/C_sym{k}", None))
    for i in range(8):
        for kind in "lb":
            s.append((f"s_{kind}{i}", f"shells/S_{kind}{i}", "shells/S_plate"))
    s.append(("b_pills", "rooms/B_pills", "rooms/B_bath"))
    return s


def clean(d, a, b):
    """Keeps the object, its shadow and its light, and drops the specks where
    only the dithering flipped because the lighting changed a little far away:
    pixels that changed a lot, the small changes right around them, and no
    tiny islands."""
    if not d.any():
        return d
    step = np.abs(a.astype(np.int32) // 36 - b.astype(np.int32) // 36).sum(axis=2)
    strong = step >= 2
    lab, n = ndimage.label(strong, structure=np.ones((3, 3)))
    if n:
        sizes = ndimage.sum(strong, lab, range(1, n + 1))
        keep = np.zeros(n + 1, bool)
        keep[1:] = sizes >= min(6, sizes.max())
        strong = keep[lab]
    near = ndimage.binary_dilation(strong, iterations=3)
    return d & (strong | near)


def main():
    OUT.mkdir(exist_ok=True)
    (OUT / "sprites").mkdir(exist_ok=True)
    art = {"plates": {}, "sprites": {}}
    for name, src in PLATES.items():
        save(load_settled(src), OUT / f"{name}.png")
        art["plates"][name] = f"{name}.png"
    for name, src, base in sprites():
        if not src.endswith("_none") and not (R / f"{src}.png").exists():
            print("missing", src)
            continue
        a = load(src)
        if (R / f"{src}__base.png").exists() and not (base or "").endswith("_none"):
            base = f"{src}__base"
        b = load(base)
        if (R / f"{src}__mask.png").exists():
            d = mask_of(src)  # the object alone
        else:
            d = np.any(a != b, axis=2)
            if name not in RAW:
                d = clean(d, a, b)
        ys, xs = np.nonzero(d)
        if not len(ys):
            print("empty sprite", name)
            art["sprites"][name] = None
            continue
        y0, y1, x0, x1 = int(ys.min()), int(ys.max()) + 1, int(xs.min()), int(xs.max()) + 1
        sa, sb = load_levels(src), load_levels(base)
        if sa is not None and sb is not None:
            kept = settle(sb, sticky=sticky_of(src))
            a = to_rgb(settle(sa, ~d, kept, sticky_of(src)))
        rgba = np.zeros((y1 - y0, x1 - x0, 4), np.uint8)
        rgba[..., :3] = a[y0:y1, x0:x1]
        rgba[..., 3] = d[y0:y1, x0:x1] * 255
        save(rgba, OUT / "sprites" / f"{name}.png")
        art["sprites"][name] = [x0, y0]
    (OUT / "art.json").write_text(json.dumps(art, indent=0))
    print(len(art["sprites"]), "sprites")


if __name__ == "__main__":
    main()
