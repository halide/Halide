#include "Dimension.h"

#include "IR.h"
#include "IROperator.h"
#include <utility>

namespace Halide {
namespace Internal {

Dimension::Dimension(const Internal::Parameter &p, int d, Func f)
    : param(p), d(d), f(std::move(f)) {
    user_assert(param.defined())
        << "Can't access the dimensions of an undefined Parameter\n";
    user_assert(param.is_buffer())
        << "Can't access the dimensions of a scalar Parameter\n";
    user_assert(d >= 0 && d < param.dimensions())
        << "Can't access dimension " << d
        << " of a " << param.dimensions() << "-dimensional Parameter\n";
}

Expr Dimension::min() const {
    std::ostringstream s;
    s << param.name() << ".min." << d;
    return Variable::make(Int(32), s.str(), param);
}

Expr Dimension::extent() const {
    std::ostringstream s;
    s << param.name() << ".extent." << d;
    return Variable::make(Int(32), s.str(), param);
}

Expr Dimension::max() const {
    return min() + extent() - 1;
}

// DEPRECATED
Expr Dimension::min_estimate() const {
    return param.min_constraint_estimate(d);
}

// DEPRECATED
Expr Dimension::extent_estimate() const {
    return param.extent_constraint_estimate(d);
}

Expr Dimension::stride() const {
    std::ostringstream s;
    s << param.name() << ".stride." << d;
    return Variable::make(Int(32), s.str(), param);
}

Dimension Dimension::set_extent(const Expr &extent) {
    // Propagate constant bounds into estimates as well.
    if (is_const(extent)) {
        param.set_extent_constraint_estimate(d, extent);
    }
    param.set_extent_constraint(d, extent);
    return *this;
}

Dimension Dimension::set_min(const Expr &min) {
    // Propagate constant bounds into estimates as well.
    if (is_const(min)) {
        param.set_min_constraint_estimate(d, min);
    }
    param.set_min_constraint(d, min);
    return *this;
}

Dimension Dimension::set_stride(const Expr &stride) {
    param.set_stride_constraint(d, stride);
    return *this;
}

Dimension Dimension::set_bounds(const Expr &min, const Expr &extent) {
    return set_min(min).set_extent(extent);
}

Dimension Dimension::set_estimate(const Expr &min, const Expr &extent) {
    // Update the estimates on the linked Func as well.
    // (This matters mainly for OutputImageParams.)
    // Note that while it's possible/legal for a Dimension to have an undefined
    // Func, you shouldn't ever call set_estimate on such an instance.
    internal_assert(f.defined());
    f.set_estimate(f.args()[d], min, extent);
    param.set_min_constraint_estimate(d, min);
    param.set_extent_constraint_estimate(d, extent);
    return *this;
}

Dimension Dimension::dim(int i) const {
    return Dimension(param, i, f);
}

}  // namespace Internal
}  // namespace Halide
