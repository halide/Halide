fn main() {

    //todo move & generate in halide::build
    println!(
        "cargo:rustc-link-search=native={}",
        "src/"
    );
    println!(
        "cargo:rustc-link-lib=static={}",
        "runtime"
    );
}