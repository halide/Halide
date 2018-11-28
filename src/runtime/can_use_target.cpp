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

// C++11 (and thus, static_assert) aren't available here. Use this old standby:
#define fake_static_assert(VALUE) do { enum { __some_value = 1 / (!!(VALUE)) }; } while (0)

WEAK int halide_default_can_use_target_features(int count, const uint64_t *features) {
    // cpu features should never change, so call once and cache.
    // Note that since CpuFeatures has a (trivial) ctor, compilers may insert guards
    // for threadsafe initialization (per C++11); this can fail at link time
    // on some systems (MSVC) because our runtime is a special beast. We'll
    // work around this by using a sentinel for the initialization flag and
    // some horribleness with memcpy (which we can do since CpuFeatures is still POD).
    static bool initialized = false;
    static uint64_t cpu_features_storage[sizeof(CpuFeatures)/sizeof(uint64_t)] = {0};
    fake_static_assert(sizeof(cpu_features_storage) == sizeof(CpuFeatures));
    if (!initialized) {
        CpuFeatures tmp = halide_get_cpu_features();
        memcpy(&cpu_features_storage, &tmp, sizeof(tmp));
        initialized = true;
    }

    if (count != CpuFeatures::kWordCount) {
        // This should not happen unless our runtime is out of sync with the rest of libHalide.
 #ifdef DEBUG_RUNTIME
        debug(NULL) << "count " << count << " CpuFeatures::kWordCount " << CpuFeatures::kWordCount << "\n";
#endif
        halide_error(NULL, "Internal error: wrong structure size passed to halide_can_use_target_features()\n");
    }
    const CpuFeatures* cpu_features = reinterpret_cast<const CpuFeatures*>(&cpu_features_storage[0]);
    for (int i = 0; i < CpuFeatures::kWordCount; ++i) {
        uint64_t m;
        if ((m = (features[i] & cpu_features->known[i])) != 0) {
            if ((m & cpu_features->available[i]) != m) {
                return 0;
            }
        }
    }

    return 1;
}

}
