"""The game's text: its language sheets (EN_*.txt TextAssets, encrypted; the key is found in the game's own assembly),
as {sheet: {key: text}}."""
import base64, functools, html, os, re, struct
import numpy as np
import unity


def _candidates(dll):
    """32-character alphanumeric strings in an assembly's user strings (UTF-16)."""
    data = open(dll, "rb").read()
    for m in re.finditer(rb"(?:[A-Za-z0-9]\x00){32}", data):
        s = m.group(0).decode("utf-16-le")
        yield s


def _decrypt(key, b64):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    raw = base64.b64decode(b64)
    d = Cipher(algorithms.AES(key.encode()), modes.ECB()).decryptor()
    out = d.update(raw) + d.finalize()
    pad = out[-1]
    if not 1 <= pad <= 16 or out[-pad:] != bytes([pad]) * pad:
        raise ValueError("padding")
    return out[:-pad].decode("utf-8")


def _assets():
    f = unity.asset_file("resources.assets")
    out = {}
    for o in f.objects.values():
        if o.type.name == "TextAsset":
            t = o.read()
            if t.m_Name.startswith("EN_"):
                data = t.m_Script
                out[t.m_Name[3:]] = data if isinstance(data, str) else data.decode("utf-8", "replace")
    return out


@functools.lru_cache(maxsize=None)
def _key():
    sample = _assets()["General"]
    dll = os.path.join(unity.DATA, "Managed", "Assembly-CSharp.dll")
    for k in _candidates(dll):
        try:
            _decrypt(k, sample)
            return k
        except Exception:
            continue
    raise RuntimeError("no key for the language sheets")


@functools.lru_cache(maxsize=None)
def sheets():
    """{sheet name (EN_ less): {entry key: text}}"""
    out = {}
    key = _key()
    for name, b64 in _assets().items():
        xml = _decrypt(key, b64)
        out[name] = {m.group(1): m.group(2) for m in re.finditer(r'<entry name="([^"]*)">(.*?)</entry>', xml, re.S)}
    return out


# ---------------------------------------------------------------- the texts the game shows, in its own code page
TEXT_K = 4 / 3   # text a third bigger than the game's (its sizes at this screen's are hard to read), boxes with it
BR, PAGE = 10, 12
PHASES = 4       # glyphs drawn a quarter pixel apart

# the styles: (font, em in pixels, line advance in pixels, ascender, descender in pixels)
#   TextMeshPro: an em is font size / 10 units; a line is (the font's line height + line spacing) / point size ems
HUD_PX = 100 / 8.7107        # (the HUD camera's: pixels a unit)
WORLD_PX = 421.94 / 38.1     # (the room's camera, at z = 0)


def _style(font, size, px, line_height, line_spacing, point, asc, desc):
    em = size * 0.1 * px * TEXT_K
    return (font, em, (line_height + line_spacing) / point * em, asc / point * em, -desc / point * em)


STYLES = {
    "DIALOGUE": _style("Perpetua", 7.2, HUD_PX, 134.0625, 24.6, 117, 95.9375, -38.1875),
    "PROMPT": _style("TrajanPro-Bold", 6.67, WORLD_PX, 84, 0, 70, 52.5, -17.5),
}


def clean(s):
    """A sheet's entry as shown: its markup to breaks and pages."""
    s = html.unescape(s)
    s = s.replace("<br>", "\n").replace("<page>", "\f")
    return s.strip(" ")


class Texts:
    def __init__(self):
        self.list, self.index = [], {}

    def id(self, sheet, key, style="DIALOGUE"):
        """A sheet's entry, by its key (shown in a style): its index."""
        return self.add(clean(sheets()[sheet][key]), style)

    def add(self, s, style):
        k = (s, style)
        if k not in self.index:
            self.index[k] = len(self.list)
            self.list.append(k)
        return self.index[k]

    def code_page(self):
        extra = sorted({c for s, _ in self.list for c in s if not (32 <= ord(c) < 127 or c in "\n\f")})
        assert len(extra) <= 128, extra
        cp = {chr(i): i for i in range(32, 127)}
        cp["\n"], cp["\f"] = BR, PAGE
        for i, c in enumerate(extra):
            cp[c] = 128 + i
        return cp

    def blob(self):
        """TEXT: count, offsets, then each text (its code page's bytes, 0 ended)"""
        cp = self.code_page()
        offs, body = [], bytearray()
        for s, _ in self.list:
            offs.append(len(body))
            body += bytes(cp[c] for c in s) + b"\0"
        return struct.pack("<I", len(self.list)) + b"".join(struct.pack("<I", o) for o in offs) + bytes(body)

    def fonts(self):
        """FONT: per style (as STYLES' order) its offset; a style: em, line advance, ascender, descender (floats),
        256 x PHASES glyph offsets (u16, from the style, 0 none: a code's glyph with the pen 0, 1/4... of a pixel right
        of a whole pixel), then the glyphs: width, height, left, top (from the pen's pixel on the baseline, pixels),
        advance (1/64 pixels, u16), then rows of 4-bit alpha (low nibble first)."""
        import font
        cp = self.code_page()
        blocks = []
        for name, (fname, em, line, asc, desc) in STYLES.items():
            chars = sorted({c for s, st in self.list if st == name for c in s if c not in "\n\f"} | {" ", "-"})
            head = bytearray(struct.pack("<ffff", em, line, asc, desc))
            offs = [0] * (256 * PHASES)
            body = bytearray()
            base = len(head) + 2 * len(offs)
            for ph in range(PHASES):
                for ch, g in zip(chars, font.bitmaps(fname, chars, em, ph / PHASES)):
                    a, left, top, adv = g
                    h, w = a.shape
                    offs[cp[ch] * PHASES + ph] = base + len(body)
                    body += struct.pack("<BBbbH", w, h, left, top, min(65535, round(adv * 64)))
                    q = (a.astype(np.uint16) * 15 + 127) // 255
                    for row in q:
                        r = list(row) + [0] * (w & 1)
                        body += bytes(r[i] | r[i + 1] << 4 for i in range(0, len(r), 2))
                    if len(body) & 1:
                        body.append(0)
            assert base + len(body) < 65536, name
            while len(body) % 4:
                body.append(0)
            blocks.append(bytes(head) + struct.pack("<%dH" % len(offs), *offs) + bytes(body))
        out = bytearray(struct.pack("<I", len(blocks)))
        pos = 4 + 4 * len(blocks)
        for b in blocks:
            out += struct.pack("<I", pos)
            pos += len(b)
        for b in blocks:
            out += b
        return bytes(out)
