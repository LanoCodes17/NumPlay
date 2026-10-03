"""PlayMaker FSM decoder: turns a parsed PlayMakerFSM MonoBehaviour (tt.parse) into readable states, actions with
their parameters, transitions and variables. Reference tool, follows ActionData.cs (PlayMaker.dll)."""
import struct

PT = ["Integer", "Boolean", "Float", "String", "Color", "ObjectReference", "LayerMask", "Enum", "Vector2", "Vector3",
      "Vector4", "Rect", "Array", "Character", "AnimationCurve", "FsmFloat", "FsmInt", "FsmBool", "FsmString",
      "FsmGameObject", "FsmOwnerDefault", "FunctionCall", "FsmAnimationCurve", "FsmEvent", "FsmObject", "FsmColor",
      "Unsupported", "GameObject", "FsmVector3", "LayoutOption", "FsmRect", "FsmEventTarget", "FsmMaterial",
      "FsmTexture", "Quaternion", "FsmQuaternion", "FsmProperty", "FsmVector2", "FsmTemplateControl", "FsmVar",
      "CustomClass", "FsmArray", "FsmEnum"]


def S(x):
    if isinstance(x, (bytes, bytearray)):
        return x.decode("utf8", "replace")
    return x


def _f(b, p): return struct.unpack_from("<f", b, p)[0]
def _i(b, p): return struct.unpack_from("<i", b, p)[0]


def rnd(v):
    if isinstance(v, float):
        r = round(v, 4)
        return int(r) if r == int(r) and abs(r) < 1e9 else r
    if isinstance(v, (list, tuple)):
        return type(v)(rnd(x) for x in v)
    return v


def named(d, val):
    """FsmFloat/FsmInt/...: a variable reference ('$name') or a value."""
    if d is None:
        return None
    n = S(d.get("name", b""))
    if n:
        return "$" + n
    if d.get("useVariable") and not n:
        return None  # 'None' variable: unset
    return rnd(val(d))


def vec(d, keys):
    return tuple(d[k] for k in keys)


def ref(r):
    if not r:
        return None
    if isinstance(r, dict) and "m_PathID" in r:
        return ("ref", r["m_FileID"], r["m_PathID"]) if r["m_PathID"] else None
    return r


def decode_params(ad, data_version, names_of=None):
    """Returns per-action list of (paramName, value)."""
    b = bytes(ad["byteData"])
    types = ad["paramDataType"]
    pnames = [S(x) for x in ad["paramName"]]
    pos = ad["paramDataPos"]
    size = ad["paramByteDataSize"]
    out = []

    def one(i):
        t = PT[types[i]]
        p = pos[i]
        n = size[i]
        if t == "Integer" or t == "LayerMask" or t == "Enum":
            return _i(b, p)
        if t == "Float":
            return rnd(_f(b, p))
        if t == "Boolean":
            return bool(b[p])
        if t == "Character":
            return chr(b[p])
        if t == "String":
            if data_version > 1 and ad["stringParams"]:
                return S(ad["stringParams"][p])
            return b[p:p + n].decode("utf8", "replace")
        if t == "Color":
            return rnd(struct.unpack_from("<4f", b, p))
        if t == "Vector2":
            return rnd(struct.unpack_from("<2f", b, p))
        if t in ("Vector3",):
            return rnd(struct.unpack_from("<3f", b, p))
        if t in ("Vector4", "Rect", "Quaternion"):
            return rnd(struct.unpack_from("<4f", b, p))
        if t == "FsmEvent":
            if data_version > 1 and ad["stringParams"]:
                s = S(ad["stringParams"][p])
            else:
                s = b[p:p + n].decode("utf8", "replace")
            return ("event", s) if s else None
        if t == "FsmFloat":
            if data_version > 1:
                return named(ad["fsmFloatParams"][p], lambda d: d["value"])
            nm = b[p + 5:p + n].decode()
            return "$" + nm if nm else (None if b[p + 4] else rnd(_f(b, p)))
        if t == "FsmInt":
            if data_version > 1:
                return named(ad["fsmIntParams"][p], lambda d: d["value"])
            nm = b[p + 5:p + n].decode()
            return "$" + nm if nm else (None if b[p + 4] else _i(b, p))
        if t == "FsmBool":
            if data_version > 1:
                return named(ad["fsmBoolParams"][p], lambda d: bool(d["value"]))
            nm = b[p + 2:p + n].decode()
            return "$" + nm if nm else (None if b[p + 1] else bool(b[p]))
        if t == "FsmVector2":
            if data_version > 1:
                return named(ad["fsmVector2Params"][p], lambda d: vec(d["value"], "xy"))
            nm = b[p + 9:p + n].decode()
            return "$" + nm if nm else (None if b[p + 8] else rnd(struct.unpack_from("<2f", b, p)))
        if t == "FsmVector3":
            if data_version > 1:
                return named(ad["fsmVector3Params"][p], lambda d: vec(d["value"], "xyz"))
            nm = b[p + 13:p + n].decode()
            return "$" + nm if nm else (None if b[p + 12] else rnd(struct.unpack_from("<3f", b, p)))
        if t == "FsmColor":
            if data_version > 1:
                return named(ad["fsmColorParams"][p], lambda d: vec(d["value"], "rgba"))
            nm = b[p + 17:p + n].decode()
            return "$" + nm if nm else (None if b[p + 16] else rnd(struct.unpack_from("<4f", b, p)))
        if t == "FsmRect":
            if data_version > 1:
                return named(ad["fsmRectParams"][p], lambda d: vec(d["value"], ("x", "y", "width", "height")))
            return rnd(struct.unpack_from("<4f", b, p))
        if t == "FsmQuaternion":
            if data_version > 1:
                return named(ad["fsmQuaternionParams"][p], lambda d: vec(d["value"], "xyzw"))
            return rnd(struct.unpack_from("<4f", b, p))
        if t == "FsmString":
            return named(ad["fsmStringParams"][p], lambda d: S(d["value"]))
        if t == "FsmGameObject":
            d = ad["fsmGameObjectParams"][p]
            return named(d, lambda d: ref(d["value"]))
        if t == "FsmOwnerDefault":
            d = ad["fsmOwnerDefaultParams"][p]
            if d["ownerOption"] == 0:
                return "Owner"
            go = d["gameObject"]
            return named(go, lambda d: ref(d["value"]))
        if t in ("FsmObject", "FsmMaterial", "FsmTexture"):
            d = ad["fsmObjectParams"][p]
            return named(d, lambda d: ref(d["value"]))
        if t == "ObjectReference":
            return ref(ad["unityObjectParams"][p])
        if t == "FsmVar":
            d = ad["fsmVarParams"][p]
            vn = S(d.get("variableName", b""))
            if vn:
                return "$" + vn
            return {"type": d.get("type"), "f": d.get("floatValue"), "i": d.get("intValue"),
                    "b": d.get("boolValue"), "s": S(d.get("stringValue", b""))}
        if t == "FsmArray":
            d = ad["fsmArrayParams"][p]
            n2 = S(d.get("name", b""))
            if n2:
                return "$" + n2
            return {k: (rnd(v) if not isinstance(v, (bytes, bytearray)) else S(v)) for k, v in d.items()
                    if k in ("type", "floatValues", "intValues", "boolValues", "stringValues")
                    and v}
        if t == "FsmEnum":
            d = ad["fsmEnumParams"][p]
            return named(d, lambda d: d["intValue"])
        if t == "FsmEventTarget":
            d = ad["fsmEventTargetParams"][p]
            tgt = {0: "Self", 1: "GameObject", 2: "GameObjectFSM", 3: "FSMComponent", 4: "BroadcastAll",
                   5: "HostFSM", 6: "SubFSMs"}.get(d["target"], d["target"])
            if tgt == "Self":
                return "Self"
            go = d["gameObject"]
            fsmname = d["fsmName"]
            return {"target": tgt, "go": "Owner" if go["ownerOption"] == 0 else named(go["gameObject"], lambda d: ref(d["value"])),
                    "fsm": named(fsmname, lambda d: S(d["value"]))}
        if t == "FsmProperty":
            d = ad["fsmPropertyParams"][p]
            return {"prop": S(d.get("PropertyName", b"")), "target": named(d.get("TargetObject"), lambda d: ref(d["value"]))}
        if t == "FunctionCall":
            d = ad["functionCallParams"][p]
            t = S(d.get("parameterType", d.get("ParameterType", b"")))
            out = {"fn": S(d.get("FunctionName", b"")), "type": t}
            for key, name in (("int", "IntParameter"), ("float", "FloatParameter"), ("bool", "BoolParameter"),
                              ("string", "StringParameter")):
                if t == key and name in d:
                    out["value"] = named(d[name], lambda q: q["value"] if key != "string" else S(q["value"]))
            return out
        if t in ("AnimationCurve", "FsmAnimationCurve"):
            d = ad["animationCurveParams"][p]
            c = d.get("curve", d)
            keys = c.get("m_Curve", [])
            return ("curve", [rnd((k["time"], k["value"])) for k in keys])
        if t == "FsmTemplateControl":
            return "template"
        if t == "LayoutOption":
            return "layout"
        if t == "GameObject":
            return ref(ad["unityObjectParams"][p]) if ad["unityObjectParams"] else None
        return "?" + t

    # arrays and custom classes: their elements follow as separate params
    i = 0
    flat = []
    while i < len(types):
        t = PT[types[i]]
        if t == "Array":
            p = pos[i]
            cnt = ad["arrayParamSizes"][p]
            el = []
            for k in range(cnt):
                el.append(one(i + 1 + k))
            flat.append((pnames[i], el))
            i += 1 + cnt
            continue
        if t == "CustomClass":
            p = pos[i]
            cnt = ad["customTypeSizes"][p]
            fields = {}
            for k in range(cnt):
                fields[pnames[i + 1 + k]] = one(i + 1 + k)
            flat.append((pnames[i], fields))
            i += 1 + cnt
            continue
        flat.append((pnames[i], one(i)))
        i += 1
    return flat


def split_actions(ad, flat_count_fn=None):
    return ad["actionStartIndex"]


def decode(v):
    """v: tt.parse() of a PlayMakerFSM. Returns a dict."""
    f = v["fsm"]
    dv = f.get("dataVersion", 2)
    states = []
    for st in f["states"]:
        ad = st["actionData"]
        names = [S(x) for x in ad["actionNames"]]
        starts = list(ad["actionStartIndex"])
        enabled = bytes(ad["actionEnabled"])
        # decode params per action: params between start[i] and start[i+1] (raw param indices)
        types = ad["paramDataType"]
        acts = []
        for ai, nm in enumerate(names):
            a = starts[ai]
            e = starts[ai + 1] if ai + 1 < len(starts) else len(types)
            sub = {k: ad[k] for k in ad}
            for key in ("paramDataType", "paramName", "paramDataPos", "paramByteDataSize"):
                sub[key] = ad[key][a:e]
            params = decode_params(sub, dv)
            acts.append({"name": nm.split(".")[-1], "enabled": bool(enabled[ai]) if ai < len(enabled) else True,
                         "params": params})
        trans = [(S(t["fsmEvent"]["name"]), S(t["toState"])) for t in st["transitions"]]
        states.append({"name": S(st["name"]), "actions": acts, "transitions": trans})
    gtrans = [(S(t["fsmEvent"]["name"]), S(t["toState"])) for t in f.get("globalTransitions", [])]
    vars_ = {}
    V = f.get("variables", {})
    for key, fn in (("floatVariables", lambda d: rnd(d["value"])), ("intVariables", lambda d: d["value"]),
                    ("boolVariables", lambda d: bool(d["value"])), ("stringVariables", lambda d: S(d["value"])),
                    ("vector2Variables", lambda d: rnd(vec(d["value"], "xy"))),
                    ("vector3Variables", lambda d: rnd(vec(d["value"], "xyz"))),
                    ("colorVariables", lambda d: rnd(vec(d["value"], "rgba"))),
                    ("rectVariables", lambda d: rnd(vec(d["value"], ("x", "y", "width", "height")))),
                    ("gameObjectVariables", lambda d: ref(d["value"])),
                    ("objectVariables", lambda d: ref(d["value"]))):
        for d in V.get(key, []):
            vars_[S(d["name"])] = (key.replace("Variables", ""), fn(d))
    return {"name": S(f["name"]), "start": S(f["startState"]), "states": states, "global": gtrans, "vars": vars_,
            "events": [S(e["name"]) for e in f.get("events", [])]}


def fmt(d, go=""):
    lines = ["FSM %r on %r start=%s" % (d["name"], go, d["start"])]
    if d["vars"]:
        lines.append("  vars: " + ", ".join("%s:%s=%s" % (k, t, v) for k, (t, v) in d["vars"].items()))
    if d["global"]:
        lines.append("  global: " + ", ".join("%s->%s" % t for t in d["global"]))
    for st in d["states"]:
        lines.append("  [%s]" % st["name"])
        for a in st["actions"]:
            ps = ", ".join("%s=%s" % (k, v) for k, v in a["params"] if v not in (None, "", [], {}) and k not in ("everyFrame",) or (k == "everyFrame" and v))
            lines.append("    %s%s(%s)" % ("" if a["enabled"] else "#", a["name"], ps))
        for e, t in st["transitions"]:
            lines.append("    %s -> %s" % (e, t))
    return "\n".join(lines)
