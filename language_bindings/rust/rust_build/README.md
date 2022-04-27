# Halide Rust Building

This crate is for compiling and binding a generator and create a Halide runtime.

## Process

This is done by first creating a GenBuider by doing something like:

let builder = GenBuilder::new(Path to Halide, Path to generator).debug(True or False)

For example:
```
let builder = GenBuilder::new("../../../../", "src/gens").debug(true);
```

Then one can use new_gen() to specify a generator name 

let gen = builder.new_gen(generator name)

For example:
```
 let gen = Hal.new_gen("iir_blur".to_string());
```

Then a user can use the relevant generator functions 
### compile()
  Make the generator executable using halide GenGen and g++
 
### run_gen()
  Runs the previously made executable
  
### make_runtime()
  Makes the Halide runtime
  
### rename
  Renames the gen outputs to be what Rust and Bindgen expect on Linux distros
  
### bind()
  Runs Bindgen on the generator
  
If you want to use all of these together, you can simply use

### build_bind()
  which compiles, runs, renames, and then buinds the generator
  
  
## Example using build_bind()
```
use halide_build::{GenBuilder, Generator};
use std::io;
use std::io::Write;

fn main() {
    let Hal = GenBuilder::new("../../../../", "src/gens").debug(true);

    let gen = Hal.new_gen("iir_blur".to_string());

    gen.build_bind();
}

```


## Example without using build_bind() and instead using more debug options
```
use halide_build::{GenBuilder, Generator};
use std::io;
use std::io::Write;

fn main() {
    let Hal = GenBuilder::new("../../../../", "src/gens").debug(true);
    // .out_dir("src/rs");

    let gen = Hal.new_gen("iir_blur".to_string());

    let out = gen.compile();
    println!("compile Status: {}", out.status.success());
    io::stdout().write_all(&out.stdout);
    io::stderr().write_all(&out.stderr);
    
    assert!(out.status.success());
    assert!(gen.run_gen().status.success());
    assert!(gen.bind().is_ok());
    assert!(gen.rename().is_ok());
    assert!(gen.make_runtime().is_ok());
    
}

```
