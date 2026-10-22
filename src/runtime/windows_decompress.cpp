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
#define COMPRESS_RAW (1 << 29)

WIN32API int CreateDecompressor(unsigned long Algorithm, void *AllocationRoutines,
                                DECOMPRESSOR_HANDLE *DecompressorHandle);
WIN32API int Decompress(DECOMPRESSOR_HANDLE DecompressorHandle,
                        const void *CompressedData, size_t CompressedDataSize,
                        void *UncompressedBuffer, size_t UncompressedBufferSize,
                        size_t *UncompressedDataSize);
WIN32API int CloseDecompressor(DECOMPRESSOR_HANDLE DecompressorHandle);
WIN32API unsigned long GetLastError();

// The source is a sequence of MSZIP blocks, each preceded by its size in bytes
// as a little-endian u32, and each holding 32KB of the source except the last
// one. See CompressKernels.cpp.
WEAK bool halide_decompress_kernel_source(void *user_context, const void *src, int src_size, void *dst, int size) {
    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP | COMPRESS_RAW, nullptr, &decompressor)) {
        error(user_context) << "halide_decompress_kernels: CreateDecompressor() failed with "
                            << (uint64_t)GetLastError() << ".\n";
        return false;
    }
    const uint8_t *in = (const uint8_t *)src;
    uint8_t *out = (uint8_t *)dst;
    int in_offset = 0;
    int out_offset = 0;
    bool ok = true;
    while (ok && out_offset < size) {
        if (src_size - in_offset < 4) {
            ok = false;
            break;
        }
        const uint32_t block_size = in[in_offset] | (in[in_offset + 1] << 8) |
                                    (in[in_offset + 2] << 16) | ((uint32_t)in[in_offset + 3] << 24);
        in_offset += 4;
        if (block_size > (uint32_t)(src_size - in_offset)) {
            ok = false;
            break;
        }
        const int n = size - out_offset < 32 * 1024 ? size - out_offset : 32 * 1024;
        ok = Decompress(decompressor, in + in_offset, block_size, out + out_offset, n, nullptr);
        in_offset += block_size;
        out_offset += n;
    }
    unsigned long err = ok ? 0 : GetLastError();
    CloseDecompressor(decompressor);
    if (!ok || in_offset != src_size) {
        error(user_context) << "halide_decompress_kernels: Decompress() failed with "
                            << (uint64_t)err << " at offset " << in_offset << ".\n";
        return false;
    }
    return true;
}

}  // extern "C"
