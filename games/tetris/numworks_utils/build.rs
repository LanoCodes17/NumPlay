use std::process::Command;

fn main() {
    // The C storage helpers read the calculator's memory directly: they are only
    // built for the calculator. Other targets (the simulator build of NumPlay)
    // provide the same functions.
    if !std::env::var("TARGET").unwrap_or_default().starts_with("thumb") {
        return;
    }
    // Exécuter la commande pour récupérer les CFLAGS
    let output = Command::new("sh")
        .arg("-c")
        .arg("nwlink eadk-cflags-device") // Remplacez $(NWLINK) par la commande appropriée
        .output()
        .expect("Échec lors de l'exécution de la commande NWLINK");

    if !output.status.success() {
        panic!(
            "La commande nwlink a échoué avec le statut : {}",
            output.status
        );
    }
    // Convertir la sortie en chaîne de caractères
    let cflags = String::from_utf8(output.stdout)
        .expect("Impossible de convertir la sortie de NWLINK en String");
    let mut build = cc::Build::new();
    for flag in cflags.split_whitespace() {
        build.flag(flag.trim_matches('"'));
    }
    build
        .file("src/storage/storage.c")
        .file("src/storage/mystring.c")
        //        .flag("-Os")
        .compiler("arm-none-eabi-gcc")
        .compile("storage");

    println!("cargo::rerun-if-changed=src/storage/mystring.c");
    println!("cargo::rerun-if-changed=src/storage/storage.c");
}
