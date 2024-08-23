#include "HalideRuntime.h"
#include "runtime_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {

struct AllTheTypes {
    halide_buffer_t a;
    halide_device_interface_t b;
    halide_dimension_t c;
    halide_filter_argument_t d;
    halide_filter_metadata_t e;
    halide_parallel_task_t f;
    halide_pseudostack_slot_t g;
    halide_scalar_value_t h;
    halide_semaphore_acquire_t i;
    halide_semaphore_t j;
    halide_trace_event_t k;
    halide_trace_packet_t l;
    halide_type_t m;
};

WEAK void halide_unused_force_include_types() {
    static __attribute__((used)) AllTheTypes a;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
