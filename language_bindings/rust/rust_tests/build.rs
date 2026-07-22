//This build script is only required for test functions
use halide_build::{GenBuilder, Generator};
fn main() {
    GenBuilder::new("../../../", "").make_runtime();
}
