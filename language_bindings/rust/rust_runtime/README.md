# Rust Runtime

src/lib.rs contains a helper struct to more easily create buffer_t objects in Rust.

# Example:
```
use halide_runtime::runtime_bindings::{ halide_type_t}; 
use halide_runtime::HalideBuffer;
use image::io::Reader;

fn main() {

    let img = Reader::open("images/Hummingbird.jpg")
        //Img source: https://commons.wikimedia.org/wiki/File:Hummingbird.jpg#filelinks
        //Image released into public domain by Jon Sullivan PDPhoto.org
        .unwrap()
        .decode()
        .unwrap()
        .to_rgba8();

    let height = img.height();
    let width = img.width();
    let channels = 4;

    let mut img_raw = img.into_raw();

    let mut input: Vec<f32> = vec![0.0; img_raw.len()];
    for x in 0..img_raw.len() {
        input[x] = img_raw[x] as f32;
    }

    // Create the input buffer
    let mut inbuf = HalideBuffer {
        width: width as i32,
        height: height as i32,
        channels: channels as i32,
        t: halide_type_t {
            bits: 32,
            code: 2,
            lanes: 1,
        },
        data: input.as_mut_ptr(),
        flags: 1,
    }
    .create_buffer();
}
```
