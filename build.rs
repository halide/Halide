
use halide_build::halide_build::{Generator,Gen_Builder};

fn main() {

    let Hal = Gen_Builder::new(
        "/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide",
        "src/gens"
    );
       // .out_dir("src/rs");


    let gen = Hal.newGen("iir_blur".to_string());

    assert!(gen.make().status.success());

    assert!(gen.run_gen().status.success());

    assert!(gen.bind().is_ok());

    assert!(gen.rename_move().is_ok());


}