"""A scene's scenery as the game draws it: sprite instances with their world quads, draw order, blend, tint and
lighting; the scene's settings (size, color grading, ambient light, background blur plane); and the camera's view."""
import math, functools
import numpy as np
import unity

# sorting layers in render order (TagManager)
LAYER_ORDER = [0, 3315419377, 1459018367, 4015848369, 2917268371, 1270309357, 3557629463, 3868594333, 3784110789,
               31172181, 1371964999, 2577183099, 1038907033, 3945752401, 629535577, 957720295]
LAYER_NAMES = ["Default", "Far BG 2", "Far BG 1", "Mid BG", "Immediate BG", "Actors", "Player", "Tiles",
               "MID Dressing", "Immediate FG", "Scene Border", "Far FG", "Vignette", "Over", "HUD", "Inventory"]


def layer_index(uid):
    uid &= 0xffffffff
    return LAYER_ORDER.index(uid) if uid in LAYER_ORDER else 0


# the camera (GameCameras: perspective, field of view 24 at 16:9; ForceCameraAspect keeps 16:10 on the calculator)
CAMZ = -38.1
FOV_V = 24.0 * (16 / 9) / 1.6
VIEW_W, VIEW_H = 320, 200
FOCAL = (VIEW_H / 2) / math.tan(math.radians(FOV_V / 2))
PXU = FOCAL / -CAMZ                 # pixels per unit at z = 0
CAM_XMIN, CAM_YMIN = 14.6, 8.3      # CameraController limits

SOLIDS = ("black_solid", "white_solid")


def blend_of(shader):
    s = shader.lower()
    if "screen" in s:
        return "screen"
    if "linearlight" in s:
        return "linearlight"
    if "lineardodge" in s or "additive" in s:
        return "add"
    if "overlay" in s:
        return "overlay"
    if "multiply" in s:
        return "multiply"
    return "alpha"


def dynamic_ids(d):
    """Objects run by scripts (FSMs, breakables, grass...), animated or physical, with everything under them."""
    ob = unity.by_id(d)
    dyn = {}

    def is_dyn(o):
        i = o["id"]
        if i in dyn:
            return dyn[i]
        r = any(c["type"] in ("MonoBehaviour", "Animator", "Rigidbody2D", "Animation") for c in o["c"])
        if not r and o["parent"]:
            r = is_dyn(ob[o["parent"]])
        dyn[i] = r
        return r
    for o in d["objects"]:
        is_dyn(o)
    return dyn


def settings(d):
    st = {"saturation": 1.0, "curves": None, "ambient": (1.0, 1.0, 1.0), "blur_z": None, "size": None,
          "hero_light": (1, 1, 1, 0.5), "darkness": 0}
    for o in d["objects"]:
        for c in o["c"]:
            cl = c.get("class")
            v = c.get("v") or {}
            if cl == "SceneManager" and o["active"]:
                st["saturation"] = v["saturation"] + (0 if v.get("ignorePlatformSaturationModifiers") else 0.17)
                st["curves"] = [v["redChannel"]["m_Curve"], v["greenChannel"]["m_Curve"], v["blueChannel"]["m_Curve"]]
                dc = v["defaultColor"]
                k = 1 + (v["defaultIntensity"] - 1) * 0.5        # SceneManager.SetLighting (AmbientIntesityMix 0.5)
                st["ambient"] = (dc["r"] * k, dc["g"] * k, dc["b"] * k)
                h = v["heroLightColor"]
                st["hero_light"] = (h["r"], h["g"], h["b"], h["a"])
                st["darkness"] = v.get("darknessLevel", 0)
                st["map_zone"] = v.get("mapZone", 0)
                st["manager"] = v
            elif cl == "BlurPlane" and o["active"]:
                z = o["pos"][2]
                if st["blur_z"] is None or z < st["blur_z"]:
                    st["blur_z"] = z
            elif cl == "tk2dTileMap":
                st["size"] = (v["width"], v["height"])
    return st


def hermite(keys, t):
    """Unity AnimationCurve.Evaluate (hermite between keys, clamped outside)."""
    if not keys:
        return t
    if t <= keys[0]["time"]:
        return keys[0]["value"]
    if t >= keys[-1]["time"]:
        return keys[-1]["value"]
    for a, b in zip(keys, keys[1:]):
        if a["time"] <= t <= b["time"]:
            dt = b["time"] - a["time"]
            if dt <= 0:
                return b["value"]
            u = (t - a["time"]) / dt
            m0, m1 = a["outSlope"] * dt, b["inSlope"] * dt
            if math.isinf(m0) or math.isinf(m1):
                return a["value"]
            u2, u3 = u * u, u * u * u
            return (2 * u3 - 3 * u2 + 1) * a["value"] + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * b["value"] + (u3 - u2) * m1
    return t


def grading_luts(st, n=256):
    """ColorCorrectionCurves: a curve per channel, then saturation."""
    if st["curves"] is None:
        return [np.arange(n) / (n - 1)] * 3, st["saturation"]
    return [np.array([min(1.0, max(0.0, hermite(k, i / (n - 1)))) for i in range(n)]) for k in st["curves"]], st["saturation"]


def grade(rgb, st):
    luts, sat = grading_luts(st)
    idx = np.clip(np.round(rgb * 255), 0, 255).astype(np.int32)
    out = np.stack([luts[c][idx[..., c]] for c in range(3)], -1)
    lum = (out * np.array([0.2126, 0.7152, 0.0722])).sum(-1, keepdims=True)
    return np.clip(lum + (out - lum) * sat, 0, 1)


class Inst:
    pass


@functools.lru_cache(maxsize=None)
def additive(name):
    """The scenes a scene loads with it (SceneAdditiveLoadConditional): [(scene, PlayerData bool, value, which)]: the
    first (which 1) loaded when the bool has that value, the alternative (which 2) otherwise."""
    out = []
    for o in unity.scene(name)["objects"]:
        for c in o["c"]:
            v = c.get("v") or {}
            if c.get("class") == "SceneAdditiveLoadConditional" and o["active"]:
                flag, val = v.get("needsPlayerDataBool", ""), bool(v.get("playerDataBoolValue"))
                if v.get("sceneNameToLoad"):
                    out.append((v["sceneNameToLoad"], flag, val, 1))
                if v.get("altSceneNameToLoad"):
                    out.append((v["altSceneNameToLoad"], flag, val, 2))
    return out


def additive_scenes(rooms):
    return {a[0] for r in rooms for a in additive(r)}


@functools.lru_cache(maxsize=None)
def instances(name):
    """The scene's SpriteRenderers, active and enabled, in a fixed draw order (sorting layer, order, depth); with those
    of the scenes it loads with it (tagged: it.scene, it.which)."""
    out = _instances(name)
    for k, (an, _, _, which) in enumerate(additive(name)):
        for it in _instances(an):
            it.scene, it.which = an, which
            out.append(it)
    if additive(name):
        out.sort(key=lambda i: (i.layer, i.order, -i.z))
        for k, it in enumerate(out):
            it.id = k
    return out


def _instances(name):
    d = unity.scene(name)
    level, exts = d["level"], tuple(d["externals"])
    dyn = dynamic_ids(d)
    out = []
    for o in d["objects"]:
        if not o["active"]:
            continue
        for c in o["c"]:
            if c["type"] != "SpriteRenderer":
                continue
            v = c["v"]
            if not v.get("m_Enabled", 1) or not v.get("m_Sprite"):
                continue
            s = unity.sprite(level, exts, *v["m_Sprite"])
            if s is None:
                continue
            mats = v.get("m_Materials") or []
            mat = unity.material(unity.ref_path(d, mats[0][0]), mats[0][1]) if mats and mats[0] else ("?", "?")
            if mat[0] == "Sprites-Darkness-Cutout":
                continue        # the ultrawide cutouts by the gates: only for screens wider than 16:9
            col = v["m_Color"]
            it = Inst()
            it.scene, it.which = name, 0
            it.obj, it.sprite, it.mat = o, s, mat
            it.pos, it.m3 = o["pos"], o["m3"]
            it.color = (col["r"], col["g"], col["b"], col["a"])
            cf = next((x.get("v") for x in o["c"] if x.get("class") == "ColorFader" and x.get("v")), None)
            if cf:
                # (a ColorFader's Start: its down color, until a script fades it up)
                dc = cf.get("downColour") or {}
                it.color = tuple(a * dc.get(k, 1) for a, k in zip(it.color, "rgba"))
                if it.color[3] <= 0:
                    continue
            it.layer = layer_index(v["m_SortingLayerID"])
            it.order = v["m_SortingOrder"]
            it.flip = (v.get("m_FlipX", 0), v.get("m_FlipY", 0))
            it.blend = blend_of(mat[1])
            it.lit = "Lit" in mat[1] or "Grass" in mat[1] or "Diffuse" in mat[0]
            it.grass = "Grass" in mat[1]
            it.draw_mode = v.get("m_DrawMode", 0)
            it.size = (v.get("m_Size", {}).get("x", 0), v.get("m_Size", {}).get("y", 0))
            it.dynamic = dyn[o["id"]]
            # (a turned one is drawn as a texture: the solid fill is a box)
            it.solid = s.name in SOLIDS and abs(it.m3[1]) < 1e-3 and abs(it.m3[3]) < 1e-3
            it.quad = world_quad(it)
            it.center = sum(it.quad) / 4
            it.z = float(it.center[2])
            out.append(it)
    # the game sorts by distance from the camera within a layer and order: far first, a fixed order here
    out.sort(key=lambda i: (i.layer, i.order, -i.z))
    for k, it in enumerate(out):
        it.id = k
    return out


def local_quad(it):
    s = it.sprite
    w, h = s.w / s.ppu, s.h / s.ppu
    if it.draw_mode and it.size[0]:
        w, h = it.size
    x0, y0 = -s.px * w, -s.py * h
    fx = -1 if it.flip[0] else 1
    fy = -1 if it.flip[1] else 1
    # bottom-left, bottom-right, top-right, top-left
    return [(x0 * fx, y0 * fy), ((x0 + w) * fx, y0 * fy), ((x0 + w) * fx, (y0 + h) * fy), (x0 * fx, (y0 + h) * fy)]


def world_quad(it):
    m = np.array(it.m3).reshape(3, 3)
    p = np.array(it.pos)
    return [m @ np.array([x, y, 0.0]) + p for x, y in local_quad(it)]


def project(pt, cx, cy, scale=1):
    dz = pt[2] - CAMZ
    if dz <= 0.05:
        return None
    k = FOCAL * scale / dz
    return (VIEW_W * scale / 2 + (pt[0] - cx) * k, VIEW_H * scale / 2 - (pt[1] - cy) * k)


def camera_range(st):
    w, h = st["size"]
    return (CAM_XMIN, max(CAM_XMIN, w - CAM_XMIN), CAM_YMIN, max(CAM_YMIN, h - CAM_YMIN))


def display_scale(it):
    """Screen pixels per sprite pixel (the larger axis) at the instance's depth."""
    m = it.m3
    sx, sy = math.hypot(m[0], m[3]), math.hypot(m[1], m[4])
    dz = it.z - CAMZ
    if dz < 0.5:
        return 0
    s = it.sprite
    if it.draw_mode and it.size[0]:
        sx *= it.size[0] / (s.w / s.ppu)
        sy *= it.size[1] / (s.h / s.ppu)
    return max(sx, sy) * FOCAL / dz / s.ppu
