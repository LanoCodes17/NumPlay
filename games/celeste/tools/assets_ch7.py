"""What the Summit's own entities draw (src/ch7.c): its checkpoints, gems, clouds, the ascent between its parts
(SummitBackgroundManager: AscendManager), and the credits' rooms."""
import sys as _sys

_GEMS = ["fam:collectables/summitgems/%d/gem" % i for i in range(6)]

ASSETS = {
    "summitcheckpoint": ["tex:scenery/summitcheckpoints/base00", "tex:scenery/summitcheckpoints/base01",
                         "tex:scenery/summitcheckpoints/base02", "fam:scenery/summitcheckpoints/numberbg",
                         "fam:scenery/summitcheckpoints/number"],
    "summitgem": ["tex:collectables/heartGem/orb"],   # NEEDS: its own gem
    "summitGemManager": _GEMS + ["tex:collectables/summitgems/%d/bg" % i for i in range(6)],
    "summitcloud": ["tex:scenery/summitclouds/cloud00", "tex:scenery/summitclouds/cloud01"],
    "SummitBackgroundManager": ["fam:scenery/launch/slice", "tex:scenery/launch/cloud00", "tex:scenery/launch/cloud01"],
    "creditsTrigger": [],   # VARIANTS: the credits' Oshiro
}

NEEDS = {"summitgem": lambda e: [_GEMS[int(e.get("gem", 0)) % 6]]}

# the credits' resort: Oshiro sweeping, his broom, the dust bunnies and their boxes
VARIANTS = {"creditsTrigger": ("event", {"Oshiro": ["bank:oshiro", "tex:characters/oshiro/broom",
                                                    "tex:decals/3-resort/brokenbox_a", "tex:decals/3-resort/brokenbox_b",
                                                    "tex:decals/3-resort/brokenbox_c", "fam:danger/dustcreature/base",
                                                    "fam:danger/dustcreature/center", "fam:danger/dustcreature/eyes"]})}

# SummitBackgroundManager is the game's AscendManager (Level.LoadLevel): the Summit's climbs between its parts and their
# cutscenes (CS07_Ascend). tools/entities.py drops it with the sound-only entities; it is kept here.
_E = _sys.modules.get("entities")
if _E is not None:
    _E.DROP.discard("SummitBackgroundManager")
