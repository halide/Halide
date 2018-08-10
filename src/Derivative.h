#ifndef HALIDE_DERIVATIVE_H
#define HALIDE_DERIVATIVE_H

/** \file
 *  Automatic differentiation
 */

#include "Module.h"
#include "Expr.h"
#include "Func.h"

#include <vector>
#include <array>
#include <set>

namespace Halide {

// function name & update_id, for initialization update_id == -1
using FuncKey = std::pair<std::string, int>;

/**
 *  Helper structure storing the adjoints Func.
 *  Use d(func) or d(buffer) to obtain the derivative Func.
 */
struct Derivative {
    std::map<FuncKey, Func> adjoints;

    Func operator()(const Func &func, int update_id = -1, bool bounded = true) const {
        std::string name = func.name();
        if (!bounded) {
            name += "_unbounded";
        }
        auto it = adjoints.find(FuncKey{name, update_id});
        assert(it != adjoints.end());
        return it->second;
    }

    Func operator()(const Buffer<> &buffer) const {
        auto it = adjoints.find(FuncKey{buffer.name(), -1});
        assert(it != adjoints.end());
        return it->second;
    }
};

/**
 *  Given a Func and a corresponding adjoint, (back)propagate the
 *  adjoint to all dependent Funcs, buffers, and parameters.
 *  The bounds of output and adjoint needs to be specified with pair {min, max}
 */
Derivative propagate_adjoints(const Func &output,
                              const Func &adjoint,
                              const std::vector<std::pair<Expr, Expr>> &output_bounds);
/**
 *  Given a Func and a corresponding adjoint buffer, (back)propagate the
 *  adjoint to all dependent Funcs, buffers, and parameters.
 */
Derivative propagate_adjoints(const Func &output,
                              const Buffer<float> &adjoint);
/**
 *  Given a scalar Func with size 1, (back)propagate the gradient
 *  to all dependent Funcs, buffers, and parameters.
 */
Derivative propagate_adjoints(const Func &output);
/**
 *  Given a Func and the tangents of inputs, (forward-)propagate the derivatives
 *  to the output.
 */
Func propagate_tangents(const Func &output,
                        const std::map<std::string, Func> &tangents);

struct PrintFuncOptions {
    bool ignore_non_adjoints = false;
    bool ignore_bc = false;
    int depth = -1;
    std::map<std::string, Expr> variables;
};
void print_func(const Func &func, const PrintFuncOptions &options = PrintFuncOptions());

namespace Internal {

void derivative_test();

}

}

#endif
