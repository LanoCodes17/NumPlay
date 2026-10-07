#![no_std]
#![no_main]

/// A text players read, NUL-terminated for the calculator. NumPlay's language builds put each
/// one's translation in its place (tools/lang.py, games/tetris/lang/<code>.txt).
#[macro_export]
macro_rules! T {
    ($s:literal) => {
        concat!($s, "\0")
    };
}

pub mod app;
pub mod eadk;
pub mod graphical;
pub mod menu;
pub mod numbers;
pub mod storage;
pub mod utils;
pub mod widgets;

#[macro_use]
pub mod macros;
