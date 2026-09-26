#!/usr/bin/env python3
"""Plays each game's calculator build in the ARM emulator, twice, and checks
what used to crash calculators or lose saves:

- the app quits on Home by itself (Epsilon holds Home back while it runs) and
  hands back its RAM cleared, so Epsilon's memory pools come back safely;
- no unaligned multi-word access (a fault on the Cortex-M7), no write to flash
  or to the firmware's RAM outside the file system;
- the file system stays valid: the game's save is there, the other files are
  untouched, and a file added after the save does not stop the game from
  updating it on the second run.

Usage: test_games.py build/apps [--game NAME] [--out DIR]
"""
import argparse
import json
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import emu  # noqa: E402

K = emu.KEYS


def presses(*items):
    """(time, key[, duration]) -> key ranges."""
    out = []
    for it in items:
        t, k = it[0], it[1]
        d = it[2] if len(it) > 2 else 90
        out.append((t, t + d, K[k]))
    return out


def every(t0, t1, step, key, d=80):
    return [(t, t + d, K[key]) for t in range(t0, t1, step)]


# name: (nwa, save record, first run keys, home time, second run keys, home time)
GAMES = {
    "crossyroad": ("CrossyRoad.nwa", "crossyroad.sav",
                   every(1500, 9000, 350, "up"), 12000, every(1500, 4000, 400, "up"), 6000),
    "chess": ("NumChess.nwa", None, presses((1500, "ok"), (2500, "ok")), 4000, [], 2500),
    "numdash": ("NumDash.nwa", "numdash.nds", presses((2500, "ok"), (4000, "ok"), (5500, "ok")), 9000,
                presses((2500, "ok")), 5000),
    # pause, level list, next level, play: that saves the level to start from
    "numdrive": ("NumDrive.nwa", "drivemad.sav", presses((1500, "ok"), (2000, "back"), (2500, "right"), (3000, "ok")),
                 5000, presses((1500, "ok"), (2000, "back"), (2500, "left"), (3000, "ok")), 5000),
    "tetris": ("Tetris.nwa", "tetris.sav", presses((1200, "ok")) + every(2500, 7000, 500, "up"), 8000,
               presses((1200, "ok"), (2500, "ok")), 5000),
}


# saves already on the calculator, and what each session must leave in them
SEEDS = {
    # the first level done and last played
    "numdrive": b"MD" + bytes([1] + [0] * 24) + b"\0\0\0",
}
CHECKS = {
    "numdrive": (lambda v: v[27] == 1, lambda v: v[27] == 0),  # last played level
}


def records(buf):
    out, p = [], 0
    while p + 2 <= len(buf):
        n, = struct.unpack_from("<H", buf, p)
        if n == 0:
            return out
        if n < 4 or p + n > len(buf):
            raise ValueError(f"corrupt record at {p}")
        name = buf[p + 2:buf.index(b"\0", p + 2)].decode("latin1")
        out.append((name, bytes(buf[p + 3 + len(name):p + n])))
        p += n
    raise ValueError("no end of the record list")


def through_launcher(index, keys, home):
    """The same session, started from NumPlay's carousel (game `index`)."""
    lead = [(1200 + 300 * i, 1290 + 300 * i, K["right"]) for i in range(index)]
    ok = 1500 + 300 * index
    lead.append((ok, ok + 90, K["ok"]))
    shift = ok + 800
    return lead + [(a + shift, b + shift, k) for a, b, k in keys], home + shift


def run_once(nwa, storage, keys, home, out, tag):
    c = emu.Calculator(nwa, storage_file=storage)
    c.enable_checks(reads=True)
    c.keys = keys + [(home, home + 400, K["home"])]
    c.pending_shots = [(home - 100, os.path.join(out, f"{tag}.png"))]
    c.run(home + 3000)
    problems = []
    if not c.exited:
        problems.append("did not quit on Home")
    if c.violations:
        problems += sorted(set(c.violations))[:8]
    if not c.app_ram_clean():
        problems.append("RAM not cleared on exit")
    if c.max_locks != 1 or c.locks != 0:
        problems.append(f"Home lock not held and released once (max {c.max_locks}, now {c.locks})")
    changed = c.firmware_ram_changes()
    if changed:
        problems.append(f"{len(changed)} words of the firmware's RAM changed, first at {changed[0]:#x}")
    stack = c.stack_used()
    if stack > 30 * 1024:
        problems.append(f"stack use {stack} bytes, close to the 32 KB limit")
    return c, problems, stack


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("apps")
    ap.add_argument("--game")
    ap.add_argument("--out", default="build/test_games")
    ap.add_argument("--numplay", help="play the games inside this NumPlay.nwa instead")
    a = ap.parse_args()
    order = [g["id"] for g in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "games",
                                                          "games.json")))]
    os.makedirs(a.out, exist_ok=True)
    failed = False
    for name, (nwa, save, keys1, home1, keys2, home2) in GAMES.items():
        if a.game and a.game != name:
            continue
        path = os.path.join(a.apps, nwa)
        if a.numplay:  # Home in a game quits NumPlay altogether
            path = a.numplay
            keys1, home1 = through_launcher(order.index(name), keys1, home1)
            keys2, home2 = through_launcher(order.index(name), keys2, home2)
        with tempfile.TemporaryDirectory() as d:
            storage = os.path.join(d, "storage.bin")
            # the calculator already has a script
            pre = [("pi.py", b"\x01print(3.14159)\n")]
            if name in SEEDS:
                pre.append((save, SEEDS[name]))
            buf = bytearray(emu.STORAGE_SIZE)
            p = 0
            for rn, content in pre:
                size = 2 + len(rn) + 1 + len(content)
                struct.pack_into("<H", buf, p, size)
                buf[p + 2:p + size] = rn.encode() + b"\0" + content
                p += size
            open(storage, "wb").write(buf)
            c, problems, stack = run_once(path, storage, keys1, home1, a.out, f"{name}_1")
            recs = dict(records(open(storage, "rb").read()))
            if recs.get("pi.py") != pre[0][1]:
                problems.append("pi.py changed")
            if save and save not in recs:
                problems.append(f"{save} not written")
            elif name in CHECKS and not CHECKS[name][0](recs[save]):
                problems.append(f"{save} not updated")
            # a script added after the save, then a second session
            buf = bytearray(open(storage, "rb").read())
            lst = records(buf)
            p = sum(2 + len(n) + 1 + len(v) for n, v in lst)
            late = b"\x01print('later')\n"
            size = 2 + len("late.py") + 1 + len(late)
            struct.pack_into("<H", buf, p, size)
            buf[p + 2:p + size] = b"late.py\0" + late
            struct.pack_into("<H", buf, p + size, 0)
            open(storage, "wb").write(buf)
            c2, problems2, stack2 = run_once(path, storage, keys2, home2, a.out, f"{name}_2")
            problems += [f"second run: {x}" for x in problems2]
            try:
                recs2 = dict(records(open(storage, "rb").read()))
                if recs2.get("pi.py") != pre[0][1] or recs2.get("late.py") != late:
                    problems.append("second run: other files changed")
                if save and save not in recs2:
                    problems.append(f"second run: {save} lost")
                elif name in CHECKS and not CHECKS[name][1](recs2[save]):
                    problems.append(f"second run: {save} not updated")
            except ValueError as e:
                problems.append(f"second run: file system corrupt ({e})")
        status = "ok" if not problems else "FAIL"
        failed |= bool(problems)
        print(f"{name:11s} {status}  stack {max(stack, stack2)} bytes, files {sorted(recs)}", flush=True)
        for x in problems:
            print("   ", x)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
