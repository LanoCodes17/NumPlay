#!/usr/bin/env python3
"""Checks of Celeste on a computer (make test): the host build, with
AddressSanitizer and UBSan, through the menus, the saves and every room.

    python3 tests/check.py build/play src/data.bin [--quick]

--quick skips playing every room (the longest part)."""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
DATA = sys.argv[2] if len(sys.argv) > 2 else "src/data.bin"
QUICK = "--quick" in sys.argv
failures = []
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")


def run(args, frames, keys="", saves=None, env=None, what=""):
    """Plays (from a save folder, a new one if None); returns its output (the session line)."""
    own = saves is None
    if own:
        saves = tempfile.mkdtemp(prefix="celeste_check_")
    e = dict(ENV)
    e.update(env or {})
    p = subprocess.run([PLAY, DATA, "--saves", saves, "--frames", str(frames), "--keys", keys] + args,
                       capture_output=True, text=True, env=e, timeout=600)
    if own:
        shutil.rmtree(saves, ignore_errors=True)
    if p.returncode or "runtime error" in p.stderr or "AddressSanitizer" in p.stderr:
        failures.append(f"{what or args}: crashed or a sanitizer error\n{p.stderr[-2000:]}")
    return p.stdout


def session(out):
    m = re.search(r"session: chapter (\d+) area (\d+) mode (\d+) deaths (\d+) time (\d+)", out)
    return tuple(int(v) for v in m.groups()) if m else None


def check(cond, msg):
    if not cond:
        failures.append(msg)


# the first time: the key sheet, the title, the main menu, Climb, the chapter, Start: the Prologue
MENU_TO_PLAY = "40-41:o,100-101:o,160-161:o,220-221:o,280-281:o"
d = tempfile.mkdtemp(prefix="celeste_check_")
out = run([], 500, MENU_TO_PLAY + ",400-450:r", saves=d, what="the menus to the Prologue")
s = session(out)
check(s and s[1] == 0 and s[4] > 100, f"the menus did not start the Prologue: {s}")

# Home saves and leaves; Climb then shows Continue, which carries on the same run
out = run([], 700, MENU_TO_PLAY + ",600-601:h", saves=d, what="Home in the Prologue")
check(os.path.exists(d + "/celeste.sav") and os.path.exists(d + "/celeste_saves.py"), "Home did not save")
t0 = session(out)[4] if session(out) else 0
out = run([], 300, "40-41:o,100-101:o,160-161:o", saves=d, what="Continue after Home")
s = session(out)
check(s and s[4] > t0, f"Continue did not carry on the run ({t0} frames before): {s}")

# the copy in the Python script brings the save back when it is gone
os.remove(d + "/celeste.sav")
out = run([], 10, saves=d, what="the save from its copy")
s2 = session(out)
check(s2 and s2[4] >= t0, f"the save did not come back from celeste_saves.py: {s2}")
shutil.rmtree(d, ignore_errors=True)

# a 1.6.0 save: its keys on backspace (pause now) go to Back, as its defaults (dash and talk on backspace) did
def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


for old, new in (([4, 17, 16, 17], "4 5 16 5"), ([17, 16, 4, 29], "5 16 4 29")):
    d = tempfile.mkdtemp(prefix="celeste_check_")
    v1 = bytearray(1552)
    struct.pack_into("<IHH", v1, 0, 0x43454C53, 1, 1552)
    v1[1071] = 1   # the key sheet seen
    v1[1100:1104] = bytes(old)
    struct.pack_into("<I", v1, 1544, fnv(v1[:1544]))
    open(d + "/celeste.sav", "wb").write(v1)
    out = run([], 10, saves=d, what="a 1.6.0 save's keys")
    check("keys " + new in out, f"the keys {old} of a 1.6.0 save did not become {new}: {out[-60:]}")
    shutil.rmtree(d, ignore_errors=True)

# the pause menu: Retry is a death
out = run(["--chapter", "1", "--room", "1"], 300, "200-201:p,215-216:d,230-231:o", what="Retry from the pause menu")
s = session(out)
check(s and s[3] == 1, f"Retry did not count a death: {s}")

# a chapter's end: its screen, then the chapter select
out = run(["--chapter", "1", "--room", "1"], 500, "400-401:o", env={"COMPLETE_AT": "60"}, what="a chapter's end")

# every room, moving and dashing about, drawn every frame
if not QUICK:
    rooms = subprocess.run([PLAY, DATA, "--list"], capture_output=True, text=True).stdout.split("\n")
    for line in filter(None, rooms):
        c, r = line.split(" ", 1)
        run(["--chapter", c, "--room", r, "--draw", "1"], 150, "10-140:r,30-36:j,50:x,70-90:l,80-86:j,100:x", what=f"room {c} {r}")

print("\n".join(failures) if failures else "all good")
sys.exit(1 if failures else 0)
