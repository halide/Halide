#include "Param.h"
#include "IROperator.h"

namespace Halide {

void OutputImageParam::add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                                        Expr last_arg,
                                                        int total_args,
                                                        bool *placeholder_seen) const {
    const Internal::Variable *var = last_arg.as<Internal::Variable>();
    bool is_placeholder = var && Var::is_placeholder(var->name);
    if (is_placeholder) {
        user_assert(!(*placeholder_seen))
            << "Only one implicit placeholder ('_') allowed in argument list for ImageParam " << name() << "\n";
        *placeholder_seen = true;

        // The + 1 in the conditional is because one provided argument is an placeholder
        for (int i = 0; i < (dimensions() - total_args + 1); i++) {
            args.push_back(Var::implicit(i));
        }
    } else {
        args.push_back(last_arg);
    }
}

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

ImageParam::ImageParam(Type t, int d) :
    OutputImageParam(Internal::Parameter(t, true, d, Internal::make_entity_name(this, "Halide::ImageParam", 'p')), Argument::InputBuffer) {}

ImageParam::ImageParam(Type t, int d, const std::string &n) :
    OutputImageParam(Internal::Parameter(t, true, d, n, /* is_explicit_name */ true), Argument::InputBuffer) {
    // Discourage future Funcs from having the same name
    Internal::unique_name(n);
}

void ImageParam::set(Buffer b) {
    if (b.defined()) {
        user_assert(b.type() == type())
            << "Can't bind ImageParam " << name()
            << " of type " << type()
            << " to Buffer " << b.name()
            << " of type " << b.type() << "\n";
    }
    param.set_buffer(b);
}

Buffer ImageParam::get() const {
    return param.get_buffer();
}

Expr ImageParam::operator()(std::vector<Expr> args_passed) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    for (size_t i = 0; i < args_passed.size(); i++) {
        add_implicit_args_if_placeholder(args, args_passed[i],
                                         args_passed.size(), &placeholder_seen);
    }

    Internal::check_call_arg_types(name(), &args, dimensions());
    return Internal::Call::make(param, args);
}

Expr ImageParam::operator()(std::vector<Var> vars) const {
    std::vector<Expr> exprs;
    for (Var v : vars) {
        exprs.push_back(v);
    }
    return (*this)(exprs);
}

}
