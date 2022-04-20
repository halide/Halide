//#[link(name="runtime", kind="static")]
extern "C" {
    pub fn halide_malloc(
        user_context: *mut ::std::os::raw::c_void,
        x: isize,
    ) -> *mut ::std::os::raw::c_void;
}

extern "C" {
    pub fn halide_free(user_context: *mut ::std::os::raw::c_void, ptr: *mut ::std::os::raw::c_void);
}

extern "C" {
    #[doc = " Set the number of threads used by Halide's thread pool. Returns"]
    #[doc = " the old number."]
    #[doc = ""]
    #[doc = " n < 0  : error condition"]
    #[doc = " n == 0 : use a reasonable system default (typically, number of cpus online)."]
    #[doc = " n == 1 : use exactly one thread; this will always enforce serial execution"]
    #[doc = " n > 1  : use a pool of exactly n threads."]
    #[doc = ""]
    #[doc = " (Note that this is only guaranteed when using the default implementations"]
    #[doc = " of halide_do_par_for(); custom implementations may completely ignore values"]
    #[doc = " passed to halide_set_num_threads().)"]
    pub fn halide_set_num_threads(n: ::std::os::raw::c_int) -> ::std::os::raw::c_int;
}
extern "C" {
    #[doc = " Print out timing statistics for everything run since the last"]
    #[doc = " reset. Also happens at process exit."]
    pub fn halide_profiler_report(user_context: *mut ::std::os::raw::c_void);
}
extern "C" {
    #[doc = "Reset profiler state cheaply. May leave threads running or some"]
    #[doc = " memory allocated but all accumulated statistics are reset."]
    #[doc = ""]
    #[doc = "WARNING: Do NOT call this method while any halide pipeline is"]
    #[doc = "running; halide_profiler_memory_allocate/free and"]
    #[doc = "halide_profiler_stack_peak_update update the profiler pipeline's"]
    #[doc = "state without grabbing the global profiler state's lock. "]
    pub fn halide_profiler_reset();
}
extern "C" {
    #[doc = "If halide_memoization_cache_lookup succeeds,"]
    #[doc = "halide_memoization_cache_release must be called to signal the"]
    #[doc = "storage is no longer being used by the caller. "]
    #[doc = ""]
    #[doc = "It will be passed to the host pointer of one the buffers returned by"]
    #[doc = "halide_memoization_cache_lookup. "]
    #[doc = ""]
    #[doc = "That is halide_memoization_cache_release will be called multiple times for"]
    #[doc = "the case where halide_memoization_cache_lookup is handling multiple"]
    #[doc = "buffers.  (This corresponds to memoizing a Tuple in Halide.) "]
    #[doc = ""]
    #[doc = "Note that the host pointer must be sufficient to get to all information"]
    #[doc = "the release operation needs. "]
    #[doc = ""]
    #[doc = "The default Halide cache implementation accomplishes this by storing extra data before the start of the user"]
    #[doc = "modifiable host storage."]
    #[doc = ""]

    #[doc = "This call is like free and does not have a failure return."]

    pub fn halide_memoization_cache_release(
        user_context: *mut ::std::os::raw::c_void,
        host: *mut ::std::os::raw::c_void,
    );
}
extern "C" {
    #[doc = "Set the soft maximum amount of memory, in bytes, that the LRU"]
    #[doc = "cache will use to memoize Func results.  "]
    #[doc = ""]
    #[doc = "This is not a strict maximum in that concurrency and simultaneous use of memoized"]
    #[doc = "results larger than the cache size can both cause it to"]
    #[doc = "temporarily be larger than the size specified here."]

    pub fn halide_memoization_cache_set_size(size: i64);
}
extern "C" {
    #[doc = " Set the file descriptor that Halide should write binary trace"]
    #[doc = " events to. If called with 0 as the argument, Halide outputs trace"]
    #[doc = " information to stdout in a human-readable format. If never called,"]
    #[doc = " Halide checks the for existence of an environment variable called"]
    #[doc = " HL_TRACE_FILE and opens that file. If HL_TRACE_FILE is not defined,"]
    #[doc = " it outputs trace information to stdout in a human-readable"]
    #[doc = " format."]
    pub fn halide_set_trace_file(fd: ::std::os::raw::c_int);
}

extern "C" {
    #[doc = "Define halide_do_par_for to replace the default thread pool"]
    #[doc = "implementation. halide_shutdown_thread_pool can also be called to"]
    #[doc = "release resources used by the default thread pool on platforms"]
    #[doc = "where it makes sense. See Func::set_custom_do_task and"]
    #[doc = "Func::set_custom_do_par_for. Should return zero if all the jobs"]
    #[doc = "return zero, or an arbitrarily chosen return value from one of the"]
    #[doc = "jobs otherwise."]

    pub fn halide_shutdown_thread_pool();
}

extern "C" {
    #[doc = " If tracing is writing to a file. This call closes that file"]
    #[doc = " (flushing the trace). Returns zero on success."]
    pub fn halide_shutdown_trace() -> ::std::os::raw::c_int;
}

#[doc = " An opaque struct containing per-GPU API implementations of the"]
#[doc = " device functions."]
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct HalideDeviceInterfaceImplT {
    pub(crate) _unused: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct halide_dimension_t {
    pub min: i32,
    pub extent: i32,
    pub stride: i32,
    pub flags: u32,
}
#[doc = "A runtime tag for a type in the halide type system. Can be ints,"]
#[doc = "unsigned ints, or floats of various bit-widths the 'bits' field)."]
#[doc = "(Can also be vectors of the same (by setting the 'lanes' field to something larger than one)."]
#[doc = " This struct should be exactly 32-bits in size."]
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct halide_type_t {
    pub code: u8,
    #[doc = " The number of bits of precision of a single scalar value of this type."]
    pub bits: u8,
    #[doc = " How many elements in a vector. This is 1 for scalar types."]
    pub lanes: u16,
}
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct halide_device_interface_t {
    pub device_malloc: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
            device_interface: *const halide_device_interface_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub device_free: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub device_sync: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub device_release: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            device_interface: *const halide_device_interface_t,
        ),
    >,
    pub copy_to_host: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub copy_to_device: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
            device_interface: *const halide_device_interface_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub device_and_host_malloc: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
            device_interface: *const halide_device_interface_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub device_and_host_free: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub buffer_copy: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            src: *mut halide_buffer_t,
            dst_device_interface: *const halide_device_interface_t,
            dst: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub device_crop: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            src: *const halide_buffer_t,
            dst: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub device_slice: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            src: *const halide_buffer_t,
            slice_dim: ::std::os::raw::c_int,
            slice_pos: ::std::os::raw::c_int,
            dst: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub device_release_crop: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub wrap_native: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
            handle: u64,
            device_interface: *const halide_device_interface_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub detach_native: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            buf: *mut halide_buffer_t,
        ) -> ::std::os::raw::c_int,
    >,
    pub compute_capability: ::std::option::Option<
        unsafe extern "C" fn(
            user_context: *mut ::std::os::raw::c_void,
            major: *mut ::std::os::raw::c_int,
            minor: *mut ::std::os::raw::c_int,
        ) -> ::std::os::raw::c_int,
    >,
    pub impl_: *const HalideDeviceInterfaceImplT,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct halide_buffer_t {
    #[doc = " A device-handle for e.g. GPU memory used to back this buffer."]
    pub device: u64,
    #[doc = " The interface used to interpret the above handle."]
    pub device_interface: *const halide_device_interface_t,
    #[doc = " A pointer to the start of the data in main memory. In terms of"]
    #[doc = " the Halide coordinate system, this is the address of the min"]
    #[doc = " coordinates (defined below)."]
    pub host: *mut f32,
    #[doc = " flags with various meanings."]
    pub flags: u64,
    #[doc = " The type of each buffer element."]
    pub type_: halide_type_t,
    #[doc = " The dimensionality of the buffer."]
    pub dimensions: i32,
    #[doc = " The shape of the buffer. Halide does not own this array - you"]
    #[doc = " must manage the memory for it yourself."]
    pub dim: *mut halide_dimension_t,
    #[doc = " Pads the buffer up to a multiple of 8 bytes"]
    pub padding: *mut ::std::os::raw::c_void,
}



extern "C" {
    #[doc = " Copy image data from device memory to host memory. This must be called"]
    #[doc = " explicitly to copy back the results of a GPU-based filter."]
    pub fn halide_copy_to_host(
        user_context: *mut ::std::os::raw::c_void,
        buf: *mut halide_buffer_t,
    ) -> ::std::os::raw::c_int;
}

#[doc = "< signed integers"]
pub const halide_type_code_t_halide_type_int: halide_type_code_t = 0;
#[doc = "< unsigned integers"]
pub const halide_type_code_t_halide_type_uint: halide_type_code_t = 1;
#[doc = "< IEEE floating point numbers"]
pub const halide_type_code_t_halide_type_float: halide_type_code_t = 2;

#[doc = " Types in the halide type system. They can be ints, unsigned ints,"]
#[doc = " or floats (of various bit-widths), or a handle (which is always 64-bits)."]
#[doc = " Note that the int/uint/float values do not imply a specific bit width"]
#[doc = " (the bit width is expected to be encoded in a separate value)."]
pub type halide_type_code_t = ::std::os::raw::c_uint;