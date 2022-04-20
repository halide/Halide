use std::io;
use std::io::Write;
use halide::build::{Generator, GenBuilder};

fn main() {

    let Hal = GenBuilder::new(
        "/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide",
        "src/gens"
    );
       // .out_dir("src/rs");


    let gen = Hal.new_gen("iir_blur".to_string());

    let out = gen.make();
    println!("compile Status: {}", out.status.success());
    io::stdout().write_all(&out.stdout);
    io::stderr().write_all(&out.stderr);
    assert!(out.status.success());

    assert!(gen.run_gen().status.success());

    assert!(gen.bind().is_ok());

    assert!(gen.rename_move().is_ok());

}