#include "HalideRuntime.h"
#include "printer.h"

// The Windows Compression API, from cabinet.dll. Code that uses this module
// must link against cabinet.lib.
extern "C" {

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

typedef void *DECOMPRESSOR_HANDLE;
#define COMPRESS_ALGORITHM_MSZIP 2

WIN32API int CreateDecompressor(unsigned long Algorithm, void *AllocationRoutines,
                                DECOMPRESSOR_HANDLE *DecompressorHandle);
WIN32API int Decompress(DECOMPRESSOR_HANDLE DecompressorHandle,
                        const void *CompressedData, size_t CompressedDataSize,
                        void *UncompressedBuffer, size_t UncompressedBufferSize,
                        size_t *UncompressedDataSize);
WIN32API int CloseDecompressor(DECOMPRESSOR_HANDLE DecompressorHandle);
WIN32API unsigned long GetLastError();

WEAK bool halide_decompress_kernel_source(void *user_context, const void *src, int src_size, void *dst, int size) {
    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &decompressor)) {
        error(user_context) << "halide_decompress_kernels: CreateDecompressor() failed with "
                            << (uint64_t)GetLastError() << ".\n";
        return false;
    }
    size_t dst_len = 0;
    bool ok = Decompress(decompressor, src, (size_t)src_size, dst, (size_t)size, &dst_len);
    unsigned long err = ok ? 0 : GetLastError();
    CloseDecompressor(decompressor);
    if (!ok || dst_len != (size_t)size) {
        error(user_context) << "halide_decompress_kernels: Decompress() failed with "
                            << (uint64_t)err << ".\n";
        return false;
    }
    return true;
}

}  // extern "C"
