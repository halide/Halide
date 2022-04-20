include!("rs/iir_blur.rs");

use halide_runtime::halide_runtime::{halide_buffer, halide_type_t, halide_buffer_t};

use std::ffi::c_void;
use std::ffi::*;
use std::os::raw::c_int;
use std::sync::mpsc::channel;
use image::io::Reader;
use image::save_buffer_with_format;

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
    let mut inbuf = halide_buffer{
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
    let mut outbuf = halide_buffer{
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
