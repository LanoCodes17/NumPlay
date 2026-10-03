"""A room's solid ground for the Knight: the outlines of its terrain colliders (edges, polygons, boxes, circles) as
line segments in world units, joined where they run on in a line, and a grid of the segments near each cell."""
import math
import numpy as np
import unity

SOLID_LAYERS = {8, 25}          # Terrain, Soft Terrain (the Knight collides with these; its rays see Terrain only)
CELL = 8.0                      # grid cell (units)
UNIT = 128                      # segment ends are stored in 1/128 unit
# collider flags (src/hk.h: CF_*)
CF_TERRAIN, CF_STEEP, CF_NONSLIDER, CF_NOHARDLAND, CF_ROOF, CF_SOLID, CF_NONTHUNKER = 1, 2, 4, 8, 16, 32, 64


def _world(o, pts, off):
    m = np.array(o["m3"]).reshape(3, 3)
    p = np.array(o["pos"])
    return [tuple((m @ np.array([x + off[0], y + off[1], 0.0]) + p)[:2]) for x, y in pts]


def _merge(chain, closed):
    """Drops the points of a polyline that lie on the line through their neighbours."""
    pts = list(chain)
    changed = True
    while changed and len(pts) > 2:
        changed = False
        out = []
        n = len(pts)
        for i, q in enumerate(pts):
            if not closed and (i == 0 or i == n - 1):
                out.append(q)
                continue
            a, b = pts[i - 1], pts[(i + 1) % n]
            cross = (q[0] - a[0]) * (b[1] - a[1]) - (q[1] - a[1]) * (b[0] - a[0])
            if abs(cross) < 1e-4 and (q[0] - a[0]) * (b[0] - q[0]) + (q[1] - a[1]) * (b[1] - q[1]) >= 0:
                changed = True
                continue
            out.append(q)
        pts = out
    return pts


def _tilemap_solid(edges, w, h):
    """The tiles inside the tilemap's outlines (each tile's center: odd crossings of the vertical edges to its right)."""
    tw, th = int(math.ceil(w)), int(math.ceil(h))
    solid = np.zeros((th, tw), bool)
    vert = [(x0, min(y0, y1), max(y0, y1)) for x0, y0, x1, y1 in edges if abs(x0 - x1) < 1e-4]
    for ty in range(th):
        cy = ty + 0.5
        xs = sorted(x for x, a, b in vert if a < cy < b)
        for i in range(0, len(xs) - 1, 2):
            a, b = int(round(xs[i])), int(round(xs[i + 1]))
            solid[ty, max(0, a):max(0, min(tw, b))] = True
        if len(xs) % 2:
            print("coll: tilemap outline open at row", ty)
    return solid


def tile_faces(solid):
    """The unit edges between solid and open tiles (outside the map is open)."""
    th, tw = solid.shape
    f = set()
    def s(x, y):
        return 0 <= x < tw and 0 <= y < th and solid[y, x]
    for y in range(th):
        for x in range(tw):
            if not solid[y, x]:
                continue
            if not s(x - 1, y): f.add(("v", x, y))
            if not s(x + 1, y): f.add(("v", x + 1, y))
            if not s(x, y - 1): f.add(("h", x, y))
            if not s(x, y + 1): f.add(("h", x, y + 1))
    return f


def _unit_edges(edges):
    out = set()
    for x0, y0, x1, y1 in edges:
        if abs(x0 - x1) < 1e-4:
            for y in range(int(round(min(y0, y1))), int(round(max(y0, y1)))):
                out.add(("v", int(round(x0)), y))
        else:
            for x in range(int(round(min(x0, x1))), int(round(max(x0, x1)))):
                out.add(("h", x, int(round(y0))))
    return out


def path_key(d):
    """A sort key that keeps every object's subtree together: (name, id) from the root down."""
    by = {o["id"]: o for o in d["objects"]}
    keys = {}

    def key(o):
        k = keys.get(o["id"])
        if k is None:
            p = by.get(o["parent"])
            k = keys[o["id"]] = (key(p) if p else ()) + ((o["name"], o["id"]),)
        return k
    return key


# objects off in the scene whose colliders the scripts turn on (ActivateAllChildren): there, off at first
SWITCHED = {"Hornet Saver/Colliders", "Fk Break Wall/Breakable", "Fk Break Wall/Repaired", "Fk Break Wall/Broken/Slope",
            "Fk Break Wall/Broken/Roof Collider"}
# (solid though on another layer: the False Knight's broken wall's slope, on Hero Detector, which the Knight meets)
SOLID_PATHS = {"Fk Break Wall/Broken/Slope"}


def room(d, w, h):
    """-> (solid tiles of the tilemap, segments [(x0, y0, x1, y1, collider)], colliders [flags], each collider's object
    id); collider 0 is the tilemap's. An object's subtree has consecutive colliders (subtree_colliders)."""
    segs, cols, owners = [], [CF_SOLID | CF_TERRAIN], [0]
    tm_edges = []
    for o in sorted(d["objects"], key=path_key(d)):
        if (not o["active"] and o["path"] not in SWITCHED) or (o["layer"] not in SOLID_LAYERS and o["path"] not in SOLID_PATHS):
            continue
        if "TileMap Render Data/" in o["path"]:
            for c in o["c"]:
                v = c.get("v")
                if c["type"] == "EdgeCollider2D" and v and not v.get("m_IsTrigger") and v.get("m_Enabled", 1):
                    off = (v["m_Offset"]["x"], v["m_Offset"]["y"])
                    pts = _world(o, [(p["x"], p["y"]) for p in v["m_Points"]], off)
                    tm_edges += [(a[0], a[1], b[0], b[1]) for a, b in zip(pts, pts[1:]) if a != b]
            continue
        scripts = {c.get("class") for c in o["c"] if c["type"] == "MonoBehaviour"}
        for c in o["c"]:
            t, v = c["type"], c.get("v")
            if t not in ("EdgeCollider2D", "PolygonCollider2D", "BoxCollider2D", "CircleCollider2D"):
                continue
            if v is None:
                print("coll: %s: %s not read" % (o["path"], t))
                continue
            if v.get("m_IsTrigger") or not v.get("m_Enabled", 1):
                continue
            off = (v["m_Offset"]["x"], v["m_Offset"]["y"])
            chains = []
            if t == "EdgeCollider2D":
                chains.append((_world(o, [(p["x"], p["y"]) for p in v["m_Points"]], off), False))
            elif t == "PolygonCollider2D":
                paths = v["m_Points"]["m_Paths"] if isinstance(v["m_Points"], dict) else v["m_Points"]
                for path in paths:
                    chains.append((_world(o, [(p["x"], p["y"]) for p in path], off), True))
            elif t == "BoxCollider2D":
                sx, sy = v["m_Size"]["x"] / 2, v["m_Size"]["y"] / 2
                chains.append((_world(o, [(-sx, -sy), (sx, -sy), (sx, sy), (-sx, sy)], off), True))
            else:
                r = v["m_Radius"]
                chains.append((_world(o, [(r * math.cos(a * math.pi / 4), r * math.sin(a * math.pi / 4)) for a in range(8)], off), True))
            flags = CF_SOLID | (CF_TERRAIN if o["layer"] == 8 else 0)
            if "SteepSlope" in scripts:
                flags |= CF_STEEP
            if "NonSlider" in scripts:
                flags |= CF_NONSLIDER
            if "NoHardLanding" in scripts:
                flags |= CF_NOHARDLAND
            if "Roof" in scripts:
                flags |= CF_ROOF
            if any(c.get("class") == "NonThunker" and (c.get("v") or {}).get("active", True) for c in o["c"]):
                flags |= CF_NONTHUNKER
            ci = len(cols)
            cols.append(flags)
            owners.append(o["id"])
            for pts, closed in chains:
                pts = _merge(pts, closed)
                n = len(pts)
                for i in range(n if closed else n - 1):
                    a, b = pts[i], pts[(i + 1) % n]
                    if a != b:
                        segs.append((a[0], a[1], b[0], b[1], ci))
    solid = _tilemap_solid(tm_edges, w, h)
    # (the tilemap's chunks also outline their borders: edges inside solid ground, no faces)
    th, tw = solid.shape
    def s(x, y):
        return 0 <= x < tw and 0 <= y < th and solid[y, x]
    want = {(k, x, y) for k, x, y in _unit_edges(tm_edges)
            if not (s(x - 1, y) and s(x, y) if k == "v" else s(x, y - 1) and s(x, y))}
    got = tile_faces(solid)
    if want != got:
        print("coll: tilemap faces differ: %d missing, %d extra" % (len(want - got), len(got - want)))
    assert len(cols) <= 256, len(cols)
    return solid, segs, cols, owners


def subtree_colliders(d, owners, oid):
    """The colliders of an object and everything under it -> (first, count)."""
    by = {o["id"]: o for o in d["objects"]}
    ids = set()
    todo = [oid]
    while todo:
        i = todo.pop()
        ids.add(i)
        todo += by[i].get("children", [])
    idx = [k for k, ow in enumerate(owners) if k and ow in ids]
    if not idx:
        return 0, 0
    assert idx == list(range(idx[0], idx[-1] + 1)), idx
    return idx[0], len(idx)


def grid(segs, w, h):
    """Cells (CELL units, the room from 0, 0, a cell of margin around) -> segment indices whose box touches them."""
    gw, gh = int(math.ceil(w / CELL)) + 2, int(math.ceil(h / CELL)) + 2
    cells = [[] for _ in range(gw * gh)]
    for i, (x0, y0, x1, y1, _) in enumerate(segs):
        cx0 = max(0, min(gw - 1, int(math.floor(min(x0, x1) / CELL)) + 1))
        cx1 = max(0, min(gw - 1, int(math.floor(max(x0, x1) / CELL)) + 1))
        cy0 = max(0, min(gh - 1, int(math.floor(min(y0, y1) / CELL)) + 1))
        cy1 = max(0, min(gh - 1, int(math.floor(max(y0, y1) / CELL)) + 1))
        for cy in range(cy0, cy1 + 1):
            for cx in range(cx0, cx1 + 1):
                cells[cy * gw + cx].append(i)
    return gw, gh, cells
