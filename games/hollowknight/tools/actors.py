"""The game's characters and effects (2D Toolkit): their sprite frames at the size they are seen, and their animation
clips. Each sprite frame becomes a texture like the scenery's; pack.py writes the frames' placements (SPR) and the
clips (CLIP)."""
import numpy as np
from PIL import Image
import unity, tk2d, scene, art, text

# the Knight's clips the game uses (from the Knight's library, resources.assets)
KNIGHT = ("resources.assets", 20600, [
    "Idle", "Idle Hurt", "Idle Wind", "Run", "Run To Idle", "Walk", "Turn", "TurnToIdle", "Airborne", "Fall", "Land",
    "HardLand", "Dash", "Dash To Idle", "Dash Effect", "Slash", "SlashAlt", "UpSlash", "DownSlash", "SlashEffect",
    "SlashEffectAlt", "UpSlashEffect", "DownSlashEffect", "LookUp", "LookUpEnd", "LookUpToIdle", "LookDown",
    "LookDownEnd", "LookDownToIdle", "Recoil", "Stun", "Death", "Death Head Normal", "Spike Death",
    "Spike Death Antic", "Hazard Respawn", "Respawn Wake", "Focus", "Focus Get", "Focus Get Once", "Focus End",
    "Fireball Antic", "Fireball1 Cast", "Sit", "Sit Lean", "Sit Idle", "Sitting Asleep", "Sit Fall Asleep", "Wake",
    "Wake To Sit", "Get Off", "Sit Map Open", "Sit Map Close", "Map Open", "Map Idle", "Map Walk", "Map Turn",
    "Map Away", "Map Update", "Enter", "Exit", "Exit Door To Idle", "TurnToBG", "TurnFromBG", "Roar Lock",
    "Prostrate", "Prostrate Rise", "Wake Up Ground", "Collect Normal 1", "Collect Normal 2", "Collect Normal 3",
    "Collect Magical 1", "Collect Magical 2", "Collect Magical 3", "Collect Magical Fall", "Collect Magical Land",
    "Collect Heart Piece", "Collect Heart Piece End", "Collect StandToIdle", "GetUpToIdle", "Death Head Cracked",
])

# enemy kinds: name, the FSM (or component) that runs them, their library, the C code for that FSM, and the clips each
# role plays (src/enemy.c: R_*); the corpse's from its library (by default the enemy's)
ROLES = ["IDLE", "TURN", "WALK", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "DEATH_AIR", "DEATH_LAND"]
ENEMY_KINDS = [
    ("crawler", "Crawler", ("sharedassets6.assets", 1113), "CRAWLER", {"WALK": "Walk", "TURN": "Turn"}),
    ("buzzer", "chaser", ("sharedassets6.assets", 1150), "BUZZER", {"IDLE": "Idle"}),
    ("shade", None, ("resources.assets", 22801), "SHADE", {}),
    ("tiktik", "Climber", ("sharedassets37.assets", 146), "CLIMBER", {"WALK": "Walk", "A1": "Stun"}),
    ("gruzzer", "Bouncer Control", ("sharedassets40.assets", 208), "BOUNCER", {"A1": "Fly"}),
    ("aspid", "spitter", ("sharedassets39.assets", 104), "SPITTER", {"A1": "Fly", "A2": "TurnToFly", "A3": "Fire Long"}),
    ("baldur", "Roller", ("sharedassets50.assets", 111), "ROLLER", {"IDLE": "Idle", "A1": "Start", "A2": "Roll", "A3": "Stop"}),
    ("elderbaldur", "Blocker Control", ("sharedassets50.assets", 109), "BLOCKER",
     {"IDLE": "Idle", "A1": "Closed", "A2": "Open", "A3": "Close1", "A4": "Close2", "A5": "Shoot Antic", "A6": "Shoot CD",
      "A7": "Hit", "A8": "Death Stun", "DEATH_LAND": "Death"}),
    ("husk", "Zombie Swipe", ("sharedassets37.assets", 149), "HUSK",
     {"IDLE": "Idle", "TURN": "Turn", "WALK": "Walk", "A1": "Attack Anticipate", "A2": "Attack Lunge", "A3": "Attack Cooldown"}),
    ("barger", "Zombie Swipe", ("sharedassets40.assets", 198), "HUSK",
     {"IDLE": "Idle", "TURN": "Turn", "WALK": "Walk", "A1": "Attack Anticipate", "A2": "Attack Lunge", "A3": "Attack Cooldown"}),
    ("hornhead", "Zombie Swipe", ("sharedassets40.assets", 192), "HUSK",
     {"IDLE": "Idle", "TURN": "Turn", "WALK": "Walk", "A1": "Attack Anticipate", "A2": "Attack Lunge", "A3": "Attack Cooldown"}),
    ("mossrunner", "Zombie Swipe", ("sharedassets128.assets", 192), "HUSK",
     {"IDLE": "Idle", "TURN": "Turn", "WALK": "Walk", "A1": "Attack Anticipate", "A2": "Attack Lunge", "A3": "Attack Cooldown"}),
    ("leaper", "Zombie Leap", ("sharedassets57.assets", 82), "LEAPER",
     {"IDLE": "Idle", "TURN": "Turn", "WALK": "Walk", "A1": "Attack", "A2": "Land"}),
    # (its own code plays its clips by name: CLIP_GUARD_*)
    ("guard", "Zombie Guard", ("sharedassets58.assets", 57), "GUARD", {"IDLE": "Idle"},
     ["Dormant", "Wake", "Walk", "Run", "Stop Run", "Stop Walk", "Turn", "Anticipate", "Startle", "Stomp Antic",
      "Stomp Land", "Attack2", "Swipe", "Stomp Jump"]),
    ("fk", "FalseyControl", ("sharedassets47.assets", 9), "FK", {"IDLE": "Idle"},
     ["Jump Antic", "Land", "Jump", "Attack Antic", "Turn", "Jump Attack Up", "Jump Attack Hit 1", "Jump Attack Hit 2",
      "Jump Attack Hit 3", "Attack", "Attack Recover", "Blank", "Run Antic", "Run", "Stun Roll", "Stun Roll End",
      "Stun Open", "Stun Hit", "Stun Recover", "Rage", "Death Fall", "Death Land", "Death Head 1", "Death Head 2",
      "Death Spaz", "Body", "Stun Opened"]),
    ("fkhead", "Health Check", ("sharedassets47.assets", 9), "FKHEAD", {"IDLE": "Head Idle"}, ["Head Hit", "Head Spaz"]),
    # (Aspid Mother and her young)
    ("hatcher", "Hatcher", ("sharedassets57.assets", 79), "HATCHER", {"A1": "Fly", "A2": "Fire"}),
    ("hatchling", "Control", ("resources.assets", 20637), "HATCHLING", {"A1": "Fly"}),
    ("slug", "Control", ("sharedassets46.assets", 58), "SLUG",
     {"IDLE": "Idle Up", "A1": "Idle ToDown", "A2": "Idle ToUp", "A3": "Startle", "A4": "Run", "A5": "Bounce"}),
    # (Gruz Mother; her corpse's and its burster's clips are hers too)
    ("gfly", "Big Fly Control", ("sharedassets32.assets", 765), "GFLY", {"IDLE": "Sleep"},
     ["Wake", "Fly", "Charge Antic", "Charge", "Charge Recover", "Slam Down", "Slam Up", "Slam End", "Death", "Fall",
      "Wiggle", "Stop", "Gurgle Once", "Gurgle Loop", "Burst"]),
    # (Greenpath: Mosscreep, the birds, Fool Eater, Volatile Mosskin)
    ("mosswalker", "Moss Walker", ("sharedassets128.assets", 222), "MOSSWALKER",
     {"WALK": "Walk", "TURN": "Turn", "A1": "Shake", "A2": "Appear", "A3": "Bury"}),
    ("pigeon", "Pigeon", ("sharedassets128.assets", 206), "PIGEON", {"A1": "Idle 01", "A2": "Idle 02", "A3": "Idle 03", "A4": "Fly"}),
    ("planttrap", "Plant Trap Control", ("sharedassets128.assets", 202), "PLANTTRAP",
     {"IDLE": "Idle", "A1": "Snap Ready", "A2": "Snap", "A3": "Retract", "DEATH_AIR": "Death"}),
    ("shaker", "Fungus Zombie Attack", ("sharedassets128.assets", 220), "SHAKER",
     {"IDLE": "Idle", "WALK": "Walk", "TURN": "Turn", "A1": "Attack"}),
    # (Squit, Obble, Moss Charger)
    ("mosquito", "Mozzie", ("sharedassets27.assets", 795), "MOSQUITO",
     {"IDLE": "Idle", "A1": "TurnToIdle", "A2": "Startle", "A3": "Attack Antic", "A4": "Attack"}),
    ("fatfly", "Fatty Fly Attack", ("sharedassets147.assets", 98), "FATFLY", {"IDLE": "Fly", "A1": "Attack"}),
    ("mosscharger", "Mossy Control", ("sharedassets139.assets", 37), "MOSSCHARGER",
     {"A1": "Appear", "A2": "Charge", "A3": "Disappear", "A4": "Stun", "A5": "Get Up", "A6": "TurnRun", "A7": "Escape"}),
    # (Moss Knight: its code plays its clips by name, CLIP_MOSSKNIGHT_*)
    ("mossknight", "Moss Knight Control", ("sharedassets149.assets", 104), "MOSSKNIGHT",
     {"IDLE": "Idle", "WALK": "Walk", "TURN": "Turn"},
     ["Shield Front", "Shield Front Bump", "Shield Top", "Shield Top Bump", "Unshield Front", "Unshield Top", "Slash",
      "Slash Quick", "Slash Effect", "Slash Effect Quick", "Slash 2", "Slash 2 Effect", "Slash End", "Evade", "Shoot",
      "Shoot End", "Wake", "Dormant 1", "Dormant 2", "Shake"]),
]
ENEMY_CORPSE_LIBS = {}   # (name -> its corpse's library, if not its own)


def enemy_kind_id(name):
    return 1 + [k[0] for k in ENEMY_KINDS].index(name)


def _enemy_actors():
    out = {}
    for name, fsm, (f, pid), code, roles, *extra in ENEMY_KINDS:
        if name in ("crawler", "buzzer"):
            out[name] = (f, pid, None)
            continue
        if name == "shade":
            continue
        have = {unity.S(c["name"]) for c in tk2d.animation(f, pid)["clips"]}
        out[name] = (f, pid, sorted((set(roles.values()) | set(extra[0] if extra else []) | {"Death Air", "Death Land"}) & have))
    return out


def kind_table(clip_ids):
    """src/data.h's KIND_TABLE: each kind's FSM code and its roles' clips (-1: none)"""
    rows = []
    for name, fsm, lib, code, roles, *extra in ENEMY_KINDS:
        ids = []
        for r in ROLES:
            clip = roles.get(r) or {"DEATH_AIR": "Death Air", "DEATH_LAND": "Death Land"}.get(r)
            ids.append(clip_ids.get(clip_id(name, clip), -1) if clip else -1)
        rows.append("{EF_%s, {%s}}" % (code, ", ".join(str(i) for i in ids)))
    return "{{0}, " + ", ".join(rows) + "}"


ACTORS = {"knight": KNIGHT,
          "georock": ("sharedassets6.assets", 1149, None),
          "chest": ("sharedassets6.assets", 1148, None),
          # the HUD: drawn by its own camera, 11.48 pixels a unit (scenes: K0), masks at 0.7135
          "hud": ("resources.assets", 20665, ["Health Empty", "Health Idle", "Health Break", "Health Refill", "Health Appear",
                                              "Blue Appear", "Blue Idle", "Blue Break", "Blue Break Fast", "HUD Frame", "HUD Frame Idle",
                                              "HUD Frame CrackAppear", "HUD Frame Cracked", "Coin Appear", "Coin Idle",
                                              "Coin Get", "Soul Burst"]),
          "liquid": ("resources.assets", 20843, None),
          "geo": ("resources.assets", 23266, ["Small Idle", "Small Air", "Med Idle", "Med Air", "Large Idle", "Large Air"]),
          # the dialogue box (on the HUD, a third bigger with its text) and the prompt markers
          "dialogue": ("resources.assets", 20724, ["Arrow Up", "Arrow Down", "Stop Up", "Stop Down", "Fleur Top Up",
                                                   "Fleur Top Down", "Fleur Bot Up", "Fleur Bot Down"]),
          "prompt": ("resources.assets", 23333, ["Up", "Down", "Blank"]),
          # the shade (Hollow Shade, and the Hero Death's rising one)
          "shade": ("resources.assets", 22801, ["Idle", "Startle", "Fly", "TurnToFly", "TurnToIdle", "Slash Antic", "Slash",
                                                 "Slash CD", "Slash Effect", "Cast Antic", "Cast Charge", "Cast",
                                                 "Retreat Start", "Retreat End", "Death Start", "Death", "Depart",
                                                 "Appear", "Fireball", "Fireball End", "Cast Ring"])}
ACTORS.update(_enemy_actors())
# battle gates (BG Control: the plain ones and the bone ones)
ACTORS["bgate"] = ("sharedassets9.assets", 197, ["BG Opened", "BG Close 1", "BG Close 2", "BG Open", "BG Closed",
                                                 "Bone Gate Opened", "Bone Gate Close", "Bone Gate Closed", "Bone Gate Open"])
# lifeblood cocoons and their scuttlers (HealthCocoon, ScuttlerControl); effects (splats)
ACTORS["cocoon"] = ("sharedassets6.assets", 1092, ["Cocoon Idle", "Cocoon Sweat", "Scuttler Land", "Scuttler Run"])
ACTORS["fx"] = ("resources.assets", 21298, ["Splat", "Splat2"])
# Vengeful Spirit (Fireball Top's blast, Fireball: its ball, its end, its impact on a wall)
ACTORS["fireball"] = ("resources.assets", 23262, ["Blast", "Ball", "Ball End", "Fireball Wall Impact"])
# water drips (WaterDrip)
ACTORS["drip"] = ("sharedassets6.assets", 1095, ["Idle", "Drip", "Fall", "Impact"])
PROP_ACTOR = {}   # (props' libraries: their actors)


def add_props(rooms):
    """The rooms' props (ents.prop: sprites that only show) with an animator: their libraries and default clips as
    actors ("prop0", ...)."""
    import ents
    libs = {}
    for r in rooms:
        d = unity.scene(r)
        by = {o["id"]: o for o in d["objects"]}
        for o in d["objects"]:
            if o["active"]:
                pr = ents.prop(d, o, by)
                if pr and pr[0]:
                    libs.setdefault(pr[0], set()).add(pr[1])
    for i, (lib, names) in enumerate(sorted(libs.items())):
        PROP_ACTOR[lib] = "prop%d" % i
        ACTORS["prop%d" % i] = (lib[0], lib[1], sorted(names))


# enemies' shots (EnemyBullet)
ACTORS["bullet"] = ("sharedassets32.assets", 745, ["Idle", "Impact", "Shockwave Spurt"])

# sprites of prefab objects the game draws itself: name -> (file, prefab, object, resolution)
NAMED = {
    "HERO_LIGHT": ("resources.assets", 3916, "Knight/HeroLight", 1.0),
    # (these at a scale of their own: the box's, with its text's; the shadow's x)
    "DIALOGUE_BACKBOARD": ("resources.assets", 4446, "DialogueManager/DialogueBox/backboard", "hud", 2.325 * text.TEXT_K),
    "PROMPT_SHADOW": ("resources.assets", 8342, "Arrow Prompt New/Shadow", 1.0, 0.8),
    "CORPSE_NAIL": ("resources.assets", 6648, "Corpse Nail Hero", 1.0),
    # (the False Knight's barrels, and its staff flung as it dies: a scene's object)
    "FK_BARREL": ("sharedassets48.assets", 61, "Falling Barrel", 1.0),
    "FK_STAFF": ("scene:Crossroads_10_boss", 0, "Battle Scene/False Knight New/Staff", 1.0),
}


def named_sprites(sprites):
    """Registers NAMED in sprites (ents.Sprites) -> {name: sprite id}."""
    out = {}
    for name, (path, gid, opath, res, *scale) in NAMED.items():
        d = unity.scene(path[6:]) if path.startswith("scene:") else unity.prefab(path, gid)
        o = next(o for o in d["objects"] if o["path"] == opath)
        v = next(c["v"] for c in o["c"] if c["type"] == "SpriteRenderer")
        m = np.array(o["m3"]).reshape(3, 3)
        k = scale[0] if scale else float(max(np.hypot(m[0, 0], m[1, 0]), np.hypot(m[0, 1], m[1, 1])))
        out[name] = sprites.id(d, v["m_Sprite"], k, HUD_K / K0 if res == "hud" else res)
    return out
# clips drawn bigger than their sprites (the object's scale): their frames are kept that much finer
DRAWN_SCALE = {"Arrow Up": 1.3, "Arrow Down": 1.3, "Stop Up": 1.3, "Stop Down": 1.3, "SlashEffect": 1.645, "SlashEffectAlt": 1.422, "UpSlashEffect": 1.4, "DownSlashEffect": 1.28,
               "Blast": 1.4, "Ball": 1.45, "Ball End": 1.45, "Fireball Wall Impact": 2.0,
               "Small Idle": 1.5, "Small Air": 1.5, "Med Idle": 1.5, "Med Air": 1.5, "Large Idle": 1.5, "Large Air": 1.5}
K0 = scene.FOCAL / (0.004 - scene.CAMZ)   # screen pixels a unit, where actors are (z near 0)
HUD_K = (scene.VIEW_H / 2) / 8.7107        # the HUD's (its orthographic camera)
# (actor, clip): drawn bigger or smaller than their sprites
ACTOR_SCALE = {"hud": HUD_K / K0, "liquid": 1.4 * HUD_K / K0, "dialogue": text.TEXT_K * HUD_K / K0}
HUD_MASK = 0.7135


def clip_id(actor, name):
    return "".join(c if c.isalnum() else "_" for c in ("CLIP_%s_%s" % (actor, name)).upper())


def build():
    """-> (sprites, clips). sprites: [{key, job, lx, ty, tu, tv}] (texture top-left in local units, units a texel);
    clips: [{id, fps, wrap, loop, frames: [(sprite, trigger)]}]."""
    sprites, index, clips = [], {}, []
    for actor, (path, pid, names) in ACTORS.items():
        lib = tk2d.animation(path, pid)
        byname = {unity.S(c["name"]): c for c in lib["clips"]}
        for name in names or [unity.S(c["name"]) for c in lib["clips"]]:
            c = byname[name]
            frames = []
            for f in c["frames"]:
                cpath, cpid = tk2d.ref_file(path, f["spriteCollection"])
                key = (cpath, cpid, f["spriteId"], actor, name.startswith("Health") or name.startswith("Blue"))
                if key not in index:
                    col = tk2d.collection(cpath, cpid)
                    img, lx, ty, upp, (wu, hu) = tk2d.sprite_image(cpath, col, f["spriteId"])
                    k = K0 * DRAWN_SCALE.get(name, 1.0) * ACTOR_SCALE.get(actor, 1.0)
                    if actor == "hud" and (name.startswith("Health") or name.startswith("Blue")):
                        k *= HUD_MASK
                    w = max(1, round(img.width * upp * k))
                    h = max(1, round(img.height * upp * k))
                    small = img.resize((w, h), Image.BOX)
                    arr = np.asarray(small).copy()
                    index[key] = len(sprites)
                    sprites.append({"key": key, "job": art.ImageJob(arr, "%s/%s" % (actor, name)),
                                    "lx": lx, "ty": ty, "tu": wu / w, "tv": hu / h,
                                    "col": _collider(col["spriteDefinitions"][f["spriteId"]])})
                frames.append((index[key], bool(f.get("triggerEvent"))))
            clips.append({"id": clip_id(actor, name), "fps": float(c["fps"]), "wrap": int(c["wrapMode"]),
                          "loop": int(c.get("loopStart", 0)), "frames": frames})
            # (the clip's new frames share one palette: theirs together)
            new = sorted({i for i, _ in frames if sprites[i]["job"].pal is None})
            if new:
                pal = art.joint_palette([sprites[i]["job"].arr for i in new])
                for i in new:
                    sprites[i]["job"].pal = pal
    return sprites, clips


def _collider(sd):
    """A frame's collider as 2D Toolkit sets it when the frame shows: None (left as it is), ("none",), ("box", cx, cy, hx,
    hy) or ("poly", [(x, y)...]); sprite units"""
    if sd.get("physicsEngine") != 1:
        return None
    t = sd.get("colliderType")
    if t == 1:
        return ("none",)
    if t == 2:
        v = sd["colliderVertices"]
        return ("box", v[0]["x"], v[0]["y"], abs(v[1]["x"]), abs(v[1]["y"]))
    if t == 3 and sd.get("polygonCollider2D"):
        pts = [(q["x"], q["y"]) for q in sd["polygonCollider2D"][0]["points"]]
        return ("poly", pts[:8])
    return None


def unity_sprites(keys):
    """Unity sprites objects show as actors (ents.Sprites: debris...) -> sprite records like build()'s; each at its
    object's scale."""
    out = []
    for level, exts, fid, pid, scale, res in keys:
        if level == "tk2d":
            path, col, name = exts, fid, pid
            c = tk2d.collection(path, col)
            sid = next(i for i, dd in enumerate(c["spriteDefinitions"]) if unity.S(dd["name"]) == name)
            img, lx, ty, upp, (wu, hu) = tk2d.sprite_image(path, c, sid)
            w, h = max(1, round(img.width * upp * K0 * scale * res)), max(1, round(img.height * upp * K0 * scale * res))
            arr = np.asarray(img.resize((w, h), Image.BOX)).copy()
            out.append({"key": (path, col, sid), "job": art.ImageJob(arr, "tk2d/%s" % name), "lx": lx * scale,
                        "ty": ty * scale, "tu": wu * scale / w, "tv": hu * scale / h})
            continue
        s = unity.sprite(level, list(exts), fid, pid)
        img = unity.sprite_image(s)
        wu, hu = s.w / s.ppu * scale, s.h / s.ppu * scale
        w, h = max(1, round(wu * K0 * res)), max(1, round(hu * K0 * res))
        arr = np.asarray(img.resize((w, h), Image.BOX)).copy()
        out.append({"key": (level, fid, pid), "job": art.ImageJob(arr, "piece/%s" % s.name),
                    "lx": -s.px * wu, "ty": (1 - s.py) * hu, "tu": wu / w, "tv": hu / h})
    return out


def title_fleurs(sprites):
    """The large area title's fleurs (Fleur Top, Fleur Bot: their Appear clips): their last frames as HUD sprites, and
    each frame's bounds (HUD units, from the fleur's place: what of the last frame it shows) -> {name: value}"""
    lib = tk2d.animation("resources.assets", 23444)
    byname = {unity.S(c["name"]): c for c in lib["clips"]}
    out = {}
    for name, clip in (("TOP", "Top Appear"), ("BOT", "Bottom Appear")):
        frames = byname[clip]["frames"]
        cpath, cpid = tk2d.ref_file("resources.assets", frames[-1]["spriteCollection"])
        col = tk2d.collection(cpath, cpid)
        last = frames[-1]["spriteId"]
        out["SPRITE_FLEUR_" + name] = sprites.tk2d(cpath, cpid, unity.S(col["spriteDefinitions"][last]["name"]), 1.0,
                                                    HUD_K / K0)
        rects = []
        for f in frames:
            img, lx, ty, upp, (wu, hu) = tk2d.sprite_image(cpath, col, f["spriteId"])
            rects.append("{%.3ff, %.3ff, %.3ff, %.3ff}" % (lx, ty - hu, lx + wu, ty))
        out["FLEUR_%s_RECTS" % name] = "{%s}" % ", ".join(rects)
        out["FLEUR_FRAMES"] = len(frames)
        out["FLEUR_FPS"] = "%.1ff" % byname[clip]["fps"]
    return out
