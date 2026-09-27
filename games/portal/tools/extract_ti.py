#!/usr/bin/env python3
"""Pulls the art and levels out of the original TI-84 Plus CE release of
Portal Returns (Portal.8xp and Portal1P.8xv, by MateoConLechuga, sprites and
tiles by CKH4) into the editable files in ../assets:

  tiles.png      the 31 level tiles, 16x16, indexed with the game's palette
  sprites.png    player frames, portals, HUD, cube, pellet, fields, buttons...
  sprites.txt    name, x, y, w, h of each sprite in sprites.png
  font.png       the 8x8 font (characters 0x20 to 0x5A), white = ink
  title.png      the title screen pictures (1 bit), white = ink
  palette.txt    the default 16 color palette (RGB565)
  main.bin       the 40 built-in test chambers
  prelude.bin    the 38 Portal Prelude chambers (Portal1P.8xv, ported by Unicorn)

Only needed once; gen_assets.py turns the assets into src/assets.h.
usage: extract_ti.py Portal.8xp Portal1P.8xv
"""
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'assets')
BASE = 0xD1A881  # where the program runs (userMem)


def tivars(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'**TI83F*'
    p, end, out = 55, 55 + struct.unpack('<H', d[53:55])[0], []
    while p < end:
        hl, vl, typ = struct.unpack('<HHB', d[p:p + 5])
        name = d[p + 5:p + 13].rstrip(b'\0').decode('latin1')
        q = p + 2 + hl
        n = struct.unpack('<H', d[q:q + 2])[0]
        out.append((name, typ, d[q + 2:q + 2 + n]))
        p = q + 2 + n
    return out


def main():
    prog = tivars(sys.argv[1])[0][2][2 + 2:]  # skip the size and the asm token
    pack1 = tivars(sys.argv[2])[0][2][2:]

    def at(a, n):
        return prog[a - BASE:a - BASE + n]

    # palette: the program's default table, with the default color scheme
    # (white text and player on dark gray) applied like the game does.
    pal = list(struct.unpack('<16H', at(0xD1DF8B, 32)))
    pal[0] = pal[1] = 0xFFFF
    pal[2] = 0x4A4A

    def rgb(v):
        r, g, b = v >> 11 & 31, v >> 5 & 63, v & 31
        return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
    flat = [c for v in pal for c in rgb(v)]
    with open(os.path.join(OUT, 'palette.txt'), 'w') as f:
        f.write(' '.join('%04X' % v for v in pal) + '\n')

    def sprite4(a):
        h, w = at(a, 2)
        data = at(a + 2, h * w)
        im = Image.new('P', (2 * w, h))
        im.putpalette(flat)
        for y in range(h):
            for x in range(w):
                v = data[y * w + x]
                im.putpixel((2 * x, y), v >> 4)
                im.putpixel((2 * x + 1, y), v & 15)
        return im

    # tiles 1..31
    tiles = Image.new('P', (31 * 16, 16))
    tiles.putpalette(flat)
    for i in range(1, 32):
        tiles.paste(sprite4(0xD1F288 + 0x82 * i), ((i - 1) * 16, 0))
    tiles.save(os.path.join(OUT, 'tiles.png'))

    # sprites: the table after the font, then the title's portal letter
    addrs, a = [], 0xD1E1C7
    while a < 0xD1E75F:
        h, w = at(a, 2)
        addrs.append(a)
        a += 2 + h * w
    addrs.append(0xD1DD13)
    ims = [(a, sprite4(a)) for a in addrs]
    W, x, y, rowh, rects = 128, 0, 0, 0, []
    for a, im in ims:
        if x + im.width > W:
            x, y, rowh = 0, y + rowh + 1, 0
        rects.append((a, x, y, im.width, im.height))
        x += im.width + 1
        rowh = max(rowh, im.height)
    sheet = Image.new('P', (W, y + rowh))
    sheet.putpalette(flat)
    for (a, im), (_, x, y, w, h) in zip(ims, rects):
        sheet.paste(im, (x, y))
    sheet.save(os.path.join(OUT, 'sprites.png'))
    with open(os.path.join(OUT, 'sprites.txt'), 'w') as f:
        f.write('# name x y w h (name = address in the original program)\n')
        for a, x, y, w, h in rects:
            f.write('%04X %d %d %d %d\n' % (a & 0xFFFF, x, y, w, h))

    # font: 8 bytes per character from 0x20
    n = (0xD1E1C7 - 0xD1DFEF) // 8
    font = Image.new('1', (n * 8, 8), 0)
    for c in range(n):
        g = at(0xD1DFEF + 8 * c, 8)
        for yy in range(8):
            for xx in range(8):
                if not g[yy] >> (7 - xx) & 1:  # 0 bits are ink
                    font.putpixel((c * 8 + xx, yy), 1)
    font.save(os.path.join(OUT, 'font.png'))

    # title pictures: 1 bit, (h, width in bytes) header
    pics = [0xD1EE78, 0xD1EFB2, 0xD1F108, 0xD1F25E]
    dims = [(at(p, 2)[0], at(p, 2)[1] * 8) for p in pics]
    title = Image.new('1', (sum(w for h, w in dims) + len(pics), max(h for h, w in dims)), 0)
    x = 0
    for p, (h, w) in zip(pics, dims):
        data = at(p + 2, h * w // 8)
        for yy in range(h):
            for xx in range(w):
                if not data[yy * (w // 8) + xx // 8] >> (7 - xx % 8) & 1:  # 0 bits are ink
                    title.putpixel((x + xx, yy), 1)
        x += w + 1
    title.save(os.path.join(OUT, 'title.png'))

    # levels: the built-in pack runs to the end of the program
    main_pack = prog[0xD202C8 - BASE:]
    # keep up to the terminator of the last chamber
    p = 1
    for _ in range(main_pack[0]):
        p += 1
        while main_pack[p] != 0xAF:
            p += 1
        p += 1
    main_pack = main_pack[:p]
    # the calculator's keys: the TI's [2nd] is OK here
    for old, new in ((b'PRESS 2ND TO PICK THEM UP.', b'PRESS OK TO PICK THEM UP.'), (b'USE (2ND).', b'USE (OK).')):
        main_pack = main_pack.replace(old, new)
        pack1 = pack1.replace(old, new)
    open(os.path.join(OUT, 'main.bin'), 'wb').write(main_pack)
    open(os.path.join(OUT, 'prelude.bin'), 'wb').write(pack1)
    print('ok: %d sprites, %d font chars, main %d bytes, prelude %d bytes' % (len(rects), n, p, len(pack1)))


if __name__ == '__main__':
    main()
