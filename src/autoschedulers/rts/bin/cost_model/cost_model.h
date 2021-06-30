#ifndef HALIDE__bin___cost_model___cost_model_h
#define HALIDE__bin___cost_model___cost_model_h
#include <stdint.h>

// Forward declarations of the types used in the interface
// to the Halide pipeline.
//
// For the definitions of these structs, include HalideRuntime.h

// Halide's representation of a multi-dimensional array.
// Halide::Runtime::Buffer is a more user-friendly wrapper
// around this. Its declaration is in HalideBuffer.h
struct halide_buffer_t;

// Metadata describing the arguments to the generated function.
// Used to construct calls to the _argv version of the function.
struct halide_filter_metadata_t;

#ifndef HALIDE_MUST_USE_RESULT
#ifdef __has_attribute
#if __has_attribute(nodiscard)
#define HALIDE_MUST_USE_RESULT [[nodiscard]]
#elif __has_attribute(warn_unused_result)
#define HALIDE_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define HALIDE_MUST_USE_RESULT
#endif
#else
#define HALIDE_MUST_USE_RESULT
#endif
#endif

#ifndef HALIDE_FUNCTION_ATTRS
#define HALIDE_FUNCTION_ATTRS
#endif



#ifdef __cplusplus
extern "C" {
#endif

HALIDE_FUNCTION_ATTRS
int cost_model(int32_t _num_stages, int32_t _batch_size, int32_t _num_cores, struct halide_buffer_t *_pipeline_features_buffer, struct halide_buffer_t *_schedule_features_buffer, struct halide_buffer_t *_head1_filter_buffer, struct halide_buffer_t *_head1_bias_buffer, struct halide_buffer_t *_head2_filter_buffer, struct halide_buffer_t *_head2_bias_buffer, struct halide_buffer_t *_filter1_buffer, struct halide_buffer_t *_bias1_buffer, float _learning_rate, int32_t _timestep, int32_t _reference, struct halide_buffer_t *_true_runtime_buffer, struct halide_buffer_t *_prediction_output_buffer, struct halide_buffer_t *_loss_output_buffer);

HALIDE_FUNCTION_ATTRS
int cost_model_argv(void **args);

HALIDE_FUNCTION_ATTRS
const struct halide_filter_metadata_t *cost_model_metadata();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
