#include "HalideRuntime.h"

extern "C" {

extern long sysconf(int);

WEAK int halide_host_cpu_count() {
    // Works for Android ARMv7. Probably bogus on other platforms.
    return sysconf(97);
}

}
