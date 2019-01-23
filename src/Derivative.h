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
struct Derivative {
    // function name & update_id, for initialization update_id == -1
    using FuncKey = std::pair<std::string, int>;

    std::map<FuncKey, Func> adjoints;

    Func operator()(const Func &func, int update_id = -1, bool bounded = true) const {
        std::string name = func.name();
        if (!bounded) {
            name += "_unbounded";
        }
        auto it = adjoints.find(FuncKey{ name, update_id });
        internal_assert(it != adjoints.end()) << "Could not find Func " << name << "\n";
        return it->second;
    }

    Func operator()(const Buffer<> &buffer) const {
        auto it = adjoints.find(FuncKey{ buffer.name(), -1 });
        internal_assert(it != adjoints.end()) << "Could not find Buffer " << buffer.name() << "\n";
        return it->second;
    }

    /** Get the entire chain of new synthesized Funcs that compute the
     * derivative of a given user-written Func for the purpose of
     * scheduling. */
    std::vector<Func> funcs(const Func &func) const {
        std::vector<Func> result;
        FuncKey k {func.name(), -1};
        FuncKey k_unbounded = k;
        k_unbounded.first += "_unbounded";
        for (int i = func.num_update_definitions() - 1; i >= -1; i--) {
            k.second = k_unbounded.second = i;
            auto it = adjoints.find(k);
            internal_assert(it != adjoints.end()) << "Could not find derivative of " << k.first << " " << k.second << "\n";
            result.push_back(it->second);
            it = adjoints.find(k_unbounded);
            if (it != adjoints.end()) {
                result.push_back(it->second);
            }
        }
        return result;
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

}  // namespace Halide

#endif
