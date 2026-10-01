#!/usr/bin/env python3
"""Packs Minecraft 1.8.8's textures for NumBlocks into src/data.c and src/data.h.

    python3 tools/pack.py CLIENT_JAR

CLIENT_JAR is the official 1.8.8 client (client.jar, from Mojang's launcher
servers); only its pictures are read. Every texture is 16 x 16 with its own
palette of up to 16 colours (4 bits a texel): tinted ones (grass, leaves,
water) keep their grey levels, and the game multiplies them by the biome's
colour. Index 0 is transparent in textures that have see-through texels.

Also written: each block state's six faces, model, light, flags, hardness and
tool (from tools/blocks.py)."""
import io
import os
import sys
import zipfile

from PIL import Image


def pixels(im):
    return list(im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata())

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import blocks  # noqa: E402

TEX = 'assets/minecraft/textures/'


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


class Jar:
    def __init__(self, path):
        self.z = zipfile.ZipFile(path)

    def image(self, name):
        return Image.open(io.BytesIO(self.z.read(TEX + name + '.png'))).convert('RGBA')


def quantize(im, colors, keep_grey=False):
    """(indices 16x16, palette [(r,g,b)], transparent?): index 0 is transparent if any texel is."""
    px = pixels(im)
    alpha = any(a < 128 for _, _, _, a in px)
    opaque = [(r, g, b) for r, g, b, a in px if a >= 128]
    n = colors - (1 if alpha else 0)
    src = Image.new('RGB', (len(opaque), 1))
    src.putdata(opaque)
    q = src.quantize(n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:3 * n]
    pal = [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
    idx = pixels(q)
    out, k = [], 0
    for r, g, b, a in px:
        if a < 128:
            out.append(0)
        else:
            out.append(idx[k] + (1 if alpha else 0))
            k += 1
    if alpha:
        pal = [(0, 0, 0)] + pal
    return out, pal, alpha


def first_frame(im):
    w, h = im.size
    return im.crop((0, 0, w, w)).resize((16, 16), Image.NEAREST) if w != 16 or h != 16 else im


def pack_textures(jar):
    """Textures in the order blocks.py first names them."""
    names = []
    for s in blocks.S:
        t = s['tex']
        if t is None:
            continue
        for n in ([t] if isinstance(t, str) else list(t.values())):
            if n and n not in names:
                names.append(n)
    tinted = set()
    for s in blocks.S:
        if s['tint'] and s['tex']:
            t = s['tex']
            if isinstance(t, str):
                tinted.add(t)
            else:
                tinted.add(t['top'] if s['name'].startswith('GRASS') else t.get('side'))
                if 'overlay' in t:
                    tinted.add(t['overlay'])
    tinted.discard('grass_side')   # its fringe comes from the overlay
    tinted.discard('grass_side_snowed')
    px, pals, flags = [], [], []
    for n in names:
        im = first_frame(jar.image('blocks/' + n))
        if n == 'grass_side':
            # the dirt side keeps its colours; the fringe (the overlay's texels)
            # becomes grey levels tinted by the biome: palette indices from 8
            over = first_frame(jar.image('blocks/grass_side_overlay'))
            base = im.copy()
            ov = [a >= 128 for _, _, _, a in pixels(over)]
            dirt = Image.new('RGBA', (16, 16))
            dirt.putdata([p if not o else (0, 0, 0, 0) for p, o in zip(pixels(base), ov)])
            grey = Image.new('RGBA', (16, 16))
            grey.putdata([p if o else (0, 0, 0, 0) for p, o in zip(pixels(over), ov)])
            di, dp, _ = quantize(dirt, 9)
            gi, gp, _ = quantize(grey, 9)
            idx = [(gi[i] + 7) if ov[i] else di[i] for i in range(256)]
            pal = dp[:8] + gp[1:9]
            while len(pal) < 16:
                pal.append((0, 0, 0))
            px.append(idx)
            pals.append(pal)
            flags.append(8)   # tinted from index 8
            continue
        idx, pal, alpha = quantize(im, 16)
        while len(pal) < 16:
            pal.append((0, 0, 0))
        px.append(idx)
        pals.append(pal)
        tint_from = (1 if alpha else 0) if n in tinted else 16
        flags.append(tint_from | (0x20 if alpha else 0))
    return names, px, pals, flags


FACE = ['bottom', 'top', 'north', 'south', 'west', 'east']   # -Y +Y -Z +Z -X +X


def faces(s, names):
    t = s['tex']
    if t is None:
        return [0] * 6
    if isinstance(t, str):
        i = names.index(t)
        return [i] * 6
    side = t.get('side')
    out = []
    for f in FACE:
        n = t.get(f) or (t.get('top') if f == 'bottom' and 'bottom' not in t else side)
        if f in ('west', 'east') and 'x' in t:
            n = t['x']
        if f in ('north', 'south') and 'z' in t:
            n = t['z']
        out.append(names.index(n))
    # a front: furnaces and chests face north (meta 2), pumpkins south (meta 0)
    if 'front' in t:
        out[2 if s['meta'] == 2 else 3] = names.index(t['front'])
    return out


TINT = {None: 0, 'grass': 1, 'foliage': 2, 'spruce': 3, 'birch': 4, 'water': 5, 'lily': 6}
OPAQUE = {'cube', 'slab'}   # (slabs and layers only hide what is under them)


# biome id: (temperature, rainfall) as BiomeBase sets them (1.8.8); mutated biomes (id + 128) share them
BIOMES = {0: (0.5, 0.5), 1: (0.8, 0.4), 2: (2.0, 0.0), 3: (0.2, 0.3), 4: (0.7, 0.8), 5: (0.25, 0.8), 6: (0.8, 0.9),
          7: (0.5, 0.5), 8: (2.0, 0.0), 9: (0.5, 0.5), 10: (0.0, 0.5), 11: (0.0, 0.5), 12: (0.0, 0.5), 13: (0.0, 0.5),
          14: (0.9, 1.0), 15: (0.9, 1.0), 16: (0.8, 0.4), 17: (2.0, 0.0), 18: (0.7, 0.8), 19: (0.25, 0.8),
          20: (0.2, 0.3), 21: (0.95, 0.9), 22: (0.95, 0.9), 23: (0.95, 0.8), 24: (0.5, 0.5), 25: (0.2, 0.3),
          26: (0.05, 0.3), 27: (0.6, 0.6), 28: (0.6, 0.6), 29: (0.7, 0.8), 30: (-0.5, 0.4), 31: (-0.5, 0.4),
          32: (0.3, 0.8), 33: (0.3, 0.8), 34: (0.2, 0.3), 35: (1.2, 0.0), 36: (1.0, 0.0), 37: (2.0, 0.0), 38: (2.0, 0.0),
          39: (2.0, 0.0)}
# (forest 4 is 0.7/0.8; birch forests 0.6/0.6; plains 0.8/0.4; mega spruce taiga mutated 160: 0.25/0.8)


def biome_colors(jar):
    grass = jar.image('colormap/grass').convert('RGB')
    foliage = jar.image('colormap/foliage').convert('RGB')

    def look(img, t, r):
        t = min(max(t, 0.0), 1.0)
        r = min(max(r, 0.0), 1.0) * t
        return img.getpixel((int((1 - t) * 255), int((1 - r) * 255)))

    g, f, w = [], [], []
    for i in range(256):
        base = i & 127 if i >= 128 else i
        t, r = BIOMES.get(base, (0.5, 0.5))
        if i == 160 or i == 161:
            t, r = 0.25, 0.8
        gc, fc = look(grass, t, r), look(foliage, t, r)
        if base == 6:   # swamp: fixed colours
            gc, fc = (0x6A, 0x70, 0x39), (0x6A, 0x70, 0x39)
        if base in (37, 38, 39):   # mesa
            gc, fc = (0x90, 0x81, 0x4D), (0x9E, 0x81, 0x4D)
        if base == 29:   # roofed forest: grass halfway to a dark green
            c = (gc[0] << 16) | (gc[1] << 8) | gc[2]
            c = ((c & 0xFEFEFE) + 0x28340A) >> 1
            gc = ((c >> 16) & 255, (c >> 8) & 255, c & 255)
        wc = (0xE0, 0xFF, 0xAE) if base == 6 else (0xFF, 0xFF, 0xFF)
        g.append(gc)
        f.append(fc)
        w.append(wc)
    return g, f, w


# GUI pieces: name, file, x, y, w, h
SPRITES = [
    ('hotbar', 'gui/widgets', 0, 0, 182, 22),
    ('hotbar_sel', 'gui/widgets', 0, 22, 24, 24),
    ('crosshair', 'gui/icons', 0, 0, 16, 16),
    ('heart_bg', 'gui/icons', 16, 0, 9, 9),
    ('heart', 'gui/icons', 52, 0, 9, 9),
    ('heart_half', 'gui/icons', 61, 0, 9, 9),
    ('food_bg', 'gui/icons', 16, 27, 9, 9),
    ('food', 'gui/icons', 52, 27, 9, 9),
    ('food_half', 'gui/icons', 61, 27, 9, 9),
    ('armor_bg', 'gui/icons', 16, 9, 9, 9),
    ('xp_bg', 'gui/icons', 0, 64, 182, 5),
    ('xp', 'gui/icons', 0, 69, 182, 5),
    ('bubble', 'gui/icons', 16, 18, 9, 9),
]


def iso_icon(jar, s, names_px):
    """A block as the inventory shows it: three faces, the top lit, the left 0.8, the right 0.6 (16 x 16)."""
    t = s['tex']
    if s['model'] in ('cross', 'flat', 'torch', 'vine', 'ladder', 'door', 'pane'):
        n = t if isinstance(t, str) else t.get('side')
        im = first_frame(jar.image('blocks/' + n))
        return im
    top = t if isinstance(t, str) else t['top']
    left = t if isinstance(t, str) else t.get('front', t['side']) if s['meta'] in (0,) else t['side']
    right = t if isinstance(t, str) else t['side']
    out = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    timg = first_frame(jar.image('blocks/' + top))
    limg = first_frame(jar.image('blocks/' + (t.get('front', t['side']) if isinstance(t, dict) and 'front' in t else left)))
    rimg = first_frame(jar.image('blocks/' + right))
    tint = s['tint']
    def tinted(im, shade, tint_on):
        px = []
        for r, g, b, a in pixels(im):
            if tint_on:
                r, g, b = r * 0x91 // 255, g * 0xBD // 255, b * 0x59 // 255   # plains grass/foliage
            px.append((int(r * shade), int(g * shade), int(b * shade), a))
        o = Image.new('RGBA', im.size)
        o.putdata(px)
        return o
    timg = tinted(timg, 1.0, tint in ('grass', 'foliage'))
    limg = tinted(limg, 0.8, tint == 'foliage')
    rimg = tinted(rimg, 0.6, tint == 'foliage')
    h = 0.5 if s['model'] == 'slab' else 1.0
    for y in range(16):
        for x in range(16):
            px, py = x + 0.5, y + 0.5
            # top face: (8,0) (16,4) (8,8) (0,4); u along (8,0)->(16,4), v along (8,0)->(0,4)
            oy = (1 - h) * 8
            a = (px - 8) / 8 + (py - oy) / 4
            b = -(px - 8) / 8 + (py - oy) / 4
            if 0 <= a < 1 and 0 <= b < 1:
                c = timg.getpixel((min(15, int(a * 16)), min(15, int(b * 16))))
                if c[3] >= 128:
                    out.putpixel((x, y), c)
                continue
            # left face: (0,4) (8,8) down 8: u = x/8, v = (y - 4 - x/2) / 8
            if px < 8:
                u, v = px / 8, (py - 4 - oy - px / 2) / (8 * h)
                if 0 <= u < 1 and 0 <= v < 1:
                    c = limg.getpixel((min(15, int(u * 16)), min(15, int(((1 - h) + v * h) * 16))))
                    if c[3] >= 128:
                        out.putpixel((x, y), c)
            else:
                u, v = (px - 8) / 8, (py - 8 - oy + (px - 8) / 2) / (8 * h)
                if 0 <= u < 1 and 0 <= v < 1:
                    c = rimg.getpixel((min(15, int(u * 16)), min(15, int(((1 - h) + v * h) * 16))))
                    if c[3] >= 128:
                        out.putpixel((x, y), c)
    return out


def pack_sprite(im):
    idx, pal, alpha = quantize(im, 16)
    if not alpha:   # keep index 0 free: a sprite is always drawn with 0 as see-through
        idx, pal, _ = quantize(im, 15)
        idx = [i + 1 for i in idx]
        pal = [(0, 0, 0)] + pal
    while len(pal) < 16:
        pal.append((0, 0, 0))
    w, h = im.size
    bs = []
    for y in range(h):
        row = idx[y * w:(y + 1) * w] + [0]
        bs += [row[i] | (row[i + 1] << 4) for i in range(0, w, 2)]
    return bs, pal, alpha


def main():
    jar = Jar(sys.argv[1])
    names, px, pals, flags = pack_textures(jar)
    out = ['/* Generated by tools/pack.py from Minecraft 1.8.8\'s client.jar: textures and block tables. */',
           '#include "data.h"', '']
    out.append(f'const uint8_t tex_px[{len(names)}][128] = {{')
    for p in px:
        bs = [p[i] | (p[i + 1] << 4) for i in range(0, 256, 2)]
        out.append('  {' + ','.join(str(v) for v in bs) + '},')
    out.append('};')
    out.append(f'const uint16_t tex_pal[{len(names)}][16] = {{')
    for pal in pals:
        out.append('  {' + ','.join(f'0x{rgb565(*c):04X}' for c in pal) + '},')
    out.append('};')
    out.append(f'const uint8_t tex_flags[{len(names)}] = {{' + ','.join(str(f) for f in flags) + '};')
    out.append('')
    out.append(f'const uint8_t blk_tex[B_COUNT][6] = {{')
    for s in blocks.S:
        out.append('  {' + ','.join(str(v) for v in faces(s, names)) + '},   /* ' + s['name'] + ' */')
    out.append('};')
    models = blocks.MODELS
    out.append('const uint8_t blk_model[B_COUNT] = {' + ','.join(str(models.index(s['model'])) for s in blocks.S) + '};')
    out.append('const uint8_t blk_light[B_COUNT] = {' + ','.join(str(s['light']) for s in blocks.S) + '};')
    fl = []
    for s in blocks.S:
        f = TINT[s['tint']]
        if s['model'] in ('cube',):
            f |= BF_OPAQUE
        if s['model'] in ('cube', 'leaves', 'glass', 'slab', 'cactus', 'fence', 'pane', 'door', 'layer'):
            f |= BF_SOLID
        fl.append(f)
    out.append('const uint8_t blk_flags[B_COUNT] = {' + ','.join(str(v) for v in fl) + '};')
    out.append('const uint8_t blk_tool[B_COUNT] = {' + ','.join(str(blocks.TOOLS.index(s['tool'])) for s in blocks.S) +
               '};')
    out.append('const uint8_t blk_level[B_COUNT] = {' + ','.join(str(s['level']) for s in blocks.S) + '};')
    # hardness in twentieths (255: unbreakable)
    out.append('const uint8_t blk_hard[B_COUNT] = {' +
               ','.join(str(255 if s['hard'] < 0 or s['hard'] >= 12.7 else int(round(s['hard'] * 20)))
                        for s in blocks.S) + '};')
    out.append('const uint8_t blk_id[B_COUNT] = {' + ','.join(str(s['id']) for s in blocks.S) + '};')
    out.append('const uint8_t blk_meta[B_COUNT] = {' + ','.join(str(s['meta']) for s in blocks.S) + '};')
    # GUI sprites and block icons: 4 bits a pixel, a palette each; index 0 see-through
    sprites = [(n, jar.image(f).crop((x, y, x + w, y + h))) for n, f, x, y, w, h in SPRITES]
    for st in blocks.S:
        if st['tex'] is not None:
            sprites.append(('icon_' + st['name'].lower(), iso_icon(jar, st, None)))
    data, offs, pals, dims, flags2 = [], [], [], [], []
    for n, im in sprites:
        bs, pal, alpha = pack_sprite(im)
        offs.append(len(data))
        data += bs
        pals.append(pal)
        dims.append(im.size)
        flags2.append(1 if alpha else 0)
    out.append(f'const uint8_t spr_px[{len(data)}] = {{' + ','.join(str(v) for v in data) + '};')
    out.append(f'const uint32_t spr_off[{len(sprites)}] = {{' + ','.join(str(v) for v in offs) + '};')
    out.append(f'const uint8_t spr_w[{len(sprites)}] = {{' + ','.join(str(w) for w, h in dims) + '};')
    out.append(f'const uint8_t spr_h[{len(sprites)}] = {{' + ','.join(str(h) for w, h in dims) + '};')
    out.append(f'const uint8_t spr_alpha[{len(sprites)}] = {{' + ','.join(str(v) for v in flags2) + '};')
    out.append(f'const uint16_t spr_pal[{len(sprites)}][16] = {{')
    for pal in pals:
        out.append('  {' + ','.join(f'0x{rgb565(*c):04X}' for c in pal) + '},')
    out.append('};')
    icon_of = []
    k = len(SPRITES)
    for st in blocks.S:
        if st['tex'] is not None:
            icon_of.append(k)
            k += 1
        else:
            icon_of.append(0xFFFF)
    out.append('const uint16_t blk_icon[B_COUNT] = {' + ','.join(str(v) for v in icon_of) + '};')
    # Minecraft's clouds: 256 x 256, 1 bit a cell
    cl = jar.image('environment/clouds')
    bits = bytearray(256 * 256 // 8)
    for i, (r, g, b, a) in enumerate(pixels(cl)):
        if a >= 128:
            bits[i >> 3] |= 1 << (i & 7)
    out.append('const uint8_t clouds[8192] = {' + ','.join(str(v) for v in bits) + '};')
    g, f, w = biome_colors(jar)
    out.append('const uint16_t biome_grass[256] = {' + ','.join(f'0x{rgb565(*c):04X}' for c in g) + '};')
    out.append('const uint16_t biome_foliage[256] = {' + ','.join(f'0x{rgb565(*c):04X}' for c in f) + '};')
    out.append('const uint16_t biome_water[256] = {' + ','.join(f'0x{rgb565(*c):04X}' for c in w) + '};')
    temps = [BIOMES.get(i & 127 if i >= 128 else i, (0.5, 0.5))[0] for i in range(256)]
    out.append('const int8_t biome_temp[256] = {' + ','.join(str(int(round(t * 50))) for t in temps) + '};   /* x 50 */')
    open(os.path.join(ROOT, 'src', 'data.c'), 'w').write('\n'.join(out) + '\n')

    h = ['/* Generated by tools/pack.py: textures and block tables (see data.c). */', '#ifndef NB_DATA_H',
         '#define NB_DATA_H', '#include <stdint.h>', '#include "blocks.h"', '',
         f'#define NTEX {len(names)}',
         '/* tex_flags: bits 0-4, the first palette index tinted by the biome (16: none); 0x20: has see-through texels */',
         'extern const uint8_t tex_px[NTEX][128];', 'extern const uint16_t tex_pal[NTEX][16];',
         'extern const uint8_t tex_flags[NTEX];', '',
         '/* faces: -Y +Y -Z +Z -X +X */', 'extern const uint8_t blk_tex[B_COUNT][6];',
         'extern const uint8_t blk_model[B_COUNT], blk_light[B_COUNT], blk_flags[B_COUNT], blk_tool[B_COUNT];',
         'extern const uint8_t blk_level[B_COUNT], blk_hard[B_COUNT], blk_id[B_COUNT], blk_meta[B_COUNT];',
         '/* blk_flags: bits 0-2 the tint (1 grass, 2 foliage, 3 spruce, 4 birch, 5 water, 6 lily pad) */',
         'extern const uint16_t biome_grass[256], biome_foliage[256], biome_water[256];',
         'extern const int8_t biome_temp[256];   /* temperature x 50 */',
         f'#define BF_OPAQUE {BF_OPAQUE}', f'#define BF_SOLID {BF_SOLID}', '#define BF_TINT 7', '']
    for i, n in enumerate(names):
        h.append(f'#define TX_{n.upper()} {i}')
    h += ['', 'extern const uint8_t spr_px[], spr_w[], spr_h[], spr_alpha[];', 'extern const uint32_t spr_off[];',
          'extern const uint16_t spr_pal[][16];', 'extern const uint16_t blk_icon[B_COUNT];   /* sprite of its icon */']
    for i, sp in enumerate(SPRITES):
        h.append(f'#define SP_{sp[0].upper()} {i}')
    h += ['', '#endif', '']
    open(os.path.join(ROOT, 'src', 'data.h'), 'w').write('\n'.join(h))
    print(f'{len(names)} textures, {len(blocks.S)} block states')


BF_OPAQUE = 8
BF_SOLID = 16

if __name__ == '__main__':
    main()
