#include "Lambda.h"

#include <string>
#include <type_traits>

#include "Util.h"

namespace Halide {

Func lambda(const Expr &e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(_) = e;
    return f;
}

Func lambda(const Var &x, const Expr &e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x) = e;
    return f;
}

Func lambda(const Var &x, const Var &y, const Expr &e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x, y) = e;
    return f;
}

Func lambda(const Var &x, const Var &y, const Var &z, const Expr &e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x, y, z) = e;
    return f;
}

Func lambda(const Var &x, const Var &y, const Var &z, const Var &w, const Expr &e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x, y, z, w) = e;
    return f;
}

Func lambda(const Var &x, const Var &y, const Var &z, const Var &w, const Var &v, const Expr &e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x, y, z, w, v) = e;
    return f;
}

}  // namespace Halide
