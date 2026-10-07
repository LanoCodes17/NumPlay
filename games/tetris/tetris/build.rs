use std::process::Command;

const TILESIZE: u16 = 12;

fn main() {
    // in NumPlay's language builds (tools/lang.py: NP_TEXT_EXTRA on in the common header), the
    // code for letters beyond ASCII (cfg np_text_extra)
    println!("cargo::rerun-if-changed=../../common/np_text.h");
    println!("cargo::rustc-check-cfg=cfg(np_text_extra)");
    let header = std::fs::read_to_string("../../common/np_text.h").unwrap_or_default();
    if header.contains("#define NP_TEXT_EXTRA 1") {
        println!("cargo::rustc-cfg=np_text_extra");
    }
    // Turn icon.png into icon.nwi
    println!("cargo:rerun-if-changed=src/data/icon.png");
    let output = Command::new("nwlink")
        .args(["png-nwi", "src/data/icon.png", "target/icon.nwi"])
        .output()
        .expect("Failure to launch process");
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );

    // Turn image.ppm into image.nppm
    nppm_decoder::decoder::extract_data_from_file(
        "src/data/image.ppm",
        "src/data/image.nppm",
        TILESIZE,
    );
}
