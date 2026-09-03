#include "Tuple.h"
#include "Debug.h"
#include "Func.h"

namespace Halide {

Tuple::Tuple(const FuncRef &f)
    : exprs(f.size()) {
    user_assert(f.function().has_pure_definition() ||
                f.function().has_extern_definition())
        << "Can't call Func \"" << f.function().name()
        << "\" because it has not yet been defined.\n";

    if (f.size() == 1) {
        exprs[0] = f;
    } else {
        for (size_t i = 0; i < f.size(); i++) {
            exprs[i] = f[i];
        }
    }
}

Tuple::operator Expr() const {
    user_assert(exprs.size() == 1)
        << "Can't treat this Tuple of size " << exprs.size() << " as an Expr:\n"
        << (*this) << "\n"
        << "Only one-element Tuples can be cast to Expr.\n";
    return exprs[0];
}

}  // namespace Halide
