#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

// This is unused and expected to be optimized away; it exists solely to ensure
// that the halide_filter_metadata_t type is in the runtime module, so that
// Codegen_LLVM can access its description.
WEAK const halide_filter_metadata_t *unused_function_to_get_halide_filter_metadata_t_declared() { return NULL; }

} } }

