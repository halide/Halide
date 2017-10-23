#include "OutputImageParam.h"
#include "IROperator.h"

namespace Halide {

using Internal::Dimension;

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

Dimension OutputImageParam::dim(int i) {
    return Dimension(param, i);
}

const Dimension OutputImageParam::dim(int i) const {
    return Dimension(param, i);
}

int OutputImageParam::host_alignment() const {
    return param.get_host_alignment();
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
