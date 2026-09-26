//! How a game starts and ends on the calculator.
//!
//! `run` starts the game with Home held back by Epsilon and cleans up when
//! it ends; `leave` ends it from anywhere (Home, On/Off, or after a panic).
//! Both are in C (src/storage/storage.c), which can jump out of the game.

extern "C" {
    fn np_app_run(game: extern "C" fn()) -> i32;
    fn np_app_leave();
}

/// Runs `game` (an `extern "C" fn()`), returns when it ends or is left.
pub fn run(game: extern "C" fn()) -> i32 {
    unsafe { np_app_run(game) }
}

/// Ends the game now and returns from `run`.
pub fn leave() {
    unsafe { np_app_leave() }
}
