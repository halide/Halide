#ifndef HALIDE_IRCOMPARATOR_H_
#define HALIDE_IRCOMPARATOR_H_
#include "Pipeline.h"

namespace Halide {
namespace Internal {
bool equal(const Pipeline &p1, const Pipeline &p2);
}
}  // namespace Halide

#endif  // HALIDE_SERIALIZATION_IRCOMPARATOR_H_
