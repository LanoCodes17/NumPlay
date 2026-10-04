"""Which scenery can ever be seen: the scene drawn from camera positions all over its range, at the calculator's
resolution, front to back. Every sprite instance gets the parts of its sprite that show (cells of 8 sprite pixels);
instances that never show are dropped. Only static scenery hides what is behind it (scripted objects can move or
fade). Results are cached in tools/cache/NAME.vis.npz."""
import os, sys, math, multiprocessing as mp
import numpy as np
from PIL import Image
import unity, scene

CELL = 8
STEP = 2.0          # camera grid (units)
ALPHA_MIN = 0.02    # a pixel shows an instance when its weight (alpha x what is left in front) is above this

_sc = None


def _load(name):
    global _sc
    if _sc is None or _sc[0] != name:
        inst = scene.instances(name)
        st = scene.settings(unity.scene(name))
        alphas = {}
        _sc = (name, inst, st, alphas)
    return _sc


def alpha_of(it, alphas):
    s = it.sprite
    a = alphas.get(s.key)
    if a is None:
        a = unity.sprite_image(s).getchannel("A")
        alphas[s.key] = a
    return a


def view(args):
    name, cx, cy = args
    _, inst, st, alphas = _load(name)
    W, H = scene.VIEW_W, scene.VIEW_H
    T = np.ones((H, W), np.float32)
    seen = []
    lst = []
    for it in inst:
        q = [scene.project(p, cx, cy) for p in it.quad]
        if any(p is None for p in q):
            continue
        xs = [p[0] for p in q]
        ys = [p[1] for p in q]
        if max(xs) < 0 or min(xs) > W or max(ys) < 0 or min(ys) > H:
            continue
        lst.append((it, q))
    for it, q in reversed(lst):
        a_img = alpha_of(it, alphas)
        w, h = a_img.size
        tl, tr, bl = np.array(q[3]), np.array(q[2]), np.array(q[0])
        A = np.array([[(tr - tl)[0] / w, (bl - tl)[0] / h, tl[0]], [(tr - tl)[1] / w, (bl - tl)[1] / h, tl[1]], [0, 0, 1]])
        try:
            Ai = np.linalg.inv(A)
        except np.linalg.LinAlgError:
            continue
        xs = [p[0] for p in q]
        ys = [p[1] for p in q]
        x0, y0 = max(0, int(math.floor(min(xs)))), max(0, int(math.floor(min(ys))))
        x1, y1 = min(W, int(math.ceil(max(xs)))), min(H, int(math.ceil(max(ys))))
        if x1 <= x0 or y1 <= y0:
            continue
        Tm = Ai @ np.array([[1, 0, x0], [0, 1, y0], [0, 0, 1]])
        a = np.asarray(a_img.transform((x1 - x0, y1 - y0), Image.AFFINE, tuple(Tm[:2].flatten()), resample=Image.BILINEAR),
                       dtype=np.float32) / 255.0 * it.color[3]
        t = T[y0:y1, x0:x1]
        wgt = a * t
        vis = wgt > ALPHA_MIN
        if vis.any():
            ys_, xs_ = np.nonzero(vis)
            px, py = xs_ + x0 + 0.5, ys_ + y0 + 0.5
            u = Ai[0, 0] * px + Ai[0, 1] * py + Ai[0, 2]
            v = Ai[1, 0] * px + Ai[1, 1] * py + Ai[1, 2]
            cu = np.clip((u // CELL).astype(np.int32), 0, (w - 1) // CELL)
            cv = np.clip((v // CELL).astype(np.int32), 0, (h - 1) // CELL)
            seen.append((it.id, float(wgt.sum()), np.unique(cv * 4096 + cu)))
        if not it.dynamic and it.blend == "alpha":
            t *= (1 - a)
    return seen


VERSION = 2   # (bumped when what a view sees changes, beyond the instances themselves)


def signature(inst):
    """the instances the results are for, in order (the cells are each one's): stale results are made again"""
    import zlib
    return np.array([zlib.crc32(repr((VERSION, it.obj["id"], it.sprite.key, round(it.sprite.w), round(it.sprite.h),
                                     getattr(it.sprite, "tight", None), round(it.color[3], 4))).encode())
                     for it in inst], np.uint32)


def compute(name):
    p = os.path.join(unity.CACHE, name + ".vis.npz")
    inst = scene.instances(name)
    sig = signature(inst)
    if os.path.exists(p):
        z = np.load(p, allow_pickle=True)
        if "sig" in z.files and np.array_equal(z["sig"], sig):
            return z["weight"], z["cells"].item()
    st = scene.settings(unity.scene(name))
    if st["size"] is None:
        n = len(inst)
        return np.ones(n), {}
    x0, x1, y0, y1 = scene.camera_range(st)
    xs = np.arange(x0, x1 + 1e-6, STEP) if x1 > x0 else [x0]
    ys = np.arange(y0, y1 + 1e-6, STEP) if y1 > y0 else [y0]
    xs = sorted(set(list(xs) + [x1]))
    ys = sorted(set(list(ys) + [y1]))
    jobs = [(name, float(cx), float(cy)) for cx in xs for cy in ys]
    weight = np.zeros(len(inst))
    cells = {}
    with mp.get_context("spawn").Pool(min(4, os.cpu_count() or 1)) as pool:
        for seen in pool.imap_unordered(view, jobs, chunksize=4):
            for i, wsum, c in seen:
                weight[i] = max(weight[i], wsum)
                cells.setdefault(i, set()).update(c.tolist())
    cells = {i: np.array(sorted(c), np.int32) for i, c in cells.items()}
    np.savez_compressed(p, weight=weight, cells=np.array(cells, dtype=object), sig=sig)
    print("visible", name, len(jobs), "views:", int((weight >= 0.5).sum()), "of", len(inst), "instances show", flush=True)
    return weight, cells


if __name__ == "__main__":
    for n in sys.argv[1:]:
        compute(n)
