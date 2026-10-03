"""Hollow Knight for the NumWorks calculator: makes src/data.bin (and src/data.h) from the game's files.
HOLLOWKNIGHT=<the game's Data folder> python3 pack.py"""
import os, sys, struct, math, lzma, time, multiprocessing as mp
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import unity, scene, visible, art, actors, coll, ents

SRC = os.path.join(HERE, "..", "src")
ROOMS = [l.strip() for l in open(os.path.join(HERE, "rooms.txt")) if l.strip() and not l.startswith("#")]
STRINGS = ents.Strings()
PERSIST = ents.Persist()
SPRITES = ents.Sprites()
ROOM_DATA = {}   # name -> (coll.room's, ents.room's)


def room_data(name):
    """A room's ground and objects (once: the objects' sprites go into SPRITES)."""
    if name not in ROOM_DATA:
        d = unity.scene(name)
        st = scene.settings(d)
        w, h = st["size"] if st["size"] else (40, 24)
        cr = coll.room(d, w, h)
        ROOM_DATA[name] = (cr, ents.room(d, ROOMS, STRINGS, PERSIST, name, SPRITES, cr[3]))
    return ROOM_DATA[name]

SECTIONS = ["TEX", "TMAP", "PAL", "BIDX", "BLK", "ROOMS", "RBLOB", "PRIOR", "SOFT", "SPR", "CLIP", "STR"]
BLENDS = {"alpha": 0, "add": 1, "screen": 2, "linearlight": 3, "overlay": 4, "multiply": 5}
F_LIT, F_ROT, F_SOLID, F_DYN, F_GRASS = 8, 16, 32, 64, 128

TC_N0 = 1   # (src/tilecode.h)
LZ = [{"id": lzma.FILTER_LZMA1, "lc": 0, "lp": 0, "pb": 0, "dict_size": 1 << 16, "preset": 9 | lzma.PRESET_EXTREME}]


def lz(b):
    return lzma.compress(b, format=lzma.FORMAT_RAW, filters=LZ)


def f16(x):
    return int(np.array(x, np.float16).view(np.uint16))


def i16(x):
    return max(-32768, min(32767, int(round(x))))


def f16s(x):
    """A half float's bits as a signed 16-bit value (for struct 'h')."""
    v = f16(x)
    return v - 65536 if v >= 32768 else v


class Blob:
    def __init__(self):
        self.b = bytearray()

    def align(self, n=4):
        while len(self.b) % n:
            self.b.append(0)

    def add(self, data):
        self.align()
        o = len(self.b)
        self.b += data
        return o


SECTOR = 16.0        # sectors along the room's long camera axis (units)
GLOBAL_SPAN = 40.0   # instances seen over a longer stretch of camera positions are always kept
MARGIN = 2.0


def inst_record(it, tex_id, tints):
    """An instance: where, its texture, how it is drawn."""
    q = it.quad      # bl, br, tr, tl (world)
    tl, tr, bl = q[3], q[2], q[0]
    flags = BLENDS.get(it.blend, 0)
    if it.lit:
        flags |= F_LIT
    if it.group:
        flags |= F_DYN   # (in a render group: a byte more)
    if it.grass:
        flags |= F_GRASS
    color = it.color
    if it.solid and it.sprite.name == "black_solid":
        color = (0, 0, 0, color[3])
    col = tuple(int(round(min(1, max(0, c)) * 255)) for c in color)
    if col not in tints:
        tints.append(col)
    tint = min(255, tints.index(col))
    rot = 0
    if it.solid:
        flags |= F_SOLID
        tex = 0xFFFF
        ax, ay = min(p[0] for p in q), max(p[1] for p in q)
        a, b = max(p[0] for p in q) - ax, -(ay - min(p[1] for p in q))
    else:
        t = tex_id[id(it.variant)]
        tex, tw, th = t
        eu = (tr - tl) / tw
        ev = (bl - tl) / th
        ax, ay = tl[0], tl[1]
        a = math.hypot(eu[0], eu[1])
        ang = math.atan2(eu[1], eu[0])
        # flips: keep the angle small, put the sign in a
        if abs(ang) > math.pi / 2:
            ang = ang - math.pi if ang > 0 else ang + math.pi
            a = -a
        c, s_ = math.cos(ang), math.sin(ang)
        b = ev[0] * -s_ + ev[1] * c        # along (-sin, cos)
        rot = int(round(ang / (2 * math.pi) * 65536))
        if rot:
            flags |= F_ROT
    return (ax, ay, it.z, tex, tint, flags, a, b, rot, it.group)


def uvar(u):
    out = bytearray()
    while u >= 128:
        out.append(u & 127 | 128)
        u >>= 7
    out.append(u)
    return out


def svar(v):
    return uvar(v << 1 if v >= 0 else ((-v) << 1) - 1)


def sector_stream(recs):
    """A sector's instances in draw order, each relative to the one before (read in place by room.c):
    flags, aux (1 tint, 2 same texture, 4 same z, 8|16 b: 0 its own, 1 = a, 2 = -a, 3 same as before, 32 same a,
    64 next rank, 128 another sorting layer or order), [rank step - 2], [layer, order], [texture step], [tint],
    x step, y step, [z step], [a], [b], [angle], [render group (F_DYN)]."""
    out = bytearray()
    pr, pt, px, py, pz, pa, pb, pg = -1, 0, 0, 0, 0, 0, 0, None
    for rank, (ax, ay, z, tex, tint, flags, a, b, rot, rgroup), group in recs:
        X, Y, Z, A, B = i16(ax * 64), i16(ay * 64), i16(z * 128), f16s(a) & 0xFFFF, f16s(b) & 0xFFFF
        aux = 0
        if tint:
            aux |= 1
        if tex == pt:
            aux |= 2
        if Z == pz:
            aux |= 4
        bm = 1 if B == A else 2 if B == A ^ 0x8000 else 3 if B == pb else 0
        aux |= bm << 3
        if A == pa:
            aux |= 32
        if rank == pr + 1:
            aux |= 64
        if group != pg:
            aux |= 128
        out += bytes([flags, aux])
        if not aux & 64:
            out += uvar(rank - pr - 2)
        if aux & 128:
            out += bytes([group[0]]) + svar(group[1])
        if not aux & 2:
            out += svar(tex - pt)
        if tint:
            out.append(tint)
        out += svar(X - px) + svar(Y - py)
        if not aux & 4:
            out += svar(Z - pz)
        if not aux & 32:
            out += struct.pack("<H", A)
        if bm == 0:
            out += struct.pack("<H", B)
        if flags & F_ROT:
            out += struct.pack("<h", rot)
        if flags & F_DYN:
            out.append(rgroup)
        pr, pt, px, py, pz, pa, pb, pg = rank, tex, X, Y, Z, A, B, group
    return bytes(out)


def room_blobs(name, keep, st, tex_id):
    """A room: its header (kept in RAM) and its instances in sectors (decoded when the camera is near)."""
    w, h = st["size"] if st["size"] else (40, 24)
    cx0, cx1, cy0, cy1 = scene.camera_range(st) if st["size"] else (20, 20, 12, 12)
    axis = 0 if (cx1 - cx0) >= (cy1 - cy0) else 1
    lo, hi = (cx0, cx1) if axis == 0 else (cy0, cy1)
    HWU, HHU = scene.VIEW_W / 2 / scene.FOCAL, scene.VIEW_H / 2 / scene.FOCAL
    (solid, segs, cols, _), (erecs, groups) = room_data(name)
    for it in keep:
        it.group = groups.get(it.obj["id"], 0)
    tints = [(255, 255, 255, 255)]
    secs = {}
    for rank, it in enumerate(keep):
        rec = inst_record(it, tex_id, tints)
        q = np.array(it.quad)
        dist = max(0.5, it.z - scene.CAMZ)
        half = (HWU if axis == 0 else HHU) * dist
        p0, p1 = (q[:, 0].min(), q[:, 0].max()) if axis == 0 else (q[:, 1].min(), q[:, 1].max())
        va, vb = max(lo, p0 - half - MARGIN), min(hi, p1 + half + MARGIN)
        if it.dynamic or vb - va > GLOBAL_SPAN or vb < va:
            sec = -1
        else:
            sec = int((va - lo) // SECTOR)
        e = secs.setdefault(sec, [[], 1e9, -1e9])
        e[0].append((rank, rec, (it.layer, it.order)))
        e[1] = min(e[1], va)
        e[2] = max(e[2], vb)
    order = sorted(secs.keys())
    luts, sat = scene.grading_luts(st, 64)
    lut = bytes(int(round(luts[c][i] * 255)) for c in range(3) for i in range(64))
    bz = st["blur_z"] if st["blur_z"] is not None else 1000.0
    blobs = []
    table = bytearray()
    for k in order:
        recs, va, vb = secs[k]
        blobs.append((sector_stream(recs), len(recs)))
        if k < 0:
            va, vb = -1e9, 1e9
        table += struct.pack("<IIHHff", 0, 0, len(recs), 0, va, vb)   # (offsets filled in by main)
    HDR = 84 + 192
    gw, gh, cells = coll.grid(segs, w, h)
    hdr = struct.pack("<4H4f4f4fff6HII", 6, len(order), len(tints), axis, w, h, bz, sat, *st["ambient"], 0.0,
                      *st["hero_light"], lo, hi, len(keep), len(segs), len(cols), gw, gh, len(erecs), 0, 0) + lut
    assert len(hdr) == HDR, len(hdr)
    body = bytearray(hdr)
    body += b"".join(struct.pack("<4B", *t) for t in tints)
    while len(body) % 4:
        body.append(0)
    o_sec = len(body)
    body += table
    # the ground (read in place): colliders' flags (0: the tilemap's), the tilemap's solid tiles (1 bit each, rows from
    # the bottom), segments (1/128 unit), their colliders, cells' first entries, entries
    cb = bytearray(bytes(cols))
    if len(cb) % 2:
        cb.append(0)
    th, tw = solid.shape
    cb += struct.pack("<HH", tw, th)
    rowb = (tw + 7) // 8
    for y in range(th):
        row = bytearray(rowb)
        for x in range(tw):
            if solid[y, x]:
                row[x >> 3] |= 1 << (x & 7)
        cb += row
    if len(cb) % 2:
        cb.append(0)
    for x0, y0, x1, y1, ci in segs:
        cb += struct.pack("<4h", *(i16(c * coll.UNIT) for c in (x0, y0, x1, y1)))
    cb += bytes(ci for x0, y0, x1, y1, ci in segs)
    if len(cb) % 2:
        cb.append(0)
    first = 0
    for c in cells:
        cb += struct.pack("<H", first)
        first += len(c)
    cb += struct.pack("<H", first)
    for c in cells:
        cb += struct.pack("<%dH" % len(c), *c)
    return body, o_sec, blobs, bytes(cb), b"".join(erecs)


def tilecode(blocks):
    """Trains the tile coder's starting probabilities on every block and encodes them (tools/tilecode.c)."""
    import subprocess, tempfile
    tmp = tempfile.mkdtemp()
    exe = os.path.join(tmp, "tilecode")
    subprocess.check_call(["cc", "-O2", "-o", exe, os.path.join(HERE, "tilecode.c")])
    job = bytearray(struct.pack("<I", len(blocks)))
    for bits, tiles in blocks:
        job += struct.pack("<BBH", bits, 0, len(tiles)) + b"".join(tiles)
    open(os.path.join(tmp, "in.bin"), "wb").write(job)
    subprocess.check_call([exe, "train", os.path.join(tmp, "in.bin"), os.path.join(tmp, "prior.bin")])
    subprocess.check_call([exe, "encode", os.path.join(tmp, "in.bin"), os.path.join(tmp, "prior.bin"), os.path.join(tmp, "out.bin")])
    prior = open(os.path.join(tmp, "prior.bin"), "rb").read()
    out = open(os.path.join(tmp, "out.bin"), "rb").read()
    res, p = [], 0
    for _ in blocks:
        n = struct.unpack_from("<I", out, p)[0]
        res.append(out[p + 4:p + 4 + n])
        p += 4 + n
    return prior, res


def main():
    t0 = time.time()
    fp = os.path.join(HERE, "fit.json")
    if os.path.exists(fp):
        import json
        art.FIT.update(json.load(open(fp)))
    variants, per_room = art.build_variants(ROOMS, visible.compute)
    print("variants", len(variants), "%.0fs" % (time.time() - t0), flush=True)
    jobs = [art.Job(v) for v in variants]
    sprites, clips = actors.build()
    # the rooms' objects, and the sprites they show (after the actors')
    SPRITES.base = len(sprites)
    named = actors.named_sprites(SPRITES)
    for r in ROOMS:
        room_data(r)
    sprites += actors.unity_sprites(SPRITES.list)
    print("actor frames", len(sprites), "clips", len(clips), "%.0fs" % (time.time() - t0), flush=True)
    jobs += [sp["job"] for sp in sprites]
    with mp.get_context("spawn").Pool(min(4, os.cpu_count() or 1)) as pool:
        texs = pool.map(art.encode_job, jobs, chunksize=4)
    # (the actors' textures come after the scenery's)
    variants = variants + [sp["job"] for sp in sprites]
    print("textures %.0fs" % (time.time() - t0), flush=True)
    blocks = []
    for t in texs:
        t.first_block = len(blocks)
        for i in range(0, t.ntiles, art.BLOCK_TILES):
            blocks.append((t.bits, t.tiles[i:i + art.BLOCK_TILES]))
    prior, coded = tilecode(blocks)
    print("tiles coded %.0fs" % (time.time() - t0), flush=True)
    secs = {k: Blob() for k in SECTIONS}
    # the starting probabilities as the game uses them: each with its count (tilecode.h: tc_pack)
    secs["PRIOR"].b += b"".join(struct.pack("<H", v << 4 | TC_N0) for v in struct.unpack("<%dH" % (len(prior) // 2), prior))
    tex_id = {}
    nblk = 0
    texrec = bytearray()
    for i, (v, t) in enumerate(zip(variants, texs)):
        tex_id[id(v)] = (i, t.w, t.h)
        pal_off = 0xFFFFFFFF
        if t.pal is not None:
            pal_off = secs["PAL"].add(t.pal.tobytes())
        if t.fmt == art.FMT_SOFTA:   # (alpha only: read in place)
            pal_off = secs["SOFT"].add(t.soft)
            texrec += struct.pack("<HHBBBBIII", t.w, t.h, 0, 0, t.fmt, 0, pal_off, t.base, 0)
            continue
        if t.fmt == art.FMT_SOFT:
            c = lz(t.soft)
            pal_off = secs["SOFT"].add(struct.pack("<I", len(c)) + c)
            texrec += struct.pack("<HHBBBBIII", t.w, t.h, 0, 0, t.fmt, 0, pal_off, t.base, 0)
            continue
        # tile map: per row, the rank of its first tile, then 2 bits a tile (0 empty, 1 tile, 3 opaque tile)
        m = bytearray()
        rank = 0
        for ty in range(t.th):
            m += struct.pack("<H", rank)
            row = bytearray((t.tw + 3) // 4)
            for tx in range(t.tw):
                code = 0 if t.empty[ty, tx] else (3 if t.opaque[ty, tx] else 1)
                row[tx >> 2] |= code << ((tx & 3) * 2)
                rank += code != 0
            m += row
        map_off = secs["TMAP"].add(bytes(m))
        bidx = secs["BIDX"].b
        first = len(bidx) // 4
        for bl in coded[t.first_block:t.first_block + (t.ntiles + art.BLOCK_TILES - 1) // art.BLOCK_TILES]:
            bidx += struct.pack("<I", len(secs["BLK"].b))
            secs["BLK"].b += bl
        bidx += struct.pack("<I", len(secs["BLK"].b))
        texrec += struct.pack("<HHBBBBIII", t.w, t.h, t.tw, t.th, t.fmt, 1 if v.blur else 0, pal_off, map_off, first)
    secs["TEX"].b += struct.pack("<I", len(variants)) + texrec
    # actors: each sprite frame's texture and where its top-left corner is (local units), units a texel; the clips
    first_actor = len(variants) - len(sprites)
    for i, sp in enumerate(sprites):
        secs["SPR"].b += struct.pack("<HHffff", first_actor + i, 0, sp["lx"], sp["ty"], sp["tu"], sp["tv"])
    secs["CLIP"].b += struct.pack("<I", len(clips))
    frames = []
    for c in clips:
        secs["CLIP"].b += struct.pack("<HHfBBH", len(frames), len(c["frames"]), c["fps"], c["wrap"], c["loop"], 0)
        frames += [sid | (0x8000 if trig else 0) for sid, trig in c["frames"]]
    secs["CLIP"].b += struct.pack("<%dH" % len(frames), *frames)
    with open(os.path.join(unity.CACHE, "texnames.txt"), "w") as f:
        for i, (v, t) in enumerate(zip(variants, texs)):
            name = v.name if isinstance(v, art.ImageJob) else v.sprite.name
            f.write("%d %s %.3f %d %d %dx%d %d\n" % (i, name.replace(" ", "_"), getattr(v, "scale", 1.0), v.blur, v.alpha_only,
                                                      t.w, t.h, t.ntiles))
    rooms = bytearray(struct.pack("<I", len(ROOMS)))
    for r in ROOMS:
        keep, st = per_room[r]
        body, o_sec, blobs, ground, recs = room_blobs(r, keep, st, tex_id)
        struct.pack_into("<I", body, 76, secs["RBLOB"].add(ground))
        struct.pack_into("<I", body, 80, secs["RBLOB"].add(recs))
        packed = 0
        for i, (c, n) in enumerate(blobs):
            off = secs["RBLOB"].add(c)
            struct.pack_into("<II", body, o_sec + 20 * i, off, len(c))
            packed += len(c)
        hc = lz(bytes(body))
        off = secs["RBLOB"].add(hc)
        assert len(r) < 32, r
        rooms += struct.pack("<32sIII", r.encode(), off, len(hc), len(body))
        print("room %-26s %5d instances in %2d sectors, %6d packed" % (r, len(keep), len(blobs), packed + len(hc)), flush=True)
    secs["ROOMS"].b += rooms
    secs["STR"].b += STRINGS.blob()
    # the file: "HKNW", count, then (offset, size) per section
    head = struct.pack("<4sI", b"HKNW", len(SECTIONS))
    pos = len(head) + 8 * len(SECTIONS)
    table = bytearray()
    body = bytearray()
    for k in SECTIONS:
        while (pos + len(body)) % 4:
            body.append(0)
        table += struct.pack("<II", pos + len(body), len(secs[k].b))
        body += secs[k].b
    data = head + table + body
    open(os.path.join(SRC, "data.bin"), "wb").write(data)
    sizes = {k: len(secs[k].b) for k in SECTIONS}
    print("data.bin", len(data), sizes, "%.0fs" % (time.time() - t0))
    with open(os.path.join(SRC, "data.h"), "w") as f:
        f.write("/* made by tools/pack.py */\n#pragma once\n")
        for i, k in enumerate(SECTIONS):
            f.write("#define SEC_%s %d\n" % (k, i))
        f.write("#define NUM_ROOMS %d\n" % len(ROOMS))
        for i, r in enumerate(ROOMS):
            f.write("#define ROOM_%s %d\n" % (r.upper(), i))
        for i, c in enumerate(clips):
            f.write("#define %s %d\n" % (c["id"], i))
        for k, v in named.items():
            f.write("#define SPRITE_%s %d\n" % (k, v))


if __name__ == "__main__":
    main()
