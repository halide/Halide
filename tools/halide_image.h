#ifndef HALIDE_TOOLS_IMAGE_H
#define HALIDE_TOOLS_IMAGE_H

/** \file
 *
 * This allows code that relied on halide_image.h and
 * Halide::Tools::Image to continue to work with newer versions of
 * Halide where HalideBuffer.h and Halide::Buffer are the way to work
 * with data.
 *
 * Besides mapping Halide::Tools::Image to Halide::Buffer, it defines
 * USING_HALIDE_BUFFER to allow code to conditionally compile for one
 * or the other.
 *
 * It is intended as a stop-gap measure until the code can be updated.
 */

#include "HalideBuffer.h"

namespace Halide {
namespace Tools {

#define USING_HALIDE_BUFFER

template< typename T >
using Image = Buffer<T>;

}   // namespace Tools
}   // mamespace Halide

#endif  // #ifndef HALIDE_TOOLS_IMAGE_H
