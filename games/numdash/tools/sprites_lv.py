"""Artwork for the objects of the later levels (Clubstep, Deadlocked, Dash).

Like sprites_game.py, every sprite is drawn from geometric primitives in the
style of the Geometry Dash object it stands for; nothing is taken from the
game's textures. Returns (name, Art, format) triples; see assets_lv.py.
"""
import math
import random
import numpy as np
from art import Art, S, SS, stroke_mask, inset_polygon, lerp, smooth, hexc
from sprites_game import px, mode_portal_art, portal_arc, PORTAL_STYLE

WHITE = (1.0, 1.0, 1.0)
BLACK = (0.0, 0.0, 0.0)


# ---------------------------------------------------------------- tiles

def edge_mask(u, v, half, edges, bw=2.0):
    inside = (np.abs(u) < half) & (np.abs(v) < half)
    E = np.zeros_like(u, dtype=bool)
    if 'l' in edges: E |= inside & (u < -half + bw)
    if 'r' in edges: E |= inside & (u > half - bw)
    if 't' in edges: E |= inside & (v > half - bw)
    if 'b' in edges: E |= inside & (v < -half + bw)
    if 'c' in edges: E |= inside & (u < -half + bw) & (v > half - bw)
    return E


def tile(body, edges='', variant=0, size=30):
    """30x30 block: a body pattern plus bright outline edges (tinted by the
    object colour; black stays black)."""
    half = size / 2
    a = Art(px(size), px(size))
    u, v = a.grid()
    inside = (np.abs(u) < half) & (np.abs(v) < half)
    lum = np.zeros_like(u)
    al = np.zeros_like(u)
    if body == 'black':
        al[inside] = 1.0
    elif body == 'bevel':
        left, top = (u < v) & (u < -v), (v > u) & (v > -u)
        right = (u > v) & (u > -v)
        al = np.where(left, 0.92, np.where(top, 0.78, np.where(right, 0.55, 0.68)))
        al = np.where(inside, al, 0)
    elif body == 'brick':
        # two courses of bricks: one long brick on top, two below, dark mortar
        al = np.where(inside, 0.62, 0)
        bricks = [(-13, 1.5, 13, 13), (-15, -13, -1.5, -1.5), (1.5, -13, 15, -1.5)]
        for (x0, y0, x1, y1) in bricks:
            m = (u > x0) & (u < x1) & (v > y0) & (v < y1)
            al = np.where(m, 0.9, al)
            inner = (u > x0 + 2.5) & (u < x1 - 2.5) & (v > y0 + 2.5) & (v < y1 - 2.5)
            al = np.where(inner, 0.97, al)
    elif body == 'stone':
        # pale tile with darker panels; the variant picks the pattern
        al = np.where(inside, 0.42, 0)
        lum = np.where(inside, 1.0, 0)
        dark = np.zeros_like(u, dtype=bool)
        if variant == 't':          # a pale band on top, dark below
            dark = v < 4
        elif variant == 'tl':       # L-shaped pale band, dark square in the corner
            dark = (u > -4) & (v < 4)
        elif variant == 'c':        # dark with a pale corner square
            dark = ~((u < -4) & (v > 4))
        elif variant == 'in':
            dark = v < 4
        elif variant == 'ltr':      # an arch
            dark = (np.abs(u) < 5) & (v < 4)
        elif variant == 'lr':       # a dark column between pale pillars
            dark = np.abs(u) < 5
        elif variant == 'cren':     # battlements
            dark = (v < 0) | ((np.abs(u) < 5) & (v < 15))
        elif variant == 'check':
            dark = ((np.floor((u + 15) / 10) + np.floor((v + 15) / 10)) % 2) == 1
        elif variant == 'square':
            dark = np.ones_like(u, dtype=bool)
        lum = np.where(dark & inside, 0.0, lum)
        al = np.where(dark & inside, 0.75, al)
    elif body == 'metal':
        al = np.where(inside, 0.85, 0)
    E = edge_mask(u, v, half, edges)
    lum = np.where(E, 1.0, lum)
    al = np.where(E, 1.0, al)
    a.rgb = np.repeat(lum[..., None], 3, 2).astype(np.float32)
    a.a = al.astype(np.float32)
    return a


def metal_slab(end=False):
    """Metal slab (30 x 21): dark body, bright top rail and rivets."""
    a = Art(px(30), px(22) + 1)
    u, v = a.grid()
    body = (np.abs(u) < 15) & (v > -10.5) & (v < 10.5)
    al = np.where(body, 0.85, 0)
    lum = np.zeros_like(u)
    rail = body & (v > 4) & (v < 9)
    lum = np.where(rail, 1.0, lum); al = np.where(rail, 1.0, al)
    top = body & (v >= 9)
    lum = np.where(top, 0.6, lum); al = np.where(top, 1.0, al)
    for x in ((-9, 9) if not end else (-9,)):
        m = (u - x) ** 2 + (v + 3) ** 2 < 3.2 ** 2
        lum = np.where(m, 0.8, lum); al = np.where(m, 1.0, al)
    if end:
        bar = body & (u > 8) & (v < 4)
        lum = np.where(bar, 0.5, lum); al = np.where(bar, 1.0, al)
    a.rgb = np.repeat(lum[..., None], 3, 2).astype(np.float32)
    a.a = al.astype(np.float32)
    return a


def fake_spike(w=30, h=30, bottom=-15):
    """Decorative dark triangle with a bright rim (1.x fake spikes)."""
    a = Art(px(30), px(30))
    tri = [(-w / 2, bottom), (w / 2, bottom), (0, bottom + h)]
    outer = a.poly_mask(tri)
    inner = a.poly_mask(inset_polygon(tri, 2.2))
    u, v = a.grid()
    t = np.clip((bottom + h - v) / h, 0, 1)
    a.rgb[:] = 0
    a.a = (outer * (1 - inner) * 1.0 + inner * (0.95 - 0.5 * t)).astype(np.float32)
    rim = np.clip(outer - inner, 0, 1)
    a.rgb = np.repeat(np.where(rim > 0.5, 0.35, 0)[..., None], 3, 2).astype(np.float32)
    return a


def ground_spikes(seed=5):
    """ID 61: a strip of wavy black ground spikes fading downwards."""
    rnd = random.Random(seed)
    a = Art(px(30), px(20))
    u, v = a.grid()
    top = 1.5 + 3.2 * np.cos((u + 15) / 30 * 2 * math.pi * 1.5 + 0.6) + 1.2 * np.sin(u * 0.9)
    body = (np.abs(u) <= 15) & (v < top) & (v > -10)
    fade = np.clip((v + 10) / 12, 0, 1)
    a.rgb[:] = 0
    a.a = np.where(body, 0.2 + 0.8 * fade, 0).astype(np.float32)
    return a


def outline_spike(w, h, bottom=-15, width=1.8, peaks=None, alpha=1.0):
    """Outlined spike (ice spikes and invisible spikes): stroke only."""
    a = Art(px(30), px(30))
    if peaks is None:
        pts = [(-w / 2, bottom), (0, bottom + h), (w / 2, bottom)]
    else:
        pts = [(x * w / 2, bottom + y * h) for x, y in peaks]
    m = stroke_mask(a, pts, width, closed=True)
    a.paint(m, WHITE, alpha)
    return a


def saw(r_out, r_in, teeth, seed):
    """Black spinning sawblade: jagged teeth, the body fading out towards the
    centre so what sits behind it (usually a pulsing disc) shows through."""
    R = r_out
    a = Art(px(2 * R) + 2, px(2 * R) + 2)
    u, v = a.grid()
    r = np.hypot(u, v)
    ang = np.arctan2(v, u)
    # saw teeth: a ramp per tooth (asymmetric, like a circular saw)
    ph = (ang / (2 * math.pi) * teeth) % 1.0
    edge = r_in + (R - r_in) * (1 - ph) ** 1.3
    fade = np.clip((r - 0.12 * R) / (0.62 * R), 0, 1)
    a.rgb[:] = 1
    a.a = np.where(r < edge, fade * fade * (3 - 2 * fade), 0).astype(np.float32)
    return a


def star_blade(r, arms, inner, width=1.4, hole=0.0):
    """Outlined star-shaped blade (183-187)."""
    a = Art(px(2 * r) + 3, px(2 * r) + 3)
    pts = []
    for i in range(arms * 2):
        ang = math.pi / 2 + i * math.pi / arms
        rr = r if i % 2 == 0 else inner
        pts.append((rr * math.cos(ang), rr * math.sin(ang)))
    a.paint(stroke_mask(a, pts, width), WHITE, 1.0)
    if hole:
        n = 24
        ring = [(hole * math.cos(2 * math.pi * i / n), hole * math.sin(2 * math.pi * i / n)) for i in range(n)]
        a.paint(stroke_mask(a, ring, width), WHITE, 1.0)
    return a


def gear(R, teeth, spokes, hole):
    """White cogwheel (85-87) drawn for additive player-colour tint."""
    a = Art(px(2 * R) + 2, px(2 * R) + 2)
    u, v = a.grid()
    r = np.hypot(u, v)
    ang = np.arctan2(v, u)
    tooth = (np.cos(ang * teeth) > 0.1)
    rim_out = np.where(tooth, R, R * 0.8)
    body = (r < rim_out) & (r > R * 0.62)
    hub = r < R * 0.28
    spoke = (np.abs(np.sin((ang) * spokes / 2)) * r < R * 0.1) & (r < R * 0.66)
    m = body | hub | spoke
    m &= ~(r < hole)
    a.paint(m.astype(np.float32), WHITE, 1.0)
    # soft shading like the original's bevelled metal
    a.a = (a.a * (0.75 + 0.25 * np.clip(1 - r / R, 0, 1))).astype(np.float32)
    return a


def wheel(R, notches, thick):
    """Ring with inner notches (137-139)."""
    a = Art(px(2 * R) + 2, px(2 * R) + 2)
    u, v = a.grid()
    r = np.hypot(u, v)
    ang = np.arctan2(v, u)
    ring = (r < R) & (r > R - thick)
    notch = (r > R - thick - 3) & (r <= R - thick) & (np.cos(ang * notches) > 0.3)
    gap = (np.abs(np.sin(ang * 1.0 - 0.3)) < 0.06) & (r > R - thick - 1)
    m = (ring | notch) & ~gap
    a.paint(m.astype(np.float32), WHITE, 0.85)
    return a


def cartwheel(R, spokes):
    a = Art(px(2 * R) + 2, px(2 * R) + 2)
    u, v = a.grid()
    r = np.hypot(u, v)
    ang = np.arctan2(v, u)
    rim = (r < R) & (r > R * 0.82)
    hub = (r < R * 0.3) & (r > R * 0.12)
    spoke = (np.abs(np.sin(ang * spokes / 2)) * r < R * 0.07) & (r < R * 0.85) & (r > R * 0.25)
    m = rim | hub | spoke
    a.paint(m.astype(np.float32), WHITE, 0.9)
    return a


def spikewheel(R):
    """154: four long spikes around a small ring with notches."""
    a = Art(px(2 * R) + 2, px(2 * R) + 2)
    for k in range(4):
        ang = k * math.pi / 2
        c, s = math.cos(ang), math.sin(ang)
        pts = [(c * R - s * 0, s * R + c * 0), (c * 9 - s * 4.5, s * 9 + c * 4.5), (c * 9 + s * 4.5, s * 9 - c * 4.5)]
        a.paint(a.poly_mask(pts), WHITE, 0.95)
    u, v = a.grid()
    r = np.hypot(u, v)
    a.paint(((r < 13) & (r > 8.5)).astype(np.float32), WHITE, 1.0)
    a.paint((r < 4).astype(np.float32), WHITE, 1.0)
    for k in range(4):
        ang = math.pi / 4 + k * math.pi / 2
        a.paint(a.ellipse_mask(10.5 * math.cos(ang), 10.5 * math.sin(ang), 3.2, 3.2), WHITE, 1.0)
    return a


def cloud(w, h, bumps, seed, fade=False):
    rnd = random.Random(seed)
    a = Art(px(w) + 2, px(h) + 2)
    u, v = a.grid()
    m = np.zeros_like(u)
    base = -h / 2 + h * 0.18
    xs = np.linspace(-w / 2 + h * 0.3, w / 2 - h * 0.3, bumps)
    for i, x in enumerate(xs):
        rr = h * (0.28 + 0.2 * rnd.random()) * (1.35 if i == bumps // 2 else 1.0)
        m = np.maximum(m, a.ellipse_mask(x, base + rr * 0.55, rr, rr))
    m = np.maximum(m, a.rect_mask(-w / 2 + h * 0.2, -h / 2 + 1, w / 2 - h * 0.2, base + 2))
    t = np.clip((v + h / 2) / h, 0, 1)
    shade = 0.55 + 0.45 * t if not fade else 0.15 + 0.85 * t
    a.paint(m, WHITE, 1.0)
    a.a = (a.a * shade).astype(np.float32)
    return a


def wide_chain(length, links):
    """Wide chain (106/107): rectangular links with a hook at the bottom."""
    a = Art(px(22) + 1, px(length) + 1)
    top = length / 2
    lh = (length - 10) / links
    for i in range(links):
        cy = top - lh * (i + 0.5)
        outer = a.rect_mask(-6, cy - lh * 0.55, 6, cy + lh * 0.55)
        inner = a.rect_mask(-3, cy - lh * 0.3, 3, cy + lh * 0.3)
        a.paint(np.clip(outer - inner, 0, 1), WHITE, 0.9)
    a.paint(a.rect_mask(-9, -top, 9, -top + 3), WHITE, 1.0)
    a.paint(np.clip(a.ellipse_mask(0, -top + 6, 4.5, 4.5) - a.ellipse_mask(0, -top + 6, 2, 2), 0, 1), WHITE, 1.0)
    return a


def spike_rod(h, dots):
    a = Art(px(14) + 1, px(h) + 1)
    top = h / 2
    pts = [(0, top), (5, -top + 8), (-5, -top + 8)]
    a.paint(a.poly_mask(pts), WHITE, 1.0)
    for i in range(dots):
        y = -top + 12 + i * 6
        a.erase(a.ellipse_mask(0, y, 2.0, 2.0))
    a.paint(a.poly_mask([(-6, -top), (6, -top), (0, -top + 6)]), WHITE, 1.0)
    return a


def diamond_rod(h):
    a = Art(px(14) + 1, px(h) + 1)
    top = h / 2
    n = 3
    dh = (h - 8) / n
    for i in range(n):
        cy = top - dh * (i + 0.5)
        pts = [(0, cy + dh / 2), (5.5, cy), (0, cy - dh / 2), (-5.5, cy)]
        a.paint(stroke_mask(a, pts, 1.4), WHITE, 1.0)
    a.paint(a.poly_mask([(-5, -top), (5, -top), (0, -top + 7)]), WHITE, 1.0)
    return a


def deco_bricks(w, h, seed):
    """Stepped skyline of blocks fading down (113/114)."""
    rnd = random.Random(seed)
    a = Art(px(w) + 1, px(h) + 1)
    x = -w / 2
    while x < w / 2:
        bw = rnd.choice([15, 22.5, 30, 37.5])
        bh = rnd.uniform(0.35, 1.0) * h
        a.paint(a.rect_mask(x, -h / 2, min(w / 2, x + bw), -h / 2 + bh), WHITE, 1.0)
        x += bw
    u, v = a.grid()
    a.a = (a.a * (0.1 + 0.9 * np.clip((v + h / 2) / (h * 0.8), 0, 1))).astype(np.float32)
    return a


def wavy(kind):
    """Wavy decoration (157-159) in the light BG colour."""
    a = Art(px(30), px(30))
    u, v = a.grid()
    crest = 7 + 3 * np.sin((u + 15) / 30 * 2 * math.pi + 0.8)
    m = (np.abs(u) <= 15) & (v < crest)
    if kind == 'l':
        m &= (u + 15) ** 2 / 225 + np.clip(-v, 0, None) * 0 < 5
        m &= ~((u < -5) & (v > crest - (u + 5) * 1.2))
    if kind == 'r':
        m &= ~((u > 5) & (v > crest - (5 - u) * 1.2))
    t = np.clip((v + 15) / 22, 0, 1)
    a.paint(m.astype(np.float32), WHITE, 1.0)
    a.a = (a.a * (0.35 + 0.65 * t)).astype(np.float32)
    return a


def pulse_shape(kind):
    a = Art(px(30) + 1, px(30) + 1)
    u, v = a.grid()
    r = np.hypot(u, v)
    if kind == 'disc':
        m = (r < 11).astype(np.float32)
    elif kind == 'ring':
        m = ((r < 11) & (r > 6)).astype(np.float32)
    elif kind == 'diamond':
        m = ((np.abs(u) + np.abs(v)) < 11).astype(np.float32)
    elif kind == 'cross':
        d1, d2 = np.abs(u - v), np.abs(u + v)
        m = (((d1 < 5) | (d2 < 5)) & (np.maximum(np.abs(u), np.abs(v)) < 12)).astype(np.float32)
    else:  # arrow
        m = a.poly_mask([(-14, 3.5), (2, 3.5), (2, 9), (14, 0), (2, -9), (2, -3.5), (-14, -3.5)])
    a.paint(m, WHITE, 1.0)
    return a


def size_portal(style, part):
    """Size portals (99 green, 101 pink): crescent with a stepped shell."""
    colors = PORTAL_STYLE[style]
    light, mid, dark = colors
    a = Art(px(34) + 1, px(92) + 1)
    u, v = a.grid()
    if part == 'back':
        portal_arc(a, -3.0, 10.0, 40.0, 6.0, colors, 0.95)
        return a
    for i in range(9):
        y0 = -36 + i * 8
        w = 5 + 3 * math.cos((y0 + 4) / 40 * math.pi / 2)
        a.paint(a.rect_mask(1.5, y0, 1.5 + w, y0 + 5.5), BLACK, 0.95)
        a.paint(a.rect_mask(0.5, y0 + 1, 2.5, y0 + 4.5), mid, 0.9)
    portal_arc(a, -4.5, 9.5, 40.0, 5.5, colors, 1.0)
    for dy in (44.0, -44.0):
        a.paint(a.ellipse_mask(0.5, dy, 2.6, 2.6), mid, 1.0)
    return a


class Quarter:
    """The top right quarter of a round sprite, anchored at the centre; the
    renderer mirrors it into the other three (ObjPart flag PF_QUAD)."""

    def __init__(self, art):
        self.art = art
        self.ax, self.ay = 0, art.ay

    def image(self):
        img = self.art.image()
        return img[:self.art.ay, self.art.ax:]


class Half:
    """A sprite stored at half resolution, drawn at twice the size (ObjPart
    flag PF_HALF): for large soft shapes such as clouds."""

    def __init__(self, art):
        self.art = art
        self.ax, self.ay = art.ax // 2, art.ay // 2

    def image(self):
        img = self.art.image()
        h, w = img.shape[0] // 2 * 2, img.shape[1] // 2 * 2
        img = img[:h, :w]
        a = img[..., 3:4]
        prem = (img[..., :3] * a).reshape(h // 2, 2, w // 2, 2, 3).mean(axis=(1, 3))
        a2 = a.reshape(h // 2, 2, w // 2, 2, 1).mean(axis=(1, 3))
        rgb = np.where(a2 > 1e-6, prem / np.maximum(a2, 1e-6), 0)
        return np.concatenate([rgb, a2], axis=2)


def bevel_body():
    """Beveled face (1.x ID 73 family): four black facets of different depth."""
    a = Art(px(30), px(30))
    u, v = a.grid()
    inside = (np.abs(u) < 15) & (np.abs(v) < 15)
    left, top = (u < v) & (u < -v), (v > u) & (v > -u)
    right = (u > v) & (u > -v)
    al = np.where(left, 0.92, np.where(top, 0.78, np.where(right, 0.55, 0.68)))
    a.rgb[:] = 0
    a.a = np.where(inside, al, 0).astype(np.float32)
    return a


PORTAL_STYLE.update({
    'ufo': (hexc('fff0b0'), hexc('ffb000'), hexc('e86a00')),
    'mini': (hexc('ffc8ff'), hexc('ff4cff'), hexc('c800c8')),
    'big': (hexc('c8ffc8'), hexc('20ff40'), hexc('00a020')),
})


def ufo_layers():
    """Default UFO in the layout of Geometry Dash's first one: a saucer whose
    upper band is the primary colour and lower bowl the secondary one, under
    a glass dome (drawn behind the half-size cube). The anchor is the
    player's centre; returns (primary, secondary, dome)."""
    W, H = px(39) + 2, px(32) + 2
    p, s, d = Art(W, H), Art(W, H), Art(W, H)
    u, v = p.grid()
    top = -0.5
    bowl = (((u / 18.2) ** 2 + ((v - top) / 13.8) ** 2) < 1) & (v <= top)
    rim = (np.abs(u) < 18.8) & (v > top - 1.2) & (v < top + 2.4)
    body = bowl | rim
    p.paint(body.astype(np.float32), BLACK, 1.0)
    inner_bowl = (((u / 16.6) ** 2 + ((v - top) / 12.2) ** 2) < 1) & (v <= top - 1.2)
    inner_rim = (np.abs(u) < 17.4) & (v > top - 0.2) & (v < top + 1.2)
    band = inner_bowl & (v > top - 6.0)
    p.paint((band | inner_rim).astype(np.float32), WHITE, 1.0)
    p.paint((inner_rim & (v > top + 0.6)).astype(np.float32), (0.8, 0.8, 0.8), 1.0)
    # the lower bowl is the secondary colour (a hole in the primary layer)
    low = inner_bowl & (v <= top - 7.4)
    p.erase(low.astype(np.float32))
    s.paint(low.astype(np.float32), WHITE, 1.0)
    s.paint((low & (v < top - 10.5)).astype(np.float32), (0.75, 0.75, 0.75), 1.0)
    # dome: a pale glass half disc with a dark outline and a highlight
    r = np.hypot(u / 12.5, (v - top - 1.2) / 13.2)
    above = v > top + 1.2
    d.paint(((r < 1) & above).astype(np.float32), WHITE, 0.3)
    d.paint(((r < 1) & (r > 0.9) & above).astype(np.float32), BLACK, 0.85)
    shine = (r < 0.8) & (r > 0.66) & above & (u < -2) & (v > top + 5)
    d.paint(shine.astype(np.float32), WHITE, 0.7)
    return p, s, d


def demon_face():
    """The Demon difficulty face: an angry red head with two horns, slanted
    brows and a grin full of teeth (sized like the other faces)."""
    from sprites_ui import Px, grow
    d = 26
    c = Px(d + 9, d + 9)
    cx, cy, r = (d + 9) / 2, (d + 9) / 2 + 2.5, d / 2 - 1
    horns = np.maximum(c.poly([(cx - r * 0.95, cy - r * 0.2), (cx - r * 1.12, cy - r * 1.38), (cx - r * 0.35, cy - r * 0.78)]),
                       c.poly([(cx + r * 0.95, cy - r * 0.2), (cx + r * 1.12, cy - r * 1.38), (cx + r * 0.35, cy - r * 0.78)]))
    head = c.ellipse(cx, cy, r * 1.02, r * 0.92)
    c.paint(grow(np.maximum(horns, head), 1.2), BLACK)
    c.paint(horns, c.vgrad(hexc('ffd2c8'), hexc('b43c28'), cy - r * 1.4, cy - r * 0.2))
    c.paint(head, c.vgrad(hexc('ff5a46'), hexc('a00a0a'), cy - r, cy + r))
    c.paint(c.ellipse(cx - r * 0.35, cy - r * 0.55, r * 0.3, r * 0.14), WHITE, 0.35)
    for sx in (-1, 1):
        ex = cx + sx * r * 0.4
        brow = c.poly([(ex - sx * r * 0.36, cy - r * 0.52), (ex + sx * r * 0.34, cy - r * 0.2),
                       (ex + sx * r * 0.3, cy - r * 0.04), (ex - sx * r * 0.36, cy - r * 0.34)])
        eye = c.poly([(ex - sx * r * 0.3, cy - r * 0.26), (ex + sx * r * 0.26, cy - r * 0.02), (ex - sx * r * 0.22, cy + r * 0.1)])
        c.paint(eye, hexc('fff05a'))
        c.paint(c.ellipse(ex - sx * r * 0.06, cy - r * 0.07, r * 0.07), BLACK)
        c.paint(brow, BLACK)
    mouth = c.ellipse(cx, cy + r * 0.3, r * 0.62, r * 0.42) * c.rect(0, cy + r * 0.3, 999, 999)
    c.paint(mouth, BLACK)
    for tx in (-0.42, -0.14, 0.14, 0.42):
        c.paint(c.poly([(cx + (tx - 0.12) * r, cy + r * 0.3), (cx + (tx + 0.12) * r, cy + r * 0.3), (cx + tx * r, cy + r * 0.52)]), WHITE)
    return c


def clubstep_sprites():
    out = []
    add = lambda n, a, f: out.append((n, a, f))
    for st in ('ufo',):
        add('portal_%s_back' % st, mode_portal_art(st, 'back'), 'PAL4')
        add('portal_%s_front' % st, mode_portal_art(st, 'front'), 'PAL4')
    add('portal_mini_back', size_portal('mini', 'back'), 'PAL4')
    add('portal_mini_front', size_portal('mini', 'front'), 'PAL4')
    add('portal_big_back', size_portal('big', 'back'), 'PAL4')
    add('portal_big_front', size_portal('big', 'front'), 'PAL4')
    add('face_demon', demon_face(), 'PAL4')
    up, us, ud = ufo_layers()
    add('ufo1_p', up, 'LA44')
    add('ufo1_s', us, 'LA44')
    add('ufo1_dome', ud, 'LA44')
    add('bev_body', bevel_body(), 'A4')
    add('fake_spike', fake_spike(), 'A4')
    add('fake_spike_h', fake_spike(30, 12, -15), 'A4')
    add('fake_spike_s', fake_spike(18, 20, -15), 'A4')
    add('ground_spikes', ground_spikes(), 'LA44')
    ice = [(-1, 0), (-0.45, 0.62), (-0.2, 0.45), (0.2, 1.0), (0.5, 0.55), (0.7, 0.72), (1, 0)]
    add('ice_spike', outline_spike(30, 30, -15, peaks=ice), 'A4')
    add('ice_spike_half', outline_spike(30, 14, -15, peaks=[(-1, 0), (-0.5, 0.7), (-0.2, 0.45), (0.2, 1.0), (0.5, 0.6), (1, 0)]), 'A4')
    add('ice_spike_small', outline_spike(18, 20, -12, peaks=[(-1, 0), (-0.2, 0.8), (0.05, 0.6), (0.4, 1.0), (1, 0)]), 'A4')
    add('invis_spike', outline_spike(30, 30, -15, 1.4, alpha=0.6), 'A4')
    add('invis_spike_s', outline_spike(20, 19, -9.5, 1.4, alpha=0.6), 'A4')
    inv = Art(px(30), px(30))
    inv.paint(stroke_mask(inv, [(-14, -14), (14, -14), (14, 14), (-14, 14)], 1.4), WHITE, 0.6)
    add('invis_square', inv, 'A4')
    add('saw_big', Quarter(saw(47, 30, 16, 1)), 'A4')
    add('saw_med', Quarter(saw(32, 20, 12, 2)), 'A4')
    add('saw_small', saw(18, 11, 9, 3), 'A4')
    add('blade_big', Quarter(star_blade(33, 4, 7)), 'A4')
    add('blade_med', star_blade(26, 3, 8), 'A4')
    add('blade_small', star_blade(12, 2, 3.5), 'A4')
    add('oblade_big', Quarter(star_blade(33, 12, 24, 1.6, 10)), 'A4')
    add('oblade_med', Quarter(star_blade(23, 12, 17, 1.5, 7)), 'A4')
    add('gear_l', Quarter(gear(40, 8, 4, 5)), 'A4')
    add('gear_m', Quarter(gear(26, 8, 4, 4)), 'A4')
    add('gear_s', gear(17, 8, 4, 2.5), 'A4')
    add('wheel_l', Quarter(wheel(43, 8, 7)), 'A4')
    add('wheel_m', Quarter(wheel(26, 8, 5)), 'A4')
    add('wheel_s', wheel(15, 6, 3.5), 'A4')
    add('spikewheel', Quarter(spikewheel(40)), 'A4')
    add('cartwheel_l', Quarter(cartwheel(30, 12)), 'A4')
    add('cartwheel_m', Quarter(cartwheel(22, 8)), 'A4')
    add('cartwheel_s', cartwheel(15, 3), 'A4')
    add('wide_chain', wide_chain(90, 3), 'A4')
    add('wide_chain_s', wide_chain(45, 1), 'A4')
    add('cloud_fade_l', Half(cloud(128, 40, 5, 4, True)), 'A4')
    add('cloud_fade_s', Half(cloud(90, 30, 4, 5, True)), 'A4')
    add('cloud_m', Half(cloud(90, 32, 3, 6)), 'A4')
    add('cloud_l', Half(cloud(128, 44, 3, 7)), 'A4')
    add('cloud_s', cloud(44, 16, 3, 8), 'A4')
    add('pulse_disc', pulse_shape('disc'), 'A4')
    add('pulse_ring', pulse_shape('ring'), 'A4')
    add('pulse_diamond', pulse_shape('diamond'), 'A4')
    add('pulse_arrow', pulse_shape('arrow'), 'A4')
    add('pulse_cross', pulse_shape('cross'), 'A4')
    add('spikerod_l', spike_rod(58, 3), 'A4')
    add('spikerod_m', spike_rod(36, 2), 'A4')
    add('spikerod_s', spike_rod(22, 1), 'A4')
    add('diamond_rod', diamond_rod(60), 'A4')
    add('wavy', wavy(''), 'A4')
    add('wavy_l', wavy('l'), 'A4')
    add('wavy_r', wavy('r'), 'A4')
    return out


def lv_sprites():
    return clubstep_sprites()
