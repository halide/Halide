#ifndef HALIDE__pipeline_hvx64_h
#define HALIDE__pipeline_hvx64_h
#ifndef HALIDE_ATTRIBUTE_ALIGN
  #ifdef _MSC_VER
    #define HALIDE_ATTRIBUTE_ALIGN(x) __declspec(align(x))
  #else
    #define HALIDE_ATTRIBUTE_ALIGN(x) __attribute__((aligned(x)))
  #endif
#endif
#ifndef BUFFER_T_DEFINED
#define BUFFER_T_DEFINED
#include <stdbool.h>
#include <stdint.h>
typedef struct buffer_t {
    uint64_t dev;
    uint8_t* host;
    int32_t extent[4];
    int32_t stride[4];
    int32_t min[4];
    int32_t elem_size;
    HALIDE_ATTRIBUTE_ALIGN(1) bool host_dirty;
    HALIDE_ATTRIBUTE_ALIGN(1) bool dev_dirty;
    HALIDE_ATTRIBUTE_ALIGN(1) uint8_t _padding[10 - sizeof(void *)];
} buffer_t;
#endif
struct halide_filter_metadata_t;
#ifndef HALIDE_FUNCTION_ATTRS
#define HALIDE_FUNCTION_ATTRS
#endif
#ifdef __cplusplus
extern "C" {
#endif
int pipeline_hvx64(buffer_t *_in1_buffer, buffer_t *_in2_buffer, buffer_t *_f_buffer) HALIDE_FUNCTION_ATTRS;
int pipeline_hvx64_argv(void **args) HALIDE_FUNCTION_ATTRS;
// Result is never null and points to constant static data
extern const struct halide_filter_metadata_t *pipeline_hvx64_metadata();
#ifdef __cplusplus
}  // extern "C"
#endif
#endif
