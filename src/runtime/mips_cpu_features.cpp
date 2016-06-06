#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK CpuFeatures halide_get_cpu_features() {
    // MIPS has no CPU-specific Features.
    const uint64_t known = 0;
    const uint64_t available = 0;
    CpuFeatures features = {known, available};
    return features;
}

}}} // namespace Halide::Runtime::Internal
