#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {
WEAK halide_can_use_target_features_t custom_can_use_target_features = halide_default_can_use_target_features;
}}}

extern "C" {

WEAK halide_can_use_target_features_t halide_set_custom_can_use_target_features(halide_can_use_target_features_t fn) {
    halide_can_use_target_features_t result = custom_can_use_target_features;
    custom_can_use_target_features = fn;
    return result;
}

WEAK int halide_can_use_target_features(uint64_t features) {
    return (*custom_can_use_target_features)(features);
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
