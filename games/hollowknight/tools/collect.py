"""A mask shard's and a vessel fragment's UI (Heart Container UI, Vessel Fragment UI): where their pieces are, about the
camera (they follow it, in front of the room): COLLECT_* defines for data.h."""
import math
import unity

UIS = (("sharedassets10.assets", 249), ("sharedassets10.assets", 219))   # heart, vessel


def build():
    U = lambda v: "%.4ff" % v
    out = {}

    def at(o):
        """{x, y, z, scale} from the UI's root"""
        return "{%s, %s, %s, %s}" % (U(o["pos"][0]), U(o["pos"][1]), U(o["pos"][2]), U(math.hypot(o["m3"][0], o["m3"][3])))

    pieces = []
    for path, pid in UIS:
        p = unity.prefab(path, pid)
        root = p["objects"][0]["pos"]
        by = {o["name"]: dict(o, pos=[o["pos"][i] - root[i] for i in range(3)]) for o in p["objects"]}
        pieces.append(at(by["Piece"]))
        if pid == UIS[0][1]:
            out["COLLECT_FLEUR"], out["COLLECT_MOVER"], out["COLLECT_GLOW"] = at(by["Fleur"]), at(by["Mover"]), at(by["Get Glow"])
    out["COLLECT_PIECE"] = "{%s}" % ", ".join(pieces)
    # (Tween Mover: the mover and the piece to the new mask's place, the mover to its scale, 1)
    out["COLLECT_MOVER_TO"] = "{%s, %s}" % (U(-7.7), U(6.04))
    return out
