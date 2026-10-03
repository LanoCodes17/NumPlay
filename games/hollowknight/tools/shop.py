"""The shops (Shop Menu: Sly's, Iselda's, Salubra's): where the menu's pieces are (HUD units from the screen's center:
the scenes put it there), the items each sells (ShopMenuStock, ShopItemStats), their texts (src/shop.c)."""
import math
import numpy as np
import unity, tk2d, text, ents, state

# the shops: (room, its figurehead, the conversation it has when nothing is left, its title)
SHOPS = [("Room_shop", "Figurehead Sly", "SLY_NOSTOCK_1", "Sly", "SLY"),
         ("Room_mapper", "Figurehead Iselda", "ISELDA_NOSTOCK", "Iselda", "ISELDA"),
         ("Room_Charm_Shop", "Figurehead Slug", "CHARMSLUG_NOSTOCK", "Charm Slug", "CHARM_SLUG")]
ROOT = "Shop Menu/"
# ShopItemStats.specialType (src/shop.c: ST_*)
ST_NONE, ST_HEART, ST_CHARM, ST_VESSEL, ST_NOTCH, ST_MAP, ST_KEY, ST_EGG, ST_PIN, ST_MARKER = 0, 1, 2, 3, 8, 9, 10, 11, 16, 17
TYPES = {ST_NONE, ST_HEART, ST_CHARM, ST_VESSEL, ST_NOTCH, ST_MAP, ST_KEY, ST_EGG, ST_PIN, ST_MARKER}


def _known(name):
    """a PlayerData bool: its flag, or None if it is always false here"""
    pdf = ents.pd_flags()
    if name in pdf:
        return pdf[name]
    import vm
    if vm.PD_CONST.get(name, None) is False or state.CONSTANT.get(name, None) is False:
        return None
    raise KeyError("PlayerData bool %s: add it to src/game.h (PDF_*)" % name)


def build(sprites, texts, res, actor_clip, charms):
    """-> {define: value} for data.h. res: the HUD's resolution; actor_clip(actor, clip) -> a clip's id (the menu's
    animated pieces: the "shopui" actor); charms: the charms this part of the game has"""
    d = unity.scene(SHOPS[0][0])
    by = {o["path"]: o for o in d["objects"]}
    U = lambda v: "%.4ff" % v
    out = {}
    sh = text.sheets()

    def comp(o, cls):
        return next((c["v"] for c in o["c"] if (c.get("class") or c["type"]) == cls and c.get("v") is not None), None)

    def piece(o, doc=d, k=1.0, at=None):
        """an object's sprite at its place (HUD units), its scale: "{sprite, x, y, kx, ky}" """
        x, y = at if at is not None else (o["pos"][0], o["pos"][1])
        m = o["m3"]
        sx, sy = math.hypot(m[0], m[3]) * k * (1 if m[0] >= 0 else -1), math.hypot(m[1], m[4]) * k
        s = max(abs(sx), abs(sy))
        sr = comp(o, "SpriteRenderer")
        if sr and sr.get("m_Sprite"):
            spr = sprites.id(doc, sr["m_Sprite"], s, res)
        else:
            tk = comp(o, "tk2dSprite")
            cp, cpid = ents._ref_file(doc, tk["collection"])
            col = tk2d.collection(cp, cpid)
            spr = sprites.tk2d(cp, cpid, unity.S(col["spriteDefinitions"][tk["_spriteId"]]["name"]), s, res)
        return "{%d, %s, %s, %s, %s}" % (spr, U(x), U(y), U(sx / s), U(sy / s))

    def tx(o):
        """a text's place (TextMeshPro in its TextContainer): "{x, top, width}", x its rect's left, center or right as
        it is aligned (0 left, 1 center, 2 right)"""
        x, y = o["pos"][0], o["pos"][1]
        tc, tm = comp(o, "TextContainer"), comp(o, "TextMeshPro")
        k = math.hypot(o["m3"][0], o["m3"][3])
        w, h = tc["m_rect"]["width"] * k, tc["m_rect"]["height"] * k
        mg = tc.get("m_margins") or {}
        ml, mt, mr = mg.get("x", 0) * k, mg.get("y", 0) * k, mg.get("z", 0) * k   # (its margins: left, top, right)
        al = tm["m_textAlignment"] % 4
        x0 = x - tc["m_pivot"]["x"] * w + ml
        w -= ml + mr
        top = y + (1 - tc["m_pivot"]["y"]) * h - mt
        return "{%s, %s, %s}" % (U(x0 + (0, w / 2, w, 0)[al]), U(top), U(w))

    def txt(sheet, key, style):
        return texts.add(text.clean(sh[sheet][key]), style)

    def anim(o):
        """an InvAnimateUpAndDown piece: "{x, y, kx, ky, up clip, down clip, delay}" (its clips the "shopui" actor's)"""
        a = comp(o, "InvAnimateUpAndDown")
        m = o["m3"]
        return "{%s, %s, %s, %s, %s, %s, %s}" % (U(o["pos"][0]), U(o["pos"][1]), U(math.hypot(m[0], m[3])),
                                                U(math.hypot(m[1], m[4])), actor_clip("shopui", a["upAnimation"]),
                                                actor_clip("shopui", a["downAnimation"]), U(a.get("upDelay", 0)))

    # the window (Window's FadeGroup: 0.2 s), its borders and the shopkeepers' figureheads, sliding up and down
    for n, p in (("BACKBOARD", "Window/Shop_Backboard"), ("MASK_TOP", "Window/shop_mask_top"),
                 ("MASK_BOTTOM", "Window/shop_mask_bottom"), ("ARROW_U", "Arrow U"), ("ARROW_D", "Arrow D"),
                 ("SELECTOR", "selector"), ("CONFIRM_GEO", "Confirm/Geo Sprite"),
                 ("POINTER_L", "Confirm/UI List/Yes/Pointer L"), ("POINTER_R", "Confirm/UI List/Yes/Pointer R")):
        out["SHOP_" + n] = piece(by[ROOT + p])
    # (the masks' cover along the list, every 0.25 from y -7: their alpha down the middle; the costs' text, drawn over
    # them, fades by it)
    prof = [0] * 56
    for p in ("Window/shop_mask_top", "Window/shop_mask_bottom"):
        o = by[ROOT + p]
        s = unity.sprite(d["level"], tuple(d["externals"]), *comp(o, "SpriteRenderer")["m_Sprite"])
        im = np.asarray(unity.sprite_image(s))
        h = s.h / s.ppu * math.hypot(o["m3"][1], o["m3"][4])
        top = o["pos"][1] + (1 - s.py) * h
        for i in range(len(prof)):
            r = int((top - (-7 + i * 0.25)) / h * im.shape[0])
            if 0 <= r < im.shape[0]:
                prof[i] = max(prof[i], int(im[r, im.shape[1] // 2, 3]))
    out["SHOP_MASK_A"] = "{%s}" % ", ".join(map(str, prof))
    ci = by[ROOT + "Confirm/Item Sprite"]
    out["SHOP_CONFIRM_ITEM"] = "{%s, %s}" % (U(ci["pos"][0]), U(ci["pos"][1]))
    out["TXT_KEY_BACK"] = texts.add("Back", "MSG")
    # (the costs: the list's, the confirm's)
    texts.add("0123456789", "TUTE")
    texts.add("0123456789", "NOTICE")
    out["SHOP_BACKBOARD_A"] = U(comp(by[ROOT + "Window/Shop_Backboard"], "SpriteRenderer")["m_Color"]["a"])
    out["SHOP_BORDERS"] = "{%s, %s}" % (anim(by[ROOT + "Window/Shop_Border_top"]), anim(by[ROOT + "Window/Shop_Border_Bottom"]))
    out["SHOP_FIGUREHEADS"] = "{%s}" % ", ".join(anim(by[ROOT + "Window/Figureheads/" + f]) for _, f, _, _, _ in SHOPS)
    # (the pointers beside No: Yes's, lower by the gap between them)
    out["SHOP_NO_DY"] = U(by[ROOT + "Confirm/UI List/No"]["pos"][1] - by[ROOT + "Confirm/UI List/Yes"]["pos"][1])
    # the texts
    for n, p in (("NAME", "Item Details/Item name"), ("DESC", "Item Details/Item desc"), ("NOTCH_TEXT", "Item Details/Notch Cost/Text Cost"),
                 ("CONFIRM_NAME", "Confirm/Item name"), ("CONFIRM_COST", "Confirm/Item cost"), ("CONFIRM_MSG", "Confirm/Confirm msg"),
                 ("YES", "Confirm/UI List/Yes"), ("NO", "Confirm/UI List/No"), ("THANKS", "Thankyou"),
                 ("ACT_CONFIRM", "Action Confirm/Text"), ("ACT_CANCEL", "Action Cancel/Text")):
        out["SHOP_T_" + n] = tx(by[ROOT + p])
    # (the item's description: its place without a notch cost (Notch Display Init: -0.09 in Item Details), with one
    # (-1.7, as the scene has it))
    det, desc = by[ROOT + "Item Details"], by[ROOT + "Item Details/Item desc"]
    out["SHOP_DESC_DY"] = U(det["pos"][1] - 0.09 - desc["pos"][1])
    # the notch cost's pegs: the first's place, the gap between them, Pegs' x as Centre Pegs sets it for 1 .. 6
    pegs = [by[ROOT + "Item Details/Notch Cost/Pegs/Cost %d" % i] for i in range(1, 7)]
    out["SHOP_PEG"] = piece(pegs[0])
    out["SHOP_PEG_DX"] = U(pegs[1]["pos"][0] - pegs[0]["pos"][0])
    pg = by[ROOT + "Item Details/Notch Cost/Pegs"]
    out["SHOP_PEGS_X"] = "{%s}" % ", ".join(U(x - pg["lpos"][0]) for x in (1.86, 1.49, 1.1, 0.75, 0.28, -0.05))
    # the action keys' boxes (Action Confirm, Action Cancel: their ActionButtonIcon)
    for n, p in (("KEY_CONFIRM", "Action Confirm/ActionButtonIcon"), ("KEY_CANCEL", "Action Cancel/ActionButtonIcon")):
        o = by[ROOT + p]
        out["SHOP_" + n] = "{%s, %s}" % (U(o["pos"][0]), U(o["pos"][1]))
    # the list: its rows' place (Item List at x -7.98, y 0.17 for the first; each 1.5 below), what is shown of it
    sm, il = by[ROOT[:-1]], by[ROOT + "Item List"]
    out["SHOP_LIST_X"], out["SHOP_LIST_Y"] = U(sm["pos"][0] - 7.98), U(sm["pos"][1] + 0.17)
    smv = comp(sm, "ShopMenuStock")
    out["SHOP_ROW_DY"] = U(-smv.get("yDistance", -1.5))
    out["SHOP_ROW_TOP"], out["SHOP_ROW_BOTTOM"] = U(4.9), U(-5.25)
    # the arrows' bob (Arrow Anim: 0.15 out in 0.05 s, back in 0.1 s), the selector's jitter (Can't Buy: 0.11)
    out["SHOP_ARROW_BOB"] = U(0.15)
    # texts the menu has
    for key, sheet, style in (("CTRL_BUY", "UI", "MSG"), ("CTRL_EXIT", "UI", "MSG"), ("CTRL_CONFIRM", "UI", "MSG"),
                              ("CTRL_CANCEL", "UI", "MSG"), ("SHOP_PURCHASE_CONFIRM", "UI", "TUTE"),
                              ("SHOP_PURCHASE_COMPLETE", "UI", "NOTICE"), ("CHARM_TXT_NOTCHCOST", "UI", "MSG"),
                              ("YES", "Prompts", "DIALOGUE"), ("NO", "Prompts", "DIALOGUE")):
        out["TXT_SHOP_" + key] = txt(sheet, key, style)

    # the items: each shop's stock (the alternative stock, for keys this part of the game has not, left out), in order
    items, firsts, descs = [], [], set()
    row_parts = None
    # (what needs more charms than there are here is never bought: what needs it, never sold)
    never = set()
    for room, *_ in SHOPS:
        ds = unity.scene(room)
        st = comp(next(o for o in ds["objects"] if o["path"] == ROOT[:-1]), "ShopMenuStock")
        for r in st["stock"]:
            v = comp(unity.prefab(*ents._ref_file(ds, r))["objects"][0], "ShopItemStats")
            if v.get("charmsRequired", 0) > len(charms):
                never.add(v["playerDataBoolName"])
    for si, (room, fig, nostock, nsheet, title) in enumerate(SHOPS):
        ds = unity.scene(room)
        smo = next(o for o in ds["objects"] if o["path"] == ROOT[:-1])
        st = comp(smo, "ShopMenuStock")
        for b in (st.get("altPlayerDataBool"), st.get("altPlayerDataBoolAlt")):
            assert not b or _known(b) is None, (room, b)
        firsts.append(len(items))
        for r in st["stock"]:
            f, pid = ents._ref_file(ds, r)
            p = unity.prefab(f, pid)
            root = p["objects"][0]
            v = comp(root, "ShopItemStats")
            req = v.get("requiredPlayerDataBool") or ""
            rq = None if req in never else _known(req) if req not in ("", "Null") else -1
            if rq is None:
                continue   # (never sold here)
            assert v["specialType"] in TYPES and not v.get("relic"), (room, v["specialType"])
            assert not v.get("removalPlayerDataBool"), root["name"]
            rk = math.hypot(root["m3"][0], root["m3"][3])
            sp = next(q for q in p["objects"] if q["name"] == "Item Sprite")
            geo = next(q for q in p["objects"] if q["name"] == "Geo Sprite")
            cost = next(q for q in p["objects"] if q["name"] == "Item cost")
            if row_parts is None:
                # (the rows' geo and cost: where they are from the row, at its scale)
                # (the cost's text, left aligned at the top of its rect: that rect's left and top)
                tc = comp(cost, "TextContainer")
                cw, ch = tc["m_rect"]["width"], tc["m_rect"]["height"]
                cx0 = cost["lpos"][0] - tc["m_pivot"]["x"] * cw
                ctop = cost["lpos"][1] + (1 - tc["m_pivot"]["y"]) * ch
                assert comp(cost, "TextMeshPro")["m_textAlignment"] == 0
                row_parts = (piece(geo, p, at=(geo["lpos"][0] * rk, geo["lpos"][1] * rk)),
                             "{%s, %s}" % (U(cx0 * rk), U(ctop * rk)),
                             v["activeColour"], v["inactiveColour"])
            ks = math.hypot(sp["m3"][0], sp["m3"][3])
            # (its sprite, kept as big as the confirmation shows it: 1.75 times the row's)
            sr = comp(sp, "SpriteRenderer")
            spr = sprites.id(p, sr["m_Sprite"], ks * 1.75, res)
            name = v["nameConvo"]
            descs |= set(text.clean(sh["UI"][v["descConvo"]])) - set("\n\f")
            charm = int(v["playerDataBoolName"].split("_")[1]) if v["specialType"] == ST_CHARM else 0
            price = int(sh["Prices"][v["priceConvo"]])
            items.append("{%d, %s, %d, %d, %d, %d, %d, %d, %d, %d}" % (
                spr, U(ks / (ks * 1.75)), txt("UI", name, "TUTE"), txt("UI", v["descConvo"], "MSG"), price,
                v["specialType"], _known(v["playerDataBoolName"]), rq, v.get("charmsRequired", 0), charm))
    firsts.append(len(items))
    # (the smaller style's glyphs: those of the descriptions)
    texts.add("".join(sorted(descs)), "MSG_S")
    out["SHOP_DESC_BOTTOM"] = U(-5.05)
    out["SHOP_ITEMS"] = "{%s}" % ", ".join(items)
    out["SHOP_FIRST"] = "{%s}" % ", ".join(map(str, firsts))
    out["SHOP_ROW_GEO"], out["SHOP_ROW_COST"] = row_parts[0], row_parts[1]
    c = lambda col: "{%d, %d, %d}" % tuple(round(col[k] * 255) for k in "rgb")
    out["SHOP_ACTIVE_RGB"], out["SHOP_INACTIVE_RGB"] = c(row_parts[2]), c(row_parts[3])
    # each shop's room, its conversation when nothing is left and the title it shows
    out["SHOP_ROOMS"] = "{%s}" % ", ".join('"%s"' % r for r, *_ in SHOPS)
    out["SHOP_NOSTOCK"] = "{%s}" % ", ".join(str(texts.id(s, k)) for _, _, k, s, _ in SHOPS)
    out["SHOP_TITLES"] = "{%s}" % ", ".join("TITLE_" + t for *_, t in SHOPS)
    out["SHOP_N"] = len(SHOPS)
    return out
