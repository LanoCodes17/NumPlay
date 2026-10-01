# 2048

Slide the tiles, join equal numbers, get to 2048. Gabriele Cirulli's puzzle for the NumWorks calculator, looking and moving like the original.

<img src="docs/shot.png" width="320" alt="A game of 2048 in progress">

- **Four board sizes:** 3×3, 4×4 (the classic), 5×5 and 6×6. On 3×3 the goal is 512.
- **Undo:** take back your last move, even from the Game over screen.
- **Keep going** after 2048: the tiles go on to 4096, 8192 and beyond.
- Your game and your best score are kept for each size, so you can stop and carry on later.
- Tiles slide, pop when they merge and zoom in when they appear, the big ones glow, and the points you win rise from the score.

## Controls

| Key | Action |
| --- | --- |
| Arrows, or 8 4 6 2 | Slide the tiles |
| ⌫ | Undo |
| Back | Pause: Resume, New game, Quit game |
| OK | Choose |

On the title screen, **Left / Right** pick the board size and **Up / Down** choose between Continue and New game.

Everything is drawn with code, and all the text comes from one small stroke font drawn with a round pen at any size, so the whole game is about 12 KB.

## Credits

2048 is **[Gabriele Cirulli](https://github.com/gabrielecirulli/2048)**'s game (2014), itself based on 1024 by Veewo Studio and similar in concept to Threes by Asher Vollmer.

This version is written in C for NumPlay, inspired by **[Tatone26](https://github.com/Tatone26)**'s Python 2048 in [Numworks-games](https://github.com/Tatone26/Numworks-games/blob/main/python%20games/g2048.py) (All the Apps), with no code copied from it. Compared with that one:

- the look of Cirulli's original: its colours, rounded tiles and bold numbers, the score and best boxes, and the You win! and Game over! messages;
- smooth animations, and Undo;
- winning, then keeping on after 2048;
- the game in progress and the best score saved for each size;
- sizes from 3×3 to 6×6 (7×7 and 8×8 left out: their numbers would be too small to read).

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `2048.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
