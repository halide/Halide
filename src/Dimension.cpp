#include "Dimension.h"
#include "IR.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

Dimension::Dimension(const Internal::Parameter &p, int d) : param(p), d(d) {
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

Expr Dimension::min_estimate() const {
    return param.get_min_constraint_estimate(d);
}

Expr Dimension::extent_estimate() const {
    return param.get_extent_constraint_estimate(d);
}

Expr Dimension::stride() const {
    std::ostringstream s;
    s << param.name() << ".stride." << d;
    return Variable::make(Int(32), s.str(), param);
}

Dimension Dimension::set_extent(Expr extent) {
    param.set_extent_constraint(d, extent);
    return *this;
}

Dimension Dimension::set_min(Expr min) {
    param.set_min_constraint(d, min);
    return *this;
}

Dimension Dimension::set_stride(Expr stride) {
    param.set_stride_constraint(d, stride);
    return *this;
}

Dimension Dimension::set_bounds(Expr min, Expr extent) {
    return set_min(min).set_extent(extent);
}

Dimension Dimension::set_min_estimate(Expr min) {
    param.set_min_constraint_estimate(d, min);
    return *this;
}

Dimension Dimension::set_extent_estimate(Expr stride) {
    param.set_extent_constraint_estimate(d, stride);
    return *this;
}

Dimension Dimension::set_bounds_estimate(Expr min, Expr extent) {
    return set_min_estimate(min).set_extent_estimate(extent);
}

Dimension Dimension::dim(int i) {
    return Dimension(param, i);
}

const Dimension Dimension::dim(int i) const {
    return Dimension(param, i);
}

}  // namespace Internal
}  // namespace Halide
