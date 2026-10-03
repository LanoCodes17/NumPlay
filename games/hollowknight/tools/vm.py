"""The game's own scripts (PlayMaker FSMs) of the characters and set pieces this port runs as they are: compiled to a
small bytecode that src/vm.c interprets. A room's VM data: the objects the scripts use (their places, sprites, triggers,
children), then the FSMs (each a definition, shared by those made from the same template, and its variables' values).

Values are 32-bit slots: a definition's variables first (in RAM), then its constants (in the data). Objects are numbers:
the room's VM objects, or the game's own (the Knight, the dialogue box, the area title...)."""
import struct
import numpy as np
import unity, text, state

# rooms -> the objects whose scripts run here (and all under them)
ROOMS = {
    "Town": ["_NPCs/Elderbug", "_NPCs/Tiso Town NPC"],
    "Room_temple": ["Quirrel"],
    "Crossroads_47": ["_NPCs/Tiso Bench NPC"],
    "Room_ruinhouse": ["Sly Dazed"],
    "Fungus1_04": ["Hornet Infected Knight Encounter", "Hornet Saver", "Cloak Corpse", "Dream Scene Activate",
                   "Dreamer Scene 1"],
    "Crossroads_ShamanTemple": ["_Props/Shaman Meeting", "_Props/Shaman Trapped", "_Props/Shaman Killed Blocker",
                                "_Props/Knight Get Fireball", "Battle Scene/Reminder Cast"],
    # Hornet seen in Greenpath before her arena
    "Fungus1_02": ["Hornet Encounter GP1"],
    "Fungus1_03": ["Set Hornet Encounter"],
    "Fungus1_17": ["Set Hornet Encounter"],
    "Fungus1_31": ["Hornet Encounter Control"],
}
# FSMs left out (what this port does not have: the dream nail, sounds...)
SKIP_FSMS = {"npc_dream_dialogue", "Dream Dialogue", "Rotate", "Shop Open Voice"}
# objects left out (effects drawn by the C code, or nothing at all; the dream nail's; Dreamer Scene 1's Knight Lift,
# which nothing turns on)
SKIP_CLASSES = {"SpellGetOrb", "ParticleSystem"}
SKIP_NAMES = {"Dream Dialogue", "Dream Dialogue Flower", "Flower", "Flower Give", "white_light", "white_light 1",
              "Knight Lift"}
MAX_OBJS, MAX_FSMS, MAX_VARS = 32, 24, 256   # (src/vm.c)

# the game's own objects
SPECIAL = {"Hero": 0xFF00, "HeroLight": 0xFF01, "DialogueManager": 0xFF02, "DialogueText": 0xFF03, "AreaTitle": 0xFF04,
           "CameraParent": 0xFF05, "MainCamera": 0xFF06, "GameManager": 0xFF07, "HUD Blanker": 0xFF08,
           "DialogueTextYN": 0xFF09, "UIManager": 0xFF0A, "Enemy Dream Msg": 0xFF0B, "HUD Blanker White": 0xFF0D}
O_NONE = 0xFFFF
# PlayerData ints (src/vm.c: pd_int)
PD_INTS = ["MPCharge", "health", "maxHealth", "geo", "fireballLevel", "quakeLevel", "screamLevel", "shaman", "elderbug",
           "permadeathMode", "nailDamage", "hornetGreenpath", "quirrelEggTemple"]
# PlayerData bools kept elsewhere (src/vm.c)
PD_SPECIAL = {"disablePause": 0xFFF0, "hasSpell": 0xFFF1, "canDash": 0xFFF2}
# PlayerData bools that keep their new game value all through this part of the game
PD_CONST = {"backerCredits": False, "equippedCharm_10": False, "elderbugGaveFlower": False, "elderbugRequestedFlower": False,
            "xunFlowerBroken": False, "hasXunFlower": False, "openedBlackEggDoor": False,
            "visitedCrossroadsInfected": False, "defeatedNightmareGrimm": False, "jijiDoorUnlocked": False,
            "visitedCliffs": False, "mineLiftOpened": False, "brettaRescued": False, "elderbugHistory2": False,
            "shamanFireball2Convo": False, "shamanScreamConvo": False, "shamanScream2Convo": False,
            "shamanQuakeConvo": False, "shamanQuake2Convo": False, "hasDreamNail": False, "equippedCharm_19": False,
            "equippedCharm_11": False, "dungDefenderEncounterReady": False, "elderbugSpeechBretta": False,
            "elderbugSpeechJiji": False, "elderbugSpeechKingsPass": False, "elderbugSpeechMinesLift": False,
            "elderbugSpeechInfectedCrossroads": False, "elderbugSpeechFinalBossDoor": False, "hasDoubleJump": False,
            "hasSuperDash": False, "hasWalljump": False, "mageLordDefeated": False, "elderbugConvoGrimm": False,
            "elderbugNymmConvo": False, "elderbugTroupeLeftConvo": False, "elderbugBrettaLeft": False,
            "hasAcidArmour": False}


class Strings:
    """Strings the scripts use as values (clip names, events, convo keys...): numbered."""
    def __init__(self):
        self.list, self.index = [], {}

    def id(self, s):
        if s not in self.index:
            self.index[s] = len(self.list)
            self.list.append(s)
        return self.index[s]


STR = Strings()
EVENTS = Strings()
for e in ("FINISHED", "CONVO_FINISH", "CONVO START", "CONVO END", "BIG TITLE START", "BIG TITLE END", "HERO DAMAGED",
          "NPC TITLE DOWN", "NPC CONVO START", "BOX UP", "BOX DOWN", "LEAVING SCENE", "TAKE DAMAGE", "GET ITEM MSG END",
          "HORNET LEAVE", "BG CLOSE", "BG QUICK CLOSE", "BG OPEN", "BG QUICK OPEN", "BG DESTROY", "WAKE", "BOX UP DREAM",
          "BOX DOWN DREAM", "FADE IN", "FADE OUT", "FSM CANCEL"):
    EVENTS.id(e)
FIXED_EVENTS = len(EVENTS.list)   # (src/data.h: VMEV_*)

# ---------------------------------------------------------------- the actions it runs: name -> (opcode, operands)
# operand kinds: f i b s o value slots (u16), F I B S O V output variables (u8: 255 none), e event (u8), t event target,
# p PlayerData bool (u16 flag; 0xFFFE false, 0xFFFD true), q PlayerData int (u8), x text (u16), n byte, k float,
# B* / b* lists
OPS = {}


def op(name, *params):
    OPS[name] = (len(OPS) + 1, params)


op("Nop")
op("Wait", ("time", "f"), ("finishEvent", "e"))
op("NextFrameEvent", ("sendEvent", "e"))
op("SendEvent", ("eventTarget", "t"), ("sendEvent", "e"), ("delay", "f"))
op("SendEventByName", ("eventTarget", "t"), ("sendEvent", "es"), ("delay", "f"))
op("SetBoolValue", ("boolVariable", "B"), ("boolValue", "b"), ("everyFrame", "n"))
op("SetFloatValue", ("floatVariable", "F"), ("floatValue", "f"), ("everyFrame", "n"))
op("SetIntValue", ("intVariable", "I"), ("intValue", "i"), ("everyFrame", "n"))
op("SetStringValue", ("stringVariable", "S"), ("stringValue", "s"), ("everyFrame", "n"))
op("SetGameObject", ("variable", "O"), ("gameObject", "o"), ("everyFrame", "n"))
op("GetOwner", ("storeGameObject", "O"))
op("GetHero", ("storeResult", "O"))
op("FindChild", ("gameObject", "o"), ("childName", "s"), ("storeResult", "O"))
op("GetParent", ("gameObject", "o"), ("storeResult", "O"))
op("FloatAdd", ("floatVariable", "F"), ("add", "f"), ("everyFrame", "n"), ("perSecond", "n"))
op("FloatSubtract", ("floatVariable", "F"), ("subtract", "f"), ("everyFrame", "n"), ("perSecond", "n"))
op("FloatMultiply", ("floatVariable", "F"), ("multiplyBy", "f"), ("everyFrame", "n"))
op("FloatCompare", ("float1", "f"), ("float2", "f"), ("tolerance", "f"), ("equal", "e"), ("lessThan", "e"),
   ("greaterThan", "e"), ("everyFrame", "n"))
op("FloatTestToBool", ("float1", "f"), ("float2", "f"), ("tolerance", "f"), ("equalBool", "B"), ("lessThanBool", "B"),
   ("greaterThanBool", "B"), ("everyFrame", "n"))
op("FloatInRange", ("floatVariable", "f"), ("lowerValue", "f"), ("upperValue", "f"), ("boolVariable", "B"),
   ("trueEvent", "e"), ("falseEvent", "e"), ("everyFrame", "n"))
op("IntCompare", ("integer1", "i"), ("integer2", "i"), ("equal", "e"), ("lessThan", "e"), ("greaterThan", "e"),
   ("everyFrame", "n"))
op("IntTestToBool", ("int1", "i"), ("int2", "i"), ("equalBool", "B"), ("lessThanBool", "B"), ("greaterThanBool", "B"),
   ("everyFrame", "n"))
op("IntCompareToBool", ("integer1", "i"), ("integer2", "i"), ("equalBool", "B"), ("lessThanBool", "B"),
   ("greaterThanBool", "B"), ("everyFrame", "n"))
op("IntAdd", ("intVariable", "I"), ("add", "i"), ("everyFrame", "n"))
op("IntSwitch", ("intVariable", "i"), ("compareTo", "i*"), ("sendEvent", "e*"), ("everyFrame", "n"))
op("BoolTest", ("boolVariable", "b"), ("isTrue", "e"), ("isFalse", "e"), ("everyFrame", "n"))
op("BoolAllTrue", ("boolVariables", "b*"), ("sendEvent", "e"), ("storeResult", "B"), ("everyFrame", "n"))
op("BoolAnyTrue", ("boolVariables", "b*"), ("sendEvent", "e"), ("storeResult", "B"), ("everyFrame", "n"))
op("BoolTestMulti", ("boolVariables", "b*"), ("boolStates", "b*"), ("trueEvent", "e"), ("falseEvent", "e"),
   ("storeResult", "B"), ("everyFrame", "n"))
op("StringCompare", ("stringVariable", "s"), ("compareTo", "s"), ("equalEvent", "e"), ("notEqualEvent", "e"),
   ("storeResult", "B"), ("everyFrame", "n"))
op("GetPlayerDataBool", ("boolName", "p"), ("storeValue", "B"))
op("SetPlayerDataBool", ("boolName", "p"), ("value", "b"))
op("PlayerDataBoolTest", ("boolName", "p"), ("isTrue", "e"), ("isFalse", "e"))
op("PlayerDataBoolTrueAndFalse", ("trueBool", "p"), ("falseBool", "p"), ("isTrue", "e"), ("isFalse", "e"))
op("GetPlayerDataInt", ("intName", "q"), ("storeValue", "I"))
op("SetPlayerDataInt", ("intName", "q"), ("value", "i"))
op("ActivateGameObject", ("gameObject", "o"), ("activate", "b"), ("recursive", "n"), ("resetOnExit", "n"),
   ("everyFrame", "n"))
op("SetSpriteRenderer", ("gameObject", "o"), ("active", "b"))
op("SetMeshRenderer", ("gameObject", "o"), ("active", "b"))
op("SetCollider", ("gameObject", "o"), ("active", "b"))
op("DestroyObject", ("gameObject", "o"), ("delay", "f"))
op("DestroySelf")
op("DestroyAllChildren", ("gameObject", "o"))
op("GetPosition", ("gameObject", "o"), ("vector", "V3"), ("x", "F"), ("y", "F"), ("z", "F"), ("space", "n"),
   ("everyFrame", "n"))
op("SetPosition", ("gameObject", "o"), ("vector", "v3"), ("x", "f"), ("y", "f"), ("z", "f"), ("space", "n"),
   ("everyFrame", "n"))
op("GetScale", ("gameObject", "o"), ("xScale", "F"), ("yScale", "F"))
op("SetScale", ("gameObject", "o"), ("x", "f"), ("y", "f"), ("everyFrame", "n"))
op("SetVelocity2d", ("gameObject", "o"), ("x", "f"), ("y", "f"), ("everyFrame", "n"))
op("SetGravity2dScale", ("gameObject", "o"), ("gravityScale", "f"))
op("CheckTargetDirection", ("gameObject", "o"), ("target", "o"), ("aboveEvent", "e"), ("belowEvent", "e"),
   ("rightEvent", "e"), ("leftEvent", "e"), ("everyFrame", "n"))
op("FaceObject", ("objectA", "o"), ("objectB", "o"), ("spriteFacesRight", "n"), ("playNewAnimation", "n"),
   ("newAnimationClip", "s"), ("resetFrame", "n"), ("everyFrame", "n"))
op("Tk2dPlayAnimation", ("gameObject", "o"), ("clipName", "s"))
op("Tk2dPlayAnimationWithEvents", ("gameObject", "o"), ("clipName", "s"), ("animationTriggerEvent", "e"),
   ("animationCompleteEvent", "e"))
op("Tk2dWatchAnimationEvents", ("gameObject", "o"), ("animationTriggerEvent", "e"), ("animationCompleteEvent", "e"))
op("Tk2dPlayFrame", ("gameObject", "o"), ("frame", "i"))
op("Tk2dSpriteGetId", ("gameObject", "o"), ("spriteID", "I"), ("everyframe", "n"))
op("Trigger2dEvent", ("trigger", "n"), ("sendEvent", "e"), ("tag", "n"))
op("ListenForUp", ("wasPressed", "e"), ("isPressed", "e"))
op("ListenForDown", ("wasPressed", "e"), ("isPressed", "e"))
op("ListenForLeft", ("wasPressed", "e"))
op("ListenForRight", ("wasPressed", "e"))
op("ListenForAttack", ("wasPressed", "e"))
op("ListenForJump", ("wasPressed", "e"))
op("ListenForCast", ("wasPressed", "e"))
op("CheckTrackTriggerCount", ("count", "i"), ("test", "n"), ("successEvent", "e"), ("everyFrame", "n"))
op("ShowPromptMarker", ("labelName", "s"), ("spawnPoint", "o"), ("storeObject", "O"))
op("HidePromptMarker", ("storedObject", "o"))
op("SetFsmBool", ("gameObject", "o"), ("fsmName", "s"), ("variableName", "s"), ("setValue", "b"), ("slot", "n"))
op("SetFsmString", ("gameObject", "o"), ("fsmName", "s"), ("variableName", "s"), ("setValue", "s"), ("slot", "n"))
op("StartConversation", ("text", "x"))
op("HeroCall", ("method", "n"), ("store", "B"), ("a", "f"))
op("Shake", ("kind", "n"))
op("SetRumble", ("kind", "n"), ("on", "b"))
op("EaseFloat", ("fromValue", "f"), ("toValue", "f"), ("floatVariable", "F"), ("time", "f"), ("finishEvent", "e"))
op("CameraZoom", ("z", "f"), ("time", "f"), ("delay", "f"))
op("Collision2dEvent", ("sendEvent", "e"))
op("HudBlanker", ("alpha", "f"), ("time", "f"), ("on", "n"))
op("CreateObject", ("gameObject", "o"), ("x", "f"), ("y", "f"), ("storeObject", "O"), ("spawnPoint", "o"))
op("GetItemMsg", ("item", "n"))
op("Blanker", ("alpha", "f"), ("everyFrame", "n"))
op("BlankerOn", ("on", "b"))
op("FindGameObject", ("objectName", "s"), ("store", "O"))
op("SetParent", ("gameObject", "o"))
op("IntOp", ("intVariable", "I"), ("value", "i"))
op("ActivateAllChildren", ("gameObject", "o"), ("activate", "b"))
op("iTweenMoveBy", ("gameObject", "o"), ("vector", "v3"), ("time", "f"), ("easeType", "n"), ("loopType", "n"),
   ("finishEvent", "e"))
op("SetFsmFloat", ("gameObject", "o"), ("fsmName", "s"), ("variableName", "s"), ("setValue", "f"), ("slot", "n"))
op("TextAlign", ("centre", "n"))
op("ObjAlpha", ("gameObject", "o"), ("alpha", "f"), ("everyFrame", "n"))
op("BuildString")   # (custom: its parts, the strings they can make, where it keeps it)
op("StartConversationOf")   # (custom: its key and sheet, then the conversations they can name)
op("GetObjAlpha", ("gameObject", "o"), ("store", "F"))

# HeroController's methods the scripts call (HeroCall's method)
HERO_METHODS = ["RelinquishControl", "RegainControl", "StopAnimationControl", "StartAnimationControl", "FaceLeft",
                "FaceRight", "CanTalk", "PreventCastByDialogueEnd", "SetBackOnGround", "AddMPCharge",
                "FindGroundPoint", "SetBenchRespawn", "SetHazardRespawn", "RelinquishControlNotVelocity",
                "SetCState", "SaveGame", "AffectedByGravity", "ResetHardLandingTimer", "StopPlayingAudio", "CanInspect"]
# the prompt marker the pool gives (Arrow Prompt New): a script's own, shown and hidden as ShowPromptMarker's
PROMPT_PREFAB = ("resources.assets", 6142)
# prefabs CreateObject makes that the scripts go on with (made beforehand, off): (file, path id)
PREFAB_SPAWNS = {("sharedassets76.assets", 69), ("sharedassets133.assets", 23), ("sharedassets6.assets", 509)}
# prefabs the pool gives (SpawnObjectFromGlobalPool) that the scripts show (made beforehand, off)
POOL_SPAWNS = {("resources.assets", 5267)}
# the game's objects the scripts find by name that are not theirs (enemies): what they are to them
GAME_OBJECTS = {"Hornet Boss 1": 0xFF0C}
# what a trigger's Trigger2dEvent hears, by collideTag: the Knight (any), a spell
TRIGGER_TAGS = {None: 0, "": 0, "Player": 0, "Untagged": 0, "Hero Spell": 1}
# the items the message shows (text.MSGS's order), by Msg Control's Item
MSG_ITEMS = {"Fireball": 0, "Dash": 1}
# camera shake events
SHAKES = {"EnemyKillShake": 1, "AverageShake": 2, "BigShake": 3, "SmallShake": 4}


class Room:
    """A room's VM data being built."""
    def __init__(self, name, d, sprites, texts, actor_of):
        self.name, self.d = name, d
        self.sprites, self.texts, self.actor_of = sprites, texts, actor_of
        self.by_id = {o["id"]: o for o in d["objects"]}
        self.by_path = {o["path"]: o for o in d["objects"]}
        for o in d["objects"]:
            o["_scene"], o["_raw"] = name, o["id"]
        # (the scenes it loads with it, the first of them: their objects there only as they are, by a PlayerData bool)
        import scene
        for an, flag, val, which in scene.additive(name):
            if which != 1:
                continue
            da = unity.scene(an)
            ids = {q["id"] for q in da["objects"]}
            k = lambda i, an=an: ("sc", an, i)
            for q in da["objects"]:
                c = dict(q)
                c["id"] = k(q["id"])
                c["parent"] = k(q["parent"]) if q.get("parent") in ids else None
                c["children"] = [k(x) for x in q.get("children", [])]
                c["_doc"], c["_key"], c["_scene"], c["_raw"] = da, k, an, q["id"]
                if c["parent"] is None:
                    c["_variant"] = (flag, val)
                self.by_id[c["id"]] = c
                self.by_path.setdefault(q["path"], c)
        self.objs, self.obj_index = [], {}
        self.fsms = []
        self.problems = []

    # ------------------------------------------------------------ objects
    def external_vars(self):
        """the variables FSMs set in other FSMs (SetFsmBool, PersistentBoolItem's Activated)"""
        if not hasattr(self, "_ext"):
            self._ext = {"Activated"}
            for o in list(self.by_id.values()):
                for c in o["c"]:
                    for st in (c.get("fsm") or {}).get("states", []):
                        for a in st["actions"]:
                            if a["name"].startswith("SetFsm"):
                                self._ext.add(_one_string(self, c["fsm"], dict(a["params"]).get("variableName")))
        return self._ext

    def want(self, o):
        cls = {c.get("class") or c["type"] for c in o["c"]}
        return not (cls & SKIP_CLASSES) and o["name"] not in SKIP_NAMES

    def add_tree(self, o):
        if not self.want(o):
            return
        self.obj(o)
        for c in o.get("children", []):
            self.add_tree(self.by_id[c])

    def obj(self, o):
        if o["id"] in self.obj_index:
            return self.obj_index[o["id"]]
        p = self.by_id.get(o.get("parent"))
        if p is not None and p["id"] not in self.obj_index and self.want(p) and any(
                o["path"].startswith(q + "/") for q in ROOMS.get(self.name, [])):
            self.obj(p)
        self.obj_index[o["id"]] = len(self.objs)
        self.objs.append(o)
        return self.obj_index[o["id"]]

    def add_prefab(self, f, pid, site=None):
        """A prefab's objects, made beforehand (off): its root's VM object (one for each site that makes it)."""
        key = ("pf", f, pid, site)
        if key in self.obj_index:
            return self.obj_index[key]
        d2 = unity.prefab(f, pid)
        ids = {q["id"] for q in d2["objects"]}
        k = lambda i: ("pf", f, pid, site, i)
        root = None
        for q in d2["objects"]:
            c = dict(q)
            c["id"] = k(q["id"])
            c["parent"] = k(q["parent"]) if q.get("parent") in ids else None
            c["children"] = [k(x) for x in q.get("children", [])]
            c["_doc"], c["_key"] = d2, k
            self.by_id[c["id"]] = c
            if c["parent"] is None:
                root = c
        root["active"] = root["self_active"] = False
        self.add_tree(root)
        self.obj_index[key] = self.obj_index[root["id"]]
        return self.obj_index[root["id"]]

    def ref_obj(self, r, key=None):
        """["ref", file, pid] -> a VM object (this scene's, or this prefab's, objects only)"""
        if not r or r[1] != 0:
            return O_NONE
        o = self.by_id.get(key(r[2]) if key else r[2])
        if o is None:
            return O_NONE
        if o["id"] not in self.obj_index:
            self.add_tree(o)
        return self.obj_index.get(o["id"], O_NONE)


def _string_values(rm, f, v, depth=0):
    """the strings a string operand of an FSM can be (constants: itself; a variable: as it starts and as it is set,
    by other FSMs or its own SetStringValue and BuildString)"""
    import itertools
    if not (isinstance(v, str) and v.startswith("$")):
        return [v or ""]
    n = v[1:]
    out = [f["vars"].get(n, ["string", ""])[1] or ""]
    for o in list(rm.by_id.values()):
        for c in o["c"]:
            for st in (c.get("fsm") or {}).get("states", []):
                for a in st["actions"]:
                    P = dict(a["params"])
                    if a["name"] == "SetFsmString" and P.get("fsmName") == f["name"] and P.get("variableName") == n:
                        out.append(P.get("setValue") or "")
    for st in f["states"]:
        for a in st["actions"]:
            if not a.get("enabled", True):
                continue
            P = dict(a["params"])
            if a["name"] == "SetStringValue" and P.get("stringVariable") == v and depth < 4:
                out += _string_values(rm, f, P.get("stringValue"), depth + 1)
            elif a["name"] == "BuildString" and P.get("storeResult") == v and depth < 4:
                sep = P.get("separator") or ""
                parts = [_string_values(rm, f, q, depth + 1) for q in P.get("stringParts") or []]
                for row in itertools.product(*parts):
                    out.append(sep.join(row) + (sep if P.get("addToEnd") else ""))
    return sorted(set(out))


def _one_string(rm, f, v):
    """a string operand: its one value if a variable can be only that, else itself"""
    vals = _string_values(rm, f, v) if isinstance(v, str) and v.startswith("$") else [v]
    return vals[0] if len(vals) == 1 else v


def _active_chain(rm, o):
    while o is not None:
        if not o["active"]:
            return False
        o = rm.by_id.get(o.get("parent"))
    return True


# ---------------------------------------------------------------- compiling an FSM
def _slots(vars_):
    """Its variables -> {name: (slot, type)}, the number of slots (vectors take 3, colors 4)."""
    out, n = {}, 0
    for name, (t, _) in vars_.items():
        out[name] = (n, t)
        n += {"vector3": 3, "vector2": 2, "color": 4, "rect": 4}.get(t, 1)
    return out, n


def _u32(v):
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, float):
        return struct.unpack("<I", struct.pack("<f", v))[0]
    return v & 0xFFFFFFFF


OWNER = 0xFF0F


class Compiler:
    def __init__(self, rm, o, f):
        self.rm, self.o, self.f = rm, o, f
        self.slots, self.nvars = _slots(f["vars"])
        self.consts = []
        self.states = {s["name"]: i for i, s in enumerate(f["states"])}
        self.where = ""
        # (a prompt marker it spawns from the pool, Arrow Prompt New: its spawn point, where it keeps it, its label)
        self.prompt = None
        for st in f["states"]:
            for a in st["actions"]:
                P = dict(a["params"])
                if a["name"] == "SpawnObjectFromGlobalPool" and P.get("storeObject"):
                    import ents
                    r = P.get("gameObject")
                    if isinstance(r, (list, tuple)) and ents._ref_file(o.get("_doc") or rm.d, [r[1], r[2]]) == PROMPT_PREFAB:
                        self.prompt = [P.get("spawnPoint"), P.get("storeObject"), None]
        for st in f["states"] if self.prompt else []:
            for a in st["actions"]:
                P = dict(a["params"])
                if a["name"] == "SetFsmString" and P.get("gameObject") == self.prompt[1] and P.get("variableName") == "Prompt Name":
                    self.prompt[2] = P.get("setValue")

    def alpha(self, c):
        """a color operand's alpha -> its value slot, None if not one"""
        if isinstance(c, str) and c[1:] in self.slots and self.slots[c[1:]][1] == "color":
            return self.slots[c[1:]][0] + 3
        if isinstance(c, (list, tuple)) and len(c) == 4:
            return self.value(float(c[3]), "f")
        return None

    def string_values(self, v):
        """the strings a string operand can be (constants: itself; a variable: as it starts and as it is set)"""
        return _string_values(self.rm, self.f, v)

    def build_string(self, P):
        """BuildString: each way its parts can be, the string they make (with its separator)"""
        import itertools
        parts = P.get("stringParts") or []
        sep = P.get("separator") or ""
        store = self.store(P.get("storeResult"))
        rows = list(itertools.product(*[self.string_values(q) for q in parts]))
        assert len(rows) < 64, rows
        body = bytes([len(parts)]) + b"".join(struct.pack("<H", self.value(q, "s")) for q in parts) + bytes([len(rows)])
        for r in rows:
            body += struct.pack("<H", STR.id(sep.join(r) + (sep if P.get("addToEnd") else "")))
            body += b"".join(struct.pack("<H", STR.id(x)) for x in r)
        body += bytes([store])
        return bytes([OPS["BuildString"][0], len(body)]) + body

    def fsm_slot(self, fsm_name, var):
        """another FSM's variable, by its FSM's name and its own (one of the room's): its slot, 255 not known"""
        for o in list(self.rm.by_id.values()):
            for c in o["c"]:
                q = c.get("fsm")
                if q and q["name"] == fsm_name and var in q["vars"]:
                    return _slots(q["vars"])[0][var][0]
        return 255

    def problem(self, msg):
        self.rm.problems.append("%s: %s: %s: %s: %s" % (self.rm.name, self.o["path"], self.f["name"], self.where, msg))

    def const(self, v):
        v = _u32(v)
        if v not in self.consts:
            self.consts.append(v)
        return self.nvars + self.consts.index(v)

    def obj_value(self, v):
        if v == "Owner":
            return OWNER
        if isinstance(v, (list, tuple)) and v and v[0] == "ref":
            return self.rm.ref_obj(v, self.o.get("_key"))
        return O_NONE

    def value(self, v, kind):
        """an input operand -> its slot (u16)"""
        if v is None:
            return 0xFFFF
        if isinstance(v, str) and v.startswith("$"):
            n = v[1:]
            if n in self.slots:
                return self.slots[n][0]
            if n in SPECIAL:
                return self.const(SPECIAL[n])
            self.problem("variable %s unknown" % n)
            return 0xFFFF
        if kind == "o":
            if isinstance(v, tuple) and v[0] == "objindex":
                return self.const(v[1])
            return self.const(self.obj_value(v))
        if kind == "s":
            return self.const(STR.id(v if isinstance(v, str) else str(v)))
        if kind == "f":
            return self.const(float(v))
        if kind in ("i", "b"):
            return self.const(int(v))
        self.problem("value %r for %s" % (v, kind))
        return 0xFFFF

    def store(self, v):
        if v is None:
            return 255
        if isinstance(v, str) and v.startswith("$") and v[1:] in self.slots:
            return self.slots[v[1:]][0]
        if isinstance(v, str) and v.startswith("$"):
            self.problem("store to %s" % v)
        return 255

    def event(self, v):
        if v is None:
            return 255
        if isinstance(v, (list, tuple)) and v[0] == "event":
            return EVENTS.id(v[1])
        if isinstance(v, str) and not v.startswith("$"):
            return EVENTS.id(v)
        if isinstance(v, str) and v[1:] in self.f["vars"] and self.f["vars"][v[1:]][0] == "string" and \
                v[1:] not in self.rm.external_vars():
            # (an event named by a string nothing changes: that one, or none)
            name = self.f["vars"][v[1:]][1]
            return EVENTS.id(name) if name else 255
        self.problem("event %r" % (v,))
        return 255

    def pd_bool(self, name):
        import ents
        pdf = ents.pd_flags()
        if name in pdf:
            return pdf[name]
        if name in PD_SPECIAL:
            return PD_SPECIAL[name]
        if name in PD_CONST:
            return 0xFFFD if PD_CONST[name] else 0xFFFE
        if name in state.CONSTANT:
            return 0xFFFD if state.CONSTANT[name] else 0xFFFE
        self.problem("PlayerData bool %s" % name)
        return 0xFFFE

    def target(self, v):
        """an event target -> bytes: kind (0 self, 1 an object's FSMs, 2 all, 3 one FSM of an object), object, FSM name"""
        if v == "Self" or v is None:
            return bytes([0]) + struct.pack("<HH", 0xFFFF, 0xFFFF)
        t = v.get("target")
        if t == "BroadcastAll":
            return bytes([2]) + struct.pack("<HH", 0xFFFF, 0xFFFF)
        go = self.value(v.get("go"), "o")
        if t == "GameObject":
            return bytes([1]) + struct.pack("<HH", go, 0xFFFF)
        if t == "GameObjectFSM":
            return bytes([3]) + struct.pack("<HH", go, STR.id(v.get("fsm") or ""))
        self.problem("event target %r" % (v,))
        return bytes([0]) + struct.pack("<HH", 0xFFFF, 0xFFFF)

    def operand(self, kind, v):
        if kind in ("f", "i", "b", "s", "o"):
            return struct.pack("<H", self.value(v, kind))
        if kind in ("F", "I", "B", "S", "O", "V3"):
            return bytes([self.store(v)])
        if kind == "v3":
            if v is None:
                return struct.pack("<H", 0xFFFF)
            if isinstance(v, str):
                return struct.pack("<H", self.value(v, "f"))
            a = self.const(float(v[0]))
            self.const(float(v[1]))
            self.const(float(v[2]))
            return struct.pack("<H", a)
        if kind == "e":
            return bytes([self.event(v)])
        if kind == "es":
            return bytes([self.event(v)])
        if kind == "t":
            return self.target(v)
        if kind == "p":
            if isinstance(v, str) and not v.startswith("$"):
                self.pd_bool(v)   # (known?)
            return struct.pack("<H", self.value(v, "s"))
        if kind == "q":
            if v not in PD_INTS:
                self.problem("PlayerData int %s" % v)
                return bytes([255])
            return bytes([PD_INTS.index(v)])
        if kind == "n":
            return bytes([int(v or 0) & 255])
        if kind == "k":
            return struct.pack("<f", float(v or 0))
        if kind == "x":
            return struct.pack("<H", v)
        if kind in ("i*", "b*", "e*"):
            v = v or []
            out = bytes([len(v)])
            for q in v:
                out += self.operand(kind[0], q)
            return out
        self.problem("operand kind %s" % kind)
        return b""

    def emit(self, name, params):
        code, schema = OPS[name]
        body = b"".join(self.operand(k, params.get(p)) for p, k in schema)
        assert len(body) < 256
        return bytes([code, len(body)]) + body

    # ------------------------------------------------------------ actions
    def action(self, a):
        n = a["name"]
        P = dict(a["params"])
        if n.startswith("SetFsm"):
            # (another FSM's variable named by variables: what they always are)
            P = dict(P, fsmName=_one_string(self.rm, self.f, P.get("fsmName")),
                     variableName=_one_string(self.rm, self.f, P.get("variableName")))
        if n in ("AudioPlayerOneShot", "AudioPlayerOneShotSingle", "AudioStop", "AudioPlay", "AudioPlaySimple",
                 "PlayParticleEmitter", "StopParticleEmitter", "SetParticleEmission", "SetParticleEmissionRate",
                 "ForceHeroFootstepSound", "SendEventToRegister", "AddTrackTrigger", "PlayVibration",
                 "VibrationPlayerStop", "TransitionToAudioSnapshot", "SetAudioPitch", "SetAudioVolume",
                 "AudioPlayInState", "FadeAudio", "PlayRandomSound", "SetRotation", "RandomFloat",
                 "GetLastEvent", "SetBoxCollider2DSize", "Tk2dSpriteSetColor", "SetTextMeshProColor"):
            return None
        if n in ("PlayerDataBoolTest",):
            return self.emit(n, P)
        if n == "CreateUIMsgGetItem":
            return None   # (made by the SetFsmString of its Item that follows)
        if n == "SetFsmString" and self.prompt and P.get("gameObject") == self.prompt[1]:
            return None   # (its prompt's label: kept for UP)
        if n == "SpawnObjectFromGlobalPool" and self.prompt and P.get("storeObject") == self.prompt[1]:
            return None
        if n == "SetFsmString" and P.get("fsmName") == "Msg Control" and P.get("variableName") == "Item":
            if P.get("setValue") not in MSG_ITEMS:
                self.problem("item message %s" % P.get("setValue"))
                return None
            return self.emit("GetItemMsg", {"item": MSG_ITEMS[P.get("setValue")]})
        if n == "SetFsmString":
            return self.emit(n, dict(P, slot=self.fsm_slot(P.get("fsmName"), P.get("variableName"))))
        if n == "EaseColor":
            # (colors: their alphas only, what the scripts change)
            cv = P.get("colorVariable")
            if not (isinstance(cv, str) and cv[1:] in self.slots) or (P.get("easeType") or 0) != 21:
                self.problem("EaseColor to %r" % cv)
                return None
            slot = self.slots[cv[1:]][0] + 3
            fv, tv = self.alpha(P.get("fromValue")), self.alpha(P.get("toValue"))
            if fv is None or tv is None:
                self.problem("EaseColor from %r" % P.get("fromValue"))
                return None
            code, schema = OPS["EaseFloat"]
            body = struct.pack("<HH", fv, tv) + bytes([slot]) + \
                struct.pack("<H", self.value(P.get("time"), "f")) + bytes([self.event(P.get("finishEvent"))])
            return bytes([code, len(body)]) + body
        if n in ("SetColorValue", "GetColorRGBA"):
            # (a color's alpha into another's, or a float)
            src = self.alpha(P.get("color"))
            dst = P.get("colorVariable") if n == "SetColorValue" else P.get("storeAlpha")
            if src is None or not (isinstance(dst, str) and dst[1:] in self.slots):
                return None
            code, _ = OPS["SetFloatValue"]
            body = bytes([self.slots[dst[1:]][0] + (3 if n == "SetColorValue" else 0)]) + struct.pack("<H", src) + \
                bytes([int(P.get("everyFrame") or 0)])
            return bytes([code, len(body)]) + body
        if n == "GetMaterialColor" and isinstance(P.get("color"), str) and P.get("color")[1:] in self.slots:
            # (its alpha into the color's)
            body = struct.pack("<H", self.value(P.get("gameObject") or "Owner", "o")) + \
                bytes([self.slots[P.get("color")[1:]][0] + 3])
            return bytes([OPS["GetObjAlpha"][0], len(body)]) + body
        if n == "SetMaterialColor" and P.get("gameObject") not in ("$HUD Blanker",):
            a = self.alpha(P.get("color"))
            if a is None:
                self.problem("SetMaterialColor %r" % P.get("color"))
                return None
            code, _ = OPS["ObjAlpha"]
            body = struct.pack("<HH", self.value(P.get("gameObject") or "Owner", "o"), a) + bytes([int(P.get("everyFrame") or 0)])
            return bytes([code, len(body)]) + body
        if n == "SetTextMeshProAlignment":
            if P.get("gameObject") != "$DialogueText":
                return None
            return self.emit("TextAlign", {"centre": 1 if P.get("topCentre") else 0})
        if n == "SetFsmFloat":
            return self.emit(n, dict(P, slot=self.fsm_slot(P.get("fsmName"), P.get("variableName"))))
        if n == "BuildString":
            return self.build_string(P)
        if n == "SetMaterialColor" and P.get("gameObject") == "$HUD Blanker":
            c = P.get("color")
            if isinstance(c, str) and c[1:] in self.slots:
                a = self.slots[c[1:]][0] + 3
            elif isinstance(c, (list, tuple)):
                a = self.value(float(c[3]), "f")
            else:
                self.problem("SetMaterialColor %r" % c)
                return None
            code, _ = OPS["Blanker"]
            body = struct.pack("<H", a) + bytes([int(P.get("everyFrame") or 0)])
            return bytes([code, len(body)]) + body
        if n == "SetSpriteRenderer" and P.get("gameObject") == "$HUD Blanker":
            return self.emit("BlankerOn", {"on": P.get("active")})
        if n == "FindGameObject":
            if P.get("objectName") in GAME_OBJECTS:
                return self.emit("SetGameObject", {"variable": P.get("store"),
                                                   "gameObject": ("objindex", GAME_OBJECTS[P.get("objectName")])})
            if P.get("objectName"):
                return self.emit("FindGameObject", P)
            return None   # (by tag: the main camera)
        if n == "CreateObject":
            r = P.get("gameObject")
            if isinstance(r, (list, tuple)) and r and r[0] == "ref":
                import ents
                f, pid = ents._ref_file(self.o.get("_doc") or self.rm.d, [r[1], r[2]])
                if (f, pid) in PREFAB_SPAWNS:
                    pos = P.get("position") or [None, None, None]
                    obj = self.rm.add_prefab(f, pid, (self.o["id"], self.where, P.get("storeObject")))
                    return self.emit("CreateObject", {"gameObject": ("objindex", obj), "x": pos[0], "y": pos[1],
                                                      "storeObject": P.get("storeObject")})
            return None   # (effects)
        if n == "SpawnObjectFromGlobalPool":
            r = P.get("gameObject")
            if isinstance(r, (list, tuple)) and r and r[0] == "ref":
                import ents
                f, pid = ents._ref_file(self.o.get("_doc") or self.rm.d, [r[1], r[2]])
                if (f, pid) in POOL_SPAWNS:
                    pos = P.get("position") or [None, None, None]
                    obj = self.rm.add_prefab(f, pid, (self.o["id"], self.where, P.get("storeObject")))
                    return self.emit("CreateObject", {"gameObject": ("objindex", obj), "x": pos[0], "y": pos[1],
                                                      "storeObject": P.get("storeObject"), "spawnPoint": P.get("spawnPoint")})
            return None
        if n == "GetButtonDown":
            m = {"Jump": "ListenForJump", "Attack": "ListenForAttack", "Cast": "ListenForCast"}.get(P.get("buttonName"))
            return self.emit(m, {"wasPressed": P.get("sendEvent")}) if m else None
        if n == "SetParent":
            return None
        if n == "CallMethodProper":
            beh, m = P.get("behaviour"), P.get("methodName")
            args = P.get("parameters") or []
            if beh == "DialogueBox" and m == "StartConversation":
                arg = lambda q: q["s"] if isinstance(q, dict) else q
                key, sheet = arg(args[0]), arg(args[1])
                keys, sheets = self.string_values(key), self.string_values(sheet)
                import text
                rows = [(k, sh) for k in keys for sh in sheets if k in text.sheets().get(sh, {})]
                if not rows:
                    self.problem("conversation %s %s" % (keys, sheets))
                    return None
                if len(keys) == 1 and len(sheets) == 1:
                    return self.emit("StartConversation", {"text": self.rm.texts.id(sheet, key)})
                # (by what its variables hold: each conversation they can name)
                body = struct.pack("<HHB", self.value(key, "s"), self.value(sheet, "s"), len(rows))
                for k, sh in rows:
                    body += struct.pack("<HHH", STR.id(k), STR.id(sh), self.rm.texts.id(sh, k))
                assert len(body) < 256, rows
                return bytes([OPS["StartConversationOf"][0], len(body)]) + body
            if beh == "GameManager" and m == "CheckCharmAchievements":
                return None
            if beh == "HeroController" and m in HERO_METHODS:
                a0 = args[0]["f"] if args and args[0].get("type") in (5, 0) else 0
                return self.emit("HeroCall", {"method": HERO_METHODS.index(m), "store": P.get("storeResult"), "a": a0})
            self.problem("CallMethodProper %s.%s" % (beh, m))
            return None
        if n == "SendMessage":
            fn = (P.get("functionCall") or {}).get("fn")
            if fn in HERO_METHODS:
                v = (P.get("functionCall") or {}).get("value")
                return self.emit("HeroCall", {"method": HERO_METHODS.index(fn), "store": None,
                                              "a": float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else 0})
            if fn in ("advanceTypewriter", "TimePasses", "StoryRecord_acquired", "StoryRecord_visited", "SetActionString",
                      "RefreshButtonIcon", "StopBounce"):
                return None
            self.problem("SendMessage %s" % fn)
            return None
        if n in ("SendEventByName", "SendEvent"):
            t = P.get("eventTarget")
            ev = P.get("sendEvent")
            evn = ev[1] if isinstance(ev, (list, tuple)) else ev
            go = t.get("go") if isinstance(t, dict) else None
            if go == "$CameraParent" or (isinstance(t, dict) and t.get("fsm") == "CameraShake"):
                if evn in SHAKES:
                    return self.emit("Shake", {"kind": SHAKES[evn]})
                return None
            if go == "$AudioManager":
                return None   # (music)
            if go in ("$DialogueManager",) and evn in ("BOX UP", "BOX DOWN"):
                return self.emit("SendEventByName", P)
            if self.prompt and go == self.prompt[1] and evn in ("UP", "DOWN"):
                # (a prompt marker it spawned: shown at its spawn point with its label, or hidden)
                if evn == "UP":
                    return self.emit("ShowPromptMarker", {"labelName": self.prompt[2], "spawnPoint": self.prompt[0],
                                                          "storeObject": go})
                return self.emit("HidePromptMarker", {"storedObject": go})
            return self.emit("SendEventByName" if n == "SendEventByName" else "SendEvent", P)
        if n == "SetFsmBool":
            go = P.get("gameObject")
            if go == "$CameraParent":
                k = {"RumblingSmall": 1, "RumblingMed": 2, "RumblingBig": 3}.get(P.get("variableName"), 0)
                return self.emit("SetRumble", {"kind": k, "on": P.get("setValue")}) if k else None
            if go == "$DialogueText":
                return None   # (Use Stop)
            return self.emit(n, dict(P, slot=self.fsm_slot(P.get("fsmName"), P.get("variableName"))))
        if n == "iTweenMoveBy":
            if P.get("gameObject") == "$Main Camera Obj" or P.get("gameObject") == "$MainCamera":
                vec = P.get("vector") or [0, 0, 0]
                return self.emit("CameraZoom", {"z": vec[2], "time": P.get("time"), "delay": P.get("delay") or 0})
            if P.get("speed") is not None or (P.get("loopType") or 0) > 2 or P.get("space") or P.get("delay") or \
                    (P.get("easeType") or 0) > 21:
                self.problem("iTweenMoveBy %r" % P)
                return None
            return self.emit(n, P)
        if n == "GetScale":
            return self.emit(n, P)
        if n == "Trigger2dEvent":
            if P.get("collideTag") not in TRIGGER_TAGS:
                self.problem("Trigger2dEvent tag %s" % P.get("collideTag"))
                return None
            return self.emit(n, {"trigger": P.get("trigger"), "sendEvent": P.get("sendEvent"),
                                 "tag": TRIGGER_TAGS[P.get("collideTag")]})
        if n in ("Collision2dEvent", "Collision2dEventLayer"):
            return self.emit("Collision2dEvent", P)
        if n in ("ListenForUp", "ListenForDown", "ListenForLeft", "ListenForRight", "ListenForAttack", "ListenForJump",
                 "ListenForCast"):
            if P.get("eventTarget") not in (None, "Self"):
                self.problem("%s to another" % n)
            return self.emit(n, P)
        if n in OPS:
            return self.emit(n, P)
        self.problem("action %s" % n)
        return None

    def compile(self):
        """-> the definition's bytes (states, actions, constants), its variables' initial values"""
        f = self.f
        states = []
        live = reachable(f, self.rm.external_vars())
        for s in f["states"]:
            self.where = s["name"]
            acts = []
            cut = len(s["actions"])
            if s["name"] in live:
                # (an event sent for sure, and taken: what follows never runs)
                _, sure, at = _events_of_state(f, s, _const_vars(f, self.rm.external_vars()), True)
                cut = at + 1 if sure else cut
            for a in s["actions"][:cut]:
                if not a.get("enabled", True) or s["name"] not in live:
                    continue
                b = self.action(a)
                if b:
                    acts.append(b)
            trans = []
            for ev, to in s["transitions"]:
                if to in self.states:
                    trans.append((EVENTS.id(ev), self.states[to]))
            states.append((trans, acts))
        glob = [(EVENTS.id(ev), self.states[to]) for ev, to in f["global"] if to in self.states]
        assert len(states) < 255 and self.nvars < 255
        # the definition: nstates, nvars, start, nglobal, nconsts, then the constants, the global transitions, each
        # state's offset (u16, from the definition), then the states: ntrans, nactions, transitions, actions
        head = struct.pack("<BBBBH", len(states), self.nvars, self.states.get(f["start"], 0), len(glob), len(self.consts))
        body = bytearray(head)
        body += b"".join(struct.pack("<I", c) for c in self.consts)
        body += b"".join(bytes([e, t]) for e, t in glob)
        if len(body) & 1:
            body.append(0)
        table_at = len(body)
        body += bytes(2 * len(states))
        for i, (trans, acts) in enumerate(states):
            struct.pack_into("<H", body, table_at + 2 * i, len(body))
            body += bytes([len(trans), len(acts)])
            body += b"".join(bytes([e, t]) for e, t in trans)
            body += b"".join(acts)
            if len(body) & 1:
                body.append(0)
        assert len(body) < 65536
        init = [0] * self.nvars
        for name, (slot, t) in self.slots.items():
            v = f["vars"][name][1]
            if t == "gameObject":
                init[slot] = self.obj_value(v) if v else O_NONE
            elif t == "string":
                init[slot] = STR.id(v or "")
            elif t in ("vector3", "vector2", "color", "rect"):
                for k, q in enumerate(v or []):
                    init[slot + k] = _u32(float(q))
            elif t == "float":
                init[slot] = _u32(float(v or 0))
            elif t in ("int", "bool"):
                init[slot] = _u32(int(v or 0))
            else:
                init[slot] = 0
        return bytes(body), init


# ---------------------------------------------------------------- the rooms
DEFS, DEF_INDEX = [], {}   # (definitions: shared)
BUILT = {}                 # room -> Room
LIBS = {}                  # (an animator's library: (file, pid)) -> actor name
ANIMATORS = {}             # (a Unity Animator's controller: (file, pid)) -> (actor, its clip's name)
TITLE_STRINGS = {}


def _animator(o):
    return next((c.get("v") for c in o["c"] if c.get("class") == "tk2dSpriteAnimator" and c.get("v")), None)


def prepare(rooms, sprites, texts):
    """Compiles the rooms' scripts; registers their characters' animation libraries as actors (before actors.build)."""
    import actors, ents, tk2d
    for name, prefixes in ROOMS.items():
        if name not in rooms:
            continue
        d = unity.scene(name)
        rm = Room(name, d, sprites, texts, None)
        for p in prefixes:
            if p in rm.by_path:
                rm.add_tree(rm.by_path[p])
            else:
                rm.problems.append("%s: no %s" % (name, p))
        done = 0
        while done < len(rm.objs):
            o = rm.objs[done]
            done += 1
            for c in o["c"]:
                f = c.get("fsm")
                if not f or f["name"] in SKIP_FSMS:
                    continue
                cp = Compiler(rm, o, f)
                body, init = cp.compile()
                if body not in DEF_INDEX:
                    DEF_INDEX[body] = len(DEFS)
                    DEFS.append(body)
                rm.fsms.append((DEF_INDEX[body], rm.obj_index[o["id"]], init, STR.id(f["name"])))
        for p in rm.problems:
            print("vm:", p)
        nvars = sum(len(i) for _, _, i, _ in rm.fsms)
        assert len(rm.objs) <= MAX_OBJS and len(rm.fsms) <= MAX_FSMS and nvars <= MAX_VARS, \
            (name, len(rm.objs), len(rm.fsms), nvars)
        BUILT[name] = rm
    # Unity Animators (a sprite swapped each frame): their clips as actors'
    for rm in BUILT.values():
        for o in rm.objs:
            ua = _unity_animator(o)
            if not ua:
                continue
            import ents
            key = ents._ref_file(o.get("_doc") or rm.d, ua["m_Controller"])
            if key not in ANIMATORS:
                name, fps, loops, frames = actors.animator_clip(*key)
                m = np.array(o["m3"]).reshape(3, 3)
                k = float(max(np.hypot(m[0, 0], m[1, 0]), np.hypot(m[0, 1], m[1, 1])))
                a = "anim%d" % len(ANIMATORS)
                actors.ANIMATOR_ACTORS[a] = [(name, fps, loops, frames, k)]
                ANIMATORS[key] = (a, name)
    # the animators' libraries: actors with the clips the scripts name (and their first)
    names = set(STR.list)
    known = {(v[0], v[1]): k for k, v in actors.ACTORS.items()}
    for rm in BUILT.values():
        for o in rm.objs:
            an = _animator(o)
            if not an:
                continue
            lib = ents._ref_file(o.get("_doc") or rm.d, an["library"])
            clips = [unity.S(c["name"]) for c in tk2d.animation(*lib)["clips"]]
            want = {c for c in clips if c in names}
            if 0 <= an.get("defaultClipId", 0) < len(clips):
                want.add(clips[an.get("defaultClipId", 0)])
            want.discard("")
            if lib in known:
                a = known[lib]
                have = actors.ACTORS[a][2]
                if have is not None:
                    actors.ACTORS[a] = (lib[0], lib[1], sorted(set(have) | want))
                LIBS[lib] = a
            else:
                a = LIBS.get(lib) or "vm%d" % len(LIBS)
                prev = actors.ACTORS.get(a, (lib[0], lib[1], []))[2]
                actors.ACTORS[a] = (lib[0], lib[1], sorted(set(prev) | want))
                LIBS[lib] = a
                known[lib] = a


def _unity_animator(o):
    """its Unity Animator (with a SpriteRenderer it swaps the sprite of), or None"""
    if not any(c["type"] == "SpriteRenderer" for c in o["c"]):
        return None
    return next((c.get("v") for c in o["c"] if c["type"] == "Animator" and c.get("v") and c["v"].get("m_Controller")), None)


OBJ = "<fffffffffHHHHHBBHHhHBBBBBBH"   # x y z, sx sy, its trigger (center from its place, half size), parent, name, sprite,
#                                  first clip, clip map, its count, flags, condition, sorting layer, order, blend, its
#                                  colliders (first, count), its color (r g b a), (pad)
OF_ACTIVE, OF_RENDERER, OF_ANIMATOR, OF_TRIGGER, OF_COLLIDER, OF_ANIM_OFF, OF_WAVE, OF_FADE = 1, 2, 4, 8, 16, 32, 64, 128


def room_blob(name, clip_index, sprites, owners=()):
    """A room's VM data (src/vm.c): counts, its objects, their clip maps, its FSMs (definition, owner, name, variables).
    owners: each collider's (scene, object id): the colliders an object has, the scripts turn on and off with it."""
    import actors, ents, tk2d
    rm = BUILT.get(name)
    if rm is None:
        return b""
    pdf = ents.pd_flags()
    objs, clipmap = bytearray(), bytearray()
    nmap = 0
    for o in rm.objs:
        p = rm.by_id.get(o.get("parent"))
        parent = rm.obj_index.get(p["id"], 0xFFFF) if p is not None else 0xFFFF
        m = np.array(o["m3"]).reshape(3, 3)
        sx = float(np.hypot(m[0, 0], m[1, 0])) * (1 if m[0, 0] >= 0 else -1)
        sy = float(np.hypot(m[0, 1], m[1, 1])) * (1 if m[1, 1] >= 0 else -1)
        fl = (OF_ACTIVE if o.get("self_active", o["active"]) else 0)
        box = None
        for c in o["c"]:
            if c["type"] in ("BoxCollider2D", "CircleCollider2D", "PolygonCollider2D") and c.get("v"):
                b = ents._box(o, c)
                if b and c["v"].get("m_IsTrigger") and box is None:
                    box = b
                if b and c["v"].get("m_Enabled", 1):
                    fl |= OF_COLLIDER
        if box:
            fl |= OF_TRIGGER
        if any(c.get("class") == "DeactivateAfter2dtkAnimation" for c in o["c"]):
            fl |= OF_ANIM_OFF   # (off once its clip is over)
        bx, by = ((box[0] + box[2]) / 2 - o["pos"][0], (box[1] + box[3]) / 2 - o["pos"][1]) if box else (0, 0)
        wv = next((c.get("v") for c in o["c"] if c.get("class") == "WaveEffectControl" and c.get("v")), None)
        if wv:
            # (WaveEffectControl: it grows and fades as it is on; its speed, its scale)
            fl |= OF_WAVE
            bx, by = wv.get("accelStart", 5), wv.get("scaleMultiplier", 1) * abs(sx) / abs(o["lscale"][0] or 1)
        sf = next((c.get("v") for c in o["c"] if c.get("class") == "SimpleSpriteFade" and c.get("v")), None)
        if sf:
            # (SimpleSpriteFade: from its color to the fade's as it is on, in its time; then off, or back in its pool)
            assert sf.get("fadeInOnStart") and (sf.get("deactivateOnFadeIn") or sf.get("recycleOnFadeIn")), o["path"]
            fl |= OF_FADE
            bx, by = sf.get("fadeDuration", 1), (sf.get("fadeInColor") or {}).get("a", 0)
        bhx, bhy = ((box[2] - box[0]) / 2, (box[3] - box[1]) / 2) if box else (0, 0)
        sprite, first, start, count = 0xFFFF, 0xFFFF, nmap, 0
        an = _animator(o)
        mr = next((c.get("v") for c in o["c"] if c["type"] in ("MeshRenderer", "SpriteRenderer") and isinstance(c.get("v"), dict)), None)
        if mr is not None and mr.get("m_Enabled", 1):
            fl |= OF_RENDERER
        ua = _unity_animator(o)
        if ua:
            # (a Unity Animator: its clip, its first)
            fl |= OF_ANIMATOR
            a, cn = ANIMATORS[ents._ref_file(o.get("_doc") or rm.d, ua["m_Controller"])]
            first = clip_index[actors.clip_id(a, cn)]
        elif an:
            fl |= OF_ANIMATOR
            lib = ents._ref_file(o.get("_doc") or rm.d, an["library"])
            a = LIBS[lib]
            clips = [unity.S(c["name"]) for c in tk2d.animation(*lib)["clips"]]
            dc = an.get("defaultClipId", 0)
            if 0 <= dc < len(clips):
                first = clip_index.get(actors.clip_id(a, clips[dc]), 0xFFFF)
            for cn in clips:
                ci = clip_index.get(actors.clip_id(a, cn))
                if ci is not None and cn in STR.index:
                    clipmap += struct.pack("<HH", STR.index[cn], ci)
                    nmap += 1
                    count += 1
        else:
            sr = next((c.get("v") for c in o["c"] if c["type"] == "SpriteRenderer" and c.get("v")), None)
            if sr and sr.get("m_Sprite"):
                # (its own size, drawn at its scale: its texture as fine as it is drawn)
                sprite = sprites.id(o.get("_doc") or rm.d, sr["m_Sprite"], 1.0, max(abs(sx), abs(sy)))
        # (DeactivateIfPlayerdataTrue, False: two at most)
        conds = [pdf[bn] | (0x8000 if off_if else 0) for bn, off_if in state.conditions(o) if bn in pdf]
        conds = sorted(set(conds), key=conds.index)
        if o.get("_variant"):
            # (a scene loaded with the room's by a PlayerData bool: there only when it has that value)
            bn, val = o["_variant"]
            assert not conds, o["path"]
            conds = [pdf[bn] | (0 if val else 0x8000)]
        assert len(conds) <= 2, o["path"]
        cond, cond2 = (conds + [0xFFFF, 0xFFFF])[:2]
        cols = [k for k, ow in enumerate(owners) if k and ow == (o.get("_scene", name), o.get("_raw", o["id"]))]
        assert cols == list(range(cols[0], cols[-1] + 1)) if cols else True, o["path"]
        assert len(cols) < 256 and (not cols or cols[-1] < 256), o["path"]
        layer, order = ents._sorting(o)
        # (its material's blend: pack.BLENDS)
        import scene
        blend = 0
        if mr is not None and mr.get("m_Materials"):
            m0 = mr["m_Materials"][0]
            if m0 and m0[1]:
                doc = o.get("_doc") or rm.d
                blend = {"alpha": 0, "add": 1, "screen": 2, "linearlight": 3, "overlay": 4, "multiply": 5}[
                    scene.blend_of(unity.material(unity.ref_path(doc, m0[0]), m0[1])[1])]
        col = (mr or {}).get("m_Color") or {"r": 1, "g": 1, "b": 1, "a": 1}
        rgba = [max(0, min(255, int(round(col[k] * 255)))) for k in "rgba"]
        objs += struct.pack(OBJ, o["pos"][0], o["pos"][1], o["pos"][2], sx, sy, bx, by, bhx, bhy, parent,
                            STR.id(o["name"]), sprite, first, start, min(count, 255), fl, cond, layer, order, blend,
                            cols[0] if cols else 0, len(cols), *rgba, cond2)
    fsms = bytearray()
    for di, owner, init, fname in rm.fsms:
        fsms += struct.pack("<HHHH", di, owner, fname, len(init)) + b"".join(struct.pack("<I", v) for v in init)
    head = struct.pack("<HHHH", len(rm.objs), len(rm.fsms), nmap, 0)
    return bytes(head + objs + clipmap + fsms)


def tables(clip_index, prompt_text):
    """VMDEF: the definitions' offsets and the definitions; per string: its title (255 none), its prompt's text
    (0xFFFF none), the Knight's clip of that name (0xFFFF none)."""
    import actors
    defs = bytearray()
    offs = []
    for b in DEFS:
        while len(defs) & 3:
            defs.append(0)
        offs.append(len(defs))
        defs += b
    titles = bytes(text.TITLES.index(s) if s in text.TITLES else 255 for s in STR.list)
    pdb = b"".join(struct.pack("<H", _pd_code(s)) for s in STR.list)
    prompts = b"".join(struct.pack("<H", prompt_text.get(s, 0xFFFF)) for s in STR.list)
    knight = b"".join(struct.pack("<H", clip_index.get(actors.clip_id("knight", s), 0xFFFF)) for s in STR.list)
    head = struct.pack("<II", len(DEFS), len(STR.list))
    base = len(head) + 4 * len(DEFS)
    body = head + b"".join(struct.pack("<I", base + o) for o in offs) + bytes(defs)
    while len(body) & 3:
        body += b"\0"
    return body + titles + (b"\0" if len(titles) & 1 else b"") + prompts + knight + pdb


def _pd_code(name):
    """a string as a PlayerData bool's name -> its flag (0xFFFE always false, 0xFFFD always true, 0xFFF0... kept elsewhere,
    0xFFFF not one)"""
    import ents
    pdf = ents.pd_flags()
    if name in pdf:
        return pdf[name]
    if name in PD_SPECIAL:
        return PD_SPECIAL[name]
    if name in PD_CONST:
        return 0xFFFD if PD_CONST[name] else 0xFFFE
    if name in state.CONSTANT:
        return 0xFFFD if state.CONSTANT[name] else 0xFFFE
    return 0xFFFF


# ---------------------------------------------------------------- the states that can be reached (in this part of the
# game): PlayerData bools and ints that never change here decide some tests for good; a state's transition on an event
# its own actions send only counts if one of them can send it
PD_INT_CONST = {"permadeathMode": 0, "quakeLevel": 0, "screamLevel": 0}


def _const_bool(name):
    if name in PD_CONST:
        return PD_CONST[name]
    if name in state.CONSTANT:
        return state.CONSTANT[name]
    return None


def _const_vars(f, external=()):
    """its bool variables nothing changes (none of its actions write them, no other FSM sets them): their values"""
    written = set(external)
    for st in f["states"]:
        for a in st["actions"]:
            for k, v in a["params"]:
                if isinstance(v, str) and v.startswith("$") and (k.startswith("store") or k.endswith("Bool") and k != "boolName" or
                                                                 (a["name"] in ("SetBoolValue", "BoolFlip") and k == "boolVariable")):
                    written.add(v[1:])
    return {n: bool(val) for n, (t, val) in f["vars"].items() if t == "bool" and n not in written}


def _events_of_state(f, s, consts=None, where=False):
    """the events this state's actions can send it (None: any of the transitions': unknown)"""
    known = dict(consts or {})   # (variables whose value is known here)
    takes = {e for e, _ in s["transitions"]} | {e for e, _ in f["global"]}
    out = set()

    def ev(v):
        return v[1] if isinstance(v, (list, tuple)) and v and v[0] == "event" else None

    def var(v):
        return v[1:] if isinstance(v, str) and v.startswith("$") else None

    def bval(v):
        if isinstance(v, bool):
            return v
        n = var(v)
        return known.get(n) if n else None

    for ai, a in enumerate(s["actions"]):
        if not a.get("enabled", True):
            continue
        P = dict(a["params"])
        n = a["name"]
        if n == "GetPlayerDataBool":
            c = _const_bool(P.get("boolName"))
            if var(P.get("storeValue")):
                known[var(P.get("storeValue"))] = c
            continue
        if n == "GetPlayerDataInt":
            if var(P.get("storeValue")):
                known[var(P.get("storeValue"))] = PD_INT_CONST.get(P.get("intName"))
            continue
        if n == "PlayerDataBoolTest":
            c = _const_bool(P.get("boolName"))
            for e, want in ((ev(P.get("isTrue")), True), (ev(P.get("isFalse")), False)):
                if e and (c is None or c == want):
                    out.add(e)
                    if c is not None and not P.get("everyFrame") and e in takes:
                        # (sent for sure, and taken: the state's actions stop there)
                        return (out, True, ai) if where else (out, True)
            continue
        if n == "PlayerDataBoolTrueAndFalse":
            t, fl = _const_bool(P.get("trueBool")), _const_bool(P.get("falseBool"))
            can_true = (t is None or t) and (fl is None or not fl)
            if ev(P.get("isTrue")) and can_true:
                out.add(ev(P.get("isTrue")))
            if ev(P.get("isFalse")):
                out.add(ev(P.get("isFalse")))
            continue
        if n == "BoolTest":
            b = bval(P.get("boolVariable"))
            for e, want in ((ev(P.get("isTrue")), True), (ev(P.get("isFalse")), False)):
                if e and (b is None or b == want):
                    out.add(e)
                    if b is not None and not P.get("everyFrame") and e in takes:
                        return (out, True, ai) if where else (out, True)
            continue
        if n == "BoolTestMulti":
            vals = [bval(v) for v in P.get("boolVariables") or []]
            states = P.get("boolStates") or []
            sure_false = any(v is not None and v != w for v, w in zip(vals, states))
            if ev(P.get("trueEvent")) and not sure_false:
                out.add(ev(P.get("trueEvent")))
            if ev(P.get("falseEvent")):
                out.add(ev(P.get("falseEvent")))
            continue
        if n in ("BoolAllTrue",):
            vals = [bval(v) for v in P.get("boolVariables") or []]
            if ev(P.get("sendEvent")) and not any(v is False for v in vals):
                out.add(ev(P.get("sendEvent")))
            continue
        if n == "IntCompare":
            a1 = known.get(var(P.get("integer1"))) if var(P.get("integer1")) else P.get("integer1")
            a2 = known.get(var(P.get("integer2"))) if var(P.get("integer2")) else P.get("integer2")
            for e, test in ((ev(P.get("equal")), lambda x, y: x == y), (ev(P.get("lessThan")), lambda x, y: x < y),
                            (ev(P.get("greaterThan")), lambda x, y: x > y)):
                if e and (a1 is None or a2 is None or test(a1, a2)):
                    out.add(e)
            continue
        if n in ("IntCompareToBool", "IntTestToBool"):
            k1, k2 = ("integer1", "integer2") if n == "IntCompareToBool" else ("int1", "int2")
            a1 = known.get(var(P.get(k1))) if var(P.get(k1)) else P.get(k1)
            a2 = known.get(var(P.get(k2))) if var(P.get(k2)) else P.get(k2)
            for k, test in (("equalBool", lambda x, y: x == y), ("lessThanBool", lambda x, y: x < y),
                            ("greaterThanBool", lambda x, y: x > y)):
                if var(P.get(k)):
                    known[var(P.get(k))] = test(a1, a2) if a1 is not None and a2 is not None else None
            continue
        if n == "SetBoolValue":
            if var(P.get("boolVariable")):
                known[var(P.get("boolVariable"))] = bval(P.get("boolValue"))
            continue
        # anything else: whatever events it names
        for k, v in a["params"]:
            if isinstance(v, (list, tuple)) and v and v[0] == "event":
                out.add(v[1])
            elif isinstance(v, list):
                for q in v:
                    if isinstance(q, (list, tuple)) and q and q[0] == "event":
                        out.add(q[1])
            if k == "sendEvent" and isinstance(v, str):
                out.add(v)
    return (out, False, len(s["actions"])) if where else (out, False)


def reachable(f, external=()):
    """the names of the FSM's states that can be entered (external: the variables other FSMs set)"""
    consts = _const_vars(f, external)
    local = set()
    for s in f["states"]:
        for a in s["actions"]:
            for k, v in a["params"]:
                if isinstance(v, (list, tuple)) and v and v[0] == "event":
                    local.add(v[1])
                elif isinstance(v, list):
                    for q in v:
                        if isinstance(q, (list, tuple)) and q and q[0] == "event":
                            local.add(q[1])
    local.discard("FINISHED")
    by = {s["name"]: s for s in f["states"]}
    todo = [f["start"]] + [to for _, to in f["global"]]
    seen = set()
    while todo:
        n = todo.pop()
        if n in seen or n not in by:
            continue
        seen.add(n)
        s = by[n]
        can, sure = _events_of_state(f, s, consts)
        for e, to in s["transitions"]:
            if e in can or (e not in local and not (sure and e == "FINISHED")):
                todo.append(to)
    return seen
