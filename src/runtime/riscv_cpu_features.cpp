#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

extern "C" WEAK int halide_get_cpu_features(Halide::Runtime::Internal::CpuFeatures *features) {
    // For now, no version specific features, though RISCV promises to have many.
    return halide_error_code_success;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
