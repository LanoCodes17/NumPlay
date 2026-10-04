"""The menus' big pictures, made from the game's files for tools/pack.py:

- the chapters' end screens: CompleteScreens.xml's layers where they rest once slid in (CompleteRenderer), one
  picture each;
- the mountain behind the chapter select: the Overworld models (mountain, its Core wall, the buildings) with their
  textures, the skybox and the fog ring, seen from each chapter's Idle camera (AreaViews.xml), as MountainModel draws
  them (no lighting: its shader only samples the textures), with the vignette over them; and one from the main
  menu's turning camera.

All at half the view's size (160 x 90), as baseline JPEG (Pillow's, standard tables): src/pic.c decodes one into
the cache and draws it twice as big. PICS section: u16 n, u16 0, per picture u16 w, h, u32 offset, u32 bytes; the
quantization tables (2 x 64, zigzag order); the Huffman tables DC0, AC0, DC1, AC1 (16 counts, then the symbols);
then the pictures' entropy coded data (no markers, no 0xFF00 stuffing)."""
import io
import math
import os
import struct
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

from cel import CELESTE, Atlas, Reader, read_data

W, H = 160, 90
QUALITY = 50
COMPLETE = ["ForsakenCity", "OldSite", "CelestialResort", "Cliffside", "MirrorTemple", "Fall", "Summit", "Core"]
COMPLETE_AREAS = [1, 2, 3, 4, 5, 6, 7, 9]   # AreaData.CompleteScreenName (the Prologue and the Epilogue have none)
ATLASES = os.path.join(CELESTE, "Graphics", "Atlases")
OVERWORLD = os.path.join(CELESTE, "Overworld")
CACHE = os.environ.get("CELESTE_CACHE", "/tmp/celeste_cache")


# ------------------------------------------------------------------ the chapters' end screens

def _meta(atlas):
    """A PackerNoAtlas atlas (an image file each): name -> (x, y, w, h, frame x, frame y)."""
    r = Reader(open(os.path.join(ATLASES, atlas + ".meta"), "rb").read())
    r.i32()
    r.str()
    r.i32()
    out = {}
    for _ in range(r.i16()):
        r.str()
        for _ in range(r.i16()):
            path = r.str().replace("\\", "/")
            x, y, w, h = r.i16(), r.i16(), r.i16(), r.i16()
            fx, fy = r.i16(), r.i16()
            r.i16()
            r.i16()
            out[path] = (x, y, w, h, fx, fy)
    return out


def complete_screen(name):
    """CompleteRenderer's layers at rest (Scroll = CenterScroll), first frames, 1920 x 1080 RGB floats."""
    screen = ET.parse(os.path.join(CELESTE, "Graphics", "CompleteScreens.xml")).getroot().find(name)
    atlas = screen.get("atlas")
    meta = _meta(atlas)

    def pos(e):
        return (float(e.get("x", 0)), float(e.get("y", 0))) if e is not None else (0.0, 0.0)
    center, offset = pos(screen.find("center")), pos(screen.find("offset"))
    out = np.zeros((1080, 1920, 3), np.float32)
    for e in screen.find("layers"):
        if e.tag != "layer":
            continue
        if "scroll" in e.attrib:
            sx = sy = float(e.get("scroll"))
        else:
            sx, sy = float(e.get("scrollX", 0)), float(e.get("scrollY", 0))
        x, y = pos(e)
        x, y = x + offset[0], y + offset[1]
        # GetScrollPosition(-Scroll), floored
        px, py = math.floor(x - center[0] * sx), math.floor(y - center[1] * sy)
        img = e.get("img").split(",")[0]
        if img not in meta:
            continue
        ix, iy, iw, ih, fx, fy = meta[img]
        data = read_data(os.path.join(ATLASES, atlas, img + ".data"))[iy:iy + ih, ix:ix + iw]
        src = data[1:ih - 1, 1:iw - 1].astype(np.float32) / 255 * float(e.get("alpha", 1))   # (ClipRect less a pixel)
        X0, Y0 = px - fx, py - fy   # + DrawOffset
        x0, y0 = max(X0, 0), max(Y0, 0)
        x1, y1 = min(X0 + src.shape[1], 1920), min(Y0 + src.shape[0], 1080)
        if x0 < x1 and y0 < y1:
            s = src[y0 - Y0:y1 - Y0, x0 - X0:x1 - X0]
            out[y0:y1, x0:x1] = s[..., :3] + out[y0:y1, x0:x1] * (1 - s[..., 3:4])   # (premultiplied)
    return out


def complete_slide(name):
    """StartScroll - CenterScroll: how far the end screen slides in."""
    screen = ET.parse(os.path.join(CELESTE, "Graphics", "CompleteScreens.xml")).getroot().find(name)
    s, c = screen.find("start"), screen.find("center")
    return (float(s.get("x", 0)) - float(c.get("x", 0)), float(s.get("y", 0)) - float(c.get("y", 0)))


# ------------------------------------------------------------------ the mountain

def _texture(name):
    path = os.path.join(CACHE, "mountain_" + name + ".npy")
    if os.path.exists(path):
        return np.load(path)
    a = read_data(os.path.join(ATLASES, "Mountain", name + ".data")).astype(np.float32) / 255
    os.makedirs(CACHE, exist_ok=True)
    np.save(path, a)
    return a


def _model(name):
    """ObjModel: triangles (n, 3, 3) and their texture coordinates (n, 3, 2), v going down."""
    vs, vts, P, T = [], [], [], []
    for line in open(os.path.join(OVERWORLD, name + ".obj")):
        a = line.split()
        if not a:
            continue
        if a[0] == "v":
            vs.append([float(x) for x in a[1:4]])
        elif a[0] == "vt":
            vts.append([float(x) for x in a[1:3]])
        elif a[0] == "f":
            idx = [s.split("/") for s in a[1:4]]
            P.append([vs[int(i[0]) - 1] for i in idx])
            T.append([[vts[int(i[1]) - 1][0], 1 - vts[int(i[1]) - 1][1]] for i in idx])
    return np.array(P, np.float32), np.array(T, np.float32)


def _views():
    out = {}
    for a in ET.parse(os.path.join(OVERWORLD, "AreaViews.xml")).getroot():
        d = {"state": int(a.get("state"))}
        for c in a:
            if c.tag != "Cursor":
                d[c.tag] = ([float(x) for x in c.get("position").split(",")],
                            [float(x) for x in c.get("target").split(",")])
        out[int(a.get("id"))] = d
    return out


class Raster:
    """Triangles with a depth buffer, perspective correct, textures sampled bilinearly
    (CreatePerspectiveFieldOfView(pi / 4, 320 / 180, 0.25, 50))."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.f = 1 / math.tan(math.pi / 8)
        self.color = np.zeros((h, w, 3), np.float32)
        self.depth = np.full((h, w), np.inf, np.float32)

    def draw(self, rot, origin, P, T, tex, tint=(1, 1, 1, 1), depth=True, blend=False, wrap=False):
        cam = ((P.reshape(-1, 3) - origin) @ rot.T).reshape(-1, 3, 3)
        w = -cam[..., 2]
        cx, cy = self.f * 180 / 320 * cam[..., 0], self.f * cam[..., 1]
        tint = np.array(tint, np.float32)
        for k in range(len(P)):
            poly = [(cx[k, i], cy[k, i], w[k, i], T[k, i, 0], T[k, i, 1]) for i in range(3)]
            if min(p[2] for p in poly) < 0.25:
                poly = _clip(poly, 0.25)
            for j in range(1, len(poly) - 1):
                self._tri((poly[0], poly[j], poly[j + 1]), tex, tint, depth, blend, wrap)

    def _tri(self, tri, tex, tint, depth, blend, wrap):
        pts = []
        for x, y, w, u, v in tri:
            iw = 1 / w
            pts.append(((x * iw * 0.5 + 0.5) * self.w, (0.5 - y * iw * 0.5) * self.h, iw, u * iw, v * iw))
        (x0, y0, *_), (x1, y1, *_), (x2, y2, *_) = pts
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if area == 0:
            return
        xa, xb = max(int(math.floor(min(x0, x1, x2))), 0), min(int(math.ceil(max(x0, x1, x2))), self.w)
        ya, yb = max(int(math.floor(min(y0, y1, y2))), 0), min(int(math.ceil(max(y0, y1, y2))), self.h)
        if xa >= xb or ya >= yb:
            return
        X, Y = np.meshgrid(np.arange(xa, xb) + 0.5, np.arange(ya, yb) + 0.5)
        l0 = ((x1 - X) * (y2 - Y) - (x2 - X) * (y1 - Y)) / area
        l1 = ((x2 - X) * (y0 - Y) - (x0 - X) * (y2 - Y)) / area
        l2 = 1 - l0 - l1
        m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        iw = l0 * pts[0][2] + l1 * pts[1][2] + l2 * pts[2][2]
        z = 1 / np.where(m, iw, 1)
        zb = self.depth[ya:yb, xa:xb]
        if depth:
            m &= z < zb
        if not m.any():
            return
        u = (l0 * pts[0][3] + l1 * pts[1][3] + l2 * pts[2][3])[m] * z[m]
        v = (l0 * pts[0][4] + l1 * pts[1][4] + l2 * pts[2][4])[m] * z[m]
        s = _sample(tex, u, v, wrap) * tint
        cb = self.color[ya:yb, xa:xb]
        cb[m] = s[:, :3] + cb[m] * (1 - s[:, 3:4]) if blend else s[:, :3]
        if depth:
            zb[m] = z[m]


def _clip(poly, near):
    out = []
    for i in range(len(poly)):
        p, q = poly[i], poly[(i + 1) % len(poly)]
        if p[2] >= near:
            out.append(p)
        if (p[2] >= near) != (q[2] >= near):
            t = (near - p[2]) / (q[2] - p[2])
            out.append(tuple(a + (b - a) * t for a, b in zip(p, q)))
    return out


def _sample(img, u, v, wrap):
    h, w = img.shape[:2]
    x, y = u * w - 0.5, v * h - 0.5
    x0, y0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    xs = [x0 % w, (x0 + 1) % w] if wrap else [np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1)]
    ys = [np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)]
    a = img[ys[0], xs[0]] * (1 - fx) + img[ys[0], xs[1]] * fx
    b = img[ys[1], xs[0]] * (1 - fx) + img[ys[1], xs[1]] * fx
    return a * (1 - fy) + b * fy


def _look_at(pos, target):   # Matrix.CreateLookAt's rotation (rows: right, up, back)
    pos, target = np.array(pos, np.float64), np.array(target, np.float64)
    z = (pos - target) / np.linalg.norm(pos - target)
    x = np.cross((0, 1, 0), z)
    x /= np.linalg.norm(x)
    return np.stack([x, np.cross(z, x), z])


def _skybox(tex, s=25.0):   # Skybox: five faces of 820 pixels, from one texture
    v = {1: (-s, s, -s), 2: (s, s, -s), 3: (s, s, s), 4: (-s, s, s), 5: (-s, -s, -s), 6: (s, -s, -s), 7: (s, -s, s),
         8: (-s, -s, s)}
    faces = [(0, 1, 2, 3, 4, 820), (2460, 2, 1, 5, 6, 820), (820, 4, 3, 7, 8, 820), (3280, 3, 2, 6, 7, 819),
             (1640, 1, 4, 8, 5, 820)]
    h, w = tex.shape[:2]
    P, T = [], []
    for x, a, b, c, d, fw in faces:
        l, t, r, bo = (x + 1) / w, 1 / h, (x + fw - 1) / w, 819 / h
        P += [[v[a], v[b], v[c]], [v[a], v[c], v[d]]]
        T += [[(l, t), (r, t), (r, bo)], [(l, t), (r, bo), (l, bo)]]
    return np.array(P, np.float32), np.array(T, np.float32)


def _ring(top, bottom, dist, steps=24):
    P, T = [], []
    for i in range(steps):
        a0, a1 = (i - 1) / steps, i / steps
        A = (math.cos(a0 * 2 * math.pi) * dist, math.sin(a0 * 2 * math.pi) * dist)
        B = (math.cos(a1 * 2 * math.pi) * dist, math.sin(a1 * 2 * math.pi) * dist)
        q = [(A[0], top, A[1]), (B[0], top, B[1]), (B[0], bottom, B[1]), (A[0], bottom, A[1])]
        P += [[q[0], q[1], q[2]], [q[0], q[2], q[3]]]
        T += [[(a0, 0.01), (a1, 0.01), (a1, 1)], [(a0, 0.01), (a1, 1), (a0, 1)]]
    return np.array(P, np.float32), np.array(T, np.float32)


FOG = {0: 0x010817, 1: 0x13203E, 2: 0x281A35}   # MountainModel's states' fog colors
_MODELS = {}


def mountain_view(pos, target, state, core_door=0.0, near_fog=0.0, size=(640, 360)):
    for name in ("mountain", "mountain_wall", "buildings"):
        if name not in _MODELS:
            _MODELS[name] = _model(name)
    r = Raster(*size)
    rot = _look_at(pos, target)
    sky = _texture("skybox_%d" % state)
    P, T = _skybox(sky)
    r.draw(rot, np.array([0, pos[1] * 1.1 - 5, 0]), P, T, sky, depth=False)
    terrain = _texture("mountain_%d" % state)
    r.draw(rot, np.array(pos), *_MODELS["mountain"], terrain)
    # the Core wall opens for chapter 9 (MountainRenderer's door)
    r.draw(rot, np.array(pos) - np.array([1.5, -1.5, -1.0]) * core_door, *_MODELS["mountain_wall"], terrain)
    r.draw(rot, np.array(pos), *_MODELS["buildings"], _texture("buildings_%d" % state))
    fog, c = _texture("fog"), FOG[state]
    r.draw(rot, np.array(pos), *_ring(6, -1, 20), fog, ((c >> 16) / 255, (c >> 8 & 255) / 255, (c & 255) / 255, 1),
           blend=True, wrap=True)
    if near_fog:
        r.draw(rot, np.array(pos), *_ring(6, -4, 10), fog, (0.3 * near_fog,) * 4, blend=True, wrap=True)
    # MountainRenderer: the vignette over it at 0.2
    vig = Image.fromarray(Atlas("Overworld")["vignette"].image()[..., 3]).resize(size, Image.BILINEAR)
    return r.color * (1 - 0.2 * np.asarray(vig, np.float32)[..., None] / 255)


# ------------------------------------------------------------------ JPEG

def _jpeg(img):
    """Pillow's baseline JPEG of an RGB float image: (quantization tables, Huffman tables, entropy coded data)."""
    im = Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)).resize((W, H), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=QUALITY, subsampling=2, optimize=False)
    data = b.getvalue()
    p, dqt, dht, scan = 2, {}, {}, None
    while scan is None:
        assert data[p] == 0xFF
        m, n = data[p + 1], struct.unpack(">H", data[p + 2:p + 4])[0]
        seg = data[p + 4:p + 2 + n]
        if m == 0xDB:
            for i in range(0, len(seg), 65):
                assert seg[i] >> 4 == 0
                dqt[seg[i] & 15] = bytes(seg[i + 1:i + 65])
        elif m == 0xC4:
            i = 0
            while i < len(seg):
                k = 17 + sum(seg[i + 1:i + 17])
                dht[seg[i] >> 4, seg[i] & 15] = bytes(seg[i + 1:i + k])
                i += k
        elif m == 0xC0:
            comps = [tuple(seg[6 + 3 * k:9 + 3 * k]) for k in range(seg[5])]
            assert comps == [(1, 0x22, 0), (2, 0x11, 1), (3, 0x11, 1)], comps
        elif m == 0xDA:
            assert list(seg[1:7]) == [1, 0x00, 2, 0x11, 3, 0x11], list(seg)
            q, ent = p + 2 + n, bytearray()
            while not (data[q] == 0xFF and data[q + 1] != 0):
                ent.append(data[q])
                q += 2 if data[q] == 0xFF else 1
            scan = bytes(ent)
        elif m == 0xDD:
            raise ValueError("restart markers")
        p += 2 + n
    return dqt[0] + dqt[1], dht[0, 0] + dht[1, 0] + dht[0, 1] + dht[1, 1], scan


def build():
    """The PICS section's bytes and what goes in src/data.h."""
    images = []
    for name in COMPLETE:
        images.append(complete_screen(name))
    views = _views()
    for a in range(10):
        pos, target = views[a]["Idle"]
        images.append(mountain_view(pos, target, views[a]["state"], core_door=1.0 if a == 9 else 0.0))
    # the main menu: MountainRenderer's turning camera (15 away, 3 up, looking at 0, 7, 0), the near fog on
    images.append(mountain_view((0, 3, 15), (0, 7, 0), 0, near_fog=1.0))
    tables, body, entries = None, bytearray(), []
    for img in images:
        q, h, scan = _jpeg(img)
        assert tables in (None, q + h), "the pictures' tables differ"
        tables = q + h
        entries.append((len(body), len(scan)))
        body += scan
    out = bytearray(struct.pack("<HH", len(images), 0))
    base = 4 + 12 * len(images) + len(tables)
    for off, n in entries:
        out += struct.pack("<HHII", W, H, base + off, n)
    out += tables + body
    header = ["#define PIC_COMPLETE %d   /* the end screens, in COMPLETE_AREAS' order */" % 0,
              "#define PIC_MOUNTAIN %d   /* the mountain from each chapter's Idle camera */" % len(COMPLETE),
              "#define PIC_MAIN %d   /* and from the main menu's */" % (len(COMPLETE) + 10)]
    slides = [complete_slide(n) for n in COMPLETE]
    header.append("static const int16_t PIC_SLIDE[%d][2] = {%s};   /* StartScroll - CenterScroll */" % (
        len(slides), ", ".join("{%d, %d}" % (round(x), round(y)) for x, y in slides)))
    return bytes(out), header
