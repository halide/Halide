#include "HalideRuntime.h"
#include "cpu_features.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal {
WEAK halide_can_use_target_features_t custom_can_use_target_features = halide_default_can_use_target_features;
}}}

extern "C" {

WEAK halide_can_use_target_features_t halide_set_custom_can_use_target_features(halide_can_use_target_features_t fn) {
    halide_can_use_target_features_t result = custom_can_use_target_features;
    custom_can_use_target_features = fn;
    return result;
}

WEAK int halide_can_use_target_features(int count, const uint64_t *features) {
    return (*custom_can_use_target_features)(count, features);
}

WEAK int halide_default_can_use_target_features(int count, const uint64_t *features) {
    // cpu features should never change, so call once and cache.
    static CpuFeatures cpu_features = halide_get_cpu_features();

    if (count != CpuFeatures::kWordCount) {
        // This should not happen unless our runtime is out of sync with the rest of libHalide.
 #ifdef DEBUG_RUNTIME
        debug(NULL) << "count " << count << " CpuFeatures::kWordCount " << CpuFeatures::kWordCount << "\n";
#endif
        halide_error(NULL, "Internal error: wrong structure size passed to halide_can_use_target_features()\n");
    }
    for (int i = 0; i < CpuFeatures::kWordCount; ++i) {
        uint64_t m;
        if ((m = (features[i] & cpu_features.known[i])) != 0) {
            if ((m & cpu_features.available[i]) != m) {
                return 0;
            }
        }
    }

    return 1;
}

}
