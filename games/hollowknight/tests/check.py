"""The host build's checks (make test): check.py build/play src/data.bin

- every room's hazard respawn points: ground under them, in the room
- every gate: the Knight arrives in the next room, in control, on the ground, and can move; and spikes there send him
  back to a good place
- a long walk of made up keys from every room: the Knight never out of the room, without control, wedged, undrawn or
  out of the view
- every room played with its enemies hit, in a new game and after the bosses, drawn whole and fast (as a slow
  calculator draws it): the sanitizers stop on any fault
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

PLAY, DATA = sys.argv[1], sys.argv[2]
WALKS = [(seed, 4000) for seed in range(1, 5)]   # (seed, ticks of 20 ms)
SWEEP_KEYS = "_*30,R*120,A*3,J*25,L*200,A*4,_*60,R*60,J*30,A*3,_*900"
WORLDS = ("", "7,11")   # PlayerData bools on, by number: none, then the False Knight and Hornet beaten
JOBS = os.cpu_count() or 2


def play(*args, env=None):
    p = subprocess.run([PLAY, DATA] + list(args), capture_output=True, text=True, env=dict(os.environ, **(env or {})))
    return p.returncode, p.stdout + p.stderr


def faults(out):
    return [l for l in out.splitlines() if "AddressSanitizer" in l or "runtime error" in l or l.startswith("SUMMARY")]


def report(name, rc, out, bad_prefixes):
    bad = [l for l in out.splitlines() if l.startswith(bad_prefixes)] + faults(out)
    ok = rc == 0 and not bad
    print("%s %s" % ("ok  " if ok else "FAIL", name))
    for l in bad[:20]:
        print("     " + l)
    return ok


def main():
    results = []
    rc, out = play("--gates", "markers")
    results.append(report("hazard respawn points", rc, out, ("BAD",)))
    rc, out = play("--gates", "all")
    results.append(report("gates", rc, out, ("FAIL",)))
    with ThreadPoolExecutor(JOBS) as pool:
        walks = list(pool.map(lambda w: play("--gates", "wander:all:%d:%d" % w, env={"HKFAST": "1"} if w[0] % 2 else None), WALKS))
    for (seed, ticks), (rc, out) in zip(WALKS, walks):
        results.append(report("walk %d (%d s from every room%s)" % (seed, ticks // 50, ", drawn fast" if seed % 2 else ""), rc, out, ("FAIL",)))
    rc, out = play("--list")
    rooms = out.split()
    runs = [(r, w, f) for r in rooms for w in WORLDS for f in ("", "1")]
    env = {"HKGOD": "1", "HKHIT": "50", "HKHITN": "20", "HKHITD": "10"}
    with ThreadPoolExecutor(JOBS) as pool:
        played = list(pool.map(lambda rwf: play("--room", rwf[0], "--play", SWEEP_KEYS, "--shot", os.devnull,
                                                env=dict(env, HKFLAGS=rwf[1], **({"HKFAST": "1"} if rwf[2] else {}))), runs))
    bad = [(r, w, f, rc, out) for (r, w, f), (rc, out) in zip(runs, played) if rc != 0 or faults(out)]
    print("%s every room played (%d runs)" % ("FAIL" if bad else "ok  ", len(runs)))
    for r, w, f, rc, out in bad:
        print("     %s [%s]%s exit %d" % (r, w, " fast" if f else "", rc))
        for l in faults(out)[:3]:
            print("       " + l)
    results.append(not bad)
    print("%d of %d checks failing" % (results.count(False), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
