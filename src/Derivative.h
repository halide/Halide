#ifndef HALIDE_DERIVATIVE_H
#define HALIDE_DERIVATIVE_H

/** \file
 *  Automatic differentiation
 */

#include "Expr.h"
#include "Func.h"

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

    // These all return an undefined Func if no derivative is found
    // (typically, if the input Funcs aren't differentiable)
    Func operator()(const Func &func, int update_id = -1) const;
    Func operator()(const Buffer<> &buffer) const;
    Func operator()(const Param<> &param) const;
    Func operator()(const std::string &name) const;

private:
    const std::map<FuncKey, Func> adjoints;
};

/**
 *  Given a Func and a corresponding adjoint, (back)propagate the
 *  adjoint to all dependent Funcs, buffers, and parameters.
 *  The bounds of output and adjoint need to be specified with pair {min, extent}
 *  For each Func the output depends on, and for the pure definition and
 *  each update of that Func, it generates a derivative Func stored in
 *  the Derivative.
 */
Derivative propagate_adjoints(const Func &output,
                              const Func &adjoint,
                              const Region &output_bounds);
/**
 *  Given a Func and a corresponding adjoint buffer, (back)propagate the
 *  adjoint to all dependent Funcs, buffers, and parameters.
 *  For each Func the output depends on, and for the pure definition and
 *  each update of that Func, it generates a derivative Func stored in
 *  the Derivative.
 */
Derivative propagate_adjoints(const Func &output,
                              const Buffer<float> &adjoint);
/**
 *  Given a scalar Func with size 1, (back)propagate the gradient
 *  to all dependent Funcs, buffers, and parameters.
 *  For each Func the output depends on, and for the pure definition and
 *  each update of that Func, it generates a derivative Func stored in
 *  the Derivative.
 */
Derivative propagate_adjoints(const Func &output);

}  // namespace Halide

#endif
