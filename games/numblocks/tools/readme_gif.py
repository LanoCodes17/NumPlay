#!/usr/bin/env python3
"""Records docs/media/numblocks.gif, the README's NumBlocks clip, with the host player (make host):
a walk across a meadow, the inventory, TNT lit and a crater, water poured in it, lava flowing to it.

Usage (from games/numblocks): python3 tools/readme_gif.py [out.gif]
Needs ffmpeg and gifsicle."""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLAY = os.path.join(HERE, "..", "build", "play")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "..", "docs", "media", "numblocks.gif")
FRAMES = 440   # 50 ms each in the game, shown at 15 a second
K = []


def hold(a, b, k):
    K.append(f"{a}-{b}:{k}")


def tap(a, k):
    K.append(f"{a}-{a + 1}:{k}")


# A: walk across the meadow, turning a little, one hop
hold(4, 40, "fwd"); hold(14, 24, "right"); tap(22, "jump")
# B: the inventory
tap(48, "inv"); hold(58, 59, "right"); hold(66, 67, "down"); hold(74, 75, "right"); tap(90, "inv")
# C: TNT a few blocks ahead, lit, then back off and watch it go
tap(98, "1"); hold(100, 106, "down"); tap(110, "use"); tap(116, "2"); tap(120, "use")
hold(124, 168, "back"); hold(126, 129, "up")
# D: walk up to the crater and look in
hold(214, 228, "fwd"); hold(230, 238, "down")
# E: fill it with water
tap(242, "3"); tap(246, "use")
# F: lava on the grass a little back from the rim: it flows in and hardens where it meets the water
hold(282, 300, "right"); hold(302, 307, "up"); tap(310, "4"); tap(314, "use")
hold(322, 344, "sright"); hold(346, 355, "left"); hold(348, 351, "up")

with tempfile.TemporaryDirectory() as d:
    subprocess.run([PLAY, "--frames", str(FRAMES), "--seed", "99", "--showcase", "--keys", ",".join(K),
                    "--shots", ",".join(str(f) for f in range(FRAMES)), "--out", d], check=True, capture_output=True)
    n = 0
    for k in range(FRAMES * 15 // 20):
        shutil.copy(os.path.join(d, f"shot_{round(k * 20 / 15)}.ppm"), os.path.join(d, f"s_{n}.ppm"))
        n += 1
    raw = os.path.join(d, "raw.gif")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-framerate", "15", "-i", os.path.join(d, "s_%d.ppm"), "-vf",
                    "scale=640:480:flags=neighbor,split[a][b];[a]palettegen=max_colors=192:stats_mode=diff[p];"
                    "[b][p]paletteuse=dither=none:diff_mode=rectangle", raw], check=True)
    subprocess.run(["gifsicle", "-O3", "--lossy=40", raw, "-o", OUT], check=True)
print(f"{OUT}: {os.path.getsize(OUT) // 1024} KB")
