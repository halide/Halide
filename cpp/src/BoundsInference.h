#ifndef BOUNDS_INFERENCE_H
#define BOUNDS_INFERENCE_H

#include "IR.h"

namespace Halide {
namespace Internal {

// Take a partially lowered statement that includes symbolic
// representations of the bounds over which things should be realized,
// and inject expressions defining those bounds.

Stmt bounds_inference(Stmt, const vector<string> &realization_order, const map<string, Function> &environment);

}
}

#endif
