#!/usr/bin/env python3
"""The 12-pixel font for letters the games' own fonts don't have (Chinese).

  xfont.py extract font.pcf assets/fonts/fusion-pixel-12px.bin
      Keeps the letters of GB2312 (simplified Chinese), CJK and fullwidth
      punctuation, and Latin (ASCII, Latin-1, Latin Extended-A) from Fusion Pixel 12px (SIL Open Font License 1.1, see
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
    # Latin too: for a game whose own font lacks letters a translation needs (np_xdraw draws them)
    cps |= set(range(0x21, 0x7F)) | set(range(0xA1, 0x180))
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
    # every letter beyond ASCII the texts use, and the Latin ones too (for games whose font lacks some)
    cps = sorted(c for c in used if c in font and (c >= 0x2100 or 0x21 <= c < 0x180))
    L = ["/* Made by tools/xfont.py from Fusion Pixel 12px (SIL Open Font License 1.1): the letters this",
         "   build's texts use that the games' fonts don't have. See games/common/np_text.h. */"]
    bits, sizes, size, start = [], [], [], []
    for k, cp in enumerate(cps):
        adv, x, y, w, h, r = font[cp]
        if cp >= 0x10000:
            raise SystemExit("xfont: letters above 0xFFFF")
        if k % 16 == 0:
            start.append(len(bits))
        stride = (w + 7) // 8
        bits += [r[j * stride + i // 8] >> (i % 8) & 1 for j in range(h) for i in range(w)]
        s = (adv, w, h, x, y)  # (the Chinese letters share a few)
        if s not in sizes:
            sizes.append(s)
        size.append(sizes.index(s))
    if len(sizes) > 256:
        raise SystemExit("xfont: more than 256 sizes")
    rows = bytearray((len(bits) + 7) // 8)
    for k, b in enumerate(bits):
        rows[k // 8] |= b << (k % 8)

    def array(ctype, name, values, per_line=24):
        out = [f"static const {ctype} {name}[{len(values)}] = {{"]
        for i in range(0, len(values), per_line):
            out.append("  " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
        return out + ["};"]
    if cps:
        L += array("uint16_t", "np_xfont_cps", cps, 16)
        L += array("uint8_t", "np_xfont_size", size)
        L.append("static const np_xglyph_t np_xfont_sizes[] = {")
        L += [f"  {{{adv}, {w}, {h}, {x}, {y}}}," for adv, w, h, x, y in sizes]
        L.append("};")
        L += array("uint32_t", "np_xfont_start", start, 12)
        L += array("uint8_t", "np_xfont_rows", list(rows))
        L.append(f"const np_xfont_t np_xfont = {{{len(cps)}, np_xfont_cps, np_xfont_size, np_xfont_sizes, "
                 "np_xfont_start, np_xfont_rows};")
    else:
        L.append("const np_xfont_t np_xfont = {0, 0, 0, 0, 0, 0};")
    open(out, "w").write("\n".join(L) + "\n")
    return len(cps), 3 * len(cps) + 5 * len(sizes) + 4 * len(start) + len(rows)


if __name__ == "__main__":
    if sys.argv[1] == "extract":
        extract(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "table":
        n, size = table(sys.argv[2], sys.argv[3], sys.argv[4:])
        print(f"{sys.argv[3]}: {n} letters, about {size} bytes")
