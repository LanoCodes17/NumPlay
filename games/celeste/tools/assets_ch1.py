"""Forsaken City (chapter 1): what src/ch1.c's entities draw (Theo's sprite comes with the npc entity)."""

ASSETS = {
    "memorial": ["tex:scenery/memorial/memorial"],
    "bonfire": ["bank:campfire"],
}
# the dreamy memorial (Old Site's start, while dreaming)
VARIANTS = {
    "memorial": ("dreaming", {True: ["fam:scenery/memorial/floatytext"]}),
}
