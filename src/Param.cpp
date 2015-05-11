#include "Param.h"


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

OutputImageParam::OutputImageParam(const Internal::Parameter &p) :
    param(p) {
}

OutputImageParam::~OutputImageParam() {
}

const std::string &OutputImageParam::name() const {
    return param.name();
}

Type OutputImageParam::type() const {
    return param.type();
}

bool OutputImageParam::defined() {
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

Expr OutputImageParam::max(int x) const {
    return Internal::Add::make(min(x), Internal::Sub::make(extent(x), 1));
}

Expr OutputImageParam::stride(int x) const {
    std::ostringstream s;
    s << name() << ".stride." << x;
    return Internal::Variable::make(Int(32), s.str(), param);
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
    return max(0);
}

Expr OutputImageParam::top() const {
    user_assert(dimensions() > 1) << "Can't ask for the top of a zero- or one-dimensional image\n";
    return min(1);
}

Expr OutputImageParam::bottom() const {
    user_assert(dimensions() > 1) << "Can't ask for the bottom of a zero- or one-dimensional image\n";
    return max(1);
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
    return Argument(name(), Argument::OutputBuffer, type(), dimensions());
}

OutputImageParam::operator ExternFuncArgument() const {
    return param;
}

ImageParam::ImageParam(Type t, int d) :
    OutputImageParam(Internal::Parameter(t, true, d, Internal::make_entity_name(this, "Halide::ImageParam", 'p'))) {}

ImageParam::ImageParam(Type t, int d, const std::string &n) :
    OutputImageParam(Internal::Parameter(t, true, d, n, /* is_explicit_name */ true)) {
    // Discourage future Funcs from having the same name
    Internal::unique_name(n);
}

ImageParam::~ImageParam() {
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

Expr ImageParam::operator()() const {
    user_assert(dimensions() == 0)
        << "Zero-argument access to Buffer " << name()
        << ", which has " << dimensions() << "dimensions.\n";
    std::vector<Expr> args;
    return Internal::Call::make(param, args);
}

Expr ImageParam::operator()(Expr x) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    add_implicit_args_if_placeholder(args, x, 1, &placeholder_seen);

    Internal::check_call_arg_types(name(), &args, dimensions());
    return Internal::Call::make(param, args);
}

Expr ImageParam::operator()(Expr x, Expr y) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    add_implicit_args_if_placeholder(args, x, 2, &placeholder_seen);
    add_implicit_args_if_placeholder(args, y, 2, &placeholder_seen);

    Internal::check_call_arg_types(name(), &args, dimensions());
    return Internal::Call::make(param, args);
}

Expr ImageParam::operator()(Expr x, Expr y, Expr z) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    add_implicit_args_if_placeholder(args, x, 3, &placeholder_seen);
    add_implicit_args_if_placeholder(args, y, 3, &placeholder_seen);
    add_implicit_args_if_placeholder(args, z, 3, &placeholder_seen);

    Internal::check_call_arg_types(name(), &args, dimensions());
    return Internal::Call::make(param, args);
}

Expr ImageParam::operator()(Expr x, Expr y, Expr z, Expr w) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    add_implicit_args_if_placeholder(args, x, 4, &placeholder_seen);
    add_implicit_args_if_placeholder(args, y, 4, &placeholder_seen);
    add_implicit_args_if_placeholder(args, z, 4, &placeholder_seen);
    add_implicit_args_if_placeholder(args, w, 4, &placeholder_seen);

    Internal::check_call_arg_types(name(), &args, dimensions());
    return Internal::Call::make(param, args);
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

Expr ImageParam::operator()(std::vector<Var> args_passed) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    for (size_t i = 0; i < args_passed.size(); i++) {
        add_implicit_args_if_placeholder(args, args_passed[i],
                                         args_passed.size(), &placeholder_seen);
    }

    Internal::check_call_arg_types(name(), &args, dimensions());
    return Internal::Call::make(param, args);
}

ImageParam::operator Argument() const {
    return Argument(name(), Argument::InputBuffer, type(), dimensions());
}

}
