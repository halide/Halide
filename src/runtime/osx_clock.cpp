#include "runtime_internal.h"

#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL

#ifdef BITS_64
// OSX always uses an int32 for the usec field
struct timeval {
    int64_t tv_sec;
    int32_t tv_usec;
};
#else
struct timeval {
    int32_t tv_sec;
    int32_t tv_usec;
};
#endif

#endif

#include "posix_clock.cpp"

