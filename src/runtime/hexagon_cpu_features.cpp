#include "HalideRuntime.h"
#include "cpu_features.h"

extern "C" {

WEAK int halide_get_cpu_features(Halide::Runtime::Internal::CpuFeatures *features) {
    // Hexagon has no CPU-specific Features.
    return halide_error_code_success;
}

}  // extern "C" linkage
