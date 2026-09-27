> **Connect 4 is part of [NumPlay](../../README.md)**, every NumWorks game in one app. Get it with all the others, or on its own as `ConnectFour.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

# Connect 4

The classic four-in-a-row game for the NumWorks calculator. Drop red and yellow discs into the blue grid; the first to line up four, across, down or diagonally, wins.

<img src="docs/shot.png" width="320" alt="A game in progress on the blue grid">

- **Against a friend or the computer**, at three levels. Weak makes mistakes, Normal looks a few moves ahead, and Strong is hard to beat: it thinks for up to a second and knows the row-parity rules that decide endgames.
- **Two or three players.** Three players (red, yellow and green) play on a wider grid, 8 columns instead of 7. Against the computer, you play red against two of them.
- **The real thing**: the blue plastic grid with its round holes, discs with a raised rim that fall behind the grid with a little bounce, the winning four lit up, and all the discs dropping out of the bottom before the next round.
- **A tally** of wins for each player, kept between sessions (it starts over when you change the opponent, the players or the level). Who starts takes turns each round.
- **Undo** a move you regret, and a **dark theme**.

## Controls

| Key | Action |
| --- | --- |
| Left / Right | Choose a column |
| OK, Down or EXE | Drop the disc |
| 1 to 8 | Drop straight into that column |
| ⌫ | Undo (against the computer: your last move and its answer) |
| Back | Pause: Resume, Undo move, Restart, Quit game |
| Home | Leave |

In the menu, Up and Down pick a line and Left / Right change it.

## Credits

Rewritten in C for NumPlay from **[Tatone26](https://github.com/Tatone26)**'s version in [Numworks-games](https://github.com/Tatone26/Numworks-games/tree/main/apps/connectfour) (All the Apps), which is released into the public domain ([UNLICENSE](UNLICENSE)). Its modes are all here: solo or not, two or three players on the same grid sizes, three computer levels, and the dark mode. What changed:

- It looks like the Milton Bradley / Hasbro game (invented by Howard Wexler and Ned Strongin, 1974): the blue grid, embossed discs that fall with gravity, instead of flat discs on a plain grid.
- A new computer player: bitboards, alpha-beta search with a transposition table, moves that make threats tried first, and an evaluation that knows which rows win endgames. It answers in under a second (the original's strongest level could take many seconds), and a disc sways over the board while it thinks.
- Undo, a tally of wins, alternating first player, the winning four highlighted, the discs dropping out between rounds, a pause menu, and saved settings.

## Build

`make` (needs `arm-none-eabi-gcc` and Node.js for nwlink) writes `output/connectfour.nwa`.
