#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK CpuFeatures halide_get_cpu_features() {
    // For now, no version specific features, though RISCV promises to have many.
    return CpuFeatures();
}

}}} // namespace Halide::Runtime::Internal
