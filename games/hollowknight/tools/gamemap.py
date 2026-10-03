"""The map (Game_Map: the areas' rooms as Cornifer drew them, rough until the quill maps them; their pins, the arrows to
the areas next to them, the areas' names; the compass, the shade's mark) and the quick map's HUD (Quick Map): MAP_*
defines for data.h (src/map.c). Places are in the map's units from its root; the HUD shows them at its scale."""
import math
import unity, text

GAME_MAP = ("resources.assets", 4808)
HUD = ("resources.assets", 5473)
QUICK = "_GameCameras/HudCamera/Quick Map/"
# the areas this part of the game has maps of: (Game_Map's child, the PlayerData bool that has its map (None: always),
# the quick map's place for it (Quick Map: SetPosition), its name (Map Zones))
AREAS = (("Town_Tutorial", None, (4.07, -11.62), "TOWN"),
         ("Crossroads", "mapCrossroads", (-0.04, -7.88), "CROSSROADS"))
QUICK_K, WORLD_K = 1.55, 1.45   # (the map's scale: the quick map's (Check Area), the world map's (its own))
MAP_MSG = ("sharedassets7.assets", 539)   # (Map Update Msg: what the bench makes as the map is updated)
MAP_MSG_AT = (-1.29, -6.83)              # (its Map Msg's Position)
COCOON = "hasPinCocoon"         # (pin_blue_health: SetupMap's, by the rooms a cocoon was broken in)


def build(sprites, texts, res, rooms, pd_flags, room_zone):
    """rooms: the rooms this part of the game has (their order); pd_flags: PlayerData bools -> flags; room_zone(room) ->
    its map zone (SceneManager's mapZone)"""
    d = unity.prefab(*GAME_MAP)
    by = {o["path"]: o for o in d["objects"]}
    byid = {o["id"]: o for o in d["objects"]}
    kids = {}
    for o in d["objects"]:
        kids.setdefault(o.get("parent"), []).append(o)
    U = lambda v: "%.4ff" % v
    sh = text.sheets()
    out = {}

    def comp(o, cls):
        return next((c["v"] for c in o["c"] if (c.get("class") or c["type"]) == cls and c.get("v") is not None), None)

    def fsm(o):
        return next((c["fsm"] for c in o["c"] if c.get("fsm")), None)

    def flag(name):
        """a PlayerData bool's flag; -2 one this part of the game never sets (so: never)"""
        return pd_flags.get(name, -2) if name else -1

    def scale(o):
        """its scale in the map's units"""
        k, q = 1.0, o
        while q is not None and q["id"] != root["id"]:
            k *= q["lscale"][0]
            q = byid.get(q.get("parent"))
        return k

    def at(o):
        """its place in the map's units from the map's root"""
        x = y = 0.0
        p = o
        while p is not None and p["id"] != root["id"]:
            k = scale(byid[p["parent"]]) if byid.get(p.get("parent")) and p["parent"] != root["id"] else 1.0
            x += p["lpos"][0] * k
            y += p["lpos"][1] * k
            p = byid.get(p.get("parent"))
        return x, y

    def spr(ref, k, doc=d):
        return sprites.id(doc, ref, k, res)

    def tx(o, style, doc_k=1.0):
        """a text's place (its rect's left, middle or right, as it is aligned), its rect's top, its color, its
        alignment (0 left, 1 centred, 2 right)"""
        tc, tm, gt = comp(o, "TextContainer"), comp(o, "TextMeshPro"), comp(o, "SetTextMeshProGameText")
        k = scale(o) * doc_k
        x, y = at(o)
        w, h = tc["m_rect"]["width"] * k, tc["m_rect"]["height"] * k
        al = tm["m_textAlignment"] % 4
        assert al < 3, o["path"]
        col = tm.get("m_fontColor") or {"r": 1, "g": 1, "b": 1}
        rgb = (int(round(col["r"] * 255)) << 16) | (int(round(col["g"] * 255)) << 8) | int(round(col["b"] * 255))
        t = texts.add(text.clean(sh[gt["sheetName"]][gt["convName"]]), style)
        return t, x + (al / 2 - tc["m_pivot"]["x"]) * w, y + (1 - tc["m_pivot"]["y"]) * h, rgb, al

    root = by["Game_Map"]
    rows, pins, nexts, names, areas = [], [], [], [], []
    for ai, (an, have, qpos, title) in enumerate(AREAS):
        a = by["Game_Map/" + an]
        first = len(rows)
        zones = 0
        for r in kids.get(a["id"], []):
            sr = comp(r, "SpriteRenderer")
            cls = {(c.get("class") or c["type"]) for c in r["c"]}
            if "TextMeshPro" in cls:
                # (the area's name: the world map's only; a sub area's: both)
                t, mx, top, rgb, al = tx(r, "MSG")
                world_only = "DisplayOnWorldMapOnly" in cls or fsm(r) is not None
                names.append("{%d, -1, %d, %s, %s, 0x%06X, %d, %d}" % (ai, t, U(mx), U(top), rgb, al, world_only))
                continue
            if r["name"] == "Grub Pins":
                continue   # (the Collector's Map's: not here)
            rm = comp(r, "RoughMapRoom")
            ri = rooms.index(r["name"]) if r["name"] in rooms else 255
            rough = bool(r["self_active"] and rm)
            if ri == 255 and not rough:
                continue   # (never mapped here, never shown)
            if ri != 255:
                zones |= 1 << room_zone(r["name"])
            k = scale(r)
            x, y = at(r)
            if sr is not None and sr.get("m_Sprite"):
                col = sr["m_Color"]
                rgb = sum(int(round(col[c] * 255)) << (16 - 8 * i) for i, c in enumerate("rgb"))
                s = unity.sprite(d["level"], d["externals"], *sr["m_Sprite"])
                sid = spr(sr["m_Sprite"], QUICK_K * k)
                full = spr(rm["fullSprite"], QUICK_K * k) if rm else -1
                w, h = s.w / s.ppu * k, s.h / s.ppu * r["lscale"][1] * k / r["lscale"][0]
            else:
                sid, full, w, h, rgb = -1, -1, 0, 0, 0   # (Crossroads_ShamanTemple: its pins only)
            rows.append("{%d, %d, %d, %d, %s, %s, %s, %s, 0x%06X, %d}" % (ai, ri, sid, full, U(x), U(y), U(w), U(h), rgb,
                                                                         rough))
            mi = len(rows) - 1
            for q in kids.get(r["id"], []):
                qc = {(c.get("class") or c["type"]) for c in q["c"]}
                if q["name"].startswith("pin_") and "SpriteRenderer" in qc:
                    f = fsm(q)
                    if f is not None:
                        # (pin FSM: its Pin Type Bool, its Specific Bool)
                        kind, f1, f2 = 0, flag(f["vars"]["Pin Type Bool"][1]), flag(f["vars"]["Specific Bool"][1])
                    elif q["name"] == "pin_blue_health":
                        kind, f1, f2 = 1, flag(COCOON), -1
                    else:
                        continue   # (pin_dream_tree: the dream nail's)
                    if f1 == -2 or f2 == -2:
                        continue
                    px, py = at(q)
                    pins.append("{%d, %d, %d, %s, %s, %d, %d}" % (
                        mi, kind, spr(comp(q, "SpriteRenderer")["m_Sprite"], QUICK_K * scale(q)), U(px), U(py), f1, f2))
                elif "TextMeshPro" in qc:
                    # (a sub area's name: shown with its room)
                    t, mx, top, rgb, al = tx(q, "MSG")
                    names.append("{%d, %d, %d, %s, %s, 0x%06X, %d, %d}" % (ai, mi, t, U(mx), U(top), rgb, al,
                                                                          "DisplayOnWorldMapOnly" in qc))
                elif "MapNextAreaDisplay" in qc:
                    # (an arrow to the area next to it, and its name: the quick map's, once that area was visited)
                    vs = comp(q, "MapNextAreaDisplay")["visitedString"]
                    vf = flag(vs) if vs else -1
                    if vf == -2:
                        continue
                    arrow = next(z for z in kids[q["id"]] if z["name"] == "Map_Arrow")
                    nm = next(z for z in kids[q["id"]] if z["name"] == "Area Name")
                    m3 = arrow["m3"]
                    ang = math.degrees(math.atan2(m3[3], m3[0]))
                    ax, ay = at(arrow)
                    t, mx, top, rgb, al = tx(nm, "MSG")
                    nexts.append("{%d, %d, %s, %s, %s, %d, %s, %s, %d, %d}" % (
                        mi, spr(comp(arrow, "SpriteRenderer")["m_Sprite"], QUICK_K * scale(arrow)), U(ax), U(ay),
                        U(ang), t, U(mx), U(top), al, vf))
        areas.append("{%d, %d, %d, %s, %s, %d, 0x%Xu}" % (first, len(rows) - first, flag(have) if have else -1,
                                                        U(qpos[0]), U(qpos[1]),
                                                        texts.add(text.clean(sh["Map Zones"][title]), "DIALOGUE"), zones))
    out["MAP_AREAS"] = "{%s}" % ", ".join(areas)
    out["MAP_ROOMS"] = "{%s}" % ", ".join(rows)
    out["MAP_PINS"] = "{%s}" % ", ".join(pins)
    out["MAP_NEXT"] = "{%s}" % ", ".join(nexts)
    out["MAP_NAMES"] = "{%s}" % ", ".join(names)
    out["MAP_QUICK_K"], out["MAP_WORLD_K"] = U(QUICK_K), U(WORLD_K)
    # the shade's mark (Shade Pos: its fader, its head), the markers (Map Markers: B1, R1, Y1, W1)
    sp = by["Game_Map/Shade Pos"]
    parts = []
    for n in ("Map_shade_fader", "Map_shade_head"):
        q = by["Game_Map/Shade Pos/" + n]
        v = comp(q, "SpriteRenderer")
        parts.append("{%d, %s, %s, %d}" % (spr(v["m_Sprite"], QUICK_K * scale(q)), U(q["lpos"][0] * sp["lscale"][0]),
                                           U(q["lpos"][1] * sp["lscale"][1]), int(round(v["m_Color"]["a"] * 255))))
    out["MAP_SHADE"] = "{%s}" % ", ".join(parts)
    out["MAP_MARKERS"] = "{%s}" % ", ".join(
        str(spr(comp(by["Game_Map/Map Markers/%s1" % c], "SpriteRenderer")["m_Sprite"],
                QUICK_K * scale(by["Game_Map/Map Markers/%s1" % c]))) for c in "BRYW")
    out["MAP_COMPASS_K"] = U(scale(by["Game_Map/Compass Icon"]))
    # the quick map's HUD: its backdrop (BG: black, its color's alpha at its fade group's full), the area's name (its
    # rect's middle and top, on its backboard), the no map message (its symbol, its line)
    h = unity.prefab(*HUD)
    hb = {o["path"]: o for o in h["objects"]}
    bg = hb[QUICK + "BG"]
    out["MAP_BG_A"] = U(comp(bg, "SpriteRenderer")["m_Color"]["a"] * comp(bg, "FadeGroup")["fullAlpha"])
    an = hb[QUICK + "Area Name"]
    tc = comp(an, "TextContainer")
    k = an["lscale"][1]
    out["MAP_TITLE"] = "{%s, %s}" % (U(an["pos"][0]), U(an["pos"][1] + (1 - tc["m_pivot"]["y"]) * tc["m_rect"]["height"] * k))
    bb = hb[QUICK + "Area Name/Backboard"]
    v = comp(bb, "SpriteRenderer")
    out["MAP_TITLE_BACK"] = "{%d, %s, %s, %s, %s, %d}" % (
        sprites.id(h, v["m_Sprite"], max(bb["m3"][0], bb["m3"][4]), res), U(bb["pos"][0]), U(bb["pos"][1]),
        U(bb["m3"][0] / max(bb["m3"][0], bb["m3"][4])), U(bb["m3"][4] / max(bb["m3"][0], bb["m3"][4])),
        int(round(v["m_Color"]["a"] * 255)))
    sym = hb[QUICK + "No Map/No_Map_symbol"]
    out["MAP_NO_MAP"] = "{%d, %s, %s}" % (sprites.id(h, comp(sym, "SpriteRenderer")["m_Sprite"], sym["lscale"][0], res),
                                          U(sym["pos"][0]), U(sym["pos"][1]))
    msg = hb[QUICK + "No Map/Msg"]
    tc = comp(msg, "TextContainer")
    out["MAP_NO_MAP_MSG"] = "{%d, %s, %s}" % (
        texts.add(text.clean(sh["Prompts"]["NO_MAP"]), "DIALOGUE"), U(msg["pos"][0]),
        U(msg["pos"][1] + (1 - tc["m_pivot"]["y"]) * tc["m_rect"]["height"] * msg["lscale"][1]))
    # the map's update message (Map Update Msg: made at the HUD's (-1.29, -6.83) at its scale; its quill writing, its
    # line (left aligned, its middle), its large backboard)
    mm = unity.prefab(*MAP_MSG)
    mb = {o["path"]: o for o in mm["objects"]}
    r0 = mb["Map Update Msg"]
    k0 = r0["lscale"][0]
    rel = lambda o: ((o["pos"][0] - r0["pos"][0]), (o["pos"][1] - r0["pos"][1]))
    an = mb["Map Update Msg/Animation"]
    tq = mb["Map Update Msg/Text"]
    tc = comp(tq, "TextContainer")
    bb = mb["Map Update Msg/Backboard Large"]
    v = comp(bb, "SpriteRenderer")
    kx, ky = math.hypot(bb["m3"][0], bb["m3"][3]), math.hypot(bb["m3"][1], bb["m3"][4])
    out["MAP_MSG"] = "{%s, %s, %s, %s, %s, %s, %d, %d, %s, %s, %s, %s, %d}" % (
        U(MAP_MSG_AT[0]), U(MAP_MSG_AT[1]), U(rel(an)[0]), U(rel(an)[1]),
        U(rel(tq)[0] - tc["m_pivot"]["x"] * tc["m_rect"]["width"] * k0), U(rel(tq)[1]),
        texts.add(text.clean(sh["UI"]["MAP_UPDATED"]), "MSG"),
        sprites.id(mm, v["m_Sprite"], max(kx, ky), res), U(rel(bb)[0]), U(rel(bb)[1]), U(kx / max(kx, ky)),
        U(ky / max(kx, ky)), int(round(v["m_Color"]["a"] * 255)))
    out["MAP_MSG_K"] = U(k0)
    # the first map's lesson (Prompts/First Map, on FIRST MAP UP): its backdrop's alpha, its lines (their middles and
    # rects' tops: the title in the item message's name style), its picture, its divider, the stop, the key's place
    fm = lambda n: hb["_GameCameras/HudCamera/Prompts/First Map/" + n]
    out["FIRSTMAP_BG_A"] = U(comp(fm("BG"), "SpriteRenderer")["m_Color"]["a"])
    rows = []
    for n, style in (("Text 1", "MSG_NAME"), ("Text Hold", "MSG"), ("Text 2", "MSG"), ("Text 3", "MSG")):
        q = fm(n)
        tc, gt = comp(q, "TextContainer"), comp(q, "SetTextMeshProGameText")
        rows.append("{%d, %s, %s}" % (texts.add(text.clean(sh[gt["sheetName"]][gt["convName"]]), style), U(q["pos"][0]),
                                      U(q["pos"][1] + (1 - tc["m_pivot"]["y"]) * tc["m_rect"]["height"] * q["lscale"][1])))
    out["FIRSTMAP_TEXTS"] = "{%s}" % ", ".join(rows)
    parts = []
    for n in ("Map_prompt", "Inv_0017_divider", "Stop"):
        q = fm(n)
        kx, ky = q["lscale"][0], q["lscale"][1]
        parts.append("{%d, %s, %s, %s, %s}" % (sprites.id(h, comp(q, "SpriteRenderer")["m_Sprite"], max(kx, ky), res),
                                               U(q["pos"][0]), U(q["pos"][1]), U(kx / max(kx, ky)), U(ky / max(kx, ky))))
    out["FIRSTMAP_PIECES"] = "{%s}" % ", ".join(parts)
    out["FIRSTMAP_KEY"] = "{%d, %s, %s}" % (texts.add("var", "PROMPT"), U(fm("Button")["pos"][0]), U(fm("Button")["pos"][1]))
    return out
