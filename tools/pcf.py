"""Minimal PCF reader: {codepoint: (width_advance, xoff, yoff_from_baseline_top, w, h, rows)}"""
import struct
def read_pcf(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'\x01fcp'
    n, = struct.unpack_from('<i', d, 4)
    tables = {}
    for i in range(n):
        t, fmt, size, off = struct.unpack_from('<iiii', d, 8 + 16 * i)
        tables[t] = (fmt, size, off)
    def hdr(t):
        fmt0, size, off = tables[t]
        fmt, = struct.unpack_from('<i', d, off)
        e = '>' if fmt & 4 else '<'
        return fmt, e, off + 4
    # metrics
    fmt, e, p = hdr(4)
    metrics = []
    if fmt & 0x100:
        cnt, = struct.unpack_from(e + 'H', d, p); p += 2
        for i in range(cnt):
            l, r, w, a, de = (x - 0x80 for x in d[p:p + 5]); p += 5
            metrics.append((l, r, w, a, de))
    else:
        cnt, = struct.unpack_from(e + 'i', d, p); p += 4
        for i in range(cnt):
            l, r, w, a, de, at = struct.unpack_from(e + 'hhhhhH', d, p); p += 12
            metrics.append((l, r, w, a, de))
    # bitmaps
    fmt, e, p = hdr(8)
    cnt, = struct.unpack_from(e + 'i', d, p); p += 4
    offs = struct.unpack_from(e + '%di' % cnt, d, p); p += 4 * cnt
    sizes = struct.unpack_from(e + '4i', d, p); p += 16
    pad = 1 << (fmt & 3)
    msbit = bool(fmt & 8)
    bits = d[p:p + sizes[fmt & 3]]
    # encodings
    fmt2, e2, q = hdr(32)
    min2, max2, min1, max1, dflt = struct.unpack_from(e2 + '5h', d, q); q += 10
    ncodes = (max2 - min2 + 1) * (max1 - min1 + 1)
    idx = struct.unpack_from(e2 + '%dH' % ncodes, d, q)
    out = {}
    k = 0
    for b1 in range(min1, max1 + 1):
        for b2 in range(min2, max2 + 1):
            gi = idx[k]; k += 1
            if gi == 0xFFFF: continue
            cp = b1 * 256 + b2
            l, r, w, a, de = metrics[gi]
            gw, gh = r - l, a + de
            stride = ((gw + 7) // 8 + pad - 1) // pad * pad if gw > 0 else 0
            rows = []
            o = offs[gi]
            for y in range(gh):
                row = 0
                for x in range(gw):
                    byte = bits[o + y * stride + x // 8]
                    bit = (byte >> (7 - x % 8)) & 1 if msbit else (byte >> (x % 8)) & 1
                    row |= bit << x
                rows.append(row)
            out[cp] = (w, l, a, gw, gh, rows)
    return out
if __name__ == '__main__':
    import sys
    f = read_pcf(sys.argv[1])
    print(len(f))
    for ch in '你好游戏设置é':
        g = f.get(ord(ch))
        print(ch, g[:5] if g else None)
        if g:
            for r in g[5]:
                print(''.join('#' if r >> x & 1 else '.' for x in range(g[3])))
