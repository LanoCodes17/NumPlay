"""Scenery textures for the calculator: each sprite at the size it is seen (a smaller copy for far instances, a
blurred half-size copy behind the background blur plane), 16 colors with alpha (or alpha alone for sprites only
ever drawn black), cut in 16x16 tiles (32x16 for alpha alone); only tiles that can ever be seen are kept, in blocks of up to 16 tiles
compressed with LZMA."""
import math, lzma, struct, os
import numpy as np
from PIL import Image, ImageFilter
import unity, scene, visible

TILE = 16
WIDE = 32   # alpha-only tiles are 32 texels wide (2 bits each: 128 bytes too)
BLOCK_TILES = int(os.environ.get("HK_BLOCK_TILES", "16"))
LEVEL_RATIO = 0.6       # an instance this much smaller than its texture gets a smaller copy
BLUR_SIGMA = 1.2        # background blur, calculator pixels (LightBlurredBackground)
BLUR_SCALE = float(os.environ.get("HK_BLUR_SCALE", "0.25"))   # texels a screen pixel behind the blur plane
FG_SCALE = float(os.environ.get("HK_FG_SCALE", "0.6"))       # ... in front of the gameplay plane (z < FG_Z)
FG_Z = -2.5
FIT_K = float(os.environ.get("HK_FIT_K", "0.8"))   # (all the rooms' scenery a little less fine, to fit the flash)
BLUR_PRE = False   # (the game blurs the background as a whole: the calculator does too)
LZMA_FILTERS = [{"id": lzma.FILTER_LZMA1, "lc": 0, "lp": 0, "pb": 0, "dict_size": 1 << 12, "preset": 9 | lzma.PRESET_EXTREME}]

FMT_PAL4 = 0     # 4 bits per texel, 15 colors + transparent
FMT_ALPHA2 = 1   # 2 bits per texel: alpha only (sprites always drawn black)
FMT_SOFT = 2     # smooth (glows, fog, soft masks): a few RGBA texels, drawn interpolated
FMT_SOFTA = 3    # the same, all one color: a few alpha texels and the color
SOFT_MAX = 2304  # texels
SOFT_ERR = 14    # the largest error (99th percentile, 0..255) a smaller copy may have
SOFT_RGBA_MAX = 400    # texels at most (4 bytes each once graded)
SOFT_MONO_MAX = 1600   # (1 byte each)


def lzma_raw(b):
    return lzma.compress(b, format=lzma.FORMAT_RAW, filters=LZMA_FILTERS)


class Variant:
    """A texture: one sprite at one scale, maybe blurred, maybe alpha only."""
    def __init__(self, sprite, scale, blur, alpha_only):
        self.sprite, self.scale, self.blur, self.alpha_only = sprite, scale, blur, alpha_only
        self.insts = []
        self.cells = set()      # visible 8-pixel cells of the sprite
        self.all_cells = False  # (scripted instances: everything)


def gaussian(arr, sigma):
    """Separable gaussian blur of a float array (H x W x C)."""
    r = int(math.ceil(sigma * 3))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    out = arr
    for axis in (0, 1):
        pad = [(0, 0)] * arr.ndim
        pad[axis] = (r, r)
        p = np.pad(out, pad, mode="constant")
        acc = np.zeros_like(out)
        for i, kv in enumerate(k):
            sl = [slice(None)] * arr.ndim
            sl[axis] = slice(i, i + out.shape[axis])
            acc += kv * p[tuple(sl)]
        out = acc
    return out


def kmeans_palette(px, k, iters=10, seed=0):
    """px: N x 4 float features. Returns centers (k x 4) and labels."""
    rng = np.random.default_rng(seed)
    n = len(px)
    samp = px[rng.choice(n, min(n, 20000), replace=False)] if n > 20000 else px
    uniq = np.unique(samp, axis=0)
    if len(uniq) <= k:
        c = uniq.astype(np.float32)
    else:
        # k-means++ init
        c = [samp[rng.integers(len(samp))]]
        d2 = ((samp - c[0]) ** 2).sum(1)
        for _ in range(1, k):
            p = d2 / d2.sum() if d2.sum() > 0 else None
            c.append(samp[rng.choice(len(samp), p=p)])
            d2 = np.minimum(d2, ((samp - c[-1]) ** 2).sum(1))
        c = np.array(c, np.float32)
        for _ in range(iters):
            lab = ((samp[:, None, :] - c[None]) ** 2).sum(-1).argmin(1)
            for j in range(len(c)):
                sel = samp[lab == j]
                if len(sel):
                    c[j] = sel.mean(0)
    labels = np.empty(n, np.int32)
    for i in range(0, n, 65536):
        labels[i:i + 65536] = ((px[i:i + 65536, None, :] - c[None]) ** 2).sum(-1).argmin(1)
    return c, labels


def _features(p):
    """RGBA texels (N x 4 float) -> what colors are told apart by: premultiplied color, and alpha weighted more"""
    al = p[:, 3:4] / 255.0
    return np.concatenate([p[:, :3] * al, p[:, 3:4] * 1.5], 1)


def joint_palette(arrs):
    """The palette (15 x RGBA, by brightness) several images share (an animation's frames)."""
    px = [a[a[..., 3] >= 8].astype(np.float32) for a in arrs]
    px = np.concatenate([p for p in px if len(p)] or [np.zeros((0, 4), np.float32)])
    pal = np.zeros((15, 4), np.uint8)
    if not len(px):
        return pal
    c, _ = kmeans_palette(_features(px), 15)
    ca = np.clip(c[:, 3] / 1.5, 1, 255)
    ca[ca >= 250] = 255
    rgb = np.clip(c[:, :3] / (ca[:, None] / 255.0), 0, 255)
    cols = np.concatenate([rgb, ca[:, None]], 1)
    cols = cols[np.argsort(cols[:, :3].sum(1) * cols[:, 3])]
    pal[:len(cols)] = np.round(cols).astype(np.uint8)
    return pal


def quantize_to(a, pal):
    """RGBA uint8 image, a palette -> indices (0 transparent, 1..15: the nearest of the palette's colors)."""
    mask = a[..., 3] >= 8
    idx = np.zeros(a.shape[:2], np.uint8)
    if mask.any():
        f = _features(a[mask].astype(np.float32))
        c = _features(pal.astype(np.float32))
        lab = np.empty(len(f), np.int32)
        for i in range(0, len(f), 65536):
            lab[i:i + 65536] = ((f[i:i + 65536, None, :] - c[None]) ** 2).sum(-1).argmin(1)
        idx[mask] = (lab + 1).astype(np.uint8)
    return idx


def quantize(a):
    """RGBA uint8 image -> (indices: 0 transparent, 1..15), palette 15 x RGBA (straight alpha)."""
    alpha = a[..., 3].astype(np.float32)
    mask = alpha >= 8
    idx = np.zeros(alpha.shape, np.uint8)
    pal = np.zeros((15, 4), np.uint8)
    if not mask.any():
        return idx, pal
    p = a[mask].astype(np.float32)
    al = p[:, 3:4] / 255.0
    feat = np.concatenate([p[:, :3] * al, p[:, 3:4] * 1.5], 1)
    c, lab = kmeans_palette(feat, 15)
    ca = np.clip(c[:, 3] / 1.5, 1, 255)
    ca[ca >= 250] = 255   # (nearly opaque colors are opaque: whole tiles of them hide what is behind)
    rgb = np.clip(c[:, :3] / (ca[:, None] / 255.0), 0, 255)
    cols = np.concatenate([rgb, ca[:, None]], 1)
    # palette sorted by brightness (helps compression)
    order = np.argsort(cols[:, :3].sum(1) * cols[:, 3])
    inv = np.empty_like(order)
    inv[order] = np.arange(len(order))
    pal[:len(order)] = np.round(cols[order]).astype(np.uint8)
    idx[mask] = (inv[lab] + 1).astype(np.uint8)
    return idx, pal


_black = {}


def black_sprite(s):
    """A sprite drawn black whatever its tint (masks, silhouettes): its alpha is all there is."""
    if s.key not in _black:
        a = np.asarray(unity.sprite_image(s))
        m = a[..., 3] > 8
        _black[s.key] = bool(m.any()) and int(a[m][:, :3].max()) <= 12
    return _black[s.key]


FIT = {}   # room -> texture scale factor (fit.py: so that every view's tiles fit the calculator's cache)


def build_variants(rooms, vis_of, owned=lambda r: ()):
    """Assign every instance a texture variant. vis_of(room) -> (weights, cells); owned(room) -> the (scene, object id)
    drawn by others (left out)."""
    variants = {}
    per_room = {}
    for r in rooms:
        inst = scene.instances(r)
        st = scene.settings(unity.scene(r))
        w, cells = vis_of(r)
        bz = st["blur_z"] if st["blur_z"] is not None else 1e9
        keep = []
        mine = set(owned(r))
        for it in inst:
            if (it.scene, it.obj["id"]) in mine:
                continue
            if it.solid:
                keep.append(it)
                continue
            if not it.dynamic and w[it.id] < 0.5:
                continue        # never seen
            sc = scene.display_scale(it)
            if sc <= 0:
                continue
            it.blur = it.z > bz and not it.dynamic
            it.alpha_only = it.color[:3] == (0, 0, 0) or black_sprite(it.sprite)
            it.want = min(1.0, sc) * (BLUR_SCALE if it.blur else FG_SCALE if it.z < FG_Z else 1.0)
            if not it.blur:
                it.want *= FIT.get(r, 1.0) * FIT_K
            it.cells = None if it.dynamic else cells.get(it.id)
            keep.append(it)
        per_room[r] = (keep, st)
        for it in keep:
            if it.solid:
                continue
            key = (it.sprite.key, it.blur, it.alpha_only)
            variants.setdefault(key, []).append(it)
    # scale levels per sprite: the largest wanted size, then a smaller copy for instances much smaller than that
    out = []
    for key, its in variants.items():
        its.sort(key=lambda i: -i.want)
        groups = []
        for it in its:
            if groups and it.want >= groups[-1][0] * LEVEL_RATIO:
                groups[-1][1].append(it)
            else:
                groups.append((it.want, [it]))
        for scale, g in groups:
            v = Variant(g[0].sprite, scale, key[1], key[2])
            for it in g:
                v.insts.append(it)
                it.variant = v
                if it.cells is None or v.blur:
                    v.all_cells = True   # (scripted, or blurred: its soft edges show what is near the parts seen)
                else:
                    v.cells.update(it.cells.tolist())
            out.append(v)
    return out, per_room


class Job:
    """What a worker needs to make a variant's texture (picklable)."""
    def __init__(self, v):
        self.key, self.scale, self.blur, self.alpha_only = v.sprite.key, v.scale, v.blur, v.alpha_only
        self.all_cells = v.all_cells
        self.cells = sorted(v.cells)
        self.magnified = min(scene.display_scale(i) for i in v.insts) >= 1.5


_SRC_HASH = None


def encode_job(job):
    """encode_variant, remembered in cache/tex (by the job and this file)."""
    import hashlib, pickle
    global _SRC_HASH
    if _SRC_HASH is None:
        _SRC_HASH = hashlib.sha1(open(__file__, "rb").read()).hexdigest()
    if isinstance(job, ImageJob):
        key = hashlib.sha1(_SRC_HASH.encode() + job.arr.tobytes() + repr(job.arr.shape).encode() +
                           (job.pal.tobytes() if getattr(job, "pal", None) is not None else b"")).hexdigest()
    else:
        # (a sprite packed tight: where its pixels are in its rect too)
        tight = unity.sprite_by_key(job.key).tight
        key = hashlib.sha1(repr((_SRC_HASH, job.key, job.scale, job.blur, job.alpha_only, job.all_cells, job.cells,
                                 job.magnified) + ((tight,) if tight else ())).encode()).hexdigest()
    path = os.path.join(unity.CACHE, "tex", key + ".pkl")
    if os.path.exists(path):
        with open(path, "rb") as f:
            return pickle.load(f)
    if not isinstance(job, ImageJob):
        job.sprite = unity.sprite_by_key(job.key)
    t = encode_variant(job)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = "%s.%d.tmp" % (path, os.getpid())
    with open(tmp, "wb") as f:
        pickle.dump(t, f)
    os.replace(tmp, path)
    return t


def render_variant(v):
    """The variant's texels as RGBA (resized, blurred) and its visible-tile mask."""
    s = v.sprite
    img = unity.sprite_image(s)
    w = max(1, round(s.w * v.scale))
    h = max(1, round(s.h * v.scale))
    if v.blur and BLUR_PRE:
        # blur at the screen scale, then down
        w2, h2 = max(1, round(s.w * v.scale / BLUR_SCALE)), max(1, round(s.h * v.scale / BLUR_SCALE))
        big = img.resize((w2, h2), Image.BOX)
        pad = int(BLUR_SIGMA * 3) + 1
        canvas = Image.new("RGBA", (w2 + 2 * pad, h2 + 2 * pad))
        canvas.paste(big, (pad, pad))
        prem = np.asarray(canvas, np.float32)
        prem[..., :3] *= prem[..., 3:4] / 255.0
        arr = gaussian(prem, BLUR_SIGMA)[pad:pad + h2, pad:pad + w2]
        a = np.maximum(arr[..., 3:4], 1e-3)
        arr[..., :3] = arr[..., :3] / (a / 255.0)
        img2 = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
        im = img2.resize((w, h), Image.BOX)
    else:
        im = img.resize((w, h), Image.BOX) if (w, h) != img.size else img
    arr = np.asarray(im).copy()
    TW = WIDE if v.alpha_only else TILE
    tw, th = (w + TW - 1) // TW, (h + TILE - 1) // TILE
    if v.all_cells:
        tmask = np.ones((th, tw), bool)
    else:
        # visible cells (8 sprite pixels), grown by one, in texels
        tmask = np.zeros((th, tw), bool)
        k = v.scale * visible.CELL
        for c in v.cells:
            cu, cv = c % 4096, c // 4096
            x0, x1 = (cu - 1) * k, (cu + 2) * k
            y0, y1 = (cv - 1) * k, (cv + 2) * k
            tx0, tx1 = max(0, int(x0 // TW)), min(tw - 1, int((x1 - 1e-6) // TW))
            ty0, ty1 = max(0, int(y0 // TILE)), min(th - 1, int((y1 - 1e-6) // TILE))
            tmask[ty0:ty1 + 1, tx0:tx1 + 1] = True
    return arr, tmask


class Texture:
    pass


def soft_factor(arr, magnified):
    """How much smaller a smooth texture can be stored (1: not smooth)."""
    h, w = arr.shape[:2]
    a = arr.astype(np.float32)
    a[..., :3] *= a[..., 3:4] / 255
    best = 1
    for f in (2, 4, 8, 16):
        sw, sh = round(w / f), round(h / f)
        if sw < 3 or sh < 3:
            break
        small = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize((sw, sh), Image.BOX)
        back = np.asarray(small.resize((w, h), Image.BILINEAR), np.float32)
        err = np.percentile(np.abs(back - a), 99)
        if err <= SOFT_ERR or (magnified and f == 2 and err <= 2 * SOFT_ERR):
            best = f
        else:
            break
    if best > 1 and round(w / best) * round(h / best) > SOFT_MAX:
        return 1
    return best


class ImageJob:
    """A texture made from an image already at its size (actors' sprite frames): all of it kept."""
    def __init__(self, arr, name, pal=None):
        self.arr, self.name = arr, name
        self.pal = pal   # (a palette shared with others: its clip's)
        self.alpha_only = False
        self.blur = False
        self.magnified = False


def encode_variant(v):
    """-> Texture with header fields and its tiles (or its few texels, if smooth)."""
    if isinstance(v, ImageJob):
        arr = v.arr
        tmask = np.ones(((arr.shape[0] + TILE - 1) // TILE, (arr.shape[1] + TILE - 1) // TILE), bool)
    else:
        arr, tmask = render_variant(v)
    h, w = arr.shape[:2]
    if True:
        f = soft_factor(arr, getattr(v, "magnified", False))
        if f > 1:
            t = Texture()
            t.base = 0
            mono = False
            m = arr[..., 3] > 16
            if m.any():
                rgb = arr[m][:, :3].astype(np.float32)
                base = np.median(rgb, 0)
                if np.percentile(np.abs(rgb - base).max(1), 95) <= 24:
                    mono = True
                    b = np.clip(np.round(base), 0, 255).astype(int)
                    t.base = int(b[0]) | int(b[1]) << 8 | int(b[2]) << 16
            # the game keeps a frame's smooth textures together (gfx.c: SOFT_POOL): big ones get coarser
            cap = SOFT_MONO_MAX if mono else SOFT_RGBA_MAX
            while round(w / f) * round(h / f) > cap and f < 64:
                f *= 2
            sw, sh = max(2, round(w / f)), max(2, round(h / f))
            a = arr.astype(np.float32)
            a[..., :3] *= a[..., 3:4] / 255
            small = np.asarray(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize((sw, sh), Image.BOX))
            t.w, t.h, t.tw, t.th = sw, sh, 0, 0
            t.fmt = FMT_SOFTA if mono else FMT_SOFT
            t.soft = small[..., 3].tobytes() if mono else small.tobytes()   # (premultiplied RGBA)
            t.pal, t.tiles, t.ntiles, t.bits = None, [], 0, 4
            t.scale_f = f
            return t
    tw, th = tmask.shape[1], tmask.shape[0]
    t = Texture()
    t.w, t.h, t.tw, t.th = w, h, tw, th
    t.fmt = FMT_ALPHA2 if v.alpha_only else FMT_PAL4
    if v.alpha_only:
        idx = np.clip(np.round(arr[..., 3] / 85.0), 0, 3).astype(np.uint8)
        t.pal = None
    elif getattr(v, "pal", None) is not None:
        idx, t.pal = quantize_to(arr, v.pal), v.pal
    else:
        idx, t.pal = quantize(arr)
    TW = WIDE if v.alpha_only else TILE
    pad = np.zeros((th * TILE, tw * TW), np.uint8)
    pad[:h, :w] = idx
    tiles = []
    t.empty = np.zeros((th, tw), bool)
    t.opaque = np.zeros((th, tw), bool)
    opaque_ids = None
    if not v.alpha_only:
        opaque_ids = set(i + 1 for i in range(15) if t.pal[i][3] >= 250)
    for ty in range(th):
        for tx in range(tw):
            blk = pad[ty * TILE:(ty + 1) * TILE, tx * TW:(tx + 1) * TW]
            if not tmask[ty, tx] or not blk.any():
                t.empty[ty, tx] = True
                continue
            full = (tx + 1) * TW <= w and (ty + 1) * TILE <= h
            if v.alpha_only:
                t.opaque[ty, tx] = full and (blk == 3).all()
            else:
                t.opaque[ty, tx] = full and np.isin(blk, list(opaque_ids)).all() if opaque_ids else False
            tiles.append(blk.astype(np.uint8).tobytes())
    t.tiles = tiles
    t.bits = 2 if v.alpha_only else 4
    t.ntiles = len(tiles)
    return t
