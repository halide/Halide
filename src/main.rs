use std::env;
use halide_runtime::halide_runtime::*;
include!(concat!(env!("OUT_DIR"), "/iir_blur.rs"));

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

    let mut inbuf: halide_buffer_t = halide_buffer(
        width as i32,
        height as i32,
        channels as i32,
        halide_type_t {
            bits: 32,
            code: 2,
            lanes: 1,
        },
        input.as_mut_ptr(),
        1,
    );
    let mut outbuf: halide_buffer_t = halide_buffer(
        width as i32,
        height as i32,
        channels as i32,
        halide_type_t {
            bits: 32,
            code: 2,
            lanes: 1,
        },
        output_raw.as_mut_ptr(),
        0,
    );

    unsafe {
        iir_blur(&mut inbuf, 0.1, &mut outbuf);
    }

    //save

    let mut output: Vec<u8> = vec![0; output_raw.len()];
    for x in 0..output_raw.len() {
        output[x] = output_raw[x] as u8;
    }

    image::save_buffer(
        "images/outBlurred.png",
        &output,
        width,
        height,
        image::ColorType::Rgba8,
    );
}


fn halide_buffer(
    width: i32,
    height: i32,
    channels: i32,
    t: halide_type_t,
    data: *mut f32,
    flags: u64,
) -> halide_buffer_t {


    let mut dim = Vec::new();

    dim.push(halide_dimension_t {
        flags: flags as u32,
        min: 0,
        extent: width,
        stride: channels,
    });

    dim.push(halide_dimension_t {
        flags: flags as u32,
        min: 0,
        extent: height,
        stride: width * channels,
    });

    dim.push(halide_dimension_t {
        flags: flags as u32,
        min: 0,
        extent: channels,
        stride: 1,
    });


    //dim.shrink_to_fit();

    let buf = halide_buffer_t {
        device: 0,
        device_interface: std::ptr::null(),
        dimensions: 3,
        host: data,
        flags: flags,
        padding: std::ptr::null_mut(),
        type_: t,
        dim: dim.as_mut_ptr(),
    };

    std::mem::forget(dim);

    buf
}
