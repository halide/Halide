#ifndef HALIDE_ALLOCATION_BOUNDS_INFERENCE_H
#define HALIDE_ALLOCATION_BOUNDS_INFERENCE_H

#include <map>
#include <string>
#include "IR.h"

namespace Halide {
namespace Internal {

Stmt allocation_bounds_inference(Stmt s, const std::map<std::string, Function> &env);

}
}

#endif
