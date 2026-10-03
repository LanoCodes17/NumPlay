"""What the Mirror Temple's own entities draw (src/ch5.c). "lazy:" assets: only their first texture is
loaded with the room, the rest when drawn."""

ASSETS = {
    "torch": ["lazy:fam:objects/temple/torch", "lazy:fam:objects/temple/litTorch"],
    "templeEye": ["tex:scenery/temple/eye/bg_eye", "tex:scenery/temple/eye/bg_pupil", "tex:scenery/temple/eye/fg_eye",
                  "tex:scenery/temple/eye/fg_pupil", "fam:scenery/temple/eye/bg_lid", "fam:scenery/temple/eye/fg_lid"],
    "templeGate": [],      # NEEDS: one door (10 KB each)
    "dashSwitchH": [],     # NEEDS: one look
    "dashSwitchV": [],
    "swapBlock": ["tex:objects/swapblock/block", "tex:objects/swapblock/blockRed", "tex:objects/swapblock/target",
                  "tex:objects/swapblock/pathH", "tex:objects/swapblock/pathV", "fam:objects/swapblock/midBlock",
                  "fam:objects/swapblock/midBlockRed"],
    "templeCrackedBlock": ["lazy:fam:objects/temple/breakBlock", "tex:debris/1"],
    "seekerBarrier": [],
    "seeker": ["bank:seeker", "lazy:bank:seekerShockWave"],   # the shockwave: 19 frames of 96x96, when it recovers
    "seekerStatue": ["bank:seeker", "lazy:bank:seekerShockWave"],
    "playerSeeker": ["bank:seeker"],
    "theoCrystal": ["fam:characters/theoCrystal/idle"],   # its shatter only plays by the big eyeball
    "theoCrystalPedestal": ["tex:characters/theoCrystal/pedestal"],
    "conditionBlock": [],
    "templeMirror": ["tex:scenery/templemirror"],
    "templeMirrorPortal": ["tex:objects/temple/portal/portalframe", "tex:objects/temple/portal/surface",
                           "bank:temple_portal_curtain", "lazy:fam:objects/temple/portal/portaltorch"],
    "templeBigEyeball": ["bank:temple_eyeball", "tex:danger/templeeye/pupil", "bank:theo_crystal",
                         "lazy:fam:scenery/temple/eye/bg_burst", "lazy:fam:scenery/temple/eye/fg_burst"],
}


# the game never loads it: Level.LoadLevel has no case for it
DROP = {"theoCrystalHoldingBarrier"}


def _gate(e):
    # Sprites.xml says objects/door/templeDoor, the atlas has TempleDoor (Monocle's lookups ignore case)
    s = str(e.get("sprite", "default")).lower()
    return ["lazy:fam:objects/door/TempleDoor" + {"mirror": "B", "theo": "C"}.get(s, "")]


def _dash_switch(e):
    s = e.get("sprite", "default") or "default"
    return ["bank:dashSwitch_" + ("default" if s == "default" else "mirror")]


def _swap(e):
    if e.get("theme", "Normal") == "Moon":
        return ["tex:objects/swapblock/moon/block", "tex:objects/swapblock/moon/blockRed",
                "tex:objects/swapblock/moon/target", "fam:objects/swapblock/moon/midBlock",
                "fam:objects/swapblock/moon/midBlockRed"]
    return []


# assets that depend on an entity's attributes: name -> function(entity) -> list
NEEDS = {"templeGate": _gate, "dashSwitchH": _dash_switch, "dashSwitchV": _dash_switch, "swapBlock": _swap}

# the story (CS05_TheoPhone): the phone the interact trigger of ch5_theo_phone puts down
VARIANTS = {"interactTrigger": ("event", {"ch5_theo_phone": ["tex:characters/theo/phone"]})}




