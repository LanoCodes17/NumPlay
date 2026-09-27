#!/usr/bin/env python3
"""Builds src/data.c and src/data.h (names, prices, rarities, descriptions)
from Balatro's own game data: game.lua and localization/en-us.lua.

Usage: mkdata.py PATH_TO_BALATRO_SOURCE

Descriptions keep the game's wording and colours. Colour tags become the
control codes of src/gfx.h, and #n# placeholders become bytes 0x11+n-1, filled
in at run time."""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


# ------------------------------------------------------------------ lua
class Lua:
    def __init__(self, s):
        self.s = s
        self.i = 0

    def ws(self):
        s = self.s
        while self.i < len(s):
            if s[self.i] in " \t\r\n,;":
                self.i += 1
            elif s.startswith("--", self.i):
                while self.i < len(s) and s[self.i] != "\n":
                    self.i += 1
            else:
                break

    def value(self):
        self.ws()
        s = self.s
        c = s[self.i]
        if c == "{":
            return self.table()
        if c in "\"'":
            q = c
            self.i += 1
            out = []
            while s[self.i] != q:
                if s[self.i] == "\\":
                    self.i += 1
                    out.append({"n": "\n", "t": "\t"}.get(s[self.i], s[self.i]))
                else:
                    out.append(s[self.i])
                self.i += 1
            self.i += 1
            return "".join(out)
        m = re.compile(r"-?\d+(\.\d+)?").match(s, self.i)
        if m:
            self.i = m.end()
            return float(m.group()) if "." in m.group() else int(m.group())
        m = re.compile(r"[A-Za-z_][A-Za-z_0-9.]*").match(s, self.i)
        self.i = m.end()
        w = m.group()
        return {"true": True, "false": False, "nil": None}.get(w, w)

    def table(self):
        self.i += 1
        d, arr = {}, []
        while True:
            self.ws()
            if self.s[self.i] == "}":
                self.i += 1
                break
            m = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)\s*=(?!=)").match(self.s, self.i)
            if m:
                self.i = m.end()
                d[m.group(1)] = self.value()
                continue
            m = re.compile(r"\[\s*(\"[^\"]*\"|'[^']*'|\d+)\s*\]\s*=").match(self.s, self.i)
            if m:
                self.i = m.end()
                k = m.group(1)
                d[k.strip("\"'") if k[0] in "\"'" else int(k)] = self.value()
                continue
            arr.append(self.value())
        if arr and not d:
            return arr
        if arr:
            d["_"] = arr
        return d


def centers(game_lua):
    """P_CENTERS / P_TAGS / P_BLINDS entries, one per line: key -> dict of simple fields."""
    out = {}
    for line in game_lua.split("\n"):
        m = re.match(r"\s*([a-z]+_[a-z0-9_]+)\s*=\s*(\{.*\}),?\s*$", line)
        if not m:
            continue
        body = m.group(2)
        body = re.sub(r"HEX\('([0-9a-fA-F]+)'\)", r"'\1'", body)
        body = re.sub(r"localize\('([a-z_]+)'\)", r"'\1'", body)
        body = body.replace("9.6/4", "2.4").replace("32/4", "8")
        body = re.sub(r"G\.[A-Za-z_.]+", "0", body)
        try:
            v = Lua(body).value()
            if isinstance(v, dict):
                out[m.group(1)] = v
        except Exception:
            pass
    return out


# ------------------------------------------------------------------ text
CODES = {
    "mult": "\x03", "red": "\x03", "chips": "\x02", "blue": "\x02", "attention": "\x04", "important": "\x04",
    "filter": "\x04", "orange": "\x04", "money": "\x05", "gold": "\x05", "yellow": "\x05", "green": "\x06",
    "inactive": "\x07", "tarot": "\x08", "purple": "\x08", "legendary": "\x08", "planet": "\x09",
    "spectral": "\x0B", "dark_edition": "\x0F", "edition": "\x0F", "white": "\x01", "clubs": "\x04",
    "hearts": "\x03", "diamonds": "\x04", "spades": "\x0C", "grey": "\x07", "joy_spades": "\x04",
    "voucher": "\x04", "rare": "\x03", "uncommon": "\x06", "common": "\x02",
}


def conv(line):
    def tag(m):
        body = m.group(1)
        if body == "":
            return "\x10"
        parts = dict(p.split(":", 1) for p in body.split(",") if ":" in p)
        if parts.get("X") in ("mult", "red"):
            return "\x0E"
        if "X" in parts:
            return "\x04"
        if "V" in parts:
            return "\x04"
        c = parts.get("C")
        if c is None:
            return ""
        return CODES.get(c, "\x04")
    line = re.sub(r"\{([^}]*)\}", tag, line)
    line = re.sub(r"#(\d)#", lambda m: chr(0x10 + int(m.group(1))), line)
    return line


def cstr(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == "\\" or ch == '"':
            out.append("\\" + ch)
        elif o < 32 or o > 126:
            out.append(f"\\x{o:02X}\"\"")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


TEXT_GET = r"""
static char *expand(char *o, char *end, int c) {
  if (c < 0x80) {
    if (o < end) *o++ = (char)c;
    return o;
  }
  o = expand(o, end, text_pairs[c - 0x80][0]);
  return expand(o, end, text_pairs[c - 0x80][1]);
}

const char *text_get(int idx, int part) {
  static char bufs[4][220];
  static int k;
  char *b = bufs[k = (k + 1) & 3], *o = b;
  const uint8_t *p = text_blob + text_off[idx];
  if (part)
    while (*p++) {}
  while (*p) o = expand(o, b + sizeof bufs[0] - 1, *p++);
  *o = 0;
  return b;
}
"""


def main():
    src = sys.argv[1]
    game = open(os.path.join(src, "game.lua")).read()
    loc = Lua(open(os.path.join(src, "localization", "en-us.lua")).read().split("return", 1)[1]).value()
    C = centers(game)
    D = loc["descriptions"]
    misc = loc["misc"]

    jokers = sorted([(v["order"], k, v) for k, v in C.items() if k.startswith("j_") and "order" in v])
    tarots = sorted([(v["order"], k, v) for k, v in C.items() if v.get("set") == "Tarot" and k.startswith("c_")])
    planets = sorted([(v["order"], k, v) for k, v in C.items() if v.get("set") == "Planet" and k.startswith("c_")])
    spectrals = sorted([(v["order"], k, v) for k, v in C.items() if v.get("set") == "Spectral" and k.startswith("c_")])
    vouchers = sorted([(v["order"], k, v) for k, v in C.items() if v.get("set") == "Voucher" and k.startswith("v_")])
    tags = sorted([(v["order"], k, v) for k, v in C.items() if k.startswith("tag_") and "order" in v])
    blinds = [(k, v) for k, v in C.items() if k.startswith("bl_")]
    blinds.sort(key=lambda kv: kv[1]["pos"]["y"])
    assert len(jokers) == 150 and len(tarots) == 22 and len(planets) == 12 and len(spectrals) == 18
    assert len(vouchers) == 32 and len(tags) == 24 and len(blinds) == 30, (len(vouchers), len(tags), len(blinds))
    # vouchers in sprite order: 16 base ones by order, then the upgrade of each
    base = [x for x in vouchers if x[0] % 2 == 1]
    up = [x for x in vouchers if x[0] % 2 == 0]
    vouchers = base + up

    texts = []  # (name, description)

    def desc(cat, key):
        e = D[cat][key]
        lines = e.get("text", [])
        return e["name"], "\n".join(conv(l) for l in lines)

    rows = []
    for o, k, v in jokers:
        n, t = desc("Joker", k)
        rows.append(("J", k, n, t, v))
    for grp, cat in ((tarots, "Tarot"), (planets, "Planet"), (spectrals, "Spectral")):
        for o, k, v in grp:
            n, t = desc(cat, k)
            rows.append(("C", k, n, t, v))
    for o, k, v in vouchers:
        n, t = desc("Voucher", k)
        rows.append(("V", k, n, t, v))
    for o, k, v in tags:
        n, t = desc("Tag", k)
        rows.append(("T", k, n, t, v))
    for k, v in blinds:
        n, t = desc("Blind", k)
        rows.append(("B", k, n, t, v))
    extra = [("Enhanced", k) for k in ["m_bonus", "m_mult", "m_wild", "m_glass", "m_steel", "m_stone", "m_gold",
                                       "m_lucky"]]
    extra += [("Edition", k) for k in ["e_foil", "e_holo", "e_polychrome", "e_negative"]]
    extra += [("Other", k) for k in ["gold_seal", "red_seal", "blue_seal", "purple_seal"]]
    extra += [("Other", k) for k in ["p_arcana_normal", "p_arcana_jumbo", "p_arcana_mega", "p_celestial_normal",
                                     "p_celestial_jumbo", "p_celestial_mega", "p_standard_normal",
                                     "p_standard_jumbo", "p_standard_mega", "p_buffoon_normal",
                                     "p_buffoon_jumbo", "p_buffoon_mega", "p_spectral_normal",
                                     "p_spectral_jumbo", "p_spectral_mega"]]
    extra += [("Back", "b_red")]
    for cat, k in extra:
        n, t = desc(cat, k)
        rows.append(("X", k, n, t, {}))

    # string blob, compressed with byte pair encoding (codes 0x80..0xFF)
    raw = [(r[2] + "\0" + r[3] + "\0").encode("latin1") for r in rows]
    pairs = []
    seqs = [list(x) for x in raw]
    for code in range(0x80, 0x100):
        counts = {}
        for sq in seqs:
            for i in range(len(sq) - 1):
                if sq[i] and sq[i + 1]:
                    counts[(sq[i], sq[i + 1])] = counts.get((sq[i], sq[i + 1]), 0) + 1
        if not counts:
            break
        best = max(counts, key=counts.get)
        if counts[best] < 3:
            break
        pairs.append(best)
        for k, sq in enumerate(seqs):
            out, i = [], 0
            while i < len(sq):
                if i + 1 < len(sq) and (sq[i], sq[i + 1]) == best:
                    out.append(code)
                    i += 2
                else:
                    out.append(sq[i])
                    i += 1
            seqs[k] = out
    blob = b"".join(bytes(x) for x in seqs)
    offs = []
    pos = 0
    for sq in seqs:
        offs.append(pos)
        pos += len(sq)
    h = ["/* Generated by tools/mkdata.py from Balatro's game data: do not edit. */", "#ifndef DATA_H",
         "#define DATA_H", "#include <stdint.h>", ""]
    idx = 0
    for tag, name in (("J", "JOKER"), ("C", "CONS"), ("V", "VOUCH"), ("T", "TAG"), ("B", "BLIND"), ("X", "EXTRA")):
        n = sum(1 for r in rows if r[0] == tag)
        h.append(f"#define TXT_{name} {idx}\n#define N_{name} {n}")
        idx += n
    h.append(f"#define N_TEXTS {idx}")
    def ename(k, pre):
        return pre + re.sub(r"^(j|c|v|tag|bl)_", "", k).upper()
    for pre, lst in (("J_", [k for o, k, v in jokers]), ("C_", [k for o, k, v in tarots + planets + spectrals]),
                     ("V_", [k for o, k, v in vouchers]), ("TAG_", [k for o, k, v in tags]),
                     ("BL_", [k for k, v in blinds])):
        h.append("enum { " + ", ".join(ename(k, pre) for k in lst) + " };")
    h.append("extern const uint8_t text_blob[];")
    h.append("extern const uint8_t text_pairs[128][2];")
    h.append("const char *text_get(int idx, int part); /* 0 name, 1 description; decoded */")
    h.append("extern const uint16_t text_off[N_TEXTS];")
    h.append("typedef struct { uint8_t rarity, cost, bp; } joker_info_t;")
    h.append("extern const joker_info_t joker_info[N_JOKER];")
    h.append("typedef struct { uint8_t dollars, mult2, min, showdown; uint32_t colour; } blind_info_t;")
    h.append("extern const blind_info_t blind_info[N_BLIND];")
    h.append("typedef struct { int8_t min_ante; } tag_info_t;")
    h.append("extern const tag_info_t tag_info[N_TAG];")
    h.append("extern const char *const hand_names[12];")
    h.append("#endif")
    c = ["/* Generated by tools/mkdata.py from Balatro's game data (LocalThunk): do not edit. */",
         '#include "data.h"', ""]
    c.append("const uint8_t text_blob[%d] = {" % len(blob))
    for i in range(0, len(blob), 24):
        c.append("  " + ",".join(str(v) for v in blob[i:i + 24]) + ",")
    c.append("};")
    c.append("const uint8_t text_pairs[128][2] = {" + ",".join("{%d,%d}" % p for p in pairs) + "};")
    c.append(TEXT_GET)
    c.append("const uint16_t text_off[N_TEXTS] = {" + ",".join(map(str, offs)) + "};")
    c.append("const joker_info_t joker_info[N_JOKER] = {")
    for o, k, v in jokers:
        c.append(f"  {{{v['rarity']}, {v['cost']}, {1 if v.get('blueprint_compat') else 0}}}, /* {v['name']} */")
    c.append("};")
    c.append("const blind_info_t blind_info[N_BLIND] = {")
    for k, v in blinds:
        boss = v.get("boss") or {}
        col = int(v.get("boss_colour", "0"), 16) if v.get("boss_colour") else 0
        c.append(f"  {{{v['dollars']}, {int(v['mult'] * 2)}, {boss.get('min', 0)}, {1 if boss.get('showdown') else 0},"
                 f" 0x{col:06X}}}, /* {v['name']} */")
    c.append("};")
    c.append("const tag_info_t tag_info[N_TAG] = {" + ",".join(
        f"{{{v.get('min_ante') or 0}}}" for o, k, v in tags) + "};")
    hands = ["Flush Five", "Flush House", "Five of a Kind", "Straight Flush", "Four of a Kind", "Full House", "Flush",
             "Straight", "Three of a Kind", "Two Pair", "Pair", "High Card"]
    c.append("const char *const hand_names[12] = {" + ",".join(cstr(misc["poker_hands"][x]) for x in hands) + "};")
    open(os.path.join(ROOT, "src", "data.h"), "w").write("\n".join(h) + "\n")
    open(os.path.join(ROOT, "src", "data.c"), "w").write("\n".join(c) + "\n")
    print("texts", len(rows), "bytes", pos, "(raw", sum(len(x) for x in raw), ") pairs", len(pairs))

if __name__ == "__main__":
    main()
