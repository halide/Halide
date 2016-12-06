#ifndef HALIDE_TOOLS_IMAGE_H
#define HALIDE_TOOLS_IMAGE_H

#include "HalideBuffer.h"

namespace Halide {
namespace Tools {

#define USING_HALIDE_BUFFER

template< typename T >
using Image = Buffer<T>;

}   // namespace Tools
}   // mamespace Halide

#endif  // #ifndef HALIDE_TOOLS_IMAGE_H
