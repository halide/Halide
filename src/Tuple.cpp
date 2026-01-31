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

    user_assert(f.size() > 1)
        << "Can't construct a Tuple from a call to Func \""
        << f.function().name() << "\" because it does not return a Tuple.\n";
    for (size_t i = 0; i < f.size(); i++) {
        exprs[i] = f[i];
    }
}

}  // namespace Halide
