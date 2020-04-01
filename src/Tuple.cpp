#include "Tuple.h"

#include <ext/alloc_traits.h>
#include <string>

#include "Func.h"
#include "Function.h"

namespace Halide {

Tuple::Tuple(const FuncRef &f)
    : exprs(f.size()) {
    user_assert(f.size() > 1)
        << "Can't construct a Tuple from a call to Func \""
        << f.function().name() << "\" because it does not return a Tuple.\n";
    for (size_t i = 0; i < f.size(); i++) {
        exprs[i] = f[i];
    }
}

}  // namespace Halide
