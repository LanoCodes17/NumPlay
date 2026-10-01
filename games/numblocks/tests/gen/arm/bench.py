#!/usr/bin/env python3
"""Builds bench.c + gen.c for the Cortex-M7 (-Os, hard float) and runs it in Unicorn,
counting instructions per gen_slab call (1 instruction ~ 1 cycle at 216 MHz, the same
convention as tools/emu.py). Also reports the deepest stack use (painted stack).
Usage: bench.py [OUTDIR]"""
import os
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "..", "src")
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp"
ELF = os.path.join(OUT, "gen_bench.elf")
CFLAGS = ["-mcpu=cortex-m7", "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard", "-mthumb", "-Os", "-std=c11",
          "-Wall", "-Wextra", "-Wdouble-promotion", "-ffunction-sections", "-fdata-sections"]
subprocess.check_call(["arm-none-eabi-gcc", *CFLAGS, "-nostartfiles", "--specs=nano.specs", "--specs=nosys.specs",
                       "-T", os.path.join(HERE, "bench.ld"), "-Wl,--gc-sections",
                       os.path.join(HERE, "bench.c"), os.path.join(SRC, "gen.c"), "-lm", "-o", ELF])
subprocess.call(["arm-none-eabi-size", ELF])

syms = {}
with open(ELF, "rb") as f:
    elf = ELFFile(f)
    for s in elf.get_section_by_name(".symtab").iter_symbols():
        syms[s.name] = s["st_value"]
    segs = [(p["p_paddr"], p.data()) for p in elf.iter_segments() if p["p_type"] == "PT_LOAD"]

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.mem_map(0x08000000, 1 << 20)
uc.mem_map(0x20000000, 512 << 10)
uc.mem_map(0xE0000000, 1 << 20)
for addr, data in segs:
    uc.mem_write(addr, data)
STACK_TOP = 0x20000000 + (512 << 10)
PAINT = 64 << 10
uc.mem_write(STACK_TOP - PAINT, b"\xa5" * PAINT)
vec = uc.mem_read(0x08000000, 8)
sp = int.from_bytes(vec[0:4], "little")
pc = int.from_bytes(vec[4:8], "little")
uc.reg_write(UC_ARM_REG_SP, sp)

CHUNK = 21600  # 0.1 ms
insns = 0
marks = []
last_phase = 0
phase_addr = syms["phase"]
while True:
    uc.emu_start(pc | 1, 0xFFFFFFFF, count=CHUNK)
    insns += CHUNK
    pc = uc.reg_read(UC_ARM_REG_PC)
    ph = int.from_bytes(uc.mem_read(phase_addr, 4), "little")
    if ph != last_phase:
        marks.append((ph, insns))
        last_phase = ph
    op = uc.mem_read(pc & ~1, 2)
    if op == b"\x00\xbe":  # bkpt
        break
    if insns > 20_000_000_000:
        break

prev = 0
names = ["gen_init"] + [f"gen_slab 0..128 #{i}" for i in range(16)] + ["gen_slab 32..64", "gen_spawn"]
slabs = []
for i, (ph, n) in enumerate(marks):
    d = n - prev
    prev = n
    name = names[i] if i < len(names) else f"phase {ph}"
    if name.startswith("gen_slab 0"):
        slabs.append(d)
    print(f"{name:22s} {d / 1e6:8.2f} M insns  ~{d / 216e3:7.1f} ms at 216 MHz")
if slabs:
    print(f"gen_slab 0..128: avg {sum(slabs) / len(slabs) / 216e3:.1f} ms, max {max(slabs) / 216e3:.1f} ms")
st = uc.mem_read(STACK_TOP - PAINT, PAINT)
used = PAINT - next(i for i in range(PAINT) if st[i] != 0xA5)
print(f"deepest stack: {used} bytes")
sums = uc.mem_read(syms["sums"], 64)
print("checksums:", " ".join(f"{int.from_bytes(sums[i:i + 4], 'little'):08x}" for i in range(0, 64, 4)))
sp3 = uc.mem_read(syms["spawn"], 12)
print("spawn:", [int.from_bytes(sp3[i:i + 4], "little", signed=True) for i in range(0, 12, 4)])
