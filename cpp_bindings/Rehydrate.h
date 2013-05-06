#ifndef HALIDE_REHYDRATE_H
#define HALIDE_REHYDRATE_H

#include "Func.h"
#include "Expr.h"
#include <string>
using std::string;

namespace Halide {
    Func rehydrate(const string envSexp, const string rootFunc);
}

#endif
