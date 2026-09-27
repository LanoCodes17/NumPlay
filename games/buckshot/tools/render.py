#!/usr/bin/env python3
"""Renders Buckshot's art from the original game's scenes.

The pictures come from the Godot project of Buckshot Roulette (an open
decompilation, e.g. github.com/1503Dev/OpenBuckshotRoulette), rendered by
Godot 4.7 with the game's own lighting and post-processing at 320x240.
render/director.gd poses the scene for each shot; this script lists the
shots. The PNGs land in RENDER_DIR; tools/cut.py turns them into the sprites
and plates in assets/.

Usage: GODOT=/path/to/Godot BR_PROJECT=/path/to/project RENDER_DIR=dir render.py [job ...]
"""
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GODOT = os.environ.get("GODOT", "/Applications/Godot.app/Contents/MacOS/Godot")
PROJECT = os.environ.get("BR_PROJECT", "")
OUT = Path(os.environ.get("RENDER_DIR", HERE / ".renders"))

# node paths in scenes/main.tscn
DEALER_HEAD = "dealer model parent"
DEALER_HANDS = "dealer hands main1"
GUN = "tabletop parent/shotgun main parent"
GUN_SEG = "tabletop parent/shotgun main parent/shotgun parent new/shotgun cut segment"
GUN_HAND_R = "tabletop parent/shotgun main parent/shotgun parent new/dealer hand_shotgun right"
GUN_HAND_L = "tabletop parent/shotgun main parent/shotgun parent new/shotgun-forearm_003/dealer hand shotgun_left"
A_DEALER = "dealer model parent/animator_dealer parent"
A_HEAD_BOB = "dealer model parent/animator_dealer head bob"
A_HEAD_LOOK = "dealer model parent/animator_dealer head look"
A_HANDS = "dealer hands main1/animator_dealer hands"
A_GUN = "tabletop parent/shotgun main parent/anim_shotgun main parent"
A_COMP = "tabletop parent/main tabletop/compartments parent/animator_compartments"
A_BRIEF = "tabletop parent/main tabletop/compartments parent/animator_briefcase compartments"
A_SHELLS = "tabletop parent/main tabletop/compartment_shells/animator_shell compartment"
A_PLAYER = "player item interaction parent/animator_player items"

ITEMS = ["handsaw", "magnifying glass", "beer", "cigarettes", "handcuffs", "expired medicine", "burner phone",
         "adrenaline", "inverter"]

# cameras: (position, rotation in degrees, vertical field of view).
# T: the table, from behind and above the original's "home" seat, on a long
#    lens: the Dealer looms across the table, and every item slot and the
#    shotgun still fit on the screen (the player's turn).
# D: the original's "enemy" camera, closer: the Dealer looms over his side of
#    the table (his turn, his words, his items).
VIEWS = {
    "T": ([-15.8, 7.9, 0.024], [-23.5, -90, 0], 53),
    "D": ([-13.59, 7.323, 0.024], [-14.5, -90, 0], 42),
}
# the field of view of the original's "home" camera: the player's hands are
# rendered with it (cut out by their mask, over the table view), so that they
# cover the screen as they do there
HOME_FOV = 65.5
# the Dealer turning the barrel on you, up close
ZVIEW = dict(campos=[-13.59, 7.323, 0.024], camrot=[-9, -88, 0], fov=22)
SS = 4  # every picture is rendered 4x larger and averaged down (tools/cut.py)

SEATED = [A_DEALER, "dealer return to table", 2.0]
OPEN = [A_COMP, "show items", 0.9]


# the waiver lying on the table at the start of the original: some reloads of
# the scene leave it showing, so every shot hides it
RELEASE_FORM = "signature machine2/release form on table"


def shot(name, view=None, anims=(), hide=(), show=(), items=(), **kw):
    s = {"name": name, "anims": [list(a) for a in anims], "hide": list(hide), "show": list(show),
         "items": [list(i) for i in items]}
    kw["hide2"] = list(kw.get("hide2", [])) + [RELEASE_FORM]
    if view:
        pos, rot, fov = VIEWS[view]
        s.update(campos=pos, camrot=rot, fov=fov)
    s.update(kw)
    return s


NO_DEALER = [DEALER_HEAD, DEALER_HANDS]
A_SHOOT_TEXT = "ui parent_shooting decision/animator_shooting text"
A_LOAD = "tabletop parent/shotgun main parent/shotgun parent new/dealer hand_shotgun right/animator_dealer hand shotgun right"
HC = "tabletop parent/main tabletop/health counter/health counter ui parent/"
HCM = "health counter main1/main counter/health counter parent/"
PIP = "player item interaction parent"
HVIEW = ([-1.74, 1.55, 5.1], [-17.6, 180, 0], 33)
RIND = [None, [-0.616, -0.018, 0.438], [0.004, -0.018, 0.438], [0.636, -0.018, 0.438]]
EMPTY = {"items": [], "shells": [], "rebase": [], "examine": None, "ejectshell": None, "show": [], "show2": [], "hide2": [],
         "xform": [], "layers": [], "props": [], "env": []}
BM = "briefcase machine parent/briefcase machine animator"
LID = "briefcase machine parent/Plane_005/briefcase parent in vehicle/briefcase hinge/animator_lid"
ENDING_ENV = [["adjustment_brightness", 5.68], ["adjustment_contrast", 1.3], ["adjustment_saturation", 0.26]]


def base(opened=True, *extra):
    a = [SEATED]
    if opened:
        a.append(OPEN)
    return a + [list(e) for e in extra]


def plate_of(**kw):
    d = dict(EMPTY)
    d.update(kw)
    return d


def job_table():
    """The table view: plates, the shotgun, the items, the item box, the Dealer at rest."""
    hide_all = NO_DEALER + [GUN]
    P2 = plate_of(anims=base(), hide=hide_all)
    out = [
        shot("T_plate1", "T", base(False), hide=hide_all),
        shot("T_plate2", "T", base(), hide=hide_all),
        shot("T_gun", "T", base(), hide=NO_DEALER, base=P2),
        shot("T_gunsaw", "T", base(), hide=NO_DEALER + [GUN_SEG], base=P2),
        shot("T_box", "T", base(True, [A_BRIEF, "show briefcase", 2.0]), hide=hide_all, base=P2),
        shot("T_pcuff", "T", base(), hide=hide_all, show=["player cuff bar/handcuffs player bar"], base=P2),
        shot("T_hands", "T", base(), hide=[DEALER_HEAD, GUN], base=P2),
        shot("T_cuffed", "T", base(True, [A_HANDS, "dealer get handcuffed", 3.1]), hide=[DEALER_HEAD, GUN], base=P2),
    ]
    for k, t in enumerate((0.0, 0.5, 1.0, 1.5)):
        out.append(shot(f"T_head{k}", "T", base(True, [A_HEAD_BOB, "dealer head bob", t]), hide=[DEALER_HANDS, GUN], base=P2))
    for i, it in enumerate(ITEMS):
        for side in "pd":
            for g in range(8):
                out.append(shot(f"T_it_{i}_{side}{g}", "T", base(), hide=hide_all, items=[(it, side, g)], base=P2))
    return {"shots": out, "chunk": 24, "size": [320 * SS, 240 * SS]}  # items stop showing after a few hundred renders


def job_dealer():
    """The Dealer view: plates, his items, the shotgun on the table, the Dealer
    at rest, and every pose (items, shotgun, flying back), each cut against him at rest."""
    hide_all = NO_DEALER + [GUN]
    P2 = plate_of(anims=base(), hide=hide_all)
    IDLE = plate_of(anims=base())                       # the Dealer at rest, the gun on the table
    NOGUN = plate_of(anims=base(), hide=[GUN])
    out = [
        shot("D_plate1", "D", base(False), hide=hide_all),
        shot("D_plate2", "D", base(), hide=hide_all),
        shot("D_gun", "D", base(), hide=NO_DEALER, base=P2),
        shot("D_gunsaw", "D", base(), hide=NO_DEALER + [GUN_SEG], base=P2),
        shot("D_head", "D", base(), hide=[DEALER_HANDS, GUN], base=P2),
        shot("D_hands", "D", base(), hide=[DEALER_HEAD, GUN], base=P2),
        shot("D_eyes", "D", base(), hide=hide_all, show=["briefcase machine eyes"], base=P2),
    ]
    for i, it in enumerate(ITEMS):
        for g in range(8):
            out.append(shot(f"D_it_{i}_{g}", "D", base(), hide=hide_all, items=[(it, "d", g)], base=P2))
    poses = [("cuffed", "dealer get handcuffed", 3.1), ("getcuff", "dealer get handcuffed", 1.55),
             ("adr0", "dealer use adrenaline", 0.64), ("adr1", "dealer use adrenaline", 1.06),
             ("beer0", "dealer use beer", 0.22), ("beer1", "dealer use beer", 1.55),
             ("phone0", "dealer use burner phone", 1.15), ("phone1", "dealer use burner phone", 2.99),
             ("cig0", "dealer use cigarettes", 1.18), ("cig1", "dealer use cigarettes", 2.12),
             ("med0", "dealer use expired medicine", 2.12), ("med1", "dealer use expired medicine", 2.58),
             ("cuffs0", "dealer use handcuffs", 0.12), ("cuffs1", "dealer use handcuffs", 0.88),
             ("saw0", "dealer use handsaw", 1.0), ("saw1", "dealer use handsaw", 2.2), ("saw2", "dealer use handsaw", 2.6),
             ("inv0", "dealer use inverter", 1.11), ("inv1", "dealer use inverter", 1.56),
             ("mag0", "dealer use magnifying glass", 1.39), ("mag1", "dealer use magnifying glass", 3.06),
             ("medfall", "dealer death medicine", 0.22)]
    for name, an, t in poses:
        out.append(shot(f"D_{name}", "D", base(True, [A_HANDS, an, t]), hide=[GUN] if not name.startswith("saw") else [], base=NOGUN if not name.startswith("saw") else IDLE))
    gp = lambda an, t, *more: base(True, [A_HANDS, "dealer grab shotgun", 1.0], [A_GUN, an, t], *more)
    guns = [("load", gp("grab shotgun_pointing enemy", 0.45)),
            ("load1", gp("grab shotgun_pointing enemy", 0.45, [A_LOAD, "load single shell", 0.09])),
            ("aimp", gp("enemy shoot player", 0.63)), ("aims", gp("enemy shoot self", 0.78)),
            ("rack", gp("enemy eject shell_from player", 0.53)), ("racks", gp("enemy eject shell_from self", 0.53)),
            ("aimp0", gp("enemy shoot player", 0.21)), ("aims0", gp("enemy shoot self", 0.61)),
            ("lift", gp("grab shotgun_pointing enemy", 0.14))]
    for name, a in guns:
        out.append(shot(f"D_gun_{name}", "D", a, show2=[GUN_HAND_R, GUN_HAND_L], base=IDLE))
    for name, a in guns[2:4]:
        out.append(shot(f"D_gun_{name}_saw", "D", a, show2=[GUN_HAND_R, GUN_HAND_L], hide2=[GUN_SEG], base=IDLE))
    for k, t in enumerate((0.01, 0.02)):
        out.append(shot(f"D_fly{k}", "D", [OPEN, [A_DEALER, "dealer fly away", t]], hide=[GUN], base=P2))
    # the barrel turned to you, up close: a picture of its own
    out.append(shot("Z_aimp", None, gp("enemy shoot player", 0.63), show2=[GUN_HAND_R, GUN_HAND_L], **ZVIEW))
    ED = "shell parent_ejecting enemy side"
    for k in ("live", "blank"):
        out.append(shot(f"D_eject_{k[0]}", "D", base(True, [ED + "/animator_eject shell enemy", "eject shell beer", 0.3]),
                        hide=hide_all, ejectshell=[ED + "/shell eject manager", k], maskroots=[ED], base=P2))
    out.append(shot("P_aimd", "D", base(True, [A_GUN, "player shoot dealer", 1.85]),
                    rebase=[[GUN, "enemy"]], maskroots=[GUN], base=NOGUN))
    out.append(shot("P_aimd_saw", "D", base(True, [A_GUN, "player shoot dealer", 1.85]), hide2=[GUN_SEG],
                    rebase=[[GUN, "enemy"]], maskroots=[GUN], base=NOGUN))
    # the ending: the briefcase comes down onto the table and opens
    out.append(shot("E_brief0", "D", base(True, [BM, "move to table", 3.0]), hide=hide_all, show=["briefcase machine parent"], base=P2))
    out.append(shot("E_brief1", "D", base(True, [BM, "move to table", 4.5]), hide=hide_all, show=["briefcase machine parent"], base=P2))
    out.append(shot("E_brief2", "D", base(True, [BM, "move to table", 4.5], [LID, "open", 1.0]), hide=hide_all,
                    show=["briefcase machine parent"], base=P2))
    return {"shots": out, "chunk": 4, "size": [320 * SS, 240 * SS]}


def job_poses():
    """The player's hands in the table view, placed in front of the camera the
    way they sit in front of the original's camera. Each is cut out with its
    mask pass: the object itself, not the shadow and light it throws on the table."""
    hide_all = NO_DEALER + [GUN]
    P2 = plate_of(anims=base(), hide=hide_all)
    home = [[GUN, "home"], [PIP, "home"]]
    g = lambda an, t, *m: base(True, [A_GUN, an, t], *m)
    tx = base(True, [A_SHOOT_TEXT, "show text", 1.0])
    LBL = "ui parent_shooting decision/text "
    out = [
        shot("P_hold", "T", g("player grab shotgun", 5.0), hide=NO_DEALER, rebase=home, fov=HOME_FOV, maskroots=[GUN], base=P2),
        # "DEALER" written on the table ("YOU" lies below the picture: the game writes it)
        shot("P_lbl_dealer", "T", tx, hide=hide_all, show=[LBL + "dealer"], hide2=[LBL + "you"], maskroots=[LBL + "dealer"], base=P2),
        shot("P_aims", "T", g("player shoot self", 2.6), hide=NO_DEALER, rebase=home, fov=HOME_FOV, maskroots=[GUN], base=P2),
        shot("P_aims_saw", "T", g("player shoot self", 2.6), hide=NO_DEALER, hide2=[GUN_SEG], rebase=home, fov=HOME_FOV, maskroots=[GUN], base=P2),
        shot("P_rack", "T", g("player eject shell2", 1.1), hide=NO_DEALER, rebase=home, fov=HOME_FOV, maskroots=[GUN], base=P2),
        shot("P_god", "T", base(True, ["release form_god/animator_god release form", "grab form", 1.0]),
             hide=hide_all, rebase=[["release form_god", "briefcase"]], maskroots=["release form_god"], base=P2),
    ]
    for k in ("", "live", "blank"):
        kw = {"examine": k} if k else {}
        out.append(shot("P_mag" + (f"_{k[0]}" if k else ""), "T", base(True, [A_PLAYER, "player use magnifier", 3.2]),
                        hide=NO_DEALER, rebase=home, fov=HOME_FOV, maskroots=[GUN], base=P2, **kw))
    # the items: the item alone (the shotgun stays on the table), or with the shotgun
    ip = lambda *ns: [PIP + "/" + n for n in ns]
    for name, an, t, roots in (
            ("mag0", "player use magnifier", 0.29, ip("magnifying glass parent")),
            ("saw0", "player use handsaw", 0.27, ip("handsaw parent")),
            ("saw1", "player use handsaw", 1.34, ip("handsaw parent", "player shotgun segment") + [GUN]),
            ("beer0", "player use beer", 0.98, ip("beer can pivot parent")),
            ("beer1", "player use beer", 1.9, ip("beer can pivot parent")),
            ("cig0", "player use cigarettes", 0.65, ip("cigarette box pivot parent", "zippo lighter parent2")),
            ("cig1", "player use cigarettes", 1.08, ip("cigarette box pivot parent", "zippo lighter parent2")),
            ("phone0", "player use burner phone", 1.44, ip("burner phone")),
            ("phone1", "player use burner phone", 2.41, ip("burner phone")),
            ("med0", "player use expired pills", 0.69, ip("expired medicine parent")),
            ("med1", "player use expired pills", 1.16, ip("expired medicine parent")),
            ("adr0", "player use adrenaline", 0.99, ip("adrenaline", "adrenaline cap2")),
            ("adr1", "player use adrenaline", 2.32, ip("adrenaline", "adrenaline cap2")),
            ("inv0", "player use inverter", 0.54, ip("inverter parent")),
            ("inv1", "player use inverter", 1.27, ip("inverter parent"))):
        gun = GUN in roots
        out.append(shot(f"P_{name}", "T", base(True, [A_PLAYER, an, t]), hide=NO_DEALER if gun else hide_all,
                        rebase=home if gun else [[PIP, "home"]], fov=HOME_FOV, maskroots=roots, base=P2))
    EP = "shell parent_ejecting player side"
    for k in ("live", "blank"):
        out.append(shot(f"P_eject_{k[0]}", "T", base(True, [EP + "/animator_ejecting shell player", "ejecting shell_player1", 0.25]),
                        hide=hide_all, ejectshell=[EP + "/shell eject manager", k], rebase=[[EP, "home"]], fov=HOME_FOV, maskroots=[EP], base=P2))
    return {"shots": out, "chunk": 3, "size": [320 * SS, 240 * SS]}  # one pose per run: the plate, the pose, its mask


def job_health():
    """The charge display, straight on."""
    ui = [HC + "health UI_dealer side", HC + "health UI_player side", HC + "health UI_dividing line"]
    syms_d = [HC + "health UI_dealer side/symbol_dealer" + s for s in "123456"]
    syms_p = [HC + "health UI_player side/symbol_player" + s for s in "123456"]
    txt = [HC + "health UI_dealer side/text_dealer", HC + "health UI_player side/text_player"]
    V = dict(campos=HVIEW[0], camrot=HVIEW[1], fov=HVIEW[2])
    allsym = syms_d + syms_p + [txt[1]]
    frame = plate_of(anims=base(), show=ui, hide2=allsym, **V)
    plate = plate_of(anims=base(), hide=[], hide2=[], **V)
    out = [shot("H_plate", None, base(), **V),
           shot("H_frame", None, base(), show=ui, hide2=allsym, base=plate, **V)]
    for k, p in enumerate(syms_d + syms_p):
        out.append(shot(f"H_sym{k}", None, base(), show=ui + [p], hide2=[q for q in allsym if q != p], base=frame, **V))
    for k, p in enumerate([syms_d[0], syms_d[1], syms_p[0], syms_p[1]]):
        sk = p + "/" + p.rsplit("/", 1)[1] + " skull"
        out.append(shot(f"H_skull{k}", None, base(), show=ui + [p], hide2=[q for q in allsym if q != p],
                        layers=[[sk, 1, True], [p, 1, False]], base=frame, **V))
    ri = [HC + "round indicator parent", HC + "round indicator parent/round indicator"]
    blink = base(True, [HC + "round indicator parent/animator_round blinker", "round blinking", 0.05])
    r0 = plate_of(anims=blink, show=[ri[0]], hide2=[ri[1]], **V)
    out.append(shot("H_round0", None, blink, show=[ri[0]], hide2=[ri[1]], base=plate, **V))
    for k in range(1, 4):
        out.append(shot(f"H_round{k}", None, blink, show=ri, xform=[[ri[1], RIND[k], None]], base=r0, **V))
    endless = [["res://misc/mat_round indicator main.tres", "albedo_texture", "res://misc/display rounds endless png.png"]]
    for k in range(1, 4):
        out.append(shot(f"H_endless{k}", None, blink, show=ri, xform=[[ri[1], RIND[k], None]], base=r0, resources=endless, **V))
    out.append(shot("H_err_p", None, base(), show=[HCM + "health counter error_player"], base=plate, **V))
    out.append(shot("H_err_d", None, base(), show=[HCM + "health counter error_dealer"], base=plate, **V))
    # the same display on the table, seen from the table view: its lit symbols
    TV = dict(zip(("campos", "camrot", "fov"), VIEWS["T"]))
    tframe = plate_of(anims=base(), hide=NO_DEALER + [GUN], show=ui, hide2=allsym, **TV)
    tplate = plate_of(anims=base(), hide=NO_DEALER + [GUN], **TV)
    out.append(shot("C_frame", None, base(), hide=NO_DEALER + [GUN], show=ui, hide2=allsym, base=tplate, **TV))
    for k, p in enumerate(syms_d + syms_p):
        out.append(shot(f"C_sym{k}", None, base(), hide=NO_DEALER + [GUN], show=ui + [p], hide2=[q for q in allsym if q != p],
                        base=tframe, **TV))
    return {"shots": out, "disable": ["standalone managers"], "size": [320 * SS, 240 * SS]}


def job_shells():
    """The shell compartment and the eight shells, live and blank."""
    a = base(True, [A_SHELLS, "show shells", 0.3])
    pl = plate_of(anims=a, socket="shell compartment")
    out = [shot("S_plate", None, a, socket="shell compartment")]
    for i in range(8):
        for kind in ("live", "blank"):
            out.append(shot(f"S_{kind[0]}{i}", None, a, socket="shell compartment", shells=[(kind, i)], base=pl))
    return {"shots": out, "chunk": 4, "size": [320 * SS, 240 * SS]}


SIGN = "signature machine2/signature machine main parent/baseplate main/"


def job_scenes():
    """Full-size plates: the waiver, the offer machine, the car of the ending."""
    out = [
        shot("W_waiver", None, base(True, ["standalone managers/signature manager/animator_pickup waiver", "pickup waiver", 2.6]),
             socket="home", show=["signature machine2/signature machine main parent"],
             hide2=[SIGN + "screen parent/entry marker"],
             props=[[SIGN + f"screen parent/letter_{i}", "text", ""] for i in range(6)] +
                   [[SIGN + f"release form1_003/letter_signed_joined_{i}", "text", ""] for i in range(6)]),
        shot("Y_don", None, base(True, ["double nothing machine1/animator_double nothing machine", "show", 1.15]), socket="yes no"),
        shot("E_car", None, base(), socket="player vehicle", env=ENDING_ENV),
    ]
    return {"shots": out, "chunk": 1, "size": [320 * SS, 240 * SS]}


def job_rooms():
    """The rooms around the table: bathroom, revival, hallway."""
    I = "intro parent/"
    PILL = "restroom_CLUB/pill bottle main"
    out = [
        shot("B_bath", None, base(), socket="restroom", hide=[PILL]),
        shot("B_pills", None, base(), socket="restroom", show=[PILL], show2=[PILL],
             base=plate_of(anims=base(), socket="restroom", hide=[PILL])),
        shot("B_revive", None, base(True, [I + "animator_intro", "camera player revival", 1.0],
                                    [I + "smoker dude revive player1/animator_smoker dude revival", "revive player", 1.6]),
             show=[I + "smoker dude revive player1"]),
        shot("L_hall", None, base(), socket="main lobby"),
    ]
    return {"shots": out, "chunk": 2, "size": [320 * SS, 240 * SS]}


def job_other():
    """Heaven, as the scene plays by itself for a few seconds."""
    return {"shots": [shot("X_heaven", None, [], reset=False, wait=400)], "scene": "res://scenes/heaven.tscn",
            "disable": [], "warmup": 200, "size": [320 * SS, 240 * SS]}


def job_icon():
    """The app icon: the Dealer's face."""
    out = [shot(f"I_face{f}", None, base(), campos=[-13.59, 7.323, 0.024], camrot=[-4.2, -89.2, 0], fov=f, hide=[GUN, DEALER_HANDS])
           for f in (13, 15, 17)]
    return {"shots": out, "size": [55 * SS, 56 * SS]}


JOBS = {"icon": job_icon, "table": job_table, "poses": job_poses, "dealer": job_dealer, "health": job_health, "shells": job_shells,
        "scenes": job_scenes, "rooms": job_rooms, "heaven": job_other}


def paired(shots):
    """Renders each sprite's plate right before it (base=dict of overrides for the plate),
    so that any state an earlier shot left behind is in both and cancels out; and
    for a shot with "maskroots", its mask pass right after it (the posed objects alone)."""
    out = []
    for sh in shots:
        b = sh.pop("base", None)
        if b is not None:
            p = dict(sh)
            p.update(b)
            p["hide2"] = list(p.get("hide2", [])) + [RELEASE_FORM]
            p.pop("maskroots", None)
            p["name"] = sh["name"] + "__base"
            out.append(p)
        out.append(sh)
        if sh.get("maskroots"):
            out.append(dict(sh, name=sh["name"] + "__mask", maskpass=True))
    return out


def run(name):
    job = JOBS[name]()
    shots = paired(job.pop("shots"))
    out = OUT / name
    out.mkdir(parents=True, exist_ok=True)
    job.setdefault("out", str(out))
    job.setdefault("disable", ["."])
    job.setdefault("fresh", False)
    job.setdefault("wait", 8)
    n = job.pop("chunk", 400)  # Godot slows down after many scene reloads: several processes
    for k in range(0, len(shots), n):
        part = dict(job, shots=shots[k:k + n])
        spec = OUT / f"{name}.json"
        spec.write_text(json.dumps(part))
        env = dict(os.environ, SHOTS_FILE=str(spec))
        r = subprocess.run([GODOT, "--path", PROJECT, "-s", str(HERE / "render" / "director.gd")], env=env,
                           capture_output=True, text=True)
        for line in r.stdout.splitlines():
            if line.startswith(("MISSING", "NOANIM", "NOITEM")):
                print(name, line)
    print(f"{name}: {len(shots)} shots -> {out}")

if __name__ == "__main__":
    if not PROJECT:
        sys.exit("set BR_PROJECT to the game's Godot project")
    for j in sys.argv[1:] or JOBS:
        run(j)
