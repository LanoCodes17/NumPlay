"""What Core's own entities draw (src/ch9.c)."""

ASSETS = {
    "coreModeToggle": ["bank:coreFlipSwitch"],
    "fireBall": ["bank:fireball"],
    "wallBooster": ["fam:objects/wallBooster/fireTop", "fam:objects/wallBooster/fireMid",
                    "fam:objects/wallBooster/fireBottom", "fam:objects/wallBooster/iceTop",
                    "fam:objects/wallBooster/iceMid", "fam:objects/wallBooster/iceBottom"],
    # the code says objects/bumpblocknew, the atlas BumpBlockNew (Monocle's lookups ignore case)
    "bounceBlock": ["tex:objects/BumpBlockNew/fire00", "tex:objects/BumpBlockNew/ice00",
                    "fam:objects/BumpBlockNew/fire_rubble", "fam:objects/BumpBlockNew/ice_rubble",
                    "bank:bumpBlockCenterFire", "bank:bumpBlockCenterIce"],
    "fireBarrier": ["fam:danger/lava/bubble_a"],
    "iceBlock": [],
    "risingLava": ["fam:danger/lava/bubble_a"],
    "sandwichLava": ["fam:danger/lava/bubble_a"],
    "heartGemDoor": ["fam:objects/heartdoor/icon", "tex:objects/heartdoor/edge", "tex:objects/heartdoor/top",
                     "tex:objects/heartdoor/mist"],
    "coreMessage": [],
}
