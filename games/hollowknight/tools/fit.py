"""Fits each room's texture sizes to the calculator's tile cache: packs, then draws every camera position of every
room on the computer (tests/play.c --sweep2d), and makes the textures of rooms whose busiest view needs more tiles than
BUDGET smaller, until all fit. Writes fit.json (read by pack.py)."""
import os, sys, json, subprocess, math, multiprocessing as mp
HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "..")
BUDGET = int(os.environ.get("HK_TILE_BUDGET", "520"))   # tex.c NSLOTS, less what the Knight, enemies and effects need
ROOMS = [l.strip() for l in open(os.path.join(HERE, "rooms.txt")) if l.strip() and not l.startswith("#")]
EXE = os.path.join(GAME, "build", "sweep")


def build():
    src = ["src/res.c", "src/tex.c", "src/room.c", "src/gfx.c", "src/lzma/LzmaDec.c", "src/plat_host.c", "tests/play.c"]
    subprocess.check_call(["cc", "-std=gnu11", "-O2", "-Isrc", "-DHOST", "-DNSLOTS=4000"] + src + ["-lm", "-o", EXE], cwd=GAME)


def sweep(room):
    out = subprocess.run([EXE, "src/data.bin", "--room", room, "--sweep2d", "2"], cwd=GAME, capture_output=True, text=True).stdout.split()
    return room, (int(out[1]) if len(out) >= 2 else 0)


if __name__ == "__main__":
    fp = os.path.join(HERE, "fit.json")
    fit = json.load(open(fp)) if os.path.exists(fp) else {}
    build()
    for it in range(6):
        subprocess.check_call([sys.executable, os.path.join(HERE, "pack.py")], stdout=subprocess.DEVNULL)
        with mp.Pool(4) as p:
            res = dict(p.map(sweep, ROOMS))
        over = {r: n for r, n in res.items() if n > BUDGET}
        print("round", it, "over:", over, flush=True)
        if not over:
            break
        for r, n in over.items():
            fit[r] = round(fit.get(r, 1.0) * math.sqrt(BUDGET / n) * 0.98, 3)
        json.dump(fit, open(fp, "w"), indent=1, sort_keys=True)
    print("fit", fit)
