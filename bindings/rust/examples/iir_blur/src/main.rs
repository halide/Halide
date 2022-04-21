#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/iir_blur.rs"));

use halide::runtime::{HalideBuffer};
use halide::runtime::runtime_bindings::{halide_buffer_t, halide_type_t};
use image::io::Reader;

fn main(){

    println!("halide mainish thing");

    let img = Reader::open("images/cat.png")
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

    let mut output_raw: Vec<f32> = vec![0.0; img_raw.len()];

    // Create the input buffer
    let mut inbuf = HalideBuffer{
        width: width as i32,
        height: height as i32,
        channels: channels as i32,
        t: halide_type_t{
            bits: 32,
            code: 2,
            lanes: 1,
        },
        data: input.as_mut_ptr(),
        flags: 1,
    }.create_buffer();
    
    // Create the output buffer
    let mut outbuf = HalideBuffer{
        width: width as i32,
        height: height as i32,
        channels: channels as i32,
        t: halide_type_t{
            bits: 32,
            code: 2,
            lanes: 1,
        },
        data: output_raw.as_mut_ptr(),
        flags: 0,
    }.create_buffer();

    unsafe {
        iir_blur(&mut inbuf, 0.1, &mut outbuf);
    }

    // Convert the image back to rgba8
    let mut output: Vec<u8> = vec![0; output_raw.len()];
    for x in 0..output_raw.len() {
        output[x] = output_raw[x] as u8;
    }

    // Save the image
    image::save_buffer(
        "images/outBlurred.png",
        &output,
        width,
        height,
        image::ColorType::Rgba8,
    );
}
