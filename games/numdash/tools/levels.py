#!/usr/bin/env python3
"""Compile the checked-in GMD level exports to compressed C data.

Objects are cut into chunks along x; each chunk is one raw DEFLATE stream of
column-coded records (type, flags, position, then the rarer properties). The
runtime keeps only the chunks near the camera inflated in RAM, so levels of
20 000 objects play in a window of a few thousand. Colour, enter-effect and
trail triggers become a small event list. Also emits the C object catalogue.

Levels 1-9 keep their first-release data exactly (whole-unit positions, file
order) as 6-byte rows {dx, y, type, xform} so their replays stay valid.

Record columns (after the record count), one entry per record, sorted by type:
  T  zigzag(type - previous type)
  P  props: 1 rotation, 2 flips, 4 scale, 8 paint, 16 row run, 32 column run,
     64 aligned (x and y are multiples of 7.5 units)
  X  zigzag x delta, Y zigzag y delta (1/32 units, or 7.5 units if aligned);
     the base is reset to (chunk x, 0) when the type changes
  R  rotation (1024 steps), F flip bits, S scale index, Q paint index,
  N  run length - 2 and run step in 7.5 unit steps
"""
import base64
import collections
import gzip
import hashlib
import json
import math
import struct
import sys
import zlib
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import objdefs as od  # noqa: E402

GD3DS = 'https://github.com/AleFunky/gd3ds/tree/cdea1c2fad3d67fa17d2cfa781013b0ee58bb280/romfs/main_levels'
GMDKIT = 'https://github.com/UHDanke/gmdkit/tree/ec79468588c6beb46712fbdc142bb06fa9a1d9c5/data/txt/object_string/official/main'
# file, title, difficulty (1 easy .. 5 insane, 6 demon), stars, pulse bpm, source
LEVELS = [
    ('StereoMadness', 'STEREO MADNESS', 1, 1, 160, GD3DS),
    ('BackOnTrack', 'BACK ON TRACK', 1, 2, 130, GD3DS),
    ('Polargeist', 'POLARGEIST', 2, 3, 165, GD3DS),
    ('DryOut', 'DRY OUT', 2, 4, 140, GD3DS),
    ('BaseAfterBase', 'BASE AFTER BASE', 3, 5, 142, GD3DS),
    ('CantLetGo', "CAN'T LET GO", 3, 6, 144, GD3DS),
    ('Jumper', 'JUMPER', 4, 7, 175, GD3DS),
    ('TimeMachine', 'TIME MACHINE', 4, 8, 143, GD3DS),
    ('Cycles', 'CYCLES', 4, 9, 128, GD3DS),
    ('Clubstep', 'CLUBSTEP', 6, 14, 128, GMDKIT + '/14.txt'),
]
LEGACY = 9          # levels compiled exactly as before (integer positions, x order)
CHUNK_OBJS = 360    # objects per chunk at most
CHUNK_LEGACY = 1000 # objects per legacy chunk
CHUNK_SPAN = 2400   # and at most this many units wide
Q = 32              # position units per GD unit


def decode(path):
    nodes = list(ET.parse(path).getroot()[0])
    props = {nodes[i].text: nodes[i + 1].text for i in range(0, len(nodes), 2)}
    enc = props['k4']
    raw = gzip.decompress(base64.urlsafe_b64decode(enc + '=' * (-len(enc) % 4))).decode('latin-1')
    parts = raw.split(';')
    header = dict(zip(parts[0].split(',')[::2], parts[0].split(',')[1::2]))
    colors = {}
    for ch in header.get('kS38', '').split('|'):
        if not ch:
            continue
        it = ch.split('_')
        d = dict(zip(it[::2], it[1::2]))
        colors[int(d['6'])] = (int(d.get('1', 255)), int(d.get('2', 255)), int(d.get('3', 255)))
    objs = []
    for p in parts[1:]:
        it = p.split(',')
        d = dict(zip(it[::2], it[1::2]))
        if '1' in d:
            objs.append(d)
    return header, colors, objs


def compile_legacy(path):
    """Levels 1-9: the same objects, positions and order as the first releases."""
    header, colors, raw = decode(path)
    objects, events, skipped = [], [], collections.Counter()
    for order, d in enumerate(raw):
        gid = int(d['1'])
        x = int(round(float(d.get('2', 0))))
        y = int(round(float(d.get('3', 0))))
        if gid in od.COLOR_TRIGGERS:
            chan = od.COLOR_TRIGGERS[gid]
            dur = int(round(float(d.get('10', 0)) * 1000))
            flags = (1 if d.get('11') == '1' else 0) | (2 if d.get('14') == '1' else 0) | (4 if d.get('17') == '1' else 0)
            events.append((x, y, od.EV_COLOR, chan, int(d.get('7', 255)), int(d.get('8', 255)), int(d.get('9', 255)), flags, dur, order))
            continue
        if gid in od.FADE_TRIGGERS:
            events.append((x, y, od.EV_FADE, od.FADE_TRIGGERS[gid], 0, 0, 0, 0, 0, order))
            continue
        if gid in od.TRAIL_TRIGGERS:
            events.append((x, y, od.EV_TRAIL, od.TRAIL_TRIGGERS[gid], 0, 0, 0, 0, 0, order))
            continue
        t = od.BY_ID.get(gid)
        if not t:
            skipped[gid] += 1
            continue
        rot = int(round(float(d.get('6', 0)) / 90)) % 4
        flips = (1 if d.get('4') == '1' else 0) | (2 if d.get('5') == '1' else 0)
        objects.append(dict(x=x * Q, y=y * Q, t=t, rot=rot * 256, flips=flips, scale=0, paint=0, order=order))
    objects.sort(key=lambda o: (o['x'], o['order']))
    # GD triggers fire in x order, ties broken by higher y first
    events.sort(key=lambda e: (e[0], -e[1], e[9]))
    return header, colors, objects, events, skipped


# Channels of 2.0+ levels are renumbered densely; the special ones first.
SPECIAL_CH = [1000, 1001, 1002, 1004, 1003, 1005, 1006, 1007, 1009, 1010, 1011, 1012, 1, 2, 3, 4]
CT_DEFAULT_CH = {'OBJ': 1004, 'P1ADD': 1005, 'P2ADD': 1006, 'LBG': 1007, 'BLACK': 1010, 'WHITE': 1011, 'GLOW': 1004}
LEGACY_CH = {29: 1000, 30: 1001, 104: 1002, 105: 1004, 221: 1, 717: 2, 718: 3, 743: 4, 744: 1003, 900: 1009, 915: 1002}
TRIG_IDS = {899, 901, 1006, 1007, 1049} | set(LEGACY_CH)
# trigger stream kinds (level.h)
TR_COLOR, TR_MOVE, TR_ALPHA, TR_TOGGLE, TR_PULSE, TR_FADE, TR_TRAIL = 1, 2, 3, 4, 5, 6, 7
# GD z-layer values (key 24) -> NumDash's z buckets (0 = the part's own layer)
ZLAYERS = {-5: 1, -3: 1, -1: 2, 1: 3, 3: 4, 5: 5, 7: 6, 9: 7, 11: 8}


def parse_hsv(v):
    """'h a s a v a sAdd a vAdd' -> (h deg, s, v, flags) or None when neutral."""
    if not v:
        return None
    p = v.split('a')
    try:
        h, sv, vv = float(p[0]), float(p[1]), float(p[2])
        sadd, vadd = p[3] == '1', p[4] == '1'
    except (ValueError, IndexError):
        return None
    if h == 0 and (sv == 0 if sadd else sv == 1) and (vv == 0 if vadd else vv == 1):
        return None
    return (int(round(h)), int(round(sv * 64)), int(round(vv * 64)), (8 if sadd else 0) | (16 if vadd else 0))


def svarint(v, out):
    varint(zz(int(v)), out)


class Level2:
    """Tables of a 2.0+ level: dense channels, styles, groups, triggers."""

    def __init__(self, header_raw):
        self.ch_ids = list(SPECIAL_CH)          # dense index -> GD channel id (or ('hsv', base, hsv))
        self.ch_init = {}                       # GD id -> dict from the header
        for ch in header_raw.get('kS38', '').split('|'):
            if not ch:
                continue
            it = ch.split('_')
            d = dict(zip(it[::2], it[1::2]))
            self.ch_init[int(d['6'])] = d
        self.styles = {(255, 255, 0, 0, 0, 0): 0}
        self.gsets = {(): 0}
        self.gset_data = [0]
        self.groups = {}                        # GD group id -> dense
        self.scales = {(1000, 1000): 0}

    def chan(self, gid):
        if gid is None or gid == 0:
            return 255
        if gid not in self.ch_ids:
            self.ch_ids.append(gid)
        return self.ch_ids.index(gid)

    def chan_hsv(self, base_dense, hsv):
        key = ('hsv', base_dense, hsv)
        if key not in self.ch_ids:
            self.ch_ids.append(key)
        return self.ch_ids.index(key)

    def group(self, g):
        return self.groups.setdefault(g, len(self.groups))

    def gset(self, gs):
        gs = tuple(sorted(set(gs)))
        if gs not in self.gsets:
            self.gsets[gs] = len(self.gset_data)
            self.gset_data.append(len(gs))
            self.gset_data += gs
        return self.gsets[gs]

    def style(self, main, detail, zl, flags, gs, arg):
        key = (main, detail, zl, flags, gs, arg)
        return self.styles.setdefault(key, len(self.styles))

    def scale(self, sx, sy):
        key = (int(round(sx * 1000)), int(round(sy * 1000)))
        return self.scales.setdefault(key, len(self.scales))

    def chan_rows(self):
        """LChan rows {r,g,b, flags, opacity, copy, h, s, v} for every dense channel."""
        rows = []
        for cid in self.ch_ids:
            if isinstance(cid, tuple):
                _, base, (h, sv, vv, fl) = cid
                rows.append((255, 255, 255, 64 | fl, 255, base, h, sv, vv))
                continue
            d = self.ch_init.get(cid, {})
            r, g, b = int(d.get('1', 255)), int(d.get('2', 255)), int(d.get('3', 255))
            if cid == 1000 and not d: r, g, b = 40, 125, 255
            if cid == 1001 and not d: r, g, b = 0, 102, 255
            if cid == 1010: r, g, b = 0, 0, 0
            pl = int(d.get('4', -1) or -1)
            fl = (1 if d.get('5') == '1' else 0) | (2 if pl == 1 else 4 if pl == 2 else 0)
            if cid in (1005, 1006):
                fl |= 1 | (2 if cid == 1005 else 4)
            op = int(round(float(d.get('7', 1) or 1) * 255))
            copy, h, sv, vv = 255, 0, 64, 64
            if d.get('9') and int(d['9']):
                copy = self.chan(int(d['9']))
                hsv = parse_hsv(d.get('10', ''))
                if hsv:
                    h, sv, vv, f2 = hsv
                    fl |= f2
                fl |= 64
            rows.append((r, g, b, fl, op, copy, h, sv, vv))
        return rows


def compile_new(path, od_extra=None):
    """Levels 10-12: exact positions (1/32 unit), any rotation, 2.0 features:
    colour channels, groups and group triggers."""
    header, colors, raw = decode(path)
    L2 = Level2(decode_header_raw(path))
    objects, skipped = [], collections.Counter()
    ptrig, otrig = [], []           # position triggers, touch triggers
    # groups used by triggers (only those need state at run time)
    used_groups = set()
    for d in raw:
        gid = int(d['1'])
        if gid in (901, 1007, 1049) or (gid == 1006 and d.get('52') == '1'):
            used_groups.add(int(d.get('51', 0) or 0))
    # x reach of groups that move sideways (for streaming)
    reach = collections.defaultdict(float)
    for d in raw:
        if int(d['1']) == 901:
            g = int(d.get('51', 0) or 0)
            reach[g] += abs(float(d.get('28', 0) or 0))
            if d.get('58') == '1':
                reach[g] += float(d.get('10', 0) or 0) * 470 + 60
    for order, d in enumerate(raw):
        gid = int(d['1'])
        x, y = float(d.get('2', 0)), float(d.get('3', 0))
        groups = [int(v) for v in d.get('57', '').split('.') if v]
        groups = [g for g in groups if g in used_groups]
        gs = L2.gset([L2.group(g) for g in groups]) if groups else 0
        touch = d.get('11') == '1'
        if gid in TRIG_IDS or gid in od.FADE_TRIGGERS or gid in od.TRAIL_TRIGGERS:
            t = compile_trigger(L2, gid, d)
            if t is None:
                skipped[gid] += 1
                continue
            rec = (x, y, order, gs, t)
            (otrig if touch else ptrig).append(rec)
            if touch:
                # the trigger fires when the player touches its 30 x 30 box
                st = L2.style(255, 255, 0, 0, gs, len(otrig) - 1)
                objects.append(dict(x=int(round(x * Q)), y=int(round(y * Q)), t=od.INDEX['TOUCH'], rot=0, flips=0,
                                    scale=0, paint=st, order=order, ext=0))
            continue
        t = od.BY_ID.get(gid)
        if not t:
            skipped[gid] += 1
            continue
        info = od.OBJECTS[t - 1]
        extra = info[8] if len(info) > 8 else {}
        rot = int(round(float(d.get('6', 0)) % 360 / 360 * 1024)) % 1024
        flips = (1 if d.get('4') == '1' else 0) | (2 if d.get('5') == '1' else 0)
        main = L2.chan(int(d.get('21', 0) or 0)) if d.get('21') else 255
        detail = L2.chan(int(d.get('22', 0) or 0)) if d.get('22') else 255
        if d.get('41') == '1':
            hsv = parse_hsv(d.get('43', ''))
            if hsv:
                main = L2.chan_hsv(main if main != 255 else L2.chan(extra.get('base', 1004)), hsv)
        if d.get('42') == '1':
            hsv = parse_hsv(d.get('44', ''))
            if hsv:
                detail = L2.chan_hsv(detail if detail != 255 else L2.chan(extra.get('detail', 1)), hsv)
        zl = ZLAYERS.get(int(float(d.get('24', 0) or 0)), 0)
        flags = (1 if d.get('64') == '1' else 0) | (2 if d.get('67') == '1' else 0) | (4 if d.get('135') == '1' else 0)
        arg = 0
        if gid == 747:
            arg = int(round(float(d.get('54', 0) or 0)))
        st = L2.style(main, detail, zl, flags, gs, arg)
        sc = float(d.get('32', 1) or 1)
        sx, sy = sc * float(d.get('128', 1) or 1), sc * float(d.get('129', 1) or 1)
        scale = L2.scale(sx, sy)
        ext = max([reach.get(g, 0) for g in groups] or [0])
        objects.append(dict(x=int(round(x * Q)), y=int(round(y * Q)), t=t, rot=rot, flips=flips, scale=scale,
                            paint=st, order=order, ext=ext, sx=max(abs(sx), abs(sy))))
    objects.sort(key=lambda o: (o['x'], o['order']))
    ptrig.sort(key=lambda e: (e[0], -e[1], e[2]))
    stream, touch_offs = bytearray(), []
    px = 0
    for (x, y, order, gs, t) in ptrig:
        xi = max(int(round(x)), px)
        varint(xi - px, stream)
        px = xi
        stream += encode_trigger(t, gs)
    stream.append(0)   # end of the position triggers: a zero kind after a zero dx
    stream.append(0)
    for (x, y, order, gs, t) in otrig:
        touch_offs.append(len(stream))
        stream += encode_trigger(t, gs)
    events = []
    return header, colors, objects, events, skipped, L2, bytes(stream), touch_offs, len(ptrig)


def compile_trigger(L2, gid, d):
    """-> dict describing one trigger, or None when NumDash drops it."""
    f = lambda k, dv=0.0: float(d.get(k, dv) or dv)
    i = lambda k, dv=0: int(float(d.get(k, dv) or dv))
    ms = lambda k: int(round(f(k) * 1000))
    if gid in od.FADE_TRIGGERS:
        return dict(kind=TR_FADE, arg=od.FADE_TRIGGERS[gid])
    if gid in od.TRAIL_TRIGGERS:
        return dict(kind=TR_TRAIL, arg=od.TRAIL_TRIGGERS[gid])
    if gid in LEGACY_CH or gid == 899:
        ch = LEGACY_CH.get(gid) or i('23', 1) or 1
        t = dict(kind=TR_COLOR, target=L2.chan(ch), rgb=(i('7', 255), i('8', 255), i('9', 255)), dur=ms('10'),
                 opacity=int(round(f('35', 1) * 255)), flags=0, copy=255, hsv=None)
        if d.get('17') == '1': t['flags'] |= 1
        if d.get('15') == '1': t['flags'] |= 2
        if d.get('16') == '1': t['flags'] |= 4
        if d.get('14') == '1': t['flags'] |= 8          # tint ground (legacy BG)
        if i('50'):
            t['copy'] = L2.chan(i('50'))
            t['hsv'] = parse_hsv(d.get('49', ''))
            t['flags'] |= 64
        return t
    if gid == 901:
        return dict(kind=TR_MOVE, target=L2.group(i('51')), dx=f('28'), dy=f('29'), dur=ms('10'), easing=i('30'),
                    rate=int(round(f('85', 2) * 10)), flags=(1 if d.get('58') == '1' else 0) | (2 if d.get('59') == '1' else 0))
    if gid == 1007:
        return dict(kind=TR_ALPHA, target=L2.group(i('51')), opacity=int(round(f('35', 1) * 255)), dur=ms('10'))
    if gid == 1049:
        return dict(kind=TR_TOGGLE, target=L2.group(i('51')), on=1 if d.get('56') == '1' else 0)
    if gid == 1006:
        grp = d.get('52') == '1'
        tgt = L2.group(i('51')) if grp else L2.chan(i('51'))
        t = dict(kind=TR_PULSE, target=tgt, rgb=(i('7', 255), i('8', 255), i('9', 255)), t_in=ms('45'), hold=ms('46'),
                 t_out=ms('47'), flags=(1 if grp else 0) | (2 if d.get('65') == '1' else 0) | (4 if d.get('66') == '1' else 0),
                 copy=255, hsv=None)
        if d.get('48') == '1':
            t['copy'] = L2.chan(i('50') or 1)
            t['hsv'] = parse_hsv(d.get('49', ''))
            t['flags'] |= 64
        return t
    return None


def encode_trigger(t, gs):
    """kind (bit 7: guarded by a group set), [group set], then the fields."""
    out = bytearray([t['kind'] | (0x80 if gs else 0)])
    if gs:
        varint(gs, out)
    k = t['kind']
    if k in (TR_FADE, TR_TRAIL):
        out.append(t['arg'])
    elif k == TR_COLOR:
        out += bytes([t['target'], *t['rgb'], t['opacity'], t['flags']])
        varint(t['dur'], out)
        if t['flags'] & 64:
            out.append(t['copy'])
            h = t['hsv'] or (0, 64, 64, 0)
            svarint(h[0], out); svarint(h[1], out); svarint(h[2], out); out.append(h[3])
    elif k == TR_MOVE:
        varint(t['target'], out)
        svarint(round(t['dx'] * 4), out); svarint(round(t['dy'] * 4), out)
        varint(t['dur'], out)
        out += bytes([t['easing'], min(255, t['rate']), t['flags']])
    elif k == TR_ALPHA:
        varint(t['target'], out); out.append(t['opacity']); varint(t['dur'], out)
    elif k == TR_TOGGLE:
        varint(t['target'], out); out.append(t['on'])
    elif k == TR_PULSE:
        varint(t['target'], out)
        out += bytes([*t['rgb'], t['flags']])
        varint(t['t_in'], out); varint(t['hold'], out); varint(t['t_out'], out)
        if t['flags'] & 64:
            out.append(t['copy'])
            h = t['hsv'] or (0, 64, 64, 0)
            svarint(h[0], out); svarint(h[1], out); svarint(h[2], out); out.append(h[3])
    return bytes(out)


def decode_header_raw(path):
    nodes = list(ET.parse(path).getroot()[0])
    props = {nodes[i].text: nodes[i + 1].text for i in range(0, len(nodes), 2)}
    enc = props['k4']
    raw = gzip.decompress(base64.urlsafe_b64decode(enc + '=' * (-len(enc) % 4))).decode('latin-1')
    parts = raw.split(';')
    return dict(zip(parts[0].split(',')[::2], parts[0].split(',')[1::2]))


# ------------------------------------------------------------ record coding

def varint(v, out):
    assert v >= 0
    while True:
        b = v & 127
        v >>= 7
        if v:
            out.append(b | 128)
        else:
            out.append(b)
            return


def zz(v):
    return v * 2 if v >= 0 else -v * 2 - 1


ALIGN = 240   # 7.5 units in 1/32


def make_runs(objs):
    """Merge rows / columns of identical objects evenly spaced by 7.5 unit
    multiples into run records (only for sorted chunks)."""
    key = lambda o: (o['t'], o['rot'], o['flips'], o['scale'], o['paint'])
    rows = collections.defaultdict(list)
    for o in objs:
        rows[key(o) + (o['y'],)].append(o)
    recs, left = [], []
    for k, os_ in rows.items():
        os_.sort(key=lambda o: o['x'])
        i = 0
        while i < len(os_):
            j = i
            if i + 1 < len(os_):
                st = os_[i + 1]['x'] - os_[i]['x']
                if st > 0 and st % ALIGN == 0 and st // ALIGN < 256:
                    while j + 1 < len(os_) and os_[j + 1]['x'] - os_[j]['x'] == st and j - i < 255:
                        j += 1
            if j > i:
                r = dict(os_[i]); r['n'] = j - i + 1; r['step'] = os_[i + 1]['x'] - os_[i]['x']; r['dir'] = 0
                recs.append(r)
                i = j + 1
            else:
                left.append(os_[i])
                i += 1
    cols = collections.defaultdict(list)
    for o in left:
        cols[key(o) + (o['x'],)].append(o)
    for k, os_ in cols.items():
        os_.sort(key=lambda o: o['y'])
        i = 0
        while i < len(os_):
            j = i
            if i + 1 < len(os_):
                st = os_[i + 1]['y'] - os_[i]['y']
                if st > 0 and st % ALIGN == 0 and st // ALIGN < 256:
                    while j + 1 < len(os_) and os_[j + 1]['y'] - os_[j]['y'] == st and j - i < 255:
                        j += 1
            if j > i:
                r = dict(os_[i]); r['n'] = j - i + 1; r['step'] = os_[i + 1]['y'] - os_[i]['y']; r['dir'] = 1
                recs.append(r)
                i = j + 1
            else:
                r = dict(os_[i]); r['n'] = 1
                recs.append(r)
                i += 1
    return recs


def encode_legacy(objs):
    """Legacy chunks: 6-byte rows {dx, y, type, xform} in whole units, x order."""
    out = bytearray()
    px = objs[0]['x']
    for o in objs:
        assert o['x'] % Q == 0 and o['y'] % Q == 0 and o['x'] >= px
        xf = o['rot'] // 256 | o['flips'] << 2
        out += struct.pack('<HhBB', (o['x'] - px) // Q, o['y'] // Q, o['t'], xf)
        px = o['x']
    return bytes(out), list(objs), len(objs)


def encode_chunk(objs, xq0, legacy):
    """-> (payload bytes, expanded objects in runtime order, records)"""
    if legacy:
        return encode_legacy(objs)
    recs = make_runs(objs)
    recs.sort(key=lambda r: (r['t'], r['flips'], r['y'], r['x']))
    cols = collections.defaultdict(bytearray)
    pt, px, py = 0, xq0, 0
    for r in recs:
        varint(zz(r['t'] - pt), cols['T'])
        if r['t'] != pt:
            px, py = xq0, 0
        pt = r['t']
        props = (1 if r['rot'] else 0) | (2 if r['flips'] else 0) | (4 if r['scale'] else 0) | (8 if r['paint'] else 0)
        if r['n'] > 1:
            props |= 32 if r['dir'] else 16
        aligned = r['x'] % ALIGN == 0 and r['y'] % ALIGN == 0 and px % ALIGN == 0 and py % ALIGN == 0
        if aligned:
            props |= 64
        cols['P'].append(props)
        u = ALIGN if aligned else 1
        varint(zz((r['x'] - px) // u), cols['X'])
        varint(zz((r['y'] - py) // u), cols['Y'])
        px, py = r['x'], r['y']
        if r['rot']:
            varint(r['rot'], cols['R'])
        if r['flips']:
            cols['F'].append(r['flips'])
        if r['scale']:
            varint(r['scale'], cols['S'])
        if r['paint']:
            varint(r['paint'], cols['Q'])
        if r['n'] > 1:
            cols['N'].append(r['n'] - 2)
            cols['N'].append(r['step'] // ALIGN)
    out = bytearray()
    varint(len(recs), out)
    for k in 'TPXYRFSQN':
        out += cols[k]
    # the objects as the runtime will hold them: expanded, then stable-sorted by x
    exp = []
    for r in recs:
        for k in range(r['n']):
            o = dict(r)
            if r['n'] > 1:
                if r['dir']:
                    o['y'] = r['y'] + k * r['step']
                else:
                    o['x'] = r['x'] + k * r['step']
            exp.append(o)
    exp.sort(key=lambda o: o['x'])
    return bytes(out), exp, len(recs)


def deflate(data):
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    return c.compress(data) + c.flush()


def radius(o):
    """How far (units) from its x the object can reach: collisions and drawing
    (scaled objects reach further, and groups that move sideways further still)."""
    return int(math.ceil(od.RADIUS.get(o['t'], 45) * max(1.0, o.get('sx', 1.0)) + o.get('ext', 0)))


def cut_chunks(objs, legacy):
    """Chunks of objects in x order. Objects of groups that move sideways go
    into chunks of their own, whose reach covers the whole movement."""
    def cut(objs):
        chunks, cur = [], []
        limit = CHUNK_LEGACY if legacy else CHUNK_OBJS
        for o in objs:
            if cur and (len(cur) >= limit or (not legacy and o['x'] - cur[0]['x'] > CHUNK_SPAN * Q)):
                chunks.append(cur)
                cur = []
            cur.append(o)
        if cur:
            chunks.append(cur)
        return chunks
    chunks = cut([o for o in objs if not o.get('ext')]) + cut([o for o in objs if o.get('ext')])
    chunks.sort(key=lambda ch: min(o['x'] / Q - radius(o) for o in ch))
    return chunks


def c_bytes(name, data):
    lines = ['static const uint8_t %s[%d] = {' % (name, len(data))]
    for i in range(0, len(data), 40):
        lines.append('  ' + ','.join(str(b) for b in data[i:i + 40]) + ',')
    lines.append('};')
    return lines


def c_ident(spr):
    return 'SPR_' + ''.join(ch.upper() if ch.isalnum() else '_' for ch in spr)


def emit_objdefs():
    h = ['/* Generated by tools/levels.py. Do not edit. */', '#ifndef NUMDASH_OBJDEFS_H', '#define NUMDASH_OBJDEFS_H',
         '#include <stdint.h>', 'enum {', '  OT_NONE = 0,']
    for i, o in enumerate(od.OBJECTS):
        h.append('  OT_%s = %d,' % (o[0], i + 1))
    h.append('  OT_COUNT = %d' % (len(od.OBJECTS) + 1))
    h.append('};')
    h.append('enum { HIT_NONE, HIT_SOLID, HIT_HAZARD, HIT_SPECIAL };')
    h.append('enum { SHAPE_BOX, SHAPE_CIRCLE, SHAPE_SLOPE };')
    h.append('enum { ' + ', '.join('SP_' + s for s in od.SP) + ' };')
    h.append('enum { ' + ', '.join('CT_' + s for s in od.CT) + ' };')
    h.append('enum { ' + ', '.join('LAYER_' + s for s in od.LAYERS) + ', LAYER_COUNT };')
    h.append('enum { PF_PULSE = 1, PF_RANDOM3 = 2, PF_COIN = 4, PF_ANIM = 8, PF_QUAD = 16, PF_HALF = 32 };')
    h.append('enum { ' + ', '.join('ANIM_%s = %d' % kv for kv in sorted(od.ANIM.items(), key=lambda kv: kv[1])) + ' };')
    h.append('/* Tile programs: ops of {op, x0, y0, x1, y1, colour type, alpha[, bottom alpha]} in half units')
    h.append('   (2 units with TOP_BIG), y up; rectangle, or rectangle with a vertical alpha ramp. */')
    h.append('enum { TOP_END, TOP_RECT, TOP_VGRAD, TOP_BIG = 0x80 };')
    h.append('extern const uint8_t tile_prog_data[];')
    h.append('extern const uint16_t tile_prog_off[];')
    h.append('enum { EV_COLOR = 1, EV_FADE = 2, EV_TRAIL = 3 };')
    h.append('typedef struct { int16_t sprite; int8_t dx4, dy4; uint8_t layer, ctype, flags, prog; } ObjPart;')
    h.append('/* dbase, ddetail: default colour channels (dense index, see level.h); hx2: hitbox x offset in half units */')
    h.append('typedef struct { uint16_t gd_id; uint8_t hit, special; uint16_t w10, h10; int8_t editor_dy; uint8_t nparts;'
             ' uint16_t part0; uint8_t shape, anim, dbase, ddetail; int8_t hx2; } ObjDef;')
    h.append('extern const ObjDef objdefs[OT_COUNT];')
    h.append('extern const ObjPart objparts[];')
    h.append('#define OBJ_PART(d, k) (&objparts[(d)->part0 + (k)])')
    h.append('#define EDITOR_TYPES %d' % len(od.EDITOR))
    h.append('extern const uint8_t editor_types[EDITOR_TYPES];')
    h.append('#endif')
    (ROOT / 'src/objdefs.h').write_text('\n'.join(h) + '\n')

    c = ['/* Generated by tools/levels.py. Do not edit. */', '#include "objdefs.h"', '#include "assets_lv.h"']
    # tile programs (index 0 = none)
    prog_index, prog_bytes, prog_off = {}, bytearray(), [0]
    for pname, ops in od.TILE_PROGS.items():
        prog_index[pname] = len(prog_off)
        prog_off.append(len(prog_bytes))
        prog_bytes += od.encode_prog(ops)
    c.append('const uint8_t tile_prog_data[%d] = {%s};' % (max(1, len(prog_bytes)), ','.join(str(b) for b in prog_bytes) or '0'))
    c.append('const uint16_t tile_prog_off[%d] = {%s};' % (len(prog_off), ','.join(str(v) for v in prog_off)))
    c += ['const ObjDef objdefs[OT_COUNT] = {', '  {0,0,0,0,0,0,0,0,0,0,0,0,0},']
    parts_out = []
    for o in od.OBJECTS:
        name, ids, hit, w, hh, sp, edy, parts = o[:8]
        extra = o[8] if len(o) > 8 else {}
        first = len(parts_out)
        for (spr, dx, dy, layer, ct, fl) in parts:
            dx4, dy4 = int(round(dx * 4)), int(round(dy * 4))
            assert -128 <= dx4 <= 127 and -128 <= dy4 <= 127, f'{name}: part offset too large for int8'
            if spr.startswith('prog:'):
                parts_out.append('{-1,%d,%d,LAYER_%s,CT_%s,%d,%d}' % (dx4, dy4, layer, ct, fl, prog_index[spr[5:]]))
            else:
                parts_out.append('{%s,%d,%d,LAYER_%s,CT_%s,%d,0}' % (c_ident(spr), dx4, dy4, layer, ct, fl))
        ct0 = next((p[4] for p in parts if p[4] in CT_DEFAULT_CH), 'OBJ')
        dbase = SPECIAL_CH.index(extra.get('base', CT_DEFAULT_CH[ct0]))
        ddetail = SPECIAL_CH.index(extra.get('detail', 1))
        c.append('  {%d,%d,SP_%s,%d,%d,%d,%d,%d,SHAPE_%s,%d,%d,%d,%d},' % (
            ids[0] if ids else 0, hit, sp, int(round(w * 10)), int(round(hh * 10)), edy, len(parts), first,
            extra.get('shape', 'BOX'), od.ANIM[extra.get('anim', 'NONE')], dbase, ddetail, int(round(extra.get('hx', 0) * 2))))
    c.append('};')
    c.append('const ObjPart objparts[] = {')
    for i in range(0, len(parts_out), 4):
        c.append('  ' + ','.join(parts_out[i:i + 4]) + ',')
    c.append('};')
    c.append('const uint8_t editor_types[EDITOR_TYPES] = {' + ','.join('OT_' + n for n in od.EDITOR) + '};')
    (ROOT / 'src/objdefs.c').write_text('\n'.join(c) + '\n')


def main():
    emit_objdefs()
    out = ['/* Generated by tools/levels.py from levels/reference. Do not edit. */', '#include "level.h"']
    table, manifest = [], []
    total_z = 0
    max_window = 0
    for i, (fname, title, diff, stars, bpm, source) in enumerate(LEVELS):
        path = ROOT / 'levels/reference' / (fname + '.gmd')
        legacy = i < LEGACY
        if legacy:
            header, colors, objects, events, skipped = compile_legacy(path)
            L2 = None
        else:
            header, colors, objects, events, skipped, L2, trig, touch, npos = compile_new(path)
        chunks = cut_chunks(objects, legacy)
        blob = bytearray()
        rows = []
        coins, base = [], 0
        for ch in chunks:
            xq0 = ch[0]['x'] // ALIGN * ALIGN if not legacy else ch[0]['x']
            payload, exp, nrec = encode_chunk(ch, xq0, legacy)
            z = deflate(payload)
            assert zlib.decompress(z, -15) == payload
            assert len(payload) <= od.CHUNK_BYTES, '%s: chunk of %d bytes' % (title, len(payload))
            xs = [o['x'] / Q for o in exp]
            x0 = min(x - radius(o) for x, o in zip(xs, exp))
            x1 = max(x + radius(o) for x, o in zip(xs, exp))
            rmax = max(radius(o) for o in exp)
            for k, o in enumerate(exp):
                if o['t'] == od.INDEX['COIN']:
                    coins.append(base + k)
            rows.append('{%d,%d,%d,%.1ff,%.1ff,%d,%d,%d,%d,%d}' % (len(blob), base, xq0, x0, x1, len(z), len(exp), nrec, rmax, 1 if legacy else 0))
            blob += z
            base += len(exp)
        total_z += len(blob)
        # the most objects any camera position needs at once
        need = 0
        for cx in range(0, int(objects[-1]['x'] / Q) + 600, 60):
            lo, hi = cx - 200, cx + 800
            n = 0
            for r in rows:
                v = r.strip('{}').split(',')
                if float(v[3][:-1]) < hi and float(v[4][:-1]) > lo:
                    n += int(v[6])
            need = max(need, n)
        max_window = max(max_window, need)
        last_x = max(o['x'] for o in objects) // Q
        end_x = last_x + 330
        wall_x = int(round(end_x / 30.0)) * 30
        bg = colors.get(1000, (40, 125, 255))
        g1 = colors.get(1001, (0, 102, 255))
        line = colors.get(1002, (255, 255, 255))
        obj = colors.get(1004, (255, 255, 255))
        out += c_bytes('level%d_data' % i, bytes(blob))
        out.append('static const LChunk level%d_chunks[%d] = {' % (i, len(rows)))
        out += ['  ' + r + ',' for r in rows]
        out.append('};')
        if L2:
            rows_ch = L2.chan_rows()
            out.append('static const LChan level%d_chans[%d] = {' % (i, len(rows_ch)))
            for r in rows_ch:
                out.append('  {{%d,%d,%d},%d,%d,%d,%d,%d,%d},' % r)
            out.append('};')
            st = sorted(L2.styles.items(), key=lambda kv: kv[1])
            out.append('static const LStyle level%d_styles[%d] = {' % (i, len(st)))
            for k, _ in st:
                out.append('  {%d,%d,%d,%d,%d,%d},' % k)
            out.append('};')
            out.append('static const uint16_t level%d_gsets[%d] = {%s};' % (i, len(L2.gset_data), ','.join(map(str, L2.gset_data))))
            sc = sorted(L2.scales.items(), key=lambda kv: kv[1])
            out.append('static const uint16_t level%d_scales[%d][2] = {%s};' % (i, len(sc), ','.join('{%d,%d}' % k for k, _ in sc)))
            out += c_bytes('level%d_trig' % i, trig)
            out.append('static const uint16_t level%d_touch[%d] = {%s};' % (i, max(1, len(touch)), ','.join(map(str, touch)) or '0'))
            ext2 = '&(const LevelExt){level%d_chans,level%d_styles,level%d_gsets,level%d_scales,level%d_trig,level%d_touch,%d,%d,%d,%d,%d,%d,%d,%d}' % (
                i, i, i, i, i, i, len(rows_ch), len(st), len(L2.groups), len(sc), len(trig), len(touch),
                int(header.get('kA6', 0) or 0), int(header.get('kA7', 0) or 0))
            print('  %d channels, %d styles, %d groups, %d scales, %d triggers (%d bytes), %d touch' % (
                len(rows_ch), len(st), len(L2.groups), len(sc), npos + len(touch), len(trig), len(touch)))
            total_z += len(trig) + len(rows_ch) * 10 + len(st) * 8 + len(L2.gset_data) * 2 + len(sc) * 4
        else:
            ext2 = 'NULL'
        out.append('static const LevelEvent level%d_events[%d] = {' % (i, max(1, len(events))))
        for (x, y, kind, arg, r, g, b, flags, dur, _) in events:
            out.append('  {%d,%d,%d,%d,%d,%d,%d,%d,%d},' % (x, y, kind, arg, r, g, b, flags, dur))
        if not events:
            out.append('  {0,0,0,0,0,0,0,0,0},')
        out.append('};')
        coins = coins[:3]
        start_mode = int(header.get('kA2', 0))
        table.append('  {"%s",level%d_data,level%d_chunks,level%d_events,%s,%d,%d,%d,%d,%d,{%s},{%d,%d,%d},{%d,%d,%d},{%d,%d,%d},{%d,%d,%d},%d,%d,%d,%d,%d},' % (
            title.replace('"', '\\"'), i, i, i, ext2, base, len(rows), len(events), end_x, wall_x,
            ','.join(str(c) for c in coins + [0] * (3 - len(coins))),
            *bg, *g1, *line, *obj, diff, stars, bpm, start_mode, len(coins)))
        manifest.append({'name': title, 'file': 'levels/reference/%s.gmd' % fname, 'source': source,
                         'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                         'objects': base, 'events': len(events), 'compressed_bytes': len(blob),
                         'skipped_ids': {str(k): v for k, v in sorted(skipped.items())}})
        if skipped:
            print('%s: skipped ids %s' % (title, dict(skipped)))
        print('%-16s %6d objects in %3d chunks, %6d bytes, window %d' % (title, base, len(rows), len(blob), need))
    out.append('const LevelDef level_defs[LEVEL_COUNT] = {')
    out += table
    out.append('};')
    (ROOT / 'src/leveldata.c').write_text('\n'.join(out) + '\n')
    (ROOT / 'levels/manifest.json').write_text(json.dumps({'levels': manifest}, indent=2) + '\n')
    print('levels: %d objects -> %d bytes, largest window %d objects' % (sum(m['objects'] for m in manifest), total_z, max_window))


if __name__ == '__main__':
    main()
