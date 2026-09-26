#!/usr/bin/env python3
"""Records NumPlay in the official Epsilon simulator (headless).

The simulator build (NumPlay.nwb) follows a scripted keyboard and saves the
screen as it runs (see launcher/sim/sim.c); this turns the frames into PNG
screenshots and GIFs.

Usage:
  record.py --simulator PATH/Epsilon --nwb build/NumPlay.nwb \
      --keys "1500-1600:right,3000:ok" --out docs/raw \
      [--png 1200:home.png,3400:game.png] [--gif 500-4000:carousel.gif] [--fps 30] [--scale 2]
"""
import argparse
import glob
import os
import subprocess
import tempfile

import numpy as np
from PIL import Image


def load_frames(d):
    frames = []
    for path in sorted(glob.glob(os.path.join(d, "*.raw"))):
        t = int(os.path.basename(path).split("_")[1].split(".")[0])
        a = np.fromfile(path, dtype="<u2").reshape(240, 320)
        rgb = np.stack([((a >> 11) & 31) * 255 // 31, ((a >> 5) & 63) * 255 // 63, (a & 31) * 255 // 31], -1)
        frames.append((t, Image.fromarray(rgb.astype("uint8"), "RGB")))
    return frames


def save_gif(frames, path, scale, fps):
    ims = [im.resize((320 * scale, 240 * scale), Image.NEAREST) if scale != 1 else im for _, im in frames]
    # keep the timing of the recording
    durations = []
    for i, (t, _) in enumerate(frames):
        nxt = frames[i + 1][0] if i + 1 < len(frames) else t + 1000 // fps
        durations.append(max(20, nxt - t))
    tmp = path + ".tmp.gif"
    ims[0].save(tmp, save_all=True, append_images=ims[1:], duration=durations, loop=0, optimize=False, disposal=1)
    try:
        subprocess.run(["gifsicle", "-O3", "--lossy=30", "--colors", "256", tmp, "-o", path], check=True)
        os.remove(tmp)
    except (FileNotFoundError, subprocess.CalledProcessError):
        os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--simulator", default=os.environ.get("EPSILON"))
    ap.add_argument("--nwb", default="build/NumPlay.nwb")
    ap.add_argument("--keys", required=True)
    ap.add_argument("--end", type=int, default=0)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--out", required=True)
    ap.add_argument("--png", default="")
    ap.add_argument("--gif", action="append", default=[])
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--keep", help="also save every frame as PNG in this directory")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as d:
        state = os.path.join(d, "events.nws")
        open(state, "wb").write(b"\x04")
        env = dict(os.environ, NUMPLAY_SCRIPT=a.keys, NUMPLAY_FRAMES=d, NUMPLAY_FPS=str(a.fps))
        if a.end:
            env["NUMPLAY_END_MS"] = str(a.end)
        # asking for a screenshot is what turns on the headless simulator's frame buffer
        subprocess.run([a.simulator, "--headless", "--headless-state-file", "--load-state-file", state,
                        "--take-screenshot", os.path.join(d, "last.png"), "--nwb", os.path.abspath(a.nwb)],
                       env=env, check=False, timeout=600)
        frames = load_frames(d)
    if not frames:
        raise SystemExit("record: no frames (is the simulator path right?)")
    print(f"record: {len(frames)} frames over {frames[-1][0] / 1000:.1f} s")
    if a.keep:
        os.makedirs(a.keep, exist_ok=True)
        for t, im in frames:
            im.save(os.path.join(a.keep, f"{t:06d}.png"))
    for spec in filter(None, a.png.split(",")):
        t, name = spec.split(":")
        t = int(t)
        best = min(frames, key=lambda f: abs(f[0] - t))
        im = best[1]
        if a.scale != 1:
            im = im.resize((320 * a.scale, 240 * a.scale), Image.NEAREST)
        im.save(os.path.join(a.out, name))
    for spec in a.gif:
        rng, name = spec.split(":")
        t0, t1 = (int(x) for x in rng.split("-"))
        part = [f for f in frames if t0 <= f[0] <= t1]
        save_gif(part, os.path.join(a.out, name), a.scale, a.fps)
        print(f"record: {name}: {len(part)} frames, {os.path.getsize(os.path.join(a.out, name)) // 1024} KB")


if __name__ == "__main__":
    main()
