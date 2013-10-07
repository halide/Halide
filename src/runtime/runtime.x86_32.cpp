#include "posix_allocator.cpp"
#if defined(HALIDE_OS_linux)
#include "linux_clock.cpp"
#else
#include "posix_clock.cpp"
#endif
#include "posix_error_handler.cpp"
#include "write_debug_image.cpp"
#include "posix_io.cpp"
#include "tracing.cpp"
#include "posix_math.cpp"
#if defined(HALIDE_OS_windows)
#include "fake_thread_pool.cpp"
#else
#if defined(HALIDE_OS_os_x) || defined(HALIDE_OS_ios)
#include "gcd_thread_pool.cpp"
#else
#include "posix_thread_pool.cpp"
#endif
#endif
#include "copy_to_host_noop.cpp"
