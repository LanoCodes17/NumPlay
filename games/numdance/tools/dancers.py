"""Packs NumDance's dancers (pixel maps, 4 bits a pixel) into C arrays for src/main.c:
    python3 tools/dancers.py   (paste its output between the art markers)
Each map pixel is drawn 2x2 on the calculator. '.' is transparent."""
SPRITES = [
    ("the robot", {
        'M': 0xC3CEDA, 'm': 0x7C8C9F, 'D': 0x2E3A4C, 'V': 0x15202E, 'C': 0x5FF3FF, 'R': 0xFF5A5A, 'Y': 0xFFD23F,
    }, [
        ".......RR.......",
        "........m.......",
        "...MMMMMMMMMM...",
        "..MMVVVVVVVVMM..",
        "..MMVCCVVCCVMM..",
        "..MMVVVVVVVVMM..",
        "...MMMMMMMMMM...",
        "...MmMmMmMmMM...",
        ".....mmmmmm.....",
        "....MMMMMMMM....",
        "...MMMMMMMMMM...",
        "...MMDDDDDDMM...",
        "...MMDYDRDCMM...",
        "...MMDDDDDDMM...",
        "...MMMMMMMMMM...",
        "....MMMMMMMM....",
        "....mmmmmmmm....",
        ".....mm..mm.....",
        ".....MM..MM.....",
        ".....MM..MM.....",
        ".....mm..mm.....",
        "....MMM..MMM....",
        "....DDD..DDD....",
    ]),
    ("the fox", {
        'O': 0xF28C38, 'o': 0xB85A1C, 'W': 0xFFF4E6, 'K': 0x2B1D1A, 'G': 0x2EC4B6,
    }, [
        "..K........K..",
        "..OK......KO..",
        "..OOK....KOO..",
        "..OOOOOOOOOO..",
        ".OOOOOOOOOOOO.",
        ".OOKOOOOOOKOO.",
        ".OOOOWWWWOOOO.",
        "..OWWWKKWWWO..",
        "...WWWWWWWW...",
        "....WWWWWW....",
        ".....GGGG.....",
        "....GGGGGG....",
        "....OOWWOO....",
        "...OOOWWOOO..W",
        "...OOOWWOOO.OW",
        "...OOOWWOOOOO.",
        "....OOOOOOOO..",
        "....oo..oo....",
        "....OO..OO....",
        "....OO..OO....",
        "...KKK..KKK...",
    ]),
    ("the penguin", {
        'K': 0x1E2433, 'W': 0xF4F8FF, 'Y': 0xFFA62B, 'B': 0xE84A5F, 'k': 0x46547A,  # k: the flippers
    }, [
        ".....KKKK.....",
        "...KKKKKKKK...",
        "..KKKKKKKKKK..",
        "..KWWKKKKWWK..",
        "..KWKWKKWKWK..",
        "..KWWWYYWWWK..",
        "..KKWYYYYWKK..",
        "..KKKKYYKKKK..",
        "...KKKKKKKK...",
        "..KKWBBBBWKK..",
        "..KKWWBBWWKK..",
        ".KKWWWWWWWWKK.",
        ".KKWWWWWWWWKK.",
        ".KKWWWWWWWWKK.",
        ".KKWWWWWWWWKK.",
        "..KKWWWWWWKK..",
        "...KKWWWWKK...",
        "....KKKKKK....",
        "...YYY..YYY...",
    ]),
    ("the frog", {
        'G': 0x5CC25A, 'g': 0x2E8B3E, 'L': 0xD4F5A3, 'W': 0xFFFFFF, 'K': 0x1E2433, 'R': 0xFF5A7A,
    }, [
        "..GGG....GGG..",
        ".GWWWG..GWWWG.",
        ".GWKWG..GWKWG.",
        ".GGGGGGGGGGGG.",
        "GGGGGGGGGGGGGG",
        "GGGGGGGGGGGGGG",
        "GgGGGGGGGGGGgG",
        ".GggggggggggG.",
        "..GGGGGGGGGG..",
        "...RRRRRRRR...",
        "..GGLLLLLLGG..",
        "..GGLLLLLLGG..",
        "..GGLLLLLLGG..",
        "..GGLLLLLLGG..",
        "...GGLLLLGG...",
        "....GGGGGG....",
        "...GGG..GGG...",
        "..GGGG..GGGG..",
        ".gggg....gggg.",
    ]),
]

out, pals, meta, off = [], [], [], 0
for name, pal, rows in SPRITES:
    keys = list(pal)
    assert len(keys) <= 15, name
    w, h = len(rows[0]), len(rows)
    assert all(len(r) == w for r in rows), name
    px = [0 if ch == '.' else keys.index(ch) + 1 for r in rows for ch in r]
    if len(px) % 2: px.append(0)
    data = [px[i] | px[i + 1] << 4 for i in range(0, len(px), 2)]
    meta.append((w, h, off))
    off += len(data)
    out += data
    pals.append([pal[k] for k in keys])

print("/* dancers: art begin (made by tools/dancers.py from its pixel maps, 4 bits a pixel) */")
n = max(map(len, pals))
print("#define DPN %d /* colors in a palette */" % n)
print("static const uint32_t DPAL[4][DPN] = {")
for p in pals:
    print("  {" + ", ".join("0x%06X" % c for c in p + [0] * (n - len(p))) + "},")
print("};")
print("static const struct { uint8_t w, h; uint16_t off; } DSPR[4] = {" + ", ".join("{%d, %d, %d}" % m for m in meta) + "};")
print("static const uint8_t DPIX[%d] = {" % len(out))
for i in range(0, len(out), 24):
    print("  " + ",".join("0x%02X" % b for b in out[i:i + 24]) + ",")
print("};")
print("/* art end */")
