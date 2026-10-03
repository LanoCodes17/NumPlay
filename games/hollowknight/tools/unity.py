"""Reading the game's Unity files: scenes (GameObjects with world transforms and components), sprites, textures,
and MonoBehaviour fields through type trees made from the game's own assemblies.
The game's Data folder comes from HOLLOWKNIGHT (default: the owner's copy)."""
import os, sys, json, math, functools
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import UnityPy
import typetree, fsm as fsmdec

DATA = os.environ.get("HOLLOWKNIGHT", "/root/numplay-work/gamesrc/hk/Hollow Knight/hollow_knight.app/Contents/Resources/Data")
CACHE = os.path.join(HERE, "cache")
typetree.MANAGED = os.path.join(DATA, "Managed")
S = fsmdec.S
rnd = fsmdec.rnd

_loaded = {}


class in_data:
    """Unity resolves a file's references from the working directory: work in the game's Data folder."""
    def __enter__(self):
        self.old = os.getcwd()
        os.chdir(DATA)

    def __exit__(self, *a):
        os.chdir(self.old)


def asset_file(path):
    """A serialized file of the game by name, loaded once."""
    f = _loaded.get(path)
    if f is None:
        with in_data():
            e = UnityPy.load(path)
        f = e.files.get(path)
        _loaded[path] = f
    return f


@functools.lru_cache(maxsize=None)
def scene_index():
    """Scene name -> level number (BuildSettings)."""
    gg = asset_file("globalgamemanagers")
    for o in gg.objects.values():
        if o.type.name == "BuildSettings":
            d = o.read_typetree()
            return {os.path.basename(p).replace(".unity", ""): i for i, p in enumerate(d["scenes"])}
    raise RuntimeError("no BuildSettings")


def quat_mat(q):
    x, y, z, w = q["x"], q["y"], q["z"], q["w"]
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def local_mat(t):
    m = np.eye(4)
    s = t["m_LocalScale"]
    m[:3, :3] = quat_mat(t["m_LocalRotation"]) @ np.diag([s["x"], s["y"], s["z"]])
    p = t["m_LocalPosition"]
    m[:3, 3] = [p["x"], p["y"], p["z"]]
    return m


def clean(x):
    if isinstance(x, (bytes, bytearray)):
        try:
            s = x.decode("utf8")
            if all(c.isprintable() or c in "\n\t\r" for c in s):
                return s
        except Exception:
            pass
        return "bytes%d" % len(x)
    if isinstance(x, float):
        return rnd(x)
    if isinstance(x, dict):
        if set(x.keys()) == {"m_FileID", "m_PathID"}:
            return [x["m_FileID"], x["m_PathID"]] if x["m_PathID"] else None
        return {k: clean(v) for k, v in x.items()}
    if isinstance(x, list):
        if len(x) > 2000:
            return ["%d items" % len(x)]
        return [clean(v) for v in x]
    return x


def _dump(name):
    level = "level%d" % scene_index()[name]
    lv = asset_file(level)
    exts = [e.path for e in lv.externals]
    objs = lv.objects
    tr, gos = {}, {}
    for pid, o in objs.items():
        if o.type.name in ("Transform", "RectTransform"):
            tr[pid] = o.read_typetree()
        elif o.type.name == "GameObject":
            gos[pid] = o.read_typetree()
    wm = {}
    sys.setrecursionlimit(20000)

    def world(pid):
        if pid not in wm:
            d = tr[pid]
            m = local_mat(d)
            f = d["m_Father"]["m_PathID"]
            if f and f in tr:
                m = world(f) @ m
            wm[pid] = m
        return wm[pid]
    go_tr = {d["m_GameObject"]["m_PathID"]: pid for pid, d in tr.items()}
    act, paths = {}, {}

    def parent_go(gpid):
        tp = go_tr.get(gpid)
        if tp is None:
            return 0
        f = tr[tp]["m_Father"]["m_PathID"]
        return tr[f]["m_GameObject"]["m_PathID"] if f and f in tr else 0

    def active(gpid):
        if gpid not in act:
            a = bool(gos[gpid]["m_IsActive"])
            p = parent_go(gpid)
            act[gpid] = a and (active(p) if p else True)
        return act[gpid]

    def path(gpid):
        if gpid not in paths:
            p = parent_go(gpid)
            paths[gpid] = (path(p) + "/" if p else "") + S(gos[gpid]["m_Name"])
        return paths[gpid]
    out = {"scene": name, "level": level, "externals": exts, "objects": []}
    for gpid, g in gos.items():
        tp = go_tr.get(gpid)
        rec = {"id": gpid, "name": S(g["m_Name"]), "path": path(gpid), "active": active(gpid),
               "self_active": bool(g["m_IsActive"]), "layer": g["m_Layer"], "tag": g.get("m_Tag", 0),
               "parent": parent_go(gpid)}
        if tp is not None:
            m = world(tp)
            rec["pos"] = rnd([float(m[0, 3]), float(m[1, 3]), float(m[2, 3])])
            rec["m3"] = rnd([float(x) for x in m[:3, :3].flatten()])
            d = tr[tp]
            rec["lpos"] = rnd([d["m_LocalPosition"][k] for k in "xyz"])
            rec["lscale"] = rnd([d["m_LocalScale"][k] for k in "xyz"])
            q = d["m_LocalRotation"]
            rec["lrot"] = rnd([q[k] for k in "xyzw"])
            rec["children"] = [tr[c["m_PathID"]]["m_GameObject"]["m_PathID"] for c in d["m_Children"] if c["m_PathID"] in tr]
        cl = []
        for c in g["m_Component"]:
            o = objs.get(c["component"]["m_PathID"])
            if o is None or o.type.name in ("Transform", "RectTransform"):
                continue
            t = o.type.name
            ent = {"type": t, "pid": o.path_id}
            try:
                if t == "MonoBehaviour":
                    mb = o.read(check_read=False)
                    sc = mb.m_Script.read()
                    ent["class"] = sc.m_ClassName
                    try:
                        v, _, _ = typetree.parse(o, sc)
                        ent["enabled"] = bool(v.get("m_Enabled", 1))
                        if sc.m_ClassName == "PlayMakerFSM":
                            dfs = fsmdec.decode(v)
                            ent["fsm"] = clean(dfs)
                        else:
                            ent["v"] = clean({k: x for k, x in v.items() if k not in ("m_GameObject", "m_Enabled", "m_Script", "m_Name")})
                    except Exception as e:
                        ent["err"] = repr(e)[:160]
                elif t == "Mesh":
                    continue
                else:
                    d = o.read_typetree()
                    d.pop("m_GameObject", None)
                    if t == "ParticleSystem":
                        ent["v"] = {"note": "particles"}
                    elif t == "AudioSource":
                        ent["v"] = {}
                    else:
                        ent["v"] = clean(d)
            except Exception as e:
                ent["err"] = repr(e)[:160]
            cl.append(ent)
        rec["c"] = cl
        out["objects"].append(rec)
    return out


@functools.lru_cache(maxsize=None)
def scene(name):
    """A scene as a dict (cached as JSON in tools/cache)."""
    os.makedirs(CACHE, exist_ok=True)
    p = os.path.join(CACHE, name + ".json")
    if os.path.exists(p):
        return json.load(open(p))
    with in_data():
        d = _dump(name)
    json.dump(d, open(p, "w"), separators=(",", ":"))
    return d


def comp(o, cls=None, typ=None):
    for c in o["c"]:
        if (cls and c.get("class") == cls) or (typ and c["type"] == typ):
            return c
    return None


def by_id(d):
    return {o["id"]: o for o in d["objects"]}


class Sprite:
    pass


_sprites = {}


def sprite(level, exts, fid, pid):
    """A Sprite (name, rect size, pixels per unit, pivot, image on demand) from a scene's reference."""
    path = exts[fid - 1] if fid > 0 else level
    key = (path, pid)
    s = _sprites.get(key)
    if s is None:
        f = asset_file(path)
        if f is None or pid not in f.objects:
            return None
        with in_data():
            o = f.objects[pid].read()
        s = Sprite()
        s.key, s.name = key, o.m_Name
        s.w, s.h = o.m_Rect.width, o.m_Rect.height
        s.ppu = o.m_PixelsToUnits
        s.px, s.py = o.m_Pivot.x, o.m_Pivot.y
        b = o.m_Border
        s.border = (b.x, b.y, b.z, b.w)
        s._obj = f.objects[pid]
        s.tex = None
        _sprites[key] = s
    return s


def sprite_by_key(key):
    """A Sprite from its (file, path id)."""
    s = _sprites.get(key)
    if s is None:
        s = sprite(None, (key[0],), 1, key[1])
    return s


def sprite_image(s):
    if s.tex is None:
        with in_data():
            s.tex = s._obj.read().image.convert("RGBA")
    return s.tex


@functools.lru_cache(maxsize=None)
def material(path, pid):
    f = asset_file(path)
    if f is None or pid not in f.objects:
        return ("?", "?")
    with in_data():
        m = f.objects[pid].read()
        try:
            shn = m.m_Shader.read().m_ParsedForm.m_Name
        except Exception:
            shn = "?"
    return (m.m_Name, shn)


def ref_path(d, fid):
    return d["externals"][fid - 1] if fid > 0 else d["level"]
