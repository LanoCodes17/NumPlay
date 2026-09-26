use heapless::String;
use numworks_utils::{
    eadk::{
        display::{push_rect_uniform, wait_for_vblank, SCREEN_HEIGHT, SCREEN_WIDTH},
        Color, Point, Rect,
    },
    graphical::{draw_string_cfg, tiling::Tileset},
    include_bytes_align_as,
    utils::{string_from_u32, LARGE_CHAR_WIDTH},
};

use crate::{
    game_tetris::{
        BACKGROUND_DARK_GRAY, BACKGROUND_GRAY, CASE_SIZE, COLOR_CONFIG, PLAYFIELD_HEIGHT,
        PLAYFIELD_WIDTH,
    },
    tetriminos::Tetrimino,
};

const IMAGE_BYTES: &[u8] = include_bytes_align_as!(Color, "./data/image.nppm");

static TILESET: Tileset = Tileset::new(CASE_SIZE, 10, IMAGE_BYTES);

// Full screen layout: the playfield takes the whole height of the screen, the
// panels fill the space on both sides.
/// Left edge of the playfield, in pixels.
pub const FIELD_X: u16 = (SCREEN_WIDTH - PLAYFIELD_WIDTH * CASE_SIZE) / 2;
/// Top of the playfield, in pixels.
pub const FIELD_Y: u16 = (SCREEN_HEIGHT - PLAYFIELD_HEIGHT * CASE_SIZE) / 2;
const FRAME: u16 = 4;
const PANEL_W: u16 = 88;
const PANEL_H: u16 = 110;
const LEFT_X: u16 = (FIELD_X - FRAME - PANEL_W) / 2;
const RIGHT_X: u16 = SCREEN_WIDTH - LEFT_X - PANEL_W;
const TOP_Y: u16 = 6;
const BOTTOM_Y: u16 = SCREEN_HEIGHT - 6 - PANEL_H;
const LINE: u16 = 20;

/// Draws a framed panel (outer rectangle given) with its background.
fn draw_panel(x: u16, y: u16) {
    push_rect_uniform(
        Rect {
            x,
            y,
            width: PANEL_W,
            height: PANEL_H,
        },
        BACKGROUND_DARK_GRAY,
    );
    push_rect_uniform(
        Rect {
            x: x + FRAME,
            y: y + FRAME,
            width: PANEL_W - 2 * FRAME,
            height: PANEL_H - 2 * FRAME,
        },
        COLOR_CONFIG.bckgrd,
    );
}

/// Draws a label centred in a panel, `row` lines below its top.
fn draw_label(text: &str, panel_x: u16, panel_y: u16, row: u16) {
    let chars = text.trim_end_matches('\0').chars().count() as u16;
    let x = panel_x + (PANEL_W - chars * LARGE_CHAR_WIDTH) / 2;
    draw_string_cfg(
        text,
        Point::new(x, panel_y + FRAME + 4 + row * LINE),
        true,
        &COLOR_CONFIG,
        false,
    );
}

/// This draws every UI elements that will never change (rects, titles...)
pub fn draw_stable_ui(level: u16, level_lines: u16, score: u32, high_score: u32) {
    wait_for_vblank();
    push_rect_uniform(
        Rect {
            x: 0,
            y: 0,
            width: SCREEN_WIDTH,
            height: SCREEN_HEIGHT,
        },
        BACKGROUND_GRAY,
    );
    push_rect_uniform(
        Rect {
            x: FIELD_X - FRAME,
            y: 0,
            width: PLAYFIELD_WIDTH * CASE_SIZE + 2 * FRAME,
            height: SCREEN_HEIGHT,
        },
        BACKGROUND_DARK_GRAY,
    );
    TILESET.tiling(
        Point::new(FIELD_X, FIELD_Y),
        (PLAYFIELD_WIDTH, PLAYFIELD_HEIGHT),
        Point::new(7, 0),
        false,
        1,
    );

    wait_for_vblank();
    draw_panel(LEFT_X, TOP_Y);
    draw_panel(LEFT_X, BOTTOM_Y);
    draw_panel(RIGHT_X, TOP_Y);
    draw_panel(RIGHT_X, BOTTOM_Y);
    draw_label("NEXT\0", LEFT_X, TOP_Y, 0);
    draw_label("HOLD\0", LEFT_X, BOTTOM_Y, 0);
    draw_label("BEST\0", RIGHT_X, TOP_Y, 0);
    draw_label("SCORE\0", RIGHT_X, TOP_Y, 2);
    draw_label("LEVEL\0", RIGHT_X, BOTTOM_Y, 0);
    draw_label("LINES\0", RIGHT_X, BOTTOM_Y, 2);

    wait_for_vblank();
    draw_score(high_score, true);
    draw_score(score, false);
    draw_level(level);
    draw_lines_number(level_lines);
}

/// A number right-padded to 6 characters: zeros for scores, spaces otherwise.
fn padded(value: u32, zeros: bool) -> String<8> {
    let mut txt: String<8> = String::new();
    if value > 999_999 {
        txt.push_str("999999\0").unwrap();
        return txt;
    }
    let mut digits: String<11> = string_from_u32(value);
    digits.pop();
    for _ in 0..(6 - digits.chars().count()) {
        txt.push(if zeros { '0' } else { ' ' }).unwrap();
    }
    txt.push_str(digits.as_str()).unwrap();
    txt.push('\0').unwrap();
    txt
}

pub fn draw_score(score: u32, high_score: bool) {
    draw_label(
        padded(score, true).as_str(),
        RIGHT_X,
        TOP_Y,
        if high_score { 1 } else { 3 },
    );
}

pub fn draw_level(level: u16) {
    draw_label(padded(level as u32, false).as_str(), RIGHT_X, BOTTOM_Y, 1);
}

pub fn draw_lines_number(line: u16) {
    draw_label(padded(line as u32, false).as_str(), RIGHT_X, BOTTOM_Y, 3);
}

/// Draws a given tetrimino.
pub fn draw_tetrimino(tetri: &Tetrimino, clear: bool) {
    for pos in tetri.get_blocks_grid_pos() {
        if (pos.x >= 0) & (pos.y >= 0) {
            draw_block_image(
                FIELD_X + (pos.x as u16) * CASE_SIZE,
                FIELD_Y + (pos.y as u16) * CASE_SIZE,
                if clear { 7 } else { tetri.color as u16 },
            );
        }
    }
}

/// Draws a piece centred in the preview area of a panel.
fn draw_preview(tetri: &Tetrimino, panel_y: u16) {
    let area_y = panel_y + FRAME + LINE + 4;
    let area_h = PANEL_H - 2 * FRAME - LINE - 4;
    push_rect_uniform(
        Rect {
            x: LEFT_X + FRAME,
            y: area_y,
            width: PANEL_W - 2 * FRAME,
            height: area_h,
        },
        COLOR_CONFIG.bckgrd,
    );
    // centre the piece's bounding box in the area
    let blocks = tetri.get_blocks();
    let (mut x0, mut x1, mut y0, mut y1) = (i16::MAX, i16::MIN, i16::MAX, i16::MIN);
    for (x, y) in blocks {
        x0 = x0.min(x);
        x1 = x1.max(x);
        y0 = y0.min(y);
        y1 = y1.max(y);
    }
    let case = CASE_SIZE as i16;
    let origin_x = (LEFT_X + PANEL_W / 2) as i16 - (x1 - x0 + 1) * case / 2 - x0 * case;
    let origin_y = (area_y + area_h / 2) as i16 - (y1 - y0 + 1) * case / 2 - y0 * case;
    for (x, y) in blocks {
        draw_block_image(
            (origin_x + x * case) as u16,
            (origin_y + y * case) as u16,
            tetri.color as u16,
        );
    }
}

pub fn draw_next_tetrimino(tetri: &Tetrimino) {
    draw_preview(tetri, TOP_Y);
}

pub fn draw_held_tetrimino(tetri: &Tetrimino) {
    draw_preview(tetri, BOTTOM_Y);
}

pub fn draw_blank_line(y: u16) {
    TILESET.tiling(
        Point::new(FIELD_X, FIELD_Y + y * CASE_SIZE),
        (PLAYFIELD_WIDTH, 1),
        Point::new(7, 0),
        false,
        1,
    );
}

fn draw_block_image(abs_x: u16, abs_y: u16, x_map: u16) {
    TILESET.draw_tile(Point::new(abs_x, abs_y), Point::new(x_map, 0), 1, false);
}

pub fn draw_block(x: u16, y: u16, color: u16) {
    draw_block_image(
        FIELD_X + x * CASE_SIZE,
        FIELD_Y + y * CASE_SIZE,
        color,
    );
}
