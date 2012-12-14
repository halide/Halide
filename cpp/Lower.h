#ifndef HALIDE_INTERNAL_LOWER_H
#define HALIDE_INTERNAL_LOWER_H

#include "IR.h"
#include "Func.h"

namespace Halide {
namespace Internal {

Stmt lower(Func f);

void lower_test();

}
}

#endif
