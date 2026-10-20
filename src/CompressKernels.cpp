#include "CompressKernels.h"
#include "Error.h"
#include "Target.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/Compression.h>

#if LLVM_ENABLE_ZLIB && __has_include(<zlib.h>)
#define HALIDE_HAS_ZLIB 1
#include <zlib.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Halide {
namespace Internal {

namespace {

#ifdef HALIDE_HAS_ZLIB

void append_le(std::vector<char> &out, uint64_t value, int bytes) {
    for (int i = 0; i < bytes; i++) {
        out.push_back((char)((value >> (8 * i)) & 0xff));
    }
}

// The buffer-mode format of the Windows Compression API for
// COMPRESS_ALGORITHM_MSZIP, which is what Decompress() in cabinet.dll expects:
//
//   header: 6 magic bytes, 1 crc byte, 1 algorithm byte (2 = MSZIP),
//           u64 uncompressed size, u64 uncompressed size of the first chunk
//   chunks: u32 size (counting what follows), "CK", raw deflate stream
//
// Every chunk holds 32KB of input (the last one possibly less), and is a
// complete deflate stream that may refer back into the previous chunks.
// cabinet.dll is known to reject some streams that zlib produces with the
// default memLevel, but accepts those produced with memLevel 9 (which lets a
// whole chunk fit in a single deflate block).
std::vector<char> compress_mszip(const std::vector<char> &src) {
    constexpr size_t chunk_size = 32 * 1024;
    const uint8_t *in = (const uint8_t *)src.data();
    const size_t in_size = src.size();

    std::vector<char> out;
    const uint8_t magic[6] = {0x0a, 0x51, 0xe5, 0xc0, 0x18, 0x00};
    out.insert(out.end(), magic, magic + 6);
    out.push_back(0);  // crc, filled in below
    out.push_back(2);  // COMPRESS_ALGORITHM_MSZIP
    append_le(out, in_size, 8);
    append_le(out, std::min(in_size, chunk_size), 8);

    uLong crc = crc32(0, (const Bytef *)out.data(), 6);
    crc = crc32(crc, (const Bytef *)out.data() + 7, 17);
    out[6] = (char)(crc & 0xff);

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int ret = deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 9, Z_DEFAULT_STRATEGY);
    internal_assert(ret == Z_OK) << "deflateInit2 failed: " << ret << "\n";

    for (size_t offset = 0; offset < in_size; offset += chunk_size) {
        const size_t n = std::min(chunk_size, in_size - offset);
        ret = deflateReset(&zs);
        internal_assert(ret == Z_OK) << "deflateReset failed: " << ret << "\n";
        if (offset > 0) {
            // The deflate window is 32KB, so the previous chunk is all the
            // history a stream can refer to.
            ret = deflateSetDictionary(&zs, in + offset - chunk_size, chunk_size);
            internal_assert(ret == Z_OK) << "deflateSetDictionary failed: " << ret << "\n";
        }

        const size_t header_at = out.size();
        append_le(out, 0, 4);  // chunk size, filled in below
        out.push_back('C');
        out.push_back('K');

        const size_t stream_at = out.size();
        const size_t bound = deflateBound(&zs, n);
        out.resize(stream_at + bound);
        zs.next_in = (Bytef *)(in + offset);
        zs.avail_in = (uInt)n;
        zs.next_out = (Bytef *)out.data() + stream_at;
        zs.avail_out = (uInt)bound;
        ret = deflate(&zs, Z_FINISH);
        internal_assert(ret == Z_STREAM_END) << "deflate failed: " << ret << "\n";
        const size_t stream_size = bound - zs.avail_out;
        out.resize(stream_at + stream_size);

        const uint32_t size = (uint32_t)(stream_size + 2);
        for (int i = 0; i < 4; i++) {
            out[header_at + i] = (char)((size >> (8 * i)) & 0xff);
        }
    }
    deflateEnd(&zs);
    return out;
}

#endif

}  // namespace

std::vector<char> compress_kernel_source(const std::vector<char> &src, const Target &target) {
#ifdef HALIDE_HAS_ZLIB
    user_assert(src.size() <= 0x7fffffff)
        << "Kernel source of " << src.size() << " bytes is too large to embed.\n";
    if (target.os == Target::Windows) {
        return compress_mszip(src);
    }
    llvm::SmallVector<uint8_t, 0> compressed;
    llvm::compression::zlib::compress(llvm::ArrayRef<uint8_t>((const uint8_t *)src.data(), src.size()),
                                      compressed, llvm::compression::zlib::BestSizeCompression);
    return std::vector<char>(compressed.begin(), compressed.end());
#else
    user_error << "The compress_kernels target feature requires Halide to be built "
               << "against an LLVM with zlib support (LLVM_ENABLE_ZLIB), and with the zlib headers available.\n";
    return {};
#endif
}

}  // namespace Internal
}  // namespace Halide
