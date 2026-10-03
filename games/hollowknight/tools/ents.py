"""A room's game objects that are not scenery: camera lock areas, gates to other rooms, respawn points and the triggers
that set them. Each becomes a record (src/game.h: Ent) with its trigger box (world units) and its own values; names go to
a shared string table."""
import struct
import numpy as np

# record types (src/game.h: ENT_*)
ENT_CAMLOCK, ENT_GATE, ENT_HAZARD_MARKER, ENT_RESPAWN, ENT_HAZARD_TRIGGER, ENT_MASK, ENT_DAMAGE, ENT_SHAPE, ENT_BOX, \
    ENT_OBJ, ENT_PIECE, ENT_SHADE_MARKER = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
# objects (ENT_OBJ's flags: src/obj.c)
OK_BREAKABLE, OK_ENEMY, OK_GREAT_DOOR, OK_GEO_ROCK, OK_CHEST, OK_BENCH = 1, 2, 3, 4, 5, 6
# enemies (an OK_ENEMY's a: src/enemy.c), by their FSM and animation library
import actors as _actors
# enemies: by the FSM (or component) that runs them and their animation library -> their kind (actors.ENEMY_KINDS)
ENEMIES = {(k[1], k[2][0], k[2][1]): i + 1 for i, k in enumerate(_actors.ENEMY_KINDS) if k[1]}
# their FSMs' variables an enemy's record keeps (ET_VARS: up to four numbers, then bools as bits)
ENEMY_VARS = {"Zombie Swipe": (["Lunge Speed", "Idle Time"], ["Coward"]),
              "Bouncer Control": (["Speed"], ["Starts Inactive", "Start Up"]),
              "Roller": (["Acceleration", "Max Speed", "Roll time Min", "Roll time Max", "Stop Time"], ["Moving Right"]),
              "Blocker Control": (["Shot Y Speed"], ["Facing Right"]),
              "Zombie Leap": (["Idle Time"], []),
              "Zombie Guard": (["Chase Distance", "Roam Distance"], ["Start Facing Left"])}
# what follows an enemy's record: ENT_BOX records, tagged
ET_COLLIDER, ET_ALERT, ET_RANGE, ET_WALKER, ET_RECOIL, ET_CORPSE, ET_VARS, ET_TERRAIN, ET_HITBOX = 0, 1, 2, 3, 4, 5, 6, 7, 8
EF_START, EF_STARTLES = 1, 2   # (FSM bools: First Crawler or Start Alert; Startles)
# the layers the nail's slashes touch (Physics2D's collision matrix, layer 17 Attack)
ATTACK_HITS = {3, 6, 7, 8, 11, 12, 17, 19, 20, 21, 25, 31}
MAX_GROUPS = 256
# masks (the unmasker, remasker and remasker_inverse FSMs)
MK_SECRET, MK_REMASK, MK_SIMPLE = 1, 2, 4
# flags
CL_PREVENT_UP, CL_PREVENT_DOWN, CL_MAX_PRIORITY = 1, 2, 4
G_DOOR, G_ENTER_RIGHT, G_ENTER_LEFT, G_DONT_WALK_OUT, G_NON_HAZARD, G_HARD_LAND = 1, 2, 4, 8, 16, 32
FACING_RIGHT = 1
REC = "<BBBBHHffffffffHH"   # type, flags, group, group2, a, persist, box (x0, y0, x1, y1), p0..p3, s0, s1
NO_PERSIST = 0xFFFF


class Persist:
    """The objects whose state a save keeps (PersistentBoolItem): one bit each (or a few: a number)."""
    def __init__(self):
        self.keys, self.n, self.bits = {}, 0, []

    def id(self, room, path, bits=1):
        if (room, path) not in self.keys:
            self.keys[(room, path)] = self.n
            self.bits.append(bits)
            self.n += bits
            assert self.n <= 1024, self.n   # (src/game.h: MAX_PERSIST)
        return self.keys[(room, path)]

    def blob(self):
        """PHASH: each bit's name (a CRC of its room, object and bit), as saves keep them: the same objects across
        versions of the game's data"""
        import zlib
        out = [0] * self.n
        for (room, path), first in self.keys.items():
            n = self.bits[list(self.keys).index((room, path))]
            for k in range(n):
                out[first + k] = zlib.crc32(("%s|%s|%d" % (room, path, k)).encode()) or 1
        assert len(set(out)) == len(out)
        return struct.pack("<I%dI" % len(out), len(out), *out)


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


class Sprites:
    """Unity sprites objects show as actors (debris...): pack.py makes them like the actors' frames, after those."""
    def __init__(self):
        self.list, self.index, self.base = [], {}, 0

    def id(self, d, ref, scale, res=1.0):
        """(res: its texture's resolution, as a part of the screen's: smooth sprites can be stretched)"""
        return self._add((d["level"], tuple(d["externals"]), ref[0], ref[1], round(scale, 3), res))

    def tk2d(self, path, col, name, scale=1.0):
        """A 2D Toolkit sprite of a collection (file, path id), by name."""
        return self._add(("tk2d", path, col, name, round(scale, 3), 1.0))

    def _add(self, key):
        if key not in self.index:
            self.index[key] = len(self.list)
            self.list.append(key)
        return self.base + self.index[key]


def _colliders(o):
    return [c for c in o["c"] if c["type"] in ("BoxCollider2D", "PolygonCollider2D", "CircleCollider2D", "EdgeCollider2D")
            and c.get("v") and c["v"].get("m_Enabled", 1)]


HB_BOUNCE, HB_RECOIL = 1, 2


def _hit_boxes(o, by, depth=2):
    """Where the nail hits an object: its colliders, and its children's and grandchildren's (HitTaker looks three levels
    up from what it touched), on the layers the slashes touch -> [(box, HB_* flags)]: whether a down slash bounces off
    it, whether a slash recoils the Knight (NailSlash: enemies, attacks, interactive objects without NonBouncer)."""
    out = []
    if o["active"] and o["layer"] in ATTACK_HITS:
        nb = any(c.get("class") == "NonBouncer" and (c.get("v") or {}).get("active", True) for c in o["c"])
        fl = 0
        if not nb and o["layer"] in (11, 17, 19):
            fl |= HB_BOUNCE
        if not nb and o["layer"] == 11:
            fl |= HB_RECOIL
        out += [(b, fl) for b in (_box(o, c) for c in _colliders(o) if c["type"] != "EdgeCollider2D") if b]
    if depth:
        for ch in o.get("children", []):
            out += _hit_boxes(by[ch], by, depth - 1)
    return out


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


def _shape(o, c):
    """A collider's outline in world units (a box's four corners, a polygon's first path)."""
    v = c["v"]
    m = np.array(o["m3"]).reshape(3, 3)
    p = np.array(o["pos"])
    off = (v["m_Offset"]["x"], v["m_Offset"]["y"])
    if c["type"] == "BoxCollider2D":
        sx, sy = v["m_Size"]["x"] / 2, v["m_Size"]["y"] / 2
        pts = [(-sx, -sy), (sx, -sy), (sx, sy), (-sx, sy)]
    elif c["type"] == "PolygonCollider2D":
        paths = v["m_Points"]["m_Paths"] if isinstance(v["m_Points"], dict) else v["m_Points"]
        pts = [(q["x"], q["y"]) for q in paths[0]]
    else:
        return None
    return [tuple((m @ np.array([x + off[0], y + off[1], 0.0]) + p)[:2]) for x, y in pts]


def _is_axis_box(pts):
    xs, ys = sorted(set(round(q[0], 4) for q in pts)), sorted(set(round(q[1], 4) for q in pts))
    return len(pts) == 4 and len(xs) == 2 and len(ys) == 2


def _triggers(o):
    """The boxes of the object's trigger colliders (the events come from any of them)."""
    return [_box(o, c) for c in o["c"] if c["type"] in ("BoxCollider2D", "PolygonCollider2D", "CircleCollider2D") and
            c.get("v") and c["v"].get("m_IsTrigger") and c["v"].get("m_Enabled", 1)]


def _trigger(o):
    t = _triggers(o)
    return t[0] if t else None


def _more_boxes(o):
    """Records for the object's other trigger boxes: they follow its own."""
    return [rec(ENT_BOX, box=b) for b in _triggers(o)[1:]]


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


def _pieces(d, o, ids, by_id, sprites):
    """Debris an object flings (inactive children with a sprite and a Rigidbody2D) -> ENT_PIECE records: where each is
    from the object, its sprite, how it falls, bounces and spins."""
    import scene
    out = []
    for pid in ids:
        q = by_id.get(pid)
        if q is None:
            continue
        sr = next((c for c in q["c"] if c["type"] == "SpriteRenderer" and (c.get("v") or {}).get("m_Sprite")), None)
        if sr is None:
            continue
        v = sr["v"]
        m = np.array(q["m3"]).reshape(3, 3)
        sx, sy = float(np.hypot(m[0, 0], m[1, 0])), float(np.hypot(m[0, 1], m[1, 1]))
        mirror = -1.0 if np.linalg.det(m[:2, :2]) < 0 else 1.0
        rot = float(np.degrees(np.arctan2(m[1, 0], m[0, 0]))) if mirror > 0 else float(np.degrees(np.arctan2(-m[1, 0], -m[0, 0])))
        rb = next((c.get("v") or {} for c in q["c"] if c["type"] == "Rigidbody2D"), {})
        ob = next((c.get("v") or {} for c in q["c"] if c.get("class") == "ObjectBounce"), None)
        sp = next((c.get("v") or {} for c in q["c"] if c.get("class") == "SpinSelf"), None)
        sid = sprites.id(d, v["m_Sprite"], max(sx, sy))
        dx, dy = q["pos"][0] - o["pos"][0], q["pos"][1] - o["pos"][1]
        layer = scene.layer_index(v.get("m_SortingLayerID", 0))
        out.append(rec(ENT_PIECE, 1 if sp is not None else 0, (dx, dy, rot, q["pos"][2]),
                       (rb.get("m_GravityScale", 1), ob.get("bounceFactor", 0) if ob else -1,
                        sp.get("spinFactor", -7.5) if sp else 0, mirror),
                       a=v.get("m_SortingOrder", 0) + 32768, group=layer, s0=sid))
    return out


def _ref_file(d, ref):
    """A scene's (or prefab's) reference -> (asset file, path id)."""
    fid, pid = ref
    return (d["level"] if fid == 0 else d["externals"][fid - 1].split("/")[-1], pid)


def _alert_range(q):
    """An alert range's trigger (circle or box) -> (cx, cy, r or hx, hy or -1) in its owner's units, or None."""
    k = (abs(q["lscale"][0]), abs(q["lscale"][1]))
    for c in q["c"]:
        v = c.get("v")
        if not v or not v.get("m_Enabled", 1):
            continue
        if c["type"] == "CircleCollider2D":
            r = v["m_Radius"] * max(k)
            return (q["lpos"][0] + v["m_Offset"]["x"] * k[0], q["lpos"][1] + v["m_Offset"]["y"] * k[1], r, -1)
        if c["type"] == "BoxCollider2D":
            return (q["lpos"][0] + v["m_Offset"]["x"] * k[0], q["lpos"][1] + v["m_Offset"]["y"] * k[1],
                    v["m_Size"]["x"] / 2 * k[0], v["m_Size"]["y"] / 2 * k[1])
    return None


def _enemy(o, by_id, persist, name, d=None, strings=None, owners=None):
    """An enemy -> its records: ENT_OBJ (OK_ENEMY: its kind, place (x, y, z, x scale), hp, damage, small and medium
    geo; large geo in group; s0 how many records follow), then ENT_BOX records each tagged (flags, ET_*): its
    collider, alert ranges, Walker, Recoil, corpse, the terrain it holds (its subtree's colliders: a first, group how
    many); or None."""
    an = next((c.get("v") for c in o["c"] if c.get("class") == "tk2dSpriteAnimator" and c.get("v")), None)
    hm = next((c.get("v") for c in o["c"] if c.get("class") == "HealthManager" and c.get("v")), None)
    if not an or not hm:
        return None
    kind, fsm = None, {}
    lib = _ref_file(d, an["library"]) if d is not None else ("", an["library"][1])
    for c in o["c"]:
        f = c.get("fsm")
        key = (f["name"] if f else c.get("class"),) + lib
        if key in ENEMIES:
            kind, fsm = ENEMIES[key], f or {}
    if kind is None:
        return None
    dh = next((c.get("v") or {} for c in o["c"] if c.get("class") == "DamageHero"), {})
    box = next((c["v"] for c in o["c"] if c["type"] == "BoxCollider2D" and c.get("v") and not c["v"].get("m_IsTrigger")), None)
    if box is None:
        return None
    sx, sy = o["lscale"][0], o["lscale"][1]
    var = fsm.get("vars", {})
    fl = 0
    if (var.get("First Crawler") or var.get("Start Alert") or [0, False])[1]:
        fl |= EF_START
    if (var.get("Startles") or [0, False])[1]:
        fl |= EF_STARTLES
    rb = next((c["v"] for c in o["c"] if c["type"] == "Rigidbody2D" and c.get("v")), {})
    out = [rec(ENT_BOX, ET_COLLIDER, box=(box["m_Offset"]["x"] * abs(sx), box["m_Offset"]["y"] * abs(sy),
                                          box["m_Size"]["x"] / 2 * abs(sx), box["m_Size"]["y"] / 2 * abs(sy)),
               p=(rb.get("m_GravityScale", 1), 0, 0, 0))]
    # its alert ranges (children with AlertRange): "Alert Range New" the main one, others by name
    for ch in o.get("children", []):
        q = by_id[ch]
        if not any(c.get("class") == "AlertRange" for c in q["c"]) and q["name"] not in ("Alert Range New", "Unalert Range"):
            continue
        r = _alert_range(q)
        if r is None:
            continue
        # (p0, p1: its scale, for an FSM that changes it)
        k = (abs(q["lscale"][0]), abs(q["lscale"][1]), 0, 0)
        if q["name"] == "Alert Range New":
            out.append(rec(ENT_BOX, ET_ALERT, box=r, p=k))
        else:
            out.append(rec(ENT_BOX, ET_RANGE, box=r, p=k, s0=strings.id(q["name"]) if strings else 0))
    # its attacks: children that hurt the Knight (DamageHero), their outlines in its own units (x as if facing its
    # scale's way), then their shapes' points; a: on at first; s0: the child's name
    sgn = -1.0 if o["lscale"][0] < 0 else 1.0
    for ch in o.get("children", []):
        q = by_id[ch]
        qd = next((c.get("v") for c in q["c"] if c.get("class") == "DamageHero" and c.get("v") is not None), None)
        qc = next((c for c in q["c"] if c["type"] in ("BoxCollider2D", "PolygonCollider2D") and c.get("v") and
                   c["v"].get("m_IsTrigger") and c["v"].get("m_Enabled", 1)), None)
        if qd is None or qc is None:
            continue
        pts = [((x - o["pos"][0]) * sgn, y - o["pos"][1]) for x, y in _shape(q, qc)][:8]
        xs, ys = [t[0] for t in pts], [t[1] for t in pts]
        nrec = (len(pts) + 3) // 4
        out.append(rec(ENT_BOX, ET_HITBOX, box=(min(xs), min(ys), max(xs), max(ys)),
                       p=(len(pts), qd.get("damageDealt", 1), qd.get("hazardType", 1), 0), a=1 if q["self_active"] else 0,
                       group=nrec, s0=strings.id(q["name"]) if strings else 0))
        for i in range(nrec):
            t = pts[4 * i:4 * i + 4] + [pts[-1]] * (4 - len(pts[4 * i:4 * i + 4]))
            out.append(rec(ENT_SHAPE, box=(t[0][0], t[0][1], t[1][0], t[1][1]), p=(t[2][0], t[2][1], t[3][0], t[3][1])))
    w = next((c.get("v") for c in o["c"] if c.get("class") == "Walker" and c.get("v")), None)
    if w:
        wf = (1 if w.get("pauses") else 0) | (2 if w.get("ignoreHoles") else 0) | \
            (4 if w.get("preventTurningToFaceHero") else 0) | (8 if w.get("startInactive") else 0) | \
            (16 if w.get("ambush") else 0) | (32 if w.get("waitForHeroX") else 0) | (64 if w.get("preventTurn") else 0) | \
            (128 if w.get("preventScaleChange") else 0) | (256 if w.get("rightScale", 1) < 0 else 0)
        out.append(rec(ENT_BOX, ET_WALKER, box=(w.get("walkSpeedL", 0), w.get("walkSpeedR", 0), w.get("pauseTimeMin", 0),
                                                 w.get("pauseTimeMax", 0)),
                       p=(w.get("pauseWaitMin", 0), w.get("pauseWaitMax", 0), w.get("turnPause", 0), w.get("edgeXAdjuster", 0)),
                       group=w.get("turnAfterIdlePercentage", 0), a=wf,
                       s1=max(0, min(65535, int(round(w.get("waitHeroX", 0) * 16))))))
    cl = next((c.get("v") for c in o["c"] if c.get("class") == "Climber" and c.get("v")), None)
    if cl:
        # (Climber: speed, spin time, wall ray padding, min turn distance; start right; its start angle's quarter)
        ang = int(round(_angle(o) / 90)) % 4
        out.append(rec(ENT_BOX, ET_VARS, p=(cl.get("speed", 2), cl.get("spinTime", 0.25), cl.get("wallRayPadding", 0.1),
                                            cl.get("minTurnDistance", 0.25)), a=1 if cl.get("startRight", 1) else 0, s1=ang))
    nums, bools = ENEMY_VARS.get(fsm.get("name"), ([], []))
    if nums or bools:
        # (up to eight numbers: p0..p3, then the box's)
        vals = [float((var.get(k) or [0, 0])[1] or 0) for k in nums] + [0.0] * (8 - len(nums))
        bits = sum(1 << i for i, k in enumerate(bools) if (var.get(k) or [0, False])[1])
        out.append(rec(ENT_BOX, ET_VARS, p=tuple(vals[:4]), box=tuple(vals[4:8]), a=bits))
    rc = next((c.get("v") for c in o["c"] if c.get("class") == "Recoil" and c.get("v")), None)
    if rc:
        out.append(rec(ENT_BOX, ET_RECOIL, box=(rc.get("recoilSpeedBase", 15), rc.get("recoilDuration", 0.15), 0, 0),
                       a=(1 if rc.get("stopVelocityXWhenRecoilingUp") else 0) | (2 if rc.get("freezeInPlace") else 0) |
                       (4 if rc.get("preventRecoilUp") else 0)))
    de = next((c.get("v") for c in o["c"] if c.get("class") == "EnemyDeathEffects" and c.get("v")), None)
    if de and de.get("corpsePrefab") and d is not None:
        try:
            import unity
            cf, cp = _ref_file(d, de["corpsePrefab"])
            cd = unity.prefab(cf, cp)
            co = cd["objects"][0]
            cb = next((c["v"] for c in co["c"] if c["type"] == "BoxCollider2D" and c.get("v")), None)
            rb = next((c["v"] for c in co["c"] if c["type"] == "Rigidbody2D" and c.get("v")), {})
            ob = next((c["v"] for c in co["c"] if c.get("class") == "ObjectBounce" and c.get("v")), None)
            cc = next((c for c in co["c"] if (c.get("class") or "").startswith("Corpse")), None)
            cv = (cc or {}).get("v") or {}
            k = (abs(co["lscale"][0]), abs(co["lscale"][1]))
            sp = de.get("corpseSpawnPoint") or {"y": 0}
            cfl = (1 if cv.get("breaker") else 0) | (2 if de.get("corpseFacesRight") else 0) | \
                (4 if de.get("lowCorpseArc") else 0) | (8 if cv.get("instantChunker") else 0) | (16 if cv.get("massless") else 0)
            # (a corpse with no collider falls through all: flag 32)
            box = (cb["m_Offset"]["x"] * k[0], cb["m_Offset"]["y"] * k[1], cb["m_Size"]["x"] / 2 * k[0],
                   cb["m_Size"]["y"] / 2 * k[1]) if cb else (0, 0, 0, 0)
            out.append(rec(ENT_BOX, ET_CORPSE, box=box, p=(rb.get("m_GravityScale", 1), ob.get("bounceFactor", -1) if ob else -1,
                                                           de.get("corpseFlingSpeed", 15), sp.get("y", 0)),
                           a=cfl | (0 if cb else 32)))
        except Exception as e:
            print("ents: %s: corpse %s" % (o["path"], e))
    if owners:
        import coll
        c0, cn = coll.subtree_colliders(d, owners, o["id"])
        if cn:
            out.append(rec(ENT_BOX, ET_TERRAIN, a=c0, group=cn))
    persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
    head = rec(ENT_OBJ, OK_ENEMY, (o["pos"][0], o["pos"][1], o["pos"][2], sx),
               (hm.get("hp", 1), dh.get("damageDealt", 0), hm.get("smallGeoDrops", 0), hm.get("mediumGeoDrops", 0)),
               a=kind, group=hm.get("largeGeoDrops", 0), persist=persist.id(name, o["path"]) if persistent else NO_PERSIST,
               s0=len(out), s1=fl)
    return [head] + out


def _tk2d_sprite(d, o):
    """An object's tk2dSprite -> (its collection's file, path id, the sprite's name) or None."""
    import tk2d, unity
    v = next((c.get("v") for c in o["c"] if c.get("class") == "tk2dSprite" and c.get("v")), None)
    if not v or not v.get("collection"):
        return None
    fid, pid = v["collection"]
    path = d["externals"][fid - 1] if fid else d["level"]
    col = tk2d.collection(path, pid)
    return path, pid, unity.S(col["spriteDefinitions"][v["_spriteId"]]["name"])


def _angle(o):
    m = np.array(o["m3"]).reshape(3, 3)
    return float(np.degrees(np.arctan2(m[1, 0], m[0, 0])))


def _props(d, o, by_id, rooms, strings, persist, sprites, owners, name):
    """Objects run by their own FSMs (src/obj.c) -> records, or None."""
    import coll
    var = lambda f, k, dflt: (f.get("vars", {}).get(k) or [0, dflt])[1]
    hits = _hit_boxes(o, by_id)
    c0, cn = coll.subtree_colliders(d, owners, o["id"]) if owners else (0, 0)
    persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
    f = _fsm(o, ("Great Door",))
    if f:
        sp = _tk2d_sprite(d, o)
        ids = [sprites.tk2d(sp[0], sp[1], "door_v%02d" % k) for k in (1, 2, 3)]
        return [rec(ENT_OBJ, OK_GREAT_DOOR, (o["pos"][0], o["pos"][1], o["pos"][2], 1), (ids[0], ids[1], ids[2],
                    rooms.index("Town") if "Town" in rooms else -1), a=c0, group=cn, s0=strings.id("left1"), s1=len(hits),
                    persist=persist.id(name, o["path"]) if persistent else NO_PERSIST)] + \
            [rec(ENT_BOX, fl, box=b) for b, fl in hits]
    f = _fsm(o, ("Geo Rock",))
    if f:
        variant = 2 if str(var(f, "Gleam Anim", "Gleam 1")).endswith("2") else 1
        return [rec(ENT_OBJ, OK_GEO_ROCK, (o["pos"][0], o["pos"][1], o["pos"][2], _angle(o)),
                    (var(f, "Hits", 5), var(f, "Geo Per Hit", 2), var(f, "Final Payout", 5), variant), a=c0, group=cn,
                    s1=len(hits), persist=persist.id(name, o["path"], 4))] + [rec(ENT_BOX, fl, box=b) for b, fl in hits]
    f = _fsm(o, ("Chest Control",))
    if f:
        # (opened: its Opened child's front and back sprites; inside, geo or a shiny item)
        opened = [q for q in (by_id[c] for c in o.get("children", [])) if q["name"] == "Opened"]
        parts = []
        for q in (by_id[c] for c in opened[0].get("children", [])) if opened else []:
            sp = _tk2d_sprite(d, q)
            if sp:
                parts.append(rec(ENT_PIECE, 0, (q["lpos"][0], q["lpos"][1], 0, o["pos"][2] + q["lpos"][2]),
                                 s0=sprites.tk2d(*sp)))
        sp = _tk2d_sprite(d, o)
        shiny = 0
        for q in d["objects"]:
            if q["path"].startswith(o["path"] + "/Item/"):
                sf = _fsm(q, ("Shiny Control",))
                if sf and var(sf, "Charm", False):
                    shiny = var(sf, "Charm ID", 0)
        return [rec(ENT_OBJ, OK_CHEST, (o["pos"][0], o["pos"][1], o["pos"][2], shiny),
                    (var(f, "Geo Small", 0), var(f, "Geo Med", 0), var(f, "Geo Large", 0), len(parts)), a=c0, group=cn,
                    s0=sprites.tk2d(*sp) if sp else 0, s1=len(hits),
                    persist=persist.id(name, o["path"]) if persistent else NO_PERSIST)] + \
            [rec(ENT_BOX, fl, box=b) for b, fl in hits] + parts
    return None


def room(d, rooms, strings, persist, name, sprites=None, owners=None):
    """-> (packed records, {object id: render group}). owners: each collider's object (coll.room); sprites: where the
    objects' own sprites go (Sprites)."""
    import coll
    sprites = sprites or Sprites()
    recs = []
    rec_of, receivers = {}, []   # (an object's record; the records that send HIT to another object's)
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
        f = _fsm(o, ("Spawn Offset",))
        if f:
            # (where a shade goes when the Knight dies near: the marker and its offset; its special type)
            var = lambda k, dflt: (f.get("vars", {}).get(k) or [0, dflt])[1]
            x, y = o["pos"][0] + var("X", 0), o["pos"][1] + var("Y", 0)
            recs.append(rec(ENT_SHADE_MARKER, box=(x, y, o["pos"][0], o["pos"][1]), a=var("Special Type", 0)))
    for o in objs:
        f = _fsm(o, ("unmasker", "remasker", "remasker_inverse"))
        if f:
            m = _mask(o, f, by_id)
            box = _trigger(o) or (0, 0, 0, 0)
            if m:
                kind, fade, pause, p2, p3, inverse = m
                rec_of[o["id"]] = len(recs)
                g = new_group(o["id"])
                g2 = new_group(inverse) if inverse else 0
                persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
                recs.append(rec(ENT_MASK, kind, box, (fade, pause, p2, p3), group=g, group2=g2,
                                persist=persist.id(name, o["path"]) if persistent else NO_PERSIST))
                recs += _more_boxes(o)
        classes = {c.get("class") for c in o["c"]}
        f = _fsm(o, ("Bench Control",))
        if f:
            # a bench: its place, the Knight's offset sitting, its prompt's place; its Lit and Light sprites' groups
            # (they glow with the Knight near); then its own trigger (Rest) and its Detect Range's
            var = lambda k, dflt: (f.get("vars", {}).get(k) or [0, dflt])[1]
            adj = var("Adjust Vector", [0, 0, 0])
            kids = {by_id[c]["name"]: by_id[c] for c in o.get("children", [])}
            pm = kids.get("Prompt Marker")
            g_lit = new_group(kids["Lit"]["id"]) if "Lit" in kids else 0
            g_light = new_group(kids["Light"]["id"]) if "Light" in kids else 0
            box = _trigger(o) or (0, 0, 0, 0)
            dr = _trigger(kids["Detect Range"]) if "Detect Range" in kids else None
            recs.append(rec(ENT_OBJ, OK_BENCH, (o["pos"][0], o["pos"][1], adj[0], adj[1]),
                            (pm["pos"][0] if pm else o["pos"][0], pm["pos"][1] if pm else o["pos"][1] + 3, 0, 0),
                            group=g_lit, group2=g_light, s0=strings.id(o["name"])))
            recs.append(rec(ENT_BOX, 0, box))
            recs.append(rec(ENT_BOX, 0, dr or box))
        en = _enemy(o, by_id, persist, name, d, strings, owners) or _props(d, o, by_id, rooms, strings, persist, sprites, owners, name)
        if en:
            recs += en
        br = next((c for c in o["c"] if c.get("class") == "Breakable" and c.get("v") is not None), None)
        if br is not None:
            v = br["v"]
            z = o["pos"][2]
            hits = _hit_boxes(o, by_id)
            if hits and v.get("inertForegroundThreshold", -1) <= z <= v.get("inertBackgroundThreshold", 1):
                g = new_group(o["id"])
                rem = [r[1] for r in v.get("remnantParts") or [] if r and r[1] in by_id]
                g2 = 0
                if rem:
                    ngroups[0] += 1
                    g2 = ngroups[0]
                    for r in rem:
                        subtree(r, g2)
                c0, cn = coll.subtree_colliders(d, owners, o["id"]) if owners else (0, 0)
                if rem and owners and any(coll.subtree_colliders(d, owners, r)[1] for r in rem):
                    print("ents: %s: remnant colliders not handled" % o["path"])
                debris = [r[1] for r in v.get("debrisParts") or [] if r]
                pieces = _pieces(d, o, debris, by_id, sprites)
                sign = -1.0 if o.get("lscale", [1])[0] < 0 else 1.0
                persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
                if v.get("hitEventReciever"):
                    receivers.append((len(recs), v["hitEventReciever"][1]))
                recs.append(rec(ENT_OBJ, OK_BREAKABLE, (o["pos"][0], o["pos"][1], o["pos"][2], 0),
                                (v.get("flingSpeedMin", 10), v.get("flingSpeedMax", 17), v.get("angleOffset", -60) * sign,
                                 len(pieces)), a=c0, group=g, group2=g2,
                                persist=persist.id(name, o["path"]) if persistent else NO_PERSIST, s0=cn, s1=len(hits)))
                recs += [rec(ENT_BOX, fl, box=b) for b, fl in hits]
                recs += pieces
        dh = [c for c in o["c"] if c.get("class") == "DamageHero"]
        if dh and not classes & {"HealthManager", "StalactiteControl"}:
            # a hazard (spikes, acid): its colliders' outlines, as boxes or as shapes after it
            v = dh[0].get("v") or {}
            for c in o["c"]:
                if c["type"] not in ("BoxCollider2D", "PolygonCollider2D") or not c.get("v") or not c["v"].get("m_Enabled", 1):
                    continue
                pts = _shape(o, c)
                if not pts:
                    continue
                xs, ys = [q[0] for q in pts], [q[1] for q in pts]
                box = (min(xs), min(ys), max(xs), max(ys))
                if _is_axis_box(pts):
                    recs.append(rec(ENT_DAMAGE, box=box, p=(v.get("hazardType", 1), v.get("damageDealt", 1), 0, 0)))
                else:
                    pts = pts[:8]
                    nrec = (len(pts) + 3) // 4
                    recs.append(rec(ENT_DAMAGE, box=box, p=(v.get("hazardType", 1), v.get("damageDealt", 1), len(pts), 0), a=nrec))
                    for i in range(nrec):
                        q = pts[4 * i:4 * i + 4] + [pts[-1]] * (4 - len(pts[4 * i:4 * i + 4]))
                        recs.append(rec(ENT_SHAPE, box=(q[0][0], q[0][1], q[1][0], q[1][1]), p=(q[2][0], q[2][1], q[3][0], q[3][1])))
        for c in o["c"]:
            cls, v = c.get("class"), c.get("v") or {}
            if cls == "CameraLockArea":
                box = _trigger(o)
                if box is None:
                    continue
                fl = (CL_PREVENT_UP if v.get("preventLookUp") else 0) | (CL_PREVENT_DOWN if v.get("preventLookDown") else 0) | \
                    (CL_MAX_PRIORITY if v.get("maxPriority") else 0)
                recs.append(rec(ENT_CAMLOCK, fl, box, (v["cameraXMin"], v["cameraYMin"], v["cameraXMax"], v["cameraYMax"])))
                recs += _more_boxes(o)
            elif cls == "TransitionPoint":
                box = _trigger(o)
                if box is None:
                    continue
                fl = (G_DOOR if v.get("isADoor") else 0) | (G_ENTER_RIGHT if v.get("alwaysEnterRight") else 0) | \
                    (G_ENTER_LEFT if v.get("alwaysEnterLeft") else 0) | (G_DONT_WALK_OUT if v.get("dontWalkOutOfDoor") else 0) | \
                    (G_NON_HAZARD if v.get("nonHazardGate") else 0) | (G_HARD_LAND if v.get("hardLandOnExit") else 0)
                target = v.get("targetScene") or ""
                ti = rooms.index(target) if target in rooms else 0xFFFF
                # (its respawn marker: a component; the record of its object)
                ref = v.get("respawnMarker")
                mid = ref[1] if isinstance(ref, (list, tuple)) else None
                marker = next((marker_index[oo["id"]] for oo in objs for cc in oo["c"] if cc.get("pid") == mid and
                               oo["id"] in marker_index), -1)
                off = v.get("entryOffset") or {"x": 0, "y": 0}
                recs.append(rec(ENT_GATE, fl, box, (float(v.get("entryDelay", 0)), off["x"], off["y"], marker + 1), a=ti,
                                s0=strings.id(o["name"]), s1=strings.id(v.get("entryPoint") or "")))
                recs += _more_boxes(o)
                # (the gate's own place, where the Knight comes in: a box record flagged 1, not part of its trigger)
                recs.append(rec(ENT_BOX, 1, (o["pos"][0], o["pos"][1], o["pos"][0], o["pos"][1])))
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
                recs += _more_boxes(o)
    # (an object that sends HIT to another: that one's record + 1 in its box's y1)
    for i, target in receivers:
        if target in rec_of:
            b = bytearray(recs[i])
            struct.pack_into("<f", b, 20, rec_of[target] + 1)
            recs[i] = bytes(b)
        else:
            print("ents: %s: HIT receiver %d has no record" % (name, target))
    assert ngroups[0] < MAX_GROUPS, ngroups[0]
    assert len(recs) <= 448, len(recs)   # (src/game.h: MAX_ENTS)
    return recs, groups
