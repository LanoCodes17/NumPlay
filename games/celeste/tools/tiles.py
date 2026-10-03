"""Celeste's autotiler rules (ForegroundTiles.xml, BackgroundTiles.xml), read
the way the game reads them, and a reference autotiler for tests.

The game sorts each tileset's masks with List<T>.Sort, an unstable introsort,
so the same sort is done here to get the same order."""
import os
import xml.etree.ElementTree as ET
from cel import CELESTE


def _floor_log2(n):
    r = 0
    while n >= 1:
        r += 1
        n //= 2
    return r


def net_sort(items, cmp, capacity=None):
    """.NET Framework 4.5's Array.Sort<T> (IntrospectiveSort) on a list, in place."""
    a = items
    n = len(a)
    if n < 2:
        return

    def swap_if_greater(i, j):
        if i != j and cmp(a[i], a[j]) > 0:
            a[i], a[j] = a[j], a[i]

    def insertion(lo, hi):
        for i in range(lo, hi):
            j = i
            t = a[i + 1]
            while j >= lo and cmp(t, a[j]) < 0:
                a[j + 1] = a[j]
                j -= 1
            a[j + 1] = t

    def down_heap(i, n_, lo):
        d = a[lo + i - 1]
        while i <= n_ // 2:
            child = 2 * i
            if child < n_ and cmp(a[lo + child - 1], a[lo + child]) < 0:
                child += 1
            if not cmp(d, a[lo + child - 1]) < 0:
                break
            a[lo + i - 1] = a[lo + child - 1]
            i = child
        a[lo + i - 1] = d

    def heapsort(lo, hi):
        n_ = hi - lo + 1
        for i in range(n_ // 2, 0, -1):
            down_heap(i, n_, lo)
        for i in range(n_, 1, -1):
            a[lo], a[lo + i - 1] = a[lo + i - 1], a[lo]
            down_heap(1, i - 1, lo)

    def partition(lo, hi):
        mid = lo + ((hi - lo) >> 1)
        swap_if_greater(lo, mid)
        swap_if_greater(lo, hi)
        swap_if_greater(mid, hi)
        pivot = a[mid]
        a[mid], a[hi - 1] = a[hi - 1], a[mid]
        left, right = lo, hi - 1
        while left < right:
            left += 1
            while cmp(a[left], pivot) < 0:
                left += 1
            right -= 1
            while cmp(pivot, a[right]) < 0:
                right -= 1
            if left >= right:
                break
            a[left], a[right] = a[right], a[left]
        a[left], a[hi - 1] = a[hi - 1], a[left]
        return left

    def intro(lo, hi, depth):
        while hi > lo:
            size = hi - lo + 1
            if size <= 16:
                if size == 1:
                    return
                if size == 2:
                    swap_if_greater(lo, hi)
                    return
                if size == 3:
                    swap_if_greater(lo, hi - 1)
                    swap_if_greater(lo, hi)
                    swap_if_greater(hi - 1, hi)
                    return
                insertion(lo, hi)
                return
            if depth == 0:
                heapsort(lo, hi)
                return
            depth -= 1
            p = partition(lo, hi)
            intro(p + 1, hi, depth)
            hi = p - 1

    intro(0, n - 1, 2 * _floor_log2(capacity or n))


def list_capacity(n):
    """Capacity of a List<T> after n Adds (0, then 4, 8, 16...)."""
    c = 0
    while c < n:
        c = c * 2 if c else 4
    return c


class Terrain:
    def __init__(self, ident, path):
        self.id = ident
        self.path = path
        self.masks = []   # (mask[9] of 0/1/2, tiles[(x,y)], sprites[])
        self.padded = []
        self.center = []
        self.ignores = set()

    def ignore(self, c):
        if self.id != c:
            if c not in self.ignores:
                return '*' in self.ignores
            return True
        return False


def _read_into(t, element):
    for s in element:
        if s.tag != "set":
            continue
        mask = s.get("mask")
        tiles = [tuple(int(v) for v in p.split(",")) for p in s.get("tiles").split(";")]
        if mask == "center":
            t.center += tiles
        elif mask == "padding":
            t.padded += tiles
        else:
            m = [0 if c == "0" else 1 if c == "1" else 2 for c in mask if c in "01xX"]
            sprites = s.get("sprites").split(",") if s.get("sprites") else []
            t.masks.append((m, tiles, sprites))
    cap = list_capacity(len(t.masks))
    net_sort(t.masks, lambda a, b: a[0].count(2) - b[0].count(2), cap)


def read_tilesets(name):
    """{id char: Terrain} for ForegroundTiles or BackgroundTiles, in file order."""
    root = ET.parse(os.path.join(CELESTE, "Graphics", name + ".xml")).getroot()
    elements = {}
    out = {}
    for e in root.iter("Tileset"):
        c = e.get("id")
        t = Terrain(c, "tilesets/" + e.get("path"))
        _read_into(t, e)
        if e.get("copy"):
            _read_into(t, elements[e.get("copy")])
        if e.get("ignores"):
            for i in e.get("ignores").split(","):
                if i:
                    t.ignores.add(i[0])
        elements[c] = e
        out[c] = t
    return out


def is_empty(c):
    return c == "0" or c == "\0"


class Behaviour:
    def __init__(self, edges_extend=True, edges_ignore_out=False, padding_ignore_out=False):
        self.edges_extend = edges_extend
        self.edges_ignore_out = edges_ignore_out
        self.padding_ignore_out = padding_ignore_out


FG_BEHAVIOUR = Behaviour(True, False, True)
BG_BEHAVIOUR = Behaviour(True, False, True)


# ---------------------------------------------------------------- the game's own generation

class NetRandom:
    """.NET Framework System.Random."""
    MBIG = 2147483647

    def __init__(self, seed):
        sub = 2147483647 if seed == -2147483648 else abs(seed)
        mj = 161803398 - sub
        sa = [0] * 56
        sa[55] = mj
        mk = 1
        for i in range(1, 55):
            ii = (21 * i) % 55
            sa[ii] = mk
            mk = mj - mk
            if mk < 0:
                mk += self.MBIG
            mj = sa[ii]
        for _ in range(1, 5):
            for i in range(1, 56):
                sa[i] -= sa[1 + (i + 30) % 55]
                if sa[i] < 0:
                    sa[i] += self.MBIG
        self.sa = sa
        self.inext = 0
        self.inextp = 21

    def sample_int(self):
        a = self.inext + 1
        if a >= 56:
            a = 1
        b = self.inextp + 1
        if b >= 56:
            b = 1
        v = self.sa[a] - self.sa[b]
        if v == self.MBIG:
            v -= 1
        if v < 0:
            v += self.MBIG
        self.sa[a] = v
        self.inext, self.inextp = a, b
        return v

    def next(self, n):
        return int(self.sample_int() * (1.0 / self.MBIG) * n)


class VMap:
    """Monocle's VirtualMap: 50x50 segments, allocated by any write."""

    def __init__(self, w, h, empty):
        self.w, self.h, self.empty = w, h, empty
        self.data = [[empty] * w for _ in range(h)]
        self.seg = set()

    def __getitem__(self, xy):
        x, y = xy
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.data[y][x]
        return self.empty

    def __setitem__(self, xy, v):
        x, y = xy
        if 0 <= x < self.w and 0 <= y < self.h:
            self.seg.add((x // 50, y // 50))
            self.data[y][x] = v


def generate(sets, vmap, behaviour, level_bounds, rnd, anim_frames):
    """Autotiler.Generate over the whole map: {(x, y): (tile (tx, ty), overlay name or None, frame)}."""
    out = {}

    def check_tile(t, x, y):
        if x < 0 or y < 0 or x >= vmap.w or y >= vmap.h:
            if not behaviour.edges_extend:
                return False
            c = vmap[min(max(x, 0), vmap.w - 1), min(max(y, 0), vmap.h - 1)]
        else:
            c = vmap.data[y][x]
        return not is_empty(c) and not t.ignore(c)

    def same_level(x1, y1, x2, y2):
        for (lx, ly, lw, lh) in level_bounds:
            if lx <= x1 < lx + lw and ly <= y1 < ly + lh and lx <= x2 < lx + lw and ly <= y2 < ly + lh:
                return True
        return False

    def handler(x, y):
        c = vmap.data[y][x]
        if is_empty(c):
            return None
        t = sets[c]
        adj = []
        full = True
        for i in (-1, 0, 1):
            for j in (-1, 0, 1):
                f = check_tile(t, x + j, y + i)
                if not f and behaviour.edges_ignore_out and not same_level(x, y, x + j, y + i):
                    f = True
                adj.append(1 if f else 0)
                if not f:
                    full = False
        if full:
            if behaviour.padding_ignore_out:
                pad = any(not check_tile(t, x + dx, y + dy) and same_level(x, y, x + dx, y + dy)
                          for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2)))
            else:
                pad = any(not check_tile(t, x + dx, y + dy) for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2)))
            return (t.padded, []) if pad else (t.center, [])
        for m, tl, sprites in t.masks:
            if all(m[k] == 2 or m[k] == adj[k] for k in range(9)):
                return (tl, sprites)
        return None

    for i in range(0, vmap.w, 50):
        for j in range(0, vmap.h, 50):
            if (i // 50, j // 50) not in vmap.seg:
                continue
            for k in range(i, min(i + 50, vmap.w)):
                for l in range(j, min(j + 50, vmap.h)):
                    r = handler(k, l)
                    if r is None:
                        continue
                    tiles, sprites = r
                    tile = tiles[rnd.next(len(tiles))] if tiles else None
                    ov = None
                    if sprites:
                        ov = sprites[rnd.next(len(sprites))]
                        ov = (ov, rnd.next(anim_frames[ov]))
                    out[(k, l)] = (tile, ov, len(tiles))
    return out
