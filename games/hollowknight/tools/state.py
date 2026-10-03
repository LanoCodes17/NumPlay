"""PlayerData as the scenes see it when they load (DeactivateIfPlayerdataTrue, DeactivateIfPlayerdataFalse): the bools
that keep the value they start with all through this part of the game turn their objects off for good here; those that
change are left to the room as it loads (ents.py: OK_COND)."""

# (as a new game has them, and as they stay up to Hornet)
CONSTANT = {"crossroadsInfected": False, "troupeInTown": False, "clothInTown": False, "killedTraitorLord": False,
            "clothKilled": False, "clothLeftTown": False, "brettaLeftTown": False, "zoteDefeated": False,
            "brettaRescued": False, "visitedRuins": False, "visitedDeepnest": False, "colosseumSilverOpened": False,
            "nymmInTown": False, "killedGrimm": False, "divineInTown": False, "fatGrubKing": False}
DYNAMIC = {"visitedDirtmouth", "tisoEncounteredTown", "hasDash", "hornet1Defeated", "shamanPillar"}


def conditions(o):
    """An object's DeactivateIfPlayerdata components -> [(bool name, off if true (else off if false))]."""
    out = []
    for c in o["c"]:
        cl = c.get("class") or ""
        if cl in ("DeactivateIfPlayerdataTrue", "DeactivateIfPlayerdataFalse"):
            out.append(((c.get("v") or {}).get("boolName"), cl.endswith("True")))
    return out


def apply(d):
    """Turns off (with what is under them) the scene's objects that the constant bools turn off."""
    by = {o["id"]: o for o in d["objects"]}
    for o in d["objects"]:
        for name, off_if in conditions(o):
            if name in CONSTANT:
                if CONSTANT[name] == off_if:
                    todo = [o["id"]]
                    while todo:
                        q = by[todo.pop()]
                        q["active"] = False
                        todo += q.get("children", [])
            elif name not in DYNAMIC:
                print("state: %s: %s not known" % (o["path"], name))
    return d
