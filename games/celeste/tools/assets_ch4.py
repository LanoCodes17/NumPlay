"""Golden Ridge (chapter 4): what src/ch4.c's entities draw.

VARIANTS: assets that depend on an entity's attribute, {name: (attribute, {value: assets})}, so a room
loads the green or the red booster, not both (the texture cache is 48 KB); entities.needs() adds
them."""

ASSETS = {
    "booster": ["tex:objects/booster/outline"],
    "moveBlock": ["tex:objects/moveBlock/base", "tex:objects/moveBlock/base_h", "tex:objects/moveBlock/base_v",
                  "tex:objects/moveBlock/button", "fam:objects/moveBlock/arrow", "tex:objects/moveBlock/x",
                  "fam:objects/moveBlock/debris"],
    "cloud": [],
    "ridgeGate": ["tex:objects/ridgeGate"],
    "whiteblock": ["tex:objects/whiteblock"],
    "gondola": ["bank:gondola", "tex:objects/gondola/top", "tex:objects/gondola/back", "tex:objects/gondola/cliffsideLeft",
                "tex:objects/gondola/cliffsideRight", "fam:objects/gondola/lever"],
    "cliffside_flag": ["fam:scenery/cliffside/flag"],
    "cliffflag": [],
    # the Snowball of a windAttackTrigger
    "windAttackTrigger": ["bank:snowball"],
}

# the npcs by their names: CS04_Gondola's clouds (two Parallax it adds) and the breathing's feather (the
# GUI's is not packed: the gameplay's flyFeather), loaded when drawn
NPC_ASSETS = {
    "theo_04_cliffside": ["lazy:tex:bgs/04/bgCloudLoop", "lazy:tex:bgs/04/bgCloud", "lazy:bank:flyFeather"],
}

VARIANTS = {
    "booster": ("red", {True: ["bank:boosterRed"], False: ["bank:booster"]}),
    # (B and C sides use the Remix clouds: the map is not known here, so both)
    "cloud": ("fragile", {True: ["bank:cloudFragile", "bank:cloudFragileRemix"], False: ["bank:cloud", "bank:cloudRemix"]}),
}

