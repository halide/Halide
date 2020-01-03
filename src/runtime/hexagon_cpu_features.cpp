#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK CpuFeatures halide_get_cpu_features() {
    // Hexagon has no CPU-specific Features.
    return CpuFeatures();
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
