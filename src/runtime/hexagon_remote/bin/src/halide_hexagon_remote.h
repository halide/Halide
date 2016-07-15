#ifndef _HALIDE_HEXAGON_REMOTE_H
#define _HALIDE_HEXAGON_REMOTE_H
#ifndef __QAIC_HEADER
#define __QAIC_HEADER(ff) ff
#endif //__QAIC_HEADER

#ifndef __QAIC_HEADER_EXPORT
#define __QAIC_HEADER_EXPORT
#endif // __QAIC_HEADER_EXPORT

#ifndef __QAIC_HEADER_ATTRIBUTE
#define __QAIC_HEADER_ATTRIBUTE
#endif // __QAIC_HEADER_ATTRIBUTE

#ifndef __QAIC_IMPL
#define __QAIC_IMPL(ff) ff
#endif //__QAIC_IMPL

#ifndef __QAIC_IMPL_EXPORT
#define __QAIC_IMPL_EXPORT
#endif // __QAIC_IMPL_EXPORT

#ifndef __QAIC_IMPL_ATTRIBUTE
#define __QAIC_IMPL_ATTRIBUTE
#endif // __QAIC_IMPL_ATTRIBUTE
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _halide_hexagon_remote_buffer__seq_octet _halide_hexagon_remote_buffer__seq_octet;
typedef _halide_hexagon_remote_buffer__seq_octet halide_hexagon_remote_buffer;
struct _halide_hexagon_remote_buffer__seq_octet {
   unsigned char* data;
   int dataLen;
};
typedef unsigned int halide_hexagon_remote_handle_t;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_initialize_kernels)(const unsigned char* code, int codeLen, halide_hexagon_remote_handle_t* module_ptr) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_get_symbol_v2)(halide_hexagon_remote_handle_t module_ptr, const char* name, int nameLen, halide_hexagon_remote_handle_t* sym_ptr) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_power_hvx_on)(void) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_power_hvx_off)(void) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_run)(halide_hexagon_remote_handle_t module_ptr, halide_hexagon_remote_handle_t symbol, const halide_hexagon_remote_buffer* input_buffers, int input_buffersLen, halide_hexagon_remote_buffer* output_buffers, int output_buffersLen, const halide_hexagon_remote_buffer* input_scalars, int input_scalarsLen) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_release_kernels)(halide_hexagon_remote_handle_t module_ptr, int size) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_poll_log)(char* log, int logLen, int* read_size) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_poll_profiler_state)(int* func, int* threads) __QAIC_HEADER_ATTRIBUTE;
#ifdef __cplusplus
}
#endif
#endif //_HALIDE_HEXAGON_REMOTE_H
