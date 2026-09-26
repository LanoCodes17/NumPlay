#!/usr/bin/env python3
"""Plays Tetris in the ARM emulator for the README's GIF.

Runs the real calculator build (Tetris.nwa) in tools/emu.py, reads the board
and the NEXT panel from the screen, and places each piece with a classic
heuristic (few holes, low and flat stack, clear lines). Saves raw frames that
tools/record.py turns into a GIF.

Usage: tetris_bot.py Tetris.nwa out_dir [--pieces 40] [--fps 25]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import emu  # noqa: E402

FIELD_X, FIELD_Y, CELL, W, H = 100, 0, 12, 10, 20
# tile colours (centre pixel, RGB565) -> piece types, as in tetriminos.rs
TYPES = "TJZOSLI"
COLOR_OF = {"T": 6, "J": 1, "Z": 4, "O": 0, "S": 3, "L": 2, "I": 5}
BLOCKS = {
    "T": [[(0, 0), (-1, 0), (1, 0), (0, -1)], [(0, 0), (1, 0), (0, 1), (0, -1)], [(0, 0), (-1, 0), (1, 0), (0, 1)],
          [(0, 0), (-1, 0), (0, 1), (0, -1)]],
    "J": [[(-1, 0), (-1, -1), (0, 0), (1, 0)], [(0, 1), (0, 0), (0, -1), (1, -1)], [(-1, 0), (0, 0), (1, 0), (1, 1)],
          [(-1, 1), (0, 1), (0, 0), (0, -1)]],
    "Z": [[(-1, -1), (0, -1), (0, 0), (1, 0)], [(0, 0), (0, 1), (1, 0), (1, -1)], [(-1, 0), (0, 0), (0, 1), (1, 1)],
          [(-1, 1), (-1, 0), (0, 0), (0, -1)]],
    "O": [[(0, 0), (-1, 0), (-1, 1), (0, 1)]] * 4,
    "S": [[(-1, 0), (0, 0), (0, -1), (1, -1)], [(0, 0), (1, 1), (1, 0), (0, -1)], [(1, 0), (0, 0), (0, 1), (-1, 1)],
          [(-1, -1), (-1, 0), (0, 0), (0, 1)]],
    "L": [[(-1, 0), (0, 0), (1, 0), (1, -1)], [(1, 1), (0, 1), (0, 0), (0, -1)], [(-1, 0), (-1, 1), (0, 0), (1, 0)],
          [(0, 1), (0, 0), (0, -1), (-1, -1)]],
    "I": [[(0, 0), (1, 0), (-1, 0), (-2, 0)], [(0, 0), (0, -1), (0, 1), (0, 2)], [(0, 1), (1, 1), (-1, 1), (-2, 1)],
          [(-1, 0), (-1, -1), (-1, 1), (-1, 2)]],
}
SPAWN_X = 5


def pixel(calc, x, y):
    return struct.unpack_from("<H", calc.screen, (y * 320 + x) * 2)[0]


def classify(c):
    """Piece colour of a tile centre, or None for empty and ghost cells."""
    r, g, b = (c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31
    if max(r, g, b) < 90:
        return None
    if r > 200 and g > 180 and b < 80:
        return "O"
    if r > 200 and 80 < g < 160 and b < 60:
        return "L"
    if r > 200 and g < 60:
        return "Z"
    if g > 180 and r < 120 and b < 80:
        return "S"
    if b > 200 and g > 180:
        return "I"
    if b > 150 and r < 80 and g < 80:
        return "J"
    if r > 70 and b > 120 and g < 60:
        return "T"
    return "?"


def read_board(calc):
    board = []
    for y in range(H):
        row = []
        for x in range(W):
            row.append(classify(pixel(calc, FIELD_X + x * CELL + CELL // 2 + 1, FIELD_Y + y * CELL + CELL // 2 + 1)))
        board.append(row)
    return board


def read_next(calc):
    """Colour of the piece in the NEXT panel (left, top)."""
    seen = {}
    for y in range(40, 110, 3):
        for x in range(14, 84, 3):
            t = classify(pixel(calc, x, y))
            if t and t != "?":
                seen[t] = seen.get(t, 0) + 1
    return max(seen, key=seen.get) if seen else None


def fits(board, piece, rot, px, py):
    for dx, dy in BLOCKS[piece][rot]:
        x, y = px + dx, py + dy
        if x < 0 or x >= W or y >= H:
            return False
        if y >= 0 and board[y][x]:
            return False
    return True


def drop(board, piece, rot, px):
    py = -2
    if not fits(board, piece, rot, px, py):
        return None
    while fits(board, piece, rot, px, py + 1):
        py += 1
    new = [row[:] for row in board]
    for dx, dy in BLOCKS[piece][rot]:
        if py + dy < 0:
            return None
        new[py + dy][px + dx] = piece
    cleared = [r for r in new if all(r)]
    new = [r for r in new if not all(r)]
    new = [[None] * W for _ in range(len(cleared))] + new
    return new, len(cleared)


def score(board, lines):
    heights = []
    holes = 0
    for x in range(W):
        h = 0
        for y in range(H):
            if board[y][x]:
                h = H - y
                break
        heights.append(h)
        top = H - h
        holes += sum(1 for y in range(top, H) if not board[y][x])
    bump = sum(abs(heights[i] - heights[i + 1]) for i in range(W - 1))
    return -0.51 * sum(heights) + 0.76 * lines - 0.36 * holes - 0.18 * bump


def best_move(board, piece):
    best = None
    for rot in range(4 if piece != "O" else 1):
        for px in range(-2, W + 2):
            r = drop(board, piece, rot, px)
            if not r:
                continue
            s = score(*r)
            if best is None or s > best[0]:
                best = (s, rot, px)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nwa")
    ap.add_argument("out")
    ap.add_argument("--pieces", type=int, default=40)
    ap.add_argument("--fps", type=int, default=25)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    c = emu.Calculator(a.nwa)
    frame = [0]

    def grab(calc):
        path = os.path.join(a.out, f"{frame[0]:06d}_{int(calc.now_ms):06d}.raw")
        open(path, "wb").write(bytes(calc.screen))
        frame[0] += 1

    def run_until(ms):
        c.run(ms, grab, 1000 / a.fps)

    def tap(key, down=35, up=45):
        t = c.now_ms
        c.keys.append((t, t + down, emu.KEYS[key]))
        run_until(t + down + up)

    run_until(900)
    tap("ok")                      # Play
    run_until(c.now_ms + 1500)
    # the first piece: wait until it shows up
    current = None
    while current is None:
        run_until(c.now_ms + 100)
        for row in read_board(c)[:3]:
            for cell in row:
                if cell and cell != "?":
                    current = cell
    for n in range(a.pieces):
        board = [[cell if cell != "?" else "X" for cell in row] for row in read_board(c)]
        # the falling piece is in the top rows: remove it from the board
        for y in range(4):
            for x in range(W):
                if board[y][x] == current:
                    board[y][x] = None
        nxt = read_next(c)
        move = best_move(board, current)
        if os.environ.get("BOT_DEBUG"):
            for row in board[12:]:
                print("".join((cell or ".")[0] for cell in row))
            print("current", current, "next", nxt, "move", move)
        if not move:
            break
        _, rot, px = move
        if rot == 3:
            tap("ok")
        else:
            for _ in range(rot):
                tap("back")
        dx = px - SPAWN_X
        for _ in range(abs(dx)):
            tap("left" if dx < 0 else "right")
        tap("up", 40, 160)
        if os.environ.get("BOT_DEBUG"):
            after = read_board(c)
            landed = [(x, y) for y in range(H) for x in range(W) if after[y][x] and not board[y][x]]
            expect = drop(board, current, rot, px)
            print(f"{current} rot {rot} x {px}: landed {sorted(landed)[:4]}")
        current = nxt
    run_until(c.now_ms + 1200)
    print(f"tetris_bot: {frame[0]} frames, {n + 1} pieces")


if __name__ == "__main__":
    main()
