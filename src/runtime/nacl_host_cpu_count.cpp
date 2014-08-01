#include "runtime_internal.h"

extern "C" {

extern long sysconf(int);

WEAK int halide_host_cpu_count() {
    return sysconf(1);
}

}
