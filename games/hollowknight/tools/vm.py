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
    "Town": ["_NPCs/Elderbug"],
    "Crossroads_ShamanTemple": ["_Props/Shaman Meeting", "_Props/Shaman Trapped", "_Props/Shaman Killed Blocker",
                                "_Props/Knight Get Fireball", "Battle Scene/Reminder Cast"],
}
# FSMs left out (what this port does not have: the dream nail, sounds...)
SKIP_FSMS = {"npc_dream_dialogue", "Dream Dialogue", "Rotate", "fade and destroy", "Shop Open Voice"}
# objects left out (effects drawn by the C code, or nothing at all)
SKIP_CLASSES = {"SpellGetOrb", "ParticleSystem"}

# the game's own objects
SPECIAL = {"Hero": 0xFF00, "HeroLight": 0xFF01, "DialogueManager": 0xFF02, "DialogueText": 0xFF03, "AreaTitle": 0xFF04,
           "CameraParent": 0xFF05, "MainCamera": 0xFF06, "GameManager": 0xFF07, "HUD Blanker": 0xFF08,
           "DialogueTextYN": 0xFF09, "UIManager": 0xFF0A, "Enemy Dream Msg": 0xFF0B}
O_NONE = 0xFFFF
# PlayerData ints (src/vm.c: pd_int)
PD_INTS = ["MPCharge", "health", "maxHealth", "geo", "fireballLevel", "quakeLevel", "screamLevel", "shaman", "elderbug",
           "permadeathMode", "nailDamage"]
# PlayerData bools kept elsewhere (src/vm.c)
PD_SPECIAL = {"disablePause": 0xFFF0, "hasSpell": 0xFFF1}
# PlayerData bools that keep their new game value all through this part of the game
PD_CONST = {"equippedCharm_10": False, "elderbugGaveFlower": False, "elderbugRequestedFlower": False,
            "xunFlowerBroken": False, "hasXunFlower": False, "openedBlackEggDoor": False,
            "visitedCrossroadsInfected": False, "defeatedNightmareGrimm": False, "jijiDoorUnlocked": False,
            "visitedCliffs": False, "mineLiftOpened": False, "brettaRescued": False, "elderbugHistory2": False,
            "shamanFireball2Convo": False, "shamanScreamConvo": False, "shamanScream2Convo": False,
            "shamanQuakeConvo": False, "shamanQuake2Convo": False, "hasDreamNail": False, "equippedCharm_19": False,
            "equippedCharm_11": False, "dungDefenderEncounterReady": False, "elderbugSpeechBretta": False,
            "elderbugSpeechJiji": False, "elderbugSpeechKingsPass": False, "elderbugSpeechMinesLift": False,
            "elderbugSpeechInfectedCrossroads": False, "elderbugSpeechFinalBossDoor": False, "hasDoubleJump": False,
            "hasSuperDash": False, "hasWalljump": False, "mageLordDefeated": False, "elderbugConvoGrimm": False,
            "elderbugNymmConvo": False, "elderbugTroupeLeftConvo": False, "elderbugBrettaLeft": False}


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
          "NPC TITLE DOWN", "NPC CONVO START", "BOX UP", "BOX DOWN", "LEAVING SCENE", "TAKE DAMAGE", "GET ITEM MSG END"):
    EVENTS.id(e)

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
op("Trigger2dEvent", ("trigger", "n"), ("sendEvent", "e"))
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
op("SetFsmBool", ("gameObject", "o"), ("fsmName", "s"), ("variableName", "s"), ("setValue", "b"))
op("SetFsmString", ("gameObject", "o"), ("fsmName", "s"), ("variableName", "s"), ("setValue", "s"))
op("StartConversation", ("text", "x"))
op("HeroCall", ("method", "n"), ("store", "B"), ("a", "f"))
op("Shake", ("kind", "n"))
op("SetRumble", ("kind", "n"), ("on", "b"))
op("EaseFloat", ("fromValue", "f"), ("toValue", "f"), ("floatVariable", "F"), ("time", "f"), ("finishEvent", "e"))
op("CameraZoom", ("z", "f"), ("time", "f"), ("delay", "f"))
op("Collision2dEvent", ("sendEvent", "e"))
op("HudBlanker", ("alpha", "f"), ("time", "f"), ("on", "n"))
op("CreateObject", ("what", "n"), ("x", "f"), ("y", "f"), ("sx", "f"), ("storeObject", "O"))
op("GetItemMsg", ("what", "n"))
op("SetParent", ("gameObject", "o"))
op("IntOp", ("intVariable", "I"), ("value", "i"))

# HeroController's methods the scripts call (HeroCall's method)
HERO_METHODS = ["RelinquishControl", "RegainControl", "StopAnimationControl", "StartAnimationControl", "FaceLeft",
                "FaceRight", "CanTalk", "PreventCastByDialogueEnd", "SetBackOnGround", "AddMPCharge",
                "FindGroundPoint", "SetBenchRespawn", "SetHazardRespawn", "RelinquishControlNotVelocity",
                "SetCState", "SaveGame", "AffectedByGravity", "ResetHardLandingTimer", "StopPlayingAudio"]
# what CreateObject makes (src/vm.c)
CREATED = {}
# camera shake events
SHAKES = {"EnemyKillShake": 1, "AverageShake": 2, "BigShake": 3, "SmallShake": 4}


class Room:
    """A room's VM data being built."""
    def __init__(self, name, d, sprites, texts, actor_of):
        self.name, self.d = name, d
        self.sprites, self.texts, self.actor_of = sprites, texts, actor_of
        self.by_id = {o["id"]: o for o in d["objects"]}
        self.by_path = {o["path"]: o for o in d["objects"]}
        self.objs, self.obj_index = [], {}
        self.fsms = []
        self.problems = []

    # ------------------------------------------------------------ objects
    def want(self, o):
        cls = {c.get("class") or c["type"] for c in o["c"]}
        return not (cls & SKIP_CLASSES)

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

    def ref_obj(self, r):
        """["ref", file, pid] -> a VM object (this scene's objects only)"""
        if not r or r[1] != 0:
            return O_NONE
        o = self.by_id.get(r[2])
        if o is None:
            return O_NONE
        if o["id"] not in self.obj_index:
            self.add_tree(o)
        return self.obj_index.get(o["id"], O_NONE)


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
            return self.rm.ref_obj(v)
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
        if n in ("AudioPlayerOneShot", "AudioPlayerOneShotSingle", "AudioStop", "AudioPlay", "AudioPlaySimple",
                 "PlayParticleEmitter", "StopParticleEmitter", "SetParticleEmission", "SetParticleEmissionRate",
                 "ForceHeroFootstepSound", "SendEventToRegister", "AddTrackTrigger", "PlayVibration",
                 "VibrationPlayerStop", "TransitionToAudioSnapshot", "SetAudioPitch", "SetAudioVolume",
                 "AudioPlayInState", "FadeAudio", "PlayRandomSound", "SetRotation", "RandomFloat",
                 "SpawnObjectFromGlobalPool", "GetLastEvent", "SetBoxCollider2DSize", "Tk2dSpriteSetColor"):
            return None
        if n in ("PlayerDataBoolTest",):
            return self.emit(n, P)
        if n == "CallMethodProper":
            beh, m = P.get("behaviour"), P.get("methodName")
            args = P.get("parameters") or []
            if beh == "DialogueBox" and m == "StartConversation":
                key, sheet = args[0]["s"], args[1]["s"]
                return self.emit("StartConversation", {"text": self.rm.texts.id(sheet, key)})
            if beh == "HeroController" and m in HERO_METHODS:
                a0 = args[0]["f"] if args and args[0].get("type") in (5, 0) else 0
                return self.emit("HeroCall", {"method": HERO_METHODS.index(m), "store": P.get("storeResult"), "a": a0})
            self.problem("CallMethodProper %s.%s" % (beh, m))
            return None
        if n == "SendMessage":
            fn = (P.get("functionCall") or {}).get("fn")
            if fn in HERO_METHODS:
                return self.emit("HeroCall", {"method": HERO_METHODS.index(fn), "store": None, "a": 0})
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
            if go in ("$DialogueManager",) and evn in ("BOX UP", "BOX DOWN"):
                return self.emit("SendEventByName", P)
            return self.emit("SendEventByName" if n == "SendEventByName" else "SendEvent", P)
        if n == "SetFsmBool":
            go = P.get("gameObject")
            if go == "$CameraParent":
                k = {"RumblingSmall": 1, "RumblingMed": 2, "RumblingBig": 3}.get(P.get("variableName"), 0)
                return self.emit("SetRumble", {"kind": k, "on": P.get("setValue")}) if k else None
            if go == "$DialogueText":
                return None   # (Use Stop)
            return self.emit(n, P)
        if n == "iTweenMoveBy":
            if P.get("gameObject") == "$Main Camera Obj" or P.get("gameObject") == "$MainCamera":
                vec = P.get("vector") or [0, 0, 0]
                return self.emit("CameraZoom", {"z": vec[2], "time": P.get("time"), "delay": P.get("delay") or 0})
            self.problem("iTweenMoveBy")
            return None
        if n == "GetScale":
            return self.emit(n, P)
        if n == "Trigger2dEvent":
            return self.emit(n, {"trigger": P.get("trigger"), "sendEvent": P.get("sendEvent")})
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
        live = reachable(f)
        for s in f["states"]:
            self.where = s["name"]
            acts = []
            for a in s["actions"]:
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
        BUILT[name] = rm
    # the animators' libraries: actors with the clips the scripts name (and their first)
    names = set(STR.list)
    known = {(v[0], v[1]): k for k, v in actors.ACTORS.items()}
    for rm in BUILT.values():
        for o in rm.objs:
            an = _animator(o)
            if not an:
                continue
            lib = ents._ref_file(rm.d, an["library"])
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


OBJ = "<fffffffffHHHHHBBHHhH"   # x y z, sx sy, its trigger (center from its place, half size), parent, name, sprite,
#                               first clip, clip map, its count, flags, condition, sorting layer, order
OF_ACTIVE, OF_RENDERER, OF_ANIMATOR, OF_TRIGGER, OF_COLLIDER = 1, 2, 4, 8, 16


def room_blob(name, clip_index, sprites):
    """A room's VM data (src/vm.c): counts, its objects, their clip maps, its FSMs (definition, owner, name, variables)."""
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
        bx, by = ((box[0] + box[2]) / 2 - o["pos"][0], (box[1] + box[3]) / 2 - o["pos"][1]) if box else (0, 0)
        bhx, bhy = ((box[2] - box[0]) / 2, (box[3] - box[1]) / 2) if box else (0, 0)
        sprite, first, start, count = 0xFFFF, 0xFFFF, nmap, 0
        an = _animator(o)
        mr = next((c.get("v") for c in o["c"] if c["type"] in ("MeshRenderer", "SpriteRenderer") and isinstance(c.get("v"), dict)), None)
        if mr is not None and mr.get("m_Enabled", 1):
            fl |= OF_RENDERER
        if an:
            fl |= OF_ANIMATOR
            lib = ents._ref_file(rm.d, an["library"])
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
                sprite = sprites.id(rm.d, sr["m_Sprite"], max(abs(sx), abs(sy)))
        cond = 0xFFFF
        for bn, off_if in state.conditions(o):
            if bn in pdf:
                cond = pdf[bn] | (0x8000 if off_if else 0)
        layer, order = ents._sorting(o)
        objs += struct.pack(OBJ, o["pos"][0], o["pos"][1], o["pos"][2], sx, sy, bx, by, bhx, bhy, parent,
                            STR.id(o["name"]), sprite, first, start, min(count, 255), fl, cond, layer, order, 0)
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


def _events_of_state(f, s):
    """the events this state's actions can send it (None: any of the transitions': unknown)"""
    known = {}   # (variables whose value is known here)
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

    for a in s["actions"]:
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
    return out


def reachable(f):
    """the names of the FSM's states that can be entered"""
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
        can = _events_of_state(f, s)
        for e, to in s["transitions"]:
            if e in can or e not in local:
                todo.append(to)
    return seen
