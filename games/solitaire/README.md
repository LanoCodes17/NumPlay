# Solitaire

Klondike for the NumWorks calculator, the way Windows Solitaire plays it: green felt, the blue lattice card backs, Draw One or Draw Three, Standard or Vegas scoring, and the famous ending where the cards bounce off the bottom of the screen.

<img src="docs/shot.png" width="320" alt="A game in progress">

- **Windows rules:** draw 1, 2 or 3 cards (Easy, Normal, Hard), and Standard, Vegas or no scoring. Vegas starts at -$52, pays $5 a card and limits the passes through the deck (as many as the cards you draw); **Keep score** adds your Vegas games up.
- **Timed game:** the clock starts with your first move. In Standard scoring you lose 2 points every 10 seconds, and a fast win earns Windows' bonus (700,000 divided by your time).
- **Help when you want it:** unlimited Undo, a Hint, OK twice to send a card up like a double click, a key that plays everything it can to the foundations, and an automatic finish once every card is face up.
- **Saved as you go:** leave in the middle of a game and continue it later. Statistics count your games, wins, streaks, best time, best score and your Vegas bank.

Every card is drawn from a few tiny bitmaps (pips, court figures and one small font), and only what changes is redrawn, so the whole game is about 15 KB.

## Controls

| Key | Action |
| --- | --- |
| Arrows | Move |
| Up / Down on a column | Pick up more or fewer cards |
| OK | Pick up, put down (on the deck: draw) |
| OK twice | Send the card to a foundation |
| EXE | Draw from the deck |
| Toolbox | Play all you can to the foundations |
| ⌫ | Undo |
| Shift | Hint |
| Back | Put the cards back, or pause |

Picked-up cards turn dark, like a selection in Windows. When you put a run down, the game finds how many cards fit there.

## Scoring

| Move | Standard | Vegas |
| --- | --- | --- |
| To a foundation | +10 | +$5 |
| Waste to a column | +5 | |
| Turning a card over | +5 | |
| Foundation back to a column | -15 | -$5 |
| Recycling the deck | -100 (draw 1), -20 after 3 passes | not after the last pass |

## Credits

Rewritten in C for NumPlay from Tatone26's Solitaire in [Numworks-games](https://github.com/Tatone26/Numworks-games/tree/main/apps/solitaire) (All the Apps), released into the public domain ([UNLICENSE](UNLICENSE)). It keeps that version's Difficulty option (how many cards you draw) and adds the Windows look, the deal and win animations, Standard and Vegas scoring, the clock, Undo, Hint, the automatic finish and statistics.

The look and the rules follow Microsoft's Windows Solitaire (Wes Cherry, with cards by Susan Kare).

It comes inside [NumPlay](https://github.com/Mason363/NumPlay), or on its own as `Solitaire.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

Build: `make` (needs `arm-none-eabi-gcc` and Node.js for nwlink).
