# Flappy Bird

Tap to flap, slip between the pipes, and beat your best. It looks and plays like Dong Nguyen's 2013 original, and keeps the settings and random events of Tatone26's version for when you want something different.

<img src="docs/shot.png" width="320" alt="The yellow bird between two green pipes">

- **Classic** is the real game: day or night, a yellow, blue or red bird, the Get Ready screen, the white flash when you crash, and the score board with bronze, silver, gold and platinum medals (10, 20, 30 and 40 points).
- **Custom** plays by your own rules, set in **Settings**:
  - the starting speed and how often it goes up, how close the pipes are (Sparse to Extreme), the gap (Easy, Normal or Hard), moving pipes, and how strong a flap is (Floaty, Bouncy, Normal, Snappy or Heavy);
  - random events, from rare to frequent: a pipe surge, tailwind and headwind, narrow or wide gaps, dense pipes, low and high gravity. Each one is announced on the ground as it comes, and they can stack if you like;
  - No collisions, to practise (those scores don't count).
- The bird and the sky can be chosen in Settings too, for both games.

## Controls

| Key | Action |
| --- | --- |
| OK, EXE, Up, 8 or 5 | Flap |
| Back | Pause: Resume, Restart or Quit game |
| Arrows, OK | Menus and settings |
| Home | Leave |

Your best scores (one for Classic, one for Custom) and your settings are kept in `flappy.sav`.

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `FlappyBird.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

## Credits

Flappy Bird was made by Dong Nguyen (.GEARS, 2013). This version is written in C for NumPlay, inspired by Tatone26's in [Numworks-games](https://github.com/Tatone26/Numworks-games/tree/main/apps/flappybird) (All the Apps), with no code copied from it. Compared with that one:

- The look of the original: the sky with clouds, a city and bushes, by day or by night; the green pipes; the striped ground; a bird in three colours that flaps and tilts; the big outlined score; the title, Get Ready, Game Over and score board screens, with a fade between them.
- A Classic game with the original's rules next to the Custom one, each with its own best score.
- The events are announced by a tag on the ground instead of the sunflowers; moving pipes are Off, Rarely, Often, Slow or Fast; the gap setting also changes the gap's size.
- Smooth at 60 frames a second: nothing in the background moves, so only the pipes, the bird and the ground are redrawn. The whole game is about 16 KB.

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
