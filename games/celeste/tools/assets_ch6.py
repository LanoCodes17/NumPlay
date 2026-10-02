"""What Reflection's own entities draw (src/ch6.c)."""

HEART = ["bank:heartgem0", "bank:heartgem1", "bank:heartgem2", "bank:heartgem3", "bank:heartGemGhost"]

ASSETS = {
    # Sprites.xml says objects/bumper/ and objects/badelineBoost/, the atlas has other cases: the frames are
    # listed as families ("lazy:": only the first is loaded with the room)
    "bigSpinner": ["lazy:fam:objects/Bumper/Idle", "lazy:fam:objects/Bumper/Evil"],
    "infiniteStar": ["bank:flyFeather", "tex:objects/flyFeather/outline"],
    "starJumpBlock": ["fam:objects/starjumpBlock/corner", "fam:objects/starjumpBlock/edgeH",
                      "fam:objects/starjumpBlock/edgeV", "fam:objects/starjumpBlock/leftrailing",
                      "fam:objects/starjumpBlock/railing", "fam:objects/starjumpBlock/rightrailing"],
    "plateau": ["tex:scenery/fallplateau"],
    "crushBlock": ["fam:objects/crushblock/block", "tex:objects/crushblock/lit_left", "tex:objects/crushblock/lit_right",
                   "tex:objects/crushblock/lit_top", "tex:objects/crushblock/lit_bottom",
                   "bank:crushblock_face", "bank:giant_crushblock_face"],
    "bigWaterfall": [],
    "badelineBoost": ["lazy:fam:objects/badelineboost/idle", "tex:objects/badelineboost/stretch", "bank:badeline"],
    "reflectionHeartStatue": ["tex:objects/reflectionHeart/statue", "tex:objects/reflectionHeart/gem",
                              "fam:objects/reflectionHeart/torch", "fam:objects/reflectionHeart/hint",
                              "tex:collectables/heartGem/white00", "tex:collectables/heartGem/orb"] + HEART,
    "finalBoss": ["bank:badeline_boss", "bank:badeline_projectile", "bank:badeline_beam", "bank:badeline_beam_start",
                  "bank:badeline"],
    # their tiles: 'g' (reflection) and, highlighted, 'G' (reflectionAlt)
    "finalBossFallingBlock": ["tex:tilesets/reflection", "tex:tilesets/reflectionAlt"],
    "finalBossMovingBlock": ["tex:tilesets/reflection", "tex:tilesets/reflectionAlt", "tex:debris/f"],
    "tentacles": [],
}

# the story's own: Badeline crying after the fight, her orbs and the level up (CS06_BossEnd)
NPC_ASSETS = {
    "06_crying": ["bank:badeline_boss", "tex:characters/badelineBoss/calm_white", "tex:characters/badeline/orb",
                  "bank:player_level_up"],
}
