#include "HalideRuntime.h"

extern "C" {

extern long sysconf(int);

WEAK int halide_host_cpu_count() {
    // TODO: same as linux implementation for now. Reliable?
    return sysconf(84);
}
}
