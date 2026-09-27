# Snake

The snake game of Google's search page, for the NumWorks calculator: a blue snake glides over a checkered lawn, turns with round corners, looks where it goes and opens wide for apples.

<img src="docs/shot.png" width="320" alt="The snake about to eat an apple on the small board">

- **Smooth:** the snake moves at 60 frames a second, between cells rather than from one to the next.
- **Options:** speed (Slow, Normal, Fast), board (Small, Medium, Large), 1, 3 or 5 apples, walls, wrapping and portals.
- **Walls:** a new wall grows every two apples.
- **Wrap:** leave by one edge, come back by the other.
- **Portals:** apples come in pairs with the same colored ring. Eat one and you come out of the other, still going the same way.
- **Best scores** for every board and speed, kept with your options.

## Controls

| Key | Action |
| --- | --- |
| Arrows, or 8 4 6 2 | Turn (two turns can wait in line, and the snake never turns back on itself) |
| OK | Start, choose |
| Back | Pause (Resume, Restart, Quit game); back one step in menus |
| Home | Quit |

## Credits

Rewritten in C for NumPlay from **[Tatone26](https://github.com/Tatone26)**'s Snake in [Numworks-games](https://github.com/Tatone26/Numworks-games/tree/main/apps/snake) (All the Apps), which is released into the public domain ([UNLICENSE](UNLICENSE)). It keeps its options (speed, board size, walls every two apples, wrapping) and changes the rest:

- the look and feel of Google's Snake, drawn from shapes as it goes (no images): the lawn, the blue snake with its eyes and mouth, the apples and the band with the score and best;
- smooth movement, a two-turn input buffer, apples and walls that pop in, and a snake that stops and flashes when it crashes;
- new options from Google's game: 3 or 5 apples, and portals;
- a best score for each board and speed, and the options, saved as `snake.sav`.

Snake itself goes back to the arcade game Blockade (Gremlin, 1976) and was made famous by Nokia's phones; the look is that of Google's Snake.

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `Snake.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
