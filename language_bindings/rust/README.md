# Halide in Rust

## Instalation Steps:

For installing Halide refer to: https://github.com/halide/Halide#readme

Install Rust: https://www.rust-lang.org/tools/install

Install Cargo: https://doc.rust-lang.org/cargo/getting-started/installation.html

## Demo App Guide
   
1.) Navigate to Halide/language_bindings/rust/apps/iir_blur

2.) Run the following:

```
  Cargo build
  Cargo test
  Cargo run
```

3.) You should now have a slightly blurry picture of a hummingbird in the images/ directory called.

## Generating rustdocs

In any folder you can use the command 

```cargo doc --open``` 

To generate the rustdocs and open the html file.



## FAQ

### Where are the make files or cmake lists?
Rust handles that in the build.rs



## Link to cargo docs

TODO...

