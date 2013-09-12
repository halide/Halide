#include "posix_allocator.cpp"
#ifdef __linux__
#include "linux_clock.cpp"
#else
#include "posix_clock.cpp"
#endif
#include "posix_error_handler.cpp"
#include "write_debug_image.cpp"
#include "posix_io.cpp"
#include "posix_math.cpp"
#include "posix_thread_pool.cpp"
#include "copy_to_host_noop.cpp"
