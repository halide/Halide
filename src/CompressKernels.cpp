#include "CompressKernels.h"
#include "Error.h"
#include "Target.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Compression.h>

#include <algorithm>
#include <cstdint>

namespace Halide {
namespace Internal {

namespace {

std::vector<uint8_t> zlib_compress(const uint8_t *data, size_t size) {
    llvm::SmallVector<uint8_t, 0> compressed;
    llvm::compression::zlib::compress(llvm::ArrayRef<uint8_t>(data, size), compressed,
                                      llvm::compression::zlib::BestSizeCompression);
    return std::vector<uint8_t>(compressed.begin(), compressed.end());
}

// The MSZIP format that the Windows Compression API (cabinet.dll) decompresses
// in block mode (COMPRESS_ALGORITHM_MSZIP | COMPRESS_RAW), one block per call.
// Each MSZIP block is "CK" followed by a raw deflate stream of at most 32KB of
// input (see [MS-MCI] 2.1). That is a zlib stream of the same input, with the
// 2-byte zlib header replaced by "CK", and without the 4-byte Adler-32
// trailer. Block mode stores no sizes, so each block is preceded by its size
// in bytes as a little-endian u32. Every block but the last holds exactly
// 32KB of input, so the runtime can derive the uncompressed sizes.
std::vector<char> compress_mszip(const std::vector<char> &src) {
    constexpr size_t block_size = 32 * 1024;
    const uint8_t *in = (const uint8_t *)src.data();

    std::vector<char> out;
    for (size_t offset = 0; offset < src.size(); offset += block_size) {
        std::vector<uint8_t> block = zlib_compress(in + offset, std::min(block_size, src.size() - offset));
        internal_assert(block.size() > 6) << "Unexpectedly short zlib stream\n";
        const uint32_t size = (uint32_t)(block.size() - 4);  // "CK" + deflate stream
        for (int i = 0; i < 4; i++) {
            out.push_back((char)((size >> (8 * i)) & 0xff));
        }
        out.push_back('C');
        out.push_back('K');
        out.insert(out.end(), block.begin() + 2, block.end() - 4);
    }
    return out;
}

}  // namespace

std::vector<char> compress_kernel_source(const std::vector<char> &src, const Target &target) {
    user_assert(llvm::compression::zlib::isAvailable())
        << "The compress_kernels target feature requires Halide to be built "
        << "against an LLVM with zlib support (LLVM_ENABLE_ZLIB).\n";
    user_assert(src.size() <= 0x7fffffff)
        << "Kernel source of " << src.size() << " bytes is too large to embed.\n";
    if (target.os == Target::Windows) {
        return compress_mszip(src);
    }
    std::vector<uint8_t> compressed = zlib_compress((const uint8_t *)src.data(), src.size());
    return std::vector<char>(compressed.begin(), compressed.end());
}

}  // namespace Internal
}  // namespace Halide
