#ifdef BITS_64
#define SYS_CLOCK_GETTIME 113
#endif

#ifdef BITS_32
#define SYS_CLOCK_GETTIME 263
#endif

#include "linux_clock.cpp"
