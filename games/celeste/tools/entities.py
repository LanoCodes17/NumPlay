"""What each Celeste entity needs from the art, for the converter.

ASSETS[name] lists what a room with that entity must have loaded:
  "bank:NAME"   a sprite from Sprites.xml (all its animations' frames)
  "fam:PATH"    a family of textures PATH00, PATH01... (or PATH itself)
  "tex:PATH"    one texture
Entities in DROP are left out of the rooms (sound-only, editor-only, music)."""

DROP = {
    "soundSource", "SummitBackgroundManager", "cutsceneNode",
}
DROP_TRIGGERS = {
    "musicFadeTrigger", "musicTrigger", "altMusicTrigger", "ambienceParamTrigger", "audioFadeTrigger",
}

# The game's own defaults for attributes a map leaves out (its constructors' data.Bool, Int, Float, Char and Attr calls),
# so data.bin holds what the game reads: FallingBlock's climbFall is true unless a map says otherwise.
DEFAULTS = {
    "spring": {"playerCanUse": True},
    "wallSpringLeft": {"playerCanUse": True},
    "wallSpringRight": {"playerCanUse": True},
    "fallingBlock": {"tiletype": "3", "climbFall": True},
    "fakeWall": {"tiletype": "3"},
    "dashBlock": {"tiletype": "3", "permanent": True, "canDash": True},
    "coverupWall": {"tiletype": "3"},
    "exitBlock": {"tileType": "3"},
    "conditionBlock": {"tileType": "3"},
    "jumpThru": {"texture": "default", "surfaceIndex": -1},
    "spikesUp": {"type": "default"},
    "spikesDown": {"type": "default"},
    "spikesLeft": {"type": "default"},
    "spikesRight": {"type": "default"},
    "switchGate": {"sprite": "block"},
    "door": {"type": "wood"},
    "lockBlock": {"sprite": "wood"},
    "templeGate": {"sprite": "default"},
    "dashSwitchH": {"sprite": "default"},
    "dashSwitchV": {"sprite": "default"},
    "moveBlock": {"canSteer": True},
    "gondola": {"active": True},
    "badelineBoost": {"lockCamera": True},
    "finalBoss": {"cameraLockY": True, "cameraPastY": 120.0},
    "fireBall": {"amount": 1, "speed": 1.0},
    "oshiroTrigger": {"state": True},
    "minitextboxTrigger": {"death_count": -1},
}
# Attributes read with data.Char: one character, else the default
CHARS = {("fallingBlock", "tiletype"), ("fakeWall", "tiletype"), ("dashBlock", "tiletype"), ("coverupWall", "tiletype"),
         ("exitBlock", "tileType"), ("conditionBlock", "tileType")}
# Attributes read with data.Enum: the enum's names, its default first. The game reads them in any case (Enum.TryParse
# ignoreCase) and falls back on the default; data.bin holds the name as the enum spells it.
_POSITION = ("NoEffect", "HorizontalCenter", "VerticalCenter", "TopToBottom", "BottomToTop", "LeftToRight", "RightToLeft")
_ENUMS = {
    "bird": {"mode": ("None", "ClimbingTutorial", "DashingTutorial", "DreamJumpTutorial", "SuperWallJumpTutorial",
                      "HyperJumpTutorial", "FlyAway", "Sleeping", "MoveToNodes", "WaitForLightningOff")},
    "bonfire": {"mode": ("Unlit", "Lit", "Smoking")},
    "trackSpinner": {"speed": ("Normal", "Slow", "Fast")},
    "clutterDoor": {"type": ("Green", "Red", "Yellow", "Lightning")},
    "colorSwitch": {"type": ("Green", "Red", "Yellow", "Lightning")},
    "moveBlock": {"direction": ("Left", "Right", "Up", "Down")},
    "templeGate": {"type": ("NearestSwitch", "CloseBehindPlayer", "CloseBehindPlayerAlways", "HoldingTheo", "TouchSwitches",
                            "CloseBehindPlayerAndTheo")},
    "zipMover": {"theme": ("Normal", "Moon")},
    "swapBlock": {"theme": ("Normal", "Moon")},
    "seekerStatue": {"hatch": ("Distance", "PlayerRightOfX")},
    "conditionBlock": {"condition": ("Key", "Button", "Strawberry")},
    "crushBlock": {"axes": ("Both", "Horizontal", "Vertical")},
    "bigWaterfall": {"layer": ("BG", "FG")},
    "cameraTargetTrigger": {"positionMode": _POSITION},
    "bloomFadeTrigger": {"positionMode": _POSITION},
    "lightFadeTrigger": {"positionMode": _POSITION},
    "cameraAdvanceTargetTrigger": {"positionModeX": _POSITION, "positionModeY": _POSITION},
    "windTrigger": {"pattern": ("None", "Left", "Right", "LeftStrong", "RightStrong", "LeftOnOff", "RightOnOff", "LeftOnOffFast",
                                "RightOnOffFast", "Alternating", "LeftGemsOnly", "RightCrazy", "Down", "Up", "Space")},
    "minitextboxTrigger": {"mode": ("OnPlayerEnter", "OnLevelStart", "OnTheoEnter")},
}


def attr(entity, k):
    """Attribute k of an entity as the game reads it (None: the game reads none, and the map has none)."""
    v = entity.attr.get(k)
    names = _ENUMS.get(entity.name, {}).get(k)
    if names:
        s = str(v) if v is not None else ""
        return next((n for n in names if n.lower() == s.lower()), names[0])
    dflt = DEFAULTS.get(entity.name, {}).get(k)
    if (entity.name, k) in CHARS and (v is None or len(str(v)) != 1):
        return dflt
    return dflt if v is None else v


SPIKE_TYPES = ("default", "outline", "cliffside", "reflection")


def spikes(direction):
    return ["fam:danger/spikes/%s_%s" % (t, direction) for t in SPIKE_TYPES]


ASSETS = {
    "player": [],
    # Blade/Dust Rotate/TrackSpinner (src/dust.c): blades outside the dust rooms (those get pack.py's DUST_TEX)
    "rotateSpinner": ["lazy:bank:templeBlade"],
    "trackSpinner": ["lazy:bank:templeBlade"],
    "spikesUp": spikes("up"),
    "spikesDown": spikes("down"),
    "spikesLeft": spikes("left"),
    "spikesRight": spikes("right"),
    "jumpThru": ["tex:objects/jumpthru/wood", "tex:objects/jumpthru/dream", "tex:objects/jumpthru/temple",
                 "tex:objects/jumpthru/templeB", "tex:objects/jumpthru/cliffside", "tex:objects/jumpthru/reflection",
                 "tex:objects/jumpthru/core", "tex:objects/jumpthru/moon"],
    "spring": ["fam:objects/spring/", "fam:objects/spring/white"],
    "wallSpringLeft": ["fam:objects/spring/", "fam:objects/spring/white"],
    "wallSpringRight": ["fam:objects/spring/", "fam:objects/spring/white"],
    "refill": ["fam:objects/refill/idle", "fam:objects/refill/flash", "tex:objects/refill/outline",
               "fam:objects/refillTwo/idle", "fam:objects/refillTwo/flash", "tex:objects/refillTwo/outline"],
    "strawberry": ["bank:strawberry", "bank:ghostberry", "bank:strawberrySeed", "bank:ghostberrySeed",
                   "bank:goldberrySeed"],
    "goldenBerry": ["bank:goldberry", "bank:goldghostberry", "bank:strawberry"],
    "memorialTextController": ["bank:goldberry", "bank:goldghostberry", "bank:strawberry"],
    "crumbleBlock": ["tex:objects/crumbleBlock/default", "tex:objects/crumbleBlock/outline",
                     "tex:objects/crumbleBlock/cliffside", "tex:objects/crumbleBlock/reflection",
                     "tex:objects/crumbleBlock/summit", "tex:objects/crumbleBlock/core",
                     "tex:objects/crumbleBlock/dream", "tex:objects/crumbleBlock/temple"],
    "zipMover": ["tex:objects/zipmover/block", "fam:objects/zipmover/light", "tex:objects/zipmover/cog",
                 "fam:objects/zipmover/innercog"],
    "cassetteBlock": ["tex:objects/cassetteblock/solid", "fam:objects/cassetteblock/pressed"],
    "cassette": ["bank:cassette", "lazy:bank:cassetteGhost"],
    "fallingBlock": [],
    "fakeWall": [],
    "dashBlock": [],
    "coverupWall": [],
    "exitBlock": [],
    "lightbeam": ["tex:util/lightbeam"],
    "checkpoint": ["fam:objects/checkpoint/bg/"],
    "bonfire": ["bank:campfire"],
    "flutterbird": ["bank:flutterBird"],
    "bird": ["bank:bird"],
    "npc": [],
    "wire": [],
    "lamp": ["tex:scenery/lamp"],
    "memorial": ["tex:scenery/memorial/memorial", "tex:scenery/memorial/memorial_dreaming"],
    "introCar": ["tex:scenery/car/body", "tex:scenery/car/wheels", "tex:scenery/car/pavement", "tex:scenery/car/barrier"],
    "introCrusher": [],
    "bridge": ["tex:scenery/bridge"],
    "hahaha": ["fam:characters/oldlady/ha"],
    # ForsakenCitySatellite: the dish, its computer, the code birds, and the heart they make
    "birdForsakenCityGem": ["tex:objects/citysatellite/dish", "tex:objects/citysatellite/light",
                            "tex:objects/citysatellite/computer", "tex:objects/citysatellite/computerscreen",
                            "fam:objects/citysatellite/computerScreenNoise", "tex:objects/citysatellite/computerscreenShine",
                            "fam:scenery/flutterbird/flap", "tex:collectables/heartGem/shape", "bank:heartgem0",
                            "lazy:bank:heartGemWhite", "tex:collectables/heartGem/orb"],
    "towerviewer": ["bank:lookout"],
    "touchSwitch": ["tex:objects/touchswitch/container", "fam:objects/touchswitch/icon"],
    "switchGate": ["tex:objects/switchgate/block", "fam:objects/switchgate/icon"],
    "blackGem": ["bank:heartgem0", "lazy:bank:heartgem1", "lazy:bank:heartgem2", "lazy:bank:heartGemGhost",
                 "lazy:bank:heartGemWhite", "tex:collectables/heartGem/orb"],
}

# NPC sprites by the npc attribute's prefix
NPC_ASSETS = {
    "granny": ["bank:granny"],
    "theo": ["bank:theo"],
    "oshiro": ["bank:oshiro"],
    "badeline": ["bank:badeline"],
}


def keep(name):
    return name not in DROP


def keep_trigger(name):
    return name not in DROP_TRIGGERS


NEEDS = {}   # name -> function(entity) -> assets that depend on its attributes (chapter files add them)
OWNER = {}   # name -> the chapter file (src/chN.c) that makes it


def needs(entity):
    """Assets one entity needs (the npc entity depends on which npc it is)."""
    out = list(ASSETS.get(entity.name, []))
    if entity.name in NEEDS:
        out += NEEDS[entity.name](entity)
    if entity.name == "npc":
        who = entity.get("npc", "")
        for k, v in NPC_ASSETS.items():
            if k in who:
                out += v
    return out


def direct_textures(tex):
    """Textures drawn straight from flash, needed everywhere."""
    for fam in ("characters/player/", "characters/player_no_backpack/", "characters/badeline/"):
        pass


# per-chapter asset lists (tools/assets_*.py), each with ASSETS and maybe NPC_ASSETS
def _load_chapter_assets():
    import glob
    import importlib.util
    import os
    for f in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets_*.py"))):
        spec = importlib.util.spec_from_file_location(os.path.basename(f)[:-3], f)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        ASSETS.update(getattr(mod, "ASSETS", {}))
        for k in getattr(mod, "ASSETS", {}):
            OWNER.setdefault(k, int(os.path.basename(f)[len("assets_ch"):-3]))
        NPC_ASSETS.update(getattr(mod, "NPC_ASSETS", {}))
        DROP.update(getattr(mod, "DROP", set()))
        NEEDS.update(getattr(mod, "NEEDS", {}))
        for name, (key, table) in getattr(mod, "VARIANTS", {}).items():   # {name: (attribute, {value: assets})}
            NEEDS[name] = (lambda key, table: lambda e: table.get(attr(e, key), []))(key, table)


_load_chapter_assets()
