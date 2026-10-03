"""The game's characters and effects (2D Toolkit): their sprite frames at the size they are seen, and their animation
clips. Each sprite frame becomes a texture like the scenery's; pack.py writes the frames' placements (SPR) and the
clips (CLIP)."""
import numpy as np
from PIL import Image
import unity, tk2d, scene, art

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
    "Collect Heart Piece", "Collect Heart Piece End", "Collect StandToIdle", "GetUpToIdle",
])

# enemies: their libraries (all their clips)
ACTORS = {"knight": KNIGHT,
          "crawler": ("sharedassets6.assets", 1113, None),
          "buzzer": ("sharedassets6.assets", 1150, None),
          "geo": ("resources.assets", 23266, ["Small Idle", "Small Air", "Med Idle", "Med Air", "Large Idle", "Large Air"])}

# sprites of prefab objects the game draws itself: name -> (file, prefab, object, resolution)
NAMED = {
    "HERO_LIGHT": ("resources.assets", 3916, "Knight/HeroLight", 1.0),
}


def named_sprites(sprites):
    """Registers NAMED in sprites (ents.Sprites) -> {name: sprite id}."""
    out = {}
    for name, (path, gid, opath, res) in NAMED.items():
        d = unity.prefab(path, gid)
        o = next(o for o in d["objects"] if o["path"] == opath)
        v = next(c["v"] for c in o["c"] if c["type"] == "SpriteRenderer")
        m = np.array(o["m3"]).reshape(3, 3)
        out[name] = sprites.id(d, v["m_Sprite"], float(max(np.hypot(m[0, 0], m[1, 0]), np.hypot(m[0, 1], m[1, 1]))), res)
    return out
# clips drawn bigger than their sprites (the object's scale): their frames are kept that much finer
DRAWN_SCALE = {"SlashEffect": 1.645, "SlashEffectAlt": 1.422, "UpSlashEffect": 1.4, "DownSlashEffect": 1.28,
               "Small Idle": 1.5, "Small Air": 1.5, "Med Idle": 1.5, "Med Air": 1.5, "Large Idle": 1.5, "Large Air": 1.5}
K0 = scene.FOCAL / (0.004 - scene.CAMZ)   # screen pixels a unit, where actors are (z near 0)


def clip_id(actor, name):
    return ("CLIP_%s_%s" % (actor, name)).upper().replace(" ", "_")


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
                key = (cpath, cpid, f["spriteId"])
                if key not in index:
                    col = tk2d.collection(cpath, cpid)
                    img, lx, ty, upp, (wu, hu) = tk2d.sprite_image(cpath, col, f["spriteId"])
                    k = K0 * DRAWN_SCALE.get(name, 1.0)
                    w = max(1, round(img.width * upp * k))
                    h = max(1, round(img.height * upp * k))
                    small = img.resize((w, h), Image.BOX)
                    arr = np.asarray(small).copy()
                    index[key] = len(sprites)
                    sprites.append({"key": key, "job": art.ImageJob(arr, "%s/%s" % (actor, name)),
                                    "lx": lx, "ty": ty, "tu": wu / w, "tv": hu / h})
                frames.append((index[key], bool(f.get("triggerEvent"))))
            clips.append({"id": clip_id(actor, name), "fps": float(c["fps"]), "wrap": int(c["wrapMode"]),
                          "loop": int(c.get("loopStart", 0)), "frames": frames})
    return sprites, clips


def unity_sprites(keys):
    """Unity sprites objects show as actors (ents.Sprites: debris...) -> sprite records like build()'s; each at its
    object's scale."""
    out = []
    for level, exts, fid, pid, scale, res in keys:
        s = unity.sprite(level, list(exts), fid, pid)
        img = unity.sprite_image(s)
        wu, hu = s.w / s.ppu * scale, s.h / s.ppu * scale
        w, h = max(1, round(wu * K0 * res)), max(1, round(hu * K0 * res))
        arr = np.asarray(img.resize((w, h), Image.BOX)).copy()
        out.append({"key": (level, fid, pid), "job": art.ImageJob(arr, "piece/%s" % s.name),
                    "lx": -s.px * wu, "ty": (1 - s.py) * hu, "tu": wu / w, "tv": hu / h})
    return out
