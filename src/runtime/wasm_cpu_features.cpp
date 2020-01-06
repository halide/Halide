#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK CpuFeatures halide_get_cpu_features() {
    CpuFeatures features;

    // There isn't a way to determine what features are available --
    // if a feature we need isn't available, we couldn't
    // even load. So just declare that all wasm-related features are
    // known and available.
    features.set_known(halide_target_feature_wasm_simd128);
    features.set_available(halide_target_feature_wasm_simd128);

    return features;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
