"""A room's game objects that are not scenery: camera lock areas, gates to other rooms, respawn points and the triggers
that set them. Each becomes a record (src/game.h: Ent) with its trigger box (world units) and its own values; names go to
a shared string table."""
import struct
import numpy as np

# record types (src/game.h: ENT_*)
ENT_CAMLOCK, ENT_GATE, ENT_HAZARD_MARKER, ENT_RESPAWN, ENT_HAZARD_TRIGGER, ENT_MASK = 1, 2, 3, 4, 5, 6
# masks (the unmasker, remasker and remasker_inverse FSMs)
MK_SECRET, MK_REMASK, MK_SIMPLE = 1, 2, 4
# flags
CL_PREVENT_UP, CL_PREVENT_DOWN, CL_MAX_PRIORITY = 1, 2, 4
G_DOOR, G_ENTER_RIGHT, G_ENTER_LEFT, G_DONT_WALK_OUT, G_NON_HAZARD = 1, 2, 4, 8, 16
FACING_RIGHT = 1
REC = "<BBBBHHffffffffHH"   # type, flags, group, group2, a, persist, box (x0, y0, x1, y1), p0..p3, s0, s1
NO_PERSIST = 0xFFFF


class Persist:
    """The objects whose state a save keeps (PersistentBoolItem): one bit each."""
    def __init__(self):
        self.keys = {}

    def id(self, room, path):
        return self.keys.setdefault((room, path), len(self.keys))


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


def rec(type_, flags=0, box=(0, 0, 0, 0), p=(0, 0, 0, 0), a=0, group=0, group2=0, persist=NO_PERSIST, s0=0, s1=0):
    return struct.pack(REC, type_, flags, group, group2, a, persist, *box, *p, s0, s1)


def _fsm(o, names):
    for c in o["c"]:
        f = c.get("fsm")
        if f and f.get("name") in names:
            return f
    return None


def _state(f, name):
    for st in f["states"]:
        if st["name"] == name:
            return st
    return None


def _param(action, key):
    for k, v in action["params"]:
        if k == key:
            return v
    return None


def _value(f, v, default=0.0):
    """A parameter: a number, or an FSM variable ("$Name")."""
    if isinstance(v, str) and v.startswith("$"):
        var = f.get("vars", {}).get(v[1:])
        return float(var[1]) if var and isinstance(var[1], (int, float)) else default
    return float(v) if isinstance(v, (int, float)) else default


def _fade_time(f, state):
    st = _state(f, state)
    for a in (st["actions"] if st else []):
        if a["name"] == "iTweenFadeTo" and a.get("enabled", True):
            return _value(f, _param(a, "time"), 0.5)
    return 0.5


def _owner_alpha(f, state):
    """The alpha the state fades its owner to (-1: none)."""
    st = _state(f, state)
    for a in (st["actions"] if st else []):
        if a["name"] == "iTweenFadeTo" and a.get("enabled", True) and _param(a, "gameObject") == "Owner":
            return _value(f, _param(a, "alpha"), 0)
    return -1.0


def _mask(o, f, objs_by_id):
    """-> (kind, fade time, pause, p2, p3, inverse child id) or None: p2, p3 the trigger kind (simple masks), or the
    alphas Idle and Fade Out give the owner (remaskers)."""
    names = [st["name"] for st in f["states"]]
    inverse = None
    for cid in o.get("children", []):
        if objs_by_id.get(cid, {}).get("name") == "Inverse Mask":
            inverse = cid
    pause = 0.0
    ps = _state(f, "Pause")
    if ps:
        for a in ps["actions"]:
            if a["name"] == "Wait" and a.get("enabled", True):
                pause = _value(f, _param(a, "time"), 0)
    if "Idle Stay" in names:
        return MK_SECRET, _fade_time(f, "Fade"), pause, 1, 0, None
    if "Fade Out" in names and "Fade In" in names:
        # (what Idle and Fade Out fade the owner to; Fade In the other way; an inverse mask child the other way still)
        return MK_REMASK, _fade_time(f, "Fade Out"), pause, _owner_alpha(f, "Idle"), _owner_alpha(f, "Fade Out"), inverse
    if "Fade" in names:
        trig = 3   # (no trigger: another object uncovers it)
        idle = _state(f, f["start"])
        for a in (idle["actions"] if idle else []):
            if a["name"] == "Trigger2dEvent" and a.get("enabled", True):
                trig = int(_param(a, "trigger") or 0)
        return MK_SIMPLE, _fade_time(f, "Fade"), 0.0, trig, 0, None
    return None


def room(d, rooms, strings, persist, name):
    """-> (packed records, {object id: render group})."""
    recs = []
    groups = {}
    marker_index = {}
    objs = [o for o in d["objects"] if o["active"]]
    by_id = {o["id"]: o for o in d["objects"]}

    def subtree(oid, g):
        groups[oid] = g
        for c in by_id[oid].get("children", []):
            subtree(c, g)
    ngroups = [0]

    def new_group(oid):
        ngroups[0] += 1
        subtree(oid, ngroups[0])
        return ngroups[0]
    # markers first: the triggers refer to them
    for o in objs:
        for c in o["c"]:
            cls, v = c.get("class"), c.get("v") or {}
            if cls == "HazardRespawnMarker":
                marker_index[o["id"]] = len(recs)
                x, y = o["pos"][:2]
                recs.append(rec(ENT_HAZARD_MARKER, FACING_RIGHT if v.get("respawnFacingRight") else 0, (x, y, x, y),
                                s1=strings.id(o["name"])))
            elif cls == "RespawnMarker":
                x, y = o["pos"][:2]
                recs.append(rec(ENT_RESPAWN, FACING_RIGHT if v.get("respawnFacingRight") else 0, (x, y, x, y),
                                s0=strings.id(o["name"])))
    for o in objs:
        f = _fsm(o, ("unmasker", "remasker", "remasker_inverse"))
        if f:
            m = _mask(o, f, by_id)
            box = _trigger(o) or (0, 0, 0, 0)
            if m:
                kind, fade, pause, p2, p3, inverse = m
                g = new_group(o["id"])
                g2 = new_group(inverse) if inverse else 0
                persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
                recs.append(rec(ENT_MASK, kind, box, (fade, pause, p2, p3), group=g, group2=g2,
                                persist=persist.id(name, o["path"]) if persistent else NO_PERSIST))
        for c in o["c"]:
            cls, v = c.get("class"), c.get("v") or {}
            if cls == "CameraLockArea":
                box = _trigger(o)
                if box is None:
                    continue
                fl = (CL_PREVENT_UP if v.get("preventLookUp") else 0) | (CL_PREVENT_DOWN if v.get("preventLookDown") else 0) | \
                    (CL_MAX_PRIORITY if v.get("maxPriority") else 0)
                recs.append(rec(ENT_CAMLOCK, fl, box, (v["cameraXMin"], v["cameraYMin"], v["cameraXMax"], v["cameraYMax"])))
            elif cls == "TransitionPoint":
                box = _trigger(o)
                if box is None:
                    continue
                fl = (G_DOOR if v.get("isADoor") else 0) | (G_ENTER_RIGHT if v.get("alwaysEnterRight") else 0) | \
                    (G_ENTER_LEFT if v.get("alwaysEnterLeft") else 0) | (G_DONT_WALK_OUT if v.get("dontWalkOutOfDoor") else 0) | \
                    (G_NON_HAZARD if v.get("nonHazardGate") else 0)
                target = v.get("targetScene") or ""
                ti = rooms.index(target) if target in rooms else 0xFFFF
                recs.append(rec(ENT_GATE, fl, box, (float(v.get("entryDelay", 0)), 0, 0, 0), a=ti, s0=strings.id(o["name"]),
                                s1=strings.id(v.get("entryPoint") or "")))
            elif cls == "HazardRespawnTrigger":
                box = _trigger(o)
                ref = v.get("respawnMarker")
                mid = ref[1] if isinstance(ref, (list, tuple)) else None
                # (the marker is a component: find its object)
                target = None
                for oo in objs:
                    for cc in oo["c"]:
                        if cc.get("pid") == mid:
                            target = oo["id"]
                if box is None or target not in marker_index:
                    continue
                recs.append(rec(ENT_HAZARD_TRIGGER, box=box, a=marker_index[target]))
    assert ngroups[0] < 64, ngroups[0]
    return recs, groups
