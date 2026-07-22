use std::ffi::c_void;
use std::os::raw::c_int;

use halide_runtime::*;
use halide_runtime::runtime_bindings::*;

#[test]
fn i_got_here() {
    assert_eq!(2 + 2, 4);
}
#[test]
fn memalloc() {
    let raw_ptr: *mut ::std::os::raw::c_void = std::ptr::null_mut();
    //let raw_ptr = 10;
    unsafe {
        let x = halide_malloc(raw_ptr, 1000);
        //println!("{:?}",x);
        halide_free(raw_ptr, x);
        //println!("{}",x);
    }
}

#[test]
fn malloc_doesnt_return_null() {
    let raw_ptr: *mut ::std::os::raw::c_void = std::ptr::null_mut();
    let mut newptr: *mut c_void = std::ptr::null_mut();
    unsafe {
        assert_eq!(newptr as usize, 0x0);
        newptr = halide_malloc(raw_ptr, 1000);
        assert_ne!(newptr as usize, 0x0);
    }
}
#[test]
fn thread_set() {
    let a = 5;
    let b = 1;
    unsafe {
        let x = halide_set_num_threads(a);
        assert_eq!(halide_set_num_threads(b), a as c_int)
    }
}
#[test]
fn test_halide_set_trace_file() {
    let a = 0;
    unsafe { halide_set_trace_file(a) }
}
#[test]
fn test_shutdown_thread_pool() {
    unsafe {
        halide_shutdown_thread_pool();
    }
}

#[test]
fn test_shutdown_trace_file() {
    unsafe {
        halide_shutdown_trace();
    }
}

#[test]
fn test_Profiler_report() {
    let x = std::ptr::null_mut();
    unsafe {
        halide_profiler_report(x);
    }
}

#[test]
fn test_halide_profiler_reset() {
    unsafe {
        halide_profiler_reset();
    }
}

#[test]
fn test_halide_memoization_cache_set_size() {
    unsafe {
        halide_memoization_cache_set_size(10);
    }
}
