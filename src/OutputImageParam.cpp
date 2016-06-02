#include "OutputImageParam.h"
#include "IROperator.h"

namespace Halide {

OutputImageParam::OutputImageParam(const Internal::Parameter &p, Argument::Kind k) :
    param(p), kind(k) {
}

const std::string &OutputImageParam::name() const {
    return param.name();
}

Type OutputImageParam::type() const {
    return param.type();
}

bool OutputImageParam::defined() const {
    return param.defined();
}

OutputImageParam::Dimension OutputImageParam::dim(int i) {
    user_assert(defined())
        << "Can't access the dimensions of an undefined ImageParam\n";
    user_assert(i >= 0 && i < dimensions())
        << "Can't access dimension " << i
        << " of a " << dimensions() << "-dimensional ImageParam\n";
    return OutputImageParam::Dimension(param, i);
}

const OutputImageParam::Dimension OutputImageParam::dim(int i) const {
    user_assert(defined())
        << "Can't access the dimensions of an undefined ImageParam\n";
    user_assert(i >= 0 && i < dimensions())
        << "Can't access dimension " << i
        << " of a " << dimensions() << "-dimensional ImageParam\n";
    return OutputImageParam::Dimension(param, i);
}

Expr OutputImageParam::Dimension::min() const {
    std::ostringstream s;
    s << param.name() << ".min." << d;
    return Internal::Variable::make(Int(32), s.str(), param);
}

Expr OutputImageParam::Dimension::extent() const {
    std::ostringstream s;
    s << param.name() << ".extent." << d;
    return Internal::Variable::make(Int(32), s.str(), param);
}

Expr OutputImageParam::Dimension::max() const {
    return min() + extent() - 1;
}

Expr OutputImageParam::Dimension::stride() const {
    std::ostringstream s;
    s << param.name() << ".stride." << d;
    return Internal::Variable::make(Int(32), s.str(), param);
}
int OutputImageParam::host_alignment() const {
    return param.host_alignment();
}

OutputImageParam::Dimension OutputImageParam::Dimension::set_extent(Expr extent) {
    param.set_extent_constraint(d, extent);
    return *this;
}

OutputImageParam::Dimension OutputImageParam::Dimension::set_min(Expr min) {
    param.set_min_constraint(d, min);
    return *this;
}

OutputImageParam::Dimension OutputImageParam::Dimension::set_stride(Expr stride) {
    param.set_stride_constraint(d, stride);
    return *this;
}


OutputImageParam::Dimension OutputImageParam::Dimension::set_bounds(Expr min, Expr extent) {
    return set_min(min).set_extent(extent);
}

OutputImageParam::Dimension OutputImageParam::Dimension::dim(int i) {
    return OutputImageParam::Dimension(param, i);
}

const OutputImageParam::Dimension OutputImageParam::Dimension::dim(int i) const {
    return OutputImageParam::Dimension(param, i);
}

OutputImageParam &OutputImageParam::set_host_alignment(int bytes) {
    param.set_host_alignment(bytes);
    return *this;
}

int OutputImageParam::dimensions() const {
    return param.dimensions();
}

Expr OutputImageParam::left() const {
    user_assert(dimensions() > 0) << "Can't ask for the left of a zero-dimensional image\n";
    return dim(0).min();
}

Expr OutputImageParam::right() const {
    user_assert(dimensions() > 0) << "Can't ask for the right of a zero-dimensional image\n";
    return dim(0).max();
}

Expr OutputImageParam::top() const {
    user_assert(dimensions() > 1) << "Can't ask for the top of a zero- or one-dimensional image\n";
    return dim(1).min();
}

Expr OutputImageParam::bottom() const {
    user_assert(dimensions() > 1) << "Can't ask for the bottom of a zero- or one-dimensional image\n";
    return dim(1).max();
}

Expr OutputImageParam::width() const {
    user_assert(dimensions() > 0) << "Can't ask for the width of a zero-dimensional image\n";
    return dim(0).extent();
}

Expr OutputImageParam::height() const {
    user_assert(dimensions() > 1) << "Can't ask for the height of a zero or one-dimensional image\n";
    return dim(1).extent();
}

Expr OutputImageParam::channels() const {
    user_assert(dimensions() > 2) << "Can't ask for the channels of an image with fewer than three dimensions\n";
    return dim(2).extent();
}

Internal::Parameter OutputImageParam::parameter() const {
    return param;
}

OutputImageParam::operator Argument() const {
    return Argument(name(), kind, type(), dimensions());
}

OutputImageParam::operator ExternFuncArgument() const {
    return param;
}

}
