use heapless::Vec;
use low_level_storage::{extapp_fileExists, extapp_fileRead, extapp_fileWrite};

pub mod save;

pub const MAX_STORAGE_VALUES: usize = 32;
// const MAX_FILE_SIZE: usize = MAX_VALUES * 4; // which represents 64 values (each represented by a u32)

pub mod low_level_storage {
    // games/tetris/numworks_utils/src/storage/storage.c (on the calculator)
    extern "C" {
        pub fn extapp_fileExists(filename: *const u8) -> bool;
        pub fn extapp_fileRead(filename: *const u8, len: *mut u32) -> *const u8;
        pub fn extapp_fileWrite(filename: *const u8, content: *const u8, len: u32) -> bool;
        pub fn extapp_fileErase(filename: *const u8) -> bool;
    }
}

// Here are some helpers functions to read and write a file as a &[u32].

/// The record holding a game's settings: "<game>.set", NUL-terminated for the C
/// storage functions (Epsilon record names need an extension).
fn record_name(filename: &str) -> heapless::String<32> {
    let mut s = heapless::String::<32>::new();
    let base = filename.trim_end_matches('\0');
    let _ = s.push_str(base);
    if !base.contains('.') {
        let _ = s.push_str(".set");
    }
    let _ = s.push('\0');
    s
}

/// Takes the filename = name of the game, and opens the file associated : creates it if necessary, and returns True if it exists.
pub fn open_file(filename: &str) -> bool {
    let name = record_name(filename);
    if unsafe { extapp_fileExists(name.as_ptr()) } {
        true
    } else {
        unsafe { extapp_fileWrite(name.as_ptr(), core::ptr::null(), 0) }
    }
}

/// Like "extapp_fileRead" but returns a Vec<u32, MAX_VALUES> instead of a *const u8.
pub fn read_file(filename: &str) -> Vec<u32, MAX_STORAGE_VALUES> {
    let name = record_name(filename);
    if !unsafe { extapp_fileExists(name.as_ptr()) } {
        Vec::new()
    } else {
        let mut len = 0;
        let data = unsafe { extapp_fileRead(name.as_ptr(), &mut len) };
        let mut data_read = Vec::new();
        if data.is_null() {
            return data_read;
        }
        for i in (0..len as usize).step_by(4) {
            let v: u32 = unsafe { data.wrapping_add(i).cast::<u32>().read_unaligned() };
            if data_read.push(v).is_err() {
                break;
            }
        }

        data_read
    }
}

pub fn read_data(filename: &str, pos: usize) -> Option<u32> {
    let values = read_file(filename);
    values.get(pos).copied()
}

/// Write a given data value to the file. Will copy the entirety of the file before erasing it and rewriting it again.
/// If a position is given, will replace this value. Else, writes it at the end of the file.
/// Returns the position at which it was added.
pub fn write_data(filename: &str, pos: Option<u32>, value: u32) -> usize {
    let mut values = read_file(filename);
    let res;
    if let Some(x) = pos {
        // if a position is given
        let d = values.get_mut(x as usize);
        if let Some(old_v) = d {
            // if data was found there, replace it
            *old_v = value;
            res = x as usize;
        } else {
            let _ = values.push(value); // else add it
            res = values.len() - 1;
        }
    } else {
        let _ = values.push(value); // else add it
        res = values.len() - 1;
    }
    let name = record_name(filename);
    // rewritten in place when the size is unchanged; a full storage only
    // means the settings are not remembered
    let _ = unsafe {
        extapp_fileWrite(
            name.as_ptr(),
            values.as_ptr().cast(),
            values.len() as u32 * 4,
        )
    };
    res
}
