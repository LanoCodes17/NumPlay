use std::process::Command;

fn main() {
    // The C helpers (files, app lifetime) read the calculator's memory directly:
    // they are only built for the calculator. Other targets (the simulator
    // build of NumPlay) provide the same functions.
    println!("cargo::rerun-if-changed=src/storage/storage.c");
    println!("cargo::rerun-if-changed=../../common/epsilon_app.h");
    println!("cargo::rerun-if-changed=../../common/epsilon_files.h");
    println!("cargo::rerun-if-env-changed=NWLINK");
    if !std::env::var("TARGET").unwrap_or_default().starts_with("thumb") {
        return;
    }
    // compiler flags for the calculator, from nwlink ($NWLINK, or nwlink in the path)
    let nwlink = std::env::var("NWLINK").unwrap_or_else(|_| "nwlink".to_string());
    let output = Command::new("sh")
        .arg("-c")
        .arg(format!("{nwlink} eadk-cflags-device"))
        .output()
        .expect("could not run nwlink");
    assert!(output.status.success(), "nwlink failed: {}", output.status);
    let cflags = String::from_utf8(output.stdout).expect("nwlink output");
    let mut build = cc::Build::new();
    for flag in cflags.split_whitespace() {
        build.flag(flag.trim_matches('"'));
    }
    build
        .file("src/storage/storage.c")
        .flag("-Os")
        .compiler("arm-none-eabi-gcc")
        .compile("storage");
}
