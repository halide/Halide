#include "HalideRuntime.h"

extern "C" {

extern long sysconf(int);

WEAK int halide_host_cpu_count() {
    // _SC_NPROCESSORS_ONLN
    return sysconf(58);
}
}
