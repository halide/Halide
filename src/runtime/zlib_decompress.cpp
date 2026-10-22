#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

// From zlib. Code that uses this module must link against libz.
int uncompress(unsigned char *dest, unsigned long *dest_len,
               const unsigned char *source, unsigned long source_len);

WEAK bool halide_decompress_kernel_source(void *user_context, const void *src, int src_size, void *dst, int size) {
    unsigned long dst_len = (unsigned long)size;
    int ret = uncompress((unsigned char *)dst, &dst_len, (const unsigned char *)src, (unsigned long)src_size);
    if (ret != 0 || dst_len != (unsigned long)size) {
        error(user_context) << "halide_decompress_kernels: zlib uncompress() failed with " << ret << ".\n";
        return false;
    }
    return true;
}

}  // extern "C"
