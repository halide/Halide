#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

extern "C" WEAK int halide_get_cpu_features(CpuFeatures *features) {
    // Hexagon has no CPU-specific Features.
    return halide_error_code_success;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
