# Block Breaker

Google's Block Breaker, the brick breaker you get when you search for "block breaker", on the NumWorks calculator: the dark pixel-art field, seven bricks across in blue, red, yellow and green, bricks that hold a power-up, the big white ball and paddle, your lives as circles and the score in segments. Wall after wall, each one a little faster.

<img src="docs/shot.png" width="320" alt="The first wall, with its power-up bricks">

- **Google's first wall,** then eleven more of the same kind, and after that they come round again, faster, with power-ups in new places.
- **Power-up bricks,** each with its sign:
  - **TNT** blows up the bricks around it (and sets off the TNT next to it).
  - **+** gives you a ball (up to five).
  - **O** splits your ball into three.
  - **<->** makes the paddle wider for a while.
  - **Flame:** a fireball goes straight through bricks for a few seconds.
  - **Beams:** the paddle fires lasers for a few seconds.
- **Where the ball meets the paddle decides where it goes,** like the original: the edges send it off at a sharp angle.
- **Settings:** the speed (slow, normal, fast) and the screen shake. Start from any wall you've reached; your best score is kept.

## Controls

| Key | Action |
| --- | --- |
| Left, Right (or 4, 6) | Move the paddle (hold to go faster) |
| OK, EXE or Up | Launch the ball |
| Back | Pause (Resume, Restart, Quit game) |
| Home | Quit |

## Credits

The look and the power-ups follow **Google's Block Breaker** (Google Search, 2025), itself a tribute to Atari's Breakout (1976). This is an independent fan version, drawn from shapes (no images from Google), and it isn't affiliated with or endorsed by Google.

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `BlockBreaker.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
