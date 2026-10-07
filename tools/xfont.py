#!/usr/bin/env python3
"""The 12-pixel font for letters the games' own fonts don't have (Chinese).

  xfont.py extract font.pcf assets/fonts/fusion-pixel-12px.bin
      Keeps the letters of GB2312 (simplified Chinese), CJK and fullwidth
      punctuation from Fusion Pixel 12px (SIL Open Font License 1.1, see
      LICENSES/OFL-1.1-Fusion-Pixel.txt), in a small file committed with the
      sources: magic "NPXF", the count, then for each letter its code point
      (4 bytes), advance, x, y (from the top of a 12-pixel line), width, height
      and its rows ((width + 7) / 8 bytes each, the first pixel in bit 0).
  xfont.py table assets/fonts/fusion-pixel-12px.bin out.h TEXT_FILES...
      Writes games/common/np_xfont.h-style C: np_xfont with only the letters
      the text files use (see games/common/np_text.h).
"""
import struct
import sys

ASCENT = 10  # Fusion Pixel 12px: 10 above the baseline, 2 below


def wanted():
    cps = set()
    for b1 in range(0xA1, 0xF8):
        for b2 in range(0xA1, 0xFF):
            try:
                cps.add(ord(bytes([b1, b2]).decode("gb2312")))
            except (UnicodeDecodeError, TypeError):
                pass
    cps |= set(range(0x3000, 0x3040)) | set(range(0xFF00, 0xFFF0)) | set(range(0x2010, 0x2070))
    return cps


def extract(pcf, out):
    sys.path.insert(0, __file__.rsplit("/", 1)[0])
    from pcf import read_pcf
    font = read_pcf(pcf)
    keep = sorted(cp for cp in wanted() if cp in font and font[cp][4] > 0 or (cp in font and cp in (0x3000,)))
    data = bytearray(b"NPXF" + struct.pack("<I", len(keep)))
    for cp in keep:
        adv, l, a, w, h, rows = font[cp]
        data += struct.pack("<IBbbBB", cp, adv, l, ASCENT - a, w, h)
        stride = (w + 7) // 8
        for r in rows:
            data += r.to_bytes(stride, "little") if stride else b""
    open(out, "wb").write(data)
    print(f"{out}: {len(keep)} letters, {len(data)} bytes")


def load(path):
    d = open(path, "rb").read()
    assert d[:4] == b"NPXF"
    n, = struct.unpack_from("<I", d, 4)
    p, out = 8, {}
    for _ in range(n):
        cp, adv, x, y, w, h = struct.unpack_from("<IBbbBB", d, p)
        p += 9
        size = h * ((w + 7) // 8)
        out[cp] = (adv, x, y, w, h, d[p:p + size])
        p += size
    return out


def table(font_path, out, texts):
    font = load(font_path)
    used = set()
    for t in texts:
        used |= {ord(c) for c in open(t, encoding="utf-8").read() if ord(c) >= 0x80}
    # (Latin letters and the usual punctuation are drawn by the games' fonts: see np_latin)
    cps = sorted(c for c in used if c in font and c >= 0x2100)
    L = ["/* Made by tools/xfont.py from Fusion Pixel 12px (SIL Open Font License 1.1): the letters this",
         "   build's texts use that the games' fonts don't have. See games/common/np_text.h. */"]
    rows = bytearray()
    glyphs = []
    for cp in cps:
        adv, x, y, w, h, r = font[cp]
        stride = (w + 7) // 8
        bits = [r[j * stride + i // 8] >> (i % 8) & 1 for j in range(h) for i in range(w)]
        packed = bytearray((len(bits) + 7) // 8)
        for k, b in enumerate(bits):
            packed[k // 8] |= b << (k % 8)
        if cp >= 0x10000 or len(rows) >= 0x10000:
            raise SystemExit("xfont: too many letters for 16-bit offsets")
        glyphs.append(f"{{{cp:#x}, {adv}, {w}, {h}, {x}, {y}, {len(rows)}}}")
        rows += packed  # (each letter starts on a byte: its offset counts bytes)
    if cps:
        L.append("static const np_xglyph_t np_xfont_glyphs[] = {")
        L += [f"  {g}," for g in glyphs]
        L.append("};")
        L.append(f"static const uint8_t np_xfont_rows[{len(rows)}] = {{")
        for i in range(0, len(rows), 24):
            L.append("  " + ", ".join(str(b) for b in rows[i:i + 24]) + ",")
        L.append("};")
        L.append(f"const np_xfont_t np_xfont = {{{len(cps)}, np_xfont_glyphs, np_xfont_rows}};")
    else:
        L.append("const np_xfont_t np_xfont = {0, 0, 0};")
    open(out, "w").write("\n".join(L) + "\n")
    return len(cps), len(rows) + 10 * len(cps)


if __name__ == "__main__":
    if sys.argv[1] == "extract":
        extract(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "table":
        n, size = table(sys.argv[2], sys.argv[3], sys.argv[4:])
        print(f"{sys.argv[3]}: {n} letters, about {size} bytes")
