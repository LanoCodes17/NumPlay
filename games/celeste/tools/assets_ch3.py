"""Celestial Resort (chapter 3): what src/ch3.c's entities draw.

VARIANTS: assets that depend on an entity's attribute, {name: (attribute, {value: assets})}: a room
loads only the sprites its entities use (the texture cache is 48 KB). entities.needs() adds them."""

CLUTTER = ["fam:objects/resortclutter/red_", "fam:objects/resortclutter/green_", "fam:objects/resortclutter/yellow_"]

ASSETS = {
    "redBlocks": ["fam:objects/resortclutter/red_"],
    "greenBlocks": ["fam:objects/resortclutter/green_"],
    "yellowBlocks": ["fam:objects/resortclutter/yellow_"],
    "clutterCabinet": ["bank:clutterCabinet"],
    # the absorbed clutter flies in from offscreen, in the switch's color
    "colorSwitch": ["bank:clutterSwitch", "tex:objects/resortclutter/icon_red", "tex:objects/resortclutter/icon_green",
                    "tex:objects/resortclutter/icon_yellow"] + CLUTTER,
    "clutterDoor": ["bank:ghost_door"],
    "oshirodoor": ["bank:ghost_door"],
    "resortLantern": ["tex:objects/resortLantern/holder", "fam:objects/resortLantern/lantern"],
    "cobweb": [],
    "triggerSpikesUp": ["fam:danger/triggertentacle/wiggle_v", "fam:danger/dustcreature/base"],
    "triggerSpikesDown": ["fam:danger/triggertentacle/wiggle_v", "fam:danger/dustcreature/base"],
    "triggerSpikesLeft": ["fam:danger/triggertentacle/wiggle_h", "fam:danger/dustcreature/base"],
    "triggerSpikesRight": ["fam:danger/triggertentacle/wiggle_h", "fam:danger/dustcreature/base"],
    "door": [],
    "trapdoor": ["bank:trapdoor"],
    "sinkingPlatform": ["tex:objects/woodPlatform/default", "tex:objects/woodPlatform/cliffside"],
    "movingPlatform": ["tex:objects/woodPlatform/default", "tex:objects/woodPlatform/cliffside"],
    "clothesline": [],
    "water": [],
    "waterfall": [],
    # src/ch3.c plays oshiro_boss from a table and loads an animation when first drawn: the room loads "idle"
    # (and the banks' first animations)
    "friendlyGhost": ["bank:oshiro_boss", "bank:oshiro_boss_lightning"]
                     + ["tex:characters/oshiro/boss%02d" % i for i in range(13, 21)],
    # (the suite's cutscene breaks it: the sprite "glass")
    "resortmirror": ["tex:objects/mirror/resortframe", "tex:objects/mirror/glassbg", "tex:objects/mirror/glassbreak09",
                     "tex:objects/mirror/glassfg", "lazy:bank:glass"],
    "picoconsole": ["tex:objects/pico8Console"],
    "key": ["bank:key"],
    "lockBlock": [],
    "blockField": [],
    "killbox": [],
    "resortRoofEnding": ["tex:decals/3-resort/roofCenter", "tex:decals/3-resort/roofCenter_b",
                         "tex:decals/3-resort/roofCenter_c", "tex:decals/3-resort/roofCenter_d",
                         "tex:decals/3-resort/roofEdge", "tex:decals/3-resort/roofEdge_d",
                         "tex:decals/3-resort/roofCenter_snapped_a", "tex:decals/3-resort/roofCenter_snapped_b",
                         "tex:decals/3-resort/roofCenter_snapped_c", "bank:oshiro"],
}

# the npcs by their names: Theo's grates; the rooftop's Oshiro turns into the boss
NPC_ASSETS = {
    "theo_03": ["tex:scenery/grate"],
    "oshiro_03_rooftop": ["bank:oshiro_boss"],
}

VARIANTS = {
    "door": ("type", {"wood": ["bank:door"], "metal": ["bank:metaldoor"]}),
    "lockBlock": ("sprite", {"wood": ["bank:lockdoor_wood"], "temple_a": ["bank:lockdoor_temple_a"],
                             "temple_b": ["bank:lockdoor_temple_b"], "moon": ["bank:lockdoor_moon"]}),
}

