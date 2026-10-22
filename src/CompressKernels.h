#ifndef HALIDE_COMPRESS_KERNELS_H
#define HALIDE_COMPRESS_KERNELS_H

/** \file
 * Defines the compression applied to embedded GPU kernel sources when
 * compiling for a target with the compress_kernels feature.
 */

#include <vector>

namespace Halide {

struct Target;

namespace Internal {

/** Compress a kernel source blob into the format the runtime for the given
 * target decompresses: an MSZIP stream as produced by the Windows Compression
 * API (cabinet.dll) on Windows, and a zlib stream everywhere else. */
std::vector<char> compress_kernel_source(const std::vector<char> &src, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
