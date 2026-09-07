#include "Halide.h"

#include <iostream>

using namespace Halide;
using namespace Halide::Internal;

namespace {

using IRMatcher::Intrin;
using IRMatcher::pattern_arg;

template<typename A, typename B>
auto make_struct(A &&a, B &&b) noexcept -> Intrin<Call::make_struct, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B, typename C>
auto load_typed_struct_member(A &&a, B &&b, C &&c) noexcept -> Intrin<Call::load_typed_struct_member, decltype(pattern_arg(a)), decltype(pattern_arg(b)), decltype(pattern_arg(c))> {
    return {pattern_arg(a), pattern_arg(b), pattern_arg(c)};
}

}  // namespace

struct Foo;
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(Foo);

int main(int argc, char **argv) {
    const Type foo_ptr = type_of<Foo *>();

    // A struct that stores a void* in its first field.
    Expr stored0 = Variable::make(Handle(), "raw_ptr");
    Expr stored1 = Variable::make(Int(32), "other");
    Expr inst = Call::make(Handle(), Call::make_struct, {stored0, stored1}, Call::PureIntrinsic);
    Expr proto = Variable::make(Handle(), "closure_prototype");

    // Load it back as a Foo*: load_typed_struct_member's job is to impose the
    // member's declared type on the untyped slot.
    Expr op = Call::make(foo_ptr, Call::load_typed_struct_member, {inst, proto, 0}, Call::Intrinsic);

    IRMatcher::Wild<0> v0;
    IRMatcher::Wild<1> v1;
    IRMatcher::Wild<2> pr;

    // Textbook SROA rule instance.
    auto rewrite = IRMatcher::rewriter(op, op.type());
    bool matched = rewrite(load_typed_struct_member(make_struct(v0, v1), pr, 0),
                           IRMatcher::cast(op.type(), v0));

    if (!matched) {
        std::cout << "Rewrite pattern failed to match.\n";
        return 1;
    }

    const Type result_type = rewrite.result.type();
    if (!foo_ptr.same_handle_type(result_type)) {
        std::cout << "Initial: " << op << "\n"
                  << "Result : " << rewrite.result << "\n"
                  << "Type information was lost!\n"
                  << "  expected: " << foo_ptr << "\n"
                  << "  got:      " << result_type << "\n";
        return 1;
    }

    std::cout << "Success!\n";
    return 0;
}
