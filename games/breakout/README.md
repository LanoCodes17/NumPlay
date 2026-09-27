# Breakout

The brick breaker, for the NumWorks calculator: Atari's 1976 arcade game as it was, and an **Arcade+** mode in the spirit of Arkanoid.

<img src="docs/shot.png" width="320" alt="Classic Breakout: the wall of bricks, the ball and the paddle">

- **Classic** plays like the arcade: eight rows of bricks in red, orange, green and yellow (7, 5, 3 and 1 points), three balls, and a second wall once you clear the first. The ball speeds up after 4 and 12 hits and when it reaches the orange and red rows, and your paddle shrinks to half once the ball breaks through to the top wall. Like on the real cabinet, where colour came from strips of film on the screen, the ball takes the colour of the rows it flies through.
- **Arcade+** has 12 hand-made rounds with silver bricks that take a few hits, gold ones that never break, and capsules that fall from broken bricks: **E**nlarge, **C**atch, **L**aser, **S**low, **D**isruption (three balls), **P**layer (an extra life) and **B**reak, which opens a way out to the next round. Pick any round you have reached from the title screen.

Where the ball lands on the paddle decides where it goes. Bricks burst into sparks and the wall rattles when you hit it, the whole screen shakes when you lose a ball, and the ball leaves a short trail.

## Controls

| Key | Action |
| --- | --- |
| Left / Right, or 4 / 6 | Move the paddle (tap to nudge it, hold to speed up) |
| OK or Up | Launch the ball, fire the laser, let go of a caught ball |
| Back | Pause: Resume, Restart, Quit game |

**Settings:** difficulty (Easy, Normal, Hard: the ball's speed and the paddle's size), screen shake, and the ball's trail. Your best score in each mode, the rounds you have reached and your settings are kept for next time (`breakout.sav`).

It comes inside [NumPlay](../../README.md), or on its own as `Breakout.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

## Credits

Inspired by *Breakout* (Atari, 1976; designed by Nolan Bushnell and Steve Bristow, with a prototype built by Steve Wozniak) and its home version for the Atari 2600, and by *Arkanoid* (Taito, 1986) for the Arcade+ mode. Written from scratch in C for NumPlay; everything is drawn with code, so the whole game is about 14 KB.

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
