#!/usr/bin/env python3
"""Checks that NumBlocks on its own keeps its worlds through an install.

Installing an app from the NumWorks website empties the calculator's files but
the Python scripts, so NumBlocks keeps a copy of its saves in
numblocks_saves.py. In the ARM emulator:

1. a new world, a block mined, then Home: the world, its changed blocks and the
   options are saved, and the copy holds each of them, byte for byte, in a
   valid Python script;
2. "install again" (every file but the scripts deleted): started again, the
   saves come back byte for byte and the other script is untouched.

Usage: test_numblocks_progress.py build/apps/NumBlocks.nwa [--out DIR]
"""
import argparse
import base64
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_progress import presses, records, run, write_storage  # noqa: E402

COPY = "numblocks_saves.py"


def ours(name):
    return name == "numblocks.cfg" or (name.startswith("nb") and name[2:3].isdigit() and name[2] != "0")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nwa")
    ap.add_argument("--out", default="build/test_numblocks_progress")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    script = ("pi.py", b"\x01print(3.14159)\n\x00")
    problems = []
    with tempfile.TemporaryDirectory() as d:
        st = os.path.join(d, "storage.bin")
        write_storage(st, [script])
        # the key sheet, Singleplayer, Create New World, down to its button; then look down and mine (OK)
        keys = presses((3000, "ok"), (3600, "ok"), (4200, "ok"), (4800, "down"), (5100, "down"), (5400, "down"),
                       (5700, "down"), (6000, "down"), (6300, "ok"), (9000, "down", 1200), (10400, "ok", 3500))
        recs, p = run(a.nwa, st, keys, 14500, a.out, "1_play")
        problems += p
        saves = {n: v for n, v in recs.items() if ours(n)}
        for want in ("numblocks.cfg", "nb1.nbw"):
            if want not in saves:
                problems.append(f"{want} not written")
        if not any(n.startswith("nb1r") for n in saves):
            problems.append("no changed blocks saved (the mined block)")
        c = recs.get(COPY)
        if c is None:
            problems.append(f"{COPY} not written")
        else:
            if c[0] != 0 or c[-1] != 0 or 0 in c[1:-1]:
                problems.append(f"{COPY}: not a script")
            copied = {}
            for line in c[1:-1].decode("ascii", "replace").split("\n"):
                if line and not line.startswith("#"):
                    problems.append(f"{COPY}: a line that isn't a comment: {line[:40]!r}")
                if line.startswith("#>"):
                    name, _, data = line[2:].partition(":")
                    try:
                        copied[name] = base64.b64decode(data, validate=True)
                    except ValueError:
                        problems.append(f"{COPY}: {name} is not base64")
            if copied != saves:
                problems.append(f"{COPY} holds {sorted(copied)}, the saves are {sorted(saves)}"
                                + ("" if set(copied) != set(saves) else " (contents differ)"))
        # installed again: only the scripts are left
        write_storage(st, [(n, v) for n, v in records(open(st, "rb").read()) if n.endswith(".py")])
        recs, p = run(a.nwa, st, [], 4000, a.out, "2_restore")
        problems += p
        for n, v in saves.items():
            if recs.get(n) != v:
                problems.append(f"after installing again, {n} " + ("missing" if n not in recs else "differs"))
        if recs.get(script[0]) != script[1]:
            problems.append("the other script changed")
        print(f"   saves: {', '.join(f'{n} ({len(v)} bytes)' for n, v in sorted(saves.items()))}")
    for x in problems:
        print("   ", x)
    print("PASS" if not problems else "FAIL")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
