#include "Func.h"
#include "Var.h"
#include "Util.h"

namespace Halide {

class Lambda {
    std::vector<Var> vars;
public:
    Lambda() {}
    Lambda(Var x) : vars(Internal::vec(x)) {}
    Lambda(Var x, Var y) : vars(Internal::vec(x, y)) {}
    Lambda(Var x, Var y, Var z) : vars(Internal::vec(x, y, z)) {}
    Lambda(Var x, Var y, Var z, Var w) : vars(Internal::vec(x, y, z, w)) {}

    Func operator=(Expr e) {
        Func f;
        f(vars) = e;
        return f;
    }
};

}
