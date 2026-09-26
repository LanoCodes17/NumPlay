#!/usr/bin/env python3
"""Prints how NumPlay.nwa's flash and RAM are shared between the games."""
import json
import os
import subprocess
import sys

nwa, mods = sys.argv[1], sys.argv[2]
out = subprocess.run(["arm-none-eabi-nm", "-S", nwa], capture_output=True, text=True).stdout
sym = {}
for line in out.splitlines():
    parts = line.split()
    if len(parts) >= 3:
        sym[parts[-1]] = int(parts[0], 16)
secs = subprocess.run(["arm-none-eabi-size", "-A", nwa], capture_output=True, text=True).stdout
total = 0
rows = []
for line in secs.splitlines():
    p = line.split()
    if len(p) >= 2 and p[0].startswith(".rodata.np."):
        rows.append((p[0].split(".")[-1], int(p[1])))
        total += int(p[1])
print("NumPlay.nwa games (flash, including screenshots):")
for name, size in rows:
    info = json.load(open(os.path.join(mods, name + ".json")))
    print(f"  {name:12s} {size / 1024:7.1f} KB   RAM {(info['data'] + info['bss']) / 1024:6.1f} KB")
print(f"  {'all games':12s} {total / 1024:7.1f} KB")
