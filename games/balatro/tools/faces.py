"""Playing card faces at the calculator's card size (35x47).

Face cards (J, Q, K) are Balatro's 8-bit deck art scaled down. The corner
indices and the pips are redrawn pixel by pixel instead, because at half size
the originals blur: pips sit where the original ones do (halved), turned
upside down in the lower half like on the real cards."""
import numpy as np
from PIL import Image

W, H = 35, 47
SUITS = "HCDS"  # rows of 8BitDeck.png: Hearts, Clubs, Diamonds, Spades
COL = {"H": (0xF0, 0x34, 0x64), "C": (0x23, 0x59, 0x55), "D": (0xF0, 0x6B, 0x3F), "S": (0x3C, 0x43, 0x68)}

PIP = {
    "H": [".#.#.", "#####", "#####", ".###.", "..#.."],
    "D": ["..#..", ".###.", "#####", ".###.", "..#.."],
    "S": ["..#..", ".###.", "#####", "#####", "..#.."],
    "C": [".###.", ".###.", "#####", "#####", "..#.."],
}
# small corner suit marks (5x4 would blur the shapes; keep 5x5)
RANKS = {
    "2": ["###", "..#", "###", "#..", "###"],
    "3": ["###", "..#", ".##", "..#", "###"],
    "4": ["#.#", "#.#", "###", "..#", "..#"],
    "5": ["###", "#..", "###", "..#", "###"],
    "6": ["###", "#..", "###", "#.#", "###"],
    "7": ["###", "..#", "..#", ".#.", ".#."],
    "8": ["###", "#.#", "###", "#.#", "###"],
    "9": ["###", "#.#", "###", "..#", "###"],
    "10": ["#.###", "#.#.#", "#.#.#", "#.#.#", "#.###"],
    "J": ["..#", "..#", "..#", "#.#", "###"],
    "Q": ["###", "#.#", "#.#", "###", "..#"],
    "K": ["#.#", "#.#", "##.", "#.#", "#.#"],
    "A": [".#.", "#.#", "###", "#.#", "#.#"],
}
RANK_NAMES = ["2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"]


def stamp(img, pat, x, y, col, flip=False):
    rows = pat[::-1] if flip else pat
    for j, r in enumerate(rows):
        for i, ch in enumerate(r):
            if ch == "#" and 0 <= x + i < W and 0 <= y + j < H:
                img[y + j, x + i] = (*col, 255)


def pip_centers(cell):
    """Centers of the pips of a 1x number card (ignoring the corner indices)."""
    from scipy import ndimage
    a = cell[:, :, 3] > 128
    a[:, :13] = False
    a[:, 58:] = False
    lab, n = ndimage.label(a)
    out = []
    for k in range(1, n + 1):
        ys, xs = np.nonzero(lab == k)
        if len(ys) < 12:
            continue
        out.append(((xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2))
    return out


def corner(img, rank, suit):
    col = COL[suit]
    g = RANKS[rank]
    stamp(img, g, 2, 2, col)
    stamp(img, PIP[suit], 1 if len(g[0]) < 5 else 1, 8, col)
    # the same, upside down, bottom right
    gw = len(g[0])
    stamp(img, [r[::-1] for r in g], W - 2 - gw, H - 2 - 5, col, flip=True)
    stamp(img, [r[::-1] for r in PIP[suit]], W - 6, H - 8 - 5, col, flip=True)


def make_faces(deck_png):
    D = np.array(Image.open(deck_png).convert("RGBA"))
    faces = []
    for suit_row, suit in enumerate(SUITS):
        for r, rank in enumerate(RANK_NAMES):
            cell = D[suit_row * 95:(suit_row + 1) * 95, r * 71:(r + 1) * 71]
            img = np.zeros((H, W, 4), np.uint8)
            if rank in "JQK":
                small = np.array(Image.fromarray(cell).resize((W, H), Image.BOX))
                small[small[:, :, 3] < 110] = 0
                small[:, :, 3] = np.where(small[:, :, 3] > 0, 255, 0)
                img = small
                # clear the blurred corner indices, then redraw them
                img[0:14, 0:7] = 0
                img[H - 14:H, W - 7:W] = 0
            elif rank == "A":
                # the big pip: scaled down and made crisp
                a = cell[:, :, 3].astype(float)
                sm = np.array(Image.fromarray(a.astype(np.uint8)).resize((W, H), Image.BOX))
                sm[0:14, 0:8] = 0
                sm[H - 14:, W - 8:] = 0
                img[sm > 100] = (*COL[suit], 255)
            else:
                for cx, cy in pip_centers(cell):
                    x = int(round(cx / 2 - 2.25))
                    y = int(round(cy / 2 - 2.25))
                    stamp(img, PIP[suit], x, y, COL[suit], flip=cy > 47.5)
            corner(img, rank, suit)
            faces.append(Image.fromarray(img, "RGBA"))
    return faces


if __name__ == "__main__":
    import sys
    faces = make_faces(sys.argv[1])
    base = Image.open(sys.argv[2]).convert("RGBA").crop((71, 0, 142, 95)).resize((W, H), Image.BOX)
    sheet = Image.new("RGBA", (13 * 37, 4 * 49), (60, 90, 80, 255))
    for i, f in enumerate(faces):
        c = base.copy()
        c.alpha_composite(f)
        sheet.alpha_composite(c, ((i % 13) * 37, (i // 13) * 49))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(sys.argv[3])
