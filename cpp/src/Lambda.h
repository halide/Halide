#include "Func.h"
#include "Var.h"
#include "Util.h"

namespace Halide {

Func lambda(Expr e) {
    Func f;
    f() = e;
    return f;
}

Func lambda(Var x, Expr e) {
    Func f;
    f(x) = e;
    return f;
}

Func lambda(Var x, Var y, Expr e) {
    Func f;
    f(x, y) = e;
    return f;
}

Func lambda(Var x, Var y, Var z, Expr e) {
    Func f;
    f(x, y, z) = e;
    return f;
}

Func lambda(Var x, Var y, Var z, Var w, Expr e) {
    Func f;
    f(x, y, z, w) = e;
    return f;
}

}
