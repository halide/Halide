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
typedef unsigned int halide_hexagon_remote_uintptr_t;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_initialize_kernels)(const unsigned char* code, int codeLen, halide_hexagon_remote_uintptr_t* module_ptr) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_run)(halide_hexagon_remote_uintptr_t module_ptr, int offset, const halide_hexagon_remote_buffer* inputs, int inputsLen, halide_hexagon_remote_buffer* outputs, int outputsLen) __QAIC_HEADER_ATTRIBUTE;
__QAIC_HEADER_EXPORT int __QAIC_HEADER(halide_hexagon_remote_release_kernels)(halide_hexagon_remote_uintptr_t module_ptr, int size) __QAIC_HEADER_ATTRIBUTE;
#ifdef __cplusplus
}
#endif
#endif //_HALIDE_HEXAGON_REMOTE_H
