#include "HalideRuntime.h"
#include "cpu_features.h"

extern "C" {

WEAK int halide_get_cpu_features(Halide::Runtime::Internal::CpuFeatures *features) {
    // There isn't a way to determine what features are available --
    // if a feature we need isn't available, we couldn't
    // even load. So just declare that all wasm-related features are
    // known and available.
    Halide::Runtime::Internal::halide_set_known_cpu_feature(features, halide_target_feature_wasm_simd128);
    Halide::Runtime::Internal::halide_set_available_cpu_feature(features, halide_target_feature_wasm_simd128);
    return halide_error_code_success;
}

}  // extern "C" linkage
