# NumDance

A rhythm game for the NumWorks calculator, on the idea of the Artistic Swimming event in Google's Doodle Champion Island Games: arrows fall down four lanes onto their targets, and you press the matching key just as each one lands, while a crew dances in step beside the lanes. Here the crew is a robot, a fox, a penguin and a frog, dancing on a city rooftop at night.

<img src="docs/shot.png" width="320" alt="NumDance in the middle of a song">

- **Champion Island's scoring:** **Perfect!** is 100 points, **Good!** 50, and a **Miss** takes 5 away (never below 0). The score and your combo sit in the top corner, and when misses pile up, the sky turns red.
- **The rooftop, in pixel art:** the moon, clouds going by, stars that twinkle on the beat, the city with its lit windows and the red lights on its masts, and the crew, who change moves together on every beat.
- **7 songs and Endless:** Warm Up, Slow Jam, Bubble Pop, Neon Nights, Starlight, Hyperdrive and Boss Rush, from 84 to 180 BPM and about a minute each, plus Endless, which never stops and gets faster.
- **4 difficulties** for every song: Easy, Normal, Hard and Expert. Jumps (two arrows at once) come in on the harder ones.
- **A grade at the end** (S+, S, A, B, C or D), with your best score, grade and full combos kept for every song and difficulty.

The calculator has no speaker, so the music is something you see: the crew moves on every beat, the stars and the masts blink with it, and every chart sits on the beat grid, so the arrows themselves feel like a song.

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
- **No-fail** (on at first, like Champion Island): turn it off and a song ends once the sky is at its reddest.

## How the songs are made

Each song is a few bytes: a tempo, a seed and a list of sections (intro, verse, build-up, chorus, break, drop, outro). The notes are made bar by bar from them, so a chorus comes back the same every time (mirrored the second time), and the difficulties share one rhythm: Easy plays the strong beats, Expert fills in the rest. The patterns are streams, staircases, trills, shapes and jacks, with jumps, and they are checked to be playable with two thumbs: never more than two keys at once, enough time between notes for the difficulty, and no fast repeats on one key.

## Credits

A new game. The idea and the scoring come from the Artistic Swimming event of **Doodle Champion Island Games** (Google, 2021); the setting, the characters and the look are NumDance's own, and it isn't affiliated with or endorsed by Google. The countdown and the grades come from Friday Night Funkin' and Dance Dance Revolution. No code, art or music comes from any of them.

The crew is drawn from pixel maps in [tools/dancers.py](tools/dancers.py), which packs them at 4 bits a pixel.

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `NumDance.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
