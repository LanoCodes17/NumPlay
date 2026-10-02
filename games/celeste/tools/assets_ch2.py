"""Old Site (chapter 2): what src/ch2.c's entities draw."""

ASSETS = {
    "dreamBlock": [],          # drawn with shapes (objects/dreamblock/particles is three tiny stars)
    "darkChaser": [],          # Badeline's sprite and hair are direct textures
    "touchSwitch": ["tex:objects/touchswitch/container", "fam:objects/touchswitch/icon"],
    "switchGate": ["tex:objects/switchgate/block", "tex:objects/switchgate/mirror", "tex:objects/switchgate/temple",
                   "tex:objects/switchgate/stars", "fam:objects/switchgate/icon"],
    "hanginglamp": ["tex:objects/hanginglamp"],
    "floatingDebris": ["tex:scenery/debris"],
    "foregroundDebris": ["fam:scenery/fgdebris/rock_a", "fam:scenery/fgdebris/rock_b"],
    "invisibleBarrier": [],
    # the sprite "payphone": its idle frame loads with the room, the rest (pickUp ... eat) when first drawn
    "payphone": ["bank:payphone", "tex:cutscenes/payphone/blink"],
    # src/ch2.c plays the sprite "lookout" from a table, loading an animation when first drawn (the room
    # loads the bank's first one, "idle")
    "towerviewer": ["bank:lookout"],
    "dreammirror": ["bank:glass", "tex:objects/mirror/frame", "tex:objects/mirror/glassbg", "tex:objects/mirror/glassfg"],
}
