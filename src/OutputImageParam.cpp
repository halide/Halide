#include "OutputImageParam.h"

#include "IROperator.h"
#include <utility>

namespace Halide {

using Internal::Dimension;

OutputImageParam::OutputImageParam(const Parameter &p, Argument::Kind k, Func f)
    : param(p), kind(k), func(std::move(f)) {
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
    return Dimension(param, i, func);
}

Dimension OutputImageParam::dim(int i) const {
    return Dimension(param, i, func);
}

int OutputImageParam::host_alignment() const {
    return param.host_alignment();
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

Parameter OutputImageParam::parameter() const {
    return param;
}

OutputImageParam::operator Argument() const {
    return Argument(name(), kind, type(), dimensions(), param.get_argument_estimates());
}

OutputImageParam::operator ExternFuncArgument() const {
    return param;
}

OutputImageParam &OutputImageParam::set_estimates(const Region &estimates) {
    const int d = dimensions();
    user_assert((int)estimates.size() == d)
        << "ImageParam " << name() << " has " << d << " dimensions, "
        << "but the estimates passed to set_estimates contains " << estimates.size() << " pairs.\n";
    for (int i = 0; i < d; i++) {
        dim(i).set_estimate(estimates[i].min, estimates[i].extent);
    }
    return *this;
}

OutputImageParam &OutputImageParam::store_in(MemoryType type) {
    param.store_in(type);
    return *this;
}

}  // namespace Halide
