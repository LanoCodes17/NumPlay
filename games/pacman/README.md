# Pac-Man

The 1980 arcade game for the NumWorks calculator: the maze, the four ghosts, the fruit, and the rules of the original.

<img src="docs/shot.png" width="320" alt="Pac-Man in the maze, chased by Blinky and Pinky">

- **The arcade's maze**, all 28 x 31 tiles with its blue double walls, the pink ghost house door, dots and flashing energizers. The scores sit at the sides: 1UP, HIGH SCORE, your spare lives and the fruit of the last levels.
- **Four ghosts, four personalities**, as in the arcade: Blinky goes straight for you, Pinky aims four tiles ahead, Inky works from Blinky's position, and Clyde gets shy up close. They scatter to their corners and chase in waves, turn blue and flee after an energizer, and race home as eyes once eaten (200, 400, 800, 1600).
- **The original's rules**: the speed and fright tables of every level, Blinky's "Cruise Elroy" speed-up near the end of a level, the ghost house counters, the slow tunnel, cornering, fruit from the cherry to the key, an extra life, the death animation, the flashing maze and the first intermission.
- **Options** from the All the Apps version: game speed, lives (1, 2, 3 or 5), how long a turn is remembered (input buffer), the level to start from, the score for the bonus life, and No collisions (no high score then).
- **Quit any time and continue later**: the level, score, lives and every dot left are kept, with the high score and the options.

## Controls

| Key | Action |
| --- | --- |
| Arrows, or 8 4 6 2 | Steer. Press a turn early and Pac-Man takes it at the next gap |
| Back | Pause: Resume, Restart, Quit game |
| OK | Choose in the menus, skip the intermission |
| Home | Leave (the game is saved) |

## Credits

Pac-Man was created by Toru Iwatani at Namco in 1980; this remake follows the arcade's look and rules.

It is written in C for NumPlay, inspired by Tatone26's version in [Numworks-games](https://github.com/Tatone26/Numworks-games/tree/main/apps/pacman) (All the Apps), with no code copied from it. Compared with that one:

- The full arcade maze (the ghost house was one row shorter to fit the screen) drawn with 7-pixel tiles, and the arcade's colours, sprites, screens and HUD instead of the original's.
- The arcade's ghost behaviour, wave timers, house counters, Cruise Elroy, dot pauses and fruit timings.
- Options added: 5 lives, start levels up to 21, the bonus life score. The saved game now keeps the dots left.
- The first intermission, the attract screen with the ghosts' names, and 60 frames a second that redraw only what moves.
- About 14 KB, with every picture drawn by code or packed at 2 bits a pixel.

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `PacMan.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
