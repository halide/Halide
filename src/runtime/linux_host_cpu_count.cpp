#include "HalideRuntime.h"

extern "C" {

extern long sysconf(int);
extern int sched_getaffinity(int32_t pid, size_t cpusetsize, uint32_t *cpuset);

WEAK int halide_host_cpu_count() {
    uint32_t count = 0, cpuset[32];  // enough bits for 1024 CPUs
    memset(cpuset, 0, sizeof(cpuset));
    int rv = sched_getaffinity(0, sizeof(cpuset), cpuset);
    if (rv == 0) {
        for (size_t word = 0; word < sizeof(cpuset) / sizeof(cpuset[0]); word++) {
            count += __builtin_popcount(cpuset[word]);
        }
    }
    if (rv < 0 || count == 0) {
        return sysconf(84);
    } else {
        return count;
    }
}

}  // extern "C"
