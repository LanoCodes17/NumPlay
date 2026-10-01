# Snake

Google's Snake, the game you get when you search for "snake", on the NumWorks calculator: the checkered lawn, the blue snake with its big eyes and its shadow, the apple, the sky-blue menu card and the settings with all their little pictures.

<img src="docs/shot.png" width="320" alt="The snake about to eat an apple">

- **Google's boards:** Normal (17 x 15), Small (10 x 9) and Large (24 x 21).
- **12 modes, like Google's:**
  - **Classic**
  - **Wall:** a wall grows for every fruit.
  - **Portal:** fruit comes in pairs with the same colored ring. Eat one and you come out of the other, still going the same way.
  - **Cheese:** the snake is full of holes, and you can go through them.
  - **Borderless:** leave by one edge, come back by the other.
  - **Twin:** each fruit swaps your head and tail, so you set off back the way you came.
  - **Winged:** fruit flies around the lawn and bounces off the edges and the snake.
  - **Yin Yang:** a black snake mirrors every move you make. Neither may crash.
  - **Statue:** each fruit turns your body to stone behind you.
  - **Light:** it's dark, except around your head; fruit lights it up for a while.
  - **Magnet:** fruit near your head is pulled into your mouth.
  - **Peaceful:** you can't crash.
- **Settings, like Google's:** the fruit (apple, banana, pineapple, grapes, strawberry, cherries), 1, 3 or 5 fruits at a time, the speed (normal, fast, slow), the board, 8 snake colors and 4 themes (day, night, snow, volcano). **Shuffle** picks them all at random; **Reset** puts them back.
- **Smooth:** the snake glides at 60 frames a second, turns with round corners and opens wide for fruit.
- **Best scores** for every mode, number of fruits, speed and board, kept with your settings.

## Controls

| Key | Action |
| --- | --- |
| Arrows, or 8 4 6 2 | Turn (two turns can wait in line, and the snake never turns back on itself) |
| OK | Start, choose |
| Left, Right | Change a setting |
| Back | Pause (Resume, Restart, Quit game); back one step in menus |
| Home | Quit |

## Credits

The look, the modes and the settings follow **Google's Snake** (Google Search, 2017 onwards). This is an independent fan version, drawn from shapes as it goes (no images from Google), and it isn't affiliated with or endorsed by Google.

It is written in C for NumPlay, inspired by **[Tatone26](https://github.com/Tatone26)**'s Snake in [Numworks-games](https://github.com/Tatone26/Numworks-games/tree/main/apps/snake) (All the Apps), with no code copied from it. Its options (speed, board size, walls, wrapping) are all here, as Google's.

Snake itself goes back to the arcade game Blockade (Gremlin, 1976) and was made famous by Nokia's phones.

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `Snake.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
