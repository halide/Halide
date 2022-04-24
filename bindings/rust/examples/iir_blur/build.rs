use std::io;
use std::io::Write;
use halide::build::{Generator, GenBuilder};

fn main() {

    let Hal = GenBuilder::new(
        "../../../../",
        "src/gens"
    ).debug(true);
       // .out_dir("src/rs");


    let gen = Hal.new_gen("iir_blur".to_string());


    gen.build_bind();

    //Some useful debug calls
    /*
    let out = gen.compile();
    println!("compile Status: {}", out.status.success());
    io::stdout().write_all(&out.stdout);
    io::stderr().write_all(&out.stderr);
    assert!(out.status.success());

    assert!(gen.run_gen().status.success());

    assert!(gen.bind().is_ok());

    assert!(gen.rename().is_ok());
    assert!(gen.make_runtime().is_ok());
    */
}