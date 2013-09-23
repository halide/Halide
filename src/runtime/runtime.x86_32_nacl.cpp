#include "posix_allocator.cpp"
#ifdef __linux__
#define LINUX_CLOCK_SYSCALL_SYS_CLOCK_GETTIME 265
#include "linux_clock.cpp"
#else
#include "posix_clock.cpp"
#endif
#include "posix_error_handler.cpp"
#include "write_debug_image.cpp"
#include "posix_io.cpp"
#include "tracing.cpp"
#include "posix_math.cpp"
#include "posix_thread_pool.cpp"
#include "copy_to_host_noop.cpp"
