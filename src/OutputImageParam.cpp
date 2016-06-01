#include "OutputImageParam.h"


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

Expr OutputImageParam::min(int x) const {
    std::ostringstream s;
    s << name() << ".min." << x;
    return Internal::Variable::make(Int(32), s.str(), param);
}

Expr OutputImageParam::extent(int x) const {
    std::ostringstream s;
    s << name() << ".extent." << x;
    return Internal::Variable::make(Int(32), s.str(), param);
}

Expr OutputImageParam::stride(int x) const {
    std::ostringstream s;
    s << name() << ".stride." << x;
    return Internal::Variable::make(Int(32), s.str(), param);
}
int OutputImageParam::host_alignment() const {
    return param.host_alignment();
}

OutputImageParam &OutputImageParam::set_extent(int dim, Expr extent) {
    param.set_extent_constraint(dim, extent);
    return *this;
}

OutputImageParam &OutputImageParam::set_min(int dim, Expr min) {
    param.set_min_constraint(dim, min);
    return *this;
}

OutputImageParam &OutputImageParam::set_stride(int dim, Expr stride) {
    param.set_stride_constraint(dim, stride);
    return *this;
}

OutputImageParam &OutputImageParam::set_host_alignment(int bytes) {
    param.set_host_alignment(bytes);
    return *this;
}

OutputImageParam &OutputImageParam::set_bounds(int dim, Expr min, Expr extent) {
    return set_min(dim, min).set_extent(dim, extent);
}

int OutputImageParam::dimensions() const {
    return param.dimensions();
}

Expr OutputImageParam::left() const {
    user_assert(dimensions() > 0) << "Can't ask for the left of a zero-dimensional image\n";
    return min(0);
}

Expr OutputImageParam::right() const {
    user_assert(dimensions() > 0) << "Can't ask for the right of a zero-dimensional image\n";
    return Internal::Add::make(min(0), Internal::Sub::make(extent(0), 1));
}

Expr OutputImageParam::top() const {
    user_assert(dimensions() > 1) << "Can't ask for the top of a zero- or one-dimensional image\n";
    return min(1);
}

Expr OutputImageParam::bottom() const {
    user_assert(dimensions() > 1) << "Can't ask for the bottom of a zero- or one-dimensional image\n";
    return Internal::Add::make(min(1), Internal::Sub::make(extent(1), 1));
}

Expr OutputImageParam::width() const {
    user_assert(dimensions() > 0) << "Can't ask for the width of a zero-dimensional image\n";
    return extent(0);
}

Expr OutputImageParam::height() const {
    user_assert(dimensions() > 1) << "Can't ask for the height of a zero or one-dimensional image\n";
    return extent(1);
}

Expr OutputImageParam::channels() const {
    user_assert(dimensions() > 2) << "Can't ask for the channels of an image with fewer than three dimensions\n";
    return extent(2);
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
