"""Readers for Celeste's content files (maps, atlases), used by the converter.

CELESTE points at the game's Content folder (the one with Maps/ and Graphics/).
"""
import os
import struct
import numpy as np

CELESTE = os.environ.get("CELESTE", os.path.expanduser("~/Documents/Assets/gamesrc/celeste/Celeste"))


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.d, self.p)[0]
        self.p += 2
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]
        self.p += 4
        return v

    def str(self):
        n = shift = 0
        while True:
            b = self.u8()
            n |= (b & 0x7F) << shift
            shift += 7
            if b < 0x80:
                break
        s = self.d[self.p:self.p + n].decode("utf-8")
        self.p += n
        return s


class Element:
    __slots__ = ("name", "attr", "children")

    def __init__(self, name):
        self.name = name
        self.attr = {}
        self.children = []

    def child(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def __getitem__(self, k):
        return self.attr[k]

    def get(self, k, default=None):
        return self.attr.get(k, default)

    def __repr__(self):
        return f"<{self.name} {self.attr} [{len(self.children)}]>"


def read_map(path):
    """A map (.bin, BinaryPacker format) as an Element tree; returns (package, root)."""
    r = Reader(open(path, "rb").read())
    assert r.str() == "CELESTE MAP"
    package = r.str()
    lookup = [r.str() for _ in range(r.i16())]

    def element():
        e = Element(lookup[r.i16()])
        for _ in range(r.u8()):
            k = lookup[r.i16()]
            t = r.u8()
            if t == 0:
                v = r.u8() != 0
            elif t == 1:
                v = r.u8()
            elif t == 2:
                v = r.i16()
            elif t == 3:
                v = r.i32()
            elif t == 4:
                v = r.f32()
            elif t == 5:
                v = lookup[r.i16()]
            elif t == 6:
                v = r.str()
            elif t == 7:
                n = r.i16()
                raw = r.d[r.p:r.p + n]
                r.p += n
                v = "".join(chr(raw[i + 1]) * raw[i] for i in range(0, n, 2))
            else:
                raise ValueError(t)
            e.attr[k] = v
        for _ in range(r.i16()):
            e.children.append(element())
        return e

    return package, element()


class Sub:
    """A texture in an atlas: where it is in its page, and its frame (trim offsets)."""
    __slots__ = ("atlas", "page", "path", "x", "y", "w", "h", "fx", "fy", "fw", "fh")

    def __repr__(self):
        return f"<{self.path} {self.w}x{self.h} frame {self.fx},{self.fy} {self.fw}x{self.fh}>"

    def image(self):
        """RGBA numpy array (premultiplied alpha), with the trim restored (fw x fh)."""
        out = np.zeros((self.fh, self.fw, 4), np.uint8)
        out[-self.fy:-self.fy + self.h, -self.fx:-self.fx + self.w] = self.trimmed()
        return out

    def trimmed(self):
        own = os.path.join(self.atlas.dir, self.atlas.name, self.path + ".data")
        if not os.path.exists(os.path.join(self.atlas.dir, self.page + ".data")) and os.path.exists(own):
            return read_data(own)[self.y:self.y + self.h, self.x:self.x + self.w]
        page = self.atlas.page(self.page)
        return page[self.y:self.y + self.h, self.x:self.x + self.w]


def read_data(path):
    """An atlas page (.data: RLE coded) as an RGBA numpy array."""
    d = open(path, "rb").read()
    w, h = struct.unpack_from("<ii", d, 0)
    alpha = d[8] != 0
    out = np.zeros(w * h * 4, np.uint8)
    p, o, end = 9, 0, w * h * 4
    # runs: count, then [alpha], then BGR when visible
    mv = memoryview(d)
    while o < end:
        n = d[p]
        p += 1
        if alpha:
            a = d[p]
            p += 1
            if a:
                b, g, r = d[p], d[p + 1], d[p + 2]
                p += 3
                px = bytes((r, g, b, a)) * n
            else:
                px = bytes(4 * n)
        else:
            b, g, r = d[p], d[p + 1], d[p + 2]
            p += 3
            px = bytes((r, g, b, 255)) * n
        out[o:o + 4 * n] = np.frombuffer(px, np.uint8)
        o += 4 * n
    return out.reshape(h, w, 4)


class Atlas:
    def __init__(self, name):
        self.name = name
        self.dir = os.path.join(CELESTE, "Graphics", "Atlases")
        r = Reader(open(os.path.join(self.dir, name + ".meta"), "rb").read())
        r.i32()
        r.str()
        r.i32()
        self.subs = {}
        self.pages = []
        self._pages = {}
        for _ in range(r.i16()):
            page = r.str()
            self.pages.append(page)
            for _ in range(r.i16()):
                s = Sub()
                s.atlas, s.page = self, page
                s.path = r.str().replace("\\", "/")
                s.x, s.y, s.w, s.h = r.i16(), r.i16(), r.i16(), r.i16()
                s.fx, s.fy, s.fw, s.fh = r.i16(), r.i16(), r.i16(), r.i16()
                self.subs[s.path] = s

    def page(self, name):
        if name not in self._pages:
            cache = os.path.join(os.environ.get("CELESTE_CACHE", "/tmp/celeste_cache"), self.name + "_" + name.replace("/", "_") + ".npy")
            if os.path.exists(cache):
                self._pages[name] = np.load(cache)
            else:
                self._pages[name] = read_data(os.path.join(self.dir, name + ".data"))
                os.makedirs(os.path.dirname(cache), exist_ok=True)
                np.save(cache, self._pages[name])
        return self._pages[name]

    # Monocle's atlases ignore case
    def _lower(self):
        if not hasattr(self, "_low"):
            self._low = {k.lower(): v for k, v in self.subs.items()}
        return self._low

    def __getitem__(self, path):
        return self.subs[path] if path in self.subs else self._lower()[path.lower()]

    def __contains__(self, path):
        return path in self.subs or path.lower() in self._lower()

    def prefix(self, pre):
        """Subtextures named pre + digits, in number order (Celeste's GetAtlasSubtextures)."""
        out = []
        low = pre.lower()
        for k, s in self.subs.items():
            if k.lower().startswith(low) and k[len(pre):].isdigit():
                out.append((int(k[len(pre):]), s))
        return [s for _, s in sorted(out, key=lambda t: t[0])]
