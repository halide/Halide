#ifndef HALIDE_DERIVATIVE_H
#define HALIDE_DERIVATIVE_H

/** \file
 *  Automatic differentiation
 */

#include "Expr.h"
#include "Func.h"
#include "Module.h"

#include <map>
#include <string>
#include <vector>

namespace Halide {

/**
 *  Helper structure storing the adjoints Func.
 *  Use d(func) or d(buffer) to obtain the derivative Func.
 */
class Derivative {
public:
    // function name & update_id, for initialization update_id == -1
    using FuncKey = std::pair<std::string, int>;

    explicit Derivative(const std::map<FuncKey, Func> &adjoints_in)
        : adjoints(adjoints_in) {
    }
    explicit Derivative(std::map<FuncKey, Func> &&adjoints_in)
        : adjoints(std::move(adjoints_in)) {
    }

    Func operator()(const Func &func, int update_id = -1) const;
    Func operator()(const Buffer<> &buffer) const;
    Func operator()(const Param<> &param) const;

    /** Get the entire chain of new synthesized Funcs that compute the
     * derivative of a given user-written Func for the purpose of
     * scheduling. */
    std::vector<Func> funcs(const Func &func) const;

private:
    const std::map<FuncKey, Func> adjoints;
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

}  // namespace Halide

#endif
