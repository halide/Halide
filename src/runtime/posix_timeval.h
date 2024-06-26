#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL

#ifdef BITS_64
struct timeval {
    int64_t tv_sec, tv_usec;
};
#else
struct timeval {
    int32_t tv_sec, tv_usec;
};
#endif

#endif
