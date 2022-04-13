use std::io;
use std::io::Write;
use std::path::PathBuf;
use halide_build::halide_build::{Halide, HalideGen};

fn main() {
    let mut hs = Halide {
        halide_path: PathBuf::from("/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide"),
        gen_path: PathBuf::from("/home/rootbutcher2/CLionProjects/halide_test_app/src/gens"),
        rs_out_path: PathBuf::from("/home/rootbutcher2/CLionProjects/halide_test_app/src/rs"),
        generators: vec![],
    };
    hs.generators.push(HalideGen {
        halide_path: hs.halide_path,
        gen_name: "iir_blur".to_string(),
        gen_path: hs.gen_path,
        rs_out_path: hs.rs_out_path,
    });
    let out1 = hs.generators[0].compile_gen();
    println!("status gen: {}", out1.status);
    io::stdout().write_all(&out1.stdout).unwrap();
    //io::stderr().write_all(&out1.stderr).unwrap();
    assert!(out1.status.success());

    let out2 = hs.generators[0].run_gen();
    println!("status run gen: {}", out2.status);
    io::stdout().write_all(&out2.stdout).unwrap();
    //io::stderr().write_all(&out2.stderr).unwrap();
    assert!(out2.status.success());

    let out3 = hs.generators[0].rename_files();
    println!("status run rename: {:?}", out3);
    assert!(out3.is_ok());

    let out4 = hs.generators[0].bind();
    println!("status run rename: {:?}", out4);
    assert!(out4.is_ok());
}