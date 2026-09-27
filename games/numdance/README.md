# NumDance

A rhythm game for the NumWorks calculator, in the style of Friday Night Funkin' and Dance Dance Revolution. Arrows fall toward a row of gray targets: press the matching key just as each one lands.

<img src="docs/shot.png" width="320" alt="NumDance in the middle of a song">

- **7 songs and Endless:** Warm Up, Slow Jam, Bubble Pop, Neon Nights, Starlight, Hyperdrive and Boss Rush, from 84 to 180 BPM and about a minute each, plus Endless, which never stops and gets faster.
- **4 difficulties** for every song: Easy, Normal, Hard and Expert.
- **Taps, holds and jumps:** keep the key down along a trail, and press two keys at once for a jump.
- **Judgments** Perfect, Great, Good and Miss, with Early or Late when you are off, a combo counter, a health bar, your score and accuracy.
- **A grade at the end** (S+, S, A, B, C or D), with your best score, grade and full combos kept for every song and difficulty.

The calculator has no speaker, so the music is something you see: the stage lights, the speakers, the LED bars, the health icons and the targets all pulse on the beat, a faint line scrolls by on every beat, and every chart sits on the beat grid, so the arrows themselves feel like a song.

## Controls

| Lane | Keys |
| --- | --- |
| Left | Left, 4 or 1 |
| Down | Down, 5 or 2 |
| Up | Up, 8 or 3 |
| Right | Right, 6 or + |

All three groups work at once: the arrow keys, the 4 5 8 6 cross, or the 1 2 3 + row. **Back** pauses (Resume, Restart, Quit game), **Home** leaves the app.

In the song list, **Up / Down** picks a song and **Left / Right** the difficulty.

## Options

- **Scroll speed** from 1x to 4x.
- **Direction:** arrows falling down (the default) or rising up.
- **Offset:** moves every key press earlier or later by up to 120 ms. **Calibrate offset** measures it for you: press as eight arrows land, and it keeps the average.
- **No-fail:** the song goes on even when the health bar is empty.

## How the songs are made

Each song is a few bytes: a tempo, a seed and a list of sections (intro, verse, build-up, chorus, break, drop, outro). The notes are made bar by bar from them, so a chorus comes back the same every time (mirrored the second time), and the difficulties share one rhythm: Easy plays the strong beats, Expert fills in the rest. The patterns are streams, staircases, trills, shapes and jacks, with jumps and holds, and they are checked to be playable with two thumbs: never more than two keys at once, enough time between notes for the difficulty, and no fast repeats on one key.

## Credits

A new game, inspired by [Friday Night Funkin'](https://github.com/FunkinCrew/Funkin) by ninjamuffin99, PhantomArcade, evilsk8r and Kawai Sprite (the arrow colours, the gray targets, the health bar with its two faces, "Ready? Set Go!") and by Konami's Dance Dance Revolution (the judgments, the combo and the grades). No code, art or music comes from either.

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `NumDance.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
