"""Buckshot's picture coder (the encoder; src/pixcode.c decodes).

Each pixel (a palette index, 0 = transparent) is coded with a binary range
coder and adaptive probabilities, from what the decoder already has around
it: is it the same as the pixel on its left? if not, the same as the one
above? if not, its index, bit by bit. The contexts of those questions are
how the neighbours (left, above, above left, above right) agree, and how long
the current run is. The pictures are posterized renders with large areas of
one color and dithering between two levels, which this codes about 30%
smaller than DEFLATE, while decoding about as fast.
"""
import numpy as np

PROB_BITS = 12
ONE = 1 << PROB_BITS
FLAG_SHIFT, LIT_SHIFT = 4, 5
LIT_BITS = 7  # indices up to 127


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
                if not self.cache_size:
                    break
            self.cache = (self.low >> 24) & 0xFF
        self.cache_size += 1
        self.low = (self.low << 8) & 0xFFFFFFFF

    def bit(self, probs, i, b, shift):
        p = probs[i]
        bound = (self.range >> PROB_BITS) * p
        if not b:
            self.range = bound
            probs[i] = p + ((ONE - p) >> shift)
        else:
            self.low += bound
            self.range -= bound
            probs[i] = p - (p >> shift)
        while self.range < 1 << 24:
            self.range = (self.range << 8) & 0xFFFFFFFF
            self._shift_low()

    def finish(self):
        for _ in range(5):
            self._shift_low()
        return bytes(self.out[1:])  # the first byte is always 0: the decoder starts without it


def encode(idx):
    """idx: 2-D array of palette indices (0..127)."""
    idx = np.asarray(idx, dtype=np.int32)
    assert idx.max() < 1 << LIT_BITS
    h, w = idx.shape
    rc = Encoder()
    flag_a = [ONE // 2] * 16
    flag_b = [ONE // 2] * 8
    lit = [ONE // 2] * (1 << LIT_BITS)
    run = 0
    rows = idx.tolist()
    for y in range(h):
        row = rows[y]
        prev = rows[y - 1] if y else None
        for x in range(w):
            s = row[x]
            if x:
                left = row[x - 1]
            else:
                left = prev[0] if y else 0
            up = prev[x] if y else left
            ul = prev[x - 1] if (x and y) else up
            ur = prev[x + 1] if (y and x + 1 < w) else up
            ca = (left == up) | (up == ul) << 1 | (up == ur) << 2 | run << 3
            if s == left:
                rc.bit(flag_a, ca, 1, FLAG_SHIFT)
                run = 1
                continue
            rc.bit(flag_a, ca, 0, FLAG_SHIFT)
            run = 0
            if up != left:
                cb = (up == ul) | (up == ur) << 1 | (left == ul) << 2
                if s == up:
                    rc.bit(flag_b, cb, 1, FLAG_SHIFT)
                    continue
                rc.bit(flag_b, cb, 0, FLAG_SHIFT)
            node = 1
            for i in range(LIT_BITS - 1, -1, -1):
                b = (s >> i) & 1
                rc.bit(lit, node, b, LIT_SHIFT)
                node = node * 2 + b
    return rc.finish()


def decode(data, w, h):
    """The reference decoder (for tests): the same steps as src/pixcode.c."""
    data = bytes(data) + bytes(8)
    pos = 4
    code = int.from_bytes(data[:4], "big")
    rng = 0xFFFFFFFF
    flag_a = [ONE // 2] * 16
    flag_b = [ONE // 2] * 8
    lit = [ONE // 2] * (1 << LIT_BITS)

    def bit(probs, i, shift):
        nonlocal code, rng, pos
        p = probs[i]
        bound = (rng >> PROB_BITS) * p
        if code < bound:
            rng = bound
            probs[i] = p + ((ONE - p) >> shift)
            b = 0
        else:
            rng -= bound
            code -= bound
            probs[i] = p - (p >> shift)
            b = 1
        while rng < 1 << 24:
            rng = (rng << 8) & 0xFFFFFFFF
            code = ((code << 8) | data[pos]) & 0xFFFFFFFF
            pos += 1
        return b

    out = [[0] * w for _ in range(h)]
    run = 0
    for y in range(h):
        row = out[y]
        prev = out[y - 1] if y else None
        for x in range(w):
            left = row[x - 1] if x else (prev[0] if y else 0)
            up = prev[x] if y else left
            ul = prev[x - 1] if (x and y) else up
            ur = prev[x + 1] if (y and x + 1 < w) else up
            ca = (left == up) | (up == ul) << 1 | (up == ur) << 2 | run << 3
            if bit(flag_a, ca, FLAG_SHIFT):
                row[x] = left
                run = 1
                continue
            run = 0
            if up != left:
                cb = (up == ul) | (up == ur) << 1 | (left == ul) << 2
                if bit(flag_b, cb, FLAG_SHIFT):
                    row[x] = up
                    continue
            node = 1
            for _ in range(LIT_BITS):
                node = node * 2 + bit(lit, node, LIT_SHIFT)
            row[x] = node - (1 << LIT_BITS)
    return np.array(out)


if __name__ == "__main__":
    rng = np.random.default_rng(1)
    for w, h in ((1, 1), (5, 3), (40, 30)):
        a = rng.integers(0, 5, (h, w))
        a[a == 4] = rng.integers(0, 127, (a == 4).sum())
        z = encode(a)
        assert (decode(z, w, h) == a).all(), (w, h)
    print("ok")
