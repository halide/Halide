#ifndef PERFORMANCE_LINTERS_H
#define PERFORMANCE_LINTERS_H

#include "Pipeline.h"
#include <memory>
#include <vector>

namespace Halide {

struct Target;

namespace Internal {

std::vector<std::unique_ptr<CustomPass>> get_default_linters(const Target &t);

}

}  // namespace Halide

#endif
