#include "ImageParam.h"

namespace Halide {

ImageParam::ImageParam(Type t, int d)
        : OutputImageParam(Internal::Parameter(t, true, d, Internal::make_entity_name(this, "Halide::ImageParam", 'p')), Argument::InputBuffer)
        , func(name() + "_im") {
    // Create the Func representation of this ImageParam
    init_func();
}

ImageParam::ImageParam(Type t, int d, const std::string &n)
        : OutputImageParam(Internal::Parameter(t, true, d, n, /* is_explicit_name */ true), Argument::InputBuffer)
        , func(name() + "_im") {
    // Discourage future Funcs from having the same name
    Internal::unique_name(n);
    // Create the Func representation of this ImageParam
    init_func();
}

void ImageParam::init_func() {
    std::vector<Var> args;
    std::vector<Expr> args_expr;
    for (int i = 0; i < dimensions(); ++i) {
        args.push_back(Var::implicit(i));
        args_expr.push_back(Var::implicit(i));
    }
    func(args) = Internal::Call::make(param, args_expr);
}

void ImageParam::set(Buffer<> b) {
    if (b.defined()) {
        user_assert(b.type() == type())
            << "Can't bind ImageParam " << name()
            << " of type " << type()
            << " to Buffer " << b.name()
            << " of type " << Type(b.type()) << "\n";
    }
    param.set_buffer(b);
}

Buffer<> ImageParam::get() const {
    return param.get_buffer();
}

void ImageParam::reset() {
    set(Buffer<>());
}

Expr ImageParam::operator()(std::vector<Expr> args_passed) const {
    return func(args_passed);
}

Expr ImageParam::operator()(std::vector<Var> args_passed) const {
    return func(args_passed);
}

ImageParam::operator Func() const {
    return func;
}

Func ImageParam::in(const Func &f) {
    internal_assert(func.defined());
    return func.in(f);
}

Func ImageParam::in(const std::vector<Func> &fs) {
    internal_assert(func.defined());
    return func.in(fs);
}

Func ImageParam::in() {
    internal_assert(func.defined());
    return func.in();
}

}
