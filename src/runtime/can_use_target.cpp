#include "HalideRuntime.h"

extern "C" {

WEAK int halide_can_use_target_features(uint64_t features) {
    return halide_default_can_use_target_features(features);
}

WEAK int halide_default_can_use_target_features(uint64_t features) {
    // cpu features should never change, so call once and cache.
    static bool initialized = false;
    static CpuFeatures cpu_features;
    if (!initialized) {
        cpu_features = halide_get_cpu_features();
        initialized = true;
    }

    uint64_t m;
    if ((m = (features & cpu_features.known)) != 0) {
        if ((m & cpu_features.available) != m) {
            return 0;
        }
    }

    return 1;
}

}
