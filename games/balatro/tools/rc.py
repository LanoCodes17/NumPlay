"""The sprite coder, mirrored by src/art.c.

Each sprite is coded on its own (so any one can be decoded on demand) with a
binary range coder (the one LZMA uses) and a small context model made for
pixel art. Pixels are palette indices (0 = transparent), row by row. For each
pixel, in order, a flag says whether it equals:
  1. the group template at the same place (the common card frame), if any;
  2. the pixel to the left;  3. the pixel above;  4. the pixel above right;
and otherwise its index in the sprite's local palette follows, bit by bit.
Candidates equal to an earlier one are skipped. Contexts are small tuples of
equalities between the neighbours."""

PROB_BITS = 11
PROB_INIT = 1 << (PROB_BITS - 1)
SHIFT = 4
TOP = 1 << 24


class Encoder:
    def __init__(self):
        self.low = 0
        self.range = 0xFFFFFFFF
        self.cache = 0
        self.cache_size = 1
        self.out = bytearray()

    def _shift_low(self):
        if self.low < 0xFF000000 or self.low >= 1 << 32:
            carry = self.low >> 32
            temp = self.cache
            while True:
                self.out.append((temp + carry) & 0xFF)
                temp = 0xFF
                self.cache_size -= 1
                if self.cache_size == 0:
                    break
            self.cache = (self.low >> 24) & 0xFF
        self.cache_size += 1
        self.low = (self.low << 8) & 0xFFFFFFFF

    def bit(self, probs, i, b):
        p = probs[i]
        bound = (self.range >> PROB_BITS) * p
        if b == 0:
            self.range = bound
            probs[i] = p + (((1 << PROB_BITS) - p) >> SHIFT)
        else:
            self.low += bound
            self.range -= bound
            probs[i] = p - (p >> SHIFT)
        while self.range < TOP:
            self.range = (self.range << 8) & 0xFFFFFFFF
            self._shift_low()

    def direct(self, v, n):
        for k in range(n - 1, -1, -1):
            self.range >>= 1
            if (v >> k) & 1:
                self.low += self.range
            while self.range < TOP:
                self.range = (self.range << 8) & 0xFFFFFFFF
                self._shift_low()

    def finish(self):
        for _ in range(5):
            self._shift_low()
        # the first byte is always 0 in LZMA's scheme: drop it (the decoder starts past it)
        return bytes(self.out[1:])


# context slots
C_T, C_L, C_U, C_R, C_LIT = 0, 16, 32, 36, 40
NPROBS = C_LIT + 256


def nbits(n):
    b = 0
    while (1 << b) < n:
        b += 1
    return b


def encode_sprite(pix, w, h, tmpl=None):
    """pix: list of rows of global indices (0..255). Returns bytes."""
    e = Encoder()
    probs = [PROB_INIT] * NPROBS
    pal = sorted(set(v for row in pix for v in row))
    e.direct(len(pal) - 1, 8)
    for v in pal:
        e.direct(v, 8)
    lb = nbits(len(pal))
    loc = {v: i for i, v in enumerate(pal)}
    for y in range(h):
        for x in range(w):
            p = pix[y][x]
            L = pix[y][x - 1] if x > 0 else -1
            U = pix[y - 1][x] if y > 0 else -1
            UL = pix[y - 1][x - 1] if x > 0 and y > 0 else -1
            UR = pix[y - 1][x + 1] if y > 0 and x < w - 1 else -1
            seen = []
            if tmpl is not None:
                t = tmpl[y][x]
                lt = (tmpl[y][x - 1] == L) if x > 0 else 1
                ut = (tmpl[y - 1][x] == U) if y > 0 else 1
                ctx = C_T + (lt | ut << 1 | (t == L) << 2 | (t == U) << 3)
                e.bit(probs, ctx, p == t)
                if p == t:
                    continue
                seen.append(t)
            if L >= 0 and L not in seen:
                ctx = C_L + ((U == L) | (UL == L) << 1 | (UR == U) << 2 | (UL == U) << 3)
                e.bit(probs, ctx, p == L)
                if p == L:
                    continue
                seen.append(L)
            if U >= 0 and U not in seen:
                ctx = C_U + ((UL == U) | (UR == U) << 1)
                e.bit(probs, ctx, p == U)
                if p == U:
                    continue
                seen.append(U)
            if UR >= 0 and UR not in seen:
                e.bit(probs, C_R, p == UR)
                if p == UR:
                    continue
                seen.append(UR)
            s = loc[p]
            node = 1
            for k in range(lb - 1, -1, -1):
                b = (s >> k) & 1
                e.bit(probs, C_LIT + node, b)
                node = node * 2 + b
    return e.finish()


class Decoder:
    """Only for checking the encoder in Python."""

    def __init__(self, data):
        self.d = data
        self.i = 0
        self.range = 0xFFFFFFFF
        self.code = 0
        for _ in range(4):
            self.code = (self.code << 8) | self._byte()

    def _byte(self):
        b = self.d[self.i] if self.i < len(self.d) else 0
        self.i += 1
        return b

    def bit(self, probs, i):
        p = probs[i]
        bound = (self.range >> PROB_BITS) * p
        if self.code < bound:
            self.range = bound
            probs[i] = p + (((1 << PROB_BITS) - p) >> SHIFT)
            b = 0
        else:
            self.code -= bound
            self.range -= bound
            probs[i] = p - (p >> SHIFT)
            b = 1
        while self.range < TOP:
            self.range = (self.range << 8) & 0xFFFFFFFF
            self.code = ((self.code << 8) | self._byte()) & 0xFFFFFFFF
        return b

    def direct(self, n):
        v = 0
        for _ in range(n):
            self.range >>= 1
            b = 1 if self.code >= self.range else 0
            if b:
                self.code -= self.range
            v = v << 1 | b
            while self.range < TOP:
                self.range = (self.range << 8) & 0xFFFFFFFF
                self.code = ((self.code << 8) | self._byte()) & 0xFFFFFFFF
        return v


def decode_sprite(data, w, h, tmpl=None):
    d = Decoder(data)
    probs = [PROB_INIT] * NPROBS
    n = d.direct(8) + 1
    pal = [d.direct(8) for _ in range(n)]
    lb = nbits(n)
    pix = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            L = pix[y][x - 1] if x > 0 else -1
            U = pix[y - 1][x] if y > 0 else -1
            UL = pix[y - 1][x - 1] if x > 0 and y > 0 else -1
            UR = pix[y - 1][x + 1] if y > 0 and x < w - 1 else -1
            seen = []
            v = None
            if tmpl is not None:
                t = tmpl[y][x]
                lt = (tmpl[y][x - 1] == L) if x > 0 else 1
                ut = (tmpl[y - 1][x] == U) if y > 0 else 1
                if d.bit(probs, C_T + (lt | ut << 1 | (t == L) << 2 | (t == U) << 3)):
                    v = t
                seen.append(t)
            if v is None and L >= 0 and L not in seen:
                if d.bit(probs, C_L + ((U == L) | (UL == L) << 1 | (UR == U) << 2 | (UL == U) << 3)):
                    v = L
                seen.append(L)
            if v is None and U >= 0 and U not in seen:
                if d.bit(probs, C_U + ((UL == U) | (UR == U) << 1)):
                    v = U
                seen.append(U)
            if v is None and UR >= 0 and UR not in seen:
                if d.bit(probs, C_R):
                    v = UR
                seen.append(UR)
            if v is None:
                node = 1
                for _ in range(lb):
                    node = node * 2 + d.bit(probs, C_LIT + node)
                v = pal[node - (1 << lb)]
            pix[y][x] = v
    return pix
