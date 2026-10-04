"""Reference picture of a scene at the calculator's view, at full quality: the game's sprites in draw order with
lighting, blend modes, the background blur and the color grading. For checking the calculator's drawing.
python3 ref.py SCENE CX CY OUT.png [scale]"""
import sys, math
import numpy as np
from PIL import Image
import unity, scene, art


def layer(it, q, W, H, ambient):
    img = unity.sprite_image(it.sprite)
    w, h = img.size
    tl, tr, bl = np.array(q[3]), np.array(q[2]), np.array(q[0])
    A = np.array([[(tr - tl)[0] / w, (bl - tl)[0] / h, tl[0]], [(tr - tl)[1] / w, (bl - tl)[1] / h, tl[1]], [0, 0, 1]])
    try:
        Ai = np.linalg.inv(A)
    except np.linalg.LinAlgError:
        return None
    xs, ys = [p[0] for p in q], [p[1] for p in q]
    x0, y0 = max(0, int(math.floor(min(xs)))), max(0, int(math.floor(min(ys))))
    x1, y1 = min(W, int(math.ceil(max(xs)))), min(H, int(math.ceil(max(ys))))
    if x1 <= x0 or y1 <= y0:
        return None
    T = Ai @ np.array([[1, 0, x0], [0, 1, y0], [0, 0, 1]])
    out = np.asarray(img.transform((x1 - x0, y1 - y0), Image.AFFINE, tuple(T[:2].flatten()), resample=Image.BILINEAR), np.float32) / 255
    col = np.array(it.color, np.float32)
    if it.solid and it.sprite.name == "black_solid":
        col[:3] = 0
    rgb = out[..., :3] * col[:3]
    if it.lit:
        rgb = rgb * np.array(ambient, np.float32)
    return x0, y0, rgb, out[..., 3] * col[3]


def composite(buf, x0, y0, rgb, a, blend):
    h, w = a.shape
    d = buf[y0:y0 + h, x0:x0 + w]
    a = a[..., None]
    if blend == "add":
        d[:] = np.minimum(1, d + rgb * a)
    elif blend == "screen":
        d[:] = 1 - (1 - d) * (1 - rgb * a)
    elif blend == "linearlight":
        d[:] = np.clip(d + (2 * rgb - 1) * a, 0, 1)
    else:
        d[:] = d * (1 - a) + rgb * a


def render(name, cx, cy, scale=1):
    inst = scene.instances(name)
    st = scene.settings(unity.scene(name))
    W, H = scene.VIEW_W * scale, scene.VIEW_H * scale
    bz = st["blur_z"]
    bg = np.zeros((H, W, 3), np.float32)
    front = []
    for it in inst:
        q = [scene.project(p, cx, cy, scale) for p in it.quad]
        if any(p is None for p in q):
            continue
        r = layer(it, q, W, H, st["ambient"])
        if r is None:
            continue
        if bz is not None and it.z > bz and not it.dynamic:
            composite(bg, *r, it.blend)
        else:
            front.append((it, r))
    if bz is not None:
        bg = art.gaussian(bg, art.BLUR_SIGMA * scale)
    for it, r in front:
        composite(bg, *r, it.blend)
    return scene.grade(bg, st)


if __name__ == "__main__":
    name, cx, cy, out = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    scale = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    Image.fromarray((render(name, cx, cy, scale) * 255).astype(np.uint8)).save(out)
