"""2D Toolkit sprites and animations (the Knight, enemies, effects, HUD): collections, their sprite definitions
cut from the atlases, and animation clips."""
import functools, math
import numpy as np
from PIL import Image
import unity, typetree


def _parse(f, pid):
    o = f.objects[pid]
    with unity.in_data():
        mb = o.read(check_read=False)
        sc = mb.m_Script.read()
        v, _, _ = typetree.parse(o, sc)
    return sc.m_ClassName, v


@functools.lru_cache(maxsize=None)
def index(path):
    """Collections and animations in a file: {'collections': {name: pid}, 'animations': [(pid, clip names)]}."""
    f = unity.asset_file(path)
    cols, anims = {}, []
    if f is None:
        return {"collections": cols, "animations": anims}
    with unity.in_data():
        for pid, o in f.objects.items():
            if o.type.name != "MonoBehaviour":
                continue
            try:
                mb = o.read(check_read=False)
                sc = mb.m_Script.read()
            except Exception:
                continue
            if sc.m_ClassName == "tk2dSpriteCollectionData":
                v, _, _ = typetree.parse(o, sc)
                cols[unity.S(v["spriteCollectionName"])] = pid
            elif sc.m_ClassName == "tk2dSpriteAnimation":
                v, _, _ = typetree.parse(o, sc)
                anims.append((pid, [unity.S(c["name"]) for c in v["clips"]]))
    return {"collections": cols, "animations": anims}


@functools.lru_cache(maxsize=None)
def collection(path, pid):
    _, v = _parse(unity.asset_file(path), pid)
    return v


@functools.lru_cache(maxsize=None)
def animation(path, pid):
    _, v = _parse(unity.asset_file(path), pid)
    return v


def ref_file(path, ref):
    """A PPtr inside `path` -> (file, path id)."""
    fid, pid = ref["m_FileID"], ref["m_PathID"]
    if fid == 0:
        return path, pid
    f = unity.asset_file(path)
    return f.externals[fid - 1].path, pid


@functools.lru_cache(maxsize=64)
def atlas(path, pid):
    f = unity.asset_file(path)
    with unity.in_data():
        return f.objects[pid].read().image.convert("RGBA")


def material_texture(path, mref):
    mp, mpid = ref_file(path, mref)
    f = unity.asset_file(mp)
    with unity.in_data():
        m = f.objects[mpid].read_typetree()
    for t in m["m_SavedProperties"]["m_TexEnvs"]:
        name = t[0] if isinstance(t, (list, tuple)) else t.get("first")
        tex = t[1] if isinstance(t, (list, tuple)) else t.get("second")
        if name == "_MainTex":
            return ref_file(mp, tex["m_Texture"])
    return None


def sprite_image(path, col, sid):
    """A sprite definition: (RGBA image, local x of its left edge, local y of its top edge, units per pixel)."""
    d = col["spriteDefinitions"][sid]
    mats = col["materials"]
    mref = mats[d["materialId"]] if mats else d["material"]
    tp = material_texture(path, mref)
    img = atlas(*tp)
    W, H = img.size
    uv = d["uvs"]
    us = [p["x"] for p in uv]
    vs = [p["y"] for p in uv]
    x0, x1 = round(min(us) * W), round(max(us) * W)
    y0, y1 = round((1 - max(vs)) * H), round((1 - min(vs)) * H)
    crop = img.crop((x0, y0, x1, y1))
    if d["flipped"]:
        # tk2d stores some sprites turned in the atlas: their x along its v, their y along its u
        crop = crop.transpose(Image.Transpose.TRANSVERSE)
    pos = d["positions"]
    px = [p["x"] for p in pos]
    py = [p["y"] for p in pos]
    lx, rx, by, ty = min(px), max(px), min(py), max(py)
    upp = (rx - lx) / max(1, crop.width)
    return crop, lx, ty, upp, (rx - lx, ty - by)
