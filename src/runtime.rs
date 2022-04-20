
include!("Runtime_bindings.rs");
use crate::runtime::runtime_bindings::*;

///Need doc
pub struct HalideBuffer {
    pub width: i32,
    pub height: i32,
    pub channels: i32,
    pub t: halide_type_t,
    pub data: *mut f32,
    pub flags: u64,
}

impl HalideBuffer {
    pub fn create_buffer(&mut self) -> halide_buffer_t {
        let mut dim = Vec::new();

        dim.push(halide_dimension_t {
            flags: self.flags as u32,
            min: 0,
            extent: self.width,
            stride: self.channels,
        });

        dim.push(halide_dimension_t {
            flags: self.flags as u32,
            min: 0,
            extent: self.height,
            stride: self.width * self.channels,
        });

        dim.push(halide_dimension_t {
            flags: self.flags as u32,
            min: 0,
            extent: self.channels,
            stride: 1,
        });

        //dim.shrink_to_fit();

        let buf = halide_buffer_t {
            device: 0,
            device_interface: std::ptr::null(),
            dimensions: 3,
            host: self.data,
            flags: self.flags,
            padding: std::ptr::null_mut(),
            type_: self.t,
            dim: dim.as_mut_ptr(),
        };

        std::mem::forget(dim);

        buf
    }
}
//}
