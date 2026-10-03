"""A room's game objects that are not scenery: camera lock areas, gates to other rooms, respawn points and the triggers
that set them. Each becomes a record (src/game.h: Ent) with its trigger box (world units) and its own values; names go to
a shared string table."""
import struct
import numpy as np

# record types (src/game.h: ENT_*)
ENT_CAMLOCK, ENT_GATE, ENT_HAZARD_MARKER, ENT_RESPAWN, ENT_HAZARD_TRIGGER = 1, 2, 3, 4, 5
# flags
CL_PREVENT_UP, CL_PREVENT_DOWN, CL_MAX_PRIORITY = 1, 2, 4
G_DOOR, G_ENTER_RIGHT, G_ENTER_LEFT, G_DONT_WALK_OUT, G_NON_HAZARD = 1, 2, 4, 8, 16
FACING_RIGHT = 1
REC = "<BBHffffffffHH"   # type, flags, a, box (x0, y0, x1, y1), p0..p3, s0, s1


class Strings:
    def __init__(self):
        self.list, self.index = [], {}

    def id(self, s):
        if s not in self.index:
            self.index[s] = len(self.list)
            self.list.append(s)
        return self.index[s]

    def blob(self):
        offs, body = [], bytearray()
        for s in self.list:
            offs.append(len(body))
            body += s.encode() + b"\0"
        return struct.pack("<I", len(self.list)) + b"".join(struct.pack("<I", o) for o in offs) + bytes(body)


def _box(o, c):
    """A collider's world bounds (box colliders; others by their points)."""
    v = c["v"]
    m = np.array(o["m3"]).reshape(3, 3)
    p = np.array(o["pos"])
    off = (v["m_Offset"]["x"], v["m_Offset"]["y"])
    if c["type"] == "BoxCollider2D":
        sx, sy = v["m_Size"]["x"] / 2, v["m_Size"]["y"] / 2
        pts = [(-sx, -sy), (sx, -sy), (sx, sy), (-sx, sy)]
    elif c["type"] == "PolygonCollider2D":
        paths = v["m_Points"]["m_Paths"] if isinstance(v["m_Points"], dict) else v["m_Points"]
        pts = [(q["x"], q["y"]) for path in paths for q in path]
    elif c["type"] == "CircleCollider2D":
        r = v["m_Radius"]
        pts = [(-r, -r), (r, r)]
    else:
        return None
    w = [m @ np.array([x + off[0], y + off[1], 0.0]) + p for x, y in pts]
    xs, ys = [q[0] for q in w], [q[1] for q in w]
    return (min(xs), min(ys), max(xs), max(ys))


def _trigger(o):
    for c in o["c"]:
        if c["type"] in ("BoxCollider2D", "PolygonCollider2D", "CircleCollider2D") and c.get("v") and c["v"].get("m_IsTrigger"):
            return _box(o, c)
    return None


def room(d, rooms, strings):
    """-> list of packed records."""
    recs = []
    marker_index = {}
    objs = [o for o in d["objects"] if o["active"]]
    # markers first: the triggers refer to them
    for o in objs:
        for c in o["c"]:
            cls, v = c.get("class"), c.get("v") or {}
            if cls == "HazardRespawnMarker":
                marker_index[o["id"]] = len(recs)
                x, y = o["pos"][:2]
                recs.append(struct.pack(REC, ENT_HAZARD_MARKER, FACING_RIGHT if v.get("respawnFacingRight") else 0, 0,
                                        x, y, x, y, 0, 0, 0, 0, 0, strings.id(o["name"])))
            elif cls == "RespawnMarker":
                x, y = o["pos"][:2]
                recs.append(struct.pack(REC, ENT_RESPAWN, FACING_RIGHT if v.get("respawnFacingRight") else 0, 0,
                                        x, y, x, y, 0, 0, 0, 0, strings.id(o["name"]), 0))
    for o in objs:
        for c in o["c"]:
            cls, v = c.get("class"), c.get("v") or {}
            if cls == "CameraLockArea":
                box = _trigger(o)
                if box is None:
                    continue
                f = (CL_PREVENT_UP if v.get("preventLookUp") else 0) | (CL_PREVENT_DOWN if v.get("preventLookDown") else 0) | \
                    (CL_MAX_PRIORITY if v.get("maxPriority") else 0)
                recs.append(struct.pack(REC, ENT_CAMLOCK, f, 0, *box, v["cameraXMin"], v["cameraYMin"], v["cameraXMax"],
                                        v["cameraYMax"], 0, 0))
            elif cls == "TransitionPoint":
                box = _trigger(o)
                if box is None:
                    continue
                f = (G_DOOR if v.get("isADoor") else 0) | (G_ENTER_RIGHT if v.get("alwaysEnterRight") else 0) | \
                    (G_ENTER_LEFT if v.get("alwaysEnterLeft") else 0) | (G_DONT_WALK_OUT if v.get("dontWalkOutOfDoor") else 0) | \
                    (G_NON_HAZARD if v.get("nonHazardGate") else 0)
                target = v.get("targetScene") or ""
                ti = rooms.index(target) if target in rooms else 0xFFFF
                recs.append(struct.pack(REC, ENT_GATE, f, ti, *box, float(v.get("entryDelay", 0)), 0, 0, 0,
                                        strings.id(o["name"]), strings.id(v.get("entryPoint") or "")))
            elif cls == "HazardRespawnTrigger":
                box = _trigger(o)
                ref = v.get("respawnMarker")
                mid = ref[1] if isinstance(ref, (list, tuple)) else (ref or {}).get("m_PathID") if isinstance(ref, dict) else None
                # (the marker is a component: find its object)
                target = None
                for oo in objs:
                    for cc in oo["c"]:
                        if cc.get("pid") == mid:
                            target = oo["id"]
                if box is None or target not in marker_index:
                    continue
                recs.append(struct.pack(REC, ENT_HAZARD_TRIGGER, 0, marker_index[target], *box, 0, 0, 0, 0, 0, 0))
    return recs
