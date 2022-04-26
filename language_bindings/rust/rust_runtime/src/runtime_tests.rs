use std::ffi::c_void;
use std::os::raw::c_int;
use std::ptr::null_mut;

use crate::runtime_bindings::*;

fn link(){
    println!(":rustc-link-search=native={}","/home/rootbutcher2/CLionProjects/Halide-rustbinding/bindings/rust/target");
    println!(":rustc-link-lib=static={}","runtime");

}

#[test]
fn i_got_here() {
    assert_eq!(2 + 2, 4);
}
#[test]
fn memalloc(){
    link();
    let raw_ptr: *mut ::std::os::raw::c_void = std::ptr::null_mut();
    //let raw_ptr = 10;
    unsafe {
        let x = halide_malloc(raw_ptr, 1000);
        //println!("{:?}",x);
        halide_free(raw_ptr,x);
        //println!("{}",x);

    }
}

#[test]
fn malloc_doesnt_return_null() {
    let raw_ptr: *mut ::std::os::raw::c_void = std::ptr::null_mut();
    let mut newptr:*mut c_void = std::ptr::null_mut();
    unsafe {
        assert_eq!(newptr as usize, 0x0);
        newptr = halide_malloc(raw_ptr, 1000);
        assert_ne!(newptr as usize, 0x0);
    }
}
#[test]
fn thread_set(){
    let  a = 5;
    let b = 1;
    unsafe {
        let x = halide_set_num_threads(a );
        assert_eq!(halide_set_num_threads(b), a as c_int)
    }
}
#[test]
fn test_halide_set_trace_file(){
    let  a = 0;
    unsafe{
        halide_set_trace_file(a)
    }
}
#[test]
fn test_shutdown_thread_pool(){
    unsafe{
        halide_shutdown_thread_pool();
    }
}

#[test]
fn test_shutdown_trace_file(){
    unsafe{
        halide_shutdown_trace();
    }
}

#[test]
fn test_buff_t(){
    let host: *mut f32  = f32::min as *mut f32;
    let IMPL = HalideDeviceInterfaceImplT { _unused: [] };
    let dev_int:halide_device_interface_t = halide_device_interface_t {
        device_malloc: None,
        device_free: None,
        device_sync: None,
        device_release: None,
        copy_to_host: None,
        copy_to_device: None,
        device_and_host_malloc: None,
        device_and_host_free: None,
        buffer_copy: None,
        device_crop: None,
        device_slice: None,
        device_release_crop: None,
        wrap_native: None,
        detach_native: None,
        compute_capability: None,
        impl_: &IMPL as *const HalideDeviceInterfaceImplT,
    };
    let H_type_t: halide_type_t = halide_type_t{
        code: 0,
        bits: 0,
        lanes: 0
    };
    let mut Bdim:halide_dimension_t = halide_dimension_t {
        min: 0,
        extent: 0,
        stride: 0,
        flags: 0
    };
    let raw_ptr: *mut ::std::os::raw::c_void = std::ptr::null_mut();
    unsafe{
        let t = halide_buffer_t {
            device: 0,
            device_interface: (&dev_int as *const halide_device_interface_t),
            host: host,
            flags: 0,
            type_: H_type_t,
            dimensions: 0,
            dim: &mut Bdim as *mut halide_dimension_t,
            padding: raw_ptr,
        };
    }
}
#[test]
fn test_Profiler_report(){
    let x= std::ptr::null_mut();
    unsafe{
        halide_profiler_report(x);
    }
}

#[test]
fn test_halide_profiler_reset(){
    unsafe{
        halide_profiler_reset();
    }
}


    #[test]
    fn test_halide_memoization_cache_release(){
        //let x = std::ptr::null_mut();
        let raw_ptr: *mut ::std::os::raw::c_void = std::ptr::null_mut();

        assert_eq!(2,3);
        unsafe {
            //halide_memoization_cache_release(x, raw_ptr)
        }
    }


#[test]
fn test_halide_memoization_cache_set_size(){
    let x = 0;
    unsafe {
        halide_memoization_cache_set_size(0)
    }

}
