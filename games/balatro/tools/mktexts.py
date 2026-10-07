#!/usr/bin/env python3
"""Balatro's texts in NumPlay's language builds: output/texts.c from src/texts.h.

Usage: mktexts.py src/texts.h out.c

tools/lang.py has put each text's translation in place in texts.h, written in the
game's markup like the English ones; they become the same control codes as in
data.c (mkdata.py's conv()) and are compressed the same way, by byte pairs, with
more of them (UTF-8 letters use many bytes): the bytes the texts use are numbered
from 1, the pairs take the numbers after them, and a number from ESC0 on takes
two bytes. A pair is two such numbers, 12 bits each. The C defines what data.c
does in the English build: text_off and text_get()."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkdata import bpe, conv  # noqa: E402

N_TEXTS = 320
ESC0 = 252
ENTRY = re.compile(r'T\(\s*"((?:[^"\\]|\\.)*)"\s*\)|"((?:[^"\\]|\\.)*)"')


def unescape(s):
    return re.sub(r'\\(.)', lambda m: "\n" if m.group(1) == "n" else m.group(1), s)


def main():
    src = open(sys.argv[1], encoding="utf-8").read()
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    texts = [unescape(m.group(1) if m.group(1) is not None else m.group(2)) for m in ENTRY.finditer(src)]
    assert len(texts) == 2 * N_TEXTS, len(texts)
    raw = [(conv(texts[i]) + "\0" + conv(texts[i + 1]) + "\0").encode("utf-8") for i in range(0, len(texts), 2)]
    # the bytes the texts use become 1.. (0 ends a text), the pairs the numbers after them
    lit = sorted({b for r in raw for b in r} - {0})
    to = {b: k + 1 for k, b in enumerate(lit)}
    seqs, pairs = bpe([[to[b] if b else 0 for b in r] for r in raw], range(len(lit) + 1, ESC0 + 256 * (256 - ESC0)))
    blob, offs = bytearray(), []
    for sq in seqs:
        offs.append(len(blob))
        for v in sq:
            blob += bytes([v]) if v < ESC0 else bytes([ESC0 + (v - ESC0) // 256, (v - ESC0) % 256])
    pos = len(blob)
    packed = bytearray()
    for a, b in pairs:
        packed += bytes([a & 255, a >> 8 | (b & 15) << 4, b >> 4])
    c = ["/* Made by tools/mktexts.py from src/texts.h (translated): Balatro's texts in this language. */",
         '#include "../src/data.h"', ""]
    c.append("static const uint8_t lang_blob[%d] = {" % len(blob))
    for i in range(0, len(blob), 24):
        c.append("  " + ",".join(str(v) for v in blob[i:i + 24]) + ",")
    c.append("};")
    c.append("#define NLIT %d /* 1..NLIT: these bytes; after them, pairs */" % len(lit))
    c.append("#define ESC0 %d /* from here on, a number takes two bytes */" % ESC0)
    c.append("static const uint8_t lang_lit[NLIT] = {" + ",".join(map(str, lit)) + "};")
    c.append("static const uint8_t lang_pairs[%d] = {" % max(1, len(packed)))
    for i in range(0, len(packed), 24):
        c.append("  " + ",".join(str(v) for v in packed[i:i + 24]) + ",")
    c.append("};")
    c.append(r"""
static char *expand(char *o, char *end, int c) {
  if (c <= NLIT) {
    if (o < end) *o++ = (char)lang_lit[c - 1];
    return o;
  }
  const uint8_t *p = lang_pairs + 3 * (c - NLIT - 1);
  o = expand(o, end, p[0] | (p[1] & 15) << 8);
  return expand(o, end, p[1] >> 4 | p[2] << 4);
}

const char *text_get(int idx, int part) {
  static char bufs[4][320];
  static int k;
  char *b = bufs[k = (k + 1) & 3], *o = b;
  const uint8_t *p = lang_blob + text_off[idx];
  for (;;) {
    int c = *p++;
    if (c >= ESC0) c = ESC0 + (c - ESC0) * 256 + *p++;
    else if (!c && part--) continue; /* the name skipped */
    else if (!c) break;
    if (!part) o = expand(o, b + sizeof bufs[0] - 1, c);
  }
  *o = 0;
  return b;
}
""")
    c.append("const uint16_t text_off[N_TEXTS] = {" + ",".join(map(str, offs)) + "};")
    open(sys.argv[2], "w").write("\n".join(c) + "\n")
    print(f"{sys.argv[2]}: {len(texts) // 2} texts, {pos} bytes ({sum(len(r) for r in raw)} before), {len(pairs)} pairs")


if __name__ == "__main__":
    main()
