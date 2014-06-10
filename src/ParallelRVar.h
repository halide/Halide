#ifndef HALIDE_PARALLEL_RVAR_H
#define HALIDE_PARALLEL_RVAR_H

#include "Function.h"
#include <string>

// TODO: file-level comment

namespace Halide {
namespace Internal {

// TODO: comment
bool can_parallelize_rvar(const std::string &v,
                          const std::string &f,
                          const ReductionDefinition &r);

}
}

#endif
