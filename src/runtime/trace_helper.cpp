#include "HalideRuntime.h"

extern "C" {

// A wrapper for halide_trace called by the pipeline. Halide Stmt IR
// has a hard time packing structs itself.
WEAK int halide_trace_helper(void *user_context,
                             const char *func,
                             void *value, int *coords,
                             int type_code, int type_bits, int type_lanes,
                             int code,
                             int parent_id, int value_index, int dimensions,
                             const char *trace_tag) {
    halide_trace_event_t event;
    event.func = func;
    event.value = value;
    event.coordinates = coords;
    event.trace_tag = trace_tag;
    event.type.code = (halide_type_code_t)type_code;
    event.type.bits = (uint8_t)type_bits;
    event.type.lanes = (uint16_t)type_lanes;
    event.event = (halide_trace_event_code_t)code;
    event.parent_id = parent_id;
    event.value_index = value_index;
    event.dimensions = dimensions;
    halide_msan_annotate_memory_is_initialized(user_context, &event, sizeof(event));
    halide_msan_annotate_memory_is_initialized(user_context, value, type_lanes * ((type_bits + 7) / 8));
    halide_msan_annotate_memory_is_initialized(user_context, coords, dimensions * sizeof(int32_t));
    return halide_trace(user_context, &event);
}

}
