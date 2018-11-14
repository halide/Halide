#include "ImageParam.h"

namespace Halide {

ImageParam::ImageParam(Type t, int d)
        : OutputImageParam(
            Internal::Parameter(t, true, d, Internal::make_entity_name(this, "Halide:.*:ImageParam", 'p')),
            Argument::InputBuffer,
            Func()) {
    // We must call create_func() after the super-ctor has completed.
    func = create_func();
}

ImageParam::ImageParam(Type t, int d, const std::string &n)
        : OutputImageParam(
            Internal::Parameter(t, true, d, n),
            Argument::InputBuffer,
            Func()) {
    // We must call create_func() after the super-ctor has completed.
    func = create_func();
}

Func ImageParam::create_func() const {
    std::vector<Var> args;
    std::vector<Expr> args_expr;
    for (int i = 0; i < dimensions(); ++i) {
        args.push_back(Var::implicit(i));
        args_expr.push_back(Var::implicit(i));
    }
    if (!name().empty()) {
        // Discourage future Funcs from having the same name
        Internal::unique_name(name());
    }
    Func f(name() + "_im");
    f(args) = Internal::Call::make(param, args_expr);
    return f;
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
    return param.buffer();
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

void ImageParam::trace_loads() {
    internal_assert(func.defined());
    func.trace_loads();
}

ImageParam &ImageParam::add_trace_tag(const std::string &trace_tag) {
    internal_assert(func.defined());
    func.add_trace_tag(trace_tag);
    return *this;
}

ImageParam &ImageParam::align_bounds(Var var, Expr modulus, Expr remainder) {
    internal_assert(func.defined());
    func.align_bounds(var, modulus, remainder);
    parameter().set_constraints_from_schedule(func.function());
    return *this;
}

ImageParam &ImageParam::align_storage(Var dim, Expr alignment) {
    internal_assert(func.defined());
    func.align_storage(dim, alignment);
    parameter().set_constraints_from_schedule(func.function());
    return *this;
}

ImageParam &ImageParam::bound(Var var, Expr min, Expr extent) {
    internal_assert(func.defined());
    func.bound(var, min, extent);
    parameter().set_constraints_from_schedule(func.function());
    return *this;
}

ImageParam &ImageParam::bound_extent(Var var, Expr extent) {
    internal_assert(func.defined());
    func.bound_extent(var, extent);
    parameter().set_constraints_from_schedule(func.function());
    return *this;
}

ImageParam &ImageParam::reorder_storage(const std::vector<Var> &dims) {
    internal_assert(func.defined());
    func.reorder_storage(dims);
    parameter().set_constraints_from_schedule(func.function());
    return *this;
}

ImageParam &ImageParam::reorder_storage(Var x, Var y) {
    internal_assert(func.defined());
    func.reorder_storage(x, y);
    parameter().set_constraints_from_schedule(func.function());
    return *this;
}

}  // namespace Halide
