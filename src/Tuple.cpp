#include "Tuple.h"
#include "Func.h"
#include "Debug.h"

namespace Halide {

Tuple::Tuple(const FuncRefVar &f) : exprs(f.size()) {
    user_assert(f.size() > 1)
        << "Can't construct a Tuple from a call to Func \""
        << f.function().name() << "\" because it does not return a Tuple.\n";
    for (size_t i = 0; i < f.size(); i++) {
        exprs[i] = f[i];
    }
}

Tuple::Tuple(const FuncRefExpr &f) : exprs(f.size()) {
    user_assert(f.size() > 1)
        << "Can't construct a Tuple from a call to Func \""
        << f.function().name() << "\" because it does not return a Tuple.\n";
    for (size_t i = 0; i < f.size(); i++) {
        exprs[i] = f[i];
    }
}

}
